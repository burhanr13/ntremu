#include "nds.h"

#include <string.h>
#include <time.h>

#include "bus7.h"
#include "bus9.h"
#include "dldi.h"
#include "ppu.h"

void init_nds(NDS* nds, GameCard* card, u8* bios7, u8* bios9, u8* firmware,
              bool bootbios) {
    memset(nds, 0, sizeof *nds);
    nds->sched.master = nds;

    arm7_init(&nds->cpu7);
    nds->cpu7.master = nds;
    nds->dma7.master = nds;
    nds->tmc7.master = nds;
    nds->tmc7.io = &nds->io7;
    nds->tmc7.tm0_event = EVENT_TM07_RELOAD;

    nds->spu.master = nds;

    arm9_init(&nds->cpu9);
    nds->cpu9.master = nds;
    nds->dma9.master = nds;
    nds->tmc9.master = nds;
    nds->tmc9.io = &nds->io9;
    nds->tmc9.tm0_event = EVENT_TM09_RELOAD;

    nds->ppuA.master = nds;
    nds->ppuA.screen = nds->screen_top;
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
    nds->ppuB.screen = nds->screen_bottom;
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
    nds->gpu.texram[0] = nds->vramA;
    nds->gpu.texram[1] = nds->vramB;
    nds->gpu.texram[2] = nds->vramC;
    nds->gpu.texram[3] = nds->vramD;
    nds->gpu.texpal[0] = (u16*) nds->vramE;
    nds->gpu.texpal[1] = (u16*) nds->vramE + 0x2000;
    nds->gpu.texpal[2] = (u16*) nds->vramE + 0x4000;
    nds->gpu.texpal[3] = (u16*) nds->vramE + 0x6000;
    nds->gpu.texpal[4] = (u16*) nds->vramF;
    nds->gpu.texpal[5] = (u16*) nds->vramG;
    gpu_init_ptrs(&nds->gpu);

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

    *(u16*) &nds->wifi_io[0x000] = 0x1440;
    *(u16*) &nds->wifi_io[0x03c] = 0x0200;
    nds->wifi_bb_regs[0] = 0x6d;

    nds->next_vblank = NDS_SCREEN_H * DOTS_W * 6;

    nds->cur_cpu = (ArmCore*) &nds->cpu9;

    if (bootbios) {
        encrypt_securearea(card, (u32*) &bios7[0x30]);

        cpu_handle_exception((ArmCore*) &nds->cpu7, E_RESET);
        cpu_handle_exception((ArmCore*) &nds->cpu9, E_RESET);
    } else {

        nds->io7.wramstat = 3;
        nds->io9.wramcnt = 3;
        nds->io7.postflg = 1;
        nds->io9.postflg = 1;

        CardHeader* header = (CardHeader*) card->rom;

        *(u32*) &nds->ram[0x3ff800] = CHIPID;
        *(u32*) &nds->ram[0x3ff804] = CHIPID;
        *(u16*) &nds->ram[0x3ff808] = header->header_crc;
        *(u16*) &nds->ram[0x3ff80a] = header->secure_crc;
        *(u16*) &nds->ram[0x3ff850] = 0x5835;
        *(u32*) &nds->ram[0x3ff868] = (*(u16*) &firmware[0x20]) << 3;
        *(u16*) &nds->ram[0x3ff874] = 0x359a;
        *(u32*) &nds->ram[0x3ffc00] = CHIPID;
        *(u32*) &nds->ram[0x3ffc04] = CHIPID;
        *(u16*) &nds->ram[0x3ffc08] = header->header_crc;
        *(u16*) &nds->ram[0x3ffc0a] = header->secure_crc;
        *(u16*) &nds->ram[0x3ffc10] = 0x5835;
        *(u16*) &nds->ram[0x3ffc30] = 0xffff;
        *(u16*) &nds->ram[0x3ffc40] = 1;

        memcpy(&nds->ram[0x3ffc80], &firmware[0x3ff00], 0x70);

        memcpy(&nds->ram[0x3ffe00], header, sizeof *header);

        dldi_patch_binary(&card->rom[header->arm9_rom_offset],
                          header->arm9_size);
        for (int i = 0; i < header->arm9_size; i += 4) {
            bus9_write32(nds, header->arm9_ram_offset + i,
                         *(u32*) &card->rom[header->arm9_rom_offset + i]);
        }
        nds->cpu9.itcm_virtsize = 0x2000000;
        nds->cpu9.dtcm_base = 0x3000000;
        nds->cpu9.dtcm_virtsize = DTCMSIZE;
        nds->cpu9.c.pc = header->arm9_entry;
        nds->cpu9.c.sp = 0x3002f7c;
        nds->cpu9.c.banked_sp[B_IRQ] = 0x3003f80;
        nds->cpu9.c.banked_sp[B_SVC] = 0x3003fc0;
        nds->cpu9.c.lr = header->arm9_entry;
        nds->cpu9.c.cpsr.m = M_SYSTEM;
        cpu_flush((ArmCore*) &nds->cpu9);

        dldi_patch_binary(&card->rom[header->arm7_rom_offset],
                          header->arm7_size);
        for (int i = 0; i < header->arm7_size; i += 4) {
            bus7_write32(nds, header->arm7_ram_offset + i,
                         *(u32*) &card->rom[header->arm7_rom_offset + i]);
        }
        nds->cpu7.c.sp = 0x380fd80;
        nds->cpu7.c.banked_sp[B_IRQ] = 0x380ff80;
        nds->cpu7.c.banked_sp[B_SVC] = 0x380ffc0;
        nds->cpu7.c.lr = header->arm7_entry;
        nds->cpu7.c.pc = header->arm7_entry;
        nds->cpu7.c.cpsr.m = M_SYSTEM;
        cpu_flush((ArmCore*) &nds->cpu7);
    }

    lcd_hdraw(nds);
    spu_sample(&nds->spu);
}

void nds_run(NDS* nds) {
    while (nds->sched.now - nds->last_event < 512 &&
           !event_pending(&nds->sched)) {
        if (arm9_step(&nds->cpu9)) {
            nds->sched.now += nds->cpu9.c.cycles >> 1;
            if (!(nds->half_tick ^= nds->cpu9.c.cycles & 1)) {
                nds->sched.now++;
            }
        } else {
            nds->sched.now = FIFO_peek(nds->sched.event_queue).time;
            break;
        }
    }
    nds->cur_cpu = (ArmCore*) &nds->cpu7;
    nds->cur_cpu_type = CPU7;
    nds->sched.now = nds->last_event;
    while (nds->sched.now - nds->last_event < 512 &&
           !event_pending(&nds->sched)) {
        if (nds->halt7) {
            if (nds->io7.ie.w & nds->io7.ifl.w) {
                nds->halt7 = false;
                nds->io7.haltcnt = 0;
                nds->cpu7.c.irq = true;
            } else {
                nds->sched.now = FIFO_peek(nds->sched.event_queue).time;
                break;
            }
        } else {
            arm7_step(&nds->cpu7);
            nds->sched.now += nds->cpu7.c.cycles;
        }
    }
    run_to_present(&nds->sched);
    nds->cpu7.c.irq = nds->io7.ime && (nds->io7.ie.w & nds->io7.ifl.w);
    nds->cpu9.c.irq = nds->io9.ime && (nds->io9.ie.w & nds->io9.ifl.w);

    nds->cur_cpu = (ArmCore*) &nds->cpu9;
    nds->cur_cpu_type = CPU9;
    nds->last_event = nds->sched.now;
}

bool nds_step(NDS* nds) {
    if (nds->cur_cpu_type == CPU7) {
        if (nds->halt7) {
            if (nds->io7.ie.w & nds->io7.ifl.w) {
                nds->halt7 = false;
                nds->io7.haltcnt = 0;
                nds->cpu7.c.irq = true;
            } else {
                nds->sched.now = FIFO_peek(nds->sched.event_queue).time;
            }
        } else {
            arm7_step(&nds->cpu7);
            nds->sched.now += nds->cpu7.c.cycles;
        }
    } else {
        if (arm9_step(&nds->cpu9)) {
            nds->sched.now += nds->cpu9.c.cycles >> 1;
            if (nds->cpu9.c.cycles & 1) {
                if (nds->half_tick ^= 1) {
                    nds->sched.now++;
                }
            }
        } else {
            nds->sched.now = FIFO_peek(nds->sched.event_queue).time;
        }
    }
    if (nds->sched.now - nds->last_event >= 512 || event_pending(&nds->sched)) {
        if (nds->cur_cpu_type == CPU7) {
            run_to_present(&nds->sched);
            nds->cpu7.c.irq = nds->io7.ime && (nds->io7.ie.w & nds->io7.ifl.w);
            nds->cpu9.c.irq = nds->io9.ime && (nds->io9.ie.w & nds->io9.ifl.w);

            nds->cur_cpu = (ArmCore*) &nds->cpu9;
            nds->cur_cpu_type = CPU9;
            nds->last_event = nds->sched.now;
        } else {
            nds->cur_cpu = (ArmCore*) &nds->cpu7;
            nds->cur_cpu_type = CPU7;
            nds->sched.now = nds->last_event;
        }
        return true;
    }
    return false;
}

void firmware_spi_write(NDS* nds, u8 data, bool hold) {
    switch (nds->firmflashst.state) {
        case FIRMFLASHIDLE:
            switch (data) {
                case 0x06:
                    nds->firmflashst.write_enable = true;
                    break;
                case 0x04:
                    nds->firmflashst.write_enable = false;
                    break;
                case 0x05:
                    nds->firmflashst.state = FIRMFLASHSTAT;
                    break;
                case 0x03:
                    nds->firmflashst.read = true;
                    nds->firmflashst.addr = 0;
                    nds->firmflashst.i = 0;
                    nds->firmflashst.state = FIRMFLASHADDR;
                    break;
                case 0x02:
                    nds->firmflashst.read = false;
                    nds->firmflashst.addr = 0;
                    nds->firmflashst.i = 0;
                    nds->firmflashst.state = FIRMFLASHADDR;
                    break;
                case 0x0b:
                    nds->firmflashst.read = true;
                    nds->firmflashst.addr = 0;
                    nds->firmflashst.i = 0;
                    nds->firmflashst.state = FIRMFLASHADDR;
                    break;
                case 0x0a:
                    nds->firmflashst.read = false;
                    nds->firmflashst.addr = 0;
                    nds->firmflashst.i = 0;
                    nds->firmflashst.state = FIRMFLASHADDR;
                    break;
                case 0x9f:
                    nds->firmflashst.state = FIRMFLASHID;
                    break;
            }
            break;
        case FIRMFLASHADDR:
            nds->firmflashst.addr <<= 8;
            nds->firmflashst.addr |= data;
            if (++nds->firmflashst.i == 3) {
                nds->firmflashst.i = 0;
                nds->firmflashst.state =
                    nds->firmflashst.read ? FIRMFLASHREAD : FIRMFLASHWRITE;
            }
            break;
        case FIRMFLASHREAD:
            nds->io7.spidata = nds->firmware[nds->firmflashst.addr++];
            break;
        case FIRMFLASHWRITE:
            nds->firmware[nds->firmflashst.addr++] = data;
            break;
        case FIRMFLASHSTAT:
            nds->io7.spidata = nds->firmflashst.write_enable ? 2 : 0;
            break;
        case FIRMFLASHID:
            nds->io7.spidata = 0xff;
            break;
    }
    if (!hold) {
        nds->firmflashst.state = FIRMFLASHIDLE;
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

void rtc_write(NDS* nds) {
    if (!nds->io7.rtc.sel) {
        nds->rtc.i = 0;
        nds->rtc.bi = 0;
        nds->rtc.com = 0;
        return;
    }

    if (nds->rtc.i == 0) {
        if (nds->io7.rtc.clk) return;

        nds->rtc.com |= nds->io7.rtc.data << nds->rtc.bi;

        if (++nds->rtc.bi == 8) {
            nds->rtc.bi = -1;
            nds->rtc.i++;

            struct tm* t = localtime(&(time_t){time(NULL)});
            u8 year = (t->tm_year / 10 % 10) << 4 | t->tm_year % 10;
            u8 month = ((t->tm_mon + 1) / 10) << 4 | (t->tm_mon + 1) % 10;
            u8 day = (t->tm_mday / 10) << 4 | t->tm_mday % 10;
            u8 wday = t->tm_wday;
            u8 hour = (t->tm_hour / 10) << 4 | t->tm_hour % 10;
            if (t->tm_hour >= 12) hour |= 1 << 6;
            u8 min = (t->tm_min / 10) << 4 | t->tm_min % 10;
            u8 sec = (t->tm_sec / 10) << 4 | t->tm_sec % 10;

            switch ((nds->rtc.com >> 4) & 7) {
                case 2:
                    nds->rtc.data[0] = year;
                    nds->rtc.data[1] = month;
                    nds->rtc.data[2] = day;
                    nds->rtc.data[3] = wday;
                    nds->rtc.data[4] = hour;
                    nds->rtc.data[5] = min;
                    nds->rtc.data[6] = sec;
                    break;
                case 6:
                    nds->rtc.data[0] = hour;
                    nds->rtc.data[1] = min;
                    nds->rtc.data[2] = sec;
                    break;
                default:
                    nds->rtc.data[0] = 0;
            }
        }
    } else {
        if (nds->rtc.i < 8) {
            nds->io7.rtc.data =
                (nds->rtc.data[nds->rtc.i - 1] >> nds->rtc.bi) & 1;
        }

        if (nds->io7.rtc.clk) {
            if (++nds->rtc.bi == 8) {
                nds->rtc.bi = 0;
                nds->rtc.i++;
            }
        }
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