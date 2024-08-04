#ifndef IR_H
#define IR_H

#include <stdlib.h>

#include "../arm_core.h"
#include "../../types.h"

enum {
    IR_LOAD_REG,
    IR_STORE_REG,
    IR_LOAD_CPSR,
    IR_STORE_CPSR,
    IR_LOAD_MEM8,
    IR_LOAD_MEMS8,
    IR_LOAD_MEM16,
    IR_LOAD_MEMS16,
    IR_LOAD_MEM32,
    IR_STORE_MEM8,
    IR_STORE_MEM16,
    IR_STORE_MEM32,
    IR_NOP,
    IR_MOV,
    IR_AND,
    IR_OR,
    IR_XOR,
    IR_NOT,
    IR_LSL,
    IR_LSR,
    IR_ASR,
    IR_ROR,
    IR_ADD,
    IR_SUB,
    IR_ADC,
    IR_SBC,
    IR_SETN,
    IR_SETZ,
    IR_SETC,
    IR_SETV,
    IR_JZ,
    IR_JNZ,
    IR_END,
};

typedef struct {
    struct {
        u32 opcode : 30;
        u32 imm1 : 1;
        u32 imm2 : 1;
    };
    u32 op1;
    u32 op2;
} IRInstr;

typedef struct {
    Vector(IRInstr) code;
    u32 start_addr;
    u32 end_addr;
} IRBlock;

static inline void irblock_init(IRBlock* block) {
    Vec_init(block->code);
    Vec_push(block->code, (IRInstr){.opcode = IR_NOP});
}
static inline void irblock_free(IRBlock* block) {
    Vec_free(block->code);
}
static inline u32 irblock_write(IRBlock* block, IRInstr instr) {
    Vec_push(block->code, instr);
    return block->code.size - 1;
}

void ir_interpret(IRBlock* block, ArmCore* cpu);

void ir_disassemble(IRBlock* block);

#endif
