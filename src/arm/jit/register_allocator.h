#ifndef REGISTER_ALLOCATOR_H
#define REGISTER_ALLOCATOR_H

#include "../../types.h"
#include "ir.h"

typedef struct {
    u32 uses;
    bool calleesaved;
} RegInfo;

typedef struct {
    Vector(RegInfo) reg_info;
    u32* reg_assn;
} RegisterAllocation;

RegisterAllocation allocate_registers(IRBlock* block);
void regalloc_free(RegisterAllocation* regalloc);

void regalloc_print(IRBlock* block, RegisterAllocation* regalloc);

#endif
