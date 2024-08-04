#include "ir.h"

#define OP(n) (inst.imm##n ? inst.op##n : v[inst.op##n])

void ir_interpret(IRBlock* block, ArmCore* cpu) {
    u32 i = 0;
    u32* v = malloc(sizeof(u32) * block->code.size);
    while (true) {
        IRInstr inst = block->code.d[i];
        switch (inst.opcode) {
            case IR_LOAD_REG:
                v[i] = cpu->r[OP(1)];
                break;
            case IR_STORE_REG:
                cpu->r[OP(1)] = OP(2);
                break;
            case IR_LOAD_CPSR:
                v[i] = cpu->cpsr.w;
                break;
            case IR_STORE_CPSR:
                cpu->cpsr.w = OP(2);
                break;
            case IR_LOAD_MEM8:
                v[i] = cpu->read8(cpu, OP(1), false);
                break;
            case IR_LOAD_MEMS8:
                v[i] = cpu->read8(cpu, OP(1), true);
                break;
            case IR_LOAD_MEM16:
                v[i] = cpu->read16(cpu, OP(1), false);
                break;
            case IR_LOAD_MEMS16:
                v[i] = cpu->read16(cpu, OP(1), true);
                break;
            case IR_LOAD_MEM32:
                v[i] = cpu->read32(cpu, OP(1));
                break;
            case IR_STORE_MEM8:
                cpu->write8(cpu, OP(1), OP(2));
                break;
            case IR_STORE_MEM16:
                cpu->write16(cpu, OP(1), OP(2));
                break;
            case IR_STORE_MEM32:
                cpu->write32(cpu, OP(1), OP(2));
                break;
            case IR_NOP:
                break;
            case IR_MOV:
                v[i] = OP(2);
                break;
            case IR_AND:
                v[i] = OP(1) & OP(2);
                break;
            case IR_OR:
                v[i] = OP(1) | OP(2);
                break;
            case IR_XOR:
                v[i] = OP(1) ^ OP(2);
                break;
            case IR_NOT:
                v[i] = ~OP(2);
                break;
            case IR_LSL:
                v[i] = OP(1) << OP(2);
                break;
            case IR_LSR:
                v[i] = OP(1) >> OP(2);
                break;
            case IR_ASR:
                v[i] = (s32) OP(1) >> OP(2);
                break;
            case IR_ROR:
                v[i] = OP(1) >> OP(2) | OP(1) << (32 - OP(2));
                break;
            case IR_ADD:
                v[i] = OP(1) + OP(2);
                break;
            case IR_SUB:
                v[i] = OP(1) - OP(2);
                break;
            case IR_ADC:
                break;
            case IR_SBC:
                break;
            case IR_SETN:
                v[i] = OP(2) >> 31;
                break;
            case IR_SETZ:
                v[i] = OP(2) == 0;
                break;
            case IR_SETC:
                break;
            case IR_SETV:
                break;
            case IR_JZ:
                if (OP(1) == 0) i += OP(2) - 1;
                break;
            case IR_JNZ:
                if (OP(1) != 0) i += OP(2) - 1;
                break;
            case IR_END:
                free(v);
                return;
                break;
        }
        i++;
    }
}

#define DISASM_OP(n)                                                           \
    ((inst.imm##n) ? eprintf("0x%x", inst.op##n) : eprintf("v%d", inst.op##n))

#define DISASM(name, r, op1, op2)                                              \
    if (r) eprintf("v%d = ", i);                                               \
    eprintf("%s", #name);                                                      \
    if (op1) {                                                                 \
        eprintf(" ");                                                          \
        DISASM_OP(1);                                                          \
    }                                                                          \
    if (op2) {                                                                 \
        eprintf(" ");                                                          \
        DISASM_OP(2);                                                          \
    }                                                                          \
    return

#define DISASM_REG(name, r, op2)                                               \
    if (r) eprintf("v%d = ", i);                                               \
    eprintf(#name);                                                            \
    eprintf(" r%d ", inst.op1);                                                \
    if (op2) DISASM_OP(2);                                                     \
    return

#define DISASM_MEM(name, r, op2)                                               \
    if (r) eprintf("v%d = ", i);                                               \
    eprintf(#name " ");                                                        \
    eprintf("[");                                                              \
    DISASM_OP(1);                                                              \
    eprintf("] ");                                                             \
    if (op2) DISASM_OP(2);                                                     \
    return

#define DISASM_JMP(name, i)                                                    \
    eprintf(#name " ");                                                        \
    DISASM_OP(1);                                                              \
    eprintf(" %d", inst.op2 + i);                                              \
    return

void disasm_instr(IRInstr inst, int i) {
    switch (inst.opcode) {
        case IR_LOAD_REG:
            DISASM_REG(load_reg, 1, 0);
        case IR_STORE_REG:
            DISASM_REG(store_reg, 0, 1);
        case IR_LOAD_CPSR:
            DISASM(load_cpsr, 1, 0, 0);
        case IR_STORE_CPSR:
            DISASM(store_cpsr, 0, 0, 1);
        case IR_LOAD_MEM8:
            DISASM_MEM(load_mem8, 1, 0);
        case IR_LOAD_MEMS8:
            DISASM_MEM(load_mems8, 1, 0);
        case IR_LOAD_MEM16:
            DISASM_MEM(load_mem16, 1, 0);
        case IR_LOAD_MEMS16:
            DISASM_MEM(load_mems16, 1, 0);
        case IR_LOAD_MEM32:
            DISASM_MEM(load_mem32, 1, 0);
        case IR_STORE_MEM8:
            DISASM_MEM(store_mem8, 0, 1);
        case IR_STORE_MEM16:
            DISASM_MEM(store_mem16, 0, 1);
        case IR_STORE_MEM32:
            DISASM_MEM(store_mem32, 0, 1);
        case IR_NOP:
            return;
        case IR_MOV:
            DISASM(, 1, 0, 1);
        case IR_AND:
            DISASM(and, 1, 1, 1);
        case IR_OR:
            DISASM(or, 1, 1, 1);
        case IR_XOR:
            DISASM(xor, 1, 1, 1);
        case IR_NOT:
            DISASM(not, 1, 0, 1);
        case IR_LSL:
            DISASM(lsl, 1, 1, 1);
        case IR_LSR:
            DISASM(lsr, 1, 1, 1);
        case IR_ASR:
            DISASM(asr, 1, 1, 1);
        case IR_ROR:
            DISASM(ror, 1, 1, 1);
        case IR_ADD:
            DISASM(add, 1, 1, 1);
        case IR_SUB:
            DISASM(sub, 1, 1, 1);
        case IR_ADC:
            DISASM(adc, 1, 1, 1);
        case IR_SBC:
            DISASM(sbc, 1, 1, 1);
        case IR_SETN:
            DISASM(setn, 1, 0, 1);
        case IR_SETZ:
            DISASM(setz, 1, 0, 1);
        case IR_SETC:
            DISASM(setc, 1, 0, 0);
        case IR_SETV:
            DISASM(setv, 1, 0, 0);
        case IR_JZ:
            DISASM_JMP(jz, i);
        case IR_JNZ:
            DISASM_JMP(jnz, i);
        case IR_END:
            DISASM(end, 0, 0, 0);
    }
}

void ir_disassemble(IRBlock* block) {
    for (int i = 1; i < block->code.size; i++) {
        disasm_instr(block->code.d[i], i);
        eprintf("\n");
    }
}
