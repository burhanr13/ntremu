#ifndef ARM5_ISA_H
#define ARM5_ISA_H

#include <stdio.h>

#include "arm_common.h"
#include "types.h"

typedef struct _Arm946E Arm946E;

typedef void (*Arm5ExecFunc)(Arm946E*, ArmInstr);

void arm5_generate_lookup();
Arm5ExecFunc arm5_decode_instr(ArmInstr instr);

void arm5_exec_instr(Arm946E* cpu);

void exec_arm5_data_proc(Arm946E* cpu, ArmInstr instr);
void exec_arm5_psr_trans(Arm946E* cpu, ArmInstr instr);
void exec_arm5_multiply(Arm946E* cpu, ArmInstr instr);
void exec_arm5_multiply_long(Arm946E* cpu, ArmInstr instr);
void exec_arm5_multiply_short(Arm946E* cpu, ArmInstr instr);
void exec_arm5_swap(Arm946E* cpu, ArmInstr instr);
void exec_arm5_branch_ex(Arm946E* cpu, ArmInstr instr);
void exec_arm5_clz(Arm946E* cpu, ArmInstr instr);
void exec_arm5_sat_arith(Arm946E* cpu, ArmInstr instr);
void exec_arm5_half_trans(Arm946E* cpu, ArmInstr instr);
void exec_arm5_single_trans(Arm946E* cpu, ArmInstr instr);
void exec_arm5_undefined(Arm946E* cpu, ArmInstr instr);
void exec_arm5_block_trans(Arm946E* cpu, ArmInstr instr);
void exec_arm5_branch(Arm946E* cpu, ArmInstr instr);
void exec_arm5_cp_reg_trans(Arm946E* cpu, ArmInstr instr);
void exec_arm5_sw_intr(Arm946E* cpu, ArmInstr instr);

#endif