#pragma once

#include <common/types.h>

/*
 * Switch to the next task. A caller that isn't TASK_STATE_RUNNING is taken off
 * the runqueue and blocks until someone wakes them up. A running caller only
 * switches if something should run first.
 *
 * NOTE: A blocked caller may be woken up for a reason other than the one it
 *       waits for, so it has to check its condition again after this returns.
 */
void schedule(void);

/*
 * Same as schedule(), except the caller is woken up after 'deadline_ns' instead
 * of sleeping indefinitely. Returns true if the deadline passed, false if the
 * caller was woken up by something else before the deadline. A 'deadline_ns'
 * of NO_DEADLINE is the same as schedule().
 *
 * NOTE: The two may race. A caller woken up right before the deadline may get
 *       true, and one woken up right after it may get false. A caller that
 *       didn't sleep at all gets false.
 */
bool schedule_until(u64 deadline_ns);
