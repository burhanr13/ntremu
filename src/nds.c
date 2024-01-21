#include "nds.h"

#include <string.h>

#include "bus7.h"
#include "ppu.h"

void init_nds(NDS* nds, GameCard* card, u8* bios7, u8* bios9, u8* firmware,
              bool bootbios) {
    memset(nds, 0, sizeof *nds);
    nds->sched.master = nds;

    nds->cpu7.master = nds;
    nds->dma7.master = nds;
    nds->tmc7.master = nds;
    nds->tmc7.io = &nds->io7;
    nds->tmc7.tm0_event = EVENT_TM07_RELOAD;

    nds->cpu9.master = nds;
    nds->dma9.master = nds;
    nds->tmc9.master = nds;
    nds->tmc9.io = &nds->io9;
    nds->tmc9.tm0_event = EVENT_TM09_RELOAD;

    nds->ppuA.master = nds;
    nds->ppuA.io = &nds->io9.ppuA;
    nds->ppuA.pal = nds->palA;
    nds->ppuA.extPalBg[0] = (u16*) nds->vramE;
    nds->ppuA.extPalBg[1] = (u16*) nds->vramE + 0x1000;
    nds->ppuA.extPalBg[2] = (u16*) nds->vramE + 0x2000;
    nds->ppuA.extPalBg[3] = (u16*) nds->vramE + 0x3000;
    nds->ppuA.extPalObj = (u16*) nds->vramF;
    nds->ppuA.oam = nds->oamA;
    nds->ppuA.bgReg = VRAMBGA;
    nds->ppuA.objReg = VRAMOBJA;

    nds->ppuB.master = nds;
    nds->ppuB.io = &nds->io9.ppuB;
    nds->ppuB.pal = nds->palB;
    nds->ppuB.extPalBg[0] = (u16*) nds->vramH;
    nds->ppuB.extPalBg[1] = (u16*) nds->vramH + 0x1000;
    nds->ppuB.extPalBg[2] = (u16*) nds->vramH + 0x2000;
    nds->ppuB.extPalBg[3] = (u16*) nds->vramH + 0x3000;
    nds->ppuB.extPalObj = (u16*) nds->vramI;
    nds->ppuB.oam = nds->oamB;
    nds->ppuB.bgReg = VRAMBGB;
    nds->ppuB.objReg = VRAMOBJB;

    nds->gpu.master = nds;

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
    card->state = 0;
    card->addr = 0;
    card->i = 0;
    card->len = 0;
    card->key1mode = false;
    card->eeprom_state = 0;
    card->spidata = 0;
    memset(&card->eepromst, 0, sizeof card->eepromst);

    nds->bios7 = bios7;
    nds->bios9 = bios9;
    nds->firmware = firmware;

    nds->io9.keyinput.h = 0x3ff;
    nds->io7.keyinput.h = 0x3ff;
    nds->io7.extkeyin.h = 0x7f;

    nds->io7.ipcfifocnt.sendempty = 1;
    nds->io7.ipcfifocnt.recvempty = 1;
    nds->io9.ipcfifocnt.sendempty = 1;
    nds->io9.ipcfifocnt.recvempty = 1;
    nds->io9.gxstat.gxfifo_empty = 1;
    nds->io9.gxstat.gxfifo_half = 1;

    if (bootbios) {
        cpu7_handle_interrupt(&nds->cpu7, I_RESET);
        cpu9_handle_interrupt(&nds->cpu9, I_RESET);
    } else {

        nds->io9.ppuA.bgaff[0].pa = 1 << 8;
        nds->io9.ppuA.bgaff[0].pd = 1 << 8;
        nds->io9.ppuA.bgaff[1].pa = 1 << 8;
        nds->io9.ppuA.bgaff[1].pd = 1 << 8;
        nds->io9.ppuB.bgaff[0].pa = 1 << 8;
        nds->io9.ppuB.bgaff[0].pd = 1 << 8;
        nds->io9.ppuB.bgaff[1].pa = 1 << 8;
        nds->io9.ppuB.bgaff[1].pd = 1 << 8;

        nds->io7.wramstat = 3;
        nds->io9.wramcnt = 3;
        nds->io7.postflg = 1;
        nds->io9.postflg = 1;

        CardHeader* header = (CardHeader*) card->rom;

        *(u32*) &nds->ram[0x3ff800] = 0x00001fc2;
        *(u32*) &nds->ram[0x3ff804] = 0x00001fc2;
        *(u16*) &nds->ram[0x3ff808] = header->header_crc;
        *(u16*) &nds->ram[0x3ff80a] = header->secure_crc;
        *(u16*) &nds->ram[0x3ff850] = 0x5835;
        *(u32*) &nds->ram[0x3ff868] = (*(u16*) &firmware[0x20]) << 3;
        *(u16*) &nds->ram[0x3ff874] = 0x359a;
        *(u32*) &nds->ram[0x3ffc00] = 0x00001fc2;
        *(u32*) &nds->ram[0x3ffc04] = 0x00001fc2;
        *(u16*) &nds->ram[0x3ffc08] = header->header_crc;
        *(u16*) &nds->ram[0x3ffc0a] = header->secure_crc;
        *(u16*) &nds->ram[0x3ffc10] = 0x5835;
        *(u16*) &nds->ram[0x3ffc40] = 2;

        memcpy(&nds->ram[0x3ffc80], &firmware[0x3ff00], 0x70);

        memcpy(&nds->ram[0x3ffe00], header, sizeof *header);

        memcpy(&nds->ram[header->arm9_ram_offset & 0xffffff],
               &card->rom[header->arm9_rom_offset], header->arm9_size);
        nds->cpu9.itcm_virtsize = 0x2000000;
        nds->cpu9.dtcm_base = 0x3000000;
        nds->cpu9.dtcm_virtsize = DTCMSIZE;
        nds->cpu9.pc = header->arm9_entry;
        nds->cpu9.cpsr.m = M_SYSTEM;
        cpu9_flush(&nds->cpu9);

        if (header->arm7_ram_offset >> 24 == 3) {
            for (int i = 0; i < header->arm7_size; i += 4) {
                bus7_write32(nds, header->arm7_ram_offset + i,
                             *(u32*) &card->rom[header->arm7_rom_offset + i]);
            }
        } else {
            memcpy(&nds->ram[header->arm7_ram_offset % RAMSIZE],
                   &card->rom[header->arm7_rom_offset], header->arm7_size);
        }
        nds->cpu7.pc = header->arm7_entry;
        nds->cpu7.cpsr.m = M_SYSTEM;
        cpu7_flush(&nds->cpu7);
    }

    lcd_hdraw(nds);
    add_event(&nds->sched, EVENT_FORCESYNC, 32);
}

bool nds_step(NDS* nds) {
    if (nds->cur_cpu) {
        if (nds->halt7) {
            if (nds->io7.ie.w & nds->io7.ifl.w) {
                nds->halt7 = false;
                nds->io7.haltcnt = 0;
                nds->cpu7.irq = true;
            } else {
                nds->sched.now = nds->sched.event_queue[0].time;
            }
        } else {
            cpu7_step(&nds->cpu7);
            nds->sched.now += 1;
        }
    } else {
        if (cpu9_step(&nds->cpu9)) {
            nds->half_tick = !nds->half_tick;
            if (nds->half_tick) nds->sched.now += 1;
        } else {
            nds->sched.now = nds->sched.event_queue[0].time;
        }
    }
    if (event_pending(&nds->sched)) {
        if (nds->cur_cpu) {
            run_next_event(&nds->sched);
            nds->cpu7.irq = nds->io7.ime && (nds->io7.ie.w & nds->io7.ifl.w);
            nds->cpu9.irq = nds->io9.ime && (nds->io9.ie.w & nds->io9.ifl.w);

            nds->cur_cpu = CPU9;
            nds->last_event = nds->sched.now;
        } else {
            nds->cur_cpu = CPU7;
            nds->sched.now = nds->last_event;
        }
        return true;
    }
    return false;
}

void firmware_spi_write(NDS* nds, u8 data, bool hold) {
    switch (nds->firmflashst.state) {
        case FIRMFLASH_IDLE:
            switch (data) {
                case 0x06:
                    nds->firmflashst.write_enable = true;
                    break;
                case 0x04:
                    nds->firmflashst.write_enable = false;
                    break;
                case 0x05:
                    nds->firmflashst.state = FIRMFLASH_STAT;
                    break;
                case 0x03:
                    nds->firmflashst.read = true;
                    nds->firmflashst.addr = 0;
                    nds->firmflashst.i = 0;
                    nds->firmflashst.state = FIRMFLASH_ADDR;
                    break;
                case 0x02:
                    nds->firmflashst.read = false;
                    nds->firmflashst.addr = 0;
                    nds->firmflashst.i = 0;
                    nds->firmflashst.state = FIRMFLASH_ADDR;
                    break;
                case 0x0b:
                    nds->firmflashst.read = true;
                    nds->firmflashst.addr = 0;
                    nds->firmflashst.i = 0;
                    nds->firmflashst.state = FIRMFLASH_ADDR;
                    break;
                case 0x0a:
                    nds->firmflashst.read = false;
                    nds->firmflashst.addr = 0;
                    nds->firmflashst.i = 0;
                    nds->firmflashst.state = FIRMFLASH_ADDR;
                    break;
                case 0x9f:
                    nds->firmflashst.state = FIRMFLASH_ID;
                    break;
            }
            break;
        case FIRMFLASH_ADDR:
            nds->firmflashst.addr <<= 8;
            nds->firmflashst.addr |= data;
            if (++nds->firmflashst.i == 3) {
                nds->firmflashst.i = 0;
                nds->firmflashst.state =
                    nds->firmflashst.read ? FIRMFLASH_READ : FIRMFLASH_WRITE;
            }
            break;
        case FIRMFLASH_READ:
            nds->io7.spidata = nds->firmware[nds->firmflashst.addr++];
            break;
        case FIRMFLASH_WRITE:
            nds->firmware[nds->firmflashst.addr++] = data;
            break;
        case FIRMFLASH_STAT:
            nds->io7.spidata = nds->firmflashst.write_enable ? 2 : 0;
            break;
        case FIRMFLASH_ID:
            nds->io7.spidata = 0xff;
            break;
    }
    if (!hold) {
        nds->firmflashst.state = FIRMFLASH_IDLE;
    }
}

void tsc_spi_write(NDS* nds, u8 data) {
    TSCCom com = {data};
    if (com.start) {
        switch (com.channel) {
            case 1:
                nds->tsc.data = nds->tsc.y << 7;
                break;
            case 5:
                nds->tsc.data = nds->tsc.x << 7;
                break;
            default:
                nds->tsc.data = 0;
                break;
        }
    } else {
        nds->io7.spidata = nds->tsc.data >> 8;
        nds->tsc.data <<= 8;
    }
}

void* get_vram(NDS* nds, VRAMRegion region, u32 addr) {
    switch (region) {
        case VRAMBGA: {
            if (nds->vramstate.bgA.e && addr < VRAMESIZE)
                return &nds->vramE[addr];
            VRAMBank abcd = nds->vramstate.bgA.abcd[(addr >> 17) & 3];
            if (abcd) return &nds->vrambanks[abcd - 1][addr % VRAMABCDSIZE];
            VRAMBank fg =
                nds->vramstate.bgA.fg[((addr >> 14) & 1) | ((addr >> 15) & 2)];
            if (fg) return &nds->vrambanks[fg - 1][addr % VRAMFGISIZE];
            break;
        }
        case VRAMBGB:
            if (nds->vramstate.bgB.c) return &nds->vramC[addr % VRAMABCDSIZE];
            if ((addr >> 15) & 1) {
                if (nds->vramstate.bgB.i)
                    return &nds->vramI[addr % VRAMFGISIZE];
            } else {
                if (nds->vramstate.bgB.h) return &nds->vramH[addr % VRAMHSIZE];
            }
            break;
        case VRAMOBJA: {
            if (nds->vramstate.objA.e && addr < VRAMESIZE)
                return &nds->vramE[addr];
            VRAMBank ab = nds->vramstate.objA.ab[(addr >> 17) & 1];
            if (ab) return &nds->vrambanks[ab - 1][addr % VRAMABCDSIZE];
            VRAMBank fg =
                nds->vramstate.objA.fg[((addr >> 14) & 1) | ((addr >> 15) & 2)];
            if (fg) return &nds->vrambanks[fg - 1][addr % VRAMFGISIZE];
            break;
        }
        case VRAMOBJB:
            if (nds->vramstate.objB.d) return &nds->vramD[addr % VRAMABCDSIZE];
            if (nds->vramstate.objB.i) return &nds->vramI[addr % VRAMFGISIZE];
            break;
        default: {
            int bound = 0;
            if (addr < (bound += VRAMABCDSIZE)) {
                if (nds->vramstate.lcdc[0]) return &nds->vram[addr];
            } else if (addr < (bound += VRAMABCDSIZE)) {
                if (nds->vramstate.lcdc[1]) return &nds->vram[addr];
            } else if (addr < (bound += VRAMABCDSIZE)) {
                if (nds->vramstate.lcdc[2]) return &nds->vram[addr];
            } else if (addr < (bound += VRAMABCDSIZE)) {
                if (nds->vramstate.lcdc[3]) return &nds->vram[addr];
            } else if (addr < (bound += VRAMESIZE)) {
                if (nds->vramstate.lcdc[4]) return &nds->vram[addr];
            } else if (addr < (bound += VRAMFGISIZE)) {
                if (nds->vramstate.lcdc[5]) return &nds->vram[addr];
            } else if (addr < (bound += VRAMFGISIZE)) {
                if (nds->vramstate.lcdc[6]) return &nds->vram[addr];
            } else if (addr < (bound += VRAMHSIZE)) {
                if (nds->vramstate.lcdc[7]) return &nds->vram[addr];
            } else if (addr < (bound += VRAMFGISIZE)) {
                if (nds->vramstate.lcdc[8]) return &nds->vram[addr];
            }
            break;
        }
    }
    return NULL;
}

#define VRAMREADDECL(size)                                                     \
    u##size vram_read##size(NDS* nds, VRAMRegion region, u32 addr) {           \
        u##size* p = get_vram(nds, region, addr);                              \
        return p ? *p : 0;                                                     \
    }

#define VRAMWRITEDECL(size)                                                    \
    void vram_write##size(NDS* nds, VRAMRegion region, u32 addr,               \
                          u##size data) {                                      \
        u##size* p = get_vram(nds, region, addr);                              \
        if (p) *p = data;                                                      \
    }

VRAMREADDECL(8)
VRAMREADDECL(16)
VRAMREADDECL(32)

void vram_write8(NDS* nds, VRAMRegion region, u32 addr, u8 data) {}
VRAMWRITEDECL(16)
VRAMWRITEDECL(32)