#include <mutex.h>

#include <test_harness.h>

TEST_CASE(mutex_lock_unlock)
{
    struct mutex mutex;

    mutex_init(&mutex);

    mutex_lock(&mutex);
    ASSERT(!mutex_try_lock(&mutex));
    mutex_unlock(&mutex);

    mutex_lock(&mutex);
    mutex_unlock(&mutex);
}

TEST_CASE(mutex_try_lock_semantics)
{
    struct mutex mutex;

    mutex_init(&mutex);

    ASSERT(mutex_try_lock(&mutex));
    ASSERT(!mutex_try_lock(&mutex));
    mutex_unlock(&mutex);

    ASSERT(mutex_try_lock(&mutex));
    mutex_unlock(&mutex);
}

TEST_CASE(mutex_static_init)
{
    DEFINE_MUTEX(static_mutex)

    ASSERT(mutex_try_lock(&static_mutex));
    ASSERT(!mutex_try_lock(&static_mutex));
    mutex_unlock(&static_mutex);
}
