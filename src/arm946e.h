#ifndef ARM946E_H
#define ARM946E_H

#include "arm5_isa.h"
#include "arm_common.h"
#include "types.h"

typedef struct _NDS NDS;

typedef struct _Arm946E {
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
            u32 reserved : 19;
            u32 q : 1; // sticky overflow
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

    Arm5Instr cur_instr;
    Arm5Instr next_instr;
    u32 cur_instr_addr;

} Arm946E;

void cpu9_step(Arm946E* cpu);

void cpu9_fetch_instr(Arm946E* cpu);
void cpu9_flush(Arm946E* cpu);

void cpu9_update_mode(Arm946E* cpu, CpuMode old);
void cpu9_handle_interrupt(Arm946E* cpu, CpuInterrupt intr);

u32 cpu9_read8(Arm946E* cpu, u32 addr, bool sx);
u32 cpu9_read16(Arm946E* cpu, u32 addr, bool sx);
u32 cpu9_read32(Arm946E* cpu, u32 addr);
u32 cpu9_read32m(Arm946E* cpu, u32 addr, int i);

void cpu9_write8(Arm946E* cpu, u32 addr, u8 b);
void cpu9_write16(Arm946E* cpu, u32 addr, u16 h);
void cpu9_write32(Arm946E* cpu, u32 addr, u32 w);
void cpu9_write32m(Arm946E* cpu, u32 addr, int i, u32 w);

u16 cpu9_fetch16(Arm946E* cpu, u32 addr);
u32 cpu9_fetch32(Arm946E* cpu, u32 addr);

void cpu9_internal_cycle(Arm946E* cpu);

void print_cpu9_state(Arm946E* cpu);
void print_cur_instr9(Arm946E* cpu);

#endif