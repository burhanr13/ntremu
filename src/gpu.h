#ifndef GPU_H
#define GPU_H

#include "ppu.h"
#include "types.h"

#define MAX_VTX 6144
#define MAX_POLY 2048

enum {
    MTX_MODE = 0x10,
    MTX_PUSH,
    MTX_POP,
    MTX_STORE,
    MTX_RESTORE,
    MTX_IDENTITY,
    MTX_LOAD_44,
    MTX_LOAD_43,
    MTX_MULT_44,
    MTX_MULT_43,
    MTX_MULT_33,
    MTX_SCALE,
    MTX_TRANS,
    COLOR = 0x20,
    NORMAL,
    TEXCOORD,
    VTX_16,
    VTX_10,
    VTX_XY,
    VTX_XZ,
    VTX_YZ,
    VTX_DIFF,
    POLYGON_ATTR,
    TEXIMAGE_PARAM,
    PLTT_BASE,
    BEGIN_VTXS = 0x40,
    END_VTXS,
    SWAP_BUFFERS = 0x50,
};

typedef struct {
    float p[4];
} vec4;

typedef struct {
    float p[4][4];
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

    int mtx_mode;
    mat4 clipmtx;

    vec4 vertexram[MAX_VTX];
    u16 n_verts;

    int poly_mode;

    vec4 cur_vtx;

} GPU;

void gxfifo_write(GPU* gpu, u32 command);
void gxcmd_execute(GPU* gpu);
void gpu_render(GPU* gpu);

#endif