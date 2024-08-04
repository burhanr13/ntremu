#include "jit_block.h"

#include "recompiler.h"

JITBlock* create_jit_block(ArmCore* cpu, u32 addr) {
    JITBlock* block = malloc(sizeof *block);
    block->cpu = cpu;
    block->t = cpu->cpsr.t;
    block->v5 = cpu->v5;
    block->start_addr = addr;
    irblock_init(&block->ir);

    compile_block(cpu, &block->ir, addr);

    block->end_addr = block->ir.end_addr;

    ir_disassemble(&block->ir);

    return block;
}

void destroy_jit_block(JITBlock* block) {
    irblock_free(&block->ir);
    free(block);
}

void jit_exec(JITBlock* block) {
    ir_interpret(&block->ir, block->cpu);
}
