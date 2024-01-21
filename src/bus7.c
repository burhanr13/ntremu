#include "bus7.h"

#include "nds.h"

#define BUS7READDECL(size)                                                     \
    u##size bus7_read##size(NDS* nds, u32 addr) {                              \
        nds->memerr = false;                                                   \
        switch (addr >> 24) {                                                  \
            case R_BIOS7:                                                      \
                if (addr < BIOS7SIZE) return *(u##size*) (&nds->bios7[addr]);  \
                break;                                                         \
            case R_RAM:                                                        \
                return *(u##size*) (&nds->ram[addr % RAMSIZE]);                \
                break;                                                         \
            case R_WRAM:                                                       \
                if (addr < 0x3800000) {                                        \
                    switch (nds->io7.wramstat) {                               \
                        case 0:                                                \
                            break;                                             \
                        case 1:                                                \
                            return *(u##size*) &nds                            \
                                        ->wram0[addr % (WRAMSIZE / 2)];        \
                        case 2:                                                \
                            return *(u##size*) &nds                            \
                                        ->wram1[addr % (WRAMSIZE / 2)];        \
                        case 3:                                                \
                            return *(u##size*) &nds->wram[addr % WRAMSIZE];    \
                    }                                                          \
                }                                                              \
                return *(u##size*) (&nds->wram7[addr % WRAM7SIZE]);            \
                break;                                                         \
            case R_IO:                                                         \
                return io7_read##size(&nds->io7, addr & 0xffffff);             \
                break;                                                         \
            case R_VRAM: {                                                     \
                int ofs = (addr & VRAMABCDSIZE) ? 1 : 0;                       \
                VRAMBank b = nds->vramstate.arm7[ofs];                         \
                if (b)                                                         \
                    return *(u##size*) &nds                                    \
                                ->vrambanks[b - 1][addr % VRAMABCDSIZE];       \
                else return 0;                                                 \
                break;                                                         \
            }                                                                  \
        }                                                                      \
        nds->memerr = true;                                                    \
        return -1;                                                             \
    }

#define BUS7WRITEDECL(size)                                                    \
    void bus7_write##size(NDS* nds, u32 addr, u##size data) {                  \
        nds->memerr = false;                                                   \
        switch (addr >> 24) {                                                  \
            case R_RAM:                                                        \
                *(u##size*) (&nds->ram[addr % RAMSIZE]) = data;                \
                break;                                                         \
            case R_WRAM:                                                       \
                if (addr < 0x3800000) {                                        \
                    switch (nds->io7.wramstat) {                               \
                        case 0:                                                \
                            break;                                             \
                        case 1:                                                \
                            *(u##size*) &nds->wram0[addr % (WRAMSIZE / 2)] =   \
                                data;                                          \
                            return;                                            \
                        case 2:                                                \
                            *(u##size*) &nds->wram1[addr % (WRAMSIZE / 2)] =   \
                                data;                                          \
                            return;                                            \
                        case 3:                                                \
                            *(u##size*) &nds->wram[addr % WRAMSIZE] = data;    \
                            return;                                            \
                    }                                                          \
                }                                                              \
                *(u##size*) (&nds->wram7[addr % WRAM7SIZE]) = data;            \
                break;                                                         \
            case R_IO:                                                         \
                io7_write##size(&nds->io7, addr & 0xffffff, data);             \
                break;                                                         \
            case R_VRAM: {                                                     \
                int ofs = (addr & VRAMABCDSIZE) ? 1 : 0;                       \
                VRAMBank b = nds->vramstate.arm7[ofs];                         \
                if (b)                                                         \
                    *(u##size*) &nds->vrambanks[b - 1][addr % VRAMABCDSIZE] =  \
                        data;                                                  \
                break;                                                         \
            }                                                                  \
        }                                                                      \
    }

BUS7READDECL(8)
BUS7READDECL(16)
BUS7READDECL(32)

BUS7WRITEDECL(8)
BUS7WRITEDECL(16)
BUS7WRITEDECL(32)