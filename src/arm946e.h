#ifndef _ARM946E_H
#define _ARM946E_H

#include "arm/arm.h"
#include "arm/arm_core.h"
#include "types.h"

#define ITCMSIZE (1 << 15)
#define DTCMSIZE (1 << 14)

typedef struct _NDS NDS;

typedef struct _Arm946E {
    ArmCore c;

    NDS* master;

    bool halt;

    union {
        u32 w;
        struct {
            u32 whatever1 : 13;
            u32 vector_base : 1;
            u32 whatever2 : 1;
            u32 v4mode : 1;
            u32 dtcm_on : 1;
            u32 dtcm_load : 1;
            u32 itcm_on : 1;
            u32 itcm_load : 1;
            u32 whatever3 : 12;
        };
    } cp15_control;

    u8 itcm[ITCMSIZE];
    u8 dtcm[DTCMSIZE];

    u32 itcm_virtsize;
    u32 dtcm_base;
    u32 dtcm_virtsize;

} Arm946E;

void arm9_init(Arm946E* cpu);

bool arm9_step(Arm946E* cpu);

u32 arm9_read8(Arm946E* cpu, u32 addr, bool sx);
u32 arm9_read16(Arm946E* cpu, u32 addr, bool sx);
u32 arm9_read32(Arm946E* cpu, u32 addr);

void arm9_write8(Arm946E* cpu, u32 addr, u8 b);
void arm9_write16(Arm946E* cpu, u32 addr, u16 h);
void arm9_write32(Arm946E* cpu, u32 addr, u32 w);

u16 arm9_fetch16(Arm946E* cpu, u32 addr);
u32 arm9_fetch32(Arm946E* cpu, u32 addr);

u32 cp15_read(Arm946E* cpu, u32 cn, u32 cm, u32 cp);
void cp15_write(Arm946E* cpu, u32 cn, u32 cm, u32 cp, u32 data);

#endif