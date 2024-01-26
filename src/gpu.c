#include "gpu.h"

#include <string.h>

#include "io.h"
#include "nds.h"

const int cmd_parms[8][16] = {{0},
                              {1, 0, 1, 1, 1, 0, 16, 12, 16, 12, 9, 3, 3},
                              {1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1},
                              {1, 1, 1, 1, 32},
                              {1, 0},
                              {1},
                              {1},
                              {3, 2, 1}};

void gxfifo_write(GPU* gpu, u32 command) {
    if (gpu->params_pending) {
        gpu->param_fifo[gpu->param_fifosize++] = command;
        gpu->params_pending--;
        gpu->master->io9.gxstat.gxfifo_size++;
    } else {
        while (command) {
            u8 cmd = command;
            u8 h = cmd >> 4;
            u8 l = cmd & 0xf;
            int nparms = 0;
            if (h < 8) nparms = cmd_parms[h][l];
            gpu->cmd_fifo[gpu->cmd_fifosize++] = cmd;
            gpu->params_pending += nparms;
            if (!nparms) gpu->master->io9.gxstat.gxfifo_size++;

            command >>= 8;
        }
    }

    if (!gpu->params_pending) {
        while (gpu->cmd_fifosize) {
            gxcmd_execute(gpu);
        }
    }

    gpu->master->io9.gxstat.gxfifo_empty =
        gpu->master->io9.gxstat.gxfifo_size ? 0 : 1;
    gpu->master->io9.gxstat.gxfifo_half =
        (gpu->master->io9.gxstat.gxfifo_size < 128) ? 1 : 0;
    gpu->master->io9.ifl.gxfifo = (gpu->master->io9.gxstat.irq_empty &&
                                   gpu->master->io9.gxstat.gxfifo_empty) ||
                                  (gpu->master->io9.gxstat.irq_half &&
                                   gpu->master->io9.gxstat.gxfifo_half);
}

void matmul(mat4* src, mat4* dst) {
    mat4 res;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float sum = 0;
            for (int k = 0; k < 4; k++) {
                sum += src->p[i][k] * dst->p[k][j];
            }
            res.p[i][j] = sum;
        }
    }
    *dst = res;
}

void vecmul(mat4* src, vec4* dst) {
    vec4 res;
    for (int i = 0; i < 4; i++) {
        float sum = 0;
        for (int k = 0; k < 4; k++) {
            sum += src->p[i][k] * dst->p[k];
        }
        res.p[i] = sum;
    }
    *dst = res;
}

void update_mtxs(GPU* gpu) {
    gpu->clipmtx = gpu->posmtx;
    matmul(&gpu->projmtx, &gpu->clipmtx);

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            gpu->master->io9.clipmtx_result[j][i] =
                gpu->clipmtx.p[i][j] * (1 << 12);
        }
    }
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            gpu->master->io9.vecmtx_result[j][i] =
                gpu->vecmtx.p[i][j] * (1 << 12);
        }
    }
}

void add_poly(GPU* gpu, vec4* p0, vec4* p1, vec4* p2, vec4* p3) {
    if (gpu->n_polys == MAX_POLY) return;
    gpu->polygonram[gpu->n_polys].p[0] = p0;
    gpu->polygonram[gpu->n_polys].p[0] = p1;
    gpu->polygonram[gpu->n_polys].p[0] = p2;
    gpu->polygonram[gpu->n_polys].p[0] = p3;
    gpu->n_polys++;
}

void add_vtx(GPU* gpu) {
    if (gpu->n_verts == MAX_VTX) return;
    gpu->vertexram[gpu->n_verts] = gpu->cur_vtx;
    vecmul(&gpu->clipmtx, &gpu->vertexram[gpu->n_verts]);
    gpu->n_verts++;
    gpu->cur_vtx_ct++;

    switch (gpu->poly_mode) {
        case POLY_TRIS:
            if (gpu->cur_vtx_ct % 3 == 0) {
                add_poly(gpu, &gpu->vertexram[gpu->n_verts - 3],
                         &gpu->vertexram[gpu->n_verts - 2],
                         &gpu->vertexram[gpu->n_verts - 1], NULL);
            }
            break;
        case POLY_QUADS:
            if (gpu->cur_vtx_ct % 4 == 0) {
                add_poly(gpu, &gpu->vertexram[gpu->n_verts - 4],
                         &gpu->vertexram[gpu->n_verts - 3],
                         &gpu->vertexram[gpu->n_verts - 2],
                         &gpu->vertexram[gpu->n_verts - 1]);
            }
            break;
        case POLY_TRI_STRIP:
            if (gpu->cur_vtx_ct >= 3) {
                if (gpu->cur_vtx_ct & 1) {
                    add_poly(gpu, &gpu->vertexram[gpu->n_verts - 3],
                             &gpu->vertexram[gpu->n_verts - 2],
                             &gpu->vertexram[gpu->n_verts - 1], NULL);
                } else {
                    add_poly(gpu, &gpu->vertexram[gpu->n_verts - 2],
                             &gpu->vertexram[gpu->n_verts - 3],
                             &gpu->vertexram[gpu->n_verts - 1], NULL);
                }
            }
            break;
        case POLY_QUAD_STRIP:
            if (gpu->cur_vtx_ct >= 4 & !(gpu->cur_vtx_ct & 1)) {
                add_poly(gpu, &gpu->vertexram[gpu->n_verts - 2],
                         &gpu->vertexram[gpu->n_verts - 4],
                         &gpu->vertexram[gpu->n_verts - 3],
                         &gpu->vertexram[gpu->n_verts - 1]);
            }
            break;
    }
}

void gxcmd_execute(GPU* gpu) {
    u8 cmd = gpu->cmd_fifo[0];
    switch (cmd) {
        case MTX_MODE:
            gpu->mtx_mode = gpu->param_fifo[0] & 3;
            break;
        case MTX_PUSH:
            switch (gpu->mtx_mode) {
                case MM_PROJ:
                    if (gpu->projstk_size == 1)
                        gpu->master->io9.gxstat.mtxstk_error = 1;
                    else {
                        gpu->projmtx_stk[gpu->projstk_size++] = gpu->projmtx;
                    }
                    gpu->master->io9.gxstat.projstk_size = gpu->projstk_size;
                    break;
                case MM_POS:
                case MM_POSVEC:
                    if (gpu->mtxstk_size == 31)
                        gpu->master->io9.gxstat.mtxstk_error = 1;
                    else {
                        gpu->posmtx_stk[gpu->mtxstk_size] = gpu->posmtx;
                        gpu->vecmtx_stk[gpu->mtxstk_size] = gpu->vecmtx;
                        gpu->mtxstk_size++;
                    }
                    gpu->master->io9.gxstat.mtxstk_size = gpu->mtxstk_size;
                    break;
            }
            break;
        case MTX_POP:
            switch (gpu->mtx_mode) {
                case MM_PROJ:
                    gpu->projstk_size = 0;
                    gpu->projmtx = gpu->projmtx_stk[gpu->projstk_size];
                    gpu->master->io9.gxstat.projstk_size = gpu->projstk_size;
                    break;
                case MM_POS:
                case MM_POSVEC:
                    gpu->mtxstk_size -= (s32) gpu->param_fifo[0] << 26 >> 26;
                    gpu->mtxstk_size &= 31;
                    gpu->posmtx = gpu->posmtx_stk[gpu->mtxstk_size];
                    gpu->vecmtx = gpu->vecmtx_stk[gpu->mtxstk_size];
                    gpu->master->io9.gxstat.mtxstk_size = gpu->mtxstk_size;
                    break;
            }
            break;
        case MTX_STORE:
            switch (gpu->mtx_mode) {
                case MM_PROJ:
                    gpu->projmtx_stk[0] = gpu->projmtx;
                    break;
                case MM_POS:
                case MM_POSVEC: {
                    int i = gpu->param_fifo[0] & 31;
                    gpu->posmtx_stk[i] = gpu->posmtx;
                    gpu->vecmtx_stk[i] = gpu->vecmtx;
                    break;
                }
            }
            break;
        case MTX_RESTORE:
            switch (gpu->mtx_mode) {
                case MM_PROJ:
                    gpu->projmtx = gpu->projmtx_stk[0];
                    break;
                case MM_POS:
                case MM_POSVEC: {
                    int i = gpu->param_fifo[0] & 31;
                    gpu->posmtx = gpu->posmtx_stk[i];
                    gpu->vecmtx = gpu->vecmtx_stk[i];
                    break;
                }
            }
            break;
        case MTX_IDENTITY: {
            mat4 m = {0};
            m.p[0][0] = 1;
            m.p[1][1] = 1;
            m.p[2][2] = 1;
            m.p[3][3] = 1;
            switch (gpu->mtx_mode) {
                case MM_PROJ:
                    gpu->projmtx = m;
                    break;
                case MM_POS:
                    gpu->posmtx = m;
                    break;
                case MM_POSVEC:
                    gpu->posmtx = m;
                    gpu->vecmtx = m;
                    break;
            }
            break;
        }
        case MTX_LOAD_44: {
            mat4 m;
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    m.p[i][j] =
                        (s32) gpu->param_fifo[4 * j + i] / (float) (1 << 12);
                }
            }
            switch (gpu->mtx_mode) {
                case MM_PROJ:
                    gpu->projmtx = m;
                    break;
                case MM_POS:
                    gpu->posmtx = m;
                    break;
                case MM_POSVEC:
                    gpu->posmtx = m;
                    gpu->vecmtx = m;
                    break;
            }
            break;
        }
        case MTX_LOAD_43: {
            mat4 m;
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 4; j++) {
                    m.p[i][j] =
                        (s32) gpu->param_fifo[3 * j + i] / (float) (1 << 12);
                }
            }
            m.p[3][0] = m.p[3][1] = m.p[3][2] = 0;
            m.p[3][3] = 1;
            switch (gpu->mtx_mode) {
                case MM_PROJ:
                    gpu->projmtx = m;
                    break;
                case MM_POS:
                    gpu->posmtx = m;
                    break;
                case MM_POSVEC:
                    gpu->posmtx = m;
                    gpu->vecmtx = m;
                    break;
            }
            break;
        }
        case MTX_MULT_44: {
            mat4 m;
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    m.p[i][j] =
                        (s32) gpu->param_fifo[4 * j + i] / (float) (1 << 12);
                }
            }
            switch (gpu->mtx_mode) {
                case MM_PROJ:
                    matmul(&m, &gpu->projmtx);
                    break;
                case MM_POS:
                    matmul(&m, &gpu->posmtx);
                    break;
                case MM_POSVEC:
                    matmul(&m, &gpu->posmtx);
                    matmul(&m, &gpu->vecmtx);
                    break;
            }
            break;
        }
        case MTX_MULT_43: {
            mat4 m;
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 4; j++) {
                    m.p[i][j] =
                        (s32) gpu->param_fifo[3 * j + i] / (float) (1 << 12);
                }
            }
            m.p[3][0] = m.p[3][1] = m.p[3][2] = 0;
            m.p[3][3] = 1;
            switch (gpu->mtx_mode) {
                case MM_PROJ:
                    matmul(&m, &gpu->projmtx);
                    break;
                case MM_POS:
                    matmul(&m, &gpu->posmtx);
                    break;
                case MM_POSVEC:
                    matmul(&m, &gpu->posmtx);
                    matmul(&m, &gpu->vecmtx);
                    break;
            }
            break;
        }
        case MTX_MULT_33: {
            mat4 m;
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 3; j++) {
                    m.p[i][j] =
                        (s32) gpu->param_fifo[3 * j + i] / (float) (1 << 12);
                }
            }
            m.p[3][0] = m.p[3][1] = m.p[3][2] = 0;
            m.p[0][3] = m.p[1][3] = m.p[2][3] = 0;
            m.p[3][3] = 1;
            switch (gpu->mtx_mode) {
                case MM_PROJ:
                    matmul(&m, &gpu->projmtx);
                    break;
                case MM_POS:
                    matmul(&m, &gpu->posmtx);
                    break;
                case MM_POSVEC:
                    matmul(&m, &gpu->posmtx);
                    matmul(&m, &gpu->vecmtx);
                    break;
            }
            break;
        }
        case MTX_SCALE: {
            mat4 m = {0};
            m.p[0][0] = (s32) gpu->param_fifo[0] / (float) (1 << 12);
            m.p[1][1] = (s32) gpu->param_fifo[1] / (float) (1 << 12);
            m.p[2][2] = (s32) gpu->param_fifo[2] / (float) (1 << 12);
            m.p[3][3] = 1;
            switch (gpu->mtx_mode) {
                case MM_PROJ:
                    matmul(&m, &gpu->projmtx);
                    break;
                case MM_POS:
                case MM_POSVEC:
                    matmul(&m, &gpu->posmtx);
                    break;
            }
            break;
        }
        case MTX_TRANS: {
            mat4 m = {0};
            m.p[0][0] = 1;
            m.p[1][1] = 1;
            m.p[2][2] = 1;
            m.p[0][3] = (s32) gpu->param_fifo[0] / (float) (1 << 12);
            m.p[1][3] = (s32) gpu->param_fifo[1] / (float) (1 << 12);
            m.p[2][3] = (s32) gpu->param_fifo[2] / (float) (1 << 12);
            m.p[3][3] = 1;
            switch (gpu->mtx_mode) {
                case MM_PROJ:
                    matmul(&m, &gpu->projmtx);
                    break;
                case MM_POS:
                    matmul(&m, &gpu->posmtx);
                    break;
                case MM_POSVEC:
                    matmul(&m, &gpu->posmtx);
                    matmul(&m, &gpu->vecmtx);
                    break;
            }
        }
        case VTX_16:
            gpu->cur_vtx.p[0] =
                ((s32) (gpu->param_fifo[0] & 0xffff) << 16) / (float) (1 << 28);
            gpu->cur_vtx.p[1] =
                (s32) (gpu->param_fifo[0] & 0xffff0000) / (float) (1 << 28);
            gpu->cur_vtx.p[2] =
                ((s32) (gpu->param_fifo[1] & 0xffff) << 16) / (float) (1 << 28);
            gpu->cur_vtx.p[3] = 1;
            add_vtx(gpu);
            break;
        case VTX_10:
            gpu->cur_vtx.p[0] =
                ((s32) (gpu->param_fifo[0] & 0x3ff) << 22) / (float) (1 << 28);
            gpu->cur_vtx.p[1] =
                ((s32) (gpu->param_fifo[0] & (0x3ff << 10)) << 12) /
                (float) (1 << 28);
            gpu->cur_vtx.p[2] =
                ((s32) (gpu->param_fifo[0] & (0x3ff << 20)) << 2) /
                (float) (1 << 28);
            gpu->cur_vtx.p[3] = 1;
            add_vtx(gpu);
            break;
        case VTX_XY:
            gpu->cur_vtx.p[0] =
                ((s32) (gpu->param_fifo[0] & 0xffff) << 16) / (float) (1 << 28);
            gpu->cur_vtx.p[1] =
                (s32) (gpu->param_fifo[0] & 0xffff0000) / (float) (1 << 28);
            add_vtx(gpu);
            break;
        case VTX_XZ:
            gpu->cur_vtx.p[0] =
                ((s32) (gpu->param_fifo[0] & 0xffff) << 16) / (float) (1 << 28);
            gpu->cur_vtx.p[2] =
                (s32) (gpu->param_fifo[0] & 0xffff0000) / (float) (1 << 28);
            add_vtx(gpu);
            break;
        case VTX_YZ:
            gpu->cur_vtx.p[1] =
                ((s32) (gpu->param_fifo[0] & 0xffff) << 16) / (float) (1 << 28);
            gpu->cur_vtx.p[2] =
                (s32) (gpu->param_fifo[0] & 0xffff0000) / (float) (1 << 28);
            add_vtx(gpu);
            break;
        case VTX_DIFF:
            gpu->cur_vtx.p[0] +=
                ((s32) (gpu->param_fifo[0] & 0x3ff) << 22 >> 6) /
                (float) (1 << 28);
            gpu->cur_vtx.p[1] +=
                ((s32) (gpu->param_fifo[0] & (0x3ff << 10)) << 12 >> 6) /
                (float) (1 << 28);
            gpu->cur_vtx.p[2] +=
                ((s32) (gpu->param_fifo[0] & (0x3ff << 20)) << 2 >> 6) /
                (float) (1 << 28);
            gpu->cur_vtx.p[3] = 1;
            add_vtx(gpu);
            break;
        case BEGIN_VTXS:
            update_mtxs(gpu);
            gpu->cur_vtx_ct = 0;
            gpu->poly_mode = gpu->param_fifo[0] & 3;
            break;
        case SWAP_BUFFERS:
            gpu_render(gpu);
            gpu->n_verts = 0;
            break;
        case BOX_TEST:
            gpu->master->io9.gxstat.boxtest = 1;
            break;
        case POS_TEST:
            gpu->cur_vtx.p[0] =
                ((s32) (gpu->param_fifo[0] & 0xffff) << 16) / (float) (1 << 28);
            gpu->cur_vtx.p[1] =
                (s32) (gpu->param_fifo[0] & 0xffff0000) / (float) (1 << 28);
            gpu->cur_vtx.p[2] =
                ((s32) (gpu->param_fifo[1] & 0xffff) << 16) / (float) (1 << 28);
            gpu->cur_vtx.p[3] = 1;
            update_mtxs(gpu);
            vec4 pos = gpu->cur_vtx;
            vecmul(&gpu->clipmtx, &pos);
            gpu->master->io9.pos_result[0] = pos.p[0] * (1 << 12);
            gpu->master->io9.pos_result[1] = pos.p[1] * (1 << 12);
            gpu->master->io9.pos_result[2] = pos.p[2] * (1 << 12);
            gpu->master->io9.pos_result[3] = pos.p[3] * (1 << 12);
            break;
        default:
            break;
    }

    u8 h = cmd >> 4;
    u8 l = cmd & 0xf;
    int nparms = 0;
    if (h < 8) nparms = cmd_parms[h][l];
    gpu->cmd_fifosize--;
    gpu->param_fifosize -= nparms;
    for (int i = 0; i < gpu->cmd_fifosize; i++) {
        gpu->cmd_fifo[i] = gpu->cmd_fifo[i + 1];
    }
    if (nparms) {
        for (int i = 0; i < gpu->param_fifosize; i++) {
            gpu->param_fifo[i] = gpu->param_fifo[i + nparms];
        }
        gpu->master->io9.gxstat.gxfifo_size -= nparms;
    } else {
        gpu->master->io9.gxstat.gxfifo_size--;
    }
}

void gpu_render(GPU* gpu) {
    memset(gpu->screen, 0, sizeof gpu->screen);
    for (int i = 0; i < gpu->n_verts; i++) {
        float w = gpu->vertexram[i].p[3];
        int x = (gpu->vertexram[i].p[0] / w + 1) * NDS_SCREEN_W / 2;
        int y = (gpu->vertexram[i].p[1] / w + 1) * NDS_SCREEN_H / 2;
        if (x < 0 || x >= NDS_SCREEN_W || y < 0 || y >= NDS_SCREEN_H) continue;
        gpu->screen[y][x] = 0xffff;
    }
}