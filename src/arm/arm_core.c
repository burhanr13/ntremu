#include "arm_core.h"

#include "thumb.h"

void cpu_fetch_instr(ArmCore* cpu) {
    cpu->cur_instr = cpu->next_instr;
    if (cpu->cpsr.t) {
        cpu->next_instr = thumb_lookup[cpu->fetch16(cpu, cpu->pc)];
        cpu->cur_instr_addr += 2;
        cpu->next_instr_addr += 2;
        cpu->pc += 2;
    } else {
        cpu->next_instr.w = cpu->fetch32(cpu, cpu->pc);
        cpu->cur_instr_addr += 4;
        cpu->next_instr_addr += 4;
        cpu->pc += 4;
    }
}

void cpu_flush(ArmCore* cpu) {
    if (cpu->cpsr.t) {
        cpu->pc &= ~1;
        cpu->cur_instr_addr = cpu->pc;
        cpu->cur_instr = thumb_lookup[cpu->fetch16(cpu, cpu->pc)];
        cpu->pc += 2;
        cpu->next_instr_addr = cpu->pc;
        cpu->next_instr = thumb_lookup[cpu->fetch16(cpu, cpu->pc)];
        cpu->pc += 2;
    } else {
        cpu->pc &= ~0b11;
        cpu->cur_instr_addr = cpu->pc;
        cpu->cur_instr.w = cpu->fetch32(cpu, cpu->pc);
        cpu->pc += 4;
        cpu->next_instr_addr = cpu->pc;
        cpu->next_instr.w = cpu->fetch32(cpu, cpu->pc);
        cpu->pc += 4;
    }
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

void cpu_update_mode(ArmCore* cpu, CpuMode old) {
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
    switch (cpu->cpsr.m) {
        case M_USER:
        case M_FIQ:
        case M_IRQ:
        case M_SVC:
        case M_ABT:
        case M_UND:
        case M_SYSTEM:
            return;
        default:
            eprintf("illegal cpu mode: pc = %08x\n", cpu->pc);
    }
}

void cpu_handle_interrupt(ArmCore* cpu, CpuInterrupt intr) {
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
    cpu_update_mode(cpu, old);
    cpu->spsr = spsr;
    cpu->lr = cpu->pc;
    if (cpu->cpsr.t) {
        if (intr == I_SWI || intr == I_UND) cpu->lr -= 2;
    } else cpu->lr -= 4;
    cpu->cpsr.t = 0;
    cpu->cpsr.i = 1;
    cpu->pc = cpu->vector_base + 4 * intr;
    cpu_flush(cpu);
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

void cpu_print_state(ArmCore* cpu) {
    static char* reg_names[16] = {"r0", "r1", "r2", "r3", "r4",  "r5",
                                  "r6", "r7", "r8", "r9", "r10", "r11",
                                  "ip", "sp", "lr", "pc"};
    for (int i = 0; i < 4; i++) {
        if (i == 0) printf("CPU%d ", cpu->v5 ? 9 : 7);
        else printf("     ");
        for (int j = 0; j < 4; j++) {
            printf("%3s=0x%08x ", reg_names[4 * i + j], cpu->r[4 * i + j]);
        }
        printf("\n");
    }
    printf("     cpsr=%08x (n=%d,z=%d,c=%d,v=%d,q=%d,i=%d,f=%d,t=%d,m=%s)\n",
           cpu->cpsr.w, cpu->cpsr.n, cpu->cpsr.z, cpu->cpsr.c, cpu->cpsr.v,
           cpu->cpsr.q, cpu->cpsr.i, cpu->cpsr.v, cpu->cpsr.t,
           mode_name(cpu->cpsr.m));
}

void cpu_print_cur_instr(ArmCore* cpu) {
    if (cpu->cpsr.t) {
        ThumbInstr instr = {cpu->fetch16(cpu, cpu->cur_instr_addr)};
        printf("%08x: %04x ", cpu->cur_instr_addr, instr.h);
        thumb_disassemble(instr, cpu->cur_instr_addr, stdout);
        printf("\n");
    } else {
        printf("%08x: %08x ", cpu->cur_instr_addr, cpu->cur_instr.w);
        arm_disassemble(cpu->cur_instr, cpu->cur_instr_addr, stdout);
        printf("\n");
    }
}