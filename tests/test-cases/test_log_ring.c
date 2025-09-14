#define ULTRA_DEADLY_WARNINGS

#include <kernel-source/log_ring.c>
#include <test_harness.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#define RING_4K 12

static error_t write_one(struct log_ring *ring, const char *msg,
                         bool extend, bool publish)
{
    size_t len, prev_len = 0;
    struct log_ring_reservation reservation;
    error_t ret;

    len = strlen(msg);

    if (extend) {
        ret = log_ring_reserve_extend(ring, len, &reservation, &prev_len);
        if (ret != EOK)
            return ret;
    } else {
        ret = log_ring_reserve(ring, len, SYSLOG_CRIT, &reservation);
        if (ret != EOK)
            return ret;
    }

    memcpy(reservation.reserved_data + prev_len, msg, len);

    if (publish)
        log_ring_publish(&reservation);
    else
        log_ring_commit(&reservation);

    return EOK;
}

static error_t read_one(
    struct log_ring *ring, u64 *seq, char *out_buf, size_t *in_out_size
)
{
    struct log_record record;
    error_t ret;

    ret = log_ring_read(ring, *seq, out_buf, *in_out_size, &record);
    if (ret != EOK)
        return ret;

    if (record.length > *in_out_size)
        return EOVERFLOW;

    *in_out_size = record.length;
    *seq = record.seq_num;
    return EOK;
}

#define TEST_RING(len_shift, msg_shift)              \
    MAKE_LOG_RING(local_ring, len_shift, msg_shift); \
    struct log_ring *ring = &local_ring;             \
    __attribute__((unused)) char read_buf[4096];     \
    __attribute__((unused)) size_t in_out_length;    \
    __attribute__((unused)) u64 seq = 0;

#define READ_ONE_EXPECT_ERRNO(what, msg)                                       \
    do {                                                                       \
        size_t msg_len = (msg) ? strlen(msg ?: "") : 0;                        \
        in_out_length = msg_len;                                               \
        ASSERT_EQ(read_one(ring, &seq, read_buf, &in_out_length), what);       \
        ASSERT_EQ(in_out_length, msg_len);                                     \
        if (msg)                                                               \
            ASSERT_EQ(memcmp((msg ?: ""), read_buf, in_out_length), 0);        \
        if ((what) == EOK)                                                     \
            seq++ ;                                                            \
    } while (0)

#define READ_ONE_EXPECT(msg) READ_ONE_EXPECT_ERRNO(EOK, msg)
#define READ_ONE_EXPECT_EINVAL() READ_ONE_EXPECT_ERRNO(EINVAL, NULL)

#define DO_WRITE_ONE_EXPECT(msg, ret, extend, publish) \
    ASSERT_EQ(write_one(ring, msg, extend, publish), ret)

#define WRITE_ONE_EXPECT(msg, ret) DO_WRITE_ONE_EXPECT(msg, ret, false, true)
#define WRITE_ONE_EXPECT_SUCCESS(msg) WRITE_ONE_EXPECT(msg, EOK)

TEST_CASE(cant_read_empty)
{
    TEST_RING(RING_4K, 5);

    READ_ONE_EXPECT_EINVAL();
    ASSERT_EQ(log_ring_first_readable_sequence_number(ring), 0);
    ASSERT_EQ(log_ring_next_assigned_sequence_number(ring), 0);

    WRITE_ONE_EXPECT_SUCCESS("hello");
    ASSERT_EQ(log_ring_first_readable_sequence_number(ring), 0);
    ASSERT_EQ(log_ring_next_assigned_sequence_number(ring), 1);

    WRITE_ONE_EXPECT_SUCCESS("world");
    ASSERT_EQ(log_ring_first_readable_sequence_number(ring), 0);
    ASSERT_EQ(log_ring_next_assigned_sequence_number(ring), 2);

    READ_ONE_EXPECT("hello");
    READ_ONE_EXPECT("world");
    READ_ONE_EXPECT_EINVAL();
}

TEST_CASE(data_wrap)
{
    // Ring of 32 bytes with 16 byte messages
    TEST_RING(5, 4);

    WRITE_ONE_EXPECT_SUCCESS("foo");
    WRITE_ONE_EXPECT_SUCCESS("bar");
    ASSERT_EQ(log_ring_first_readable_sequence_number(ring), 0);

    READ_ONE_EXPECT("foo");

    // Ring wrap happens here, "foo" is now gone
    WRITE_ONE_EXPECT_SUCCESS("baz");
    ASSERT_EQ(log_ring_first_readable_sequence_number(ring), 1);

    seq = 0;
    READ_ONE_EXPECT("bar");
    // seq 0 was foo, seq 1 is bar, next is 2
    ASSERT_EQ(seq, 2);

    READ_ONE_EXPECT("baz");
    READ_ONE_EXPECT_EINVAL();

    WRITE_ONE_EXPECT_SUCCESS("12345678");
    ASSERT_EQ(log_ring_first_readable_sequence_number(ring), 2);
    READ_ONE_EXPECT("12345678");
    READ_ONE_EXPECT_EINVAL();

    /*
     * This message is 9 bytes, 8 bytes for metadata => 17 bytes
     * Aligned up for metadata is 24 bytes, which is greater than half of the
     * ring, so we expect to get ENOSPC.
     */
    WRITE_ONE_EXPECT("123456789", ENOSPC);

    WRITE_ONE_EXPECT_SUCCESS("xxx");
    ASSERT_EQ(log_ring_first_readable_sequence_number(ring), 3);
    READ_ONE_EXPECT("xxx");
    WRITE_ONE_EXPECT_SUCCESS("yyy");
    ASSERT_EQ(log_ring_first_readable_sequence_number(ring), 4);
    READ_ONE_EXPECT("yyy");
    READ_ONE_EXPECT_EINVAL();
}

TEST_CASE(extend_record)
{
    TEST_RING(RING_4K, 5);

    DO_WRITE_ONE_EXPECT("hel", EOK, false, false);
    DO_WRITE_ONE_EXPECT("lo world", EOK, true, false);

    // Not published yet
    READ_ONE_EXPECT_EINVAL();

    // Finally publish with a '!'
    DO_WRITE_ONE_EXPECT("!", EOK, true, true);
    READ_ONE_EXPECT("hello world!");

    // Shouldn't be able to extend anymore
    DO_WRITE_ONE_EXPECT("test", EBUSY, true, false);

    DO_WRITE_ONE_EXPECT("committed but not published", EOK, false, false);
    READ_ONE_EXPECT_EINVAL();

    // A new record should automatically publish the previous one
    DO_WRITE_ONE_EXPECT("published previous", EOK, false, true);

    // Both should be readable now
    READ_ONE_EXPECT("committed but not published");
    READ_ONE_EXPECT("published previous");
    READ_ONE_EXPECT_EINVAL();
}

#define MSG0 "00000000"
#define MSG1 "11111111"
#define MSG2 "22222222"

BUILD_BUG_ON((sizeof(MSG0) - 1) != 8 ||
             (sizeof(MSG1) - 1) != 8 ||
             (sizeof(MSG2) - 1) != 8);

TEST_CASE(extend_record_cross_generation)
{
    // 64-byte ring
    TEST_RING(6, 4);

    // Fill with 48 bytes (3 of 8 (+ 8 metadata) byte messages)
    WRITE_ONE_EXPECT_SUCCESS(MSG0);
    WRITE_ONE_EXPECT_SUCCESS(MSG1);
    WRITE_ONE_EXPECT_SUCCESS(MSG2);

    #define MSG3_P0 "DEADBEEF"
    #define MSG3_P1 "CAFEBABE"

    /*
     * Write 16 more bytes (8 bytes of message (+ 8 metadata)). The ring has
     * still not wrapped at this point.
     */
    DO_WRITE_ONE_EXPECT(MSG3_P0, EOK, false, false);

    // Verify the ring has not wrapped yet
    READ_ONE_EXPECT(MSG0);
    READ_ONE_EXPECT(MSG1);
    READ_ONE_EXPECT(MSG2);
    READ_ONE_EXPECT_EINVAL();

    // Reset seq again
    seq = 0;

    /*
     * Write the second part of the message above. This record is now too large
     * to exist on the previous generation, so we push the tail and invalidate
     * MSG0 & MSG1. The data that's stored at the head is now:
     *     [id, <msg3 data>] [id, <msg2 data>]
     *               HEAD ---^
     *                       ^--- TAIL
     */
    DO_WRITE_ONE_EXPECT(MSG3_P1, EOK, true, true);

    READ_ONE_EXPECT(MSG2);
    READ_ONE_EXPECT(MSG3_P0 MSG3_P1);

    #undef MSG3_P0
    #undef MSG3_P1
}

TEST_CASE(extend_record_same_generation_with_wrap)
{
    // 64-byte ring
    TEST_RING(6, 4);

    // Fill with 48 bytes (3 of 8 (+ 8 metadata) byte messages)
    WRITE_ONE_EXPECT_SUCCESS(MSG0);
    WRITE_ONE_EXPECT_SUCCESS(MSG1);
    WRITE_ONE_EXPECT_SUCCESS(MSG2);

    #define MSG3_P0 "012345678912345"
    #define MSG3_P1 "ABCDEFGHI"
    BUILD_BUG_ON((sizeof(MSG3_P0) - 1) != 15 ||
                 (sizeof(MSG3_P1) - 1) != 9);

    /*
     * Commit a message but don't publish, this one wraps around because
     * 15 + (8 metadata) => 23 => ALIGN_UP(23, 8) => 24
     * And we only have 16 bytes left at the end of the ring.
     *
     * 24 bytes are pushed to the beginning of the ring, thus invalidating
     * MSG0 and MSG1. MSG2 should still be there.
     */
    DO_WRITE_ONE_EXPECT(MSG3_P0, EOK, false, false);

    READ_ONE_EXPECT(MSG2);
    READ_ONE_EXPECT_EINVAL();

    /*
     * Add 9 more bytes to the previous message, the math now looks as follows:
     * 15 + 9 + (8 metadata) => 32. This covers the entire half of the ring, but
     * still doesn't cause MSG2 to be invalidated.
     */
    DO_WRITE_ONE_EXPECT(MSG3_P1, EOK, true, true);
    seq = 0;

    READ_ONE_EXPECT(MSG2);
    READ_ONE_EXPECT(MSG3_P0 MSG3_P1);
    READ_ONE_EXPECT_EINVAL();

    #undef MSG3_P0
    #undef MSG3_P1
}

#define NUM_READER_THREADS 4
#define NUM_WRITER_THREADS 4
#define NUM_THREADS (NUM_READER_THREADS + NUM_WRITER_THREADS)
BUILD_BUG_ON(NUM_READER_THREADS != NUM_WRITER_THREADS);

#define NMI_SIGNAL SIGUSR1
#define NMI_DELAY_US 100

static __thread void *worker_ctx;
static __thread bool is_reader;

struct writer_thread_context {
    struct log_ring *ring;
    unsigned int seed;
    u32 id;
    u64 num_posted, num_posted_from_nmi;
    u64 num_failed;
    u64 longest_reservation;
} g_writer_contexts[NUM_WRITER_THREADS];

struct reader_thread_context {
    struct log_ring *ring;
    u64 num_read, num_read_from_nmi;
    u64 num_dropped;
    u64 seq_num;
    u64 nmi_seq_num;
} g_reader_contexts[NUM_READER_THREADS];

static bool g_should_stop = false;
static u32 g_num_alive_threads = 0;

#define MAX_RESERVATION_SIZE 120

struct log_ring_message {
    u64 expected_seq_num;
    u64 expected_thread_id;
    u64 expected_length;
    u8 fill_base;

#define MAX_RANDOM_FILL_SIZE \
    (MAX_RESERVATION_SIZE - sizeof(struct log_ring_message))
    u8 fill[];
};

static void do_writer_work(struct writer_thread_context *ctx, bool is_nmi)
{
    struct log_ring *ring = ctx->ring;
    struct log_ring_message *msg;
    struct log_ring_reservation reservation;
    size_t fill_size, full_length, i;
    error_t ret;
    u64 start, total;

    fill_size = rand_r(&ctx->seed) % MAX_RANDOM_FILL_SIZE;
    full_length = fill_size + sizeof(struct log_ring_message);

    start = ns_timer();
    ret = log_ring_reserve(ring, full_length, ctx->id >> 5, &reservation);
    if (ret != EOK) {
        ctx->num_failed++;
        ASSERT_EQ(ret, ENOSPC);
        return;
    }

    msg = (struct log_ring_message*)reservation.reserved_data;
    msg->expected_length = sizeof(struct log_ring_message) + fill_size;
    msg->expected_thread_id = ctx->id;
    msg->expected_seq_num = log_ring_reservation_sequence_number(&reservation);
    log_ring_reservation_set_facility(&reservation, ctx->id & 0b11111);

    msg->fill_base = rand_r(&ctx->seed) & 0xFF;
    for (i = 0; i < fill_size; i++)
        msg->fill[i] = msg->fill_base + i;

    log_ring_publish(&reservation);
    total = ns_timer() - start;

    if (total > ctx->longest_reservation)
        ctx->longest_reservation = total;

    if (is_nmi)
        ctx->num_posted_from_nmi++;
    else
        ctx->num_posted++;
}

static void *writer_thread(void *user)
{
    struct writer_thread_context *ctx = user;
    worker_ctx = ctx;

    ctx->seed = (unsigned int)time(NULL);

    atomic_add_fetch(&g_num_alive_threads, 1, MO_RELAXED);

    while (!atomic_load_relaxed(&g_should_stop))
        do_writer_work(ctx, false);

    return NULL;
}

static void do_reader_work(struct reader_thread_context *ctx, bool is_nmi)
{
    struct log_record record;
    struct log_ring_message *msg;
    char buf[MAX_RESERVATION_SIZE] = { 0 };
    error_t ret;
    size_t i, fill_bytes;
    u32 actual_thread_id;
    struct log_ring *ring = ctx->ring;

    ret = log_ring_read(ring, ctx->seq_num, buf, sizeof(buf), &record);
    if (ret != EOK) {
        ASSERT_EQ(ret, EINVAL);
        return;
    }

    if (is_nmi)
        ctx->num_read_from_nmi++;
    else {
        ctx->num_read++;
        ctx->num_dropped += record.seq_num - ctx->seq_num;
    }

    ASSERT(record.length >= sizeof(struct log_ring_message));
    ASSERT(record.length < MAX_RESERVATION_SIZE);
    msg = (struct log_ring_message*)record.data.mutable_text;

    actual_thread_id = record.facility;
    actual_thread_id |= record.level << 5;
    ASSERT_EQ(msg->expected_thread_id, actual_thread_id);
    ASSERT_EQ(msg->expected_seq_num, record.seq_num);
    ASSERT_EQ(msg->expected_length, record.length);
    ASSERT_EQ(msg->expected_length, record.data.size);

    fill_bytes = record.length - sizeof(struct log_ring_message);
    for (i = 0; i < fill_bytes; i++)
        ASSERT_EQ(msg->fill[i], (u8)(msg->fill_base + i));

    if (is_nmi)
        ctx->nmi_seq_num = record.seq_num + 1;
    else
        ctx->seq_num = record.seq_num + 1;
}

static void *reader_thread(void *user)
{
    worker_ctx = user;
    is_reader = true;

    atomic_add_fetch(&g_num_alive_threads, 1, MO_RELAXED);

    while (!atomic_load_relaxed(&g_should_stop))
        do_reader_work(user, false);

    return NULL;
}

void handle_nmi(int sig)
{
    ASSERT_EQ(sig, NMI_SIGNAL);

    if (is_reader)
        do_reader_work(worker_ctx, true);
    else
        do_writer_work(worker_ctx, true);
}

static void signals_setup(void)
{
    struct sigaction sa;

    static bool setup = false;
    if (setup)
        return;

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = handle_nmi;
    ASSERT_EQ(sigaction(NMI_SIGNAL, &sa, NULL), 0);

    setup = true;
}

static void report_mt_stats(void)
{
#ifdef LOG_RING_TEST_DEBUG
    size_t i;

    printf("Stats:\n");
    for (i = 0; i < NUM_READER_THREADS; i++) {
        printf(
            "Reader %zu: read: %llu, read in NMI: %llu, dropped: %llu\n", i,
            g_reader_contexts[i].num_read,
            g_reader_contexts[i].num_read_from_nmi,
            g_reader_contexts[i].num_dropped
        );
    }
    for (i = 0; i < NUM_WRITER_THREADS; i++) {
        printf(
            "Writer %zu: posted: %llu, posted from NMI: %llu, failed: %llu "
            "(max live reservation: %llums)\n", i,
            g_writer_contexts[i].num_posted,
            g_writer_contexts[i].num_posted_from_nmi,
            g_writer_contexts[i].num_failed,
            g_writer_contexts[i].longest_reservation / 1000 / 1000
        );
    }
#endif
}

#define MULTITHREADED_TEST(name, buf_bits, desc_bits, duration)          \
    TEST_CASE(CONCAT(multithreaded_, name))                              \
    {                                                                    \
        size_t i;                                                        \
        time_t start;                                                    \
        int ret;                                                         \
                                                                         \
        TEST_RING(buf_bits, desc_bits);                                  \
                                                                         \
        pthread_t writer_threads[NUM_WRITER_THREADS];                    \
        pthread_t reader_threads[NUM_READER_THREADS];                    \
                                                                         \
        g_should_stop = false;                                           \
        g_num_alive_threads = 0;                                         \
        memzero(&g_reader_contexts, sizeof(g_reader_contexts));          \
        memzero(&g_writer_contexts, sizeof(g_writer_contexts));          \
                                                                         \
        signals_setup();                                                 \
                                                                         \
        for (i = 0; i < NUM_THREADS; i++) {                              \
            size_t idx = i / 2;                                          \
                                                                         \
            if (i & 1) {                                                 \
                g_reader_contexts[idx].ring = ring;                      \
                ret = pthread_create(                                    \
                    &reader_threads[idx], NULL, reader_thread,           \
                    &g_reader_contexts[idx]                              \
                );                                                       \
                ASSERT_EQ(ret, 0);                                       \
            } else {                                                     \
                g_writer_contexts[idx].ring = ring;                      \
                g_writer_contexts[idx].id = idx;                         \
                                                                         \
                ret = pthread_create(                                    \
                    &writer_threads[idx], NULL, writer_thread,           \
                    &g_writer_contexts[idx]                              \
                );                                                       \
                ASSERT_EQ(ret, 0);                                       \
            }                                                            \
        }                                                                \
                                                                         \
        while (atomic_load_relaxed(&g_num_alive_threads) != NUM_THREADS) \
            usleep(500);                                                 \
                                                                         \
        start = time(NULL);                                              \
        srand(start);                                                    \
                                                                         \
        while ((time(NULL) - start) < duration) {                        \
            int thread_idx;                                              \
                                                                         \
            thread_idx = rand() % NUM_READER_THREADS;                    \
            pthread_kill(reader_threads[thread_idx], NMI_SIGNAL);        \
                                                                         \
            thread_idx = rand() % NUM_WRITER_THREADS;                    \
            pthread_kill(writer_threads[thread_idx], NMI_SIGNAL);        \
                                                                         \
            usleep(NMI_DELAY_US);                                        \
        }                                                                \
        atomic_store_relaxed(&g_should_stop, true);                      \
                                                                         \
        for (i = 0; i < NUM_THREADS; i++) {                              \
            if (i & 1)                                                   \
                pthread_join(writer_threads[i / 2], NULL);               \
            else                                                         \
                pthread_join(reader_threads[i / 2], NULL);               \
        }                                                                \
                                                                         \
        report_mt_stats();                                               \
    }


// 512k of text, 4096 descriptors
MULTITHREADED_TEST(large_ring, 19, 7, 5)

// 1k of text, 8 descriptors
MULTITHREADED_TEST(small_ring, 10, 7, 5)

// 32k of text, but only 8 descriptors
MULTITHREADED_TEST(descriptor_contention, 15, 11, 2)

// Only 8k of text, but 2048 descriptors
MULTITHREADED_TEST(text_contention, 13, 2, 2)
