#ifndef _ARM4_ISA_H_
#define _ARM4_ISA_H_

#include <stdio.h>

#include "arm_common.h"
#include "types.h"

typedef struct _Arm7TDMI Arm7TDMI;

typedef void (*Arm4ExecFunc)(Arm7TDMI*, ArmInstr);

void arm4_generate_lookup();
Arm4ExecFunc arm4_decode_instr(ArmInstr instr);

void arm4_exec_instr(Arm7TDMI* cpu);

void exec_arm4_data_proc(Arm7TDMI* cpu, ArmInstr instr);
void exec_arm4_psr_trans(Arm7TDMI* cpu, ArmInstr instr);
void exec_arm4_multiply(Arm7TDMI* cpu, ArmInstr instr);
void exec_arm4_multiply_long(Arm7TDMI* cpu, ArmInstr instr);
void exec_arm4_swap(Arm7TDMI* cpu, ArmInstr instr);
void exec_arm4_branch_ex(Arm7TDMI* cpu, ArmInstr instr);
void exec_arm4_half_trans(Arm7TDMI* cpu, ArmInstr instr);
void exec_arm4_single_trans(Arm7TDMI* cpu, ArmInstr instr);
void exec_arm4_undefined(Arm7TDMI* cpu, ArmInstr instr);
void exec_arm4_block_trans(Arm7TDMI* cpu, ArmInstr instr);
void exec_arm4_branch(Arm7TDMI* cpu, ArmInstr instr);
void exec_arm4_sw_intr(Arm7TDMI* cpu, ArmInstr instr);

#endif