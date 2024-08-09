#include "backend_x86.h"

#include <capstone/capstone.h>
#include <xbyak/xbyak.h>

#define BACKEND_DISASM

struct Code : Xbyak::CodeGenerator {
    RegAllocation* regalloc;
    HostRegAllocation hralloc;
    ArmCore* cpu;

    Xbyak::Reg32 tempregs[6] = {esi, edi, r8d, r9d, r10d, r11d};
    Xbyak::Reg32 savedregs[5] = {ebp, r12d, r13d, r14d, r15d};

    Code(IRBlock* ir, RegAllocation* regalloc, ArmCore* cpu);

    void print_hostregs() {
        eprintf("Host Regs:");
        for (u32 i = 0; i < hralloc.nregs; i++) {
            eprintf(" $%d:", i);
            auto operand = _getOp(i);
            if (operand.isREG()) eprintf("%s", operand.toString());
            else eprintf("[rsp+%d]", 4 * hralloc.hostreg_info[i].index);
        }
        eprintf("\n");
    }

    int getSPDisp() {
        int disp =
            (hralloc.count[REG_SAVED] + 1) * 8 + hralloc.count[REG_STACK] * 4;
        disp = (disp + 15) & ~15;
        disp -= (hralloc.count[REG_SAVED] + 1) * 8;
        return disp;
    }

    Xbyak::Operand _getOp(int i) {
        HostRegInfo hr = hralloc.hostreg_info[i];
        switch (hr.type) {
            case REG_SCRATCH:
                return edx;
            case REG_TEMP:
                return tempregs[hr.index];
            case REG_SAVED:
                return savedregs[hr.index];
            case REG_STACK:
                return ptr[rsp + 4 * hr.index];
            default:
                break;
        }
        return rdx;
    }

    Xbyak::Operand getOp(int i) {
        return _getOp(regalloc->reg_assn[i]);
    }
};

#define CPU(m) (rbx + offsetof(ArmCore, m))
#define CPUR(n) (rbx + offsetof(ArmCore, r) + 4 * n)

#define OP(op, dest, src)                                                      \
    ({                                                                         \
        if ((src).isMEM() && (dest).isMEM()) {                                 \
            mov(edx, src);                                                     \
            op(dest, edx);                                                     \
        } else op(dest, src);                                                  \
    })

#define LOAD(addr)                                                             \
    ({                                                                         \
        auto dest = getOp(i);                                                  \
        OP(mov, dest, ptr[addr]);                                              \
        dest;                                                                  \
    })

#define STORE(addr)                                                            \
    ({                                                                         \
        if (inst.imm2) {                                                       \
            mov(dword[addr], inst.op2);                                        \
        } else {                                                               \
            auto src = getOp(inst.op2);                                        \
            OP(mov, ptr[addr], src);                                           \
        }                                                                      \
    })

#define SAMEREG(v1, v2) (regalloc->reg_assn[v1] == regalloc->reg_assn[v2])

#define BINARY(op)                                                             \
    ({                                                                         \
        bool op2eax = false;                                                   \
        if (!inst.imm2 && SAMEREG(inst.op2, i)) {                              \
            mov(eax, getOp(inst.op2));                                         \
            op2eax = true;                                                     \
        }                                                                      \
        auto dest = getOp(i);                                                  \
        if (inst.imm1) {                                                       \
            mov(dest, inst.op1);                                               \
        } else if (!SAMEREG(inst.op1, i)) {                                    \
            OP(mov, dest, getOp(inst.op1));                                    \
        }                                                                      \
        if (op2eax) {                                                          \
            op(dest, eax);                                                     \
        } else if (inst.imm2) {                                                \
            op(dest, inst.op2);                                                \
        } else {                                                               \
            OP(op, dest, getOp(inst.op2));                                     \
        }                                                                      \
    })

Code::Code(IRBlock* ir, RegAllocation* regalloc, ArmCore* cpu)
    : regalloc(regalloc), cpu(cpu) {

    hralloc =
        allocate_host_registers(regalloc, sizeof(tempregs) / sizeof(*tempregs),
                                sizeof(savedregs) / sizeof(*savedregs));

#ifdef BACKEND_DISASM
    print_hostregs();
#endif

    u32 flags_mask = 0;
    u32 lastflags = 0;
    u32 nlabels = 0;
    u32 jmptarget = 0;

    for (u32 i = 0; i < ir->code.size; i++) {
        IRInstr inst = ir->code.d[i];
        if (i == jmptarget && inst.opcode != IR_JELSE) {
            L(std::to_string(nlabels++));
            jmptarget = 0;
        }
        switch (inst.opcode) {
            case IR_LOAD_REG:
                LOAD(CPUR(inst.op1));
                break;
            case IR_STORE_REG:
                STORE(CPUR(inst.op1));
                break;
            case IR_LOAD_FLAG: {
                auto dest = LOAD(CPU(cpsr));
                shr(dest, 31 - inst.op1);
                and_(dest, 1);
                break;
            }
            case IR_STORE_FLAG: {
                if (ir->code.d[i - 2].opcode != IR_STORE_FLAG) {
                    xor_(ecx, ecx);
                    flags_mask = 0;
                }
                if (inst.imm2) {
                    if (inst.op2) or_(ecx, BIT(31 - inst.op1));
                } else {
                    mov(edx, getOp(inst.op2));
                    shl(edx, 31 - inst.op1);
                    or_(ecx, edx);
                }
                flags_mask |= BIT(31 - inst.op1);
                if (ir->code.d[i + 2].opcode != IR_STORE_FLAG) {
                    and_(dword[CPU(cpsr)], ~flags_mask);
                    or_(dword[CPU(cpsr)], ecx);
                }
                break;
            }
            case IR_LOAD_CPSR:
                LOAD(CPU(cpsr));
                break;
            case IR_STORE_CPSR:
                STORE(CPU(cpsr));
                break;
            case IR_LOAD_SPSR:
                LOAD(CPU(spsr));
                break;
            case IR_STORE_SPSR:
                STORE(CPU(spsr));
                break;
            case IR_LOAD_THUMB: {
                auto dest = LOAD(CPU(cpsr));
                shr(dest, 5);
                and_(dest, 1);
                break;
            }
            case IR_STORE_THUMB: {
                and_(dword[CPU(cpsr)], ~BIT(5));
                if (inst.imm2) {
                    if (inst.op2) or_(dword[CPU(cpsr)], BIT(5));
                } else {
                    mov(edx, getOp(inst.op2));
                    shl(edx, 5);
                    or_(dword[CPU(cpsr)], edx);
                }
                break;
            }
            case IR_LOAD_MEM8: {
                xor_(edx, edx);
                if (inst.imm1) {
                    mov(esi, inst.op1);
                } else {
                    auto src = getOp(inst.op1);
                    if (src != esi) mov(esi, src);
                }
                mov(rdi, rbx);
                mov(rax, (u64) cpu->read8);
                call(rax);
                mov(getOp(i), eax);
                break;
            }
            case IR_LOAD_MEMS8: {
                mov(edx, 1);
                if (inst.imm1) {
                    mov(esi, inst.op1);
                } else {
                    auto src = getOp(inst.op1);
                    if (src != esi) mov(esi, src);
                }
                mov(rdi, rbx);
                mov(rax, (u64) cpu->read8);
                call(rax);
                mov(getOp(i), eax);
                break;
            }
            case IR_LOAD_MEM16: {
                xor_(edx, edx);
                if (inst.imm1) {
                    mov(esi, inst.op1);
                } else {
                    auto src = getOp(inst.op1);
                    if (src != esi) mov(esi, src);
                }
                mov(rdi, rbx);
                mov(rax, (u64) cpu->read16);
                call(rax);
                mov(getOp(i), eax);
                break;
            }
            case IR_LOAD_MEMS16: {
                mov(edx, 1);
                if (inst.imm1) {
                    mov(esi, inst.op1);
                } else {
                    auto src = getOp(inst.op1);
                    if (src != esi) mov(esi, src);
                }
                mov(rdi, rbx);
                mov(rax, (u64) cpu->read16);
                call(rax);
                mov(getOp(i), eax);
                break;
            }
            case IR_LOAD_MEM32: {
                if (inst.imm1) {
                    mov(esi, inst.op1);
                } else {
                    auto src = getOp(inst.op1);
                    if (src != esi) mov(esi, src);
                }
                mov(rdi, rbx);
                mov(rax, (u64) cpu->read32);
                call(rax);
                mov(getOp(i), eax);
                break;
            }
            case IR_STORE_MEM8: {
                if (inst.imm2) {
                    mov(edx, inst.op2 & 0xff);
                } else {
                    auto src = getOp(inst.op2);
                    if (src.isMEM()) {
                        src.setBit(8);
                    } else {
                        src = src.getReg().changeBit(8);
                    }
                    movzx(edx, src);
                }
                if (inst.imm1) {
                    mov(esi, inst.op1);
                } else {
                    auto src = getOp(inst.op1);
                    if (src != esi) mov(esi, src);
                }
                mov(rdi, rbx);
                mov(rax, (u64) cpu->write8);
                call(rax);
                break;
            }
            case IR_STORE_MEM16: {
                if (inst.imm2) {
                    mov(edx, inst.op2 & 0xffff);
                } else {
                    auto src = getOp(inst.op2);
                    if (src.isMEM()) {
                        src.setBit(16);
                    } else {
                        src = src.getReg().changeBit(16);
                    }
                    movzx(edx, src);
                }
                if (inst.imm1) {
                    mov(esi, inst.op1);
                } else {
                    auto src = getOp(inst.op1);
                    if (src != esi) mov(esi, src);
                }
                mov(rdi, rbx);
                mov(rax, (u64) cpu->write16);
                call(rax);
                break;
            }
            case IR_STORE_MEM32: {
                if (inst.imm2) {
                    mov(edx, inst.op2);
                } else {
                    mov(edx, getOp(inst.op2));
                }
                if (inst.imm1) {
                    mov(esi, inst.op1);
                } else {
                    auto src = getOp(inst.op1);
                    if (src != esi) mov(esi, src);
                }
                mov(rdi, rbx);
                mov(rax, (u64) cpu->write32);
                call(rax);
                break;
            }
            case IR_MOV: {
                auto dst = getOp(i);
                if (inst.imm2) {
                    mov(dst, inst.op2);
                } else if (!SAMEREG(inst.op2, i)) {
                    OP(mov, dst, getOp(inst.op2));
                }
                break;
            }
            case IR_AND:
                BINARY(and_);
                break;
            case IR_OR:
                BINARY(or_);
                break;
            case IR_XOR:
                BINARY(xor_);
                break;
            case IR_NOT: {
                auto dst = getOp(i);
                if (inst.imm2) {
                    mov(dst, inst.op2);
                } else if (!SAMEREG(inst.op2, i)) {
                    OP(mov, dst, getOp(inst.op2));
                }
                not_(dst);
                break;
            }
            case IR_LSL: {
                bool op2eax = false;
                if (!inst.imm2 && SAMEREG(inst.op2, i)) {
                    mov(eax, getOp(inst.op2));
                    op2eax = true;
                }
                auto dest = getOp(i);
                if (inst.imm1) {
                    mov(dest, inst.op1);
                } else if (!SAMEREG(inst.op1, i)) {
                    OP(mov, dest, getOp(inst.op1));
                }
                if (inst.imm2) {
                    if (inst.op2 >= 32) {
                        mov(dest, 0);
                    } else {
                        shl(dest, inst.op2);
                    }
                } else {
                    if (op2eax) {
                        mov(cl, al);
                    } else {
                        mov(ecx, getOp(inst.op2));
                    }
                    cmp(cl, 32);
                    inLocalLabel();
                    jl(".normal");
                    mov(dest, 0);
                    jmp(".end");
                    L(".normal");
                    shl(dest, cl);
                    L(".end");
                    outLocalLabel();
                }
                break;
            }
            case IR_LSR: {
                bool op2eax = false;
                if (!inst.imm2 && SAMEREG(inst.op2, i)) {
                    mov(eax, getOp(inst.op2));
                    op2eax = true;
                }
                auto dest = getOp(i);
                if (inst.imm1) {
                    mov(dest, inst.op1);
                } else if (!SAMEREG(inst.op1, i)) {
                    OP(mov, dest, getOp(inst.op1));
                }
                if (inst.imm2) {
                    if (inst.op2 >= 32) {
                        mov(dest, 0);
                    } else {
                        shr(dest, inst.op2);
                    }
                } else {
                    if (op2eax) {
                        mov(cl, al);
                    } else {
                        mov(ecx, getOp(inst.op2));
                    }
                    cmp(cl, 32);
                    inLocalLabel();
                    jl(".normal");
                    mov(dest, 0);
                    jmp(".end");
                    L(".normal");
                    shr(dest, cl);
                    L(".end");
                    outLocalLabel();
                }
                break;
            }
            case IR_ASR: {
                bool op2eax = false;
                if (!inst.imm2 && SAMEREG(inst.op2, i)) {
                    mov(eax, getOp(inst.op2));
                    op2eax = true;
                }
                auto dest = getOp(i);
                if (inst.imm1) {
                    mov(dest, inst.op1);
                } else if (!SAMEREG(inst.op1, i)) {
                    OP(mov, dest, getOp(inst.op1));
                }
                if (inst.imm2) {
                    if (inst.op2 >= 32) {
                        sar(dest, 31);
                    } else {
                        sar(dest, inst.op2);
                    }
                } else {
                    if (op2eax) {
                        mov(cl, al);
                    } else {
                        mov(ecx, getOp(inst.op2));
                    }
                    cmp(cl, 32);
                    inLocalLabel();
                    jl(".normal");
                    sar(dest, 31);
                    jmp(".end");
                    L(".normal");
                    sar(dest, cl);
                    L(".end");
                    outLocalLabel();
                }
                break;
            }
            case IR_ROR: {
                bool op2eax = false;
                if (!inst.imm2 && SAMEREG(inst.op2, i)) {
                    mov(eax, getOp(inst.op2));
                    op2eax = true;
                }
                auto dest = getOp(i);
                if (inst.imm1) {
                    mov(dest, inst.op1);
                } else if (!SAMEREG(inst.op1, i)) {
                    OP(mov, dest, getOp(inst.op1));
                }
                if (inst.imm2) {
                    ror(dest, inst.op2);
                } else {
                    if (op2eax) {
                        mov(cl, al);
                    } else {
                        mov(ecx, getOp(inst.op2));
                    }
                    ror(dest, cl);
                }
                break;
            }
            case IR_RRC: {
                auto dst = getOp(i);
                if (inst.imm1) {
                    mov(dst, inst.op1);
                } else if (!SAMEREG(inst.op1, i)) {
                    OP(mov, dst, getOp(inst.op1));
                }
                rcr(dst, 1);
                break;
            }
            case IR_ADD:
                BINARY(add);
                break;
            case IR_SUB:
                BINARY(sub);
                if (ir->code.d[i + 1].opcode == IR_GETC) cmc();
                break;
            case IR_ADC:
                BINARY(adc);
                break;
            case IR_SBC:
                cmc();
                BINARY(sbb);
                if (ir->code.d[i + 1].opcode == IR_GETC) cmc();
                break;
            case IR_GETN: {
                auto dest = getOp(i);
                if (inst.imm2) {
                    mov(dest, inst.op2 >> 31);
                } else {
                    if (lastflags != inst.op2) {
                        auto src = getOp(inst.op2);
                        if (src.isMEM()) {
                            cmp(src, 0);
                        } else {
                            test(src.getReg(), src.getReg());
                        }
                        lahf();
                        lastflags = inst.op2;
                    }
                    if (dest.isMEM()) {
                        xor_(edx, edx);
                        test(ah, BIT(7));
                        setnz(dl);
                        mov(dest, edx);
                    } else {
                        xor_(dest, dest);
                        test(ah, BIT(7));
                        dest = dest.getReg().changeBit(8);
                        setnz(dest);
                    }
                }
                break;
            }
            case IR_GETZ: {
                auto dest = getOp(i);
                if (inst.imm2) {
                    mov(dest, inst.op2 == 0);
                } else {
                    if (lastflags != inst.op2) {
                        auto src = getOp(inst.op2);
                        if (src.isMEM()) {
                            cmp(src, 0);
                        } else {
                            test(src.getReg(), src.getReg());
                        }
                        lahf();
                        lastflags = inst.op2;
                    }
                    if (dest.isMEM()) {
                        xor_(edx, edx);
                        test(ah, BIT(6));
                        setnz(dl);
                        mov(dest, edx);
                    } else {
                        xor_(dest, dest);
                        test(ah, BIT(6));
                        dest = dest.getReg().changeBit(8);
                        setnz(dest);
                    }
                }
                break;
            }
            case IR_GETC: {
                lahf();
                seto(al);
                lastflags = inst.op2;
                auto dest = getOp(i);
                if (dest.isMEM()) {
                    xor_(edx, edx);
                    test(ah, BIT(0));
                    setnz(dl);
                    mov(dest, edx);
                } else {
                    xor_(dest, dest);
                    test(ah, BIT(0));
                    dest = dest.getReg().changeBit(8);
                    setnz(dest);
                }
                break;
            }
            case IR_SETC: {
                if (inst.imm2) {
                    if (inst.op2) stc();
                    else clc();
                } else {
                    shr(getOp(inst.op2), 1);
                }
                break;
            }
            case IR_GETCIFZ: {
                if (inst.imm1) {
                    if (inst.op1) {
                        if (inst.imm2) {
                            mov(getOp(i), inst.op2);
                        } else {
                            OP(mov, getOp(i), getOp(inst.op2));
                        }
                    } else {
                        auto dest = getOp(i);
                        setc(dl);
                        movzx(edx, dl);
                        mov(dest, edx);
                    }
                } else {
                    setc(dl);
                    movzx(edx, dl);
                    auto op1 = getOp(inst.op1);
                    if (op1.isMEM()) {
                        cmp(op1, 0);
                    } else {
                        test(op1.getReg(), op1.getReg());
                    }
                    if (inst.imm2) {
                        mov(ecx, inst.op2);
                        cmovne(edx, ecx);
                    } else {
                        cmovne(edx, getOp(inst.op2));
                    }
                    mov(getOp(i), edx);
                }
                break;
            }
            case IR_GETV: {
                auto dest = getOp(i);
                if (dest.isMEM()) {
                    movzx(edx, al);
                    mov(dest, edx);
                } else {
                    movzx(dest.getReg(), al);
                }
                break;
            }
            case IR_PCMASK: {
                if (inst.imm1) {
                    mov(getOp(i), inst.op1 ? ~1 : ~3);
                } else {
                    auto op1 = getOp(inst.op1);
                    if (op1.isMEM()) {
                        cmp(op1, 0);
                    } else {
                        test(op1.getReg(), op1.getReg());
                    }
                    auto dest = getOp(i);
                    if (dest.isMEM()) {
                        mov(ecx, ~3);
                        mov(edx, ~1);
                        cmovne(ecx, edx);
                        mov(dest, ecx);
                    } else {
                        mov(dest, ~3);
                        mov(edx, ~1);
                        cmovne(dest.getReg(), edx);
                    }
                }
                break;
            }
            case IR_JZ: {
                jmptarget = inst.op2;
                if (inst.imm1) {
                    if (!inst.op1) jmp(std::to_string(nlabels), T_NEAR);
                } else {
                    auto src = getOp(inst.op1);
                    if (src.isMEM()) {
                        cmp(src, 0);
                    } else {
                        test(src.getReg(), src.getReg());
                    }
                    jz(std::to_string(nlabels), T_NEAR);
                }
                break;
            }
            case IR_JNZ: {
                jmptarget = inst.op2;
                if (inst.imm1) {
                    if (inst.op1) jmp(std::to_string(nlabels), T_NEAR);
                } else {
                    auto src = getOp(inst.op1);
                    if (src.isMEM()) {
                        cmp(src, 0);
                    } else {
                        test(src.getReg(), src.getReg());
                    }
                    jnz(std::to_string(nlabels), T_NEAR);
                }
                break;
            }
            case IR_JELSE: {
                jmptarget = inst.op2;
                jmp(std::to_string(nlabels + 1), T_NEAR);
                L(std::to_string(nlabels++));
                break;
            }
            case IR_MODESWITCH: {
                mov(rdi, rbx);
                mov(esi, inst.op1);
                mov(rax, (u64) cpu_update_mode);
                call(rax);
                break;
            }
            case IR_EXCEPTION: {
                mov(byte[CPU(pending_flush)], 0);
                mov(rdi, rbx);
                mov(esi, inst.op1);
                mov(rax, (u64) cpu_handle_exception);
                call(rax);
                sub(dword[CPU(pc)], 8);
                break;
            }
            case IR_WFE: {
                mov(byte[CPU(wfe)], 1);
                break;
            }
            case IR_BEGIN: {
                push(rbx);
                mov(rbx, (u64) cpu);
                for (u32 i = 0; i < hralloc.count[REG_SAVED]; i++) {
                    push(savedregs[i].changeBit(64));
                }
                int spdisp = getSPDisp();
                if (spdisp) sub(rsp, spdisp);

                add(dword[CPU(cycles)], ir->numinstr);
                break;
            }
            case IR_END_LINK:
            case IR_END_RET: {
                mov(eax, ptr[CPU(pc)]);
                mov(ptr[CPU(cur_instr_addr)], eax);
                mov(byte[CPU(pending_flush)], 1);
                mov(dword[CPU(max_cycles)], 0);

                int spdisp = getSPDisp();
                if (spdisp) add(rsp, spdisp);
                for (int i = hralloc.count[REG_SAVED] - 1; i >= 0; i--) {
                    pop(savedregs[i].changeBit(64));
                }
                pop(rbx);
                ret();
                break;
            }
            default:
                break;
        }
    }

#ifdef BACKEND_DISASM
    csh handle;
    cs_insn* insn;
    cs_open(CS_ARCH_X86, CS_MODE_64, &handle);
    size_t count =
        cs_disasm(handle, getCode(), getSize(), (u64) getCode(), 0, &insn);
    eprintf("--------- JIT Disassembly ------------\n");
    for (size_t i = 0; i < count; i++) {
        eprintf("%04lx: %s %s\n", insn[i].address, insn[i].mnemonic,
                insn[i].op_str);
    }
    cs_free(insn, count);
#endif

    hostregalloc_free(&hralloc);
}

extern "C" {

void* generate_code_x86(IRBlock* ir, RegAllocation* regalloc, ArmCore* cpu) {
    return new Code(ir, regalloc, cpu);
}

JITFunc get_code_x86(void* backend) {
    return (JITFunc) ((Code*) backend)->getCode();
}

void free_code_x86(void* backend) {
    delete ((Code*) backend);
}
}
