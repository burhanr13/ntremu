#include "bus9.h"

#include "nds.h"

#define BUS9READDECL(size)                                                                         \
    u##size bus9_read##size(NDS* nds, u32 addr) {                                                  \
        switch (addr >> 24) {                                                                      \
            case R_RAM:                                                                            \
                return *(u##size*) (&nds->ram[addr % RAMSIZE]);                                    \
                break;                                                                             \
            case R_WRAM:                                                                           \
                break;                                                                             \
            case R_IO:                                                                             \
                return io9_read##size(&nds->io9, addr & 0xffffff);                                 \
                break;                                                                             \
            case R_PAL:                                                                            \
                return *(u##size*) (&nds->pal[addr % PALSIZE]);                                    \
                break;                                                                             \
            case R_VRAM:                                                                           \
                if (addr >= 0x6800000 && addr < 0x68a4000) {                                       \
                    return *(u##size*) (&nds->vram[addr - 0x6800000]);                             \
                }                                                                                  \
                break;                                                                             \
            case R_OAM:                                                                            \
                return *(u##size*) (&nds->oam[addr % OAMSIZE]);                                    \
                break;                                                                             \
            case R_GBAROM:                                                                         \
                break;                                                                             \
            case R_GBASRAM:                                                                        \
                break;                                                                             \
            case 0xff:                                                                             \
                if (addr >= 0xffff0000 && addr < (0xffff0000 + BIOS9SIZE))                         \
                    return *(u##size*) (&nds->bios9[addr - 0xffff0000]);                           \
        }                                                                                          \
        return -1;                                                                                 \
    }

#define BUS9WRITEDECL(size)                                                                        \
    void bus9_write##size(NDS* nds, u32 addr, u##size data) {                                      \
        switch (addr >> 24) {                                                                      \
            case R_RAM:                                                                            \
                *(u##size*) (&nds->ram[addr % RAMSIZE]) = data;                                    \
                break;                                                                             \
            case R_WRAM:                                                                           \
                break;                                                                             \
            case R_IO:                                                                             \
                io9_write##size(&nds->io9, addr & 0xffffff, data);                                 \
                break;                                                                             \
            case R_PAL:                                                                            \
                *(u##size*) (&nds->pal[addr % PALSIZE]) = data;                                    \
                break;                                                                             \
            case R_VRAM:                                                                           \
                if (addr >= 0x6800000 && addr < 0x68a4000) {                                       \
                    *(u##size*) (&nds->vram[addr - 0x6800000]) = data;                             \
                }                                                                                  \
                break;                                                                             \
            case R_OAM:                                                                            \
                *(u##size*) (&nds->oam[addr % OAMSIZE]) = data;                                    \
                break;                                                                             \
            case R_GBAROM:                                                                         \
                break;                                                                             \
            case R_GBASRAM:                                                                        \
                break;                                                                             \
        }                                                                                          \
    }

BUS9READDECL(8)
BUS9READDECL(16)
BUS9READDECL(32)

BUS9WRITEDECL(8)
BUS9WRITEDECL(16)
BUS9WRITEDECL(32)