#ifndef JIT_BLOCK_H
#define JIT_BLOCK_H

#include "../arm_core.h"
#include "ir.h"

#define MAX_BLOCK_INSTRS 512

typedef struct _JITBlock {
    ArmCore* cpu;
    u32 start_addr;
    u32 end_addr;
    u32 numinstr;
    IRBlock ir;
} JITBlock;

JITBlock* create_jit_block(ArmCore* cpu, u32 addr);
void destroy_jit_block(JITBlock* block);

void jit_exec(JITBlock* block);
JITBlock* get_jitblock(ArmCore* cpu, u32 attrs, u32 addr);

void jit_invalidate_range(ArmCore* cpu, u32 start_addr, u32 end_addr);
#define jit_invalidate_all(cpu) jit_invalidate_range(cpu, 0, 0xffffffff)

void arm_exec_jit(ArmCore* cpu);

#endif
