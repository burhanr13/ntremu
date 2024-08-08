#ifndef REGISTER_ALLOCATOR_H
#define REGISTER_ALLOCATOR_H

#include "../../types.h"
#include "ir.h"

typedef enum {
    REG_SCRATCH,
    REG_TEMP,
    REG_SAVED,
} RegType;

typedef struct {
    u32 uses;
    RegType type;
} RegInfo;

typedef struct {
    Vector(RegInfo) reg_info;
    u32* reg_assn;
    u32 nassns;
} RegisterAllocation;

RegisterAllocation allocate_registers(IRBlock* block);
void regalloc_free(RegisterAllocation* regalloc);

void regalloc_print(RegisterAllocation* regalloc);

#endif
