#include "io.h"

#include <stdio.h>

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
    return io->h[addr >> 1];
}

void io7_write16(IO* io, u32 addr, u16 data) {
    switch (addr) {
        default:
            io->h[addr >> 1] = data;
    }
}

u32 io7_read32(IO* io, u32 addr) {
    return io7_read16(io, addr) | (io7_read16(io, addr | 2) << 16);
}

void io7_write32(IO* io, u32 addr, u32 data) {
    switch (addr) {
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
    return io->h[addr >> 1];
}

void io9_write16(IO* io, u32 addr, u16 data) {
    switch (addr) {
        default:
            io->h[addr >> 1] = data;
    }
}

u32 io9_read32(IO* io, u32 addr) {
    return io9_read16(io, addr) | (io9_read16(io, addr | 2) << 16);
}

void io9_write32(IO* io, u32 addr, u32 data) {
    switch (addr) {
        default:
            io9_write16(io, addr, data);
            io9_write16(io, addr | 2, data >> 16);
    }
}