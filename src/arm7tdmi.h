#ifndef ARM7TDMI_H
#define ARM7TDMI_H

#include "arm4_isa.h"
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

typedef enum { I_RESET, I_UND, I_SWI, I_PABT, I_DABT, I_ADDR, I_IRQ, I_FIQ } CpuInterrupt;

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

    Arm4Instr cur_instr;
    Arm4Instr next_instr;
    u32 cur_instr_addr;

    u32 bus_val;

    bool next_seq;

} Arm7TDMI;

void cpu7_step(Arm7TDMI* cpu);

void cpu7_fetch_instr(Arm7TDMI* cpu);
void cpu7_flush(Arm7TDMI* cpu);

void cpu7_update_mode(Arm7TDMI* cpu, CpuMode old);
void cpu7_handle_interrupt(Arm7TDMI* cpu, CpuInterrupt intr);

u32 cpu7_readb(Arm7TDMI* cpu, u32 addr, bool sx);
u32 cpu7_readh(Arm7TDMI* cpu, u32 addr, bool sx);
u32 cpu7_readw(Arm7TDMI* cpu, u32 addr);
u32 cpu7_readm(Arm7TDMI* cpu, u32 addr, int i);

void cpu7_writeb(Arm7TDMI* cpu, u32 addr, u8 b);
void cpu7_writeh(Arm7TDMI* cpu, u32 addr, u16 h);
void cpu7_writew(Arm7TDMI* cpu, u32 addr, u32 w);
void cpu7_writem(Arm7TDMI* cpu, u32 addr, int i, u32 w);

u16 cpu7_fetchh(Arm7TDMI* cpu, u32 addr, bool seq);
u32 cpu7_fetchw(Arm7TDMI* cpu, u32 addr, bool seq);

void cpu7_internal_cycle(Arm7TDMI* cpu);

void print_cpu7_state(Arm7TDMI* cpu);
void print_cur_instr(Arm7TDMI* cpu);

#endif