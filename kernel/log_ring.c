#define MSG_FMT(msg) "log-ring: " msg

#include <common/atomic.h>
#include <common/align.h>
#include <common/minmax.h>
#include <common/helpers.h>
#include <common/types.h>

#include <log_ring.h>
#include <irq_helpers.h>

BUILD_BUG_ON_WITH_MSG(
    sizeof(reg_t) < sizeof(u64),
    "Log ring assumes at least a 64-bit architecture for fast atomics"
);

#define DESC_RING_SIZE(ring) DESC_COUNT((ring)->count_shift)
#define DESC_ID_TO_IDX(id, ring) ((id) & (DESC_RING_SIZE(ring) - 1))
#define DESC_PREVIOUS_ID(id, ring) DESC_ID((id) - DESC_RING_SIZE(ring))

#define DATA_RING_SIZE(ring) DATA_SIZE((ring)->size_shift)
#define DATA_RING_SIZE_MASK(ring) (DATA_RING_SIZE(ring) - 1)
#define DATA_RING_GENERATION(ring, pos) ((pos) >> ring->size_shift)
#define DATA_RING_START_OF_GENERATION(ring, pos) \
    ((pos) & ~DATA_RING_SIZE_MASK(ring))
#define DATA_POSITION_TO_IDX(pos, ring) ((pos) & DATA_RING_SIZE_MASK(ring))

#define DATA_POSITION_NOT_PRESENT(x) ((x) & DATA_POSITION_NOT_PRESENT_BIT)
#define DATA_POSITION_OOM(x)         ((x) & DATA_POSITION_OOM_BIT)

static bool data_position_not_present(struct log_data_position *pos)
{
    return DATA_POSITION_NOT_PRESENT(pos->begin) ||
           DATA_POSITION_NOT_PRESENT(pos->end);
}

static bool data_position_present(struct log_data_position *pos)
{
    return !data_position_not_present(pos);
}

static bool data_position_oom(struct log_data_position *pos)
{
    return DATA_POSITION_OOM(pos->begin) || DATA_POSITION_OOM(pos->end);
}

static void make_empty_data_position(struct log_data_position *pos)
{
    pos->begin = DATA_POSITION_NOT_PRESENT_BIT;
    pos->end = DATA_POSITION_NOT_PRESENT_BIT;
}

static void make_oom_data_position(struct log_data_position *pos)
{
    make_empty_data_position(pos);
    pos->begin |= DATA_POSITION_OOM_BIT;
    pos->end |= DATA_POSITION_OOM_BIT;
}

static bool fit_same_data_position_generation(
    struct log_data_ring *data_ring, u64 begin, u64 end
)
{
    return DATA_RING_GENERATION(data_ring, begin) ==
           DATA_RING_GENERATION(data_ring, end - 1);
}

static struct log_descriptor *log_descriptor_from_id(
    struct log_descriptor_ring *ring, u64 id
)
{
    return &ring->descriptors[DESC_ID_TO_IDX(id, ring)];
}

static struct log_info_record *log_info_record_from_id(
    struct log_descriptor_ring *ring, u64 id
)
{
    return &ring->info_records[DESC_ID_TO_IDX(id, ring)];
}

static struct log_data_record *log_data_record_from_position(
    struct log_data_ring *ring, u64 position
)
{
    return (struct log_data_record*)&ring->records[
        DATA_POSITION_TO_IDX(position, ring)
    ];
}

static struct log_info_record *log_info_record_from_reservation(
    struct log_ring_reservation *res
)
{
    struct log_ring *ring = res->details.ring;
    return log_info_record_from_id(&ring->descriptor_ring, res->details.id);
}

static enum descriptor_state log_descriptor_state(u64 control, u64 id)
{
    /*
     * Don't let the caller know the real state since the provided id does not
     * match one stored in control. There could be many reasons why the id
     * doesn't match, most common one being losing a race to a different CPU.
     */
    if (DESC_ID(control) != id)
        return DESCRIPTOR_STATE_LOST;

    return DESC_STATE(control);
}

static enum descriptor_state log_descriptor_acquire(
    struct log_descriptor_ring *desc_ring, u64 id,
    struct log_descriptor *out_desc, u64 *out_seq
)
{
    struct log_descriptor *desc;
    struct log_info_record *info;
    enum descriptor_state state;
    u64 control;

    desc = log_descriptor_from_id(desc_ring, id);
    control = atomic_load_acquire(&desc->control);

    state = log_descriptor_state(control, id);
    if (state == DESCRIPTOR_STATE_LOST || state == DESCRIPTOR_STATE_RESERVED)
        goto out;

    /*
     * All loads below are relaxed, we don't care in which order they get
     * loaded with respect to each other.
     */
    if (out_desc) {
        out_desc->position.begin = atomic_load_relaxed(&desc->position.begin);
        out_desc->position.end = atomic_load_relaxed(&desc->position.end);
    }

    info = log_info_record_from_id(desc_ring, id);
    if (out_seq)
        *out_seq = atomic_load_relaxed(&info->seq_num);

    /*
     * Reload the state to make sure whatever we read above is still valid.
     *
     * Use the acquire barrier to make sure the loads above finish before we
     * reload the descriptor state.
     */
    barrier_acquire();

    control = atomic_load_relaxed(&desc->control);
    state = log_descriptor_state(control, id);

out:
    if (out_desc)
        out_desc->control = control;
    return state;
}

static error_t log_descriptor_acquire_for_reading(
    struct log_descriptor_ring *desc_ring, u64 id, u64 expected_seq,
    struct log_descriptor *out_desc
)
{
    enum descriptor_state state;
    u64 actual_seq = expected_seq;

    state = log_descriptor_acquire(desc_ring, id, out_desc, &actual_seq);
    if (expected_seq != actual_seq)
        return EINVAL;

    switch (state) {
    case DESCRIPTOR_STATE_PUBLISHED:
        if (data_position_present(&out_desc->position))
            return EOK;
        FALLTHROUGH;
    case DESCRIPTOR_STATE_FREE:
        return ENOENT;
    case DESCRIPTOR_STATE_LOST:
    case DESCRIPTOR_STATE_RESERVED:
    case DESCRIPTOR_STATE_COMMITTED:
    default:
        return EINVAL;
    }
}

static bool log_descriptor_modify_state(
    struct log_descriptor_ring *desc_ring, u64 id,
    enum descriptor_state expected, enum descriptor_state desired
)
{
    u64 expected_control, desired_control;
    struct log_descriptor *desc;

    expected_control = MAKE_DESC_CONTROL(expected, id);
    desired_control = MAKE_DESC_CONTROL(desired, id);
    desc = log_descriptor_from_id(desc_ring, id);

    return atomic_cmpxchg_acq_rel(
        &desc->control, &expected_control, desired_control
    );
}

static u64 tail_sequence_number(struct log_ring *ring)
{
    struct log_descriptor_ring *desc_ring = &ring->descriptor_ring;
    struct log_descriptor desc;
    enum descriptor_state state;
    u64 id, out_seq;

    do {
        id = atomic_load_acquire(&desc_ring->tail_id);
        state = log_descriptor_acquire(desc_ring, id, &desc, &out_seq);
    } while (state != DESCRIPTOR_STATE_FREE &&
             state != DESCRIPTOR_STATE_PUBLISHED);

    return out_seq;
}

/*
 * The reason we do this is it's impossible to implement a sequence-lock-like
 * structure in C in an undefined-behavior-free way otherwise. Any data race in
 * C is considered UB, even if it's intended.
 */
static void atomic_src_memcpy(void *dest, const void *src, size_t size)
{
    size_t i;
    const u8 *byte_src = src;
    u8 *byte_dest = dest;

    for (i = 0; i < size; i++)
        byte_dest[i] = atomic_load_relaxed(&byte_src[i]);
}

static error_t get_log_data(
    struct log_data_ring *data_ring, struct log_data_position *pos,
    struct string *out_string
)
{
    struct log_data_record *rec;

    if (data_position_not_present(pos)) {
        if (data_position_oom(pos)) {
            *out_string = NULL_STR();
            return ENOENT;
        }

        *out_string = STR_CONSTEXPR("");
        return EOK;
    }

    // Simple case, same generation record, all data is contiguous
    if (fit_same_data_position_generation(data_ring, pos->begin, pos->end)) {
        rec = log_data_record_from_position(data_ring, pos->begin);
        out_string->size = pos->end - pos->begin;
        goto out;
    }

    // This must be a cross-generation record
    WARN_ON_WITH_MSG(!fit_same_data_position_generation(
        data_ring, pos->begin + DATA_RING_SIZE(data_ring), pos->end
    ), "corrupted log record position");

    rec = log_data_record_from_position(data_ring, 0);
    out_string->size = DATA_POSITION_TO_IDX(pos->end, data_ring);

out:
    if (WARN_ON(out_string->size < sizeof(*rec)))
        return EINVAL;

    out_string->size -= sizeof(*rec);
    out_string->mutable_text = rec->data;
    return EOK;
}

static error_t copy_log_data(
    struct log_data_ring *data_ring, struct log_data_position *pos,
    size_t resident_length, struct string *out_string
)
{
    error_t ret;
    struct string data;

    ret = get_log_data(data_ring, pos, &data);
    if (is_error(ret))
        return ret;

    if (data.size < resident_length)
        return EINVAL;

    atomic_src_memcpy(
        out_string->mutable_text, data.text,
        MIN(resident_length, out_string->size)
    );
    return EOK;
}

static error_t log_descriptor_read(
    struct log_ring *ring, u64 seq_num, struct log_record *out_rec
)
{
    struct log_descriptor_ring *desc_ring = &ring->descriptor_ring;
    struct log_info_record *info;
    struct log_descriptor *desc;
    struct log_descriptor local_desc;
    u64 id;
    u32 length;
    error_t ret;

    desc = log_descriptor_from_id(desc_ring, seq_num);
    id = DESC_ID(atomic_load_acquire(&desc->control));

    ret = log_descriptor_acquire_for_reading(
        desc_ring, id, seq_num, &local_desc
    );
    if (ret != EOK || out_rec == NULL)
        return ret;

    info = log_info_record_from_id(desc_ring, id);
    atomic_src_memcpy(out_rec, info, sizeof(*info));

    length = atomic_load_relaxed(&info->resident_length);
    ret = copy_log_data(
        &ring->data_ring, &local_desc.position, length, &out_rec->data
    );
    if (ret != EOK)
        return ret;

    /*
     * We use a barrier because we want to ensure the memcpy loads above are
     * finished before we try to acquire the descriptor the second time.
     */
    barrier_acquire();

    return log_descriptor_acquire_for_reading(
        desc_ring, id, seq_num, &local_desc
    );
}

static error_t do_log_ring_read(
    struct log_ring *ring, u64 *in_out_seq, struct log_record *out_rec
)
{
    u64 tail_seq, seq = *in_out_seq;
    error_t ret;

    for (;;) {
        ret = log_descriptor_read(ring, seq, out_rec);
        if (ret == EOK)
            break;

        tail_seq = tail_sequence_number(ring);

        // Fell behind the tail, just catch up
        if (seq < tail_seq) {
            seq = tail_seq;
            continue;
        }

        /*
         * The descriptor exists, but its data has been invalidated, skip it and
         * try to find the first descriptor which has its data still alive.
         */
        if (ret == ENOENT) {
            seq++;
            continue;
        }

        /*
         * Most likely EINVAL here, no record with this seq has ever existed,
         * we're done.
         */
        WARN_ON(ret != EINVAL);
        break;
    }

    *in_out_seq = seq;
    return ret;
}

static void update_last_published_sequence_number(struct log_ring *ring)
{
    u64 cur_id, old_id, new_id;
    error_t ret;
    struct log_descriptor_ring *desc_ring = &ring->descriptor_ring;

    old_id = atomic_load_acquire(&desc_ring->last_published_seq_num);

    do {
        new_id = cur_id = old_id;

        for (;;) {
            new_id++;

            ret = do_log_ring_read(ring, &new_id, NULL);
            if (ret != EOK)
                break;
            cur_id = new_id;
        }

        if (cur_id == old_id)
            return;
    } while (
        !atomic_cmpxchg_acq_rel(
            &desc_ring->last_published_seq_num, &old_id, cur_id
        ));
}

static void log_descriptor_publish(struct log_ring *ring, u64 id)
{
    struct log_descriptor_ring *desc_ring = &ring->descriptor_ring;
    bool did_modify;

    did_modify = log_descriptor_modify_state(
        desc_ring, id,
        DESCRIPTOR_STATE_COMMITTED, DESCRIPTOR_STATE_PUBLISHED
    );
    if (did_modify)
        update_last_published_sequence_number(ring);
}

static bool log_descriptor_free(
    struct log_descriptor_ring *desc_ring, u64 id
)
{
    return log_descriptor_modify_state(
        desc_ring, id, DESCRIPTOR_STATE_PUBLISHED, DESCRIPTOR_STATE_FREE
    );
}

static bool should_invalidate_next_record(
    struct log_data_ring *data_ring, u64 current_tail, u64 desired_tail
)
{
    u64 bytes_left;

    /*
     * The number of bytes we must invalidate is the difference between the
     * desired and current tail values.
     */
    bytes_left = desired_tail - current_tail;

    // Values match, nothing to do
    if (bytes_left == 0)
        return false;

    /*
     * An overflow means the current tail is already greater than the desired
     * tail, or there simply isn't a previous data generation so there's nothing
     * to invalidate.
     */
    return bytes_left < DATA_RING_SIZE(data_ring);
}

static bool data_invalidate(
    struct log_ring *ring, u64 begin, u64 end, u64 *out_end
)
{
    struct log_descriptor_ring *desc_ring = &ring->descriptor_ring;
    struct log_data_ring *data_ring = &ring->data_ring;
    struct log_data_record *rec;
    struct log_descriptor desc;
    enum descriptor_state state;
    u64 id;

    while (should_invalidate_next_record(data_ring, begin, end)) {
        rec = log_data_record_from_position(data_ring, begin);
        id = atomic_load_acquire(&rec->id);

        state = log_descriptor_acquire(desc_ring, id, &desc, NULL);
        switch (state) {
        case DESCRIPTOR_STATE_PUBLISHED:
        case DESCRIPTOR_STATE_FREE:
            break;
        case DESCRIPTOR_STATE_LOST:
        case DESCRIPTOR_STATE_COMMITTED:
        case DESCRIPTOR_STATE_RESERVED:
        default:
            return false;
        }

        if (atomic_load_acquire(&desc.position.begin) != begin)
            /*
             * The descriptor we have tried to invalidate no longer points at
             * this data block. Two possible cases here:
             * - Data ring corruption
             * - We lost a pretty rare race (either because of an NMI or
             *   otherwise) and this descriptor got invalidated and reused
             *   after we have acquired it but before this check.
             *
             * Either way we need to let the caller know we weren't successful.
             */
            return false;

        if (state == DESCRIPTOR_STATE_PUBLISHED)
            log_descriptor_free(desc_ring, id);

        begin = atomic_load_acquire(&desc.position.end);
    }

    *out_end = begin;
    return true;
}

static error_t data_tail_advance(
    struct log_ring *ring, u64 desired_tail
)
{
    struct log_data_ring *data_ring = &ring->data_ring;
    u64 cur_tail, new_tail;

    if (DATA_POSITION_NOT_PRESENT(desired_tail))
        return EOK;

    cur_tail = atomic_load_acquire(&data_ring->tail);

    while (should_invalidate_next_record(data_ring, cur_tail, desired_tail))  {
        if (!data_invalidate(ring, cur_tail, desired_tail, &new_tail)) {
            /*
             * We failed to invalidate this data. Either the descriptor is still
             * busy, or we lost a race. The way we know for sure is by
             * rechecking the current tail value. If there was any change at all
             * the failure is most likely race-related, so just retry.
             * Otherwise, we're out of luck.
             */
            new_tail = atomic_load_acquire(&data_ring->tail);
            if (new_tail == cur_tail)
                return ENOSPC;

            // Someone else advanced the tail at the same time, just retry
            cur_tail = new_tail;
            continue;
        }

        if (atomic_cmpxchg_acq_rel(&data_ring->tail, &cur_tail, new_tail))
            break;
    }

    return EOK;
}

static error_t descriptor_tail_advance(struct log_ring *ring, u64 tail_id)
{
    struct log_descriptor_ring *desc_ring = &ring->descriptor_ring;
    struct log_descriptor desc;
    enum descriptor_state state;
    u64 new_tail_id;
    error_t ret;

    state = log_descriptor_acquire(desc_ring, tail_id, &desc, NULL);
    switch (state) {
    case DESCRIPTOR_STATE_RESERVED:
    case DESCRIPTOR_STATE_COMMITTED:
        /*
         * Nothing we can do, wrapped too fast and the tail writer is still
         * active. This log will be dropped.
         */
        return ENOSPC;
    case DESCRIPTOR_STATE_LOST:
        /*
         * Someone is deallocating this descriptor at the same time as us,
         * that's fine, let them win and retry in the outer loop.
         */
        return EOK;
    case DESCRIPTOR_STATE_PUBLISHED:
        log_descriptor_free(desc_ring, tail_id);
        break;
    case DESCRIPTOR_STATE_FREE:
        break;
    default:
        WARN_ON(1);
    }

    ret = data_tail_advance(ring, desc.position.end);
    if (is_error(ret))
        return ret;

    new_tail_id = DESC_ID(tail_id + 1);
    state = log_descriptor_acquire(desc_ring, new_tail_id, &desc, NULL);

    // Check the state of the tail here so that we can safely push it below
    if (state != DESCRIPTOR_STATE_PUBLISHED && state != DESCRIPTOR_STATE_FREE) {
        /*
         * The next descriptor in the ring is not FREE/PUBLISHED. We cannot
         * blindly advance the tail to it, since the above is the expectation
         * of tail_sequence_number() and other API. The invariant is that the
         * tail is always either PUBLISHED or FREE.
         *
         * Verify below that the reason the next entry is not in the expected
         * state is because some other CPU has already moved the tail. If it is,
         * we just let it take over, otherwise we're out of luck and cannot let
         * this call succeed since it would cause the invariant to break.
         */
        return atomic_load_acquire(&desc_ring->tail_id) != tail_id ?
            // The tail was simply moved by a different writer, we're fine
            EOK :
            // The next descriptor really is just busy, nothing we can do
            ENOSPC;
    }

    atomic_cmpxchg_acq_rel(&desc_ring->tail_id, &tail_id, new_tail_id);
    return EOK;
}

static error_t log_descriptor_alloc(struct log_ring *ring, u64 *id)
{
    error_t ret;
    u64 head_id, new_id, previous_id;
    u64 control;
    struct log_descriptor_ring *desc_ring = &ring->descriptor_ring;
    struct log_descriptor *desc;
    bool success;

    head_id = atomic_load_acquire(&desc_ring->head_id);

    do {
        new_id = DESC_ID(head_id + 1);
        previous_id = DESC_PREVIOUS_ID(new_id, desc_ring);

        /*
         * We have wrapped back to tail, no free descriptors are available,
         * now we must pop whatever is at the tail.
         */
        if (previous_id == atomic_load_acquire(&desc_ring->tail_id)) {
            ret = descriptor_tail_advance(ring, previous_id);
            if (is_error(ret))
                return ret;
        }
    } while (!atomic_cmpxchg_acq_rel(&desc_ring->head_id, &head_id, new_id));

    desc = log_descriptor_from_id(desc_ring, new_id);
    control = atomic_load_acquire(&desc->control);

    success = atomic_cmpxchg_acq_rel(
        &desc->control, &control,
        MAKE_DESC_CONTROL(DESCRIPTOR_STATE_RESERVED, new_id)
    );
    if (WARN_ON(!success))
        /*
         * There are exactly 2 cases how this can happen:
         * - Broken cmpxchg on the CPU
         * - We got into a series of NMIs for long enough the log ring managed
         *   to wrap around and this descriptor is now allocated by someone else
         *
         * Both are near impossible to hit, this warrants a warning.
         */
        return ENOSPC;

    *id = new_id;
    return EOK;
}

u64 log_ring_reservation_sequence_number(struct log_ring_reservation *res)
{
    /*
     * No atomics needed here, we cannot race against anyone since this record
     * belongs to us & is in a reserved state.
     */
    return log_info_record_from_reservation(res)->seq_num;
}

void log_ring_reservation_set_facility(
    struct log_ring_reservation *res, u8 facility
)
{
    struct log_info_record *info;

    info = log_info_record_from_reservation(res);
    atomic_store_release(&info->facility, facility);
}

error_t log_ring_read(
    struct log_ring *ring, u64 seq_num, char *out_buf, size_t buf_size,
    struct log_record *out_rec
)
{
    error_t ret;

    if (out_rec != NULL)
        out_rec->data = MAKE_STR(out_buf, buf_size);

    ret = do_log_ring_read(ring, &seq_num, out_rec);
    if (ret == EOK && out_rec != NULL)
        out_rec->data.size = MIN(buf_size, out_rec->length);

    return ret;
}

error_t log_ring_readable(struct log_ring *ring, u64 seq_num)
{
    return do_log_ring_read(ring, &seq_num, NULL);
}

u64 log_ring_first_readable_sequence_number(struct log_ring *ring)
{
    struct log_descriptor_ring *desc_ring = &ring->descriptor_ring;
    enum descriptor_state state;
    u64 seq_num, tail_id;

    for (;;) {
        tail_id = atomic_load_acquire(&desc_ring->tail_id);

        state = log_descriptor_acquire(desc_ring, tail_id, NULL, &seq_num);
        if (state == DESCRIPTOR_STATE_PUBLISHED ||
            state == DESCRIPTOR_STATE_FREE)
            return seq_num;
    }
}

u64 log_ring_next_assigned_sequence_number(struct log_ring *ring)
{
    struct log_descriptor_ring *desc_ring = &ring->descriptor_ring;
    struct log_descriptor local_desc, *desc;
    u64 last_published_seq_num, last_published_id, head_id;
    error_t ret;

    for (;;) {
        last_published_seq_num = atomic_load_acquire(
            &desc_ring->last_published_seq_num
        );

        head_id = atomic_load_acquire(&desc_ring->head_id);
        desc = log_descriptor_from_id(desc_ring, last_published_seq_num);

        last_published_id = DESC_ID(atomic_load_acquire(&desc->control));
        ret = log_descriptor_acquire_for_reading(
            desc_ring, last_published_id, last_published_seq_num, &local_desc
        );
        if (ret == EINVAL) {
            if (last_published_seq_num != 0)
                // The record has been overwritten, try again
                continue;

            if (head_id == DESC0_ID(desc_ring->count_shift))
                // No records have been reserved at all
                return 0;

            /*
             * No records have been published yet, but the head has already
             * moved so some are already reserved. Pretend the first published
             * id is the first descriptor id so that the math below works out.
             */
            last_published_id = DESC0_ID(desc_ring->count_shift) + 1;
        }

        return last_published_seq_num + (head_id - last_published_id) + 1;
    }
}

static u64 next_data_position_for(
    struct log_data_ring *data_ring, u64 begin, u64 size
)
{
    u64 end = begin + size;

    /*
     * Simple case, generations match, this allocation fits right next to the
     * previous allocation.
     */
    if (fit_same_data_position_generation(data_ring, begin, end))
        return end;

    /*
     * Otherwise we allocate the entire end of the previous generation, as well
     * as the requested bytes right at the start of the new generation.
     */
    return DATA_RING_START_OF_GENERATION(data_ring, end) + size;
}

static size_t data_record_size(size_t size)
{
    return ALIGN_UP(
        size + sizeof(struct log_data_record),
        ALIGN_OF(struct log_data_record)
    );
}

static char *log_data_alloc(
    struct log_ring *ring, size_t size, struct log_data_position *out_position,
    u64 id
)
{
    struct log_data_ring *data_ring = &ring->data_ring;
    struct log_data_record *rec;
    u64 head, new_head;
    error_t ret;

    if (size == 0) {
        make_empty_data_position(out_position);
        return NULL;
    }
    size = data_record_size(size);

    head = atomic_load_acquire(&data_ring->head);
    do {
        new_head = next_data_position_for(data_ring, head, size);

        /*
         * Rebase the new head to the previous generation by subtracting the
         * ring size and make sure the tail is at least at that offset so that
         * we don't overwrite published records without first freeing them.
         *
         * If there's no previous generation (if the ring hasn't wrapped yet),
         * data_tail_advance will simply be a no-op.
         */
        ret = data_tail_advance(ring, new_head - DATA_RING_SIZE(data_ring));
        if (is_error(ret)) {
            make_oom_data_position(out_position);
            return NULL;
        }
    } while (!atomic_cmpxchg_acq_rel(&data_ring->head, &head, new_head));

    /*
     * All following id & position stores must be release stores, but we don't
     * care about the order they finish with respect to each other, so just
     * use a release barrier here.
     */
    barrier_release();

    rec = log_data_record_from_position(data_ring, head);
    atomic_store_relaxed(&rec->id, id);

    /*
     * Data allocations that cross generation boundaries (aka wrap) store the id
     * at both the end of the previous generation, and the start of the new
     * generation in order to simplify the data iteration code.
     */
    if (!fit_same_data_position_generation(data_ring, head, new_head)) {
        rec = log_data_record_from_position(data_ring, 0);
        atomic_store_relaxed(&rec->id, id);
    }

    atomic_store_relaxed(&out_position->begin, head);
    atomic_store_relaxed(&out_position->end, new_head);
    return rec->data;
}

static char *log_data_alloc_extend(
    struct log_ring *ring, size_t size,
    struct log_data_position *in_out_position, u64 id
)
{
    struct log_data_ring *data_ring = &ring->data_ring;
    struct log_data_record *rec;
    bool was_same_generation, is_same_generation;
    u64 head, new_head;
    error_t ret;

    head = atomic_load_acquire(&data_ring->head);
    if (in_out_position->end != head)
        return NULL;

    was_same_generation = fit_same_data_position_generation(
        data_ring, in_out_position->begin, in_out_position->end
    );

    size = data_record_size(size);
    new_head = next_data_position_for(data_ring, in_out_position->begin, size);

    // We were asked to make the data record smaller, that's just a no-op
    if (unlikely(head >= new_head)) {
        rec = was_same_generation ?
            log_data_record_from_position(data_ring, in_out_position->begin) :
            log_data_record_from_position(data_ring, 0);
        return rec->data;
    }

    ret = data_tail_advance(ring, new_head - DATA_RING_SIZE(data_ring));
    if (is_error(ret))
        return NULL;

    if (!atomic_cmpxchg_acq_rel(&data_ring->head, &head, new_head))
        return NULL;

    is_same_generation = fit_same_data_position_generation(
        data_ring, in_out_position->begin, new_head
    );

    // A wrap has occurred, we must copy the data to the new location
    if (was_same_generation && !is_same_generation) {
        struct log_data_record *old_rec;

        old_rec = log_data_record_from_position(
            data_ring, in_out_position->begin
        );
        rec = log_data_record_from_position(data_ring, 0);
        atomic_store_release(&rec->id, id);

        /*
         * No need for atomic here, the old data is owned by a reserved record,
         * the new data is not owned by anyone yet and cannot be destroyed.
         */
        memcpy(
            rec->data, old_rec->data,
            in_out_position->end - in_out_position->begin
        );
    } else {
        rec = log_data_record_from_position(
            data_ring, was_same_generation ? in_out_position->begin : 0
        );
    }

    atomic_store_release(&in_out_position->end, new_head);
    return rec->data;
}

static bool ring_can_fit(struct log_ring *ring, size_t size)
{
    // Empty records don't use the data ring at all
    if (size == 0)
        return true;

    /*
     * The record must fit in one half of the ring. The reason for this is
     * the ring doesn't really handle wrapping around properly, any data record
     * that has to wrap doesn't actually wrap, but rather starts at the
     * beginning of the data ring, and only leaves a data record header with its
     * ID at the end. This makes it so a large data record may actually occupy
     * more size than the ring has available.
     *
     * Example data ring:
     * 0                  4096
     * | ---- | ---- | ---- |
     * [rec0][rec1][rec2]
     *
     * Now let's attempt to push a record with a size of 4000. Since it doesn't
     * fit at the end, it has to wrap around. The way this is done is:
     * 0                  4096
     * | ---- | ---- | ---- |
     * [rec0][rec1][rec2][ 3] <- no rec3 text here, just the id
     * [       rec3       ]
     *
     * In this case the ring has to invalid rec0, rec1, as well as rec2 to fit
     * rec3 in the beginning. But it actually needs more space than rec0, rec1
     * and rec2 occupy combined, which makes it try to push the tail past the
     * current head, which makes it attempt to invalidate garbage. To avoid
     * needing to handle such complex cases simply limit one data record to
     * half the ring size.
     */
    return data_record_size(size) <= (DATA_RING_SIZE(&ring->data_ring) / 2);
}

static error_t log_data_alloc_for_reservation(
    struct log_ring_reservation *res, size_t length)
{
    struct log_ring *ring = res->details.ring;
    struct log_descriptor *desc;

    desc = log_descriptor_from_id(&ring->descriptor_ring, res->details.id);

    res->reserved_data = log_data_alloc(
        res->details.ring, length, &desc->position, res->details.id
    );
    if (unlikely(res->reserved_data == NULL)) {
        res->resident_length = 0;
        log_ring_commit(res);
        return ENOSPC;
    }

    res->resident_length = length;
    return EOK;
}

error_t log_ring_reserve(
    struct log_ring *ring, size_t length, enum log_level level,
    struct log_ring_reservation *res
)
{
    error_t ret;
    struct log_info_record *info;
    struct log_descriptor_ring *desc_ring = &ring->descriptor_ring;
    u64 seq_num;

    if (!ring_can_fit(ring, length))
        return ENOSPC;

    res->details.irq_state = irq_state_save();
    res->details.ring = ring;

    ret = log_descriptor_alloc(ring, &res->details.id);
    if (is_error(ret)) {
        irq_state_restore(res->details.irq_state);
        return ret;
    }

    info = log_info_record_from_id(desc_ring, res->details.id);

    seq_num = atomic_load_acquire(&info->seq_num);
    if (seq_num == 0 && DESC_ID_TO_IDX(res->details.id, desc_ring) != 0)
        /*
         * The initial sequence number of a record is simply its index in the
         * descriptor array. The only exception is the 0th descriptor.
         */
        seq_num = DESC_ID_TO_IDX(res->details.id, desc_ring);
    else
        /*
         * All subsequent ring generations simply increment their sequence
         * number by array size. Note that the 0th descriptor always enters this
         * branch so that it's not stuck at sequence number 0 (see comment above
         * DESC0_SEQ_NUM).
         */
        seq_num += DESC_RING_SIZE(desc_ring);

    atomic_store_release(
        &info->seq_num,
        seq_num
    );

    /*
     * There may be a previous log record that's committed, but not yet
     * published. It's no longer possible to extend it now that we've allocated
     * a new descriptor. Publish it now so it cannot be extended anymore.
     */
    if (seq_num)
        log_descriptor_publish(ring, DESC_ID(res->details.id - 1));

    ret = log_data_alloc_for_reservation(res, length);
    if (is_error(ret))
        return ret;

    atomic_store_release(&info->level, level);
    atomic_store_release(&info->facility, SYSLOG_FACILITY_KERN);

    return EOK;
}

error_t log_ring_reserve_extend(
    struct log_ring *ring, size_t length, struct log_ring_reservation *res,
    size_t *out_prev_length
)
{
    error_t ret;
    struct log_descriptor *desc;
    struct log_descriptor_ring *desc_ring = &ring->descriptor_ring;
    bool did_reopen;

    res->details.irq_state = irq_state_save();
    res->details.id = atomic_load_acquire(&desc_ring->head_id);
    res->details.ring = ring;

    did_reopen = log_descriptor_modify_state(
        desc_ring, res->details.id,
        DESCRIPTOR_STATE_COMMITTED, DESCRIPTOR_STATE_RESERVED
    );
    if (!did_reopen) {
        irq_state_restore(res->details.irq_state);
        return EBUSY;
    }

    desc = log_descriptor_from_id(desc_ring, res->details.id);
    if (likely(data_position_present(&desc->position))) {
        struct string data;
        struct log_info_record *info;
        size_t extra_bytes_needed;

        ret = get_log_data(&ring->data_ring, &desc->position, &data);
        if (is_error(ret))
            goto out_error;

        info = log_info_record_from_id(desc_ring, res->details.id);
        res->resident_length = info->resident_length;

        extra_bytes_needed = length;
        if (WARN_ON(res->resident_length > data.size)) {
            /*
             * Corrupted record info length indicates that its size is larger
             * that the actual allocation. Truncate here.
             */
            res->resident_length = data.size;
        }

        /*
         * The actual allocated size might be bigger than the resident size,
         * take that into account so we can allocate less data if possible.
         */
        extra_bytes_needed -= data.size - res->resident_length;

        if (!ring_can_fit(ring, data.size + extra_bytes_needed)) {
            ret = ENOSPC;
            goto out_error;
        }

        res->reserved_data = log_data_alloc_extend(
            res->details.ring, data.size + extra_bytes_needed,
            &desc->position, res->details.id
        );
        if (unlikely(res->reserved_data == NULL)) {
            ret = ENOSPC;
            goto out_error;
        }

        if (out_prev_length)
            *out_prev_length = res->resident_length;
        res->resident_length += length;
        return EOK;
    }

    if (!ring_can_fit(ring, length)) {
        ret = ENOSPC;
        goto out_error;
    }

    if (out_prev_length)
        *out_prev_length = 0;
    return log_data_alloc_for_reservation(res, length);

out_error:
    log_ring_commit(res);
    return ret;
}

static bool update_reservation_state(
    struct log_ring_reservation *res, enum descriptor_state target_state
)
{
    struct log_ring *ring = res->details.ring;
    bool success;

    success = log_descriptor_modify_state(
        &ring->descriptor_ring, res->details.id,
        DESCRIPTOR_STATE_RESERVED, target_state
    );
    WARN_ON_WITH_MSG(!success, "likely state corruption, reserved record gone");

    irq_state_restore(res->details.irq_state);
    return success;
}

static void log_info_record_update_from_reservation(
    struct log_ring_reservation *res
)
{
    struct log_ring *ring = res->details.ring;
    struct log_info_record *info;

    info = log_info_record_from_id(&ring->descriptor_ring, res->details.id);
    atomic_store_release(&info->resident_length, res->resident_length);
}

void log_ring_commit(struct log_ring_reservation *res)
{
    struct log_ring *ring = res->details.ring;

    log_info_record_update_from_reservation(res);

    if (unlikely(!update_reservation_state(res, DESCRIPTOR_STATE_COMMITTED)))
        return;

    /*
     * Publish ourselves if there was a race with another writer allocating a
     * new log descriptor and advancing the head pointer.
     */
    if (atomic_load_acquire(&ring->descriptor_ring.head_id) != res->details.id)
        log_descriptor_publish(ring, res->details.id);
}

void log_ring_publish(struct log_ring_reservation *res)
{
    log_info_record_update_from_reservation(res);

    if (unlikely(!update_reservation_state(res, DESCRIPTOR_STATE_PUBLISHED)))
        return;

    update_last_published_sequence_number(res->details.ring);
}
