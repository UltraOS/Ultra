#pragma once

#include <ucontext.h>

struct arch_task {
    ucontext_t context;
    void (*fn)(void *user);
    void *user;
};
