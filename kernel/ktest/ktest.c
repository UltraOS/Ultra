#define MSG_FMT(msg) "ktest: " msg

#include <ktest/ktest.h>
#include <detail/sched/task.h>

#include <common/format.h>
#include <common/string.h>
#include <common/atomic.h>

#include <sched/kthread.h>
#include <log.h>
#include <param.h>
#include <init_level.h>
#include <free_after_init.h>
#include <memory/alloc.h>
#include <sched/completion.h>
#include <time/time.h>
#include <time/units.h>

#define KTEST_RUN_CAPACITY 128

DECLARE_TYPED_SECTION_MARKERS(const struct ktest_case, KTEST_CASES_SECTION);

static char s_run[KTEST_RUN_CAPACITY];
module_parameter(s_run);

struct ktest_run {
    const struct ktest_case *test;
    const struct task *thread;
    struct completion done;
    u32 num_failures;
    bool skipped;
    const char *skip_reason;
};

void ktest_fail(const char *file, u32 line, const char *fmt, ...)
{
    struct ktest_run *run = current->ktest_run;
    char message[128];
    va_list vlist;

    if (run == nullptr)
        return;

    va_start(vlist, fmt);
    vsnprintf(message, sizeof(message), fmt, vlist);
    va_end(vlist);

    atomic_fetch_add(&run->num_failures, 1, MO_RELAXED);
    print(LOG_NOTICE "# %s:%u: %s\n", file, line, message);
}

void ktest_fail_cmp(
    const char *file, u32 line, const char *expr, u64 lhs, u64 rhs,
    bool is_signed
)
{
    if (is_signed) {
        ktest_fail(
            file, line, "expected %s, got %lld and %lld", expr, (i64)lhs,
            (i64)rhs
        );
        return;
    }

    ktest_fail(file, line, "expected %s, got %llu and %llu", expr, lhs, rhs);
}

NORETURN
static void run_finish(struct ktest_run *run)
{
    if (current == run->thread)
        complete(&run->done);

    kthread_exit();
}

void ktest_abort(void)
{
    run_finish(current->ktest_run);
}

void ktest_skip(const char *reason)
{
    struct ktest_run *run = current->ktest_run;

    run->skipped = true;
    run->skip_reason = reason;
    run_finish(run);
}

static void case_thread(void *user)
{
    struct ktest_run *run = user;

    run->thread = current;
    run->test->fn();
    complete(&run->done);
}

enum ktest_result : u8 {
    KTEST_PASSED,
    KTEST_FAILED,
    KTEST_SKIPPED,
};

static enum ktest_result run_case(const struct ktest_case *test, u32 index)
{
    struct ktest_run *run;
    u64 start_ns, duration_ms;
    bool passed;
    enum ktest_result result;
    error_t ret;

    run = alloc(sizeof(*run), ALLOC_GENERIC_ZEROED);
    if (run == nullptr) {
        print(LOG_NOTICE "not ok %u %s.%s\n", index, test->suite, test->name);
        print(LOG_NOTICE "# out of memory\n");
        return KTEST_FAILED;
    }

    run->test = test;
    completion_init(&run->done);
    current->ktest_run = run;

    start_ns = ns_since_boot();

    ret = kthread_run(case_thread, run, "ktest-%s", test->name);
    if (!is_error(ret))
        wait_for_completion(&run->done);

    duration_ms = (ns_since_boot() - start_ns) / NS_PER_MS;
    current->ktest_run = nullptr;

    passed = !is_error(ret) && run->num_failures == 0;

    if (run->skipped) {
        print(
            LOG_NOTICE "ok %u %s.%s # SKIP %s\n", index, test->suite,
            test->name, run->skip_reason
        );
    } else {
        print(
            LOG_NOTICE "%s %u %s.%s\n", passed ? "ok" : "not ok", index,
            test->suite, test->name
        );
    }

    if (is_error(ret))
        print(LOG_NOTICE "# could not start the thread: %pE\n", &ret);

    print(
        LOG_NOTICE "# %s.%s: %llu ms\n", test->suite, test->name,
        duration_ms
    );

    result = KTEST_FAILED;
    if (run->skipped)
        result = KTEST_SKIPPED;
    else if (passed)
        result = KTEST_PASSED;

    return result;
}

static void runner_thread(void *user)
{
    const struct ktest_case *test;
    u32 num_passed = 0, num_failed = 0, num_skipped = 0;
    u32 num_cases, index = 0;

    UNREFERENCED_PARAMETER(user);

    if (strcmp(s_run, "all") != 0) {
        pr_notice("nothing matched \"%s\"\n", s_run);
        return;
    }

    num_cases = SECTION_ARRAY_SIZE(KTEST_CASES_SECTION);

    print(LOG_NOTICE "KTAP version 1\n");
    print(LOG_NOTICE "1..%u\n", num_cases);

    for (test = SECTION_ARRAY_BEGIN(KTEST_CASES_SECTION);
         test < SECTION_ARRAY_END(KTEST_CASES_SECTION); test++) {
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
