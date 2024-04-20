#include "arm7tdmi.h"

#include <stdio.h>

#include "arm4_isa.h"
#include "arm_common.h"
#include "bus7.h"
#include "nds.h"
#include "thumb_isa.h"
#include "types.h"

void cpu7_step(Arm7TDMI* cpu) {
    cpu->cycles = 0;
    if (!cpu->cpsr.i && cpu->irq) {
        cpu7_handle_interrupt(cpu, I_IRQ);
        return;
    }
    arm4_exec_instr(cpu);
}

void cpu7_fetch_instr(Arm7TDMI* cpu) {
    cpu->cur_instr = cpu->next_instr;
    if (cpu->cpsr.t) {
        cpu->next_instr = thumb_lookup[cpu7_fetch16(cpu, cpu->pc)];
        cpu->pc += 2;
        cpu->cur_instr_addr += 2;
    } else {
        cpu->next_instr.w = cpu7_fetch32(cpu, cpu->pc);
        cpu->pc += 4;
        cpu->cur_instr_addr += 4;
    }
}

void cpu7_flush(Arm7TDMI* cpu) {
    if (cpu->cpsr.t) {
        cpu->pc &= ~1;
        cpu->cur_instr_addr = cpu->pc;
        cpu->cur_instr = thumb_lookup[cpu7_fetch16(cpu, cpu->pc)];
        cpu->pc += 2;
        cpu->next_instr = thumb_lookup[cpu7_fetch16(cpu, cpu->pc)];
        cpu->pc += 2;
    } else {
        cpu->pc &= ~0b11;
        cpu->cur_instr_addr = cpu->pc;
        cpu->cur_instr.w = cpu7_fetch32(cpu, cpu->pc);
        cpu->pc += 4;
        cpu->next_instr.w = cpu7_fetch32(cpu, cpu->pc);
        cpu->pc += 4;
    }
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

u32 cpu7_read8(Arm7TDMI* cpu, u32 addr, bool sx) {
    cpu->cycles++;
    u32 data = bus7_read8(cpu->master, addr);
    if (sx) data = (s8) data;
    return data;
}

u32 cpu7_read16(Arm7TDMI* cpu, u32 addr, bool sx) {
    cpu->cycles++;
    u32 data = bus7_read16(cpu->master, addr & ~1);
    if (addr & 1) {
        if (sx) {
            data = ((s16) data) >> 8;
        } else data = (data >> 8) | (data << 24);
    } else if (sx) data = (s16) data;
    return data;
}

u32 cpu7_read32(Arm7TDMI* cpu, u32 addr) {
    cpu->cycles++;
    u32 data = bus7_read32(cpu->master, addr & ~3);
    if (addr & 0b11) {
        data =
            (data >> (8 * (addr & 0b11))) | (data << (32 - 8 * (addr & 0b11)));
    }
    return data;
}

u32 cpu7_read32m(Arm7TDMI* cpu, u32 addr, int i) {
    cpu->cycles++;
    return bus7_read32(cpu->master, (addr & ~3) + 4 * i);
}

void cpu7_write8(Arm7TDMI* cpu, u32 addr, u8 b) {
    cpu->cycles++;
    bus7_write8(cpu->master, addr, b);
}

void cpu7_write16(Arm7TDMI* cpu, u32 addr, u16 h) {
    cpu->cycles++;
    bus7_write16(cpu->master, addr & ~1, h);
}

void cpu7_write32(Arm7TDMI* cpu, u32 addr, u32 w) {
    cpu->cycles++;
    bus7_write32(cpu->master, addr & ~3, w);
}

void cpu7_write32m(Arm7TDMI* cpu, u32 addr, int i, u32 w) {
    cpu->cycles++;
    bus7_write32(cpu->master, (addr & ~3) + 4 * i, w);
}

u16 cpu7_fetch16(Arm7TDMI* cpu, u32 addr) {
    cpu->cycles++;
    u16 data = bus7_read16(cpu->master, addr & ~1);
    if (cpu->master->memerr) {
        printf("Invalid CPU7 (thumb) instruction fetch at 0x%08x\n", addr);
        cpu->master->cpuerr = true;
    }
    return data;
}

u32 cpu7_fetch32(Arm7TDMI* cpu, u32 addr) {
    cpu->cycles++;
    u32 data = bus7_read32(cpu->master, addr & ~3);
    if (cpu->master->memerr) {
        printf("Invalid CPU7 instruction fetch at 0x%08x\n", addr);
        cpu->master->cpuerr = true;
    }
    return data;
}

void print_cpu7_state(Arm7TDMI* cpu) {
    static char* reg_names[16] = {"r0", "r1", "r2", "r3", "r4",  "r5",
                                  "r6", "r7", "r8", "r9", "r10", "r11",
                                  "ip", "sp", "lr", "pc"};
    for (int i = 0; i < 4; i++) {
        if (i == 0) printf("CPU7 ");
        else printf("     ");
        for (int j = 0; j < 4; j++) {
            printf("%3s=0x%08x ", reg_names[4 * i + j], cpu->r[4 * i + j]);
        }
        printf("\n");
    }
    printf("     cpsr=%08x (n=%d,z=%d,c=%d,v=%d,i=%d,f=%d,t=%d,m=%s)\n",
           cpu->cpsr.w, cpu->cpsr.n, cpu->cpsr.z, cpu->cpsr.c, cpu->cpsr.v,
           cpu->cpsr.i, cpu->cpsr.v, cpu->cpsr.t, mode_name(cpu->cpsr.m));
}

void print_cur_instr7(Arm7TDMI* cpu) {
    if (cpu->cpsr.t) {
        ThumbInstr instr = {bus7_read16(cpu->master, cpu->cur_instr_addr)};
        printf("%08x: %04x ", cpu->cur_instr_addr, instr.h);
        thumb_disassemble(instr, cpu->cur_instr_addr, stdout);
        printf("\n");
    } else {
        printf("%08x: %08x ", cpu->cur_instr_addr, cpu->cur_instr.w);
        arm_disassemble(cpu->cur_instr, cpu->cur_instr_addr, stdout);
        printf("\n");
    }
}