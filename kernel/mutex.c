#include <mutex.h>
#include <bug.h>

void mutex_init(struct mutex *mutex)
{
    spin_lock_init(&mutex->lock);
}

void mutex_lock(struct mutex *mutex)
{
    /*
     * With a single CPU and no way to sleep, a busy lock can only be
     * held by the caller itself, so spinning on it would hang forever.
     */
    BUG_ON(!spin_try_lock(&mutex->lock));
}

bool mutex_try_lock(struct mutex *mutex)
{
    return spin_try_lock(&mutex->lock);
}

void mutex_unlock(struct mutex *mutex)
{
    spin_unlock(&mutex->lock);
}
