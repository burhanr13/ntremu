#include "io.h"

#include <math.h>
#include <stdio.h>

#include "bus7.h"
#include "nds.h"

#define UPDATE_IRQ(x)                                                          \
    (io->master->cpu##x.irq = io->ime && (io->ie.w & io->ifl.w))

u8 io7_read8(IO* io, u32 addr) {
    u16 h = io7_read16(io, addr & ~1);
    if (addr & 1) {
        return h >> 8;
    } else return h;
}

void io7_write8(IO* io, u32 addr, u8 data) {
    if (addr == POSTFLG) {
        io->postflg = data;
        return;
    }
    if (addr == HALTCNT) {
        io->haltcnt = data;
        if ((data >> 6) == 2) io->master->halt7 = true;
        if ((data >> 6) == 3) printf("sleep mode\n");
        return;
    }

    u16 h;
    if (addr & 1) {
        h = data << 8;
        h |= io->b[addr & ~1];
    } else {
        h = data;
        h |= io->b[addr | 1] << 8;
    }
    io7_write16(io, addr & ~1, h);
}

u16 io7_read16(IO* io, u32 addr) {
    if (AUXSPICNT <= addr && addr <= SEED1HI && !io->exmemcnt.ndscardrights)
        return 0;
    if (addr >= IO_SIZE) {
        if (addr == IPCFIFORECV || addr == IPCFIFORECV + 2 ||
            addr == GAMECARDIN || addr == GAMECARDIN + 2) {
            u32 w = io7_read32(io, addr & ~3);
            if (addr & 2) return w >> 16;
            else return w;
        }
        addr &= ~0x8000;
        if (WIFI_OFF <= addr && addr < WIFI_OFF + WIFIIOSIZE) {
            return *(u16*) &io->master->wifi_io[addr - WIFI_OFF];
        }
        if (WIFIRAM <= addr && addr < WIFIRAM + WIFIRAMSIZE) {
            return *(u16*) &io->master->wifiram[addr - WIFIRAM];
        }
        return 0;
    }
    switch (addr) {
        case TM0CNT:
        case TM1CNT:
        case TM2CNT:
        case TM3CNT: {
            int i = (addr - TM0CNT) / (TM1CNT - TM0CNT);
            update_timer_count(&io->master->tmc7, i);
            return io->master->tmc7.counter[i];
            break;
        }
        case AUXSPIDATA: {
            if (io->master->card) {
                return io->master->card->spidata;
            } else return 0;
            break;
        }
        case SPIDATA: {
            return io->spidata;
            break;
        }
        default:
            return io->h[addr >> 1];
    }
}

void io7_write16(IO* io, u32 addr, u16 data) {
    if (AUXSPICNT <= addr && addr <= SEED1HI && !io->exmemcnt.ndscardrights)
        return;
    if (addr >= IO_SIZE) {
        addr &= ~0x8000;
        if (WIFI_OFF <= addr && addr < WIFI_OFF + WIFIIOSIZE) {
            *(u16*) &io->master->wifi_io[addr - WIFI_OFF] = data;
            *(u16*) &io->master->wifi_io[0x03c] = 0x0200;
            if (addr - WIFI_OFF == 0x158) {
                u8 idx = data & 0xff;
                if (data >> 12 == 5) {
                    io->master->wifi_bb_regs[idx] = io->master->wifi_io[0x15a];
                } else if (data >> 12 == 6) {
                    io->master->wifi_io[0x15c] = io->master->wifi_bb_regs[idx];
                }
            }
        }
        if (WIFIRAM <= addr && addr < WIFIRAM + WIFIRAMSIZE) {
            *(u16*) &io->master->wifiram[addr - WIFIRAM] = data;
        }
        return;
    }
    if (addr == POSTFLG) {
        io7_write8(io, addr, data);
        io7_write8(io, addr | 1, data >> 8);
        return;
    }
    if (SOUND0CNT <= addr && addr < SOUNDCNT) {
        int i = (addr >> 4) & 0xf;
        bool prev_ena = io->sound[i].cnt.start;
        io->h[addr >> 1] = data;
        if (prev_ena != io->sound[i].cnt.start) {
            remove_event(&io->master->sched, EVENT_SPU_CH0 + i);
            if (!prev_ena) {
                io->master->spu.sample_ptrs[i] = io->sound[i].sad & 0xffffffc;
                if (io->sound[i].cnt.format == SND_ADPCM) {
                    io->master->spu.adpcm_hi[i] = false;
                    u32 adpcm_init =
                        bus7_read32(io->master, io->master->spu.sample_ptrs[i]);
                    io->master->spu.sample_ptrs[i] += 4;
                    io->master->spu.adpcm_sample[i] =
                        (s16) adpcm_init / (float) 0x8000;
                    int ind = (adpcm_init >> 16) & 0x7f;
                    if (ind > 88) ind = 88;
                    io->master->spu.adpcm_idx[i] = ind;
                } else if (i >= 14) {
                    io->master->spu.psg_lfsr[i - 14] = 0x7fff;
                }
                spu_tick_channel(&io->master->spu, i);
            }
        }
        return;
    }
    switch (addr) {
        case DISPSTAT:
            data &= ~0b111;
            io->dispstat.h &= 0b111;
            io->dispstat.h |= data;
            break;
        case VCOUNT:
            break;
        case DMA0CNT + 2:
        case DMA1CNT + 2:
        case DMA2CNT + 2:
        case DMA3CNT + 2: {
            int i = (addr - DMA0CNT - 2) / (DMA1CNT - DMA0CNT);
            bool prev_ena = io->dma[i].cnt.enable;
            io->h[addr >> 1] = data;
            io->dma[i].cnt.mode &= 6;
            if (!prev_ena && io->dma[i].cnt.enable)
                dma7_enable(&io->master->dma7, i);
            break;
        }
        case TM0CNT + 2:
        case TM1CNT + 2:
        case TM2CNT + 2:
        case TM3CNT + 2: {
            int i = (addr - TM0CNT - 2) / (TM1CNT - TM0CNT);
            update_timer_count(&io->master->tmc7, i);
            io->h[addr >> 1] = data;
            update_timer_reload(&io->master->tmc7, i);
            break;
        }
        case KEYINPUT:
        case EXTKEYIN:
            break;
        case RTC:
            io->rtc.w = data;
            rtc_write(io->master);
            break;
        case IPCSYNC:
            data &= ~0xf;
            io->ipcsync.w &= 0xf;
            io->ipcsync.w |= data;
            io->master->io9.ipcsync.in = io->ipcsync.out;
            if (io->ipcsync.irqsend) {
                io->ipcsync.irqsend = 0;
                if (io->master->io9.ipcsync.irq) {
                    io->master->io9.ifl.ipcsync = 1;
                    io->master->cpu9.irq =
                        io->master->io9.ime &&
                        (io->master->io9.ie.w & io->master->io9.ifl.w);
                }
            }
            break;
        case IPCFIFOCNT: {
            u16 mask = 0x8404;
            io->ipcfifocnt.w &= ~mask;
            io->ipcfifocnt.w |= data & mask;
            if (data & (1 << 14)) io->ipcfifocnt.error = 0;
            if (data & (1 << 3)) {
                io->master->ipcfifo7to9_size = 0;
                io->master->ipcfifo7to9[0] = 0;
                io->ipcfifocnt.sendempty = 1;
                io->ipcfifocnt.sendfull = 0;
                io->master->io9.ipcfifocnt.recvempty = 1;
                io->master->io9.ipcfifocnt.recvfull = 0;
            }
            if (io->ipcfifocnt.send_irq && io->ipcfifocnt.sendempty) {
                io->ifl.ipcsend = 1;
            }
            if (io->ipcfifocnt.recv_irq && !io->ipcfifocnt.recvempty) {
                io->ifl.ipcrecv = 1;
            }
            UPDATE_IRQ(7);
            break;
        }
        case IPCFIFOSEND:
        case IPCFIFOSEND + 2:
            io->h[addr >> 1] = data;
            io7_write32(io, addr & ~3, io->ipcfifosend);
            break;
        case AUXSPICNT:
            io->auxspicnt.h = data;
            io->auxspicnt.busy = 0;
            break;
        case AUXSPIDATA:
            card_spi_write(io->master->card, data, io->auxspicnt.hold);
            break;
        case ROMCTRL + 2:
            io->h[addr >> 1] = data;
            io->romctrl.drq = 0;
            if (io->romctrl.busy) {
                io->romctrl.busy = 0;
                bool key1com = io->master->card->key1mode;
                if (card_write_command(io->master->card, io->romcommand)) {
                    io->romctrl.busy = 1;
                    add_event(&io->master->sched, EVENT_CARD_DRQ, 20);
                } else if (key1com) {
                    io->ifl.gamecardtrans = 1;
                    UPDATE_IRQ(7);
                }
            }
            break;
        case SPICNT:
            io->spicnt.h = data;
            io->spicnt.busy = 0;
            break;
        case SPIDATA:
            switch (io->spicnt.dev) {
                case 1:
                    firmware_spi_write(io->master, data, io->spicnt.hold);
                    break;
                case 2:
                    tsc_spi_write(io->master, data);
                    break;
            }
            if (io->spicnt.irq) {
                io->ifl.spi = 1;
                UPDATE_IRQ(7);
            }
            break;
        case EXMEMCNT:
            io->exmemcnt.h &= 0xff80;
            io->exmemcnt.h |= data & 0x7f;
            break;
        case IME:
            io->ime = data & 1;
            UPDATE_IRQ(7);
            break;
        case IE:
        case IE + 2:
            io->h[addr >> 1] = data;
            UPDATE_IRQ(7);
            break;
        case IF:
            io7_write32(io, addr & ~3, data);
            break;
        case IF + 2:
            io7_write32(io, addr & ~3, data << 16);
            break;
        case VRAMSTAT:
            break;
        case SNDCAP0CNT: {
            bool prev_ena[2] = {io->sndcapcnt[0].start, io->sndcapcnt[1].start};
            io->h[addr >> 1] = data;
            for (int i = 0; i < 2; i++) {
                if (prev_ena[i] != io->sndcapcnt[i].start) {
                    remove_event(&io->master->sched, EVENT_SPU_CAP0 + i);
                    if (!prev_ena[i]) {
                        io->master->spu.capture_ptrs[i] =
                            io->sndcap[i].dad & 0xffffffc;
                        spu_tick_capture(&io->master->spu, i);
                    }
                }
            }
            break;
        }
        default:
            io->h[addr >> 1] = data;
    }
}

u32 io7_read32(IO* io, u32 addr) {
    switch (addr) {
        case IPCFIFORECV:
            if (io->ipcfifocnt.fifo_enable) {
                u32 data = io->master->ipcfifo9to7[0];
                if (io->master->ipcfifo9to7_size == 0) {
                    io->ipcfifocnt.error = 1;
                } else {
                    if (--io->master->ipcfifo9to7_size == 0) {
                        io->ipcfifocnt.recvempty = 1;
                        io->master->io9.ipcfifocnt.sendempty = 1;
                        if (io->master->io9.ipcfifocnt.send_irq) {
                            io->master->io9.ifl.ipcsend = 1;
                            io->master->cpu9.irq =
                                io->master->io9.ime &&
                                (io->master->io9.ie.w & io->master->io9.ifl.w);
                        }
                    }
                    io->ipcfifocnt.recvfull = 0;
                    io->master->io9.ipcfifocnt.sendfull = 0;
                    for (int i = 0; i < io->master->ipcfifo9to7_size; i++) {
                        io->master->ipcfifo9to7[i] =
                            io->master->ipcfifo9to7[i + 1];
                    }
                }
                return data;
            } else {
                return io->master->ipcfifo9to7[0];
            }
            break;
        case GAMECARDIN: {
            if (!io->exmemcnt.ndscardrights) return -1;

            u32 data = -1;
            if (io->romctrl.drq) {
                io->romctrl.drq = 0;
                if (card_read_data(io->master->card, &data)) {
                    add_event(&io->master->sched, EVENT_CARD_DRQ,
                              io->master->sched.now + 20);
                } else {
                    io->romctrl.busy = 0;
                    io->ifl.gamecardtrans = 1;
                    UPDATE_IRQ(7);
                }
            }
            return data;
        }
        default:
            return io7_read16(io, addr) | (io7_read16(io, addr | 2) << 16);
    }
}

void io7_write32(IO* io, u32 addr, u32 data) {
    switch (addr) {
        case IPCFIFOSEND:
            io->ipcfifosend = data;
            if (io->ipcfifocnt.fifo_enable) {
                if (io->master->ipcfifo7to9_size == 16) {
                    io->ipcfifocnt.error = 1;
                } else {
                    io->master->ipcfifo7to9[io->master->ipcfifo7to9_size++] =
                        data;
                    if (io->master->ipcfifo7to9_size == 16) {
                        io->ipcfifocnt.sendfull = 1;
                        io->master->io9.ipcfifocnt.recvfull = 1;
                    } else if (io->master->ipcfifo7to9_size == 1) {
                        io->ipcfifocnt.sendempty = 0;
                        io->master->io9.ipcfifocnt.recvempty = 0;
                        if (io->master->io9.ipcfifocnt.recv_irq) {
                            io->master->io9.ifl.ipcrecv = 1;
                            io->master->cpu9.irq =
                                io->master->io9.ime &&
                                (io->master->io9.ie.w & io->master->io9.ifl.w);
                        }
                    }
                }
            }
            break;
        case IF:
            io->ifl.w &= ~data;
            UPDATE_IRQ(7);
            break;
        default:
            io7_write16(io, addr, data);
            io7_write16(io, addr | 2, data >> 16);
    }
}

u8 io9_read8(IO* io, u32 addr) {
    u16 h = io9_read16(io, addr & ~1);
    if (addr & 1) {
        return h >> 8;
    } else return h;
}

VRAMBank* get_vram_map(NDS* nds, VRAMBank bank, int mst, int ofs) {
    if (mst == 0) {
        return &nds->vramstate.lcdc[bank - 1];
    }
    switch (bank) {
        case VRAMA:
            switch (mst) {
                case 1:
                    return &nds->vramstate.bgA.abcd[ofs];
                case 2:
                    return &nds->vramstate.objA.ab[ofs];
            }
            break;
        case VRAMB:
            switch (mst) {
                case 1:
                    return &nds->vramstate.bgA.abcd[ofs];
                case 2:
                    return &nds->vramstate.objA.ab[ofs];
            }
            break;
        case VRAMC:
            switch (mst) {
                case 1:
                    return &nds->vramstate.bgA.abcd[ofs];
                case 2:
                    return &nds->vramstate.arm7[ofs];
                case 4:
                    return &nds->vramstate.bgB.c;
            }
            break;
        case VRAMD:
            switch (mst) {
                case 1:
                    return &nds->vramstate.bgA.abcd[ofs];
                case 2:
                    return &nds->vramstate.arm7[ofs];
                case 4:
                    return &nds->vramstate.objB.d;
            }
            break;
        case VRAME:
            switch (mst) {
                case 1:
                    return &nds->vramstate.bgA.e;
                case 2:
                    return &nds->vramstate.objA.e;
            }
            break;
        case VRAMF:
            switch (mst) {
                case 1:
                    return &nds->vramstate.bgA.fg[ofs];
                case 2:
                    return &nds->vramstate.objA.fg[ofs];
            }
            break;
        case VRAMG:
            switch (mst) {
                case 1:
                    return &nds->vramstate.bgA.fg[ofs];
                case 2:
                    return &nds->vramstate.objA.fg[ofs];
            }
            break;
        case VRAMH:
            switch (mst) {
                case 1:
                    return &nds->vramstate.bgB.h;
            }
            break;
        case VRAMI:
            switch (mst) {
                case 1:
                    return &nds->vramstate.bgB.i;
                case 2:
                    return &nds->vramstate.objB.i;
            }
            break;
        default:
            break;
    }
    return NULL;
}

void vram_map_ppu(NDS* nds, VRAMBank bank, int mst, int ofs) {
    switch (bank) {
        case VRAMA:
        case VRAMB:
        case VRAMC:
        case VRAMD:
            if (mst == 3) {
                nds->gpu.texram[ofs] = nds->vrambanks[bank - 1];
            }
            break;
        case VRAME:
            switch (mst) {
                case 3:
                    nds->gpu.texpal[0] = (u16*) nds->vramE;
                    nds->gpu.texpal[1] = (u16*) nds->vramE + 0x2000;
                    nds->gpu.texpal[2] = (u16*) nds->vramE + 0x4000;
                    nds->gpu.texpal[3] = (u16*) nds->vramE + 0x6000;
                    break;
                case 4:
                    nds->ppuA.extPalBg[0] = (u16*) nds->vramE;
                    nds->ppuA.extPalBg[1] = (u16*) nds->vramE + 0x1000;
                    nds->ppuA.extPalBg[2] = (u16*) nds->vramE + 0x2000;
                    nds->ppuA.extPalBg[3] = (u16*) nds->vramE + 0x3000;
                    break;
            }
            break;
        case VRAMF:
        case VRAMG:
            switch (mst) {
                case 3:
                    nds->gpu.texpal[((ofs & 2) << 1) + (ofs & 1)] =
                        (u16*) nds->vrambanks[bank - 1];
                    break;
                case 4:
                    nds->ppuA.extPalBg[2 * ofs] =
                        (u16*) nds->vrambanks[bank - 1];
                    nds->ppuA.extPalBg[2 * ofs + 1] =
                        (u16*) nds->vrambanks[bank - 1] + 0x1000;
                    break;
                case 5:
                    nds->ppuA.extPalObj = (u16*) nds->vrambanks[bank - 1];
                    break;
            }
            break;
        default:
            break;
    }
}

void io9_write8(IO* io, u32 addr, u8 data) {
    switch (addr) {
        case VRAMCNT_A:
        case VRAMCNT_B:
        case VRAMCNT_C:
        case VRAMCNT_D:
        case VRAMCNT_E:
        case VRAMCNT_F:
        case VRAMCNT_G:
        case VRAMCNT_H:
        case VRAMCNT_I: {
            int i = addr - VRAMCNT_A;
            VRAMBank b = i + 1;
            if (b > VRAMG) b--;
            if (io->vramcnt[i].enable) {
                VRAMBank* pre = get_vram_map(io->master, b, io->vramcnt[i].mst,
                                             io->vramcnt[i].ofs);
                if (pre && *pre == b) *pre = VRAMNULL;
                if (b == VRAMC && io->vramcnt[i].mst == 2) {
                    io->master->io7.vramstat &= ~1;
                } else if (b == VRAMD && io->vramcnt[i].mst == 2) {
                    io->master->io7.vramstat &= ~2;
                }
            }
            io->vramcnt[i].b = data;
            if (io->vramcnt[i].enable) {

                if (b == VRAMC && io->vramcnt[i].mst == 2) {
                    io->master->io7.vramstat |= 1;
                } else if (b == VRAMD && io->vramcnt[i].mst == 2) {
                    io->master->io7.vramstat |= 2;
                }

                VRAMBank* post = get_vram_map(io->master, b, io->vramcnt[i].mst,
                                              io->vramcnt[i].ofs);
                if (post) {
                    *post = b;
                } else {
                    vram_map_ppu(io->master, b, io->vramcnt[i].mst,
                                 io->vramcnt[i].ofs);
                }
            }
            break;
        }
        case WRAMCNT:
            io->wramcnt = data & 3;
            io->master->io7.wramstat = io->wramcnt;
            break;
        default: {
            u16 h;
            if (addr & 1) {
                h = data << 8;
                h |= io->b[addr & ~1];
            } else {
                h = data;
                h |= io->b[addr | 1] << 8;
            }
            io9_write16(io, addr & ~1, h);
        }
    }
}

u16 io9_read16(IO* io, u32 addr) {
    if (AUXSPICNT <= addr && addr <= SEED1HI && io->exmemcnt.ndscardrights)
        return 0;
    if (addr >= IO_SIZE) {
        if (addr == IPCFIFORECV || addr == IPCFIFORECV + 2 ||
            addr == GAMECARDIN || addr == GAMECARDIN + 2) {
            u32 w = io9_read32(io, addr & ~3);
            if (addr & 2) return w >> 16;
            else return w;
        }
        return 0;
    }
    switch (addr) {
        case TM0CNT:
        case TM1CNT:
        case TM2CNT:
        case TM3CNT: {
            int i = (addr - TM0CNT) / (TM1CNT - TM0CNT);
            update_timer_count(&io->master->tmc9, i);
            return io->master->tmc9.counter[i];
            break;
        }
        case AUXSPIDATA: {
            if (io->master->card) {
                return io->master->card->spidata;
            } else return 0;
            break;
        }
        default:
            return io->h[addr >> 1];
    }
}

void io9_write16(IO* io, u32 addr, u16 data) {
    if (AUXSPICNT <= addr && addr <= SEED1HI && io->exmemcnt.ndscardrights)
        return;
    if (addr >= IO_SIZE) return;
    if (VRAMCNT_A <= addr && addr <= VRAMCNT_I) {
        io9_write8(io, addr, data & 0xff);
        io9_write8(io, addr | 1, data >> 8);
        return;
    }
    if (addr == DIVCNT || (DIV_NUMER <= addr && addr < DIV_DENOM + 8)) {
        io->h[addr >> 1] = data;

        if (io->div_denom == 0) {
            io->divcnt.error = 1;
        } else {
            io->divcnt.error = 0;
        }

        s64 op1 = io->div_numer, op2 = io->div_denom;

        switch (io->divcnt.mode) {
            case 0:
                op1 = (s32) op1;
                op2 = (s32) op2;
                break;
            case 1:
                op2 = (s32) op2;
                break;
        }

        if (op2 == 0) {
            io->div_result = (op1 < 0) ? 1 : -1;
            if (!io->divcnt.mode) io->div_result ^= 0xffffffff00000000;
            io->divrem_result = op1;
        } else if (op1 == (1l << 63) && op2 == -1) {
            io->div_result = op1;
            io->divrem_result = 0;
        } else {
            io->div_result = op1 / op2;
            io->divrem_result = op1 % op2;
        }

        io->divcnt.busy = 0;
        return;
    }
    if (addr == SQRTCNT || (SQRT_PARAM <= addr && addr < SQRT_PARAM + 8)) {
        io->h[addr >> 1] = data;

        if (io->sqrtcnt.mode) {
            io->sqrt_result = sqrtl(io->sqrt_param);
        } else {
            io->sqrt_result = sqrt((u32) io->sqrt_param);
        }

        io->sqrtcnt.busy = 0;
        return;
    }
    if (GXFIFO <= addr && addr < GXSTAT) {
        io->h[addr >> 1] = data;
        io9_write32(io, addr & ~3, io->w[addr >> 2]);
        return;
    }
    switch (addr) {
        case DISPSTAT:
            data &= ~0b111;
            io->dispstat.h &= 0b111;
            io->dispstat.h |= data;
            break;
        case VCOUNT:
            break;
        case DISP3DCNT:
            io->disp3dcnt.w = data;
            if (io->disp3dcnt.rdlines_underflow)
                io->disp3dcnt.rdlines_underflow = 0;
            if (io->disp3dcnt.ram_overflow) io->disp3dcnt.ram_overflow = 0;
            break;
        case DMA0CNT + 2:
        case DMA1CNT + 2:
        case DMA2CNT + 2:
        case DMA3CNT + 2: {
            int i = (addr - DMA0CNT - 2) / (DMA1CNT - DMA0CNT);
            bool prev_ena = io->dma[i].cnt.enable;
            io->h[addr >> 1] = data;
            if (!prev_ena && io->dma[i].cnt.enable)
                dma9_enable(&io->master->dma9, i);
            break;
        }
        case TM0CNT + 2:
        case TM1CNT + 2:
        case TM2CNT + 2:
        case TM3CNT + 2: {
            int i = (addr - TM0CNT - 2) / (TM1CNT - TM0CNT);
            update_timer_count(&io->master->tmc9, i);
            io->h[addr >> 1] = data;
            update_timer_reload(&io->master->tmc9, i);
            break;
        }
        case KEYINPUT:
            break;
        case IPCSYNC:
            data &= ~0xf;
            io->ipcsync.w &= 0xf;
            io->ipcsync.w |= data;
            io->master->io7.ipcsync.in = io->ipcsync.out;
            if (io->ipcsync.irqsend) {
                io->ipcsync.irqsend = 0;
                if (io->master->io7.ipcsync.irq) {
                    io->master->io7.ifl.ipcsync = 1;
                    io->master->cpu7.irq =
                        io->master->io7.ime &&
                        (io->master->io7.ie.w & io->master->io7.ifl.w);
                }
            }
            break;
        case IPCFIFOCNT: {
            u16 mask = 0x8404;
            io->ipcfifocnt.w &= ~mask;
            io->ipcfifocnt.w |= data & mask;
            if (data & (1 << 14)) io->ipcfifocnt.error = 0;
            if (data & (1 << 3)) {
                io->master->ipcfifo9to7_size = 0;
                io->master->ipcfifo9to7[0] = 0;
                io->ipcfifocnt.sendempty = 1;
                io->ipcfifocnt.sendfull = 0;
                io->master->io7.ipcfifocnt.recvempty = 1;
                io->master->io7.ipcfifocnt.recvfull = 0;
            }
            if (io->ipcfifocnt.send_irq && io->ipcfifocnt.sendempty) {
                io->ifl.ipcsend = 1;
            }
            if (io->ipcfifocnt.recv_irq && !io->ipcfifocnt.recvempty) {
                io->ifl.ipcrecv = 1;
            }
            UPDATE_IRQ(9);
            break;
        }
        case IPCFIFOSEND:
        case IPCFIFOSEND + 2:
            io->h[addr >> 1] = data;
            io9_write32(io, addr & ~3, io->ipcfifosend);
            break;
        case AUXSPICNT:
            io->auxspicnt.h = data;
            io->auxspicnt.busy = 0;
            break;
        case AUXSPIDATA:
            card_spi_write(io->master->card, data, io->auxspicnt.hold);
            break;
        case ROMCTRL + 2:
            io->h[addr >> 1] = data;
            io->romctrl.drq = 0;
            if (io->romctrl.busy) {
                io->romctrl.busy = 0;
                if (card_write_command(io->master->card, io->romcommand)) {
                    io->romctrl.busy = 1;
                    add_event(&io->master->sched, EVENT_CARD_DRQ,
                              io->master->sched.now + 20);
                }
            }
            break;
        case EXMEMCNT:
            io->exmemcnt.h = data;
            io->master->io7.exmemcnt.h &= 0x7f;
            io->master->io7.exmemcnt.h |= data & 0xff80;
            break;
        case IME:
            io->ime = data & 1;
            UPDATE_IRQ(9);
            break;
        case IE:
        case IE + 2:
            io->h[addr >> 1] = data;
            UPDATE_IRQ(9);
            break;
        case IF:
            io9_write32(io, addr & ~3, data);
            break;
        case IF + 2:
            io9_write32(io, addr & ~3, data << 16);
            break;
        case GXSTAT:
            if (data & (1 << 15)) io->gxstat.mtxstk_error = 0;
            break;
        case GXSTAT + 2:
            io->h[addr >> 1] &= 0x3fff;
            io->h[addr >> 1] |= data & 0xc000;
            io->ifl.gxfifo = ((io->gxstat.gxfifo_half && io->gxstat.irq_half) ||
                              (io->gxstat.gxfifo_empty && io->gxstat.irq_empty))
                                 ? 1
                                 : 0;
            UPDATE_IRQ(9);
            break;
        default:
            io->h[addr >> 1] = data;
    }
}

u32 io9_read32(IO* io, u32 addr) {
    if (CLIPMTX_RESULT <= addr && addr < VECMTX_RESULT + 0x24) {
        update_mtxs(&io->master->gpu);
    }
    switch (addr) {
        case IPCFIFORECV:
            if (io->ipcfifocnt.fifo_enable) {
                u32 data = io->master->ipcfifo7to9[0];
                if (io->master->ipcfifo7to9_size == 0) {
                    io->ipcfifocnt.error = 1;
                } else {
                    if (--io->master->ipcfifo7to9_size == 0) {
                        io->ipcfifocnt.recvempty = 1;
                        io->master->io7.ipcfifocnt.sendempty = 1;
                        if (io->master->io7.ipcfifocnt.send_irq) {
                            io->master->io7.ifl.ipcsend = 1;
                            io->master->cpu7.irq =
                                io->master->io7.ime &&
                                (io->master->io7.ie.w & io->master->io7.ifl.w);
                        }
                    }
                    io->ipcfifocnt.recvfull = 0;
                    io->master->io7.ipcfifocnt.sendfull = 0;
                    for (int i = 0; i < io->master->ipcfifo7to9_size; i++) {
                        io->master->ipcfifo7to9[i] =
                            io->master->ipcfifo7to9[i + 1];
                    }
                }
                return data;
            } else {
                return io->master->ipcfifo7to9[0];
            }
            break;
        case GAMECARDIN: {
            if (io->exmemcnt.ndscardrights) return -1;

            u32 data = -1;
            if (io->romctrl.drq) {
                io->romctrl.drq = 0;
                if (card_read_data(io->master->card, &data)) {
                    add_event(&io->master->sched, EVENT_CARD_DRQ,
                              io->master->sched.now + 20);
                } else {
                    io->romctrl.busy = 0;
                    io->ifl.gamecardtrans = 1;
                    UPDATE_IRQ(9);
                }
            }
            return data;
        }
        default:
            return io9_read16(io, addr) | (io9_read16(io, addr | 2) << 16);
    }
}

void io9_write32(IO* io, u32 addr, u32 data) {
    if (GXFIFO <= addr && addr < GXSTAT) {
        u8 com = addr >> 2;
        if (com < 0x10) {
            gxfifo_write(&io->master->gpu, data);
        } else {
            if (!io->master->gpu.params_pending)
                gxfifo_write(&io->master->gpu, com);
            gxfifo_write(&io->master->gpu, data);
        }
        return;
    }
    switch (addr) {
        case IPCFIFOSEND:
            io->ipcfifosend = data;
            if (io->ipcfifocnt.fifo_enable) {
                if (io->master->ipcfifo9to7_size == 16) {
                    io->ipcfifocnt.error = 1;
                } else {
                    io->master->ipcfifo9to7[io->master->ipcfifo9to7_size++] =
                        data;
                    if (io->master->ipcfifo9to7_size == 16) {
                        io->ipcfifocnt.sendfull = 1;
                        io->master->io7.ipcfifocnt.recvfull = 1;
                    } else if (io->master->ipcfifo9to7_size == 1) {
                        io->ipcfifocnt.sendempty = 0;
                        io->master->io7.ipcfifocnt.recvempty = 0;
                        if (io->master->io7.ipcfifocnt.recv_irq) {
                            io->master->io7.ifl.ipcrecv = 1;
                            io->master->cpu7.irq =
                                io->master->io7.ime &&
                                (io->master->io7.ie.w & io->master->io7.ifl.w);
                        }
                    }
                }
            }
            break;
        case IF:
            io->ifl.w &= ~data;
            io->ifl.gxfifo = ((io->gxstat.gxfifo_half && io->gxstat.irq_half) ||
                              (io->gxstat.gxfifo_empty && io->gxstat.irq_empty))
                                 ? 1
                                 : 0;
            UPDATE_IRQ(9);
            break;
        default:
            io9_write16(io, addr, data);
            io9_write16(io, addr | 2, data >> 16);
    }
}