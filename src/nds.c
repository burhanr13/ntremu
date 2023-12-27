#include "nds.h"

#include <string.h>

void init_nds(NDS* nds, GameCard* card, u8* bios7, u8* bios9) {
    memset(nds, 0, sizeof *nds);
    nds->sched.master = nds;
    nds->cpu7.master = nds;
    nds->cpu9.master = nds;
    nds->ppuA.master = nds;
    nds->ppuA.io = &nds->io9.ppuA;
    nds->ppuB.master = nds;
    nds->ppuB.io = &nds->io9.ppuB;
    nds->io7.master = nds;
    nds->io9.master = nds;

    nds->vrambanks[0] = nds->vramA;
    nds->vrambanks[1] = nds->vramB;
    nds->vrambanks[2] = nds->vramC;
    nds->vrambanks[3] = nds->vramD;
    nds->vrambanks[4] = nds->vramE;
    nds->vrambanks[5] = nds->vramF;
    nds->vrambanks[6] = nds->vramG;
    nds->vrambanks[7] = nds->vramH;
    nds->vrambanks[8] = nds->vramI;

    nds->card = card;
    nds->bios7 = bios7;
    nds->bios9 = bios9;

    CardHeader* header = (CardHeader*) card->rom;

    memcpy(&nds->ram[0x3ffe00], header, sizeof *header);

    memcpy(&nds->ram[header->arm9_ram_offset & 0xffffff], &card->rom[header->arm9_rom_offset],
           header->arm9_size);
    nds->cpu9.itcm_virtsize = 0x2000000;
    nds->cpu9.dtcm_base = 0x27c0000;
    nds->cpu9.pc = header->arm9_entry;
    nds->cpu9.cpsr.m = M_SYSTEM;
    cpu9_flush(&nds->cpu9);

    if(header->arm7_ram_offset >> 24 == 3) {
        memcpy(&nds->wram7[header->arm7_ram_offset % WRAM7SIZE], &card->rom[header->arm7_rom_offset],
               header->arm7_size);
    } else {
        memcpy(&nds->ram[header->arm7_ram_offset % RAMSIZE], &card->rom[header->arm7_rom_offset],
               header->arm7_size);
    }
    nds->cpu7.pc = header->arm7_entry;
    nds->cpu7.cpsr.m = M_SYSTEM;
    cpu7_flush(&nds->cpu7);

    add_event(&nds->sched, EVENT_LCD_HDRAW, 0);
}

bool nds_step(NDS* nds) {
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
        nds->cpu7.irq = (nds->io7.ime & 1) && (nds->io7.ie.w & nds->io7.ifl.w);
        nds->cpu9.irq = (nds->io9.ime & 1) && (nds->io9.ie.w & nds->io9.ifl.w);
        return true;
    }
    return false;
}
