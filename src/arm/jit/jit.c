#include "jit.h"

#include "optimizer.h"
#include "recompiler.h"

// #define IR_DISASM

JITBlock* create_jit_block(ArmCore* cpu, u32 addr) {
    JITBlock* block = malloc(sizeof *block);
    block->cpu = cpu;
    block->start_addr = addr;
    irblock_init(&block->ir, addr);

    compile_block(cpu, &block->ir, addr);

    block->end_addr = block->ir.end_addr;

    optimize_loadstore(&block->ir);
    optimize_constprop(&block->ir);
    optimize_chainjumps(&block->ir);
    optimize_loadstore(&block->ir);
    optimize_constprop(&block->ir);
    optimize_deadcode(&block->ir);

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
}

JITBlock* get_jitblock(ArmCore* cpu, u32 addr) {
    u32 addrhi = addr >> 16;
    u32 addrlo = (addr & 0xffff) >> 1;

    u32 attrs = cpu->cpsr.w & 0x3f;

    if (!cpu->jit_cache[attrs])
        cpu->jit_cache[attrs] = calloc(1 << 16, sizeof(JITBlock**));

    if (!cpu->jit_cache[attrs][addrhi])
        cpu->jit_cache[attrs][addrhi] = calloc(1 << 15, sizeof(JITBlock*));

    if (!cpu->jit_cache[attrs][addrhi][addrlo])
        cpu->jit_cache[attrs][addrhi][addrlo] = create_jit_block(cpu, addr);

    return cpu->jit_cache[attrs][addrhi][addrlo];
}

void invalidate_l2(ArmCore* cpu, u32 attr, u32 addrhi, u32 startlo, u32 endlo) {
    if (!cpu->jit_cache[attr][addrhi]) return;
    for (int i = startlo; i < endlo; i++) {
        if (cpu->jit_cache[attr][addrhi][i]) {
            destroy_jit_block(cpu->jit_cache[attr][addrhi][i]);
            cpu->jit_cache[attr][addrhi][i] = NULL;
        }
    }
    if (startlo == 0 && endlo == (1 << 15)) {
        free(cpu->jit_cache[attr][addrhi]);
        cpu->jit_cache[attr][addrhi] = NULL;
    }
}

void jit_invalidate_range(ArmCore* cpu, u32 start_addr, u32 end_addr) {
    u32 sthi = start_addr >> 16;
    u32 stlo = (start_addr & 0xffff) >> 1;
    u32 endhi = end_addr >> 16;
    u32 endlo = (end_addr & 0xffff) >> 1;

    for (int i = 0; i < 64; i++) {
        if (!cpu->jit_cache[i]) continue;
        if (sthi == endhi) {
            invalidate_l2(cpu, i, sthi, stlo, endlo);
        } else {
            invalidate_l2(cpu, i, sthi, stlo, 1 << 15);
            for (int j = sthi + 1; j < endhi; j++) {
                invalidate_l2(cpu, i, j, 0, 1 << 15);
            }
            invalidate_l2(cpu, i, endhi, 0, endlo);
        }
        if (start_addr == 0 && end_addr == -1) {
            free(cpu->jit_cache[i]);
            cpu->jit_cache[i] = NULL;
        }
    }
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
