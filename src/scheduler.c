#include "scheduler.h"

#include <stdio.h>

#include "nds.h"
#include "ppu.h"

void run_scheduler(Scheduler* sched, int cycles) {
    u64 end_time = sched->now + cycles;
    while (sched->n_events && sched->event_queue[0].time <= end_time) {
        run_next_event(sched);
    }
    sched->now = end_time;
}

void run_next_event(Scheduler* sched) {
    if (sched->n_events == 0) return;

    Event e = sched->event_queue[0];
    sched->n_events--;
    for (int i = 0; i < sched->n_events; i++) {
        sched->event_queue[i] = sched->event_queue[i + 1];
    }
    sched->now = e.time;

    if (e.type == EVENT_LCD_HDRAW) {
        ppu_hdraw(&sched->master->ppu);
    } else if (e.type == EVENT_LCD_HBLANK) {
        ppu_hblank(&sched->master->ppu);
    }
}

void add_event(Scheduler* sched, EventType t, u64 time) {
    if (sched->n_events == EVENT_MAX) return;

    int i = sched->n_events;
    sched->event_queue[sched->n_events].type = t;
    sched->event_queue[sched->n_events].time = time;
    sched->n_events++;

    while (i > 0 && sched->event_queue[i].time < sched->event_queue[i - 1].time) {
        Event tmp = sched->event_queue[i - 1];
        sched->event_queue[i - 1] = sched->event_queue[i];
        sched->event_queue[i] = tmp;
        i--;
    }
}

void remove_event(Scheduler* sched, EventType t) {
    for (int i = 0; i < sched->n_events; i++) {
        if (sched->event_queue[i].type == t) {
            sched->n_events--;
            for (int j = i; j < sched->n_events; j++) {
                sched->event_queue[j] = sched->event_queue[j + 1];
            }
            return;
        }
    }
}

void print_scheduled_events(Scheduler* sched) {
    static char* event_names[EVENT_MAX] = {"LCD HDraw", "LCD HBlank"};

    for (int i = 0; i < sched->n_events; i++) {
        printf("%ld => %s\n", sched->event_queue[i].time, event_names[sched->event_queue[i].type]);
    }
}
