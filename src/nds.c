#include "nds.h"

#include <string.h>

void init_nds(NDS* nds, GameCard* card) {
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

    nds->card = card;

    CardHeader* header = (CardHeader*) card->rom;

    memcpy(&nds->ram[0x3ffe00], header, sizeof *header);

    memcpy(&nds->ram[header->arm9_ram_offset & 0xffffff], &card->rom[header->arm9_rom_offset],
           header->arm9_size);
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
        return true;
    }
    return false;
}