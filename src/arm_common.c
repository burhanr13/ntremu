#include "arm_common.h"

char* mode_name(CpuMode m) {
    switch (m) {
        case M_USER:
            return "USER";
        case M_FIQ:
            return "FIQ";
        case M_IRQ:
            return "IRQ";
        case M_SVC:
            return "SVC";
        case M_ABT:
            return "ABT";
        case M_UND:
            return "UND";
        case M_SYSTEM:
            return "SYSTEM";
        default:
            return "ILLEGAL";
    }
}

RegBank get_bank(CpuMode mode) {
    switch (mode) {
        case M_USER:
            return B_USER;
        case M_FIQ:
            return B_FIQ;
        case M_IRQ:
            return B_IRQ;
        case M_SVC:
            return B_SVC;
        case M_ABT:
            return B_ABT;
        case M_UND:
            return B_UND;
        case M_SYSTEM:
            return B_USER;
    }
    return B_USER;
}

void arm_disassemble(ArmInstr instr, u32 addr, FILE* out) {

    static char* reg_names[16] = {"r0", "r1", "r2", "r3", "r4",  "r5",
                                  "r6", "r7", "r8", "r9", "r10", "r11",
                                  "ip", "sp", "lr", "pc"};
    static char* cond_names[16] = {"eq", "ne", "hs", "lo", "mi", "pl",
                                   "vs", "vc", "hi", "ls", "ge", "lt",
                                   "gt", "le", "",   ""};
    static char* alu_names[16] = {"and", "eor", "sub", "rsb", "add", "adc",
                                  "sbc", "rsc", "tst", "teq", "cmp", "cmn",
                                  "orr", "mov", "bic", "mvn"};
    static char* shift_names[5] = {"lsl", "lsr", "asr", "ror", "rrx"};

    char* cond = cond_names[instr.cond];

    if (instr.sw_intr.c1 == 0b1111) {

        fprintf(out, "swi%s 0x%x", cond, instr.sw_intr.arg);

    } else if (instr.branch.c1 == 0b101) {

        u32 off = instr.branch.offset;
        if (off & (1 << 23)) off |= 0xff000000;
        if (instr.cond == 0xf) {
            fprintf(out, "blx 0x%x",
                    addr + 8 * (off << 2) + (instr.branch.l << 1));
        } else {
            fprintf(out, "b%s%s 0x%x", instr.branch.l ? "l" : "", cond,
                    addr + 8 + (off << 2));
        }

    } else if (instr.cp_reg_trans.c1 == 0b1110 && instr.cp_reg_trans.c2 == 1) {

        fprintf(out, "%s%s p%d, %d, %s, c%d, c%d, %d",
                instr.cp_reg_trans.l ? "mrc" : "mcr", cond_names[instr.cond],
                instr.cp_reg_trans.cpnum, instr.cp_reg_trans.cpopc,
                reg_names[instr.cp_reg_trans.rd], instr.cp_reg_trans.crn,
                instr.cp_reg_trans.crm, instr.cp_reg_trans.cp);

    } else if (instr.block_trans.c1 == 0b100) {

        if (instr.block_trans.rn == 13 &&
            (instr.block_trans.u != instr.block_trans.p) &&
            instr.block_trans.u == instr.block_trans.l && instr.block_trans.w) {
            fprintf(out, "%s%s {", instr.block_trans.l ? "pop" : "push", cond);
        } else {
            fprintf(out, "%s%s%s%s %s%s, {",
                    instr.block_trans.l ? "ldm" : "stm",
                    instr.block_trans.u ? "i" : "d",
                    instr.block_trans.p ? "b" : "a", cond,
                    reg_names[instr.block_trans.rn],
                    instr.block_trans.w ? "!" : "");
        }
        u16 rlist = instr.block_trans.rlist;
        for (int i = 0; i < 16; i++) {
            if (rlist & 1) {
                fprintf(out, "%s", reg_names[i]);
                rlist >>= 1;
                if (rlist) {
                    fprintf(out, ", ");
                }
            } else rlist >>= 1;
        }
        fprintf(out, "}%s", instr.block_trans.s ? "^" : "");

    } else if (instr.undefined.c1 == 0b011 && instr.undefined.c2 == 1) {

        fprintf(out, "undefined");

    } else if (instr.single_trans.c1 == 0b01) {

        if (instr.cond == 0xf) {
            fprintf(out, "pld");
            return;
        }

        if (!instr.single_trans.i && instr.single_trans.rn == 15 &&
            instr.single_trans.p) {
            fprintf(out, "%s%s%s %s, [", instr.single_trans.l ? "ldr" : "str",
                    instr.single_trans.b ? "b" : "", cond,
                    reg_names[instr.single_trans.rd]);

            u32 offset = instr.single_trans.offset;
            if (!instr.single_trans.u) offset = -offset;
            fprintf(out, "0x%x", addr + 8 + offset);
            fprintf(out, "]%s", instr.single_trans.w ? "!" : "");
        } else if (instr.single_trans.rn == 13 &&
                   instr.single_trans.offset == 4 &&
                   (!instr.single_trans.p || instr.single_trans.w) &&
                   !instr.single_trans.b && !instr.single_trans.i &&
                   instr.single_trans.l == instr.single_trans.u &&
                   instr.single_trans.u != instr.single_trans.p) {
            fprintf(out, "%s%s %s", instr.single_trans.l ? "pop" : "push", cond,
                    reg_names[instr.single_trans.rd]);
        } else {
            fprintf(out, "%s%s%s %s, [%s", instr.single_trans.l ? "ldr" : "str",
                    instr.single_trans.b ? "b" : "", cond,
                    reg_names[instr.single_trans.rd],
                    reg_names[instr.single_trans.rn]);
            if (!instr.single_trans.p) {
                fprintf(out, "]");
            }
            if (instr.single_trans.i) {
                u32 rm = instr.single_trans.offset & 0b1111;
                u32 shift = instr.single_trans.offset >> 4;
                fprintf(out, ", %s%s", instr.single_trans.u ? "" : "-",
                        reg_names[rm]);
                if (shift) {
                    u32 shift_type = (shift >> 1) & 0b11;
                    u32 shift_amt = (shift >> 3) & 0b11111;
                    if (!shift_amt) {
                        if (shift_type == S_ROR) {
                            shift_type = 4;
                            shift_amt = 1;
                        } else {
                            shift_amt = 32;
                        }
                    }
                    fprintf(out, ", %s #%d", shift_names[shift_type],
                            shift_amt);
                }
            } else if (instr.single_trans.offset) {
                fprintf(out, ", #%s0x%x", instr.single_trans.u ? "" : "-",
                        instr.single_trans.offset);
            }
            if (instr.single_trans.p) {
                fprintf(out, "]%s", instr.single_trans.w ? "!" : "");
            }
        }

    } else if (instr.clz.c1 == 0b00010110 && instr.clz.c4 == 0b0001) {

        fprintf(out, "clz %s, %s", reg_names[instr.clz.rd],
                reg_names[instr.clz.rm]);

    } else if (instr.branch_ex.c1 == 0b00010010 && instr.branch_ex.c3 == 0b00 &&
               instr.branch_ex.c4 == 1) {

        fprintf(out, "%s%s %s", instr.branch_ex.l ? "blx" : "bx", cond,
                reg_names[instr.branch_ex.rn]);

    } else if (instr.swap.c1 == 0b00010 && instr.swap.c2 == 0b00 &&
               instr.swap.c4 == 0b1001) {

        fprintf(out, "swap%s%s %s, %s, [%s]", instr.swap.b ? "b" : "", cond,
                reg_names[instr.swap.rd], reg_names[instr.swap.rm],
                reg_names[instr.swap.rn]);

    } else if (instr.multiply.c1 == 0b000000 && instr.multiply.c2 == 0b1001) {

        if (instr.multiply.a) {
            fprintf(out, "mla%s%s %s, %s, %s, %s", instr.multiply.s ? "s" : "",
                    cond, reg_names[instr.multiply.rd],
                    reg_names[instr.multiply.rm], reg_names[instr.multiply.rs],
                    reg_names[instr.multiply.rn]);
        } else {
            fprintf(out, "mul%s%s %s, %s, %s", instr.multiply.s ? "s" : "",
                    cond, reg_names[instr.multiply.rd],
                    reg_names[instr.multiply.rm], reg_names[instr.multiply.rs]);
        }

    } else if (instr.multiply_long.c1 == 0b00001 &&
               instr.multiply_long.c2 == 0b1001) {

        fprintf(out, "%s%s%s%s %s, %s, %s, %s",
                instr.multiply_long.u ? "s" : "u",
                instr.multiply_long.a ? "mlal" : "mull",
                instr.multiply_long.s ? "s" : "", cond,
                reg_names[instr.multiply_long.rdlo],
                reg_names[instr.multiply_long.rdhi],
                reg_names[instr.multiply_long.rm],
                reg_names[instr.multiply_long.rs]);

    } else if (instr.half_trans.c1 == 0b000 && instr.half_trans.c2 == 1 &&
               instr.half_trans.c3 == 1) {

        if (instr.half_trans.i && instr.half_trans.rn == 15 &&
            instr.half_trans.p) {
            fprintf(out, "%s%s%s%s %s, [", instr.half_trans.l ? "ldr" : "str",
                    instr.half_trans.s ? "s" : "",
                    instr.half_trans.h ? "h" : "b", cond,
                    reg_names[instr.half_trans.rd]);

            u32 offset = instr.half_trans.offlo | (instr.half_trans.offhi << 4);
            if (!instr.half_trans.u) offset = -offset;
            fprintf(out, "0x%x", addr + 8 + offset);
            fprintf(out, "]%s", instr.half_trans.w ? "!" : "");
        } else {
            if (!instr.half_trans.l && instr.half_trans.s) {
                fprintf(out, "%s", instr.half_trans.h ? "strd" : "ldrd");
            } else {
                fprintf(out, "%s%s%s", instr.half_trans.l ? "ldr" : "str",
                        instr.half_trans.s ? "s" : "",
                        instr.half_trans.h ? "h" : "b");
            }
            fprintf(out, "%s %s, [%s", cond, reg_names[instr.half_trans.rd],
                    reg_names[instr.half_trans.rn]);
            if (!instr.half_trans.p) {
                fprintf(out, "]");
            }
            if (instr.half_trans.i) {
                u32 offset =
                    instr.half_trans.offlo | (instr.half_trans.offhi << 4);
                if (offset) {
                    fprintf(out, ", #%s0x%x", instr.half_trans.u ? "" : "-",
                            offset);
                }
            } else {
                fprintf(out, ", %s%s", instr.half_trans.u ? "" : "-",
                        reg_names[instr.half_trans.offlo]);
            }
            if (instr.half_trans.p) {
                fprintf(out, "]%s", instr.half_trans.w ? "!" : "");
            }
        }

    } else if (instr.psr_trans.c1 == 0b00 && instr.psr_trans.c2 == 0b10 &&
               instr.psr_trans.c3 == 0) {

        if (instr.psr_trans.op) {
            fprintf(out, "msr%s %s_%s%s, ", cond,
                    instr.psr_trans.p ? "spsr" : "cpsr",
                    instr.psr_trans.c ? "c" : "", instr.psr_trans.f ? "f" : "");
            if (instr.psr_trans.i) {
                u32 op2 = instr.psr_trans.op2 & 0xff;
                u32 rot = (instr.psr_trans.op2 >> 8) << 1;
                op2 = (op2 >> rot) | (op2 << (32 - rot));
                fprintf(out, "#0x%x", op2);
            } else {
                fprintf(out, "%s", reg_names[instr.psr_trans.op2 & 0xf]);
            }
        } else {
            fprintf(out, "mrs%s %s, %s", cond, reg_names[instr.psr_trans.rd],
                    instr.psr_trans.p ? "spsr" : "cpsr");
        }

    } else if (instr.data_proc.c1 == 0b00) {

        if (instr.data_proc.i && instr.data_proc.rn == 15 &&
            (instr.data_proc.opcode == A_ADD ||
             instr.data_proc.opcode == A_SUB)) {
            fprintf(out, "adr%s%s %s, [", instr.data_proc.s ? "s" : "", cond,
                    reg_names[instr.data_proc.rd]);
            u32 offset = instr.data_proc.op2 & 0xff;
            u32 rot = (instr.data_proc.op2 >> 8) << 1;
            offset = (offset >> rot) | (offset << (32 - rot));
            if (instr.data_proc.opcode == A_SUB) offset = -offset;
            fprintf(out, "0x%x]", addr + 8 + offset);
        } else {

            if (instr.data_proc.opcode >> 2 == 0b10) {
                fprintf(out, "%s%s %s", alu_names[instr.data_proc.opcode], cond,
                        reg_names[instr.data_proc.rn]);
            } else {
                fprintf(out, "%s%s%s %s", alu_names[instr.data_proc.opcode],
                        instr.data_proc.s ? "s" : "", cond,
                        reg_names[instr.data_proc.rd]);
                if (!(instr.data_proc.opcode == A_MOV ||
                      instr.data_proc.opcode == A_MVN)) {
                    fprintf(out, ", %s", reg_names[instr.data_proc.rn]);
                }
            }

            if (instr.data_proc.i) {
                u32 op2 = instr.data_proc.op2 & 0xff;
                u32 rot = (instr.data_proc.op2 >> 8) << 1;
                fprintf(out, ", #0x%x", (op2 >> rot) | (op2 << (32 - rot)));
            } else {
                u32 rm = instr.data_proc.op2 & 0b1111;
                u32 shift = instr.data_proc.op2 >> 4;
                fprintf(out, ", %s", reg_names[rm]);
                u32 shift_type = (shift >> 1) & 0b11;
                if (shift & 1) {
                    u32 shift_reg = shift >> 4;
                    fprintf(out, ", %s %s", shift_names[shift_type],
                            reg_names[shift_reg]);
                } else if (shift) {
                    u32 shift_amt = (shift >> 3) & 0b11111;
                    if (!shift_amt) {
                        if (shift_type == S_ROR) {
                            shift_type = 4;
                            shift_amt = 1;
                        } else {
                            shift_amt = 32;
                        }
                    }
                    fprintf(out, ", %s #%d", shift_names[shift_type],
                            shift_amt);
                }
            }
        }
    } else {
        fprintf(out, "undefined");
    }
}