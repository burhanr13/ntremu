#include "jit_block.h"

#include "recompiler.h"

JITBlock* create_jit_block(ArmCore* cpu, u32 addr) {
    JITBlock* block = malloc(sizeof *block);
    block->cpu = cpu;
    block->t = cpu->cpsr.t;
    block->v5 = cpu->v5;
    block->mode = cpu->cpsr.m;
    block->start_addr = addr;
    irblock_init(&block->ir, addr);

    compile_block(cpu, &block->ir, addr);

    block->end_addr = block->ir.end_addr;
    block->modeswitch = block->ir.modeswitch;
    block->thumbswitch = block->ir.thumbswitch;

#ifdef IR_DISASM
    ir_disassemble(&block->ir);
#endif

    return block;
}

void destroy_jit_block(JITBlock* block) {
    irblock_free(&block->ir);
    free(block);
}

void jit_exec(JITBlock* block) {

    ir_interpret(&block->ir, block->cpu);

    if (block->thumbswitch) block->cpu->cpsr.t = block->cpu->pc & 1;
    if (block->cpu->cpsr.t) block->cpu->pc &= ~1;
    else block->cpu->pc &= ~3;
    block->cpu->cur_instr_addr = block->cpu->pc;
    block->cpu->pending_flush = true;
    if (block->modeswitch) {
        cpu_update_mode(block->cpu, block->mode);
    }
}

JITBlock* get_jitblock(ArmCore* cpu, u32 addr) {
    u32 addrhi = addr >> 16;
    u32 addrlo = (addr & 0xffff) >> 1;

    if (!cpu->jit_cache) cpu->jit_cache = calloc(1 << 16, sizeof(JITBlock**));

    if (!cpu->jit_cache[addrhi])
        cpu->jit_cache[addrhi] = calloc(1 << 15, sizeof(JITBlock*));

    if (!cpu->jit_cache[addrhi][addrlo])
        cpu->jit_cache[addrhi][addrlo] = create_jit_block(cpu, addr);
    else if (cpu->jit_cache[addrhi][addrlo]->t != cpu->cpsr.t ||
             cpu->jit_cache[addrhi][addrlo]->v5 != cpu->v5 ||
             cpu->jit_cache[addrhi][addrlo]->mode != cpu->cpsr.m) {
        destroy_jit_block(cpu->jit_cache[addrhi][addrlo]);
        cpu->jit_cache[addrhi][addrlo] = create_jit_block((ArmCore*) cpu, addr);
    }

    return cpu->jit_cache[addrhi][addrlo];
}

void arm_exec_jit(ArmCore* cpu) {
    JITBlock* block = get_jitblock(cpu, cpu->cur_instr_addr);
    if (block) {
        jit_exec(block);
        cpu->cycles +=
            (block->end_addr - block->start_addr) >> (cpu->cpsr.t ? 1 : 2);
    } else {
        arm_exec_instr(cpu);
    }
}
