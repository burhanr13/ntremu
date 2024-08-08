#ifndef BACKEND_X86_H
#define BACKEND_X86_H

#include "../ir.h"
#include "../jit.h"
#include "../register_allocator.h"
#include "../../arm_core.h"

void* generate_code_x86(IRBlock* ir, RegisterAllocation* regalloc, ArmCore* cpu);
JITEntry get_code_x86(void* backend);
void free_code_x86(void* backend);

#endif
