#pragma once

struct event_device;

// Called by event_device_register() on the CPU of the device
void timer_queue_set_event_device(struct event_device *dev);
void timer_queue_expire(void);
