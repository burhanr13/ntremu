#ifndef RECOMPILER_H
#define RECOMPILER_H

#include "../arm.h"
#include "../arm_core.h"
#include "../../types.h"
#include "ir.h"

IRBlock* compile_block(ArmCore* cpu, u32 start_addr);

bool arm_compile_instr(IRBlock* block, ArmCore* cpu, u32 addr, ArmInstr instr);

typedef bool (*ArmCompileFunc)(IRBlock*, ArmCore*, u32, ArmInstr);

bool compile_arm_data_proc(IRBlock* block, ArmCore* cpu, u32 addr,
                           ArmInstr instr);
bool compile_arm_branch(IRBlock* block, ArmCore* cpu, u32 addr, ArmInstr instr);

#endif
