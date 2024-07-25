#ifndef _ARM_CORE_H
#define _ARM_CORE_H

#include <stdio.h>

#include "arm.h"
#include "types.h"

typedef enum { B_USER, B_FIQ, B_SVC, B_ABT, B_IRQ, B_UND, B_CT } RegBank;
typedef enum {
    M_USER = 0b10000,
    M_FIQ = 0b10001,
    M_IRQ = 0b10010,
    M_SVC = 0b10011,
    M_ABT = 0b10111,
    M_UND = 0b11011,
    M_SYSTEM = 0b11111
} CpuMode;

typedef enum {
    I_RESET,
    I_UND,
    I_SWI,
    I_PABT,
    I_DABT,
    I_ADDR,
    I_IRQ,
    I_FIQ
} CpuInterrupt;

typedef struct _ArmCore ArmCore;

typedef struct _ArmCore {
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

    ArmInstr cur_instr;
    ArmInstr next_instr;
    u32 cur_instr_addr;
    u32 next_instr_addr;

    u32 (*read8)(ArmCore* cpu, u32 addr, bool sx);
    u32 (*read16)(ArmCore* cpu, u32 addr, bool sx);
    u32 (*read32)(ArmCore* cpu, u32 addr);

    void (*write8)(ArmCore* cpu, u32 addr, u8 b);
    void (*write16)(ArmCore* cpu, u32 addr, u16 h);
    void (*write32)(ArmCore* cpu, u32 addr, u32 w);

    u16 (*fetch16)(ArmCore* cpu, u32 addr);
    u32 (*fetch32)(ArmCore* cpu, u32 addr);

    u32 (*cp15_read)(ArmCore* cpu, u32 cn, u32 cm, u32 cp);
    void (*cp15_write)(ArmCore* cpu, u32 cn, u32 cm, u32 cp, u32 data);

    bool v5;
    u32 vector_base;

    int cycles;

    bool irq;

#ifdef CPULOG
#define LOGMAX (1 << 15)
    struct {
        u32 addr;
        ArmInstr instr;
    } log[LOGMAX];
    u32 log_idx;
#endif

} ArmCore;

void cpu_fetch_instr(ArmCore* cpu);
void cpu_flush(ArmCore* cpu);

void cpu_update_mode(ArmCore* cpu, CpuMode old);
void cpu_handle_interrupt(ArmCore* cpu, CpuInterrupt intr);

void cpu_print_state(ArmCore* cpu);
void cpu_print_cur_instr(ArmCore* cpu);

#endif