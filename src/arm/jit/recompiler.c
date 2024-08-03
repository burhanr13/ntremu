#include "recompiler.h"

#include "../arm.h"
#include "../thumb.h"

#define MAX_BLOCK_INSTRS 128

ArmCompileFunc compile_funcs[ARM_MAX] = {
    [ARM_DATAPROC] = compile_arm_data_proc,
    [ARM_MOV] = compile_arm_data_proc,
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

#define EMIT_LOADR(rn, fetched)                                                \
    (rn == 15 ? EMITI(MOV, 0, addr + (fetched ? 12 : 8))                       \
              : EMITV(LOAD_R(rn), 0, 0))
#define EMITX_STORER(rn, op, imm)                                              \
    (EMITX(STORE_R(rn), 0, op, imm),                                           \
     rn == 15 ? (EMITV(END, 0, 0), false) : true)
#define EMITV_STORER(rn, op) EMITX_STORER(rn, op, 0)
#define EMITI_STORER(rn, op) EMITX_STORER(rn, op, 1)

#define LASTV (block->code.size)

IRBlock* compile_block(ArmCore* cpu, u32 start_addr) {
    IRBlock* block = malloc(sizeof *block);
    u32 addr = start_addr;

    if (cpu->cpsr.t) {
        for (int i = 0; i < MAX_BLOCK_INSTRS; i++) {
            ArmInstr instr = thumb_lookup[cpu->fetch16(cpu, addr)];
            if (!arm_compile_instr(block, cpu, addr, instr)) {
                addr += 2;
                break;
            }
            addr += 2;
        }

        (void) EMITI_STORER(15, addr);
    } else {
        for (int i = 0; i < MAX_BLOCK_INSTRS; i++) {
            ArmInstr instr = {cpu->fetch32(cpu, addr)};
            if (!arm_compile_instr(block, cpu, addr, instr)) {
                addr += 4;
                break;
            }
            addr += 4;
        }
        (void) EMITI_STORER(15, addr);
    }
    return block;
}

u32 compile_cond(IRBlock* block, ArmInstr instr) {
    u32 jmpaddr = LASTV;
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
        EMITI(JNZ, 0, 0);
    } else {
        EMITI(JZ, 0, 0);
    }
    return jmpaddr;
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
        block->code.d[jmpaddr].op2 = LASTV - jmpaddr;
        return retval;
    } else {
        return func(block, cpu, addr, instr);
    }
}

bool compile_arm_data_proc(IRBlock* block, ArmCore* cpu, u32 addr,
                           ArmInstr instr) {
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
                //c = (op2 >> (shift_amt - 1)) & 1;
                op2 = (op2 >> shift_amt) | (op2 << (32 - shift_amt));
            }
        }
        op1 = EMIT_LOADR(instr.data_proc.rn, false);
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
        op1 = EMIT_LOADR(instr.data_proc.rn, false);
        op2 = EMIT_LOADR(rm, false);
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
        return EMITV_STORER(instr.data_proc.rd, vres);
    }
    return true;
}

bool compile_arm_branch(IRBlock* block, ArmCore* cpu, u32 addr,
                        ArmInstr instr) {
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
    return EMITI_STORER(15, dest);
}