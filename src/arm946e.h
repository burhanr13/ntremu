#ifndef _ARM946E_H
#define _ARM946E_H

#include "arm.h"
#include "arm_core.h"
#include "types.h"

#define ITCMSIZE (1 << 15)
#define DTCMSIZE (1 << 14)

typedef struct _NDS NDS;

typedef struct _Arm946E {
    ArmCore c;

    NDS* master;

    bool halt;

    u32 cp15_control;

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
u32 arm9_read32m(Arm946E* cpu, u32 addr, int i);

void arm9_write8(Arm946E* cpu, u32 addr, u8 b);
void arm9_write16(Arm946E* cpu, u32 addr, u16 h);
void arm9_write32(Arm946E* cpu, u32 addr, u32 w);
void arm9_write32m(Arm946E* cpu, u32 addr, int i, u32 w);

u16 arm9_fetch16(Arm946E* cpu, u32 addr);
u32 arm9_fetch32(Arm946E* cpu, u32 addr);

u32 cp15_read(Arm946E* cpu, u32 cn, u32 cm, u32 cp);
void cp15_write(Arm946E* cpu, u32 cn, u32 cm, u32 cp, u32 data);

#endif