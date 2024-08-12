#include "jit.h"

#include "backend/backend.h"
#include "optimizer.h"
#include "recompiler.h"
#include "register_allocator.h"

// #define IR_DISASM
// #define JIT_LOG
// #define JIT_CPULOG
//#define IR_INTERPRET
//#define DISABLE_OPT

JITBlock* create_jit_block(ArmCore* cpu, u32 addr) {
    JITBlock* block = malloc(sizeof *block);
    block->start_addr = addr;

    IRBlock ir;
    irblock_init(&ir);

    compile_block(cpu, &ir, addr);

    block->numinstr = ir.numinstr;

#ifndef DISABLE_OPT
    optimize_loadstore(&ir);
    optimize_constprop(&ir);
    optimize_literals(&ir, cpu);
    optimize_chainjumps(&ir);
    optimize_loadstore(&ir);
    optimize_constprop(&ir);
    optimize_deadcode(&ir);
    if (ir.loop) optimize_waitloop(&ir);
    optimize_blocklinking(&ir, cpu);
#endif

    block->end_addr = ir.end_addr;

    RegAllocation regalloc = allocate_registers(&ir);

#ifdef IR_DISASM
    ir_disassemble(&ir);
    regalloc_print(&regalloc);
#endif

    block->backend = generate_code(&ir, &regalloc, cpu);
    block->code = get_code(block->backend);

    regalloc_free(&regalloc);
    //irblock_free(&ir);

    block->cpu = cpu;
    block->ir = malloc(sizeof(IRBlock));
    *block->ir = ir;

    return block;
}

void destroy_jit_block(JITBlock* block) {
    free_code(block->backend);

    free(block->ir);
    free(block);
}

void jit_exec(JITBlock* block) {
#ifdef JIT_LOG
    eprintf("executing block at 0x%08x\n", block->start_addr);
#endif
#ifdef JIT_CPULOG
    cpu_print_state(block->cpu);
#endif
#ifdef IR_INTERPRET
    ir_interpret(block->ir, block->cpu);
#else
    block->code();
#endif
}

JITBlock* get_jitblock(ArmCore* cpu, u32 attrs, u32 addr) {
    u32 addrhi = addr >> 16;
    u32 addrlo = (addr & 0xffff) >> 1;

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
    if (startlo == 0 && endlo == BIT(15)) {
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

void jit_free_all(ArmCore* cpu) {
    for (int i = 0; i < 64; i++) {
        if (cpu->jit_cache[i]) {
            for (int j = 0; j < 1 << 16; j++) {
                if (cpu->jit_cache[i][j]) {
                    for (int k = 0; k < 1 << 15; k++) {
                        if (cpu->jit_cache[i][j][k]) {
                            destroy_jit_block(cpu->jit_cache[i][j][k]);
                        }
                    }
                    free(cpu->jit_cache[i][j]);
                }
            }
            free(cpu->jit_cache[i]);
        }
    }
}

void arm_exec_jit(ArmCore* cpu) {
    JITBlock* block =
        get_jitblock(cpu, cpu->cpsr.w & 0x3f, cpu->cur_instr_addr);
    if (block) {
        jit_exec(block);
    } else {
        arm_exec_instr(cpu);
    }
}
