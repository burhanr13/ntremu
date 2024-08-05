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

#define INSTRLEN (cpu->cpsr.t ? 2 : 4)

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
    (rn == 15 ? EMITI(MOV, 0, addr + (fetched ? 3 * INSTRLEN : 2 * INSTRLEN))  \
              : EMITXX(LOAD_REG, rn, 0, 1, 0))
#define EMITX_STORE_REG(rn, op, imm) EMITXX(STORE_REG, rn, op, 1, imm)
#define EMITV_STORE_REG(rn, op) EMITX_STORE_REG(rn, op, 0)
#define EMITI_STORE_REG(rn, op) EMITX_STORE_REG(rn, op, 1)

#define LASTV (block->code.size - 1)

void compile_block(ArmCore* cpu, IRBlock* block, u32 start_addr) {
    u32 addr = start_addr;
    EMITV(NOP, 0, 0);

    for (int i = 0; i < MAX_BLOCK_INSTRS; i++) {
        ArmInstr instr = cpu->cpsr.t ? thumb_lookup[cpu->fetch16(cpu, addr)]
                                     : (ArmInstr){cpu->fetch32(cpu, addr)};
        if (!arm_compile_instr(block, cpu, addr, instr)) {
            addr += INSTRLEN;
            break;
        }
        addr += INSTRLEN;
    }
    EMITI_STORE_REG(15, addr);
    EMITV(END, 0, 0);
    block->end_addr = addr;
}

#define NF 0x80000000
#define ZF 0x40000000
#define CF 0x20000000
#define VF 0x10000000

u32 compile_cond(IRBlock* block, ArmInstr instr) {
    u32 vcpsr = EMITV(LOAD_CPSR, 0, 0);
    switch (instr.cond & ~1) {
        case C_EQ:
            EMITI(AND, LASTV, ZF);
            break;
        case C_CS:
            EMITI(AND, LASTV, CF);
            break;
        case C_MI:
            EMITI(AND, LASTV, NF);
            break;
        case C_VS:
            EMITI(AND, LASTV, VF);
            break;
        case C_HI: {
            u32 tmp = EMITI(AND, LASTV, CF);
            EMITI(AND, vcpsr, ZF);
            EMITV(NOT, 0, LASTV);
            EMITI(LSR, LASTV, 1);
            EMITV(AND, tmp, LASTV);
            break;
        }
        case C_GE: {
            u32 tmp = EMITI(AND, LASTV, NF);
            EMITI(AND, vcpsr, VF);
            EMITI(LSL, LASTV, 3);
            EMITV(XOR, tmp, LASTV);
            instr.cond ^= 1;
            break;
        }
        case C_GT: {
            u32 tmp = EMITI(AND, LASTV, NF);
            EMITI(AND, vcpsr, VF);
            EMITI(LSL, LASTV, 3);
            tmp = EMITV(XOR, tmp, LASTV);
            EMITI(AND, vcpsr, ZF);
            EMITI(LSL, LASTV, 1);
            EMITV(OR, tmp, LASTV);
            instr.cond ^= 1;
            break;
        }
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
                    bool imm, u32* carry) {
    if (!imm || shamt) {
        switch (op) {
            case S_LSL:
                if (carry) {
                    if (imm) {
                        EMITI(LSR, operand, 32 - shamt);
                    } else {
                        EMITXV(SUB, 32, shamt, 1);
                        EMITV(LSR, operand, LASTV);
                    }
                    *carry = EMITI(AND, LASTV, 1);
                }
                return EMITX(LSL, operand, shamt, imm);
            case S_LSR:
                if (carry) {
                    if (imm) {
                        EMITI(LSR, operand, shamt - 1);
                    } else {
                        EMITI(SUB, shamt, 1);
                        EMITV(LSR, operand, LASTV);
                    }
                    *carry = EMITI(AND, LASTV, 1);
                }
                return EMITX(LSR, operand, shamt, imm);
            case S_ASR:
                if (carry) {
                    if (imm) {
                        EMITI(LSR, operand, shamt - 1);
                    } else {
                        EMITI(SUB, shamt, 1);
                        EMITV(ASR, operand, LASTV);
                    }
                    *carry = EMITI(AND, LASTV, 1);
                }
                return EMITX(ASR, operand, shamt, imm);
            case S_ROR:
                if (carry) {
                    if (imm) {
                        EMITI(LSR, operand, shamt - 1);
                    } else {
                        EMITI(SUB, shamt, 1);
                        EMITV(LSR, operand, LASTV);
                    }
                    *carry = EMITI(AND, LASTV, 1);
                }
                return EMITX(ROR, operand, shamt, imm);
        }
    } else {
        switch (op) {
            case S_LSL:
                return operand;
            case S_LSR:
                if (carry) *carry = EMITI(LSR, operand, 31);
                return EMITI(MOV, 0, 0);
            case S_ASR:
                if (carry) *carry = EMITI(LSR, operand, 31);
                return EMITI(ASR, operand, 31);
            case S_ROR:
                if (carry) *carry = EMITI(AND, operand, 1);
                EMITV(LOAD_CPSR, 0, 0);
                EMITI(AND, LASTV, CF);
                EMITV(SETC, 0, LASTV);
                return EMITV(RRC, operand, 0);
        }
    }
    return operand;
}

DECL_ARM_COMPILE(data_proc) {
    u32 op1, op2;
    bool imm = instr.data_proc.i;

    bool usingop1 =
        instr.data_proc.opcode != A_MOV && instr.data_proc.opcode != A_MVN;

    u32 shiftc = 0;
    if (instr.data_proc.i) {
        if (cpu->cpsr.t) {
            op2 = instr.data_proc.op2;
        } else {
            op2 = instr.data_proc.op2 & 0xff;
            u32 shift_amt = instr.data_proc.op2 >> 8;
            if (shift_amt) {
                shift_amt *= 2;
                if (instr.data_proc.s)
                    shiftc = EMITI(MOV, 0, (op2 >> (shift_amt - 1)) & 1);
                op2 = (op2 >> shift_amt) | (op2 << (32 - shift_amt));
            }
        }
        if (usingop1) op1 = EMIT_LOAD_REG(instr.data_proc.rn, false);
    } else {
        u32 rm = instr.data_proc.op2 & 0b1111;
        u32 shift = instr.data_proc.op2 >> 4;
        u32 shift_type = (shift >> 1) & 0b11;

        u32 shamt;
        bool immshift = !(shift & 1);
        if (shift & 1) {
            if (usingop1) op1 = EMIT_LOAD_REG(instr.data_proc.rn, true);
            op2 = EMIT_LOAD_REG(rm, true);

            u32 rs = shift >> 4;
            EMIT_LOAD_REG(rs, true);
            shamt = EMITI(AND, LASTV, 0xff);
            if (instr.data_proc.s) {
                op2 = compile_shifter(block, cpu, shift_type, op2, shamt,
                                      immshift, &shiftc);
                EMITV(LOAD_CPSR, 0, 0);
                EMITI(AND, LASTV, CF);
                EMITV(SETC, 0, LASTV);
                shiftc = EMITV(GETCIFZ, shamt, shiftc);
            } else {
                op2 = compile_shifter(block, cpu, shift_type, op2, shamt,
                                      immshift, NULL);
            }
        } else {
            if (usingop1) op1 = EMIT_LOAD_REG(instr.data_proc.rn, false);
            op2 = EMIT_LOAD_REG(rm, false);

            shamt = shift >> 3;
            op2 = compile_shifter(block, cpu, shift_type, op2, shamt, immshift,
                                  instr.data_proc.s ? &shiftc : NULL);
        }
    }
    if (instr.data_proc.rn == 15 && instr.data_proc.rd != 15 && cpu->cpsr.t) {
        op1 = EMITI(AND, op1, ~3);
    }

    bool save = true;
    bool arith = false;
    switch (instr.data_proc.opcode) {
        case A_AND:
            EMITX(AND, op1, op2, imm);
            break;
        case A_EOR:
            EMITX(XOR, op1, op2, imm);
            break;
        case A_SUB:
            EMITX(SUB, op1, op2, imm);
            arith = true;
            break;
        case A_RSB:
            EMITXV(SUB, op2, op1, imm);
            arith = true;
            break;
        case A_ADD:
            EMITX(ADD, op1, op2, imm);
            arith = true;
            break;
        case A_ADC:
            EMITV(LOAD_CPSR, 0, 0);
            EMITI(AND, LASTV, CF);
            EMITV(SETC, 0, LASTV);
            EMITX(ADC, op1, op2, imm);
            arith = true;
            break;
        case A_SBC:
            EMITV(LOAD_CPSR, 0, 0);
            EMITI(AND, LASTV, CF);
            EMITV(SETC, 0, LASTV);
            EMITX(SBC, op1, op2, imm);
            arith = true;
            break;
        case A_RSC:
            EMITV(LOAD_CPSR, 0, 0);
            EMITI(AND, LASTV, CF);
            EMITV(SETC, 0, LASTV);
            EMITXV(SBC, op2, op1, imm);
            arith = true;
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
            arith = true;
            save = false;
            break;
        case A_CMN:
            EMITX(ADD, op1, op2, imm);
            arith = true;
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
            if (!(cpu->cpsr.m == M_USER || cpu->cpsr.m == M_SYSTEM)) {
                EMITV(LOAD_SPSR, 0, 0);
                EMITV(STORE_CPSR, 0, LASTV);
                block->modeswitch = true;
            }
        } else {
            u32 cpsrmask = 0x3fffffff;
            u32 vc, vv;
            if (arith) {
                cpsrmask = 0x0fffffff;
                vc = EMITV(GETC, 0, 0);
                vv = EMITV(GETV, 0, 0);
            } else if (shiftc) {
                cpsrmask = 0x1fffffff;
            }
            u32 vn = EMITV(GETN, 0, vres);
            u32 vz = EMITV(GETZ, 0, vres);
            EMITV(LOAD_CPSR, 0, 0);
            u32 vcpsr = EMITI(AND, LASTV, cpsrmask);
            EMITI(LSL, vn, 31);
            vcpsr = EMITV(OR, vcpsr, LASTV);
            EMITI(LSL, vz, 30);
            vcpsr = EMITV(OR, vcpsr, LASTV);
            if (arith) {
                EMITI(LSL, vc, 29);
                vcpsr = EMITV(OR, vcpsr, LASTV);
                EMITI(LSL, vv, 28);
                vcpsr = EMITV(OR, vcpsr, LASTV);
            } else if (shiftc) {
                EMITI(LSL, shiftc, 29);
                vcpsr = EMITV(OR, vcpsr, LASTV);
            }
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
    if (instr.single_trans.rn == 15 && cpu->cpsr.t) {
        vaddr = EMITI(AND, vaddr, ~3);
    }
    u32 voffset;
    u32 immoffset = !instr.single_trans.i;
    if (instr.single_trans.i) {
        u32 rm = instr.single_trans.offset & 0b1111;
        voffset = EMIT_LOAD_REG(rm, false);
        u8 shift = instr.single_trans.offset >> 4;
        u8 shamt = shift >> 3;
        u8 op = (shift >> 1) & 3;
        voffset = compile_shifter(block, cpu, op, voffset, shamt, true, NULL);
    } else {
        voffset = instr.single_trans.offset;
    }

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

    u32 vwback;
    if (instr.block_trans.u) {
        vwback = EMITI(ADD, vaddr, wboff);
    } else {
        vwback = EMITI(SUB, vaddr, wboff);
        vaddr = vwback;
    }
    if (instr.block_trans.p == instr.block_trans.u) {
        vaddr = EMITI(ADD, vaddr, 4);
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
                    if (!(cpu->cpsr.m == M_USER || cpu->cpsr.m == M_SYSTEM)) {
                        EMITV(LOAD_SPSR, 0, 0);
                        EMITV(STORE_CPSR, 0, LASTV);
                        block->modeswitch = true;
                    }
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
    u32 dest = addr + 2 * INSTRLEN + offset;
    if (instr.branch.l || instr.cond == 0xf) {
        if (cpu->cpsr.t) {
            if (offset & (1 << 23)) {
                offset %= 1 << 23;
                EMIT_LOAD_REG(14, false);
                u32 vdest = EMITI(ADD, LASTV, offset);
                dest = cpu->lr;
                EMITI_STORE_REG(14, addr + 3);
                if (instr.cond == 0xf && cpu->v5) {
                    block->modeswitch = 1;
                }
                EMITV_STORE_REG(15, vdest);
                EMITV(END, 0, 0);
                return false;
            } else {
                if (offset & (1 << 22)) dest += 0xff800000;
                EMITI_STORE_REG(14, dest);
                return true;
            }
        } else {
            EMITI_STORE_REG(14, addr + 4);
            if (instr.cond == 0xf && cpu->v5) {
                dest += instr.branch.l << 1;
                dest |= 1;
                block->thumbswitch = true;
            }
        }
    }

    EMITI_STORE_REG(15, dest);
    EMITV(END, 0, 0);
    return false;
}