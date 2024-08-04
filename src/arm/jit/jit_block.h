#ifndef JIT_BLOCK_H
#define JIT_BLOCK_H

#include "../arm_core.h"
#include "ir.h"

typedef struct _JITBlock {
    ArmCore* cpu;
    struct {
        u32 mode : 5;
        u32 t : 1;
        u32 v5 : 1;
        u32 modeswitch : 1;
        u32 thumbswitch : 1;
    };
    u32 start_addr;
    u32 end_addr;
    IRBlock ir;
} JITBlock;

JITBlock* create_jit_block(ArmCore* cpu, u32 addr);
void destroy_jit_block(JITBlock* block);

void jit_exec(JITBlock* block);

void arm_exec_jit(ArmCore* cpu);

#endif
