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
