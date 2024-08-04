#include "recompiler.h"

#include "../arm.h"
#include "../thumb.h"

#define MAX_BLOCK_INSTRS 128

ArmCompileFunc compile_funcs[ARM_MAX] = {
    [ARM_DATAPROC] = compile_arm_data_proc,
    [ARM_MOV] = compile_arm_data_proc,
    [ARM_HALFTRANS] = compile_arm_half_trans,
    [ARM_SINGLETRANS] = compile_arm_single_trans,
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
#define EMITX_STORE_REG(rn, op, imm)                                           \
    (EMITXX(STORE_REG, rn, op, 1, imm),                                        \
     rn == 15 ? (EMITV(END, 0, 0), false) : true)
#define EMITV_STORE_REG(rn, op) EMITX_STORE_REG(rn, op, 0)
#define EMITI_STORE_REG(rn, op) EMITX_STORE_REG(rn, op, 1)

#define LASTV (block->code.size - 1)

void compile_block(ArmCore* cpu, IRBlock* block, u32 start_addr) {
    u32 addr = start_addr;

    block->start_addr = addr;

    if (cpu->cpsr.t) {
        for (int i = 0; i < MAX_BLOCK_INSTRS; i++) {
            ArmInstr instr = thumb_lookup[cpu->fetch16(cpu, addr)];
            if (!arm_compile_instr(block, cpu, addr, instr)) {
                addr += 2;
                break;
            }
            addr += 2;
        }

        (void) EMITI_STORE_REG(15, addr);
    } else {
        for (int i = 0; i < MAX_BLOCK_INSTRS; i++) {
            ArmInstr instr = {cpu->fetch32(cpu, addr)};
            if (!arm_compile_instr(block, cpu, addr, instr)) {
                addr += 4;
                break;
            }
            addr += 4;
        }
        (void) EMITI_STORE_REG(15, addr);
    }
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
        // u32 shift = instr.data_proc.op2 >> 4;

        // if (shift & 1) {
        //     cpu_fetch_instr(cpu);
        //     op2 = cpu->r[rm];

        //     u32 rs = shift >> 4;
        //     u32 shift_amt = cpu->r[rs] & 0xff;

        //     if (shift_amt >= 32) {
        //         switch ((shift >> 1) & 0b11) {
        //             case S_LSL:
        //                 if (shift_amt == 32) c = op2 & 1;
        //                 else c = 0;
        //                 op2 = 0;
        //                 break;
        //             case S_LSR:
        //                 if (shift_amt == 32) c = op2 >> 31;
        //                 else c = 0;
        //                 op2 = 0;
        //                 break;
        //             case S_ASR:
        //                 if (op2 >> 31) {
        //                     c = 1;
        //                     op2 = -1;
        //                 } else {
        //                     c = 0;
        //                     op2 = 0;
        //                 }
        //                 break;
        //             case S_ROR:
        //                 shift_amt %= 32;
        //                 c = (op2 >> (shift_amt - 1)) & 1;
        //                 op2 = (op2 >> shift_amt) | (op2 << (32 - shift_amt));
        //                 break;
        //         }
        //     } else if (shift_amt > 0) {
        //         op2 =
        //             arm_shifter(cpu, (shift & 0b111) | shift_amt << 3, op2,
        //             &c);
        //     }

        //     op1 = cpu->r[instr.data_proc.rn];
        // } else {
        //     op2 = arm_shifter(cpu, shift, cpu->r[rm], &c);
        //     op1 = cpu->r[instr.data_proc.rn];
        //     cpu_fetch_instr(cpu);
        // }
        op1 = EMIT_LOAD_REG(instr.data_proc.rn, false);
        op2 = EMIT_LOAD_REG(rm, false);
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
        return EMITV_STORE_REG(instr.data_proc.rd, vres);
    }
    return true;
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
        return true;
    } else if (instr.half_trans.h) {
        if (instr.half_trans.l) {
            if (instr.half_trans.w || !instr.half_trans.p) {
                EMITV_STORE_REG(instr.half_trans.rn, vwback);
            }
            u32 vdata = EMITV(LOAD_MEM16, vaddr, 0);
            return EMITV_STORE_REG(instr.half_trans.rd, vdata);
        } else {
            u32 vdata = EMIT_LOAD_REG(instr.half_trans.rd, true);
            EMITV(STORE_MEM16, vaddr, vdata);
            if (instr.half_trans.w || !instr.half_trans.p) {
                EMITV_STORE_REG(instr.half_trans.rn, vwback);
            }
            return true;
        }
    }
    return false;
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
        // offset = cpu->r[rm];
        // u8 shift = instr.single_trans.offset >> 4;
        // u32 carry;
        // offset = arm_shifter(cpu, shift, offset, &carry);
        voffset = EMIT_LOAD_REG(rm, false);
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
            return EMITV_STORE_REG(instr.single_trans.rd, vdata);
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
            return EMITV_STORE_REG(instr.single_trans.rd, vdata);
            // if (instr.single_trans.rd == 15) {
            //     if (cpu->v5) cpu->cpsr.t = cpu->pc & 1;
            //     cpu_flush(cpu);
            // }
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

DECL_ARM_COMPILE(branch) {
    u32 offset = instr.branch.offset;
    offset = (s32) (offset << 8) >> 8;
    if (cpu->cpsr.t) offset <<= 1;
    else offset <<= 2;
    u32 dest = addr + 8 + offset;
    // if (instr.branch.l || instr.cond == 0xf) {
    //     if (cpu->cpsr.t) {
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
    //     } else {
    //         cpu->lr = (cpu->pc - 4) & ~0b11;
    //         cpu_fetch_instr(cpu);
    //         if (instr.cond == 0xf && cpu->v5) {
    //             dest += instr.branch.l << 1;
    //             cpu->cpsr.t = 1;
    //         }
    //     }
    // }
    return EMITI_STORE_REG(15, dest);
}