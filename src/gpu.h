#ifndef GPU_H
#define GPU_H

#include "ppu.h"
#include "types.h"

typedef struct {
    s16 p[3];
} vec3;

typedef struct {
    s32 p[4][4];
} mat4;

typedef struct _NDS NDS;

typedef struct {
    NDS* master;

    u16 screen[NDS_SCREEN_H][NDS_SCREEN_W];

    u8 cmd_fifo[256];
    u32 param_fifo[256];
    u8 cmd_fifosize;
    u8 param_fifosize;
    u8 params_pending;

    mat4 cur_projmtx;
    mat4 proj_mtxstk[1];
    u8 projstk_size;

    mat4 cur_posmtx;
    mat4 pos_mtxstk[32];
    u8 posstk_size;

    mat4 cur_vecmtx;
    mat4 vec_mtxstk[32];
    u8 vecstk_size;

    vec3 vertexram[6144];
    u16 n_verts;

} GPU;

void gxfifo_write(GPU* gpu, u32 command);
void gxcmd_execute(GPU* gpu);

#endif