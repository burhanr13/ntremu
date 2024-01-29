#include "gpu.h"

#include <math.h>
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

const int texformat_bpp[8] = {0, 8, 2, 4, 8, 2, 8, 16};

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

void matmul(mat4* dst, mat4* src) {
    mat4 res;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float sum = 0;
            for (int k = 0; k < 4; k++) {
                sum += dst->p[i][k] * src->p[k][j];
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
    gpu->clipmtx = gpu->projmtx;
    matmul(&gpu->clipmtx, &gpu->posmtx);

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

void add_poly(GPU* gpu, vertex* p0, vertex* p1, vertex* p2, vertex* p3) {
    if (gpu->n_polys == MAX_POLY) {
        gpu->master->io9.disp3dcnt.ram_overflow = 1;
        return;
    }
    gpu->polygonram[gpu->n_polys].p[0] = p0;
    gpu->polygonram[gpu->n_polys].p[1] = p1;
    gpu->polygonram[gpu->n_polys].p[2] = p2;
    gpu->polygonram[gpu->n_polys].p[3] = p3;
    gpu->polygonram[gpu->n_polys].attr = gpu->cur_attr;
    gpu->polygonram[gpu->n_polys].texparam = gpu->cur_texparam;
    gpu->polygonram[gpu->n_polys].pltt_base = gpu->cur_pltt_base;
    gpu->n_polys++;
    gpu->master->io9.ram_count.n_polys = gpu->n_polys;
}

void add_vtx(GPU* gpu) {
    if (gpu->n_verts == MAX_VTX) {
        gpu->master->io9.disp3dcnt.ram_overflow = 1;
        return;
    }
    update_mtxs(gpu);

    vertex v = gpu->cur_vtx;
    if (gpu->cur_texparam.transform == 3) {
        gpu->texmtx.p[0][3] = v.vt.p[0];
        gpu->texmtx.p[1][3] = v.vt.p[1];
        v.vt = v.v;
        vecmul(&gpu->texmtx, &v.vt);
    }
    
    vecmul(&gpu->clipmtx, &v.v);
    v.v.p[0] /= v.v.p[3];
    v.v.p[1] /= v.v.p[3];
    v.v.p[2] /= v.v.p[3];

    gpu->vertexram[gpu->n_verts++] = v;
    gpu->cur_vtx_ct++;
    gpu->master->io9.ram_count.n_verts = gpu->n_verts;

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
            if (gpu->cur_vtx_ct >= 4 && !(gpu->cur_vtx_ct & 1)) {
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
                case MM_TEX:
                    gpu->texmtx_stack[0] = gpu->texmtx;
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
                case MM_TEX:
                    gpu->texmtx = gpu->texmtx_stack[0];
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
                case MM_TEX:
                    gpu->texmtx_stack[0] = gpu->texmtx;
                    break;
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
                case MM_TEX:
                    gpu->texmtx = gpu->texmtx_stack[0];
                    break;
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
                case MM_TEX:
                    gpu->texmtx = m;
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
                case MM_TEX:
                    gpu->texmtx = m;
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
                case MM_TEX:
                    gpu->texmtx = m;
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
                    matmul(&gpu->projmtx, &m);
                    break;
                case MM_POS:
                    matmul(&gpu->posmtx, &m);
                    break;
                case MM_POSVEC:
                    matmul(&gpu->posmtx, &m);
                    matmul(&gpu->vecmtx, &m);
                    break;
                case MM_TEX:
                    matmul(&gpu->texmtx, &m);
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
                    matmul(&gpu->projmtx, &m);
                    break;
                case MM_POS:
                    matmul(&gpu->posmtx, &m);
                    break;
                case MM_POSVEC:
                    matmul(&gpu->posmtx, &m);
                    matmul(&gpu->vecmtx, &m);
                    break;
                case MM_TEX:
                    matmul(&gpu->texmtx, &m);
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
                    matmul(&gpu->projmtx, &m);
                    break;
                case MM_POS:
                    matmul(&gpu->posmtx, &m);
                    break;
                case MM_POSVEC:
                    matmul(&gpu->posmtx, &m);
                    matmul(&gpu->vecmtx, &m);
                    break;
                case MM_TEX:
                    matmul(&gpu->texmtx, &m);
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
                    matmul(&gpu->projmtx, &m);
                    break;
                case MM_POS:
                case MM_POSVEC:
                    matmul(&gpu->posmtx, &m);
                    break;
                case MM_TEX:
                    matmul(&gpu->texmtx, &m);
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
                    matmul(&gpu->projmtx, &m);
                    break;
                case MM_POS:
                    matmul(&gpu->posmtx, &m);
                    break;
                case MM_POSVEC:
                    matmul(&gpu->posmtx, &m);
                    matmul(&gpu->vecmtx, &m);
                    break;
                case MM_TEX:
                    matmul(&gpu->texmtx, &m);
                    break;
            }
            break;
        }
        case COLOR:
            gpu->cur_vtx.color = gpu->param_fifo[0];
            break;
        case NORMAL:
            gpu->cur_vtx.color = (gpu->cur_mtl0.w & 0xffff);
            break;
        case TEXCOORD:
            gpu->cur_vtx.vt.p[0] =
                ((s32) (gpu->param_fifo[0] & 0xffff) << 16) / (float) (1 << 20);
            gpu->cur_vtx.vt.p[1] =
                (s32) (gpu->param_fifo[0] & 0xffff0000) / (float) (1 << 20);
            if (gpu->cur_texparam.transform == 1) {
                gpu->cur_vtx.vt.p[2] = 0.0625f;
                gpu->cur_vtx.vt.p[3] = 0.0625f;
                vecmul(&gpu->texmtx, &gpu->cur_vtx.vt);
            }
            break;
        case VTX_16:
            gpu->cur_vtx.v.p[0] =
                ((s32) (gpu->param_fifo[0] & 0xffff) << 16) / (float) (1 << 28);
            gpu->cur_vtx.v.p[1] =
                (s32) (gpu->param_fifo[0] & 0xffff0000) / (float) (1 << 28);
            gpu->cur_vtx.v.p[2] =
                ((s32) (gpu->param_fifo[1] & 0xffff) << 16) / (float) (1 << 28);
            gpu->cur_vtx.v.p[3] = 1;
            add_vtx(gpu);
            break;
        case VTX_10:
            gpu->cur_vtx.v.p[0] =
                ((s32) (gpu->param_fifo[0] & 0x3ff) << 22) / (float) (1 << 28);
            gpu->cur_vtx.v.p[1] =
                ((s32) (gpu->param_fifo[0] & (0x3ff << 10)) << 12) /
                (float) (1 << 28);
            gpu->cur_vtx.v.p[2] =
                ((s32) (gpu->param_fifo[0] & (0x3ff << 20)) << 2) /
                (float) (1 << 28);
            gpu->cur_vtx.v.p[3] = 1;
            add_vtx(gpu);
            break;
        case VTX_XY:
            gpu->cur_vtx.v.p[0] =
                ((s32) (gpu->param_fifo[0] & 0xffff) << 16) / (float) (1 << 28);
            gpu->cur_vtx.v.p[1] =
                (s32) (gpu->param_fifo[0] & 0xffff0000) / (float) (1 << 28);
            add_vtx(gpu);
            break;
        case VTX_XZ:
            gpu->cur_vtx.v.p[0] =
                ((s32) (gpu->param_fifo[0] & 0xffff) << 16) / (float) (1 << 28);
            gpu->cur_vtx.v.p[2] =
                (s32) (gpu->param_fifo[0] & 0xffff0000) / (float) (1 << 28);
            add_vtx(gpu);
            break;
        case VTX_YZ:
            gpu->cur_vtx.v.p[1] =
                ((s32) (gpu->param_fifo[0] & 0xffff) << 16) / (float) (1 << 28);
            gpu->cur_vtx.v.p[2] =
                (s32) (gpu->param_fifo[0] & 0xffff0000) / (float) (1 << 28);
            add_vtx(gpu);
            break;
        case VTX_DIFF:
            gpu->cur_vtx.v.p[0] +=
                ((s32) (gpu->param_fifo[0] & 0x3ff) << 22 >> 6) /
                (float) (1 << 28);
            gpu->cur_vtx.v.p[1] +=
                ((s32) (gpu->param_fifo[0] & (0x3ff << 10)) << 12 >> 6) /
                (float) (1 << 28);
            gpu->cur_vtx.v.p[2] +=
                ((s32) (gpu->param_fifo[0] & (0x3ff << 20)) << 2 >> 6) /
                (float) (1 << 28);
            add_vtx(gpu);
            break;
        case POLYGON_ATTR:
            gpu->cur_attr.w = gpu->param_fifo[0];
            break;
        case TEXIMAGE_PARAM:
            gpu->cur_texparam.w = gpu->param_fifo[0];
            break;
        case PLTT_BASE:
            gpu->cur_pltt_base = gpu->param_fifo[0] & 0x1fff;
            break;
        case DIF_AMB:
            gpu->cur_mtl0.w = gpu->param_fifo[0];
            break;
        case SPE_EMI:
            gpu->cur_mtl1.w = gpu->param_fifo[0];
            break;
        case BEGIN_VTXS:
            update_mtxs(gpu);
            gpu->cur_vtx_ct = 0;
            gpu->poly_mode = gpu->param_fifo[0] & 3;
            break;
        case END_VTXS:
            break;
        case SWAP_BUFFERS:
            gpu_render(gpu);
            gpu->n_verts = 0;
            gpu->n_polys = 0;
            gpu->master->io9.ram_count.w = 0;
            gpu->w_buffer = gpu->param_fifo[0] & 2;
            break;
        case VIEWPORT: {
            int x0, y0, x1, y1;
            x0 = gpu->param_fifo[0] & 0xff;
            y0 = (gpu->param_fifo[0] >> 8) & 0xff;
            x1 = (gpu->param_fifo[0] >> 0x10) & 0xff;
            y1 = (gpu->param_fifo[0] >> 0x18) & 0xff;
            gpu->view_x = x0;
            gpu->view_y = y0;
            gpu->view_w = x1 - x0;
            gpu->view_h = y1 - y0;
            break;
        }
        case BOX_TEST:
            gpu->master->io9.gxstat.boxtest = 1;
            break;
        case POS_TEST:
            gpu->cur_vtx.v.p[0] =
                ((s32) (gpu->param_fifo[0] & 0xffff) << 16) / (float) (1 << 28);
            gpu->cur_vtx.v.p[1] =
                (s32) (gpu->param_fifo[0] & 0xffff0000) / (float) (1 << 28);
            gpu->cur_vtx.v.p[2] =
                ((s32) (gpu->param_fifo[1] & 0xffff) << 16) / (float) (1 << 28);
            gpu->cur_vtx.v.p[3] = 1;
            update_mtxs(gpu);
            vec4 pos = gpu->cur_vtx.v;
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

bool in_screen_bound(vertex* v) {
    return -1 <= v->v.p[0] && v->v.p[0] <= 1 && -1 <= v->v.p[1] &&
           v->v.p[1] <= 1 && v->v.p[2] >= 0;
}

void render_line(GPU* gpu, vertex* v0, vertex* v1) {
    int x0 = (v0->v.p[0] + 1) * gpu->view_w / 2 + gpu->view_x;
    int y0 = (1 - v0->v.p[1]) * gpu->view_h / 2 + gpu->view_y;
    int x1 = (v1->v.p[0] + 1) * gpu->view_w / 2 + gpu->view_x;
    int y1 = (1 - v1->v.p[1]) * gpu->view_h / 2 + gpu->view_y;

    float m = (float) (y1 - y0) / (x1 - x0);
    if (fabsf(m) > 1) {
        m = 1 / m;
        if (y0 > y1) {
            int t = y0;
            y0 = y1;
            y1 = t;
            x0 = x1;
        }
        float x = x0;
        if (y0 < 0) y0 = 0;
        if (y1 >= NDS_SCREEN_H) y1 = NDS_SCREEN_H - 1;
        for (int y = y0; y <= y1; y++, x += m) {
            int sx = x;
            if (sx < 0 || sx >= NDS_SCREEN_W) continue;
            gpu->screen[y][sx] = 0x801f;
        }
    } else {
        if (x0 > x1) {
            int t = x0;
            x0 = x1;
            x1 = t;
            y0 = y1;
        }
        float y = y0;
        if (x0 < 0) x0 = 0;
        if (x1 >= NDS_SCREEN_W) x1 = NDS_SCREEN_W - 1;
        for (int x = x0; x <= x1; x++, y += m) {
            int sy = y;
            if (sy < 0 || sy >= NDS_SCREEN_H) continue;
            gpu->screen[sy][x] = 0x801f;
        }
    }
}

void render_polygon_wireframe(GPU* gpu, poly* p) {
    if (!in_screen_bound(p->p[0]) && !in_screen_bound(p->p[1]) &&
        !in_screen_bound(p->p[2]))
        return;

    render_line(gpu, p->p[0], p->p[1]);
    render_line(gpu, p->p[1], p->p[2]);
    if (p->p[3]) {
        render_line(gpu, p->p[2], p->p[3]);
        render_line(gpu, p->p[3], p->p[0]);
    } else {
        render_line(gpu, p->p[2], p->p[0]);
    }
}

void render_line_attrs(GPU* gpu, vertex* v0, vertex* v1,
                       struct raster_attrs* left, struct raster_attrs* right) {
    int x0 = (v0->v.p[0] + 1) * gpu->view_w / 2 + gpu->view_x;
    int y0 = (1 - v0->v.p[1]) * gpu->view_h / 2 + gpu->view_y;
    int x1 = (v1->v.p[0] + 1) * gpu->view_w / 2 + gpu->view_x;
    int y1 = (1 - v1->v.p[1]) * gpu->view_h / 2 + gpu->view_y;

    float z0 = v0->v.p[2];
    float z1 = v1->v.p[2];
    float w0 = 1 / v0->v.p[3];
    float w1 = 1 / v1->v.p[3];

    float s0 = v0->vt.p[0];
    float s1 = v1->vt.p[0];
    float t0 = v0->vt.p[1];
    float t1 = v1->vt.p[1];

    u16 c0 = v0->color;
    u16 c1 = v1->color;

    float m = (float) (y1 - y0) / (x1 - x0);
    if (fabsf(m) > 1) {
        m = 1 / m;
        if (y0 > y1) {
            int ti = y0;
            y0 = y1;
            y1 = ti;
            x0 = x1;
            ti = c0;
            c0 = c1;
            c1 = ti;
            float tf = w0;
            w0 = w1;
            w1 = tf;
            tf = z0;
            z0 = z1;
            z1 = tf;
            tf = s0;
            s0 = s1;
            s1 = tf;
            tf = t0;
            t0 = t1;
            t1 = tf;
        }
        int h = y1 - y0;

        float x = x0;
        float z = z0;
        float dz = (z1 - z) / h;
        float w = w0;
        float dw = (w1 - w) / h;
        float s = s0 * w0;
        float ds = (s1 * w1 - s) / h;
        float t = t0 * w0;
        float dt = (t1 * w1 - t) / h;
        float r = (c0 & 0x1f) * w0;
        float g = ((c0 >> 5) & 0x1f) * w0;
        float b = ((c0 >> 10) & 0x1f) * w0;
        float dr = ((c1 & 0x1f) * w1 - r) / h;
        float dg = (((c1 >> 5) & 0x1f) * w1 - g) / h;
        float db = (((c1 >> 10) & 0x1f) * w1 - b) / h;
        for (int y = y0; y <= y1; y++, x += m, z += dz, w += dw, s += ds,
                 t += dt, r += dr, g += dg, b += db) {
            int sx = x, sy = y;
            if (sy < 0 || sy >= NDS_SCREEN_H) continue;
            if (sx < 0) sx = 0;
            if (sx >= NDS_SCREEN_W) sx = NDS_SCREEN_W - 1;
            if (sx <= left[sy].x) {
                left[sy].x = sx;
                left[sy].z = z;
                left[sy].w = w;
                left[sy].s = s;
                left[sy].t = t;
                left[sy].r = r;
                left[sy].g = g;
                left[sy].b = b;
            }
            if (sx >= right[sy].x) {
                right[sy].x = sx;
                right[sy].z = z;
                right[sy].w = w;
                right[sy].s = s;
                right[sy].t = t;
                right[sy].r = r;
                right[sy].g = g;
                right[sy].b = b;
            }
        }
    } else {
        if (x0 > x1) {
            int ti = x0;
            x0 = x1;
            x1 = ti;
            y0 = y1;
            ti = c0;
            c0 = c1;
            c1 = ti;
            float tf = w0;
            w0 = w1;
            w1 = tf;
            tf = z0;
            z0 = z1;
            z1 = tf;
            tf = s0;
            s0 = s1;
            s1 = tf;
            tf = t0;
            t0 = t1;
            t1 = tf;
        }
        int h = x1 - x0;

        float y = y0;
        float z = z0;
        float dz = (z1 - z) / h;
        float w = w0;
        float dw = (w1 - w) / h;
        float s = s0 * w0;
        float ds = (s1 * w1 - s) / h;
        float t = t0 * w0;
        float dt = (t1 * w1 - t) / h;
        float r = (c0 & 0x1f) * w0;
        float g = ((c0 >> 5) & 0x1f) * w0;
        float b = ((c0 >> 10) & 0x1f) * w0;
        float dr = ((c1 & 0x1f) * w1 - r) / h;
        float dg = (((c1 >> 5) & 0x1f) * w1 - g) / h;
        float db = (((c1 >> 10) & 0x1f) * w1 - b) / h;
        for (int x = x0; x <= x1; x++, y += m, z += dz, w += dw, s += ds,
                 t += dt, r += dr, g += dg, b += db) {
            int sx = x, sy = y;
            if (sy < 0 || sy >= NDS_SCREEN_H) continue;
            if (sx < 0) sx = 0;
            if (sx >= NDS_SCREEN_W) sx = NDS_SCREEN_W - 1;
            if (sx <= left[sy].x) {
                left[sy].x = sx;
                left[sy].z = z;
                left[sy].w = w;
                left[sy].s = s;
                left[sy].t = t;
                left[sy].r = r;
                left[sy].g = g;
                left[sy].b = b;
            }
            if (sx >= right[sy].x) {
                right[sy].x = sx;
                right[sy].z = z;
                right[sy].w = w;
                right[sy].s = s;
                right[sy].t = t;
                right[sy].r = r;
                right[sy].g = g;
                right[sy].b = b;
            }
        }
    }
}

void render_polygon(GPU* gpu, poly* p) {
    if (!in_screen_bound(p->p[0]) && !in_screen_bound(p->p[1]) &&
        !in_screen_bound(p->p[2]))
        return;

    if (p->attr.alpha == 0) {
        render_polygon_wireframe(gpu, p);
        return;
    } else if (p->attr.alpha < 31) return;

    if (p->attr.mode == 3) return;

    float ax = p->p[1]->v.p[0] - p->p[0]->v.p[0];
    float ay = p->p[1]->v.p[1] - p->p[0]->v.p[1];
    float bx = p->p[2]->v.p[0] - p->p[0]->v.p[0];
    float by = p->p[2]->v.p[1] - p->p[0]->v.p[1];
    float area = ax * by - ay * bx;
    if (area < 0 && !p->attr.back) return;
    if (area > 0 && !p->attr.front) return;

    struct raster_attrs left[NDS_SCREEN_H], right[NDS_SCREEN_H];
    for (int y = 0; y < NDS_SCREEN_H; y++) {
        left[y].x = NDS_SCREEN_W;
        right[y].x = -1;
    }

    render_line_attrs(gpu, p->p[0], p->p[1], left, right);
    render_line_attrs(gpu, p->p[1], p->p[2], left, right);
    if (p->p[3]) {
        render_line_attrs(gpu, p->p[2], p->p[3], left, right);
        render_line_attrs(gpu, p->p[3], p->p[0], left, right);
    } else {
        render_line_attrs(gpu, p->p[2], p->p[0], left, right);
    }

    if (gpu->master->io9.disp3dcnt.texture && p->texparam.format) {

        u32 base = p->texparam.offset << 3;
        u32 s_shift = p->texparam.s_size + 3;
        u32 t_shift = p->texparam.t_size + 3;
        int format = p->texparam.format;

        u32 palbase = p->pltt_base << 3;
        if (format == TEX_2BPP) palbase >>= 1;

        for (int y = 0; y < NDS_SCREEN_H; y++) {
            if (left[y].x > right[y].x) continue;
            int h = right[y].x - left[y].x;

            float z = left[y].z;
            float w = left[y].w;
            float dz = (right[y].z - z) / h;
            float dw = (right[y].w - w) / h;
            float s = left[y].s;
            float t = left[y].t;
            float ds = (right[y].s - s) / h;
            float dt = (right[y].t - t) / h;

            float r = left[y].r;
            float g = left[y].g;
            float b = left[y].b;
            float dr = (right[y].r - r) / h;
            float dg = (right[y].g - g) / h;
            float db = (right[y].b - b) / h;
            for (int x = left[y].x; x <= right[y].x; x++, z += dz, w += dw,
                     s += ds, t += dt, r += dr, g += dg, b += db) {
                bool depth_test;
                if (p->attr.depth_test) {
                    if (gpu->w_buffer)
                        depth_test = fabsf(w - gpu->depth_buf[y][x]) <= 0.125;
                    else depth_test = fabsf(z - gpu->depth_buf[y][x]) <= 0.125;
                } else {
                    if (gpu->w_buffer) depth_test = w > gpu->depth_buf[y][x];
                    else depth_test = z < gpu->depth_buf[y][x];
                }
                if (0 < z && depth_test) {
                    if (gpu->w_buffer) gpu->depth_buf[y][x] = w;
                    else gpu->depth_buf[y][x] = z;

                    s32 ss = s / w;
                    s32 tt = t / w;
                    if (p->texparam.s_rep) {
                        bool flip = p->texparam.s_flip && ((ss >> s_shift) & 1);
                        ss &= (1 << s_shift) - 1;
                        if (flip) ss = (1 << s_shift) - 1 - ss;
                    } else {
                        if (ss < 0) ss = 0;
                        if (ss > (1 << s_shift) - 1) ss = (1 << s_shift) - 1;
                    }
                    if (p->texparam.t_rep) {
                        bool flip = p->texparam.t_flip && ((tt >> t_shift) & 1);
                        tt &= (1 << t_shift) - 1;
                        if (flip) tt = (1 << t_shift) - 1 - tt;
                    } else {
                        if (tt < 0) tt = 0;
                        if (tt > (1 << t_shift) - 1) tt = (1 << t_shift) - 1;
                    }
                    u32 ofs = (tt << s_shift) + ss;

                    u16 color;
                    bool transparent = false;
                    switch (format) {
                        case TEX_2BPP: {
                            u32 addr = base + (ofs >> 2);
                            u8 col_ind =
                                gpu->texram[addr >> 17][addr & 0x1ffff];
                            col_ind >>= (ofs & 3) << 1;
                            col_ind &= 3;
                            if (!col_ind && p->texparam.color0)
                                transparent = true;
                            else {
                                u32 paladdr = palbase + col_ind;
                                color = gpu->texpal[paladdr >> 13]
                                                   [paladdr & 0x1fff];
                            }
                            break;
                        }
                        case TEX_4BPP: {
                            u32 addr = base + (ofs >> 1);
                            u8 col_ind =
                                gpu->texram[addr >> 17][addr & 0x1ffff];
                            col_ind >>= (ofs & 1) << 2;
                            col_ind &= 15;
                            if (!col_ind && p->texparam.color0)
                                transparent = true;
                            else {
                                u32 paladdr = palbase + col_ind;
                                color = gpu->texpal[paladdr >> 13]
                                                   [paladdr & 0x1fff];
                            }
                            break;
                        }
                        case TEX_8BPP: {
                            u32 addr = base + ofs;
                            u8 col_ind =
                                gpu->texram[addr >> 17][addr & 0x1ffff];
                            if (!col_ind && p->texparam.color0)
                                transparent = true;
                            else {
                                u32 paladdr = palbase + col_ind;
                                color = gpu->texpal[paladdr >> 13]
                                                   [paladdr & 0x1fff];
                            }
                            break;
                        }
                        case TEX_A3I5: {
                            u32 addr = base + ofs;
                            u8 col_ind =
                                gpu->texram[addr >> 17][addr & 0x1ffff];
                            u8 alpha = col_ind >> 5;
                            col_ind &= 31;
                            if (alpha < 7) transparent = true;
                            else {
                                u32 paladdr = palbase + col_ind;
                                color = gpu->texpal[paladdr >> 13]
                                                   [paladdr & 0x1fff];
                            }
                            break;
                        }
                        case TEX_A5I3: {
                            u32 addr = base + ofs;
                            u8 col_ind =
                                gpu->texram[addr >> 17][addr & 0x1ffff];
                            u8 alpha = col_ind >> 3;
                            col_ind &= 7;
                            if (alpha < 31) transparent = true;
                            else {
                                u32 paladdr = palbase + col_ind;
                                color = gpu->texpal[paladdr >> 13]
                                                   [paladdr & 0x1fff];
                            }
                            break;
                        }
                        case TEX_COMPRESS: {
                            u32 block_ofs =
                                ((tt >> 2) << (s_shift - 2)) + (ss >> 2);
                            u32 addr = base + (block_ofs << 2) + (tt & 3);
                            u8 ind = gpu->texram[addr >> 17][addr & 0x1ffff];
                            ind >>= (ss & 3) << 1;
                            ind &= 3;
                            u16 palmode =
                                *(u16*) &gpu->texram[1][(addr >> 18 << 16) +
                                                        (block_ofs << 1)];
                            u32 paladdr = palbase + ((palmode & 0x3fff) << 1);
                            palmode >>= 14;
                            if (palmode < 2 && ind == 3) transparent = true;
                            else if (ind < 2 || !(palmode & 1)) {
                                paladdr += ind;
                                color = gpu->texpal[paladdr >> 13]
                                                   [paladdr & 0x1fff];
                            } else {
                                u16 color0 = gpu->texpal[paladdr >> 13]
                                                        [paladdr & 0x1fff];
                                paladdr++;
                                u16 color1 = gpu->texpal[paladdr >> 13]
                                                        [paladdr & 0x1fff];
                                u16 r0 = color0 & 0x1f;
                                u16 r1 = color1 & 0x1f;
                                u16 g0 = (color0 >> 5) & 0x1f;
                                u16 g1 = (color1 >> 5) & 0x1f;
                                u16 b0 = (color0 >> 10) & 0x1f;
                                u16 b1 = (color1 >> 10) & 0x1f;
                                if (palmode == 1) {
                                    color = (r0 + r1) / 2 | (g0 + g1) / 2 << 5 |
                                            (b0 + b1) / 2 << 10;
                                } else if (ind == 2) {
                                    color = (5 * r0 + 3 * r1) / 8 |
                                            (5 * g0 + 3 * g1) / 8 << 5 |
                                            (5 * b0 + 3 * b1) / 8 << 10;
                                } else {
                                    color = (3 * r0 + 5 * r1) / 8 |
                                            (3 * g0 + 5 * g1) / 8 << 5 |
                                            (3 * b0 + 5 * b1) / 8 << 10;
                                }
                            }

                            break;
                        }
                        case TEX_DIRECT: {
                            u32 addr = base + (ofs << 1);
                            color = *(u16*) &gpu
                                         ->texram[addr >> 17][addr & 0x1ffff];
                            if (!(color & 0x8000)) transparent = true;
                            break;
                        }
                        default:
                            transparent = true;
                            break;
                    }
                    if (transparent) continue;

                    u16 c = 0x8000;
                    c |= (u16) (r / w * (color & 0x1f) / 32) & 0x1f;
                    c |= ((u16) (g / w * ((color >> 5) & 0x1f) / 32) & 0x1f)
                         << 5;
                    c |= ((u16) (b / w * ((color >> 10) & 0x1f) / 32) & 0x1f)
                         << 10;
                    gpu->screen[y][x] = c;
                }
            }
        }
    } else {
        for (int y = 0; y < NDS_SCREEN_H; y++) {
            if (left[y].x > right[y].x) continue;
            int h = right[y].x - left[y].x;

            float z = left[y].z;
            float w = left[y].w;
            float dz = (right[y].z - z) / h;
            float dw = (right[y].w - w) / h;

            float r = left[y].r;
            float g = left[y].g;
            float b = left[y].b;
            float dr = (right[y].r - r) / h;
            float dg = (right[y].g - g) / h;
            float db = (right[y].b - b) / h;
            for (int x = left[y].x; x <= right[y].x;
                 x++, z += dz, w += dw, r += dr, g += dg, b += db) {
                bool depth_test;
                if (p->attr.depth_test) {
                    if (gpu->w_buffer)
                        depth_test = fabsf(w - gpu->depth_buf[y][x]) <= 0.125;
                    else depth_test = fabsf(z - gpu->depth_buf[y][x]) <= 0.125;
                } else {
                    if (gpu->w_buffer) depth_test = w > gpu->depth_buf[y][x];
                    else depth_test = z < gpu->depth_buf[y][x];
                }
                if (0 < z && depth_test) {
                    if (gpu->w_buffer) gpu->depth_buf[y][x] = w;
                    else gpu->depth_buf[y][x] = z;

                    u16 c = 0x8000;
                    c |= (u16) (r / w) & 0x1f;
                    c |= ((u16) (g / w) & 0x1f) << 5;
                    c |= ((u16) (b / w) & 0x1f) << 10;
                    gpu->screen[y][x] = c;
                }
            }
        }
    }
}

void gpu_render(GPU* gpu) {
    memset(gpu->screen, 0, sizeof gpu->screen);
    for (int y = 0; y < NDS_SCREEN_H; y++) {
        for (int x = 0; x < NDS_SCREEN_W; x++) {
            if (gpu->master->io9.clear_color.alpha) {
                gpu->screen[y][x] = 0x8000 | gpu->master->io9.clear_color.color;
            }
            if (gpu->w_buffer) {
                gpu->depth_buf[y][x] = 1 / (float) 0x7fff;
            } else {
                gpu->depth_buf[y][x] = 0x7fff;
            }
        }
    }
    for (int i = 0; i < gpu->n_polys; i++) {
        render_polygon(gpu, &gpu->polygonram[i]);
    }
}