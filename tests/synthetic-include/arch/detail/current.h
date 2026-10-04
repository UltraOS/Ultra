#pragma once

struct task;

extern __thread struct task *g_test_current;

#define current g_test_current
