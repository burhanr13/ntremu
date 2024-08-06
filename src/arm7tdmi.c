#include "arm7tdmi.h"

#include <stdio.h>

#include "arm/arm.h"
#include "arm/arm_core.h"
#include "arm/jit/jit.h"
#include "bus7.h"
#include "nds.h"
#include "arm/thumb.h"
#include "types.h"

void arm7_init(Arm7TDMI* cpu) {

    cpu->c.read8 = (void*) arm7_read8;
    cpu->c.read16 = (void*) arm7_read16;
    cpu->c.read32 = (void*) arm7_read32;
    cpu->c.write8 = (void*) arm7_write8;
    cpu->c.write16 = (void*) arm7_write16;
    cpu->c.write32 = (void*) arm7_write32;
    cpu->c.fetch16 = (void*) arm7_fetch16;
    cpu->c.fetch32 = (void*) arm7_fetch32;
    cpu->c.cp15_read = NULL;
    cpu->c.cp15_write = NULL;
}

void arm7_step(Arm7TDMI* cpu) {
    cpu->c.cycles = 0;
    if (!cpu->c.cpsr.i && cpu->c.irq) {
        cpu_handle_exception((ArmCore*) cpu, E_IRQ);
        return;
    }
    //arm_exec_instr((ArmCore*) cpu);
    arm_exec_jit((ArmCore*) cpu);
}

u32 arm7_read8(Arm7TDMI* cpu, u32 addr, bool sx) {
    cpu->c.cycles++;
    u32 data = bus7_read8(cpu->master, addr);
    if (sx) data = (s8) data;
    return data;
}

u32 arm7_read16(Arm7TDMI* cpu, u32 addr, bool sx) {
    cpu->c.cycles++;
    u32 data = bus7_read16(cpu->master, addr & ~1);
    if (addr & 1) {
        if (sx) {
            data = ((s16) data) >> 8;
        } else data = (data >> 8) | (data << 24);
    } else if (sx) data = (s16) data;
    return data;
}

u32 arm7_read32(Arm7TDMI* cpu, u32 addr) {
    cpu->c.cycles++;
    u32 data = bus7_read32(cpu->master, addr & ~3);
    if (addr & 0b11) {
        data =
            (data >> (8 * (addr & 0b11))) | (data << (32 - 8 * (addr & 0b11)));
    }
    return data;
}

void arm7_write8(Arm7TDMI* cpu, u32 addr, u8 b) {
    cpu->c.cycles++;
    bus7_write8(cpu->master, addr, b);
}

void arm7_write16(Arm7TDMI* cpu, u32 addr, u16 h) {
    cpu->c.cycles++;
    bus7_write16(cpu->master, addr & ~1, h);
}

void arm7_write32(Arm7TDMI* cpu, u32 addr, u32 w) {
    cpu->c.cycles++;
    bus7_write32(cpu->master, addr & ~3, w);
}

u16 arm7_fetch16(Arm7TDMI* cpu, u32 addr) {
    cpu->c.cycles++;
    u16 data = bus7_read16(cpu->master, addr & ~1);
    if (cpu->master->memerr && !cpu->master->cpuerr) {
        printf("Invalid CPU7 (thumb) instruction fetch at 0x%08x\n", addr);
        cpu->master->cpuerr = true;
    }
    return data;
}

u32 arm7_fetch32(Arm7TDMI* cpu, u32 addr) {
    cpu->c.cycles++;
    u32 data = bus7_read32(cpu->master, addr & ~3);
    if (cpu->master->memerr && !cpu->master->cpuerr) {
        printf("Invalid CPU7 instruction fetch at 0x%08x\n", addr);
        cpu->master->cpuerr = true;
    }
    return data;
}
