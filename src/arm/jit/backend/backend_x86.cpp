#include "backend_x86.h"

#include <capstone/capstone.h>
#include <xbyak/xbyak.h>

struct LinkPatch {
    u32 jmp_offset;
    u32 attrs;
    u32 addr;
};

struct Code : Xbyak::CodeGenerator {
    RegAllocation* regalloc;
    HostRegAllocation hralloc;
    ArmCore* cpu;

    std::vector<Xbyak::Reg32> tempregs = {esi, edi, r8d, r9d, r10d, r11d};
    std::vector<Xbyak::Reg32> savedregs = {ebp, r12d, r13d, r14d, r15d};
    std::vector<Xbyak::Address> stackslots;

    std::vector<LinkPatch> links;

    Code(IRBlock* ir, RegAllocation* regalloc, ArmCore* cpu);

    ~Code() {
        hostregalloc_free(&hralloc);
    }

    void print_hostregs() {
        printf("Host Regs:");
        for (u32 i = 0; i < hralloc.nregs; i++) {
            printf(" $%d:", i);
            auto& operand = _getOp(i);
            if (operand.isREG()) printf("%s", operand.toString());
            else printf("[rsp+%d]", 4 * hralloc.hostreg_info[i].index);
        }
        printf("\n");
    }

    int getSPDisp() {
        int disp =
            (hralloc.count[REG_SAVED] + 2) * 8 + hralloc.count[REG_STACK] * 4;
        disp = (disp + 15) & ~15;
        disp -= (hralloc.count[REG_SAVED] + 2) * 8;
        return disp;
    }

    const Xbyak::Operand& _getOp(int i) {
        HostRegInfo hr = hralloc.hostreg_info[i];
        switch (hr.type) {
            case REG_TEMP:
                return tempregs[hr.index];
            case REG_SAVED:
                return savedregs[hr.index];
            case REG_STACK:
                return stackslots[hr.index];
            default:
                break;
        }
        return rdx;
    }

    const Xbyak::Operand& getOp(int i) {
        return _getOp(regalloc->reg_assn[i]);
    }
};

#define CPU(m) (rbx + ((char*) &cpu->m - (char*) cpu))

#define OP(op, dest, src)                                                      \
    ({                                                                         \
        if ((src).isMEM() && (dest).isMEM()) {                                 \
            mov(edx, src);                                                     \
            op(dest, edx);                                                     \
        } else op(dest, src);                                                  \
    })

#define LOAD(addr)                                                             \
    ({                                                                         \
        auto& dest = getOp(i);                                                 \
        OP(mov, dest, ptr[addr]);                                              \
    })

#define STORE(addr)                                                            \
    ({                                                                         \
        if (inst.imm2) {                                                       \
            mov(dword[addr], inst.op2);                                        \
        } else {                                                               \
            auto& src = getOp(inst.op2);                                       \
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
        auto& dest = getOp(i);                                                 \
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
    : Xbyak::CodeGenerator(4096, Xbyak::AutoGrow), regalloc(regalloc),
      cpu(cpu) {

    hralloc =
        allocate_host_registers(regalloc, tempregs.size(), savedregs.size());
    for (u32 i = 0; i < hralloc.count[REG_STACK]; i++) {
        stackslots.push_back(dword[rsp + 4 * i]);
    }

    u32 flags_mask = 0;
    u32 lastflags = 0;
    u32 nlabels = 0;
    u32 jmptarget = -1;

    for (u32 i = 0; i < ir->code.size; i++) {
        while (i < ir->code.size && ir->code.d[i].opcode == IR_NOP) i++;
        if (i == ir->code.size) break;
        IRInstr inst = ir->code.d[i];
        if (i >= jmptarget && inst.opcode != IR_JELSE) {
            L(std::to_string(nlabels++));
            jmptarget = -1;
        }
        if (!(inst.opcode == IR_NOP || inst.opcode == IR_STORE_FLAG ||
              inst.opcode == IR_GETC || inst.opcode == IR_GETV ||
              inst.opcode == IR_GETN || inst.opcode == IR_GETZ))
            lastflags = 0;
        switch (inst.opcode) {
            case IR_LOAD_REG:
                LOAD(CPU(r[inst.op1]));
                break;
            case IR_STORE_REG:
                STORE(CPU(r[inst.op1]));
                break;
            case IR_LOAD_REG_USR: {
                int rd = inst.op1;
                if (rd < 13) {
                    LOAD(CPU(banked_r8_12[0][rd - 8]));
                } else if (rd == 13) {
                    LOAD(CPU(banked_sp[0]));
                } else if (rd == 14) {
                    LOAD(CPU(banked_lr[0]));
                }
                break;
            }
            case IR_STORE_REG_USR: {
                int rd = inst.op1;
                if (rd < 13) {
                    STORE(CPU(banked_r8_12[0][rd - 8]));
                } else if (rd == 13) {
                    STORE(CPU(banked_sp[0]));
                } else if (rd == 14) {
                    STORE(CPU(banked_lr[0]));
                }
                break;
            }
            case IR_LOAD_FLAG: {
                LOAD(CPU(cpsr));
                auto& dest = getOp(i);
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
                LOAD(CPU(cpsr));
                auto& dest = getOp(i);
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
            case IR_READ_CP: {
                ArmInstr cpinst = {inst.op1};
                mov(rdi, rbx);
                mov(esi, cpinst.cp_reg_trans.crn);
                mov(edx, cpinst.cp_reg_trans.crm);
                mov(ecx, cpinst.cp_reg_trans.cp);
                mov(rax, (u64) cpu->cp15_read);
                call(rax);
                mov(getOp(i), eax);
                break;
            }
            case IR_WRITE_CP: {
                if (inst.imm2) {
                    mov(r8d, inst.op2);
                } else {
                    mov(r8d, getOp(inst.op2));
                }
                ArmInstr cpinst = {inst.op1};
                mov(rdi, rbx);
                mov(esi, cpinst.cp_reg_trans.crn);
                mov(edx, cpinst.cp_reg_trans.crm);
                mov(ecx, cpinst.cp_reg_trans.cp);
                mov(rax, (u64) cpu->cp15_write);
                call(rax);
                break;
            }
            case IR_LOAD_MEM8: {
                xor_(edx, edx);
                if (inst.imm1) {
                    mov(esi, inst.op1);
                } else {
                    auto& src = getOp(inst.op1);
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
                    auto& src = getOp(inst.op1);
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
                    auto& src = getOp(inst.op1);
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
                    auto& src = getOp(inst.op1);
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
                    auto& src = getOp(inst.op1);
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
                    mov(edx, inst.op2);
                } else {
                    mov(edx, getOp(inst.op2));
                }
                if (inst.imm1) {
                    mov(esi, inst.op1);
                } else {
                    auto& src = getOp(inst.op1);
                    if (src != esi) mov(esi, src);
                }
                mov(rdi, rbx);
                mov(rax, (u64) cpu->write8);
                call(rax);
                break;
            }
            case IR_STORE_MEM16: {
                if (inst.imm2) {
                    mov(edx, inst.op2);
                } else {
                    mov(edx, getOp(inst.op2));
                }
                if (inst.imm1) {
                    mov(esi, inst.op1);
                } else {
                    auto& src = getOp(inst.op1);
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
                    auto& src = getOp(inst.op1);
                    if (src != esi) mov(esi, src);
                }
                mov(rdi, rbx);
                mov(rax, (u64) cpu->write32);
                call(rax);
                break;
            }
            case IR_MOV: {
                auto& dst = getOp(i);
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
                auto& dst = getOp(i);
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
                auto& dest = getOp(i);
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
                        mov(ecx, eax);
                    } else {
                        mov(ecx, getOp(inst.op2));
                    }
                    cmp(cl, 32);
                    inLocalLabel();
                    jb(".normal");
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
                auto& dest = getOp(i);
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
                        mov(ecx, eax);
                    } else {
                        mov(ecx, getOp(inst.op2));
                    }
                    cmp(cl, 32);
                    inLocalLabel();
                    jb(".normal");
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
                auto& dest = getOp(i);
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
                        mov(ecx, eax);
                    } else {
                        mov(ecx, getOp(inst.op2));
                    }
                    cmp(cl, 32);
                    inLocalLabel();
                    jb(".normal");
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
                auto& dest = getOp(i);
                if (inst.imm1) {
                    mov(dest, inst.op1);
                } else if (!SAMEREG(inst.op1, i)) {
                    OP(mov, dest, getOp(inst.op1));
                }
                if (inst.imm2) {
                    ror(dest, inst.op2);
                } else {
                    if (op2eax) {
                        mov(ecx, eax);
                    } else {
                        mov(ecx, getOp(inst.op2));
                    }
                    ror(dest, cl);
                }
                break;
            }
            case IR_RRC: {
                auto& dst = getOp(i);
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
            case IR_MUL: {
                IRInstr hinst = ir->code.d[i + 1];
                if (hinst.opcode == IR_SMULH || hinst.opcode == IR_UMULH) {
                    if (inst.imm1) {
                        mov(eax, inst.op1);
                    } else {
                        mov(eax, getOp(inst.op1));
                    }
                    auto& msrc = inst.imm2 ? edx : getOp(inst.op2);
                    if (inst.imm2) mov(edx, inst.op2);
                    if (hinst.opcode == IR_SMULH) {
                        imul(msrc);
                    } else {
                        mul(msrc);
                    }
                    mov(getOp(i), eax);
                    mov(getOp(i + 1), edx);
                    i++;
                } else {
                    auto& dest = getOp(i);
                    if (inst.imm2) {
                        if (inst.imm1) {
                            mov(dest, inst.op1 * inst.op2);
                        } else {
                            auto& mdst = dest.isMEM() ? edx : dest;
                            imul(mdst.getReg(), getOp(inst.op1), inst.op2);
                            if (dest.isMEM()) mov(dest, mdst);
                        }
                    } else {
                        bool op2eax = false;
                        if (!dest.isMEM() && SAMEREG(i, inst.op2)) {
                            mov(eax, getOp(inst.op2));
                            op2eax = true;
                        }
                        auto& mdst = dest.isMEM() ? edx : dest;
                        if (inst.imm1) {
                            mov(mdst, inst.op1);
                        } else {
                            mov(mdst, getOp(inst.op1));
                        }
                        if (op2eax) {
                            imul(mdst.getReg(), eax);
                        } else {
                            imul(mdst.getReg(), getOp(inst.op2));
                        }
                        if (dest.isMEM()) mov(dest, mdst);
                    }
                }
                break;
            }
            case IR_SMULH:
            case IR_UMULH: {
                if (inst.imm1) {
                    mov(eax, inst.op1);
                } else {
                    mov(eax, getOp(inst.op1));
                }
                auto& msrc = inst.imm2 ? edx : getOp(inst.op2);
                if (inst.imm2) mov(edx, inst.op2);
                if (inst.opcode == IR_SMULH) {
                    imul(msrc);
                } else {
                    mul(msrc);
                }
                mov(getOp(i), edx);
                break;
            }
            case IR_SMULW: {
                if (inst.imm1) {
                    mov(eax, inst.op1);
                    movsxd(rax, eax);
                } else {
                    movsxd(rax, getOp(inst.op1));
                }
                if (inst.imm2) {
                    mov(edx, inst.op2);
                    movsxd(rdx, edx);
                } else {
                    movsxd(rdx, getOp(inst.op2));
                }
                imul(rax, rdx);
                sar(rax, 16);
                mov(getOp(i), eax);
                break;
            }
            case IR_CLZ: {
                auto& dest = getOp(i);
                if (inst.imm2) {
                    u32 op = inst.op2;
                    u32 ct = 0;
                    if (op == 0) ct = 32;
                    else
                        while (!(op & BIT(31))) {
                            op <<= 1;
                            ct++;
                        }
                    mov(dest, ct);
                } else {
                    if (dest.isMEM()) {
                        lzcnt(edx, getOp(inst.op2));
                        mov(dest, edx);
                    } else {
                        lzcnt(dest.getReg(), getOp(inst.op2));
                    }
                }
                break;
            }
            case IR_GETN: {
                auto& dest = getOp(i);
                if (inst.imm2) {
                    mov(dest, inst.op2 >> 31);
                } else {
                    if (lastflags != inst.op2) {
                        auto& src = getOp(inst.op2);
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
                        setnz(dest.getReg().cvt8());
                    }
                }
                break;
            }
            case IR_GETZ: {
                auto& dest = getOp(i);
                if (inst.imm2) {
                    mov(dest, inst.op2 == 0);
                } else {
                    if (lastflags != inst.op2) {
                        auto& src = getOp(inst.op2);
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
                        setnz(dest.getReg().cvt8());
                    }
                }
                break;
            }
            case IR_GETC: {
                if (lastflags != inst.op2) {
                    lahf();
                    seto(al);
                    lastflags = inst.op2;
                }
                auto& dest = getOp(i);
                if (dest.isMEM()) {
                    xor_(edx, edx);
                    test(ah, BIT(0));
                    setnz(dl);
                    mov(dest, edx);
                } else {
                    xor_(dest, dest);
                    test(ah, BIT(0));
                    setnz(dest.getReg().cvt8());
                }
                break;
            }
            case IR_SETC: {
                if (inst.imm2) {
                    if (inst.op2) stc();
                    else clc();
                } else {
                    mov(edx, getOp(inst.op2));
                    shr(edx, 1);
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
                        auto& dest = getOp(i);
                        setc(dl);
                        movzx(edx, dl);
                        mov(dest, edx);
                    }
                } else {
                    setc(dl);
                    movzx(edx, dl);
                    auto& op1 = getOp(inst.op1);
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
                if (lastflags != inst.op2) {
                    lahf();
                    seto(al);
                    lastflags = inst.op2;
                }
                auto& dest = getOp(i);
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
                    auto& op1 = getOp(inst.op1);
                    if (op1.isMEM()) {
                        cmp(op1, 0);
                    } else {
                        test(op1.getReg(), op1.getReg());
                    }
                    auto& dest = getOp(i);
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
                    auto& src = getOp(inst.op1);
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
                    auto& src = getOp(inst.op1);
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
                for (u32 i = 0; i < hralloc.count[REG_SAVED]; i++) {
                    push(savedregs[i].changeBit(64));
                }
                int spdisp = getSPDisp();
                if (spdisp) sub(rsp, spdisp);
                mov(rbx, (u64) cpu);
                L("loopblock");
                break;
            }
            case IR_END_RET:
            case IR_END_LINK:
            case IR_END_LOOP: {

                add(dword[CPU(cycles)], ir->numinstr);

                if (inst.opcode == IR_END_LOOP) {
                    mov(eax, dword[CPU(max_cycles)]);
                    cmp(dword[CPU(cycles)], eax);
                    jl("loopblock");
                }

                int spdisp = getSPDisp();
                if (spdisp) add(rsp, spdisp);
                for (int i = hralloc.count[REG_SAVED] - 1; i >= 0; i--) {
                    pop(savedregs[i].changeBit(64));
                }

                if (inst.opcode == IR_END_LINK) {
                    inLocalLabel();
                    mov(eax, dword[CPU(max_cycles)]);
                    cmp(dword[CPU(cycles)], eax);
                    jge(".nolink");
                    mov(rdi, rbx);
                    mov(esi, inst.op2);
                    mov(edx, inst.op2 + MAX_BLOCK_SIZE);
                    mov(rax, (u64) jit_isdirty);
                    call(rax);
                    test(al, al);
                    jnz(".nolink");
                    pop(rbx);
                    links.push_back((LinkPatch){(u32) (getCurr() - getCode()),
                                                inst.op1, inst.op2});
                    nop(5);
                    L(".nolink");
                    outLocalLabel();
                }

                mov(dword[CPU(max_cycles)], 0);
                mov(eax, ptr[CPU(pc)]);
                mov(ptr[CPU(cur_instr_addr)], eax);
                mov(byte[CPU(pending_flush)], 1);

                pop(rbx);
                ret();
                break;
            }
            default:
                break;
        }
    }

    ready();
}

extern "C" {

void* backend_x86_generate_code(IRBlock* ir, RegAllocation* regalloc,
                                ArmCore* cpu) {
    return new Code(ir, regalloc, cpu);
}

JITFunc backend_x86_get_code(void* backend) {
    return (JITFunc) ((Code*) backend)->getCode();
}

void backend_x86_patch_links(JITBlock* block) {
    Code* code = (Code*) block->backend;
    for (auto [offset, attrs, addr] : code->links) {
        char* jmpsrc = (char*) code->getCode() + offset;
        JITBlock* linkblock = get_jitblock(code->cpu, attrs, addr);
        int rel32 = (char*) linkblock->code - (jmpsrc + 5);
        jmpsrc[0] = 0xe9;
        jmpsrc[1] = rel32 & 0xff;
        jmpsrc[2] = (rel32 >> 8) & 0xff;
        jmpsrc[3] = (rel32 >> 16) & 0xff;
        jmpsrc[4] = (rel32 >> 24) & 0xff;
        Vec_push(linkblock->linkingblocks,
                 ((BlockLocation){block->attrs, block->start_addr}));
    }
}

void backend_x86_free(void* backend) {
    delete ((Code*) backend);
}

void backend_x86_disassemble(void* backend) {
    Code* code = (Code*) backend;
    code->print_hostregs();
    csh handle;
    cs_insn* insn;
    cs_open(CS_ARCH_X86, CS_MODE_64, &handle);
    size_t count =
        cs_disasm(handle, code->getCode(), code->getSize(), 0, 0, &insn);
    printf("--------- JIT Disassembly at 0x%p ------------\n", code->getCode());
    for (size_t i = 0; i < count; i++) {
        printf("%04lx: %s %s\n", insn[i].address, insn[i].mnemonic,
               insn[i].op_str);
    }
    cs_free(insn, count);
}
}
