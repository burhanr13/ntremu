#include "arm4_isa.h"

#include <stdio.h>

#include "arm7tdmi.h"
#include "bus7.h"

Arm4ExecFunc arm4_lookup[1 << 8][1 << 4];

void arm4_generate_lookup() {
    for (int dechi = 0; dechi < 1 << 8; dechi++) {
        for (int declo = 0; declo < 1 << 4; declo++) {
            arm4_lookup[dechi][declo] =
                arm4_decode_instr((ArmInstr){.dechi = dechi, .declo = declo});
        }
    }
}

Arm4ExecFunc arm4_decode_instr(ArmInstr instr) {
    if (instr.sw_intr.c1 == 0b1111) {
        return exec_arm4_sw_intr;
    } else if (instr.branch.c1 == 0b101) {
        return exec_arm4_branch;
    } else if (instr.block_trans.c1 == 0b100) {
        return exec_arm4_block_trans;
    } else if (instr.undefined.c1 == 0b011 && instr.undefined.c2 == 1) {
        return exec_arm4_undefined;
    } else if (instr.single_trans.c1 == 0b01) {
        return exec_arm4_single_trans;
    } else if (instr.branch_ex.c1 == 0b00010010 && instr.branch_ex.c3 == 0b00 &&
               instr.branch_ex.c4 == 1) {
        return exec_arm4_branch_ex;
    } else if (instr.swap.c1 == 0b00010 && instr.swap.c2 == 0b00 &&
               instr.swap.c4 == 0b1001) {
        return exec_arm4_swap;
    } else if (instr.multiply.c1 == 0b000000 && instr.multiply.c2 == 0b1001) {
        return exec_arm4_multiply;
    } else if (instr.multiply_long.c1 == 0b00001 &&
               instr.multiply_long.c2 == 0b1001) {
        return exec_arm4_multiply_long;
    } else if (instr.half_trans.c1 == 0b000 && instr.half_trans.c2 == 1 &&
               instr.half_trans.c3 == 1) {
        return exec_arm4_half_trans;
    } else if (instr.psr_trans.c1 == 0b00 && instr.psr_trans.c2 == 0b10 &&
               instr.psr_trans.c3 == 0) {
        return exec_arm4_psr_trans;
    } else if (instr.data_proc.c1 == 0b00) {
        return exec_arm4_data_proc;
    } else {
        return exec_arm4_undefined;
    }
}

static bool eval_cond(Arm7TDMI* cpu, ArmInstr instr) {
    if (instr.cond == C_AL) return true;
    switch (instr.cond) {
        case C_EQ:
            return cpu->cpsr.z;
        case C_NE:
            return !cpu->cpsr.z;
        case C_CS:
            return cpu->cpsr.c;
        case C_CC:
            return !cpu->cpsr.c;
        case C_MI:
            return cpu->cpsr.n;
        case C_PL:
            return !cpu->cpsr.n;
        case C_VS:
            return cpu->cpsr.v;
        case C_VC:
            return !cpu->cpsr.v;
        case C_HI:
            return cpu->cpsr.c && !cpu->cpsr.z;
        case C_LS:
            return !cpu->cpsr.c || cpu->cpsr.z;
        case C_GE:
            return cpu->cpsr.n == cpu->cpsr.v;
        case C_LT:
            return cpu->cpsr.n != cpu->cpsr.v;
        case C_GT:
            return !cpu->cpsr.z && (cpu->cpsr.n == cpu->cpsr.v);
        case C_LE:
            return cpu->cpsr.z || (cpu->cpsr.n != cpu->cpsr.v);
        default:
            return true;
    }
}

void arm4_exec_instr(Arm7TDMI* cpu) {
    ArmInstr instr = cpu->cur_instr;
    if (!eval_cond(cpu, instr)) {
        cpu7_fetch_instr(cpu);
        return;
    }

    arm4_lookup[instr.dechi][instr.declo](cpu, instr);
}

static u32 arm_shifter(Arm7TDMI* cpu, u8 shift, u32 operand, u32* carry) {
    u32 shift_type = (shift >> 1) & 0b11;
    u32 shift_amt = shift >> 3;
    if (shift_amt) {
        switch (shift_type) {
            case S_LSL:
                *carry = (operand >> (32 - shift_amt)) & 1;
                return operand << shift_amt;
            case S_LSR:
                *carry = (operand >> (shift_amt - 1)) & 1;
                return operand >> shift_amt;
            case S_ASR:
                *carry = (operand >> (shift_amt - 1)) & 1;
                return (s32) operand >> shift_amt;
            case S_ROR:
                *carry = (operand >> (shift_amt - 1)) & 1;
                return (operand >> shift_amt) | (operand << (32 - shift_amt));
        }
    } else {
        switch (shift_type) {
            case S_LSL:
                return operand;
            case S_LSR:
                *carry = operand >> 31;
                return 0;
            case S_ASR:
                *carry = operand >> 31;
                return (operand >> 31) ? -1 : 0;
            case S_ROR:
                *carry = operand & 1;
                return (operand >> 1) | (cpu->cpsr.c << 31);
        }
    }
    return 0;
}

void exec_arm4_data_proc(Arm7TDMI* cpu, ArmInstr instr) {
    u32 op1, op2;

    u32 z, c = cpu->cpsr.c, n, v = cpu->cpsr.v;
    if (instr.data_proc.i) {
        if (cpu->cpsr.t) {
            op2 = instr.data_proc.op2;
        } else {
            op2 = instr.data_proc.op2 & 0xff;
            u32 shift_amt = instr.data_proc.op2 >> 8;
            if (shift_amt) {
                shift_amt *= 2;
                c = (op2 >> (shift_amt - 1)) & 1;
                op2 = (op2 >> shift_amt) | (op2 << (32 - shift_amt));
            }
        }
        op1 = cpu->r[instr.data_proc.rn];
        cpu7_fetch_instr(cpu);
    } else {
        u32 rm = instr.data_proc.op2 & 0b1111;
        u32 shift = instr.data_proc.op2 >> 4;

        if (shift & 1) {
            cpu7_fetch_instr(cpu);
            op2 = cpu->r[rm];

            u32 rs = shift >> 4;
            u32 shift_amt = cpu->r[rs] & 0xff;

            if (shift_amt >= 32) {
                switch ((shift >> 1) & 0b11) {
                    case S_LSL:
                        if (shift_amt == 32) c = op2 & 1;
                        else c = 0;
                        op2 = 0;
                        break;
                    case S_LSR:
                        if (shift_amt == 32) c = op2 >> 31;
                        else c = 0;
                        op2 = 0;
                        break;
                    case S_ASR:
                        if (op2 >> 31) {
                            c = 1;
                            op2 = -1;
                        } else {
                            c = 0;
                            op2 = 0;
                        }
                        break;
                    case S_ROR:
                        shift_amt %= 32;
                        c = (op2 >> (shift_amt - 1)) & 1;
                        op2 = (op2 >> shift_amt) | (op2 << (32 - shift_amt));
                        break;
                }
            } else if (shift_amt > 0) {
                op2 =
                    arm_shifter(cpu, (shift & 0b111) | shift_amt << 3, op2, &c);
            }

            op1 = cpu->r[instr.data_proc.rn];
        } else {
            op2 = arm_shifter(cpu, shift, cpu->r[rm], &c);
            op1 = cpu->r[instr.data_proc.rn];
            cpu7_fetch_instr(cpu);
        }
    }
    if (instr.data_proc.rn == 15 && instr.data_proc.rd != 15) op1 &= ~0b10;

    if (instr.data_proc.s) {
        u32 res = 0;
        bool arith = false;
        u32 car = 0;
        u32 tmp;
        bool save = true;
        switch (instr.data_proc.opcode) {
            case A_AND:
                res = op1 & op2;
                break;
            case A_EOR:
                res = op1 ^ op2;
                break;
            case A_SUB:
                arith = true;
                op2 = ~op2;
                car = 1;
                break;
            case A_RSB:
                arith = true;
                tmp = op1;
                op1 = op2;
                op2 = ~tmp;
                car = 1;
                break;
            case A_ADD:
                arith = true;
                break;
            case A_ADC:
                arith = true;
                car = cpu->cpsr.c;
                break;
            case A_SBC:
                arith = true;
                op2 = ~op2;
                car = cpu->cpsr.c;
                break;
            case A_RSC:
                arith = true;
                tmp = op1;
                op1 = op2;
                op2 = ~tmp;
                car = cpu->cpsr.c;
                break;
            case A_TST:
                res = op1 & op2;
                save = false;
                break;
            case A_TEQ:
                res = op1 ^ op2;
                save = false;
                break;
            case A_CMP:
                arith = true;
                op2 = ~op2;
                car = 1;
                save = false;
                break;
            case A_CMN:
                arith = true;
                save = false;
                break;
            case A_ORR:
                res = op1 | op2;
                break;
            case A_MOV:
                res = op2;
                break;
            case A_BIC:
                res = op1 & ~op2;
                break;
            case A_MVN:
                res = ~op2;
                break;
        }

        if (arith) {
            res = op1 + op2;
            c = (op1 > res) || (res > res + car);
            res += car;
            v = (op1 >> 31) == (op2 >> 31) && (op1 >> 31) != (res >> 31);
        }
        z = (res == 0) ? 1 : 0;
        n = (res >> 31) & 1;

        if (instr.data_proc.rd == 15) {
            CpuMode mode = cpu->cpsr.m;
            if (!(mode == M_USER || mode == M_SYSTEM)) {
                cpu->cpsr.w = cpu->spsr;
                cpu7_update_mode(cpu, mode);
            }
        } else {
            cpu->cpsr.z = z;
            cpu->cpsr.n = n;
            cpu->cpsr.c = c;
            cpu->cpsr.v = v;
        }
        if (save) {
            cpu->r[instr.data_proc.rd] = res;
            if (instr.data_proc.rd == 15) cpu7_flush(cpu);
        }
    } else {
        u32 rd = instr.data_proc.rd;
        switch (instr.data_proc.opcode) {
            case A_AND:
                cpu->r[rd] = op1 & op2;
                break;
            case A_EOR:
                cpu->r[rd] = op1 ^ op2;
                break;
            case A_SUB:
                cpu->r[rd] = op1 - op2;
                break;
            case A_RSB:
                cpu->r[rd] = op2 - op1;
                break;
            case A_ADD:
                cpu->r[rd] = op1 + op2;
                break;
            case A_ADC:
                cpu->r[rd] = op1 + op2 + cpu->cpsr.c;
                break;
            case A_SBC:
                cpu->r[rd] = op1 - op2 - 1 + cpu->cpsr.c;
                break;
            case A_RSC:
                cpu->r[rd] = op2 - op1 - 1 + cpu->cpsr.c;
                break;
            case A_TST:
                return;
            case A_TEQ:
                return;
            case A_CMP:
                return;
            case A_CMN:
                return;
            case A_ORR:
                cpu->r[rd] = op1 | op2;
                break;
            case A_MOV:
                cpu->r[rd] = op2;
                break;
            case A_BIC:
                cpu->r[rd] = op1 & ~op2;
                break;
            case A_MVN:
                cpu->r[rd] = ~op2;
                break;
        }
        if (instr.data_proc.rd == 15) cpu7_flush(cpu);
    }
}

void exec_arm4_psr_trans(Arm7TDMI* cpu, ArmInstr instr) {
    if (instr.psr_trans.op) {
        u32 op2;
        if (instr.psr_trans.i) {
            op2 = instr.psr_trans.op2 & 0xff;
            u32 rot = instr.psr_trans.op2 >> 7;
            op2 = (op2 >> rot) | (op2 << (32 - rot));
        } else {
            u32 rm = instr.psr_trans.op2 & 0b1111;
            op2 = cpu->r[rm];
        }
        u32 mask = 0;
        if (instr.psr_trans.f) mask |= 0xff000000;
        if (instr.psr_trans.s) mask |= 0x00ff0000;
        if (instr.psr_trans.x) mask |= 0x0000ff00;
        if (instr.psr_trans.c) mask |= 0x000000ff;
        if (cpu->cpsr.m == M_USER) mask &= 0xf0000000;
        op2 &= mask;
        if (instr.psr_trans.p) {
            cpu->spsr &= ~mask;
            cpu->spsr |= op2;
        } else {
            CpuMode m = cpu->cpsr.m;
            cpu->cpsr.w &= ~mask;
            cpu->cpsr.w |= op2;
            cpu7_update_mode(cpu, m);
        }
    } else {
        u32 psr;
        if (instr.psr_trans.p) {
            psr = cpu->spsr;
        } else {
            psr = cpu->cpsr.w;
        }
        cpu->r[instr.psr_trans.rd] = psr;
    }
    cpu7_fetch_instr(cpu);
}

void exec_arm4_multiply(Arm7TDMI* cpu, ArmInstr instr) {
    cpu7_fetch_instr(cpu);
    u32 res = cpu->r[instr.multiply.rm] * cpu->r[instr.multiply.rs];
    if (instr.multiply.a) {
        res += cpu->r[instr.multiply.rn];
    }
    cpu->r[instr.multiply.rd] = res;
    if (instr.multiply.s) {
        cpu->cpsr.z = (cpu->r[instr.multiply.rd] == 0) ? 1 : 0;
        cpu->cpsr.n = (cpu->r[instr.multiply.rd] >> 31) & 1;
    }
}

void exec_arm4_multiply_long(Arm7TDMI* cpu, ArmInstr instr) {
    cpu7_fetch_instr(cpu);
    u64 res;
    if (instr.multiply_long.u) {
        s64 sres;
        sres = (s64) ((s32) cpu->r[instr.multiply_long.rm]) *
               (s64) ((s32) cpu->r[instr.multiply_long.rs]);
        res = sres;
    } else {
        res = (u64) cpu->r[instr.multiply_long.rm] *
              (u64) cpu->r[instr.multiply_long.rs];
    }
    if (instr.multiply_long.a) {
        res += (u64) cpu->r[instr.multiply_long.rdlo] |
               ((u64) cpu->r[instr.multiply_long.rdhi] << 32);
    }
    if (instr.multiply_long.s) {
        cpu->cpsr.z = (res == 0) ? 1 : 0;
        cpu->cpsr.n = (res >> 63) & 1;
    }
    cpu->r[instr.multiply_long.rdlo] = res;
    cpu->r[instr.multiply_long.rdhi] = res >> 32;
}

void exec_arm4_swap(Arm7TDMI* cpu, ArmInstr instr) {
    u32 addr = cpu->r[instr.swap.rn];
    cpu7_fetch_instr(cpu);
    if (instr.swap.b) {
        u8 data = cpu7_read8(cpu, addr, false);
        cpu7_write8(cpu, addr, cpu->r[instr.swap.rm]);
        cpu->r[instr.swap.rd] = data;
    } else {
        u32 data = cpu7_read32(cpu, addr);
        cpu7_write32(cpu, addr, cpu->r[instr.swap.rm]);
        cpu->r[instr.swap.rd] = data;
    }
}

void exec_arm4_branch_ex(Arm7TDMI* cpu, ArmInstr instr) {
    cpu7_fetch_instr(cpu);
    cpu->pc = cpu->r[instr.branch_ex.rn];
    cpu->cpsr.t = cpu->r[instr.branch_ex.rn] & 1;
    cpu7_flush(cpu);
}

void exec_arm4_half_trans(Arm7TDMI* cpu, ArmInstr instr) {
    u32 addr = cpu->r[instr.half_trans.rn];
    u32 offset;
    if (instr.half_trans.i) {
        offset = instr.half_trans.offlo | (instr.half_trans.offhi << 4);
    } else {
        offset = cpu->r[instr.half_trans.offlo];
    }
    cpu7_fetch_instr(cpu);

    if (!instr.half_trans.u) offset = -offset;
    u32 wback = addr + offset;
    if (instr.half_trans.p) addr = wback;

    if (instr.half_trans.s) {
        if (instr.half_trans.l) {
            if (instr.half_trans.w || !instr.half_trans.p) {
                cpu->r[instr.half_trans.rn] = wback;
            }
            if (instr.half_trans.h) {
                cpu->r[instr.half_trans.rd] = cpu7_read16(cpu, addr, true);
            } else {
                cpu->r[instr.half_trans.rd] = cpu7_read8(cpu, addr, true);
            }
            if (instr.half_trans.rd == 15) cpu7_flush(cpu);
        }
    } else if (instr.half_trans.h) {
        if (instr.half_trans.l) {
            if (instr.half_trans.w || !instr.half_trans.p) {
                cpu->r[instr.half_trans.rn] = wback;
            }
            cpu->r[instr.half_trans.rd] = cpu7_read16(cpu, addr, false);
            if (instr.half_trans.rd == 15) cpu7_flush(cpu);
        } else {
            cpu7_write16(cpu, addr, cpu->r[instr.half_trans.rd]);
            if (instr.half_trans.w || !instr.half_trans.p) {
                cpu->r[instr.half_trans.rn] = wback;
            }
        }
    }
}

void exec_arm4_single_trans(Arm7TDMI* cpu, ArmInstr instr) {
    u32 addr = cpu->r[instr.single_trans.rn];
    if (instr.single_trans.rn == 15) addr &= ~0b10;
    u32 offset;
    if (instr.single_trans.i) {
        u32 rm = instr.single_trans.offset & 0b1111;
        offset = cpu->r[rm];
        u8 shift = instr.single_trans.offset >> 4;
        u32 carry;
        offset = arm_shifter(cpu, shift, offset, &carry);
    } else {
        offset = instr.single_trans.offset;
    }

    cpu7_fetch_instr(cpu);

    if (!instr.single_trans.u) offset = -offset;
    u32 wback = addr + offset;
    if (instr.single_trans.p) addr = wback;

    if (instr.single_trans.b) {
        if (instr.single_trans.l) {
            if (instr.single_trans.w || !instr.single_trans.p) {
                cpu->r[instr.single_trans.rn] = wback;
            }
            cpu->r[instr.single_trans.rd] = cpu7_read8(cpu, addr, false);
            if (instr.single_trans.rd == 15) cpu7_flush(cpu);
        } else {
            cpu7_write8(cpu, addr, cpu->r[instr.single_trans.rd]);
            if (instr.single_trans.w || !instr.single_trans.p) {
                cpu->r[instr.single_trans.rn] = wback;
            }
        }
    } else {
        if (instr.single_trans.l) {
            if (instr.single_trans.w || !instr.single_trans.p) {
                cpu->r[instr.single_trans.rn] = wback;
            }
            cpu->r[instr.single_trans.rd] = cpu7_read32(cpu, addr);
            if (instr.single_trans.rd == 15) cpu7_flush(cpu);
        } else {
            cpu7_write32(cpu, addr, cpu->r[instr.single_trans.rd]);
            if (instr.single_trans.w || !instr.single_trans.p) {
                cpu->r[instr.single_trans.rn] = wback;
            }
        }
    }
}

void exec_arm4_undefined(Arm7TDMI* cpu, ArmInstr instr) {
    cpu7_handle_interrupt(cpu, I_UND);
}

u32* get_user_reg7(Arm7TDMI* cpu, int reg) {
    if (reg < 8 || reg == 15) return &cpu->r[reg];
    if (reg < 13) {
        if (cpu->cpsr.m == M_FIQ) return &cpu->banked_r8_12[0][reg - 8];
        else return &cpu->r[reg];
    }
    if (reg == 13) return &cpu->banked_sp[0];
    if (reg == 14) return &cpu->banked_lr[0];
    return NULL;
}

void exec_arm4_block_trans(Arm7TDMI* cpu, ArmInstr instr) {
    int rcount = 0;
    int rlist[16];
    u32 addr = cpu->r[instr.block_trans.rn];
    u32 wback = addr;
    if (instr.block_trans.rlist) {
        for (int i = 0; i < 16; i++) {
            if (instr.block_trans.rlist & (1 << i)) rlist[rcount++] = i;
        }
        if (instr.block_trans.u) {
            wback += 4 * rcount;
        } else {
            wback -= 4 * rcount;
            addr = wback;
        }
    } else {
        rcount = 1;
        rlist[0] = 15;
        if (instr.block_trans.u) {
            wback += 0x40;
        } else {
            wback -= 0x40;
            addr = wback;
        }
    }

    if (instr.block_trans.p == instr.block_trans.u) addr += 4;
    cpu7_fetch_instr(cpu);

    if (instr.block_trans.s &&
        !((instr.block_trans.rlist & (1 << 15)) && instr.block_trans.l)) {
        if (instr.block_trans.l) {
            if (instr.block_trans.w) cpu->r[instr.block_trans.rn] = wback;
            for (int i = 0; i < rcount; i++) {
                *get_user_reg7(cpu, rlist[i]) = cpu7_read32m(cpu, addr, i);
            }
        } else {
            for (int i = 0; i < rcount; i++) {
                cpu7_write32m(cpu, addr, i, *get_user_reg7(cpu, rlist[i]));
                if (i == 0 && instr.block_trans.w)
                    cpu->r[instr.block_trans.rn] = wback;
            }
        }
    } else {
        if (instr.block_trans.l) {
            if (instr.block_trans.w) cpu->r[instr.block_trans.rn] = wback;
            for (int i = 0; i < rcount; i++) {
                cpu->r[rlist[i]] = cpu7_read32m(cpu, addr, i);
            }
            if ((instr.block_trans.rlist & (1 << 15)) ||
                !instr.block_trans.rlist) {
                if (instr.block_trans.s) {
                    CpuMode mode = cpu->cpsr.m;
                    if (!(mode == M_USER || mode == M_SYSTEM)) {
                        cpu->cpsr.w = cpu->spsr;
                        cpu7_update_mode(cpu, mode);
                    }
                }
                cpu7_flush(cpu);
            }
        } else {
            for (int i = 0; i < rcount; i++) {
                cpu7_write32m(cpu, addr, i, cpu->r[rlist[i]]);
                if (i == 0 && instr.block_trans.w)
                    cpu->r[instr.block_trans.rn] = wback;
            }
        }
    }
}

void exec_arm4_branch(Arm7TDMI* cpu, ArmInstr instr) {
    u32 offset = instr.branch.offset;
    if (offset & (1 << 23)) offset |= 0xff000000;
    if (cpu->cpsr.t) offset <<= 1;
    else offset <<= 2;
    u32 dest = cpu->pc + offset;
    if (instr.branch.l) {
        if (cpu->cpsr.t) {
            if (offset & (1 << 23)) {
                offset %= 1 << 23;
                cpu->lr += offset;
                dest = cpu->lr;
                cpu->lr = (cpu->pc - 2) | 1;
            } else {
                if (offset & (1 << 22)) dest += 0xff800000;
                cpu->lr = dest;
                cpu7_fetch_instr(cpu);
                return;
            }
        } else {
            cpu->lr = (cpu->pc - 4) & ~0b11;
        }
    }
    cpu7_fetch_instr(cpu);
    cpu->pc = dest;
    cpu7_flush(cpu);
}

void exec_arm4_sw_intr(Arm7TDMI* cpu, ArmInstr instr) {
    cpu7_handle_interrupt(cpu, I_SWI);
}
