#include "io.h"

#include <math.h>
#include <stdio.h>

#include "nds.h"

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
    if (addr >= IO_SIZE) {
        if (addr == IPCFIFORECV || addr == IPCFIFORECV + 2 || addr == GAMECARDIN ||
            addr == GAMECARDIN + 2) {
            u32 w = io7_read32(io, addr & ~3);
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
            update_timer_count(&io->master->tmc7, i);
            return io->master->tmc7.counter[i];
            break;
        }
        default:
            return io->h[addr >> 1];
    }
}

void io7_write16(IO* io, u32 addr, u16 data) {
    if (addr >= IO_SIZE) return;
    if (addr == POSTFLG) {
        io7_write8(io, addr, data);
        io7_write8(io, addr | 1, data >> 8);
        return;
    }
    if (ROMCOMMAND <= addr && addr < ROMCOMMAND + 8) {
        io->h[addr >> 1] = data;
        if (card_write_command(io->master->card, io->romcommand)) {
            io->romctrl.busy = 1;
            io->romctrl.drq = 1;
        } else {
            io->romctrl.busy = 0;
            io->romctrl.drq = 0;
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
        case DMA3CNT + 2:
            io->h[addr >> 1] = data;
            int i = (addr - DMA0CNT - 2) / (DMA1CNT - DMA0CNT);
            io->dma[i].cnt.mode &= 6;
            if (io->dma[i].cnt.enable) dma7_enable(&io->master->dma7, i);
            break;
        case TM0CNT + 2:
        case TM1CNT + 2:
        case TM2CNT + 2:
        case TM3CNT + 2: {
            io->h[addr >> 1] = data;
            int i = (addr - TM0CNT - 2) / (TM1CNT - TM0CNT);
            if (i == 0) io->tm[0].cnt.countup = 0;
            update_timer_count(&io->master->tmc7, i);
            update_timer_reload(&io->master->tmc7, i);
            break;
        }
        case KEYINPUT:
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
                        (io->master->io9.ime & 1) && (io->master->io9.ie.w & io->master->io9.ifl.w);
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
            io->master->cpu7.irq = (io->ime & 1) && (io->ie.w & io->ifl.w);
            break;
        }
        case IPCFIFOSEND:
        case IPCFIFOSEND + 2:
            io->h[addr >> 1] = data;
            io7_write32(io, addr & ~3, io->ipcfifosend);
            break;
        case ROMCTRL + 2:
            io->h[addr >> 1] &= 0x8080;
            io->h[addr >> 1] |= data & 0x7f7f;
            u32 len = 0x100 << io->romctrl.blocksize;
            if (len == 0x100) len = 0;
            if (len == 0x8000) len = 4;
            io->master->card->len = len;
            break;
        case EXMEMCNT:
            io->exmemcnt.w &= 0xff80;
            io->exmemcnt.w |= data & 0x7f;
            break;
        case IME:
        case IE:
        case IE + 2:
            io->h[addr >> 1] = data;
            io->master->cpu7.irq = (io->ime & 1) && (io->ie.w & io->ifl.w);
            break;
        case IF:
            io9_write32(io, addr & ~3, data);
            break;
        case IF + 2:
            io9_write32(io, addr & ~3, data << 16);
            break;
        case VRAMSTAT:
            break;
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
                            io->master->cpu9.irq = (io->master->io9.ime & 1) &&
                                                   (io->master->io9.ie.w & io->master->io9.ifl.w);
                        }
                    }
                    io->ipcfifocnt.recvfull = 0;
                    io->master->io9.ipcfifocnt.sendfull = 0;
                    for (int i = 0; i < io->master->ipcfifo9to7_size; i++) {
                        io->master->ipcfifo9to7[i] = io->master->ipcfifo9to7[i + 1];
                    }
                }
                return data;
            } else {
                return io->master->ipcfifo9to7[0];
            }
            break;
        case GAMECARDIN: {
            u32 data;
            if (card_read_data(io->master->card, &data)) {
                io->romctrl.drq = 1;
                io->romctrl.busy = 1;
            } else {
                io->romctrl.drq = 0;
                io->romctrl.busy = 0;
                if (io->auxspicnt.irq) {
                    io->ifl.gamecardtrans = 1;
                    io->master->cpu7.irq = (io->ime & 1) && (io->ie.w & io->ifl.w);
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
                    io->master->ipcfifo7to9[io->master->ipcfifo7to9_size++] = data;
                    if (io->master->ipcfifo7to9_size == 16) {
                        io->ipcfifocnt.sendfull = 1;
                        io->master->io9.ipcfifocnt.recvfull = 1;
                    } else if (io->master->ipcfifo7to9_size == 1) {
                        io->ipcfifocnt.sendempty = 0;
                        io->master->io9.ipcfifocnt.recvempty = 0;
                        if (io->master->io9.ipcfifocnt.recv_irq) {
                            io->master->io9.ifl.ipcrecv = 1;
                            io->master->cpu9.irq = (io->master->io9.ime & 1) &&
                                                   (io->master->io9.ie.w & io->master->io9.ifl.w);
                        }
                    }
                }
            }
            break;
        case IF:
            io->ifl.w &= ~data;
            io->master->cpu7.irq = (io->ime & 1) && (io->ie.w & io->ifl.w);
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
    return &nds->vramstate.lcdc[bank - 1];
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
                VRAMBank* pre = get_vram_map(io->master, b, io->vramcnt[i].mst, io->vramcnt[i].ofs);
                if (*pre == b) *pre = VRAMNULL;
                if (b == VRAMC && io->vramcnt[i].mst == 2) {
                    io->master->io7.vramstat &= ~1;
                } else if (b == VRAMD && io->vramcnt[i].mst == 2) {
                    io->master->io7.vramstat &= ~2;
                }
            }
            io->vramcnt[i].b = data;
            if (io->vramcnt[i].enable) {
                *get_vram_map(io->master, b, io->vramcnt[i].mst, io->vramcnt[i].ofs) = b;
                if (b == VRAMC && io->vramcnt[i].mst == 2) {
                    io->master->io7.vramstat |= 1;
                } else if (b == VRAMD && io->vramcnt[i].mst == 2) {
                    io->master->io7.vramstat |= 2;
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
    if (addr >= IO_SIZE) {
        if (addr == IPCFIFORECV || addr == IPCFIFORECV + 2 || addr == GAMECARDIN ||
            addr == GAMECARDIN + 2) {
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
        default:
            return io->h[addr >> 1];
    }
}

void io9_write16(IO* io, u32 addr, u16 data) {
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
            io->divrem_result = op1;
        } else if (op1 == (1UL << 63) && op2 == -1) {
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
    if (ROMCOMMAND <= addr && addr < ROMCOMMAND + 8) {
        io->h[addr >> 1] = data;
        if (card_write_command(io->master->card, io->romcommand)) {
            io->romctrl.busy = 1;
            io->romctrl.drq = 1;
        } else {
            io->romctrl.busy = 0;
            io->romctrl.drq = 0;
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
        case DMA3CNT + 2:
            io->h[addr >> 1] = data;
            int i = (addr - DMA0CNT - 2) / (DMA1CNT - DMA0CNT);
            if (io->dma[i].cnt.enable) dma9_enable(&io->master->dma9, i);
            break;
        case TM0CNT + 2:
        case TM1CNT + 2:
        case TM2CNT + 2:
        case TM3CNT + 2: {
            io->h[addr >> 1] = data;
            int i = (addr - TM0CNT - 2) / (TM1CNT - TM0CNT);
            if (i == 0) io->tm[0].cnt.countup = 0;
            update_timer_count(&io->master->tmc9, i);
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
                        (io->master->io7.ime & 1) && (io->master->io7.ie.w & io->master->io7.ifl.w);
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
            io->master->cpu9.irq = (io->ime & 1) && (io->ie.w & io->ifl.w);
            break;
        }
        case IPCFIFOSEND:
        case IPCFIFOSEND + 2:
            io->h[addr >> 1] = data;
            io9_write32(io, addr & ~3, io->ipcfifosend);
            break;
        case ROMCTRL + 2:
            io->h[addr >> 1] &= 0x8080;
            io->h[addr >> 1] |= data & 0x7f7f;
            u32 len = 0x100 << io->romctrl.blocksize;
            if (len == 0x100) len = 0;
            if (len == 0x8000) len = 4;
            io->master->card->len = len;
            break;
        case EXMEMCNT:
            io->exmemcnt.w = data;
            io->master->io7.exmemcnt.w &= 0x7f;
            io->master->io7.exmemcnt.w |= data & 0xff80;
            break;
        case IME:
        case IE:
        case IE + 2:
            io->h[addr >> 1] = data;
            io->master->cpu9.irq = (io->ime & 1) && (io->ie.w & io->ifl.w);
            break;
        case IF:
            io9_write32(io, addr & ~3, data);
            break;
        case IF + 2:
            io9_write32(io, addr & ~3, data << 16);
            break;
        default:
            io->h[addr >> 1] = data;
    }
}

u32 io9_read32(IO* io, u32 addr) {
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
                            io->master->cpu7.irq = (io->master->io7.ime & 1) &&
                                                   (io->master->io7.ie.w & io->master->io7.ifl.w);
                        }
                    }
                    io->ipcfifocnt.recvfull = 0;
                    io->master->io7.ipcfifocnt.sendfull = 0;
                    for (int i = 0; i < io->master->ipcfifo7to9_size; i++) {
                        io->master->ipcfifo7to9[i] = io->master->ipcfifo7to9[i + 1];
                    }
                }
                return data;
            } else {
                return io->master->ipcfifo7to9[0];
            }
            break;
        case GAMECARDIN: {
            u32 data;
            if (card_read_data(io->master->card, &data)) {
                io->romctrl.drq = 1;
                io->romctrl.busy = 1;
            } else {
                io->romctrl.drq = 0;
                io->romctrl.busy = 0;
                if (io->auxspicnt.irq) {
                    io->ifl.gamecardtrans = 1;
                    io->master->cpu9.irq = (io->ime & 1) && (io->ie.w & io->ifl.w);
                }
            }
            return data;
        }
        default:
            return io9_read16(io, addr) | (io9_read16(io, addr | 2) << 16);
    }
}

void io9_write32(IO* io, u32 addr, u32 data) {
    switch (addr) {
        case IPCFIFOSEND:
            io->ipcfifosend = data;
            if (io->ipcfifocnt.fifo_enable) {
                if (io->master->ipcfifo9to7_size == 16) {
                    io->ipcfifocnt.error = 1;
                } else {
                    io->master->ipcfifo9to7[io->master->ipcfifo9to7_size++] = data;
                    if (io->master->ipcfifo9to7_size == 16) {
                        io->ipcfifocnt.sendfull = 1;
                        io->master->io7.ipcfifocnt.recvfull = 1;
                    } else if (io->master->ipcfifo9to7_size == 1) {
                        io->ipcfifocnt.sendempty = 0;
                        io->master->io7.ipcfifocnt.recvempty = 0;
                        if (io->master->io7.ipcfifocnt.recv_irq) {
                            io->master->io7.ifl.ipcrecv = 1;
                            io->master->cpu7.irq = (io->master->io7.ime & 1) &&
                                                   (io->master->io7.ie.w & io->master->io7.ifl.w);
                        }
                    }
                }
            }
            break;
        case IF:
            io->ifl.w &= ~data;
            io->master->cpu9.irq = (io->ime & 1) && (io->ie.w & io->ifl.w);
            break;
        default:
            io9_write16(io, addr, data);
            io9_write16(io, addr | 2, data >> 16);
    }
}