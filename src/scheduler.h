#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "types.h"

typedef enum {
    EVENT_FORCESYNC,
    EVENT_LCD_HDRAW,
    EVENT_LCD_HBLANK,
    EVENT_CARD_DRQ,
    EVENT_TM07_RELOAD,
    EVENT_TM17_RELOAD,
    EVENT_TM27_RELOAD,
    EVENT_TM37_RELOAD,
    EVENT_TM09_RELOAD,
    EVENT_TM19_RELOAD,
    EVENT_TM29_RELOAD,
    EVENT_TM39_RELOAD,
    EVENT_SPU_SAMPLE,
    EVENT_SPU_CH0,
    EVENT_SPU_CAP0 = EVENT_SPU_CH0 + 16,
    EVENT_SPU_CAP1,
    EVENT_MAX
} EventType;

typedef struct {
    u64 time;
    EventType type;
} Event;

typedef struct _NDS NDS;

typedef struct {
    NDS* master;

    u64 now;

    Event event_queue[EVENT_MAX];
    int n_events;
} Scheduler;

void run_next_event(Scheduler* sched);

static inline bool event_pending(Scheduler* sched) {
    return sched->n_events && sched->now >= sched->event_queue[0].time;
}

void add_event(Scheduler* sched, EventType t, u64 time);
void remove_event(Scheduler* sched, EventType t);

void print_scheduled_events(Scheduler* sched);

#endif