#pragma once

#include <common/types.h>

struct log_data_record {
    u64 id;
    char data[];
};

struct log_data_position {
    /*
     * Indices of the [start, end) position of this descriptor's log_data_record,
     * aligned to sizeof(u64).
     */
    u64 begin, end;
};

struct log_data_ring {
    u32 size_shift;
    char *records;
    u64 head, tail;
};

struct log_descriptor {
    /*
     * The control value contains both the id and the state of this descriptor
     * to allow for concurrent modification of both with one atomic operation.
     *
     * The id is a monotonically increasing value to allow for arbitrary writer
     * preemption, for example from an NMI context.
     *
     * The layout looks like the following:
     *     ---------------------------
     *     | BIT  | 63 62 61 | 60..0 |
     *     +------+----------+-------+
     *     | TYPE |  state   |  id   |
     *     ---------------------------
     */
    u64 control;

    struct log_data_position position;
};

struct log_info_record {
    u64 seq_num;
    u64 timestamp_ns;

    /*
     * Number of bytes that contain log data, might be less than the number of
     * actually reserved bytes due to various reasons.
     */
    u32 resident_length;

    u8 facility;
    u8 level;
};

struct log_ring_reservation_details {
    struct log_ring *ring;
    irq_state_t irq_state;
    u64 id;
};

struct log_descriptor_ring {
    u32 count_shift;
    struct log_descriptor *descriptors;
    struct log_info_record *info_records;
    u64 head_id, tail_id;
    u64 last_published_seq_num;
};

struct log_ring {
    struct log_descriptor_ring descriptor_ring;
    struct log_data_ring data_ring;
};

enum descriptor_state {
    DESCRIPTOR_STATE_RESERVED  = 0b00,
    DESCRIPTOR_STATE_FREE      = 0b01,
    DESCRIPTOR_STATE_COMMITTED = 0b10,
    DESCRIPTOR_STATE_PUBLISHED = 0b11,

// Maximum state value is 0b11 (2 bits)
#define DESC_STATE_NUM_BITS ((u64)2)

    /*
     * This synthetic state means the provided id did not match the id stored
     * at the descriptor control value, therefore the actual state is not
     * relevant to the caller. Either a lost race or a non-existent id value.
     */
    DESCRIPTOR_STATE_LOST = -1,
};

#define DATA_SIZE(size_shift) (((u64)1) << (size_shift))

#define DESC_COUNT(count_shift) (((u64)1) << (count_shift))
#define DESC_CONTROL_NUM_BITS (sizeof(u64) * CHAR_BIT)
#define DESC_ID_NUM_BITS (DESC_CONTROL_NUM_BITS - DESC_STATE_NUM_BITS)
#define DESC_STATE_SHIFT DESC_ID_NUM_BITS

#define DESC_STATE_MASK \
    ((((u64)1 << DESC_STATE_NUM_BITS) - 1) << DESC_STATE_SHIFT)

#define DESC_ID(ctrl) ((ctrl) & ~DESC_STATE_MASK)
#define DESC_STATE(ctrl) ((ctrl) >> DESC_STATE_SHIFT)

#define MAKE_DESC_CONTROL(state, id) \
    (((u64)(state) << DESC_STATE_SHIFT) | (id))

/*
 * The first id is picked such that the initial tail makes sense. We want the
 * first pushed record to start at the beginning of the descriptor ring, but the
 * zeroth record, which exists initially is logically at the end of the
 * descriptor ring, so we pretend that the ring is already almost full and is
 * about to perform the first wrap around by giving it the id of count - 1.
 */
#define DESC0_ID(desc_shift) (DESC_COUNT(desc_shift) - 1)

/*
 * The 0th descriptor sequence number is picked such that it overflows back to
 * zero when the first record is allocated. This allows us to start records at
 * sequence number 0, which is convenient and makes all math simpler.
 */
#define DESC0_SEQ_NUM(desc_shift) (-DESC_COUNT(desc_shift))

#define DESC0_CONTROL(desc_shift) \
    MAKE_DESC_CONTROL(DESCRIPTOR_STATE_FREE, DESC0_ID(desc_shift))

#define DATA_POSITION_NOT_PRESENT_BIT 0b01
#define DATA_POSITION_OOM_BIT         0b10

#define MAKE_LOG_RING_INTERNAL(name, text_shift, desc_shift) \
    static ALIGN(struct log_data_record)                     \
    char name##_data[DATA_SIZE(text_shift)];                 \
                                                             \
    static struct log_descriptor                             \
    name##_descriptors[DESC_COUNT(desc_shift)] = {           \
        [DESC_COUNT(desc_shift) - 1] = {                     \
            .control = DESC0_CONTROL(desc_shift),            \
            .position = {                                    \
                .begin = DATA_POSITION_NOT_PRESENT_BIT,      \
                .end = DATA_POSITION_NOT_PRESENT_BIT,        \
            },                                               \
        },                                                   \
    };                                                       \
                                                             \
    static struct log_info_record                            \
    name##_info_records[DESC_COUNT(desc_shift)] = {          \
        [0] = { .seq_num = DESC0_SEQ_NUM(desc_shift), }      \
    };                                                       \
                                                             \
    static struct log_ring name = {                          \
        .descriptor_ring = {                                 \
            .count_shift = (desc_shift),                     \
            .head_id = DESC0_ID(desc_shift),                 \
            .tail_id = DESC0_ID(desc_shift),                 \
            .descriptors = name##_descriptors,               \
            .info_records = name##_info_records,             \
        },                                                   \
        .data_ring = {                                       \
            .size_shift = (text_shift),                      \
            .records = name##_data,                          \
        },                                                   \
    }
