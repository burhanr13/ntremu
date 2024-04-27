#include "bus9.h"

#include "nds.h"

#define BUS9READDECL(size)                                                     \
    u##size bus9_read##size(NDS* nds, u32 addr) {                              \
        nds->memerr = false;                                                   \
        switch (addr >> 24) {                                                  \
            case R_RAM:                                                        \
                return *(u##size*) (&nds->ram[addr % RAMSIZE]);                \
                break;                                                         \
            case R_WRAM:                                                       \
                switch (nds->io9.wramcnt) {                                    \
                    case 0:                                                    \
                        return *(u##size*) &nds->wram[addr % WRAMSIZE];        \
                    case 1:                                                    \
                        return *(u##size*) &nds->wram1[addr % (WRAMSIZE / 2)]; \
                    case 2:                                                    \
                        return *(u##size*) &nds->wram0[addr % (WRAMSIZE / 2)]; \
                    case 3:                                                    \
                        break;                                                 \
                }                                                              \
                break;                                                         \
            case R_IO:                                                         \
                return io9_read##size(&nds->io9, addr & 0xffffff);             \
                break;                                                         \
            case R_PAL:                                                        \
                return *(u##size*) (&nds->pal[addr % (2 * PALSIZE)]);          \
                break;                                                         \
            case R_VRAM:                                                       \
                return vram_read##size(nds, (addr >> 21) & 7, addr & 0xfffff); \
                break;                                                         \
            case R_OAM:                                                        \
                return *(u##size*) (&nds->oam[addr % (2 * OAMSIZE)]);          \
                break;                                                         \
            case R_GBAROM:                                                     \
            case R_GBAROMEX:                                                   \
                return *(u##size*) &nds->expansionram[addr % (1 << 25)];       \
            default:                                                           \
                if (addr >= 0xffff0000 && addr < (0xffff0000 + BIOS9SIZE))     \
                    return *(u##size*) (&nds->bios9[addr - 0xffff0000]);       \
        }                                                                      \
        nds->memerr = true;                                                    \
        return -1;                                                             \
    }

#define BUS9WRITEDECL(size)                                                    \
    void bus9_write##size(NDS* nds, u32 addr, u##size data) {                  \
        nds->memerr = false;                                                   \
        switch (addr >> 24) {                                                  \
            case R_RAM:                                                        \
                *(u##size*) (&nds->ram[addr % RAMSIZE]) = data;                \
                break;                                                         \
            case R_WRAM:                                                       \
                switch (nds->io9.wramcnt) {                                    \
                    case 0:                                                    \
                        *(u##size*) &nds->wram[addr % WRAMSIZE] = data;        \
                        break;                                                 \
                    case 1:                                                    \
                        *(u##size*) &nds->wram1[addr % (WRAMSIZE / 2)] = data; \
                        break;                                                 \
                    case 2:                                                    \
                        *(u##size*) &nds->wram0[addr % (WRAMSIZE / 2)] = data; \
                        break;                                                 \
                }                                                              \
                break;                                                         \
            case R_IO:                                                         \
                io9_write##size(&nds->io9, addr & 0xffffff, data);             \
                break;                                                         \
            case R_PAL:                                                        \
                *(u##size*) (&nds->pal[addr % (2 * PALSIZE)]) = data;          \
                break;                                                         \
            case R_VRAM:                                                       \
                vram_write##size(nds, (addr >> 21) & 7, addr & 0xfffff, data); \
                break;                                                         \
            case R_OAM:                                                        \
                *(u##size*) (&nds->oam[addr % (2 * OAMSIZE)]) = data;          \
                break;                                                         \
            case R_GBAROM:                                                     \
            case R_GBAROMEX:                                                   \
                *(u##size*) &nds->expansionram[addr % (1 << 25)] = data;       \
                break;                                                         \
        }                                                                      \
    }

BUS9READDECL(8)
BUS9READDECL(16)
BUS9READDECL(32)

BUS9WRITEDECL(8)
BUS9WRITEDECL(16)
BUS9WRITEDECL(32)