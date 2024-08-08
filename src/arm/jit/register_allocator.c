#include "register_allocator.h"

void find_uses(IRBlock* block, u32* vuses) {
    for (int i = 0; i < block->code.size; i++) {
        vuses[i] = 0;
        IRInstr inst = block->code.d[i];
        if (!inst.imm1) vuses[inst.op1]++;
        if (!inst.imm2) vuses[inst.op2]++;
    }
}

RegisterAllocation allocate_registers(IRBlock* block) {
    u32 vuses[block->code.size];
    find_uses(block, vuses);

    RegisterAllocation ret;
    Vec_init(ret.reg_info);
    ret.reg_assn = malloc(block->code.size * sizeof(u32));
    ret.nassns = block->code.size;
    Vector(bool) reg_active = {};

    for (int i = 0; i < block->code.size; i++) {
        IRInstr inst = block->code.d[i];

        if (!inst.imm1) {
            if (!--vuses[inst.op1]) {
                reg_active.d[ret.reg_assn[inst.op1]] = false;
                if (inst.op1 < i - 1) {
                    if (ret.reg_info.d[ret.reg_assn[inst.op1]].type ==
                        REG_SCRATCH)
                        ret.reg_info.d[ret.reg_assn[inst.op1]].type = REG_TEMP;
                }
            }
        }
        if (!inst.imm2) {
            if (!--vuses[inst.op2]) {
                reg_active.d[ret.reg_assn[inst.op2]] = false;
                if (inst.op2 < i - 1) {
                    if (ret.reg_info.d[ret.reg_assn[inst.op2]].type ==
                        REG_SCRATCH)
                        ret.reg_info.d[ret.reg_assn[inst.op2]].type = REG_TEMP;
                }
            }
        }

        switch (inst.opcode) {
            case IR_READ_CP:
            case IR_WRITE_CP:
            case IR_LOAD_MEM8:
            case IR_LOAD_MEMS8:
            case IR_LOAD_MEM16:
            case IR_LOAD_MEMS16:
            case IR_LOAD_MEM32:
            case IR_STORE_MEM8:
            case IR_STORE_MEM16:
            case IR_STORE_MEM32:
            case IR_MODESWITCH:
            case IR_EXCEPTION:
                for (int r = 0; r < ret.reg_info.size; r++) {
                    if (reg_active.d[r]) ret.reg_info.d[r].type = REG_SAVED;
                }
            default:
                break;
        }

        if (vuses[i]) {
            u32 assignment = -1;
            for (int r = 0; r < reg_active.size; r++) {
                if (!reg_active.d[r]) {
                    assignment = r;
                    break;
                }
            }
            if (assignment == -1) {
                assignment = reg_active.size;
                Vec_push(ret.reg_info, ((RegInfo){0, REG_SCRATCH}));
                Vec_push(reg_active, true);
            }
            ret.reg_info.d[assignment].uses += 1 + vuses[i];
            reg_active.d[assignment] = true;
            ret.reg_assn[i] = assignment;
        } else {
            ret.reg_assn[i] = -1;
        }
    }

    Vec_free(reg_active);

    return ret;
}

void regalloc_free(RegisterAllocation* regalloc) {
    Vec_free(regalloc->reg_info);
    free(regalloc->reg_assn);
}

void regalloc_print(RegisterAllocation* regalloc) {
    static const char typenames[3][2] = {"k", "t", "s"};

    eprintf("Registers:");
    for (int i = 0; i < regalloc->reg_info.size; i++) {
        eprintf(" %s%d(%d)", typenames[regalloc->reg_info.d[i].type], i,
                regalloc->reg_info.d[i].uses);
    }
    eprintf("\nAssignments:");
    for (int i = 0; i < regalloc->nassns; i++) {
        u32 assn = regalloc->reg_assn[i];
        if (assn == -1) continue;
        eprintf(" v%d:%s%d", i, typenames[regalloc->reg_info.d[assn].type],
                assn);
    }
    eprintf("\n");
}