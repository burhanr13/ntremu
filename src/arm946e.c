#include "arm946e.h"

#include <stdio.h>

#include "arm5_isa.h"
#include "arm_common.h"
#include "bus9.h"
#include "nds.h"
#include "thumb.h"
#include "types.h"

bool cpu9_step(Arm946E* cpu) {
    cpu->cycles = 0;
    if (cpu->halt) {
        if (cpu->irq) {
            cpu->halt = false;
        } else {
            return false;
        }
    }
    if (!cpu->cpsr.i && cpu->irq) {
        cpu9_handle_interrupt(cpu, I_IRQ);
    } else {
        arm5_exec_instr(cpu);
    }
    if (cpu->cycles == 0) cpu->cycles = 1;
    return true;
}

void cpu9_fetch_instr(Arm946E* cpu) {
    cpu->cur_instr = cpu->next_instr;
    if (cpu->cpsr.t) {
        cpu->next_instr = thumb_lookup[cpu9_fetch16(cpu, cpu->pc)];
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
        cpu->cur_instr = thumb_lookup[cpu9_fetch16(cpu, cpu->pc)];
        cpu->pc += 2;
        cpu->next_instr = thumb_lookup[cpu9_fetch16(cpu, cpu->pc)];
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
    cpu->cpsr.t = 0;
    cpu->cpsr.i = 1;
    cpu->pc = 0xffff0000 + 4 * intr;
    cpu9_flush(cpu);
}

u32 cpu9_read8(Arm946E* cpu, u32 addr, bool sx) {
    cpu->cycles++;
    u32 data;
    if (addr < cpu->itcm_virtsize) data = *(u8*) &cpu->itcm[addr % ITCMSIZE];
    else if (addr - cpu->dtcm_base < cpu->dtcm_virtsize)
        data = *(u8*) &cpu->dtcm[addr % DTCMSIZE];
    else data = bus9_read8(cpu->master, addr);
    if (sx) data = (s8) data;
    return data;
}

u32 cpu9_read16(Arm946E* cpu, u32 addr, bool sx) {
    cpu->cycles++;
    u32 data;
    if (addr < cpu->itcm_virtsize)
        data = *(u16*) &cpu->itcm[(addr & ~1) % ITCMSIZE];
    else if (addr - cpu->dtcm_base < cpu->dtcm_virtsize)
        data = *(u16*) &cpu->dtcm[(addr & ~1) % DTCMSIZE];
    else data = bus9_read16(cpu->master, addr & ~1);
    if (sx) data = (s16) data;
    return data;
}

u32 cpu9_read32(Arm946E* cpu, u32 addr) {
    cpu->cycles++;
    u32 data;
    if (addr < cpu->itcm_virtsize)
        data = *(u32*) &cpu->itcm[(addr & ~3) % ITCMSIZE];
    else if (addr - cpu->dtcm_base < cpu->dtcm_virtsize)
        data = *(u32*) &cpu->dtcm[(addr & ~3) % DTCMSIZE];
    else data = bus9_read32(cpu->master, addr & ~3);
    if (addr & 0b11) {
        data =
            (data >> (8 * (addr & 0b11))) | (data << (32 - 8 * (addr & 0b11)));
    }
    return data;
}

u32 cpu9_read32m(Arm946E* cpu, u32 addr, int i) {
    cpu->cycles++;
    if (addr < cpu->itcm_virtsize)
        return *(u32*) &cpu->itcm[((addr & ~3) + 4 * i) % ITCMSIZE];
    else if (addr - cpu->dtcm_base < cpu->dtcm_virtsize)
        return *(u32*) &cpu->dtcm[((addr & ~3) + 4 * i) % DTCMSIZE];
    else return bus9_read32(cpu->master, (addr & ~3) + 4 * i);
}

void cpu9_write8(Arm946E* cpu, u32 addr, u8 b) {
    cpu->cycles++;
    if (addr < cpu->itcm_virtsize) *(u8*) &cpu->itcm[addr % ITCMSIZE] = b;
    else if (addr - cpu->dtcm_base < cpu->dtcm_virtsize)
        *(u8*) &cpu->dtcm[addr % DTCMSIZE] = b;
    else bus9_write8(cpu->master, addr, b);
}

void cpu9_write16(Arm946E* cpu, u32 addr, u16 h) {
    cpu->cycles++;
    if (addr < cpu->itcm_virtsize)
        *(u16*) &cpu->itcm[(addr & ~1) % ITCMSIZE] = h;
    else if (addr - cpu->dtcm_base < cpu->dtcm_virtsize)
        *(u16*) &cpu->dtcm[(addr & ~1) % DTCMSIZE] = h;
    else bus9_write16(cpu->master, addr & ~1, h);
}

void cpu9_write32(Arm946E* cpu, u32 addr, u32 w) {
    cpu->cycles++;
    if (addr < cpu->itcm_virtsize)
        *(u32*) &cpu->itcm[(addr & ~3) % ITCMSIZE] = w;
    else if (addr - cpu->dtcm_base < cpu->dtcm_virtsize)
        *(u32*) &cpu->dtcm[(addr & ~3) % DTCMSIZE] = w;
    else bus9_write32(cpu->master, addr & ~3, w);
}

void cpu9_write32m(Arm946E* cpu, u32 addr, int i, u32 w) {
    cpu->cycles++;
    if (addr < cpu->itcm_virtsize)
        *(u32*) &cpu->itcm[((addr & ~3) + 4 * i) % ITCMSIZE] = w;
    else if (addr - cpu->dtcm_base < cpu->dtcm_virtsize)
        *(u32*) &cpu->dtcm[((addr & ~3) + 4 * i) % DTCMSIZE] = w;
    else bus9_write32(cpu->master, (addr & ~3) + 4 * i, w);
}

u16 cpu9_fetch16(Arm946E* cpu, u32 addr) {
    u16 data;
    if (addr < cpu->itcm_virtsize)
        data = *(u16*) &cpu->itcm[(addr & ~1) % ITCMSIZE];
    else {
        data = bus9_read16(cpu->master, addr & ~1);
        if (cpu->master->memerr && !cpu->master->cpuerr) {
            printf("Invalid CPU9 (thumb) instruction fetch at 0x%08x\n", addr);
            cpu->master->cpuerr = true;
        }
    }
    return data;
}

u32 cpu9_fetch32(Arm946E* cpu, u32 addr) {
    u32 data;
    if (addr < cpu->itcm_virtsize)
        data = *(u32*) &cpu->itcm[(addr & ~3) % ITCMSIZE];
    else {
        data = bus9_read32(cpu->master, addr & ~3);
        if (cpu->master->memerr && !cpu->master->cpuerr) {
            printf("Invalid CPU9 instruction fetch at 0x%08x\n", addr);
            cpu->master->cpuerr = true;
        }
    }
    return data;
}

u32 cp15_read(Arm946E* cpu, u32 cn, u32 cm, u32 cp) {
    if (cn == 0 && cm == 0) {
        if (cp == 0) {
            return 0x41059460;
        }
    }
    if (cn == 9 && cm == 1) {
        u32 virtsize = 0, base = 0;
        if (cp == 0) {
            virtsize = cpu->dtcm_virtsize;
            base = cpu->dtcm_base;
        } else if (cp == 1) {
            virtsize = cpu->itcm_virtsize;
        }
        virtsize >>= 10;
        while (virtsize) {
            base += 2;
            virtsize >>= 1;
        }
        return base;
    }
    return 0;
}

void cp15_write(Arm946E* cpu, u32 cn, u32 cm, u32 cp, u32 data) {
    if (cn == 7) {
        if ((cm == 0 && cp == 4) || (cm == 8 && cp == 2)) {
            cpu->halt = true;
        }
    } else if (cn == 9 && cm == 1) {
        u32 virtsize = 512 << ((data & 0x3e) >> 1);
        u32 base = data & 0xfffff000;
        if (cp == 0) {
            cpu->dtcm_virtsize = virtsize;
            cpu->dtcm_base = base;
        } else if (cp == 1) {
            cpu->itcm_virtsize = virtsize;
        }
    }
}

void print_cpu9_state(Arm946E* cpu) {
    static char* reg_names[16] = {"r0", "r1", "r2", "r3", "r4",  "r5",
                                  "r6", "r7", "r8", "r9", "r10", "r11",
                                  "ip", "sp", "lr", "pc"};
    for (int i = 0; i < 4; i++) {
        if (i == 0) printf("CPU9 ");
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

void print_cur_instr9(Arm946E* cpu) {
    if (cpu->cpsr.t) {
        ThumbInstr instr = {bus9_read16(cpu->master, cpu->cur_instr_addr)};
        printf("%08x: %04x ", cpu->cur_instr_addr, instr.h);
        thumb_disassemble(instr, cpu->cur_instr_addr, stdout);
        printf("\n");
    } else {
        printf("%08x: %08x ", cpu->cur_instr_addr, cpu->cur_instr.w);
        arm_disassemble(cpu->cur_instr, cpu->cur_instr_addr, stdout);
        printf("\n");
    }
}