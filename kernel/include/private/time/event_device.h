#pragma once

#include <common/types.h>

struct event_device;

// Arm 'dev' for 'deadline_ns', or as soon as it can if that has passed
void event_device_arm(struct event_device *dev, u64 deadline_ns);
