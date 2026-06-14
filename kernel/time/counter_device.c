#define MSG_FMT(msg) "time: " msg

#include <common/minmax.h>
#include <common/format.h>
#include <common/list.h>

#include <private/time/counter_device.h>
#include <private/time/keeper.h>

#include <log.h>
#include <spinlock.h>

static LIST_HEAD(s_counter_devices);
static DEFINE_SPINLOCK(s_counter_lock);
static struct counter_device *s_active_cd;

#define IDEAL_UNATTENDED_SECONDS 600

// Any faster and 'hz * IDEAL_UNATTENDED_SECONDS' no longer fits in a u64
#define MAX_COUNTER_HZ (UNSIGNED_MAX(u64) / IDEAL_UNATTENDED_SECONDS)

static void calculate_mult_and_shift(struct counter_device *dev, u64 hz)
{
    u64 numerator, tmp, max_safe_mult;

    /*
     * We want to guarantee the system can sleep for at least N seconds without
     * degrading to the slow 128-bit multiplication slow path. We calculate the
     * absolute maximum multiplier that satisfies this
     */
    max_safe_mult = UNSIGNED_MAX(u64) / (hz * IDEAL_UNATTENDED_SECONDS);

    // Maximum reasonable shift as a start
    dev->shift = 32;

    for (;;) {
        numerator = NS_PER_SEC << dev->shift;

        /*
         * Add (hz / 2) to round to the nearest whole number rather than
         * truncating
         */
        numerator += (hz / 2);
        tmp = numerator / hz;

        if (tmp <= UNSIGNED_MAX(u32) && tmp <= max_safe_mult)
            break;
        if (dev->shift == 0)
            break;

        // No fit, drop precision and try again
        dev->shift--;
    }

    dev->mult = tmp;
}

static void calculate_thresholds(struct counter_device *dev)
{
    u64 math_limit, hw_limit, max_unattended_cycles;

    math_limit = UNSIGNED_MAX(u64) / dev->mult;
    hw_limit = dev->mask;

    dev->max_cycles_before_u64_overflow = math_limit;

    /*
     * Take the number of cycles either before the hardware counter overflows,
     * or before we degrade into slow path for the cycles_to_ns helper.
     * Whichever one is smaller, takes precedence. Then we just take a half of
     * that to give it some leeway
     */
    max_unattended_cycles = MIN(math_limit, hw_limit);
    max_unattended_cycles >>= 1;
    dev->max_unattended_ns =
        ((u128)max_unattended_cycles * dev->mult) >> dev->shift;

    /*
     * Allow up to 87.5% of the counter width before a delta is discarded
     * as negative motion. Since max_unattended_ns above targets 50%, this
     * leaves enough slack for a keeper ping that arrives late to still be
     * accumulated instead of thrown away.
     */
    dev->max_acceptable_cycles_delta = dev->mask - (dev->mask >> 3);
}

static void announce_new_counter(struct counter_device *dev, u64 hz, bool best)
{
    u64 ms;
    char freq_str[32], wrap_str[32];
#define snprintf_freq(...) snprintf(freq_str, sizeof(freq_str), __VA_ARGS__)
#define snprintf_wrap(...) snprintf(wrap_str, sizeof(wrap_str), __VA_ARGS__)

    if (hz >= GHZ) {
        snprintf_freq("%llu.%03llu GHz", hz / GHZ, (hz % GHZ) / MHZ);
    } else if (hz >= MHZ) {
        snprintf_freq("%llu.%03llu MHz", hz / MHZ, (hz % MHZ) / KHZ);
    } else if (hz >= KHZ) {
        snprintf_freq("%llu.%03llu KHz", hz / KHZ, hz % KHZ);
    } else {
        snprintf_freq("%llu Hz", hz);
    }

    ms = dev->max_unattended_ns / NS_PER_MS;

    if (ms >= MS_PER_HOUR) {
        snprintf_wrap("%llu hours", ms / MS_PER_HOUR);
    } else if (ms >= MS_PER_MIN) {
        snprintf_wrap("%llu mins", ms / MS_PER_MIN);
    } else if (ms >= MS_PER_SEC) {
        snprintf_wrap("%llu.%03llu secs", ms / MS_PER_SEC, ms % MS_PER_SEC);
    } else {
        snprintf_wrap("%llu ms", ms);
    }

    pr_info(
        "%s @ %s [rated %d], unattended up to %s%s\n",
        dev->name, freq_str, dev->rating, wrap_str, best ? " [best]" : ""
    );
}

static bool counter_device_is_valid(struct counter_device *dev, u64 hz)
{
    if (dev->read == nullptr)
        return false;
    if (hz == 0 || hz > MAX_COUNTER_HZ)
        return false;

    // The wraparound math relies on the mask being contiguous low bits
    if (dev->mask == 0 || (dev->mask & (dev->mask + 1)) != 0)
        return false;

    return true;
}

static bool counter_device_is_linked(struct counter_device *dev)
{
    return dev->link.next != nullptr && !list_is_empty(&dev->link);
}

static bool counter_device_insert(struct counter_device *dev)
{
    bool best;
    struct list_link *entry = &s_counter_devices;
    struct counter_device *cursor;

    list_for_each_entry(cursor, &s_counter_devices, link) {
        if (cursor->rating < dev->rating)
            break;
        entry = &cursor->link;
    }

    list_insert_next(entry, &dev->link);
    best = dev->link.prev == &s_counter_devices;

    return best;
}

error_t counter_device_register(struct counter_device *dev, u64 hz)
{
    bool is_best;
    irq_state_t state;

    if (!counter_device_is_valid(dev, hz))
        return EINVAL;

    calculate_mult_and_shift(dev, hz);
    calculate_thresholds(dev);

    state = spin_lock_irq_save(&s_counter_lock);

    if (counter_device_is_linked(dev)) {
        spin_unlock_irq_restore(&s_counter_lock, state);
        return EBUSY;
    }

    is_best = counter_device_insert(dev);
    if (is_best) {
        atomic_store_relaxed(&s_active_cd, dev);
        time_keeper_set_counter_device(dev);
    }

    spin_unlock_irq_restore(&s_counter_lock, state);

    announce_new_counter(dev, hz, is_best);
    return EOK;
}

void counter_device_unregister(struct counter_device *dev)
{
    irq_state_t state;
    struct counter_device *best = nullptr;

    state = spin_lock_irq_save(&s_counter_lock);

    list_remove(&dev->link);

    if (atomic_load_relaxed(&s_active_cd) == dev) {
        if (!list_is_empty(&s_counter_devices)) {
            best = list_first_entry(
                &s_counter_devices, struct counter_device, link
            );
        }

        atomic_store_relaxed(&s_active_cd, best);
        time_keeper_set_counter_device(best);
    }

    spin_unlock_irq_restore(&s_counter_lock, state);
}

struct counter_device *current_counter_device(void)
{
    return atomic_load_relaxed(&s_active_cd);
}
