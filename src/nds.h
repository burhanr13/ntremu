#ifndef NDS_H
#define NDS_H

#include "arm7tdmi.h"
#include "arm946e.h"
#include "dma.h"
#include "gamecard.h"
#include "io.h"
#include "ppu.h"
#include "scheduler.h"
#include "timer.h"

#define RAMSIZE (1 << 22)

#define WRAMSIZE (1 << 15)
#define WRAM7SIZE (1 << 16)

#define VRAMSIZE 0xa4000
#define VRAMABCDSIZE (1 << 17)
#define VRAMESIZE (1 << 16)
#define VRAMFGISIZE (1 << 14)
#define VRAMHSIZE (1 << 15)

typedef enum { VRAMNULL, VRAMA, VRAMB, VRAMC, VRAMD, VRAME, VRAMF, VRAMG, VRAMH, VRAMI } VRAMBank;

#define PALSIZE (1 << 10)

#define OAMSIZE (1 << 10)
#define OAMOBJS 128

#define BIOS7SIZE (1 << 14)
#define BIOS9SIZE (1 << 15)
#define FIRMSIZE (1 << 18)

typedef enum { CPU9, CPU7 } CPUType;

enum {
    R_BIOS7,
    R_UNUSED,
    R_RAM,
    R_WRAM,
    R_IO,
    R_PAL,
    R_VRAM,
    R_OAM,
    R_GBAROM,
    R_GBAROMEX,
    R_GBASRAM
};

typedef struct _NDS {
    Scheduler sched;

    Arm7TDMI cpu7;
    DMAController dma7;
    TimerController tmc7;

    Arm946E cpu9;
    DMAController dma9;
    TimerController tmc9;

    PPU ppuA;
    PPU ppuB;

    u8 ram[RAMSIZE];

    union {
        struct {
            u8 wram0[WRAMSIZE / 2];
            u8 wram1[WRAMSIZE / 2];
        };
        u8 wram[WRAMSIZE];
    };
    u8 wram7[WRAM7SIZE];

    IO io7;
    IO io9;

    union {
        struct {
            u16 palA[PALSIZE >> 1];
            u16 palB[PALSIZE >> 1];
        };
        u8 pal[2 * PALSIZE];
    };

    union {
        struct {
            u8 vramA[VRAMABCDSIZE];
            u8 vramB[VRAMABCDSIZE];
            u8 vramC[VRAMABCDSIZE];
            u8 vramD[VRAMABCDSIZE];
            u8 vramE[VRAMESIZE];
            u8 vramF[VRAMFGISIZE];
            u8 vramG[VRAMFGISIZE];
            u8 vramH[VRAMHSIZE];
            u8 vramI[VRAMFGISIZE];
        };
        u8 vram[VRAMSIZE];
    };

    u8* vrambanks[9];

    struct {
        VRAMBank lcdc[9];
        struct {
            VRAMBank abcd[4];
            VRAMBank e;
            VRAMBank fg[4];
        } bgA;
        struct {
            VRAMBank ab[2];
            VRAMBank e;
            VRAMBank fg[4];
        } objA;
        struct {
            VRAMBank c;
            VRAMBank h;
            VRAMBank i;
        } bgB;
        struct {
            VRAMBank d;
            VRAMBank i;
        } objB;
        VRAMBank arm7[2];
    } vramstate;

    union {
        struct {
            ObjAttr oamA[OAMOBJS];
            ObjAttr oamB[OAMOBJS];
        };
        u8 oam[2 * OAMSIZE];
    };

    u8* bios7;
    u8* bios9;
    u8* firmware;
    GameCard* card;

    u32 ipcfifo7to9[16];
    u32 ipcfifo7to9_size;
    u32 ipcfifo9to7[16];
    u32 ipcfifo9to7_size;

    bool halt7;

    CPUType cur_cpu;
    bool half_tick;
    bool frame_complete;

} NDS;

void init_nds(NDS* nds, GameCard* card, u8* bios7, u8* bios9, u8* firmware);

bool nds_step(NDS* nds);

u8 vram_read8(NDS* nds, VRAMRegion region, u32 addr);
u16 vram_read16(NDS* nds, VRAMRegion region, u32 addr);
u32 vram_read32(NDS* nds, VRAMRegion region, u32 addr);

void vram_write8(NDS* nds, VRAMRegion region, u32 addr, u8 data);
void vram_write16(NDS* nds, VRAMRegion region, u32 addr, u16 data);
void vram_write32(NDS* nds, VRAMRegion region, u32 addr, u32 data);

#endif