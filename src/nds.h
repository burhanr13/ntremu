#ifndef NDS_H
#define NDS_H

#include "arm7tdmi.h"
#include "arm946e.h"
#include "gamecard.h"
#include "io.h"
#include "ppu.h"
#include "scheduler.h"

#define RAMSIZE (1<<22)

#define WRAMSIZE (1 << 15)
#define WRAM7SIZE (1 << 16)

#define VRAMSIZE 0xa4000
#define VRAMABCDSIZE (1 << 17)
#define VRAMESIZE (1 << 16)
#define VRAMFGISIZE (1 << 14)
#define VRAMHSIZE (1 << 15)

#define PALSIZE (1 << 10)

#define OAMSIZE (1 << 10)
#define OAMOBJS 128

#define BIOS7SIZE (1 << 14)
#define BIOS9SIZE (1 << 15)

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
    Arm946E cpu9;

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

    union {
        struct {
            ObjAttr oamA[OAMOBJS];
            ObjAttr oamB[OAMOBJS];
        };
        u8 oam[2 * OAMSIZE];
    };

    u8* bios7;
    u8* bios9;
    GameCard* card;

    u32 ipcfifo7to9[16];
    u32 ipcfifo7to9_size;
    u32 ipcfifo9to7[16];
    u32 ipcfifo9to7_size;

    CPUType cur_cpu;
    bool frame_complete;

} NDS;

void init_nds(NDS* nds, GameCard* card, u8* bios7, u8* bios9);

bool nds_step(NDS* nds);

#endif