#ifndef ARM7TDMI_H
#define ARM7TDMI_H

#include "arm/arm.h"
#include "arm/arm_core.h"
#include "types.h"

typedef struct _NDS NDS;

typedef struct _Arm7TDMI {
    ArmCore c;

    NDS* master;

} Arm7TDMI;

void arm7_init(Arm7TDMI* cpu);

void arm7_step(Arm7TDMI* cpu);

u32 arm7_read8(Arm7TDMI* cpu, u32 addr, bool sx);
u32 arm7_read16(Arm7TDMI* cpu, u32 addr, bool sx);
u32 arm7_read32(Arm7TDMI* cpu, u32 addr);

void arm7_write8(Arm7TDMI* cpu, u32 addr, u8 b);
void arm7_write16(Arm7TDMI* cpu, u32 addr, u16 h);
void arm7_write32(Arm7TDMI* cpu, u32 addr, u32 w);

u16 arm7_fetch16(Arm7TDMI* cpu, u32 addr);
u32 arm7_fetch32(Arm7TDMI* cpu, u32 addr);

#endif