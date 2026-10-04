#pragma once

struct task;

// Called by task_exit() for a kthread once it is marked dead
void kthread_did_exit(struct task *task);
