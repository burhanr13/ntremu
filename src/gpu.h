#ifndef GPU_H
#define GPU_H

#include "types.h"

typedef struct _NDS NDS;

typedef struct {
    NDS* master;

    u8 cmd_fifo[256];
    u32 param_fifo[256];
    u8 cmd_fifosize;
    u8 param_fifosize;
} GPU;

void gxfifo_write(GPU* gpu, u32 command);

#endif