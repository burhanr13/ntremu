#ifndef _ARM7TDMI_H
#define _ARM7TDMI_H

#include "arm4_isa.h"
#include "arm_common.h"
#include "types.h"

typedef struct _NDS NDS;

typedef struct _Arm7TDMI {
    NDS* master;

    union {
        u32 r[16];
        struct {
            u32 _r[13];
            u32 sp;
            u32 lr;
            u32 pc;
        };
    };

    union {
        u32 w;
        struct {
            u32 m : 5; // mode
            u32 t : 1; // thumb state
            u32 f : 1; // disable fiq
            u32 i : 1; // disable irq
            u32 reserved : 20;
            u32 v : 1; // overflow
            u32 c : 1; // carry
            u32 z : 1; // zero
            u32 n : 1; // negative
        };
    } cpsr;
    u32 spsr;

    u32 banked_r8_12[2][5];
    u32 banked_sp[B_CT];
    u32 banked_lr[B_CT];
    u32 banked_spsr[B_CT];

    ArmInstr cur_instr;
    ArmInstr next_instr;
    u32 cur_instr_addr;

    int cycles;

    bool irq;

} Arm7TDMI;

void cpu7_step(Arm7TDMI* cpu);

void cpu7_fetch_instr(Arm7TDMI* cpu);
void cpu7_flush(Arm7TDMI* cpu);

void cpu7_update_mode(Arm7TDMI* cpu, CpuMode old);
void cpu7_handle_interrupt(Arm7TDMI* cpu, CpuInterrupt intr);

u32 cpu7_read8(Arm7TDMI* cpu, u32 addr, bool sx);
u32 cpu7_read16(Arm7TDMI* cpu, u32 addr, bool sx);
u32 cpu7_read32(Arm7TDMI* cpu, u32 addr);
u32 cpu7_read32m(Arm7TDMI* cpu, u32 addr, int i);

void cpu7_write8(Arm7TDMI* cpu, u32 addr, u8 b);
void cpu7_write16(Arm7TDMI* cpu, u32 addr, u16 h);
void cpu7_write32(Arm7TDMI* cpu, u32 addr, u32 w);
void cpu7_write32m(Arm7TDMI* cpu, u32 addr, int i, u32 w);

u16 cpu7_fetch16(Arm7TDMI* cpu, u32 addr);
u32 cpu7_fetch32(Arm7TDMI* cpu, u32 addr);

void print_cpu7_state(Arm7TDMI* cpu);
void print_cur_instr7(Arm7TDMI* cpu);

#endif