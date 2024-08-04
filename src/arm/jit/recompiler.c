#include "recompiler.h"

#include "../arm.h"
#include "../thumb.h"

#define MAX_BLOCK_INSTRS 128

ArmCompileFunc compile_funcs[ARM_MAX] = {
    [ARM_DATAPROC] = compile_arm_data_proc,
    [ARM_MOV] = compile_arm_data_proc,
    [ARM_PSRTRANS] = compile_arm_psr_trans,
    [ARM_BRANCHEXCH] = compile_arm_branch_exch,
    [ARM_HALFTRANS] = compile_arm_half_trans,
    [ARM_SINGLETRANS] = compile_arm_single_trans,
    [ARM_BLOCKTRANS] = compile_arm_block_trans,
    [ARM_BRANCH] = compile_arm_branch,
};

#define EMITXX(opc, _op1, _op2, _imm1, _imm2)                                  \
    (irblock_write(block, (IRInstr){.opcode = IR_##opc,                        \
                                    .imm1 = _imm1,                             \
                                    .imm2 = _imm2,                             \
                                    .op1 = _op1,                               \
                                    .op2 = _op2}))

#define EMITV(opc, op1, op2) EMITXX(opc, op1, op2, 0, 0)
#define EMITI(opc, op1, op2) EMITXX(opc, op1, op2, 0, 1)
#define EMITX(opc, op1, op2, imm) EMITXX(opc, op1, op2, 0, imm)
#define EMITXV(opc, op1, op2, imm) EMITXX(opc, op1, op2, imm, 0)

#define EMIT_LOAD_REG(rn, fetched)                                             \
    (rn == 15 ? EMITI(MOV, 0, addr + (fetched ? 12 : 8))                       \
              : EMITXX(LOAD_REG, rn, 0, 1, 0))
#define EMITX_STORE_REG(rn, op, imm) EMITXX(STORE_REG, rn, op, 1, imm)
#define EMITV_STORE_REG(rn, op) EMITX_STORE_REG(rn, op, 0)
#define EMITI_STORE_REG(rn, op) EMITX_STORE_REG(rn, op, 1)

#define LASTV (block->code.size - 1)

void compile_block(ArmCore* cpu, IRBlock* block, u32 start_addr) {
    u32 addr = start_addr;
    EMITV(NOP, 0, 0);

    if (cpu->cpsr.t) {
        for (int i = 0; i < MAX_BLOCK_INSTRS; i++) {
            ArmInstr instr = thumb_lookup[cpu->fetch16(cpu, addr)];
            if (!arm_compile_instr(block, cpu, addr, instr)) {
                addr += 2;
                break;
            }
            addr += 2;
        }
    } else {
        for (int i = 0; i < MAX_BLOCK_INSTRS; i++) {
            ArmInstr instr = {cpu->fetch32(cpu, addr)};
            if (!arm_compile_instr(block, cpu, addr, instr)) {
                addr += 4;
                break;
            }
            addr += 4;
        }
    }
    EMITI_STORE_REG(15, addr);
    EMITV(END, 0, 0);
    block->end_addr = addr;
}

u32 compile_cond(IRBlock* block, ArmInstr instr) {
    EMITV(LOAD_CPSR, 0, 0);
    switch (instr.cond & ~1) {
        case C_EQ:
            EMITI(AND, LASTV, 0x40000000);
            break;
        case C_CS:
            EMITI(AND, LASTV, 0x20000000);
            break;
        case C_MI:
            EMITI(AND, LASTV, 0x80000000);
            break;
        case C_VS:
            EMITI(AND, LASTV, 0x10000000);
            break;
            // case C_HI:
            //     return cpu->cpsr.c && !cpu->cpsr.z;
            // case C_LS:
            //     return !cpu->cpsr.c || cpu->cpsr.z;
            // case C_GE:
            //     return cpu->cpsr.n == cpu->cpsr.v;
            // case C_LT:
            //     return cpu->cpsr.n != cpu->cpsr.v;
            // case C_GT:
            //     return !cpu->cpsr.z && (cpu->cpsr.n == cpu->cpsr.v);
            // case C_LE:
            //     return cpu->cpsr.z || (cpu->cpsr.n != cpu->cpsr.v);
    }
    if (instr.cond & 1) {
        EMITI(JNZ, LASTV, 0);
    } else {
        EMITI(JZ, LASTV, 0);
    }
    return LASTV;
}

bool arm_compile_instr(IRBlock* block, ArmCore* cpu, u32 addr, ArmInstr instr) {
    ArmCompileFunc func = compile_funcs[arm_lookup[instr.dechi][instr.declo]];
    if (!func) {
        eprintf("Unhandled format for JIT: %d\n",
                arm_lookup[instr.dechi][instr.declo]);
        return false;
    }
    if (instr.cond < C_AL) {
        u32 jmpaddr = compile_cond(block, instr);
        bool retval = func(block, cpu, addr, instr);
        block->code.d[jmpaddr].op2 = LASTV + 1 - jmpaddr;
        return retval;
    } else {
        return func(block, cpu, addr, instr);
    }
}

u32 compile_shifter(IRBlock* block, ArmCore* cpu, u8 op, u32 operand, u32 shamt,
                    bool imm) {
    if (!imm || shamt) {
        switch (op) {
            case S_LSL:
                //*carry = (operand >> (32 - shift_amt)) & 1;
                return EMITX(LSL, operand, shamt, imm);
            case S_LSR:
                //*carry = (operand >> (shift_amt - 1)) & 1;
                return EMITX(LSR, operand, shamt, imm);
            case S_ASR:
                //*carry = (operand >> (shift_amt - 1)) & 1;
                return EMITX(ASR, operand, shamt, imm);
            case S_ROR:
                //*carry = (operand >> (shift_amt - 1)) & 1;
                return EMITX(ROR, operand, shamt, imm);
        }
    } else {
        switch (op) {
            case S_LSL:
                return operand;
            case S_LSR:
                //*carry = operand >> 31;
                return EMITI(MOV, 0, 0);
            case S_ASR:
                //*carry = operand >> 31;
                return EMITI(ASR, operand, 31);
            case S_ROR:
                //*carry = operand & 1;
                // return (operand >> 1) | (cpu->cpsr.c << 31);
        }
    }
    return operand;
}

DECL_ARM_COMPILE(data_proc) {
    u32 op1, op2;
    bool imm = instr.data_proc.i;

    // u32 z, c = cpu->cpsr.c, n, v = cpu->cpsr.v;
    if (instr.data_proc.i) {
        if (cpu->cpsr.t) {
            op2 = instr.data_proc.op2;
        } else {
            op2 = instr.data_proc.op2 & 0xff;
            u32 shift_amt = instr.data_proc.op2 >> 8;
            if (shift_amt) {
                shift_amt *= 2;
                // c = (op2 >> (shift_amt - 1)) & 1;
                op2 = (op2 >> shift_amt) | (op2 << (32 - shift_amt));
            }
        }
        op1 = EMIT_LOAD_REG(instr.data_proc.rn, false);
    } else {
        u32 rm = instr.data_proc.op2 & 0b1111;
        u32 shift = instr.data_proc.op2 >> 4;
        u32 shift_type = (shift >> 1) & 0b11;

        u32 shamt;
        bool immshift = !(shift & 1);
        if (shift & 1) {
            op1 = EMIT_LOAD_REG(instr.data_proc.rn, true);
            op2 = EMIT_LOAD_REG(rm, true);

            u32 rs = shift >> 4;
            EMIT_LOAD_REG(rs, true);
            shamt = EMITI(AND, LASTV, 0xff);
        } else {
            op1 = EMIT_LOAD_REG(instr.data_proc.rn, false);
            op2 = EMIT_LOAD_REG(rm, false);

            shamt = shift >> 3;
        }

        op2 = compile_shifter(block, cpu, shift_type, op2, shamt, immshift);

        // if (shift & 1) {
        //     op2 = EMIT_LOAD_REG(rm, true);

        //     u32 rs = shift >> 4;
        //     EMIT_LOAD_REG(rs, true);
        //     u32 vshift_amt = EMITI(AND, LASTV, 0xff);
        //     switch ((shift >> 1) & 3) {
        //         case S_LSL:
        //             op2 = EMITV(LSL, op2, vshift_amt);
        //             break;
        //         case S_LSR:
        //             op2 = EMITV(LSR, op2, vshift_amt);
        //             break;
        //         case S_ASR:
        //             op2 = EMITV(ASR, op2, vshift_amt);
        //             break;
        //         case S_ROR:
        //             op2 = EMITV(ROR, op2, vshift_amt);
        //             break;
        //     }

        //     // if (shift_amt >= 32) {
        //     //     switch ((shift >> 1) & 0b11) {
        //     //         case S_LSL:
        //     //             if (shift_amt == 32) c = op2 & 1;
        //     //             else c = 0;
        //     //             op2 = 0;
        //     //             break;
        //     //         case S_LSR:
        //     //             if (shift_amt == 32) c = op2 >> 31;
        //     //             else c = 0;
        //     //             op2 = 0;
        //     //             break;
        //     //         case S_ASR:
        //     //             if (op2 >> 31) {
        //     //                 c = 1;
        //     //                 op2 = -1;
        //     //             } else {
        //     //                 c = 0;
        //     //                 op2 = 0;
        //     //             }
        //     //             break;
        //     //         case S_ROR:
        //     //             shift_amt %= 32;
        //     //             c = (op2 >> (shift_amt - 1)) & 1;
        //     //             op2 = (op2 >> shift_amt) | (op2 << (32 -
        //     shift_amt));
        //     //             break;
        //     //     }
        //     // }
        //     // else if (shift_amt > 0) {
        //     //     op2 =
        //     //         arm_shifter(cpu, (shift & 0b111) | shift_amt << 3,
        //     op2,
        //     //         &c);
        //     // }

        //     op1 = EMIT_LOAD_REG(instr.data_proc.rn, true);
        // }
        // else {
        //     op2 = EMIT_LOAD_REG(rm, false);
        //     op2 = compile_shifter(block, cpu, shift, op2);
        //     op1 = EMIT_LOAD_REG(instr.data_proc.rn, false);
        // }
    }
    // if (instr.data_proc.rn == 15 && instr.data_proc.rd != 15) op1 &= ~0b10;

    bool save = true;
    switch (instr.data_proc.opcode) {
        case A_AND:
            EMITX(AND, op1, op2, imm);
            break;
        case A_EOR:
            EMITX(XOR, op1, op2, imm);
            break;
        case A_SUB:
            EMITX(SUB, op1, op2, imm);
            break;
        case A_RSB:
            EMITXV(SUB, op2, op1, imm);
            break;
        case A_ADD:
            EMITX(ADD, op1, op2, imm);
            break;
        case A_ADC:
            // cpu->r[rd] = op1 + op2 + cpu->cpsr.c;
            break;
        case A_SBC:
            // cpu->r[rd] = op1 - op2 - 1 + cpu->cpsr.c;
            break;
        case A_RSC:
            // cpu->r[rd] = op2 - op1 - 1 + cpu->cpsr.c;
            break;
        case A_TST:
            EMITX(AND, op1, op2, imm);
            save = false;
            break;
        case A_TEQ:
            EMITX(XOR, op1, op2, imm);
            save = false;
            break;
        case A_CMP:
            EMITX(SUB, op1, op2, imm);
            save = false;
            break;
        case A_CMN:
            EMITX(ADD, op1, op2, imm);
            save = false;
            break;
        case A_ORR:
            EMITX(OR, op1, op2, imm);
            break;
        case A_MOV:
            EMITX(MOV, 0, op2, imm);
            break;
        case A_BIC:
            EMITX(NOT, 0, op2, imm);
            EMITV(AND, op1, LASTV);
            break;
        case A_MVN:
            EMITX(NOT, 0, op2, imm);
            break;
    }
    u32 vres = LASTV;

    if (instr.data_proc.s) {
        if (instr.data_proc.rd == 15) {
            // CpuMode mode = cpu->cpsr.m;
            // if (!(mode == M_USER || mode == M_SYSTEM)) {
            //     cpu->cpsr.w = cpu->spsr;
            //     cpu_update_mode(cpu, mode);
            // }
        } else {
            // cpu->cpsr.z = z;
            // cpu->cpsr.n = n;
            // cpu->cpsr.c = c;
            // cpu->cpsr.v = v;
            u32 vn = EMITV(SETN, 0, vres);
            u32 vz = EMITV(SETZ, 0, vres);
            EMITV(LOAD_CPSR, 0, 0);
            u32 vcpsr = EMITI(AND, LASTV, 0x3fffffff);
            EMITI(LSL, vn, 31);
            vcpsr = EMITV(OR, vcpsr, LASTV);
            EMITI(LSL, vz, 30);
            vcpsr = EMITV(OR, vcpsr, LASTV);
            EMITV(STORE_CPSR, 0, vcpsr);
        }
    }
    if (save) {
        EMITV_STORE_REG(instr.data_proc.rd, vres);
        if (instr.data_proc.rd == 15) {
            EMITV(END, 0, 0);
            return false;
        }
    }
    return true;
}

DECL_ARM_COMPILE(psr_trans) {
    if (instr.psr_trans.op) {
        u32 op2;
        bool imm = instr.psr_trans.i;
        if (instr.psr_trans.i) {
            op2 = instr.psr_trans.op2 & 0xff;
            u32 rot = instr.psr_trans.op2 >> 8 << 1;
            op2 = (op2 >> rot) | (op2 << (32 - rot));
        } else {
            u32 rm = instr.psr_trans.op2 & 0b1111;
            op2 = EMIT_LOAD_REG(rm, false);
        }
        u32 mask = 0;
        if (instr.psr_trans.f) mask |= 0xff000000;
        if (instr.psr_trans.s) mask |= 0x00ff0000;
        if (instr.psr_trans.x) mask |= 0x0000ff00;
        if (instr.psr_trans.c) mask |= 0x000000ff;
        if (cpu->cpsr.m == M_USER) mask &= 0xf0000000;

        if (imm) op2 &= mask;
        else op2 = EMITI(AND, op2, mask);

        if (instr.psr_trans.p) {
            EMITV(LOAD_SPSR, 0, 0);
        } else {
            EMITV(LOAD_CPSR, 0, 0);
        }
        EMITI(AND, LASTV, ~mask);
        EMITX(OR, LASTV, op2, imm);
        if (instr.psr_trans.p) {
            EMITV(STORE_SPSR, 0, LASTV);
            return true;
        } else {
            EMITV(STORE_CPSR, 0, LASTV);
            block->modeswitch = true;
            return false;
        }
    } else {
        if (instr.psr_trans.p) {
            EMITV(LOAD_SPSR, 0, 0);
        } else {
            EMITV(LOAD_CPSR, 0, 0);
        }
        EMITV_STORE_REG(instr.psr_trans.rd, LASTV);
        return true;
    }
}

DECL_ARM_COMPILE(branch_exch) {
    u32 vdest = EMIT_LOAD_REG(instr.branch_exch.rn, false);
    if (instr.branch_exch.l && cpu->v5) {
        if (cpu->cpsr.t) {
            EMITI_STORE_REG(14, addr + 3);
        } else {
            EMITI_STORE_REG(14, addr + 4);
        }
    }
    block->thumbswitch = true;
    EMITV_STORE_REG(15, vdest);
    EMITV(END, 0, 0);
    return false;
}

DECL_ARM_COMPILE(half_trans) {
    // if (cpu->v5 && !instr.half_trans.l && instr.half_trans.s &&
    //     (instr.half_trans.rd & 1)) {
    //     cpu_handle_interrupt(cpu, I_UND);
    //     return;
    // }

    u32 vaddr = EMIT_LOAD_REG(instr.half_trans.rn, false);
    u32 voffset;
    u32 immoffset = instr.half_trans.i;
    if (instr.half_trans.i) {
        voffset = instr.half_trans.offlo | (instr.half_trans.offhi << 4);
    } else {
        voffset = EMIT_LOAD_REG(instr.half_trans.offlo, false);
    }
    // cpu_fetch_instr(cpu);

    if (instr.half_trans.u) {
        EMITX(ADD, vaddr, voffset, immoffset);
    } else {
        EMITX(SUB, vaddr, voffset, immoffset);
    }
    u32 vwback = LASTV;
    if (instr.half_trans.p) vaddr = vwback;

    if (instr.half_trans.s) {
        // if (instr.half_trans.l) {
        //     if (instr.half_trans.w || !instr.half_trans.p) {
        //         cpu->r[instr.half_trans.rn] = wback;
        //     }
        //     if (instr.half_trans.h) {
        //         cpu->r[instr.half_trans.rd] = cpu->read16(cpu, addr, true);
        //     } else {
        //         cpu->r[instr.half_trans.rd] = cpu->read8(cpu, addr, true);
        //     }
        //     if (instr.half_trans.rd == 15) cpu_flush(cpu);
        // } else if (cpu->v5) {
        //     if (instr.half_trans.h) {
        //         cpu->write32(cpu, addr & ~3, cpu->r[instr.half_trans.rd]);
        //         cpu->write32(cpu, (addr & ~3) + 4,
        //                      cpu->r[instr.half_trans.rd + 1]);
        //         if (instr.half_trans.w || !instr.half_trans.p) {
        //             cpu->r[instr.half_trans.rn] = wback;
        //         }
        //     } else {
        //         if (instr.half_trans.w || !instr.half_trans.p) {
        //             cpu->r[instr.half_trans.rn] = wback;
        //         }
        //         cpu->r[instr.half_trans.rd] = cpu->read32(cpu, addr & ~3);
        //         cpu->r[instr.half_trans.rd + 1] =
        //             cpu->read32(cpu, (addr & ~3) + 4);
        //     }
        // }
        return false;
    } else if (instr.half_trans.h) {
        if (instr.half_trans.l) {
            if (instr.half_trans.w || !instr.half_trans.p) {
                EMITV_STORE_REG(instr.half_trans.rn, vwback);
            }
            u32 vdata = EMITV(LOAD_MEM16, vaddr, 0);
            EMITV_STORE_REG(instr.half_trans.rd, vdata);
            if (instr.half_trans.rd == 15) {
                EMITV(END, 0, 0);
                return false;
            }
            return true;
        } else {
            u32 vdata = EMIT_LOAD_REG(instr.half_trans.rd, true);
            EMITV(STORE_MEM16, vaddr, vdata);
            if (instr.half_trans.w || !instr.half_trans.p) {
                EMITV_STORE_REG(instr.half_trans.rn, vwback);
            }
            return true;
        }
    } else return false;
}

DECL_ARM_COMPILE(single_trans) {
    if (instr.cond == 0xf && cpu->v5) {
        return true;
    }

    u32 vaddr = EMIT_LOAD_REG(instr.single_trans.rn, false);
    // if (instr.single_trans.rn == 15) addr &= ~0b10;
    u32 voffset;
    u32 immoffset = !instr.single_trans.i;
    if (instr.single_trans.i) {
        u32 rm = instr.single_trans.offset & 0b1111;
        voffset = EMIT_LOAD_REG(rm, false);
        u8 shift = instr.single_trans.offset >> 4;
        u8 shamt = shift >> 3;
        u8 op = (shamt >> 1) & 3;
        if (op || shamt)
            voffset = compile_shifter(block, cpu, op, voffset, shamt, true);
    } else {
        voffset = instr.single_trans.offset;
    }

    // cpu_fetch_instr(cpu);

    if (instr.single_trans.u) {
        EMITX(ADD, vaddr, voffset, immoffset);
    } else {
        EMITX(SUB, vaddr, voffset, immoffset);
    }
    u32 vwback = LASTV;
    if (instr.single_trans.p) vaddr = vwback;

    if (instr.single_trans.b) {
        if (instr.single_trans.l) {
            if (instr.single_trans.w || !instr.single_trans.p) {
                EMITV_STORE_REG(instr.single_trans.rn, vwback);
            }
            u32 vdata = EMITV(LOAD_MEM8, vaddr, 0);
            EMITV_STORE_REG(instr.single_trans.rd, vdata);
            if (instr.single_trans.rd == 15) {
                EMITV(END, 0, 0);
                return false;
            }
            return true;
        } else {
            u32 vdata = EMIT_LOAD_REG(instr.single_trans.rd, true);
            EMITV(STORE_MEM8, vaddr, vdata);
            if (instr.single_trans.w || !instr.single_trans.p) {
                EMITV_STORE_REG(instr.single_trans.rn, vwback);
            }
            return true;
        }
    } else {
        if (instr.single_trans.l) {
            if (instr.single_trans.w || !instr.single_trans.p) {
                EMITV_STORE_REG(instr.single_trans.rn, vwback);
            }
            u32 vdata = EMITV(LOAD_MEM32, vaddr, 0);
            EMITV_STORE_REG(instr.single_trans.rd, vdata);
            if (instr.single_trans.rd == 15) {
                if (cpu->v5) block->thumbswitch = true;
                EMITV(END, 0, 0);
                return false;
            }
            return true;
        } else {
            u32 vdata = EMIT_LOAD_REG(instr.single_trans.rd, true);
            EMITV(STORE_MEM32, vaddr, vdata);
            if (instr.single_trans.w || !instr.single_trans.p) {
                EMITV_STORE_REG(instr.single_trans.rn, vwback);
            }
            return true;
        }
    }
}

DECL_ARM_COMPILE(block_trans) {
    int rcount = 0;
    int rlist[16];
    u32 vaddr = EMIT_LOAD_REG(instr.block_trans.rn, false);
    u32 wboff;
    if (instr.block_trans.rlist) {
        for (int i = 0; i < 16; i++) {
            if (instr.block_trans.rlist & (1 << i)) rlist[rcount++] = i;
        }
        wboff = rcount << 2;
    } else {
        if (cpu->v5) {
            rcount = 0;
        } else {
            rcount = 1;
            rlist[0] = 15;
        }
        wboff = 64;
    }

    if (!instr.block_trans.u) {
        wboff = -wboff;
    }
    if (instr.block_trans.p == instr.block_trans.u) wboff += 4;
    u32 vwback = EMITI(ADD, vaddr, wboff);
    if (!instr.block_trans.u) {
        vaddr = vwback;
    }

    // cpu_fetch_instr(cpu);

    vaddr = EMITI(AND, vaddr, ~3);

    if (instr.block_trans.s &&
        !((instr.block_trans.rlist & (1 << 15)) && instr.block_trans.l)) {
        // if (instr.block_trans.l) {
        //     for (int i = 0; i < rcount; i++) {
        //         if (i == rcount - 1 && instr.block_trans.w)
        //             cpu->r[instr.block_trans.rn] = wback;
        //         *get_user_reg(cpu, rlist[i]) =
        //             cpu->read32(cpu, (addr & ~3) + (i << 2));
        //     }
        //     if (rcount < 2 && instr.block_trans.w)
        //         cpu->r[instr.block_trans.rn] = wback;
        // } else {
        //     for (int i = 0; i < rcount; i++) {
        //         cpu->write32(cpu, (addr & ~3) + (i << 2),
        //                      *get_user_reg(cpu, rlist[i]));
        //     }
        //     if (instr.block_trans.w) cpu->r[instr.block_trans.rn] = wback;
        // }
        return false;
    } else {
        if (instr.block_trans.l) {
            if (cpu->cpsr.t || !cpu->v5) {
                if (instr.block_trans.w)
                    EMITV_STORE_REG(instr.block_trans.rn, vwback);
                for (int i = 0; i < rcount; i++) {
                    EMITV(LOAD_MEM32, vaddr, 0);
                    EMITV_STORE_REG(rlist[i], LASTV);
                    vaddr = EMITI(ADD, vaddr, 4);
                }
            } else {
                for (int i = 0; i < rcount; i++) {
                    if (i == rcount - 1 && instr.block_trans.w)
                        EMITV_STORE_REG(instr.block_trans.rn, vwback);
                    EMITV(LOAD_MEM32, vaddr, 0);
                    EMITV_STORE_REG(rlist[i], LASTV);
                    vaddr = EMITI(ADD, vaddr, 4);
                }
                if (rcount < 2 && instr.block_trans.w)
                    EMITV_STORE_REG(instr.block_trans.rn, vwback);
            }
            if (instr.block_trans.rlist & (1 << 15)) {
                if (cpu->v5) block->thumbswitch = true;
                if (instr.block_trans.s) {
                    // CpuMode mode = cpu->cpsr.m;
                    // if (!(mode == M_USER || mode == M_SYSTEM)) {
                    //     cpu->cpsr.w = cpu->spsr;
                    //     cpu_update_mode(cpu, mode);
                    // }
                }
                EMITV(END, 0, 0);
                return false;
            }
            return true;
        } else {
            if (cpu->v5) {
                for (int i = 0; i < rcount; i++) {
                    EMIT_LOAD_REG(rlist[i], true);
                    EMITV(STORE_MEM32, vaddr, LASTV);
                    vaddr = EMITI(ADD, vaddr, 4);
                }
                if (instr.block_trans.w)
                    EMITV_STORE_REG(instr.block_trans.rn, vwback);
            } else {
                for (int i = 0; i < rcount; i++) {
                    EMIT_LOAD_REG(rlist[i], true);
                    EMITV(STORE_MEM32, vaddr, LASTV);
                    vaddr = EMITI(ADD, vaddr, 4);
                    if (i == 0 && instr.block_trans.w)
                        EMITV_STORE_REG(instr.block_trans.rn, vwback);
                }
            }
            return true;
        }
    }
}

DECL_ARM_COMPILE(branch) {
    u32 offset = instr.branch.offset;
    offset = (s32) (offset << 8) >> 8;
    if (cpu->cpsr.t) offset <<= 1;
    else offset <<= 2;
    u32 dest = addr + 8 + offset;
    if (instr.branch.l || instr.cond == 0xf) {
        if (cpu->cpsr.t) {
            //         if (offset & (1 << 23)) {
            //             offset %= 1 << 23;
            //             cpu->lr += offset;
            //             dest = cpu->lr;
            //             cpu->lr = (cpu->pc - 2) | 1;
            //             cpu_fetch_instr(cpu);
            //             if (instr.cond == 0xf && cpu->v5) {
            //                 cpu->cpsr.t = 0;
            //             }
            //         } else {
            //             if (offset & (1 << 22)) dest += 0xff800000;
            //             cpu->lr = dest;
            //             cpu_fetch_instr(cpu);
            //             return;
            //         }
        } else {
            EMITI_STORE_REG(14, addr + 4);
            // if (instr.cond == 0xf && cpu->v5) {
            //     dest += instr.branch.l << 1;
            //     cpu->cpsr.t = 1;
            // }
        }
    }

    EMITI_STORE_REG(15, dest);
    EMITV(END, 0, 0);
    return false;
}