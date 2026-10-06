#define MSG_FMT(msg) "ktest: " msg

#include <ktest/ktest.h>
#include <private/ktest/task.h>
#include <detail/sched/task.h>

#include <common/string_container.h>
#include <common/format.h>
#include <common/string.h>
#include <common/atomic.h>

#include <sched/kthread.h>
#include <log.h>
#include <param.h>
#include <init_level.h>
#include <free_after_init.h>
#include <memory/alloc.h>
#include <refcount.h>
#include <time/time.h>
#include <time/units.h>

DECLARE_TYPED_SECTION_MARKERS(const struct ktest_case, KTEST_CASES_SECTION);

static char s_run[128];
module_parameter(s_run);

struct ktest_run {
    struct refcount32 refcount;
    const struct ktest_case *test;
    u32 num_failures;
    bool skipped;
    const char *skip_reason;
    bool finished;
};

static void run_ref(struct ktest_run *run)
{
    refcount_inc(&run->refcount);
}

static void run_unref(struct ktest_run *run)
{
    if (refcount_dec_and_test(&run->refcount))
        free(run);
}

void ktest_inherit_run(struct task *task)
{
    struct ktest_run *run = current->ktest_run;

    task->ktest_run = run;
    if (run != nullptr)
        run_ref(run);
}

void ktest_release_run(struct task *task)
{
    if (task->ktest_run != nullptr)
        run_unref(task->ktest_run);
}

void ktest_fail(const char *file, u32 line, const char *fmt, ...)
{
    struct ktest_run *run = current->ktest_run;
    struct nested_printf message;
    va_list vlist;

    if (run == nullptr)
        return;

    va_start(vlist, fmt);
    message.fmt = fmt;
    message.vlist = &vlist;

    if (atomic_load_relaxed(&run->finished)) {
        print(
            LOG_NOTICE "# %s:%u: %pV (late, %pS.%pS is already finished)\n",
            file, line, &message, &run->test->suite, &run->test->name
        );
    } else {
        atomic_fetch_add(&run->num_failures, 1, MO_RELAXED);
        print(LOG_NOTICE "# %s:%u: %pV\n", file, line, &message);
    }

    va_end(vlist);
}

#define MAX_KTEST_VALUE_DESCRIPTION_LENGTH 32

#define DESCRIBE_PRINTF(out_desc_buf, ...)                                  \
    str_printf(out_desc_buf, MAX_KTEST_VALUE_DESCRIPTION_LENGTH, __VA_ARGS__)

#define KTEST_CMP_FMT(value_fmt) "expected %s, got " value_fmt " and " value_fmt

struct ktest_value_type_ops {
    /*
     * A three-way comparison of 'lhs' and 'rhs'. A value type without an
     * order returns zero for equal values and any other number otherwise.
     * The build only allows == and != on such a type.
     */
    int (*compare)(
        const struct ktest_value *lhs, const struct ktest_value *rhs
    );

    /*
     * Return a string representation of 'value', possibly using 'out_desc_buf'
     * for storage if needed for this type.
     */
    struct string (*describe)(
        const struct ktest_value *value,
        char out_desc_buf[static MAX_KTEST_VALUE_DESCRIPTION_LENGTH]
    );
    bool quoted;
};

static int compare_u64(u64 lhs, u64 rhs)
{
    if (lhs == rhs)
        return 0;

    return lhs < rhs ? -1 : 1;
}

static bool is_negative(const struct ktest_value *value)
{
    return value->is_signed && value->as_signed < 0;
}

static int compare_integers(
    const struct ktest_value *lhs, const struct ktest_value *rhs
)
{
    bool lhs_negative, rhs_negative;

    lhs_negative = is_negative(lhs);
    rhs_negative = is_negative(rhs);

    if (lhs_negative != rhs_negative)
        return lhs_negative ? -1 : 1;

    // Two values of the same sign are ordered by their bit patterns
    return compare_u64(lhs->as_unsigned, rhs->as_unsigned);
}

static struct string describe_integer(
    const struct ktest_value *value,
    char out_desc_buf[static MAX_KTEST_VALUE_DESCRIPTION_LENGTH]
)
{
    if (value->is_signed)
        return DESCRIBE_PRINTF(out_desc_buf, "%lld", value->as_signed);

    return DESCRIBE_PRINTF(out_desc_buf, "%llu", value->as_unsigned);
}

static int compare_pointers(
    const struct ktest_value *lhs, const struct ktest_value *rhs
)
{
    return compare_u64((ptr_t)lhs->as_pointer, (ptr_t)rhs->as_pointer);
}

static struct string describe_pointer(
    const struct ktest_value *value,
    char out_desc_buf[static MAX_KTEST_VALUE_DESCRIPTION_LENGTH]
)
{
    return DESCRIBE_PRINTF(out_desc_buf, "%p", value->as_pointer);
}

static int compare_strings(
    const struct ktest_value *lhs, const struct ktest_value *rhs
)
{
    return str_equals(lhs->as_string, rhs->as_string) ? 0 : 1;
}

static struct string describe_string(
    const struct ktest_value *value,
    char out_desc_buf[static MAX_KTEST_VALUE_DESCRIPTION_LENGTH]
)
{
    UNREFERENCED_PARAMETER(out_desc_buf);
    return value->as_string;
}

static const struct ktest_value_type_ops s_value_type_ops[] = {
    [KTEST_VALUE_INTEGER] = {
        .compare = compare_integers,
        .describe = describe_integer,
    },
    [KTEST_VALUE_POINTER] = {
        .compare = compare_pointers,
        .describe = describe_pointer,
    },
    [KTEST_VALUE_STRING] = {
        .compare = compare_strings,
        .describe = describe_string,
        .quoted = true,
    },
};

int ktest_compare(const struct ktest_value *lhs, const struct ktest_value *rhs)
{
    return s_value_type_ops[lhs->type].compare(lhs, rhs);
}

void ktest_fail_cmp(
    const char *file, u32 line, const char *expr, const struct ktest_value *lhs,
    const struct ktest_value *rhs
)
{
    char lhs_buf[MAX_KTEST_VALUE_DESCRIPTION_LENGTH];
    char rhs_buf[MAX_KTEST_VALUE_DESCRIPTION_LENGTH];
    const struct ktest_value_type_ops *ops;
    struct string lhs_desc, rhs_desc;

    ops = &s_value_type_ops[lhs->type];
    lhs_desc = ops->describe(lhs, lhs_buf);
    rhs_desc = ops->describe(rhs, rhs_buf);

    ktest_fail(
        file, line,
        ops->quoted ? KTEST_CMP_FMT("\"%pS\"") : KTEST_CMP_FMT("%pS"), expr,
        &lhs_desc, &rhs_desc
    );
}

void ktest_abort(void)
{
    kthread_exit();
}

void ktest_skip(const char *reason)
{
    struct ktest_run *run = current->ktest_run;

    run->skipped = true;
    run->skip_reason = reason;
    kthread_exit();
}

static void case_thread(void *user)
{
    struct ktest_run *run = user;

    run->test->fn();
}

static struct task *create_case_thread(struct ktest_run *run)
{
    struct task *thread;

    thread = kthread_create(case_thread, run, "ktest-%pS", &run->test->name);
    if (unlikely(thread == nullptr))
        return nullptr;

    thread->ktest_run = run;
    run_ref(run);
    return thread;
}

enum ktest_result : u8 {
    KTEST_PASSED,
    KTEST_FAILED,
    KTEST_SKIPPED,
};

// Run the case in a separate thread, returns true if the thread ran and exited
static bool run_case_thread(struct ktest_run *run)
{
    struct task *thread;
    bool exited;

    thread = create_case_thread(run);
    if (unlikely(thread == nullptr)) {
        print(LOG_NOTICE "# no memory for the thread\n");
        return false;
    }

    kthread_start(thread);
    exited = kthread_wait_for_exit_timeout(
        thread, MS_TO_NS(run->test->timeout_ms)
    );
    task_unref(thread);

    if (!exited) {
        print(
            LOG_NOTICE "# timed out after %u ms, thread left running\n",
            run->test->timeout_ms
        );
    }

    return exited;
}

static enum ktest_result case_result(const struct ktest_run *run, bool exited)
{
    if (!exited)
        return KTEST_FAILED;

    if (run->skipped)
        return KTEST_SKIPPED;

    if (run->num_failures != 0)
        return KTEST_FAILED;

    return KTEST_PASSED;
}

static void print_result(
    const struct ktest_case *test, u32 index, enum ktest_result result,
    const char *skip_reason
)
{
    if (result == KTEST_SKIPPED) {
        print(
            LOG_NOTICE "ok %u %pS.%pS # SKIP %s\n", index, &test->suite,
            &test->name, skip_reason
        );
        return;
    }

    print(
        LOG_NOTICE "%s %u %pS.%pS\n", result == KTEST_PASSED ? "ok" : "not ok",
        index, &test->suite, &test->name
    );
}

static enum ktest_result run_case(const struct ktest_case *test, u32 index)
{
    struct ktest_run *run;
    u64 start_ns, duration_ms;
    enum ktest_result result;
    bool exited;

    run = alloc(sizeof(*run), ALLOC_GENERIC_ZEROED);
    if (unlikely(run == nullptr)) {
        print(LOG_NOTICE "# out of memory\n");
        print_result(test, index, KTEST_FAILED, nullptr);
        return KTEST_FAILED;
    }

    refcount_set(&run->refcount, 1);
    run->test = test;

    start_ns = ns_since_boot();
    exited = run_case_thread(run);
    duration_ms = NS_TO_MS(ns_since_boot() - start_ns);

    /*
     * The case fails if the main thread hasn't exited within the timeout,
     * but the threads that it started may still be running. If they report
     * a result after this point it's simply discarded as "late" and is not
     * counted.
     */
    atomic_store_relaxed(&run->finished, true);
    result = case_result(run, exited);

    print(
        LOG_NOTICE "# %pS.%pS: %llu ms\n", &test->suite, &test->name,
        duration_ms
    );
    print_result(test, index, result, run->skip_reason);
    run_unref(run);

    return result;
}

static void runner_thread(void *user)
{
    const struct ktest_case *test;
    u32 num_passed = 0, num_failed = 0, num_skipped = 0;
    u32 index = 0;

    UNREFERENCED_PARAMETER(user);

    if (strcmp(s_run, "all") != 0) {
        pr_notice("nothing matched \"%s\"\n", s_run);
        return;
    }

    print(LOG_NOTICE "KTAP version 1\n");
    print(LOG_NOTICE "1..%zu\n", SECTION_ARRAY_SIZE(KTEST_CASES_SECTION));

    for_each_section_array_item(test, KTEST_CASES_SECTION) {
        switch (run_case(test, ++index)) {
        case KTEST_PASSED:
            num_passed++;
            break;
        case KTEST_FAILED:
            num_failed++;
            break;
        case KTEST_SKIPPED:
            num_skipped++;
            break;
        }
    }

    pr_notice(
        "%u passed, %u failed, %u skipped\n",
        num_passed, num_failed, num_skipped
    );
}

static error_t INIT_CODE ktest_init(void)
{
    if (s_run[0] == '\0')
        return EOK;

    return kthread_run(runner_thread, nullptr, "ktest");
}
INIT_CALL_POST(GENERIC_MODULES_INITIALIZED, ktest_init);
