#pragma once

#include <common/types.h>
#include <common/list.h>

struct rr_entity {
    struct list_link link;
    u64 slice_left_ns;
    u64 running_since_ns;
};
