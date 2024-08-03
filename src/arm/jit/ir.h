#ifndef IR_H
#define IR_H

#include <stdlib.h>

#include "../../types.h"

enum {
    IR_LOAD_R0,
    IR_LOAD_CPSR = IR_LOAD_R0 + 16,
    IR_STORE_R0,
    IR_STORE_CPSR = IR_STORE_R0 + 16,
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

#define IR_LOAD_R(n) (IR_LOAD_R0 + n)
#define IR_STORE_R(n) (IR_STORE_R0 + n)

typedef struct {
    Vector(IRInstr) code;
} IRBlock;

static inline void irblock_init(IRBlock* block) {
    Vec_init(block->code);
}
static inline void irblock_destroy(IRBlock* block) {
    Vec_free(block->code);
}
static inline u32 irblock_write(IRBlock* block, IRInstr instr) {
    Vec_push(block->code, instr);
    return block->code.size;
}

#endif
