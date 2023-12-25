#include "nds.h"

#include <string.h>

void init_nds(NDS* nds) {
    memset(nds, 0, sizeof *nds);
    nds->sched.master = nds;
    nds->cpu7.master = nds;
    nds->cpu9.master = nds;
    nds->ppu.master = nds;
    nds->io.master = nds;

    nds->vrambanks[0] = nds->vramA;
    nds->vrambanks[1] = nds->vramB;
    nds->vrambanks[2] = nds->vramC;
    nds->vrambanks[3] = nds->vramD;
    nds->vrambanks[4] = nds->vramE;
    nds->vrambanks[5] = nds->vramF;
    nds->vrambanks[6] = nds->vramG;
    nds->vrambanks[7] = nds->vramH;
    nds->vrambanks[8] = nds->vramI;


}

void nds_step(NDS* nds) {
    if(nds->cur_cpu) {
        cpu7_step(&nds->cpu7);
        nds->sched.now += 2;
    }else{
        cpu9_step(&nds->cpu9);
        nds->sched.now += 1;
    }
    if(event_pending(&nds->sched)) {
        nds->cur_cpu = !nds->cur_cpu;
        run_next_event(&nds->sched);
    }
}