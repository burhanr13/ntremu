#include "optimizer.h"

#define MOVX(_op2, imm)                                                        \
    ((IRInstr){.opcode = IR_MOV, .imm1 = 1, .imm2 = imm, .op1 = 0, .op2 = _op2})
#define MOVI(op2) MOVX(op2, 1)
#define NOP                                                                    \
    ((IRInstr){.opcode = IR_NOP, .imm1 = 1, .imm2 = 1, .op1 = 0, .op2 = 0})

void optimize_loadstore_reg(IRBlock* block) {
    u32 vreg[16] = {0};
    bool immreg[16] = {0};
    u32 laststorereg[16] = {0};
    u32 vflag[5] = {0};
    bool immflag[5] = {0};
    u32 laststoreflag[5] = {0};

    u32 jmpsource = 0;
    u32 jmptarget = -1;
    for (int i = 0; i < block->code.size; i++) {
        IRInstr inst = block->code.d[i];
        if (i == jmptarget) {
            for (int r = 0; r < 16; r++) {
                if (laststorereg[r] > jmpsource || vreg[r] > jmpsource) {
                    laststorereg[r] = 0;
                    vreg[r] = 0;
                    immreg[r] = false;
                }
            }
            for (int f = 0; f < 5; f++) {
                if (laststoreflag[f] > jmpsource || vflag[f] > jmpsource) {
                    laststoreflag[f] = 0;
                    vflag[f] = 0;
                    immflag[f] = false;
                }
            }
            jmpsource = 0;
        }
        switch (inst.opcode) {
            case IR_LOAD_REG: {
                u32 rd = inst.op1;
                if (vreg[rd] || immreg[rd]) {
                    block->code.d[i] = MOVX(vreg[rd], immreg[rd]);
                } else {
                    vreg[rd] = i;
                    immreg[rd] = false;
                }
                break;
            }
            case IR_STORE_REG: {
                u32 rd = inst.op1;
                if (laststorereg[rd] > jmpsource) {
                    block->code.d[laststorereg[rd]] = NOP;
                }
                laststorereg[rd] = i;
                vreg[rd] = inst.op2;
                immreg[rd] = inst.imm2;
                break;
            }
            case IR_LOAD_FLAG: {
                u32 f = inst.op1;
                if (vflag[f] || immflag[f]) {
                    block->code.d[i] = MOVX(vflag[f], immflag[f]);
                } else {
                    vflag[f] = i;
                    immflag[f] = false;
                }
                break;
            }
            case IR_STORE_FLAG: {
                u32 f = inst.op1;
                if (laststoreflag[f] > jmpsource) {
                    block->code.d[laststoreflag[f]] = NOP;
                }
                laststoreflag[f] = i;
                vflag[f] = inst.op2;
                immflag[f] = inst.imm2;
                break;
            }
            case IR_LOAD_CPSR:
                for (int f = 0; f < 5; f++) {
                    laststoreflag[f] = 0;
                }
                break;
            case IR_STORE_CPSR:
                for (int f = 0; f < 5; f++) {
                    if (laststoreflag[f] > jmpsource) {
                        block->code.d[laststoreflag[f]] = NOP;
                    }
                    laststoreflag[f] = 0;
                    vflag[f] = 0;
                    immflag[f] = false;
                }
                break;
            case IR_JZ:
            case IR_JNZ: {
                jmpsource = i;
                jmptarget = i + inst.op2;
                break;
            }
            case IR_END: {
                for (int r = 0; r < 16; r++) {
                    laststorereg[r] = 0;
                    vreg[r] = 0;
                    immreg[r] = false;
                }
                for (int f = 0; f < 5; f++) {
                    laststoreflag[f] = 0;
                    vflag[f] = 0;
                    immflag[f] = false;
                }
                break;
            }
            default:
                break;
        }
    }
}

#define NOOPT() (vops[i] = i, vimm[i] = false)
#define OPTV(op) (vops[i] = op, vimm[i] = false, *inst = NOP)
#define OPTI(op) (vops[i] = op, vimm[i] = true, *inst = NOP)

#define ADDCV(op1, op2, c)                                                     \
    {                                                                          \
        u32 res = op1 + op2;                                                   \
        u32 tmpc = res < op1;                                                  \
        res += c;                                                              \
        if (inst[1].opcode == IR_GETC)                                         \
            inst[1] = MOVI(tmpc || res < op1 + op2);                           \
        if (inst[3].opcode == IR_GETV)                                         \
            inst[3] = MOVI(((op1 ^ res) & ~(op1 ^ op2)) >> 31);                \
        vops[i] = res;                                                         \
        vimm[i] = true;                                                        \
        *inst = NOP;                                                           \
    }

void optimize_constprop(IRBlock* block) {
    u32* vops = malloc(sizeof(u32) * block->code.size);
    bool* vimm = malloc(sizeof(bool) * block->code.size);
    for (int i = 0; i < block->code.size; i++) {
        IRInstr* inst = &block->code.d[i];
        if (!inst->imm1) {
            u32 v = inst->op1;
            inst->op1 = vops[v];
            inst->imm1 = vimm[v];
        }
        if (!inst->imm2) {
            u32 v = inst->op2;
            inst->op2 = vops[v];
            inst->imm2 = vimm[v];
        }

        if (inst->imm1 && inst->imm2) {
            switch (inst->opcode) {
                case IR_MOV:
                    OPTI(inst->op2);
                    break;
                case IR_AND:
                    OPTI(inst->op1 & inst->op2);
                    break;
                case IR_OR:
                    OPTI(inst->op1 | inst->op2);
                    break;
                case IR_XOR:
                    OPTI(inst->op1 ^ inst->op2);
                    break;
                case IR_NOT:
                    OPTI(~inst->op2);
                    break;
                case IR_LSL:
                    if (inst->op2 >= 32) {
                        OPTI(0);
                    } else {
                        OPTI(inst->op1 << inst->op2);
                    }
                    break;
                case IR_LSR:
                    if (inst->op2 >= 32) {
                        OPTI(0);
                    } else {
                        OPTI(inst->op1 >> inst->op2);
                    }
                    break;
                case IR_ASR:
                    if (inst->op2 >= 32) {
                        OPTI((s32) inst->op1 >> 31);
                    } else {
                        OPTI((s32) inst->op1 >> inst->op2);
                    }
                    break;
                case IR_ROR:
                    inst->op2 &= 31;
                    OPTI((inst->op1 >> inst->op2) |
                         (inst->op1 << (32 - inst->op2)));
                    break;
                case IR_ADD:
                    if (inst[1].opcode == IR_GETC ||
                        inst[3].opcode == IR_GETV) {
                        ADDCV(inst->op1, inst->op2, 0);
                    } else {
                        OPTI(inst->op1 + inst->op2);
                    }
                    break;
                case IR_SUB:
                    if (inst[1].opcode == IR_GETC) {
                        ADDCV(inst->op1, ~inst->op2, 1);
                    } else {
                        OPTI(inst->op1 - inst->op2);
                    }
                    break;
                case IR_GETN:
                    OPTI(inst->op2 >> 31);
                    break;
                case IR_GETZ:
                    OPTI(inst->op2 == 0);
                    break;
                case IR_PCMASK:
                    OPTI(inst->op1 ? ~1 : ~3);
                    break;
                case IR_JZ:
                    if (inst->op1) {
                        *inst = NOP;
                    } else {
                        u32 off = inst->op2;
                        for (int j = 0; j < off; j++) {
                            inst[j] = NOP;
                        }
                        i += off - 1;
                    }
                    break;
                case IR_JNZ:
                    if (!inst->op1) {
                        *inst = NOP;
                    } else {
                        u32 off = inst->op2;
                        for (int j = 0; j < off; j++) {
                            inst[j] = NOP;
                        }
                        i += off - 1;
                    }
                    break;
                default:
                    NOOPT();
                    break;
            }
        } else if (inst->imm2) {
            switch (inst->opcode) {
                case IR_AND:
                    if (inst->op2 == 0) {
                        OPTI(0);
                    } else if (inst->op2 == 0xffffffff) {
                        OPTV(inst->op1);
                    } else {
                        NOOPT();
                    }
                    break;
                case IR_OR:
                    if (inst->op2 == 0xffffffff) {
                        OPTI(0xffffffff);
                    } else if (inst->op2 == 0) {
                        OPTV(inst->op1);
                    } else {
                        NOOPT();
                    }
                    break;
                case IR_XOR:
                    if (inst->op2 == 0) {
                        OPTV(inst->op1);
                    } else {
                        NOOPT();
                    }
                    break;
                case IR_LSL:
                    if (inst->op2 >= 32) {
                        OPTI(0);
                    } else if (inst->op2 == 0) {
                        OPTV(inst->op1);
                    } else {
                        NOOPT();
                    }
                    break;
                case IR_LSR:
                    if (inst->op2 >= 32) {
                        OPTI(0);
                    } else if (inst->op2 == 0) {
                        OPTV(inst->op1);
                    } else {
                        NOOPT();
                    }
                    break;
                case IR_ASR:
                    if (inst->op2 == 0) {
                        OPTV(inst->op1);
                    } else {
                        if (inst->op2 >= 32) {
                            inst->op2 = 31;
                        }
                        NOOPT();
                    }
                    break;
                case IR_ROR:
                    if (inst->op2 == 0) {
                        OPTV(inst->op1);
                    } else {
                        inst->op2 &= 31;
                        NOOPT();
                    }
                    break;
                case IR_ADD:
                    if (inst->op2 == 0) {
                        OPTV(inst->op1);
                        if (inst[1].opcode == IR_GETC) inst[1] = MOVI(0);
                        if (inst[3].opcode == IR_GETV) inst[3] = MOVI(0);
                    } else {
                        NOOPT();
                    }
                    break;
                case IR_SUB:
                    if (inst->op2 == 0) {
                        OPTV(inst->op1);
                        if (inst[1].opcode == IR_GETC) inst[1] = MOVI(1);
                        if (inst[3].opcode == IR_GETV) inst[3] = MOVI(0);
                    } else {
                        NOOPT();
                    }
                    break;
                default:
                    NOOPT();
                    break;
            }
        } else if (inst->imm1) {
            switch (inst->opcode) {
                case IR_MOV:
                    vops[i] = inst->op2;
                    vimm[i] = false;
                    *inst = NOP;
                    break;
                case IR_AND:
                    if (inst->op1 == 0) {
                        OPTI(0);
                    } else if (inst->op1 == 0xffffffff) {
                        OPTV(inst->op2);
                    } else {
                        NOOPT();
                    }
                    break;
                case IR_OR:
                    if (inst->op1 == 0xffffffff) {
                        OPTI(0xffffffff);
                    } else if (inst->op1 == 0) {
                        OPTV(inst->op2);
                    } else {
                        NOOPT();
                    }
                    break;
                case IR_XOR:
                    if (inst->op1 == 0) {
                        OPTV(inst->op2);
                    } else {
                        NOOPT();
                    }
                    break;
                case IR_ADD:
                    if (inst->op1 == 0) {
                        OPTV(inst->op2);
                        if (inst[1].opcode == IR_GETC) inst[1] = MOVI(0);
                        if (inst[3].opcode == IR_GETV) inst[3] = MOVI(0);
                    } else {
                        NOOPT();
                    }
                    break;
                default:
                    NOOPT();
                    break;
            }
        } else {
            NOOPT();
        }
    }
    free(vops);
    free(vimm);
}

u32 chainjmp_helper(IRInstr* jmp) {
    IRInstr* next = jmp + jmp->op2;
    while (next->opcode == IR_NOP) next++;
    if (next->opcode == jmp->opcode && next->op1 == jmp->op1) {
        chainjmp_helper(next);
        jmp->op2 = next - jmp + next->op2;
        *next = NOP;
    }
    return jmp->op2;
}

void optimize_chainjumps(IRBlock* block) {
    for (int i = 0; i < block->code.size; i++) {
        IRInstr inst = block->code.d[i];
        switch (inst.opcode) {
            case IR_JZ:
            case IR_JNZ:
                i += chainjmp_helper(&block->code.d[i]) - 1;
                break;
            case IR_END:
                for (int j = i + 1; j < block->code.size; j++) {
                    block->code.d[j] = NOP;
                }
                return;
            default:
                break;
        }
    }
}

void optimize_deadcode(IRBlock* block) {
    bool* vused = calloc(block->code.size, sizeof(bool));
    for (int i = block->code.size - 1; i >= 0; i--) {
        IRInstr* inst = &block->code.d[i];
        if (!vused[i]) {
            if (inst->opcode > IR_NOP) {
                *inst = NOP;
            } else {
                switch (inst->opcode) {
                    case IR_LOAD_REG:
                    case IR_LOAD_FLAG:
                    case IR_LOAD_REG_USR:
                    case IR_LOAD_CPSR:
                    case IR_LOAD_SPSR:
                        *inst = NOP;
                    default:
                        break;
                }
            }
        }
        if (!inst->imm1) vused[inst->op1] = true;
        if (!inst->imm2) vused[inst->op2] = true;
    }
    free(vused);
}
