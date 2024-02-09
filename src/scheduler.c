#include "scheduler.h"

#include <stdio.h>

#include "nds.h"
#include "ppu.h"
#include "timer.h"

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
    u64 run_time = sched->now;
    sched->now = e.time;

    if (e.type == EVENT_FORCESYNC) {
        if (sched->master->halt7 || sched->master->cpu9.halt) {
            add_event(sched, EVENT_FORCESYNC, sched->event_queue[0].time + 32);
        } else {
            add_event(sched, EVENT_FORCESYNC, sched->now + 32);
        }
    } else if (e.type == EVENT_LCD_HDRAW) {
        lcd_hdraw(sched->master);
    } else if (e.type == EVENT_LCD_HBLANK) {
        lcd_hblank(sched->master);
    } else if (e.type == EVENT_CARD_DRQ) {
        sched->master->io7.romctrl.drq = 1;
        for (int i = 0; i < 4; i++) {
            if (sched->master->io7.dma[i].cnt.mode == DMA7_DSCARD) {
                dma7_activate(&sched->master->dma7, i);
            }
        }
        sched->master->io9.romctrl.drq = 1;
        for (int i = 0; i < 4; i++) {
            if (sched->master->io9.dma[i].cnt.mode == DMA9_DSCARD) {
                dma9_activate(&sched->master->dma9, i);
            }
        }
    } else if (e.type < EVENT_TM09_RELOAD) {
        reload_timer(&sched->master->tmc7, e.type - EVENT_TM07_RELOAD);
    } else if (e.type < EVENT_SPU_SAMPLE) {
        reload_timer(&sched->master->tmc9, e.type - EVENT_TM09_RELOAD);
    } else if (e.type == EVENT_SPU_SAMPLE) {
        spu_sample(&sched->master->spu);
    } else if (e.type < EVENT_SPU_CAP0) {
        spu_tick_channel(&sched->master->spu, e.type - EVENT_SPU_CH0);
    }else if(e.type < EVENT_MAX) {
        spu_tick_capture(&sched->master->spu, e.type - EVENT_SPU_CAP0);
    }

    if (run_time > sched->now) sched->now = run_time;
}

void add_event(Scheduler* sched, EventType t, u64 time) {
    if (sched->n_events == EVENT_MAX) return;

    int i = sched->n_events;
    sched->event_queue[sched->n_events].type = t;
    sched->event_queue[sched->n_events].time = time;
    sched->n_events++;

    while (i > 0 &&
           sched->event_queue[i].time < sched->event_queue[i - 1].time) {
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
    static char* event_names[EVENT_MAX] = {
        "Force Sync",   "LCD HDraw",    "LCD HBlank",   "GameCard DRQ",
        "TM0-7 Reload", "TM1-7 Reload", "TM2-7 Reload", "TM3-7 Reload",
        "TM0-9 Reload", "TM1-9 Reload", "TM2-9 Reload", "TM3-9 Reload"};

    printf("Now: %ld\n", sched->now);
    for (int i = 0; i < sched->n_events; i++) {
        if (sched->event_queue[i].type < EVENT_SPU_CH0) {
            printf("%ld => %s\n", sched->event_queue[i].time,
                   event_names[sched->event_queue[i].type]);
        } else {
            printf("%ld => SPU CH%x Reload\n", sched->event_queue[i].time,
                   sched->event_queue[i].type - EVENT_SPU_CH0);
        }
    }
}
