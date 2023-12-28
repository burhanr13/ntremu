#include "io.h"

#include <stdio.h>

#include "nds.h"

u8 io7_read8(IO* io, u32 addr) {
    u16 h = io7_read16(io, addr & ~1);
    if (addr & 1) {
        return h >> 8;
    } else return h;
}

void io7_write8(IO* io, u32 addr, u8 data) {
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
        if (addr == IPCFIFORECV || addr == IPCFIFORECV + 2) {
            u32 w = io7_read32(io, addr & ~3);
            if (addr & 2) return w >> 16;
            else return w;
        }
        return 0;
    }
    return io->h[addr >> 1];
}

void io7_write16(IO* io, u32 addr, u16 data) {
    if (addr >= IO_SIZE) return;
    switch (addr) {
        case DISPSTAT:
            data &= ~0b111;
            io->dispstat.h &= 0b111;
            io->dispstat.h |= data;
            break;
        case VCOUNT:
            break;
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
            break;
        }
        case IPCFIFOSEND:
        case IPCFIFOSEND + 2:
            io->h[addr >> 1] = data;
            io7_write32(io, addr & ~3, io->ipcfifosend);
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

void io9_write8(IO* io, u32 addr, u8 data) {
    switch (addr) {
        case VRAMCNT_A:
            break;
        case VRAMCNT_B:
            break;
        case VRAMCNT_C:
            break;
        case VRAMCNT_D:
            break;
        case VRAMCNT_E:
            break;
        case VRAMCNT_F:
            break;
        case VRAMCNT_G:
            break;
        case WRAMCNT:
            io->wramcnt = data & 3;
            io->master->io7.wramstat = io->wramcnt;
            break;
        case VRAMCNT_H:
            break;
        case VRAMCNT_I:
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
        if (addr == IPCFIFORECV || addr == IPCFIFORECV + 2) {
            u32 w = io9_read32(io, addr & ~3);
            if (addr & 2) return w >> 16;
            else return w;
        }
        return 0;
    }
    return io->h[addr >> 1];
}

void io9_write16(IO* io, u32 addr, u16 data) {
    if (addr >= IO_SIZE) return;
    if (VRAMCNT_A <= addr && addr <= VRAMCNT_I) {
        io9_write8(io, addr, data & 0xff);
        io9_write8(io, addr | 1, data >> 8);
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
            break;
        }
        case IPCFIFOSEND:
        case IPCFIFOSEND + 2:
            io->h[addr >> 1] = data;
            io9_write32(io, addr & ~3, io->ipcfifosend);
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
        default:
            return io9_read16(io, addr) | (io9_read16(io, addr | 2) << 16);
    }
}

void io9_write32(IO* io, u32 addr, u32 data) {
    switch (addr) {
        case DMA3CNT:
            break;
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