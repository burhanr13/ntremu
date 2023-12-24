#include "arm7tdmi.h"

#include <stdio.h>

#include "arm4_isa.h"
#include "bus7.h"
#include "thumb1_isa.h"
#include "types.h"

void cpu7_step(Arm7TDMI* cpu) {
    arm_exec_instr(cpu);
}

void cpu7_fetch_instr(Arm7TDMI* cpu) {
    cpu->cur_instr = cpu->next_instr;
    if (cpu->cpsr.t) {
        cpu->next_instr = thumb_lookup[cpu7_fetchh(cpu, cpu->pc, cpu->next_seq)];
        cpu->pc += 2;
        cpu->cur_instr_addr += 2;
    } else {
        cpu->next_instr.w = cpu7_fetchw(cpu, cpu->pc, cpu->next_seq);
        cpu->pc += 4;
        cpu->cur_instr_addr += 4;
    }
    cpu->next_seq = true;
}

void cpu7_flush(Arm7TDMI* cpu) {
    if (cpu->cpsr.t) {
        cpu->pc &= ~1;
        cpu->cur_instr_addr = cpu->pc;
        cpu->cur_instr = thumb_lookup[cpu7_fetchh(cpu, cpu->pc, false)];
        cpu->pc += 2;
        cpu->next_instr = thumb_lookup[cpu7_fetchh(cpu, cpu->pc, true)];
        cpu->pc += 2;
    } else {
        cpu->pc &= ~0b11;
        cpu->cur_instr_addr = cpu->pc;
        cpu->cur_instr.w = cpu7_fetchw(cpu, cpu->pc, false);
        cpu->pc += 4;
        cpu->next_instr.w = cpu7_fetchw(cpu, cpu->pc, true);
        cpu->pc += 4;
    }
    cpu->next_seq = true;
}

RegBank get_bank(CpuMode mode) {
    switch (mode) {
        case M_USER:
            return B_USER;
        case M_FIQ:
            return B_FIQ;
        case M_IRQ:
            return B_IRQ;
        case M_SVC:
            return B_SVC;
        case M_ABT:
            return B_ABT;
        case M_UND:
            return B_UND;
        case M_SYSTEM:
            return B_USER;
    }
    return B_USER;
}

void cpu7_update_mode(Arm7TDMI* cpu, CpuMode old) {
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

void cpu7_handle_interrupt(Arm7TDMI* cpu, CpuInterrupt intr) {
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
    cpu7_update_mode(cpu, old);
    cpu->spsr = spsr;
    cpu->lr = cpu->pc;
    if (cpu->cpsr.t) {
        if (intr == I_SWI || intr == I_UND) cpu->lr -= 2;
    } else cpu->lr -= 4;
    cpu7_fetch_instr(cpu);
    cpu->cpsr.t = 0;
    cpu->cpsr.i = 1;
    cpu->pc = 4 * intr;
    cpu7_flush(cpu);
}

u32 cpu7_readb(Arm7TDMI* cpu, u32 addr, bool sx) {
    tick_components(cpu->master, get_waitstates(cpu->master, addr, false, false));
    u32 data = bus_readb(cpu->master, addr);
    if (cpu->master->openbus) data = (u8) cpu->bus_val;
    if (sx) data = (s8) data;
    cpu->bus_val = data;
    return data;
}

u32 cpu7_readh(Arm7TDMI* cpu, u32 addr, bool sx) {
    tick_components(cpu->master, get_waitstates(cpu->master, addr, false, false));
    u32 data = bus_readh(cpu->master, addr);
    if (cpu->master->openbus) data = (u16) cpu->bus_val;
    if (addr & 1) {
        if (sx) {
            data = ((s16) data) >> 8;
        } else data = (data >> 8) | (data << 24);
    } else if (sx) data = (s16) data;
    cpu->bus_val = data;
    return data;
}

u32 cpu7_readw(Arm7TDMI* cpu, u32 addr) {
    tick_components(cpu->master, get_waitstates(cpu->master, addr, true, false));
    u32 data = bus_readw(cpu->master, addr);
    if (cpu->master->openbus) data = cpu->bus_val;
    if (addr & 0b11) {
        data = (data >> (8 * (addr & 0b11))) | (data << (32 - 8 * (addr & 0b11)));
    }
    cpu->bus_val = data;
    return data;
}

u32 cpu7_readm(Arm7TDMI* cpu, u32 addr, int i) {
    tick_components(cpu->master, get_waitstates(cpu->master, addr + 4 * i, true, i != 0));
    u32 data = bus_readw(cpu->master, addr + 4 * i);
    if (cpu->master->openbus) data = cpu->bus_val;
    else cpu->bus_val = data;
    return data;
}

void cpu7_writeb(Arm7TDMI* cpu, u32 addr, u8 b) {
    tick_components(cpu->master, get_waitstates(cpu->master, addr, false, false));
    bus_writeb(cpu->master, addr, b);
}

void cpu7_writeh(Arm7TDMI* cpu, u32 addr, u16 h) {
    tick_components(cpu->master, get_waitstates(cpu->master, addr, false, false));
    bus_writeh(cpu->master, addr, h);
}

void cpu7_writew(Arm7TDMI* cpu, u32 addr, u32 w) {
    tick_components(cpu->master, get_waitstates(cpu->master, addr, true, false));
    bus_writew(cpu->master, addr, w);
}

void cpu7_writem(Arm7TDMI* cpu, u32 addr, int i, u32 w) {
    tick_components(cpu->master, get_waitstates(cpu->master, addr + 4 * i, true, i != 0));
    bus_writew(cpu->master, addr + 4 * i, w);
}

u16 cpu7_fetchh(Arm7TDMI* cpu, u32 addr, bool seq) {
    tick_components(cpu->master, get_fetch_waitstates(cpu->master, addr, false, seq));
    u32 data = bus_readh(cpu->master, addr);
    if (cpu->master->openbus) data = cpu->bus_val;
    else {
        u32 reg = addr >> 24;
        if (reg == R_BIOS || reg == R_IWRAM || reg == R_OAM) {
            cpu->bus_val &= 0x0000ffff << (16 * (~addr & 1));
            cpu->bus_val |= data << (16 * (addr & 1));
        } else cpu->bus_val = data * 0x00010001;
    }
    return data;
}

u32 cpu7_fetchw(Arm7TDMI* cpu, u32 addr, bool seq) {
    tick_components(cpu->master, get_fetch_waitstates(cpu->master, addr, true, seq));
    u32 data = bus_readw(cpu->master, addr);
    if (cpu->master->openbus) data = cpu->bus_val;
    else cpu->bus_val = data;
    return data;
}

void cpu7_internal_cycle(Arm7TDMI* cpu) {
    tick_components(cpu->master, 1);
    cpu->master->prefetcher_cycles++;
    if (cpu->pc >= 0x8000000) cpu->next_seq = cpu->master->io.waitcnt.prefetch;
}

char* mode_name(CpuMode m) {
    switch (m) {
        case M_USER:
            return "USER";
        case M_FIQ:
            return "FIQ";
        case M_IRQ:
            return "IRQ";
        case M_SVC:
            return "SVC";
        case M_ABT:
            return "ABT";
        case M_UND:
            return "UND";
        case M_SYSTEM:
            return "SYSTEM";
        default:
            return "ILLEGAL";
    }
}

void print_cpu7_state(Arm7TDMI* cpu) {
    static char* reg_names[16] = {"r0", "r1", "r2",  "r3",  "r4", "r5", "r6", "r7",
                                  "r8", "r9", "r10", "r11", "ip", "sp", "lr", "pc"};
    for (int i = 0; i < 4; i++) {
        if (i == 0) printf("CPU ");
        else printf("    ");
        for (int j = 0; j < 4; j++) {
            printf("%3s=0x%08x ", reg_names[4 * i + j], cpu->r[4 * i + j]);
        }
        printf("\n");
    }
    printf("    cpsr=%08x (n=%d,z=%d,c=%d,v=%d,i=%d,f=%d,t=%d,m=%s)\n", cpu->cpsr.w, cpu->cpsr.n,
           cpu->cpsr.z, cpu->cpsr.c, cpu->cpsr.v, cpu->cpsr.i, cpu->cpsr.v, cpu->cpsr.t,
           mode_name(cpu->cpsr.m));
}

void print_cur_instr(Arm7TDMI* cpu) {
    if (cpu->cpsr.t) {
        printf("%08x: %04x ", cpu->cur_instr_addr, cpu->cur_instr.w);
        thumb_disassemble((Thumb1Instr){bus_readh(cpu->master, cpu->cur_instr_addr)},
                          cpu->cur_instr_addr, stdout);
        printf("\n");
    } else {
        printf("%08x: %08x ", cpu->cur_instr_addr, cpu->cur_instr.w);
        arm_disassemble(cpu->cur_instr, cpu->cur_instr_addr, stdout);
        printf("\n");
    }
}