#include "timer.h"

#include <stdio.h>

#include "io.h"
#include "nds.h"

const int RATES[4] = {0, 6, 8, 10};

void update_timer_count(TimerController* tmc, int i) {
    if (!tmc->io->tm[i].cnt.enable || tmc->io->tm[i].cnt.countup) {
        tmc->set_time[i] = tmc->master->sched.now;
        return;
    }

    int rate = RATES[tmc->io->tm[i].cnt.rate];
    tmc->counter[i] += (tmc->master->sched.now >> rate) - (tmc->set_time[i] >> rate);
    tmc->set_time[i] = tmc->master->sched.now;
}

void update_timer_reload(TimerController* tmc, int i) {
    remove_event(&tmc->master->sched, tmc->tm0_event + i);

    if (!tmc->io->tm[i].cnt.enable || tmc->io->tm[i].cnt.countup) return;

    int rate = RATES[tmc->io->tm[i].cnt.rate];
    u64 rel_time = (tmc->set_time[i] + ((0x10000 - tmc->counter[i]) << rate)) & ~((1 << rate) - 1);
    add_event(&tmc->master->sched, tmc->tm0_event + i, rel_time);
}

void reload_timer(TimerController* tmc, int i) {
    tmc->counter[i] = tmc->io->tm[i].reload;
    tmc->set_time[i] = tmc->master->sched.now;
    update_timer_reload(tmc, i);

    if (tmc->io->tm[i].cnt.irq) tmc->io->ifl.timer |= (1 << i);

    if (i + 1 < 4 && tmc->io->tm[i + 1].cnt.enable && tmc->io->tm[i + 1].cnt.countup) {
        tmc->counter[i + 1]++;
        if (tmc->counter[i + 1] == 0) reload_timer(tmc, i + 1);
    }
}