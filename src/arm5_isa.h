#ifndef ARM5_ISA_H
#define ARM5_ISA_H

#include <stdio.h>

#include "types.h"

enum { C_EQ, C_NE, C_CS, C_CC, C_MI, C_PL, C_VS, C_VC, C_HI, C_LS, C_GE, C_LT, C_GT, C_LE, C_AL };

enum {
    A_AND,
    A_EOR,
    A_SUB,
    A_RSB,
    A_ADD,
    A_ADC,
    A_SBC,
    A_RSC,
    A_TST,
    A_TEQ,
    A_CMP,
    A_CMN,
    A_ORR,
    A_MOV,
    A_BIC,
    A_MVN
};

enum { S_LSL, S_LSR, S_ASR, S_ROR };

typedef struct _Arm946E Arm946E;

typedef union {
    u32 w;
    struct {
        u32 instr : 28;
        u32 cond : 4;
    };
    struct {
        u32 op2 : 12;
        u32 rd : 4;
        u32 rn : 4;
        u32 s : 1;
        u32 opcode : 4;
        u32 i : 1;
        u32 c1 : 2; // 00
        u32 cond : 4;
    } data_proc;
    struct {
        u32 op2 : 12;
        u32 rd : 4;
        u32 c : 1;
        u32 x : 1;
        u32 s : 1;
        u32 f : 1;
        u32 c3 : 1; // 0
        u32 op : 1;
        u32 p : 1;
        u32 c2 : 2; // 10
        u32 i : 1;
        u32 c1 : 2; // 00
        u32 cond : 4;
    } psr_trans;
    struct {
        u32 rm : 4;
        u32 c2 : 4; // 1001
        u32 rs : 4;
        u32 rn : 4;
        u32 rd : 4;
        u32 s : 1;
        u32 a : 1;
        u32 c1 : 6; // 000000
        u32 cond : 4;
    } multiply;
    struct {
        u32 rm : 4;
        u32 c2 : 4; // 1001
        u32 rs : 4;
        u32 rdlo : 4;
        u32 rdhi : 4;
        u32 s : 1;
        u32 a : 1;
        u32 u : 1;
        u32 c1 : 5; // 00001
        u32 cond : 4;
    } multiply_long;
    struct {
        u32 rm : 4;
        u32 c4 : 4; // 1001
        u32 c3 : 4;
        u32 rd : 4;
        u32 rn : 4;
        u32 c2 : 2; // 00
        u32 b : 1;
        u32 c1 : 5; // 00010
        u32 cond : 4;
    } swap;
    struct {
        u32 rn : 4;
        u32 c3 : 4; // 0001
        u32 c2 : 12;
        u32 c1 : 8; // 00010010
        u32 cond : 4;
    } branch_ex;
    struct {
        u32 offlo : 4;
        u32 c3 : 1; // 1
        u32 h : 1;
        u32 s : 1;
        u32 c2 : 1; // 1
        u32 offhi : 4;
        u32 rd : 4;
        u32 rn : 4;
        u32 l : 1;
        u32 w : 1;
        u32 i : 1;
        u32 u : 1;
        u32 p : 1;
        u32 c1 : 3; // 000
        u32 cond : 4;
    } half_trans;
    struct {
        u32 offset : 12;
        u32 rd : 4;
        u32 rn : 4;
        u32 l : 1;
        u32 w : 1;
        u32 b : 1;
        u32 u : 1;
        u32 p : 1;
        u32 i : 1;
        u32 c1 : 2; // 01
        u32 cond : 4;
    } single_trans;
    struct {
        u32 u2 : 4;
        u32 c2 : 1; // 1
        u32 u1 : 20;
        u32 c1 : 3; // 011
        u32 cond : 4;
    } undefined;
    struct {
        u32 rlist : 16;
        u32 rn : 4;
        u32 l : 1;
        u32 w : 1;
        u32 s : 1;
        u32 u : 1;
        u32 p : 1;
        u32 c1 : 3; // 100
        u32 cond : 4;
    } block_trans;
    struct {
        u32 offset : 24;
        u32 l : 1;
        u32 c1 : 3; // 101
        u32 cond : 4;
    } branch;
    struct {
        u32 arg : 24;
        u32 c1 : 4; // 1111
        u32 cond : 4;
    } sw_intr;
} Arm5Instr;

typedef void (*ArmExecFunc)(Arm946E*, Arm5Instr);

extern ArmExecFunc arm_lookup[1 << 12];

void arm_generate_lookup();
ArmExecFunc arm5_decode_instr(Arm5Instr instr);

void arm_exec_instr(Arm946E* cpu);

void exec_arm5_data_proc(Arm946E* cpu, Arm5Instr instr);
void exec_arm5_psr_trans(Arm946E* cpu, Arm5Instr instr);
void exec_arm5_multiply(Arm946E* cpu, Arm5Instr instr);
void exec_arm5_multiply_long(Arm946E* cpu, Arm5Instr instr);
void exec_arm5_swap(Arm946E* cpu, Arm5Instr instr);
void exec_arm5_branch_ex(Arm946E* cpu, Arm5Instr instr);
void exec_arm5_half_trans(Arm946E* cpu, Arm5Instr instr);
void exec_arm5_single_trans(Arm946E* cpu, Arm5Instr instr);
void exec_arm5_undefined(Arm946E* cpu, Arm5Instr instr);
void exec_arm5_block_trans(Arm946E* cpu, Arm5Instr instr);
void exec_arm5_branch(Arm946E* cpu, Arm5Instr instr);
void exec_arm5_sw_intr(Arm946E* cpu, Arm5Instr instr);

void arm_disassemble(Arm5Instr instr, u32 addr, FILE* out);

#endif