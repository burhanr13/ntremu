#include "arm946e.h"

#include <stdio.h>

#include "arm5_isa.h"
#include "arm_common.h"
#include "bus9.h"
#include "thumb2_isa.h"
#include "types.h"

void cpu9_step(Arm946E* cpu) {
    arm5_exec_instr(cpu);
}

void cpu9_fetch_instr(Arm946E* cpu) {
    cpu->cur_instr = cpu->next_instr;
    if (cpu->cpsr.t) {
        cpu->next_instr = thumb2_lookup[cpu9_fetch16(cpu, cpu->pc)];
        cpu->pc += 2;
        cpu->cur_instr_addr += 2;
    } else {
        cpu->next_instr.w = cpu9_fetch32(cpu, cpu->pc);
        cpu->pc += 4;
        cpu->cur_instr_addr += 4;
    }
}

void cpu9_flush(Arm946E* cpu) {
    if (cpu->cpsr.t) {
        cpu->pc &= ~1;
        cpu->cur_instr_addr = cpu->pc;
        cpu->cur_instr = thumb2_lookup[cpu9_fetch16(cpu, cpu->pc)];
        cpu->pc += 2;
        cpu->next_instr = thumb2_lookup[cpu9_fetch16(cpu, cpu->pc)];
        cpu->pc += 2;
    } else {
        cpu->pc &= ~0b11;
        cpu->cur_instr_addr = cpu->pc;
        cpu->cur_instr.w = cpu9_fetch32(cpu, cpu->pc);
        cpu->pc += 4;
        cpu->next_instr.w = cpu9_fetch32(cpu, cpu->pc);
        cpu->pc += 4;
    }
}

void cpu9_update_mode(Arm946E* cpu, CpuMode old) {
    RegBank old_bank = get_bank(old);
    cpu->banked_sp[old_bank] = cpu->sp;
    cpu->banked_lr[old_bank] = cpu->lr;
    cpu->banked_spsr[old_bank] = cpu->spsr;
    RegBank new_bank = get_bank(cpu->cpsr.m);
    cpu->sp = cpu->banked_sp[new_bank];
    cpu->lr = cpu->banked_lr[new_bank];
    cpu->spsr = cpu->banked_spsr[new_bank];
    if (old == M_FIQ && cpu->cpsr.m != M_FIQ) {
        for (int i = 0; i < 5; i++) {
            cpu->banked_r8_12[1][i] = cpu->r[8 + i];
            cpu->r[8 + i] = cpu->banked_r8_12[0][i];
        }
    }
    if (old != M_FIQ && cpu->cpsr.m == M_FIQ) {
        for (int i = 0; i < 5; i++) {
            cpu->banked_r8_12[0][i] = cpu->r[8 + i];
            cpu->r[8 + i] = cpu->banked_r8_12[1][i];
        }
    }
}

void cpu9_handle_interrupt(Arm946E* cpu, CpuInterrupt intr) {
    CpuMode old = cpu->cpsr.m;
    u32 spsr = cpu->cpsr.w;
    switch (intr) {
        case I_RESET:
        case I_SWI:
        case I_ADDR:
            cpu->cpsr.m = M_SVC;
            break;
        case I_PABT:
        case I_DABT:
            cpu->cpsr.m = M_ABT;
            break;
        case I_UND:
            cpu->cpsr.m = M_UND;
            break;
        case I_IRQ:
            cpu->cpsr.m = M_IRQ;
            break;
        case I_FIQ:
            cpu->cpsr.m = M_FIQ;
            break;
    }
    cpu9_update_mode(cpu, old);
    cpu->spsr = spsr;
    cpu->lr = cpu->pc;
    if (cpu->cpsr.t) {
        if (intr == I_SWI || intr == I_UND) cpu->lr -= 2;
    } else cpu->lr -= 4;
    cpu9_fetch_instr(cpu);
    cpu->cpsr.t = 0;
    cpu->cpsr.i = 1;
    cpu->pc = 0xffff0000 + 4 * intr;
    cpu9_flush(cpu);
}

u32 cpu9_read8(Arm946E* cpu, u32 addr, bool sx) {
    u32 data = bus9_read8(cpu->master, addr);
    if (sx) data = (s8) data;
    return data;
}

u32 cpu9_read16(Arm946E* cpu, u32 addr, bool sx) {
    u32 data = bus9_read16(cpu->master, addr & ~1);
    if (addr & 1) {
        if (sx) {
            data = ((s16) data) >> 8;
        } else data = (data >> 8) | (data << 24);
    } else if (sx) data = (s16) data;
    return data;
}

u32 cpu9_read32(Arm946E* cpu, u32 addr) {
    u32 data = bus9_read32(cpu->master, addr & ~3);
    if (addr & 0b11) {
        data = (data >> (8 * (addr & 0b11))) | (data << (32 - 8 * (addr & 0b11)));
    }
    return data;
}

u32 cpu9_read32m(Arm946E* cpu, u32 addr, int i) {
    return bus9_read32(cpu->master, (addr & ~3) + 4 * i);
}

void cpu9_write8(Arm946E* cpu, u32 addr, u8 b) {
    bus9_write8(cpu->master, addr, b);
}

void cpu9_write16(Arm946E* cpu, u32 addr, u16 h) {
    bus9_write16(cpu->master, addr & ~1, h);
}

void cpu9_write32(Arm946E* cpu, u32 addr, u32 w) {
    bus9_write32(cpu->master, addr & ~3, w);
}

void cpu9_write32m(Arm946E* cpu, u32 addr, int i, u32 w) {
    bus9_write32(cpu->master, (addr & ~3) + 4 * i, w);
}

u16 cpu9_fetch16(Arm946E* cpu, u32 addr) {
    return bus9_read16(cpu->master, addr & ~1);
}

u32 cpu9_fetch32(Arm946E* cpu, u32 addr) {
    return bus9_read32(cpu->master, addr & ~3);
}

void cpu9_internal_cycle(Arm946E* cpu) {}

void print_cpu9_state(Arm946E* cpu) {
    static char* reg_names[16] = {"r0", "r1", "r2",  "r3",  "r4", "r5", "r6", "r7",
                                  "r8", "r9", "r10", "r11", "ip", "sp", "lr", "pc"};
    for (int i = 0; i < 4; i++) {
        if (i == 0) printf("CPU9 ");
        else printf("     ");
        for (int j = 0; j < 4; j++) {
            printf("%3s=0x%08x ", reg_names[4 * i + j], cpu->r[4 * i + j]);
        }
        printf("\n");
    }
    printf("     cpsr=%08x (n=%d,z=%d,c=%d,v=%d,q=%d,i=%d,f=%d,t=%d,m=%s)\n", cpu->cpsr.w,
           cpu->cpsr.n, cpu->cpsr.z, cpu->cpsr.c, cpu->cpsr.v, cpu->cpsr.q, cpu->cpsr.i,
           cpu->cpsr.v, cpu->cpsr.t, mode_name(cpu->cpsr.m));
}

void print_cur_instr9(Arm946E* cpu) {
    if (cpu->cpsr.t) {
        printf("%08x: %04x ", cpu->cur_instr_addr, cpu->cur_instr.w);
        thumb2_disassemble((Thumb2Instr){bus9_read16(cpu->master, cpu->cur_instr_addr)},
                           cpu->cur_instr_addr, stdout);
        printf("\n");
    } else {
        printf("%08x: %08x ", cpu->cur_instr_addr, cpu->cur_instr.w);
        arm5_disassemble(cpu->cur_instr, cpu->cur_instr_addr, stdout);
        printf("\n");
    }
}