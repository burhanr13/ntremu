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
    if (addr >= IO_SIZE) return 0;
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
        default:
            io->h[addr >> 1] = data;
    }
}

u32 io7_read32(IO* io, u32 addr) {
    return io7_read16(io, addr) | (io7_read16(io, addr | 2) << 16);
}

void io7_write32(IO* io, u32 addr, u32 data) {
    switch (addr) {
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

u16 io9_read16(IO* io, u32 addr) {
    if (addr >= IO_SIZE) return 0;
    return io->h[addr >> 1];
}

void io9_write16(IO* io, u32 addr, u16 data) {
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
    return io9_read16(io, addr) | (io9_read16(io, addr | 2) << 16);
}

void io9_write32(IO* io, u32 addr, u32 data) {
    switch (addr) {
        case DMA3CNT:
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