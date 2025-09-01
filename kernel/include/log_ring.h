#pragma once


#include <common/error.h>
#include <common/attributes.h>
#include <common/string_container.h>

/*
 * Implementation details & internal structures. Must be included here since we
 * only support statically defining the log ring, so the caller must be able to
 * "see" all the internal data structures.
 */
#include <private/log_ring.h>

/*
 * Define a log ring 'name', that has 2^'size_shift' bytes of text space,
 * with an expected average message length of 2^'avg_msg_length_shift' bytes.
 */
#define MAKE_LOG_RING(name, size_shift, avg_msg_length_shift)              \
        BUILD_BUG_ON_WITH_MSG(                                             \
            (size_shift) <= (avg_msg_length_shift),                        \
            "Log ring size cannot be less than or equal to the average "   \
            "message length"                                               \
        );                                                                 \
        BUILD_BUG_ON_WITH_MSG(                                             \
            (size_shift) > 30, "Log ring over a gigabyte in size"          \
        );                                                                 \
        MAKE_LOG_RING_INTERNAL(                                            \
            name, size_shift, (size_shift) - (avg_msg_length_shift)        \
        )

struct log_ring_reservation {
    /*
     * Pointer to the reserved data block inside the log ring, set by the log
     * ring via log_ring_reserve{_extend}.
     */
    char *reserved_data;

    /*
     * The size of the reservation, set by the log ring via
     * log_ring_reserve{_extend}, but may be reduced by the caller in case they
     * wish to store less data that originally reserved. In other words, for the
     * caller this field is simply the number of bytes written to the
     * 'reserved_data' field.
     *
     * Note that for reservations performed via log_ring_reserve_extend this
     * field initially contains the combined length of both reservations.
     */
    size_t resident_length;

    // Private data about this reservation
    struct log_ring_reservation_details details;
};

// Retrieve the sequence number of an existing reservation
u64 log_ring_reservation_sequence_number(struct log_ring_reservation*);

// Set the syslog facility for a reservation
void log_ring_reservation_set_facility(struct log_ring_reservation*, u8);

/*
 * Reserve space for a log record in the ring.
 *
 * On success, the reservation structure is set to point to the reserved data
 * block. See comments above the structure fields for more information.
 *
 * A successful reservation also disables interrupts until a following
 * log_ring_commit/publish.
 *
 * NOTE:
 * Interrupts are disabled to reduce the likelihood of the log ring clogging
 * since a live reservation prevents the tail from advancing when a wrap occurs.
 */
error_t log_ring_reserve(
    struct log_ring*, size_t length, enum log_level level,
    struct log_ring_reservation *out_reservation
);

/*
 * Reserve space for an extra log record by extending the previously commited
 * reservation instead of creating a new one.
 *
 * 'out_prev_length' optionally contains the resident length of the previously
 * stored data for easy concatenation with new data if needed.
 *
 * NOTE:
 * This is only viable for very early initialization stages before SMP since a
 * call to 'log_ring_reserve' will publish any commited records thus making it
 * impossible to extend them.
 */
error_t log_ring_reserve_extend(
    struct log_ring*, size_t length,
    struct log_ring_reservation *out_reservation, size_t *out_prev_length
);

/*
 * Commit a reservation but don't make it available to the readers yet as to
 * allow it to be possibly extended later. This also restores the IRQ state
 * prior to log_ring_reserve.
 *
 * NOTE:
 * A reservation can be extended via 'log_ring_reserve_extend' as long as no
 * calls to 'log_ring_reserve' have been made in between.
 */
void log_ring_commit(struct log_ring_reservation*);

/*
 * Publish a log ring reservation by making it available to the readers.
 * This also restores the IRQ state prior to log_ring_reserve.
 *
 * This reservation can no longer be modified.
 */
void log_ring_publish(struct log_ring_reservation*);

struct log_record {
    /*
     * Sequence number this message was assigned. Can be used to calculate the
     * number of possibly dropped messages by subtracting from the last read
     * sequence number.
     */
    u64 seq_num;

    // Nanoseconds since boot when this log message was reserved/committed
    u64 timestamp_ns;

    u32 length;

    // Syslog information
    u8 facility;
    u8 level;

    struct string data;
};
BUILD_BUG_ON(
    // These are memcopied internally
    offset_of_after(struct log_record, level) !=
    offset_of_after(struct log_info_record, level)
);

/*
 * Read a record from the log ring.
 *
 * 'seq_num' is the sequence number, which the caller expects to read. This
 * value should always be set to previously_read_record.seq_num + 1 or
 * log_ring_first_readable_sequence_number() if none were previously read.
 *
 * The record, which was assigned this 'seq_num', or the lowest matching
 * sequence number (if the requested record no longer exists
 * due to being overwritten by other log messages) is written to 'out_rec'.
 */
error_t log_ring_read(
    struct log_ring *ring, u64 seq_num, char *out_buf, size_t buf_size,
    struct log_record *out_rec
);

/*
 * Check whether reading a record with 'seq_num' would succeed. Note that this
 * doesn't mean a record with this number actually exists, only that there is
 * something to read which has a greater or equal sequence number.
 */
error_t log_ring_readable(struct log_ring *ring, u64 seq_num);

/*
 * Returns the first sequence number which can be successfully accessed via
 * 'log_ring_read'. This can be used for initializing new log ring iterators,
 * and also allows the reader to know how many records have already been
 * dropped.
 */
u64 log_ring_first_readable_sequence_number(struct log_ring*);

/*
 * Returns the sequence number which will be assigned to the next log record
 * after a successful reservation. This can be used for implementing seek or
 * flush operations for log ring iterators.
 */
u64 log_ring_next_assigned_sequence_number(struct log_ring*);
