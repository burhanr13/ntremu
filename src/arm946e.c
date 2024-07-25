#include "arm946e.h"

#include <stdio.h>

#include "arm.h"
#include "arm_core.h"
#include "bus9.h"
#include "nds.h"
#include "thumb.h"
#include "types.h"

void arm9_init(Arm946E* cpu) {

    cpu->cp15_control.w = 0x0005307d;

    cpu->c.read8 = (void*) arm9_read8;
    cpu->c.read16 = (void*) arm9_read16;
    cpu->c.read32 = (void*) arm9_read32;
    cpu->c.write8 = (void*) arm9_write8;
    cpu->c.write16 = (void*) arm9_write16;
    cpu->c.write32 = (void*) arm9_write32;
    cpu->c.fetch16 = (void*) arm9_fetch16;
    cpu->c.fetch32 = (void*) arm9_fetch32;
    cpu->c.cp15_read = (void*) cp15_read;
    cpu->c.cp15_write = (void*) cp15_write;

    cpu->c.v5 = true;
    cpu->c.vector_base = 0xffff0000;
}

bool arm9_step(Arm946E* cpu) {
    cpu->c.cycles = 0;
    if (cpu->halt) {
        if (cpu->c.irq) {
            cpu->halt = false;
        } else {
            return false;
        }
    }
    if (!cpu->c.cpsr.i && cpu->c.irq) {
        cpu_handle_interrupt((ArmCore*) cpu, I_IRQ);
    } else {
        arm_exec_instr((ArmCore*) cpu);
    }
    if (cpu->c.cycles == 0) cpu->c.cycles = 1;
    return true;
}

#define READ(size, addr)                                                       \
    if (cpu->cp15_control.itcm_on && !cpu->cp15_control.itcm_load &&           \
        (addr) < cpu->itcm_virtsize)                                           \
        data = *(u##size*) &cpu->itcm[(addr) % ITCMSIZE];                      \
    else if (cpu->cp15_control.dtcm_on && !cpu->cp15_control.dtcm_load &&      \
             (addr) - cpu->dtcm_base < cpu->dtcm_virtsize)                     \
        data = *(u##size*) &cpu->dtcm[(addr) % DTCMSIZE];                      \
    else data = bus9_read##size(cpu->master, addr);

u32 arm9_read8(Arm946E* cpu, u32 addr, bool sx) {
    cpu->c.cycles++;
    u32 data;
    READ(8, addr);
    if (sx) data = (s8) data;
    return data;
}

u32 arm9_read16(Arm946E* cpu, u32 addr, bool sx) {
    cpu->c.cycles++;
    u32 data;
    READ(16, addr & ~1);
    if (sx) data = (s16) data;
    return data;
}

u32 arm9_read32(Arm946E* cpu, u32 addr) {
    cpu->c.cycles++;
    u32 data;
    READ(32, addr & ~3);
    if (addr & 0b11) {
        data =
            (data >> (8 * (addr & 0b11))) | (data << (32 - 8 * (addr & 0b11)));
    }
    return data;
}

#define WRITE(size, addr)                                                      \
    if (cpu->cp15_control.itcm_on && (addr) < cpu->itcm_virtsize)              \
        *(u##size*) &cpu->itcm[(addr) % ITCMSIZE] = data;                      \
    else if (cpu->cp15_control.dtcm_on &&                                      \
             (addr) - cpu->dtcm_base < cpu->dtcm_virtsize)                     \
        *(u##size*) &cpu->dtcm[(addr) % DTCMSIZE] = data;                      \
    else bus9_write##size(cpu->master, addr, data);

void arm9_write8(Arm946E* cpu, u32 addr, u8 data) {
    cpu->c.cycles++;
    WRITE(8,addr);
}

void arm9_write16(Arm946E* cpu, u32 addr, u16 data) {
    cpu->c.cycles++;
    WRITE(16, addr & ~1);
}

void arm9_write32(Arm946E* cpu, u32 addr, u32 data) {
    cpu->c.cycles++;
    WRITE(32, addr & ~3);
}

u16 arm9_fetch16(Arm946E* cpu, u32 addr) {
    u16 data;
    if (cpu->cp15_control.itcm_on && !cpu->cp15_control.itcm_load &&
        addr < cpu->itcm_virtsize)
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

u32 arm9_fetch32(Arm946E* cpu, u32 addr) {
    u32 data;
    if (cpu->cp15_control.itcm_on && !cpu->cp15_control.itcm_load &&
        addr < cpu->itcm_virtsize)
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
    switch (cn) {
        case 0:
            switch (cm) {
                case 0:
                    switch (cp) {
                        case 0:
                            return 0x41059461;
                        case 1:
                            return 0x0f0d2112;
                        case 2:
                            return 0x00140180;
                        default:
                            return 0x41059461;
                    }
                    break;
            }
            break;
        case 1:
            if (cm == 0 && cp == 0) {
                return cpu->cp15_control.w;
            }
            break;
        case 9:
            switch (cm) {
                case 1: {
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
            }
            break;
    }
    return 0;
}

void cp15_write(Arm946E* cpu, u32 cn, u32 cm, u32 cp, u32 data) {
    switch (cn) {
        case 1:
            if (cm == 0 && cp == 0) {
                u32 mask = 0x000ff085;
                data &= mask;
                cpu->cp15_control.w &= ~mask;
                cpu->cp15_control.w |= data;
                if (cpu->cp15_control.vector_base) {
                    cpu->c.vector_base = 0xffff0000;
                } else {
                    cpu->c.vector_base = 0x00000000;
                }
                cpu->c.v5 = !cpu->cp15_control.v4mode;
                return;
            }
            break;
        case 7:
            if ((cm == 0 && cp == 4) || (cm == 8 && cp == 2)) {
                cpu->halt = true;
                return;
            }
            break;
        case 9:
            if (cn == 9 && cm == 1) {
                u32 virtsize = 512 << ((data & 0x3e) >> 1);
                u32 base = data & 0xfffff000;
                if (cp == 0) {
                    cpu->dtcm_virtsize = virtsize;
                    cpu->dtcm_base = base;
                } else if (cp == 1) {
                    cpu->itcm_virtsize = virtsize;
                }
                return;
            }
            break;
    }
}
