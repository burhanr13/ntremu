#ifndef TIMER_H
#define TIMER_H

#include "io.h"
#include "scheduler.h"
#include "types.h"

typedef struct _NDS NDS;

typedef struct {
    NDS* master;

    u64 set_time[4];
    u16 counter[4];

    IO* io;
    EventType tm0_event;
} TimerController;

void update_timer_count(TimerController* tmc, int i);
void update_timer_reload(TimerController* tmc, int i);

void reload_timer(TimerController* tmc, int i);

#endif