#include "bus7.h"

#include "nds.h"

#define BUS7READDECL(size)                                                                         \
    u##size bus7_read##size(NDS* nds, u32 addr) {                                                  \
        switch (addr >> 24) {                                                                      \
            case R_BIOS7:                                                                          \
                break;                                                                             \
            case R_RAM:                                                                            \
                return *(u##size*) (&nds->ram[addr % RAMSIZE]);                                    \
                break;                                                                             \
            case R_WRAM:                                                                           \
                return *(u##size*) (&nds->wram7[addr % WRAM7SIZE]);                                \
                break;                                                                             \
            case R_IO:                                                                             \
                return io7_read##size(&nds->io, addr & 0xffffff);                                  \
                break;                                                                             \
            case R_VRAM:                                                                           \
                break;                                                                             \
            case R_GBAROM:                                                                         \
                break;                                                                             \
            case R_GBASRAM:                                                                        \
                break;                                                                             \
        }                                                                                          \
        return -1;                                                                                 \
    }

#define BUS7WRITEDECL(size)                                                                        \
    void bus7_write##size(NDS* nds, u32 addr, u##size data) {                                      \
        switch (addr >> 24) {                                                                      \
            case R_BIOS7:                                                                          \
                break;                                                                             \
            case R_RAM:                                                                            \
                *(u##size*) (&nds->ram[addr % RAMSIZE]) = data;                                    \
                break;                                                                             \
            case R_WRAM:                                                                           \
                *(u##size*) (&nds->wram7[addr % WRAM7SIZE]) = data;                                \
                break;                                                                             \
            case R_IO:                                                                             \
                io7_write##size(&nds->io, addr & 0xffffff, data);                                  \
                break;                                                                             \
            case R_VRAM:                                                                           \
                break;                                                                             \
            case R_GBAROM:                                                                         \
                break;                                                                             \
            case R_GBASRAM:                                                                        \
                break;                                                                             \
        }                                                                                          \
    }

BUS7READDECL(8)
BUS7READDECL(16)
BUS7READDECL(32)

BUS7WRITEDECL(8)
BUS7WRITEDECL(16)
BUS7WRITEDECL(32)