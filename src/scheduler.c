#include "scheduler.h"

#include <stdio.h>

#include "nds.h"
#include "ppu.h"
#include "timer.h"

void run_to_present(Scheduler* sched) {
    u64 end_time = sched->now;
    while (sched->event_queue.size &&
           FIFO_peek(sched->event_queue).time <= end_time) {
        run_next_event(sched);
        if (sched->now > end_time) end_time = sched->now;
    }
    sched->now = end_time;
}

int run_next_event(Scheduler* sched) {
    if (sched->event_queue.size == 0) return 0;

    Event e;
    FIFO_pop(sched->event_queue, e);
    sched->now = e.time;

    if (e.type == EVENT_LCD_HDRAW) {
        lcd_hdraw(sched->master);
    } else if (e.type == EVENT_LCD_HBLANK) {
        lcd_hblank(sched->master);
    } else if (e.type == EVENT_CARD_DRQ) {
        if (sched->master->io7.exmemcnt.ndscardrights) {
            sched->master->io7.romctrl.drq = 1;
            for (int i = 0; i < 4; i++) {
                if (sched->master->io7.dma[i].cnt.mode == DMA7_DSCARD) {
                    dma7_activate(&sched->master->dma7, i);
                }
            }
        } else {
            sched->master->io9.romctrl.drq = 1;
            for (int i = 0; i < 4; i++) {
                if (sched->master->io9.dma[i].cnt.mode == DMA9_DSCARD) {
                    dma9_activate(&sched->master->dma9, i);
                }
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
    } else if (e.type < EVENT_MAX) {
        spu_tick_capture(&sched->master->spu, e.type - EVENT_SPU_CAP0);
    }

    return sched->now - e.time;
}

void add_event(Scheduler* sched, EventType t, u64 time) {
    if (sched->event_queue.size == EVENT_MAX) return;

    FIFO_push(sched->event_queue, ((Event){.type = t, .time = time}));

    u32 i = (sched->event_queue.tail - 1) % EVENT_MAX;
    while (i != sched->event_queue.head &&
           sched->event_queue.d[i].time <
               sched->event_queue.d[(i - 1) % EVENT_MAX].time) {
        Event tmp = sched->event_queue.d[(i - 1) % EVENT_MAX];
        sched->event_queue.d[(i - 1) % EVENT_MAX] = sched->event_queue.d[i];
        sched->event_queue.d[i] = tmp;
        i = (i - 1) % EVENT_MAX;
    }
}

void remove_event(Scheduler* sched, EventType t) {
    FIFO_foreach(i, sched->event_queue) {
        if (sched->event_queue.d[i].type == t) {
            sched->event_queue.size--;
            sched->event_queue.tail = (sched->event_queue.tail - 1) % EVENT_MAX;
            for (u32 j = i; j != sched->event_queue.tail;
                 j = (j + 1) % EVENT_MAX) {
                sched->event_queue.d[j] =
                    sched->event_queue.d[(j + 1) % EVENT_MAX];
            }
            return;
        }
    }
}

u64 find_event(Scheduler* sched, EventType t) {
    FIFO_foreach(i, sched->event_queue) {
        if (sched->event_queue.d[i].type == t) {
            return sched->event_queue.d[i].time;
        }
    }
    return -1;
}

void print_scheduled_events(Scheduler* sched) {
    static char* event_names[EVENT_MAX] = {
        "LCD HDraw",    "LCD HBlank",   "GameCard DRQ", "TM0-7 Reload",
        "TM1-7 Reload", "TM2-7 Reload", "TM3-7 Reload", "TM0-9 Reload",
        "TM1-9 Reload", "TM2-9 Reload", "TM3-9 Reload", "SPU Sample"};

    printf("Now: %ld\n", sched->now);
    FIFO_foreach(i, sched->event_queue) {
        if (sched->event_queue.d[i].type < EVENT_SPU_CH0) {
            printf("%ld => %s\n", sched->event_queue.d[i].time,
                   event_names[sched->event_queue.d[i].type]);
        } else {
            printf("%ld => SPU CH%x Reload\n", sched->event_queue.d[i].time,
                   sched->event_queue.d[i].type - EVENT_SPU_CH0);
        }
    }
}
