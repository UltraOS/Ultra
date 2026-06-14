#pragma once

struct counter_device;

void time_keeper_tick(void);
void time_keeper_set_counter_device(struct counter_device *cd);
