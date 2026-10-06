#pragma once

struct task;

// Called by task_exit() for a kthread once it is marked dead
void kthread_did_exit(struct task *task);

// Called by task_unref() for a kthread once its last reference is gone
void kthread_release(struct task *task);
