#include "gpu.h"

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "emulator_state.h"
#include "io.h"
#include "nds.h"

pthread_t gpu_thread;
pthread_mutex_t gpu_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t gpu_cond = PTHREAD_COND_INITIALIZER;

const int cmd_parms[8][16] = {{0},
                              {1, 0, 1, 1, 1, 0, 16, 12, 16, 12, 9, 3, 3},
                              {1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1},
                              {1, 1, 1, 1, 32},
                              {1, 0},
                              {1},
                              {1},
                              {3, 2, 1}};

void* gpu_thread_run(void* data) {
    GPU* gpu = data;
    pthread_mutex_lock(&gpu_mutex);
    while (true) {
        pthread_cond_wait(&gpu_cond, &gpu_mutex);
        gpu_render(gpu);
    }
    return NULL;
}

void init_gpu_thread(GPU* gpu) {
    pthread_create(&gpu_thread, NULL, gpu_thread_run, gpu);
    pthread_detach(gpu_thread);
}

void destroy_gpu_thread() {
    pthread_cancel(gpu_thread);
    pthread_mutex_destroy(&gpu_mutex);
    pthread_cond_destroy(&gpu_cond);
}

void gpu_init_ptrs(GPU* gpu) {
    gpu->screen = gpu->framebuffers[0];
    gpu->screen_back = gpu->framebuffers[1];

    gpu->vertexram = gpu->vertexrambufs[0];
    gpu->vertexram_rendering = gpu->vertexrambufs[1];

    gpu->polygonram = gpu->polygonrambufs[0];
    gpu->polygonram_rendering = gpu->polygonrambufs[1];
}

void gxfifo_write(GPU* gpu, u32 command) {
    if (gpu->master->io9.gxstat.gxfifo_size == 256) {
        gpu->master->sched.now = gpu->master->next_vblank;
        run_to_present(&gpu->master->sched);
    }

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

    if (!gpu->blocked) {
        gxcmd_execute_all(gpu);
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

void gxcmd_execute_all(GPU* gpu) {
    if (!gpu->params_pending) {
        while (!gpu->blocked && gpu->cmd_fifosize) {
            gxcmd_execute(gpu);
        }
    }
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
    if (!gpu->mtx_dirty) return;
    gpu->mtx_dirty = false;

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

    if (ntremu.freecam) {
        gpu->clipmtx = gpu->projmtx;
        matmul(&gpu->clipmtx, &ntremu.freecam_mtx);
        matmul(&gpu->clipmtx, &gpu->posmtx);
    }
}

void interp_vtxs(vertex* cur, vertex* prev, float diffcur, float diffprev,
                 vertex* dst) {
    for (int k = 0; k < 4; k++) {
        dst->v.p[k] = cur->v.p[k] * diffprev + prev->v.p[k] * diffcur;
        dst->v.p[k] /= diffprev + diffcur;
    }
    for (int k = 0; k < 2; k++) {
        dst->vt.p[k] = cur->vt.p[k] * diffprev + prev->vt.p[k] * diffcur;
        dst->vt.p[k] /= diffprev + diffcur;
    }
    dst->r = cur->r * diffprev + prev->r * diffcur;
    dst->r /= diffprev + diffcur;
    dst->g = cur->g * diffprev + prev->g * diffcur;
    dst->g /= diffprev + diffcur;
    dst->b = cur->b * diffprev + prev->b * diffcur;
    dst->b /= diffprev + diffcur;
}

int clip_poly(vertex* vtxs, int n) {
    vertex dst[MAX_POLY_N];
    bool clip = false;

    for (int i = 6; i >= 0; i--) {
        int n_new = 0;
        for (int cur = 0; cur < n; cur++) {
            int prev = cur ? cur - 1 : n - 1;

            float diffcur = vtxs[cur].v.p[3];
            float diffprev = vtxs[prev].v.p[3];
            if (i & 1) {
                diffcur -= vtxs[cur].v.p[i / 2];
                diffprev -= vtxs[prev].v.p[i / 2];
            } else {
                diffcur += vtxs[cur].v.p[i / 2];
                diffprev += vtxs[prev].v.p[i / 2];
            }

            if (diffcur * diffprev < 0) {
                interp_vtxs(&vtxs[cur], &vtxs[prev], fabsf(diffcur),
                            fabsf(diffprev), &dst[n_new++]);
            }
            if (diffcur >= 0) dst[n_new++] = vtxs[cur];
            else clip = true;
        }
        n = n_new;

        for (int j = 0; j < n; j++) {
            vtxs[j] = dst[j];
        }
    }

    return clip ? n : -1;
}

float calc_area(vec4* v0, vec4* v1, vec4* v2) {
    float ax = v1->p[0] / v1->p[3] - v0->p[0] / v0->p[3];
    float ay = v1->p[1] / v1->p[3] - v0->p[1] / v0->p[3];
    float bx = v2->p[0] / v2->p[3] - v0->p[0] / v0->p[3];
    float by = v2->p[1] / v2->p[3] - v0->p[1] / v0->p[3];
    return ax * by - ay * bx;
}

bool add_poly(GPU* gpu, int n_orig, bool strip) {
    if (gpu->n_polys == MAX_POLY) {
        gpu->master->io9.disp3dcnt.ram_overflow = 1;
        return false;
    }

    vertex vtxs[MAX_POLY_N];

    if (strip) {
        vtxs[0] = *gpu->cur_poly_strip[0];
        vtxs[1] = *gpu->cur_poly_strip[1];
        for (int i = 2; i < n_orig; i++) {
            vtxs[i] = gpu->cur_poly_vtxs[i];
        }
    } else {
        for (int i = 0; i < n_orig; i++) {
            vtxs[i] = gpu->cur_poly_vtxs[i];
        }
    }

    bool clipped = true;
    int n = clip_poly(vtxs, n_orig);
    if (!n) return false;
    if (n == -1) {
        clipped = false;
        n = n_orig;
    }
    float area = calc_area(&vtxs[0].v, &vtxs[1].v, &vtxs[n - 1].v);
    if ((area < 0 && !gpu->cur_attr.back) || (area > 0 && !gpu->cur_attr.front))
        return false;

    if (!strip || clipped) {
        for (int i = 0; i < n; i++) {
            if (gpu->n_verts == MAX_VTX) {
                gpu->master->io9.disp3dcnt.ram_overflow = 1;
                return false;
            }

            gpu->vertexram[gpu->n_verts] = vtxs[i];
            gpu->polygonram[gpu->n_polys].p[i] = &gpu->vertexram[gpu->n_verts];
            if (!clipped)
                gpu->cur_poly_strip[i] = &gpu->vertexram[gpu->n_verts];
            gpu->n_verts++;
        }
    } else {
        gpu->polygonram[gpu->n_polys].p[0] = gpu->cur_poly_strip[0];
        gpu->polygonram[gpu->n_polys].p[1] = gpu->cur_poly_strip[1];

        for (int i = 2; i < n; i++) {
            if (gpu->n_verts == MAX_VTX) {
                gpu->master->io9.disp3dcnt.ram_overflow = 1;
                return false;
            }

            gpu->vertexram[gpu->n_verts] = vtxs[i];
            gpu->polygonram[gpu->n_polys].p[i] = &gpu->vertexram[gpu->n_verts];
            gpu->cur_poly_strip[i] = &gpu->vertexram[gpu->n_verts];
            gpu->n_verts++;
        }
    }

    gpu->polygonram[gpu->n_polys].n = n;

    gpu->polygonram[gpu->n_polys].attr = gpu->cur_attr;
    gpu->polygonram[gpu->n_polys].texparam = gpu->cur_texparam;
    gpu->polygonram[gpu->n_polys].pltt_base = gpu->cur_pltt_base;
    gpu->n_polys++;

    gpu->master->io9.ram_count.n_verts = gpu->n_verts;
    gpu->master->io9.ram_count.n_polys = gpu->n_polys;
    return !clipped;
}

void add_vtx(GPU* gpu) {
    if (gpu->n_verts == MAX_VTX) {
        gpu->master->io9.disp3dcnt.ram_overflow = 1;
        return;
    }
    update_mtxs(gpu);

    if (gpu->cur_texparam.transform == TEXTF_VTX) {
        gpu->cur_vtx.vt = gpu->cur_vtx.v;
        gpu->cur_vtx.vt.p[3] = 0;
        vecmul(&gpu->texmtx, &gpu->cur_vtx.vt);
        gpu->cur_vtx.vt.p[0] /= 16;
        gpu->cur_vtx.vt.p[1] /= 16;
        gpu->cur_vtx.vt.p[0] += gpu->cur_texcoord.p[0];
        gpu->cur_vtx.vt.p[1] += gpu->cur_texcoord.p[1];
    }

    vertex v = gpu->cur_vtx;
    vecmul(&gpu->clipmtx, &v.v);

    if (gpu->cur_vtx_ct < 2) {
        gpu->cur_poly_vtxs[gpu->cur_vtx_ct++] = v;
        gpu->tri_orient = !gpu->tri_orient;
    } else {
        switch (gpu->poly_mode) {
            case POLY_TRIS: {
                gpu->cur_poly_vtxs[2] = v;
                add_poly(gpu, 3, false);
                gpu->cur_vtx_ct = 0;
                break;
            }
            case POLY_QUADS: {
                gpu->cur_poly_vtxs[gpu->cur_vtx_ct++] = v;
                if (gpu->cur_vtx_ct == 4) {
                    add_poly(gpu, 4, false);
                    gpu->cur_vtx_ct = 0;
                }
                break;
            }
            case POLY_TRI_STRIP: {
                gpu->cur_poly_vtxs[2] = v;
                gpu->tri_orient = !gpu->tri_orient;
                gpu->cur_vtx_ct++;
                if (gpu->cur_vtx_ct == 3) {
                    if (add_poly(gpu, 3, false)) {
                        if (gpu->tri_orient) {
                            gpu->cur_poly_strip[0] = gpu->cur_poly_strip[2];
                        } else {
                            gpu->cur_poly_strip[1] = gpu->cur_poly_strip[2];
                        }
                    } else {
                        if (gpu->tri_orient) {
                            gpu->cur_poly_vtxs[0] = gpu->cur_poly_vtxs[2];
                        } else {
                            gpu->cur_poly_vtxs[1] = gpu->cur_poly_vtxs[2];
                        }
                        gpu->cur_vtx_ct = 2;
                    }
                } else {
                    if (add_poly(gpu, 3, true)) {
                        if (gpu->tri_orient) {
                            gpu->cur_poly_strip[0] = gpu->cur_poly_strip[2];
                        } else {
                            gpu->cur_poly_strip[1] = gpu->cur_poly_strip[2];
                        }
                    } else {
                        if (gpu->tri_orient) {
                            gpu->cur_poly_vtxs[0] = gpu->cur_poly_vtxs[2];
                            gpu->cur_poly_vtxs[1] = *gpu->cur_poly_strip[1];
                        } else {
                            gpu->cur_poly_vtxs[1] = gpu->cur_poly_vtxs[2];
                            gpu->cur_poly_vtxs[0] = *gpu->cur_poly_strip[0];
                        }
                        gpu->cur_vtx_ct = 2;
                    }
                }
                break;
            }
            case POLY_QUAD_STRIP: {
                if (++gpu->cur_vtx_ct & 1) {
                    gpu->cur_poly_vtxs[3] = v;
                } else {
                    gpu->cur_poly_vtxs[2] = v;
                    if (add_poly(gpu, 4, gpu->cur_vtx_ct > 4)) {
                        gpu->cur_poly_strip[0] = gpu->cur_poly_strip[3];
                        gpu->cur_poly_strip[1] = gpu->cur_poly_strip[2];
                    } else {
                        gpu->cur_poly_vtxs[0] = gpu->cur_poly_vtxs[3];
                        gpu->cur_poly_vtxs[1] = gpu->cur_poly_vtxs[2];
                        gpu->cur_vtx_ct = 2;
                    }
                }
                break;
            }
        }
    }
}

void normalize_vtxs(GPU* gpu) {
    for (int i = 0; i < gpu->n_verts; i++) {
        float w = gpu->vertexram[i].v.p[3];
        gpu->vertexram[i].v.p[3] = 1 / w;
        gpu->vertexram[i].v.p[0] /= w;
        gpu->vertexram[i].v.p[1] /= w;
        gpu->vertexram[i].v.p[2] /= w;
        gpu->vertexram[i].vt.p[0] /= w;
        gpu->vertexram[i].vt.p[1] /= w;
        gpu->vertexram[i].r /= w;
        gpu->vertexram[i].g /= w;
        gpu->vertexram[i].b /= w;
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
            gpu->mtx_dirty = true;
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
            gpu->mtx_dirty = true;
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
            gpu->mtx_dirty = true;
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
            gpu->mtx_dirty = true;
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
            gpu->mtx_dirty = true;
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
            gpu->mtx_dirty = true;
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
            gpu->mtx_dirty = true;
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
            gpu->mtx_dirty = true;
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
            gpu->mtx_dirty = true;
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
            gpu->mtx_dirty = true;
            break;
        }
        case COLOR: {
            u16 color = gpu->param_fifo[0];
            gpu->cur_vtx.r = color & 0x1f;
            gpu->cur_vtx.g = (color >> 5) & 0x1f;
            gpu->cur_vtx.b = (color >> 10) & 0x1f;
            break;
        }
        case NORMAL: {
            vec4 normal;
            normal.p[0] = ((s32) (gpu->param_fifo[0] & 0x3ff) << 22) /
                          (float) (u32) (1 << 31);
            normal.p[1] = ((s32) (gpu->param_fifo[0] & (0x3ff << 10)) << 12) /
                          (float) (u32) (1 << 31);
            normal.p[2] = ((s32) (gpu->param_fifo[0] & (0x3ff << 20)) << 2) /
                          (float) (u32) (1 << 31);
            normal.p[3] = 0;

            if (gpu->cur_texparam.transform == TEXTF_NORMAL) {
                gpu->cur_vtx.vt = normal;
                vecmul(&gpu->texmtx, &gpu->cur_vtx.vt);
                gpu->cur_vtx.vt.p[0] /= 16;
                gpu->cur_vtx.vt.p[1] /= 16;
                gpu->cur_vtx.vt.p[0] += gpu->cur_texcoord.p[0];
                gpu->cur_vtx.vt.p[1] += gpu->cur_texcoord.p[1];
            }

            vecmul(&gpu->vecmtx, &normal);

            gpu->cur_vtx.r = gpu->cur_mtl1.emi_r;
            gpu->cur_vtx.g = gpu->cur_mtl1.emi_g;
            gpu->cur_vtx.b = gpu->cur_mtl1.emi_b;
            for (int l = 0; l < 4; l++) {
                if (!(gpu->cur_attr.light_enable & (1 << l))) continue;
                float dp = normal.p[0] * gpu->lightvec[l].p[0] +
                           normal.p[1] * gpu->lightvec[l].p[1] +
                           normal.p[2] * gpu->lightvec[l].p[2];
                dp = -dp;
                if (dp < 0) dp = 0;
                if (dp > 1) dp = 1;
                float sh = normal.p[0] * gpu->halfvec[l].p[0] +
                           normal.p[1] * gpu->halfvec[l].p[1] +
                           normal.p[2] * gpu->halfvec[l].p[2];
                if (sh < 0) sh = 0;
                if (sh > 1) sh = 1;
                if (gpu->cur_mtl1.shininess)
                    sh = gpu->shininess[(int) (sh * 255)] / (float) 256;

                u16 lr = gpu->lightcol[l] & 0x1f;
                u16 lg = (gpu->lightcol[l] >> 5) & 0x1f;
                u16 lb = (gpu->lightcol[l] >> 10) & 0x1f;

                gpu->cur_vtx.r +=
                    (gpu->cur_mtl0.dif_r * lr * dp +
                     gpu->cur_mtl1.spe_r * lr * sh + gpu->cur_mtl0.amb_r * lr) /
                    32;
                gpu->cur_vtx.g +=
                    (gpu->cur_mtl0.dif_g * lg * dp +
                     gpu->cur_mtl1.spe_g * lg * sh + gpu->cur_mtl0.amb_g * lg) /
                    32;
                gpu->cur_vtx.b +=
                    (gpu->cur_mtl0.dif_b * lb * dp +
                     gpu->cur_mtl1.spe_b * lb * sh + gpu->cur_mtl0.amb_b * lb) /
                    32;
            }

            if (gpu->cur_vtx.r > 31) gpu->cur_vtx.r = 31;
            if (gpu->cur_vtx.g > 31) gpu->cur_vtx.g = 31;
            if (gpu->cur_vtx.b > 31) gpu->cur_vtx.b = 31;

            break;
        }
        case TEXCOORD:
            gpu->cur_texcoord.p[0] =
                ((s32) (gpu->param_fifo[0] & 0xffff) << 16) / (float) (1 << 20);
            gpu->cur_texcoord.p[1] =
                (s32) (gpu->param_fifo[0] & 0xffff0000) / (float) (1 << 20);
            gpu->cur_vtx.vt = gpu->cur_texcoord;
            if (gpu->cur_texparam.transform == TEXTF_TEXCOORD) {
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
            if (gpu->cur_mtl0.vtx_color) {
                gpu->cur_vtx.r = gpu->cur_mtl0.dif_r;
                gpu->cur_vtx.g = gpu->cur_mtl0.dif_g;
                gpu->cur_vtx.b = gpu->cur_mtl0.dif_b;
            }
            break;
        case SPE_EMI:
            gpu->cur_mtl1.w = gpu->param_fifo[0];
            break;
        case LIGHT_VECTOR: {
            int l = gpu->param_fifo[0] >> 30;
            gpu->lightvec[l].p[0] = ((s32) (gpu->param_fifo[0] & 0x3ff) << 22) /
                                    (float) (u32) (1 << 31);
            gpu->lightvec[l].p[1] =
                ((s32) (gpu->param_fifo[0] & (0x3ff << 10)) << 12) /
                (float) (u32) (1 << 31);
            gpu->lightvec[l].p[2] =
                ((s32) (gpu->param_fifo[0] & (0x3ff << 20)) << 2) /
                (float) (u32) (1 << 31);
            gpu->lightvec[l].p[3] = 0;
            vecmul(&gpu->vecmtx, &gpu->lightvec[l]);
            gpu->halfvec[l].p[0] = gpu->lightvec[l].p[0] / 2;
            gpu->halfvec[l].p[1] = gpu->lightvec[l].p[1] / 2;
            gpu->halfvec[l].p[2] = (gpu->lightvec[l].p[2] + 1) / 2;

            break;
        }
        case LIGHT_COLOR: {
            int l = gpu->param_fifo[0] >> 30;
            gpu->lightcol[l] = gpu->param_fifo[0];
            break;
        }
        case SHININESS:
            memcpy(gpu->shininess, gpu->param_fifo, sizeof gpu->shininess);
            break;
        case BEGIN_VTXS:
            gpu->cur_vtx_ct = 0;
            gpu->tri_orient = false;
            gpu->poly_mode = gpu->param_fifo[0] & 3;
            break;
        case END_VTXS:
            break;
        case SWAP_BUFFERS: {
            gpu->blocked = true;
            gpu->w_buffer = gpu->param_fifo[0] & 2;
            gpu->autosort = !(gpu->param_fifo[0] & 1);

            if (gpu->drawing || gpu->master->io7.vcount >= NDS_SCREEN_H) {
                gpu->pending_swapbuffers = true;
                break;
            }
            swap_buffers(gpu);
            break;
        }
        case VIEWPORT: {
            int x0, y0, x1, y1;
            x0 = gpu->param_fifo[0] & 0xff;
            y0 = (gpu->param_fifo[0] >> 8) & 0xff;
            x1 = (gpu->param_fifo[0] >> 0x10) & 0xff;
            y1 = (gpu->param_fifo[0] >> 0x18) & 0xff;
            gpu->view_x = x0;
            gpu->view_y = y0;
            gpu->view_w = x1 - x0 + 1;
            gpu->view_h = y1 - y0 + 1;
            break;
        }
        case BOX_TEST: {
            update_mtxs(gpu);
            vec4 p;
            p.p[0] =
                ((s32) (gpu->param_fifo[0] & 0xffff) << 16) / (float) (1 << 28);
            p.p[1] =
                (s32) (gpu->param_fifo[0] & 0xffff0000) / (float) (1 << 28);
            p.p[2] =
                ((s32) (gpu->param_fifo[1] & 0xffff) << 16) / (float) (1 << 28);
            p.p[3] = 1;
            float w =
                (s32) (gpu->param_fifo[1] & 0xffff0000) / (float) (1 << 28);
            float h =
                ((s32) (gpu->param_fifo[2] & 0xffff) << 16) / (float) (1 << 28);
            float d =
                (s32) (gpu->param_fifo[2] & 0xffff0000) / (float) (1 << 28);
            vec4 box[8];
            for (int i = 0; i < 8; i++) {
                box[i] = p;
                if (i & 1) box[i].p[0] += w;
                if (i & 2) box[i].p[1] += h;
                if (i & 4) box[i].p[2] += d;
                vecmul(&gpu->clipmtx, &box[i]);
            }

            vertex face[MAX_POLY_N];

            static const int box_faces[6][4] = {{0, 1, 3, 2}, {0, 2, 6, 4},
                                                {0, 4, 5, 1}, {7, 6, 4, 5},
                                                {7, 5, 1, 3}, {7, 3, 2, 6}};
            gpu->master->io9.gxstat.boxtest = 0;
            for (int i = 0; i < 6; i++) {
                face[0].v = box[box_faces[i][0]];
                face[1].v = box[box_faces[i][1]];
                face[2].v = box[box_faces[i][2]];
                face[3].v = box[box_faces[i][3]];
                if (clip_poly(face, 4)) {
                    gpu->master->io9.gxstat.boxtest = 1;
                    break;
                }
            }
            break;
        }
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
        case VEC_TEST: {
            vec4 v;
            v.p[0] = ((s32) (gpu->param_fifo[0] & 0x3ff) << 22) /
                     (float) (u32) (1 << 31);
            v.p[1] = ((s32) (gpu->param_fifo[0] & (0x3ff << 10)) << 12) /
                     (float) (u32) (1 << 31);
            v.p[2] = ((s32) (gpu->param_fifo[0] & (0x3ff << 20)) << 2) /
                     (float) (u32) (1 << 31);
            v.p[3] = 0;
            vecmul(&gpu->vecmtx, &v);
            gpu->master->io9.vec_result[0] = v.p[0] * (1 << 12);
            gpu->master->io9.vec_result[1] = v.p[1] * (1 << 12);
            gpu->master->io9.vec_result[2] = v.p[2] * (1 << 12);
        }
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

void swap_buffers(GPU* gpu) {
    normalize_vtxs(gpu);

    void* tmp = gpu->vertexram;
    gpu->vertexram = gpu->vertexram_rendering;
    gpu->vertexram_rendering = tmp;
    tmp = gpu->polygonram;
    gpu->polygonram = gpu->polygonram_rendering;
    gpu->polygonram_rendering = tmp;
    gpu->n_polys_rendering = gpu->n_polys;
    gpu->n_verts = 0;
    gpu->n_polys = 0;
    gpu->master->io9.ram_count.w = 0;

    gpu->drawing = true;

    pthread_cond_signal(&gpu_cond);
    pthread_mutex_unlock(&gpu_mutex);
}

#define VTX_SCX(_v) (((_v).v.p[0] + 1) * gpu->view_w / 2 + gpu->view_x)
#define VTX_SCY(_v) ((1 - (_v).v.p[1]) * gpu->view_h / 2 + gpu->view_y)

void render_line(GPU* gpu, vertex* v0, vertex* v1) {
    int x0 = VTX_SCX(*v0);
    int y0 = VTX_SCY(*v0);
    int x1 = VTX_SCX(*v1);
    int y1 = VTX_SCY(*v1);

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
            gpu->screen_back[y][sx] = 0x801f;
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
            gpu->screen_back[sy][x] = 0x801f;
        }
    }
}

void render_polygon_wireframe(GPU* gpu, poly* p) {
    for (int i = 0; i < p->n; i++) {
        int next = (i + 1 == p->n) ? 0 : i + 1;
        render_line(gpu, p->p[i], p->p[next]);
    }
}

void render_line_attrs(GPU* gpu, vertex* v0, vertex* v1,
                       struct interp_attrs* left, struct interp_attrs* right) {

    int x0 = VTX_SCX(*v0);
    int y0 = VTX_SCY(*v0);
    int x1 = VTX_SCX(*v1);
    int y1 = VTX_SCY(*v1);

    struct interp_attrs i0, i1;

    i0.z = v0->v.p[2];
    i0.w = v0->v.p[3];
    i0.s = v0->vt.p[0];
    i0.t = v0->vt.p[1];
    i0.r = v0->r;
    i0.g = v0->g;
    i0.b = v0->b;

    i1.z = v1->v.p[2];
    i1.w = v1->v.p[3];
    i1.s = v1->vt.p[0];
    i1.t = v1->vt.p[1];
    i1.r = v1->r;
    i1.g = v1->g;
    i1.b = v1->b;

    float m = (float) (y1 - y0) / (x1 - x0);
    if (fabsf(m) > 1) {
        m = 1 / m;
        if (y0 > y1) {
            int ti = y0;
            y0 = y1;
            y1 = ti;
            x0 = x1;
            struct interp_attrs it = i0;
            i0 = i1;
            i1 = it;
        }
        int h = y1 - y0;

        float x = x0;

        struct interp_attrs di;
        di.z = (i1.z - i0.z) / h;
        di.w = (i1.w - i0.w) / h;
        di.s = (i1.s - i0.s) / h;
        di.t = (i1.t - i0.t) / h;
        di.r = (i1.r - i0.r) / h;
        di.g = (i1.g - i0.g) / h;
        di.b = (i1.b - i0.b) / h;

        for (int y = y0; y <= y1; y++, x += m) {
            int sx = x, sy = y;
            if (sy < 0 || sy >= NDS_SCREEN_H) continue;
            if (sx < 0) sx = 0;
            if (sx >= NDS_SCREEN_W) sx = NDS_SCREEN_W - 1;
            if (sx <= left[sy].x) {
                left[sy] = i0;
                left[sy].x = sx;
            }
            if (sx >= right[sy].x) {
                right[sy] = i0;
                right[sy].x = sx;
            }
            i0.z += di.z;
            i0.w += di.w;
            i0.s += di.s;
            i0.t += di.t;
            i0.r += di.r;
            i0.g += di.g;
            i0.b += di.b;
        }
    } else {
        if (x0 > x1) {
            int ti = x0;
            x0 = x1;
            x1 = ti;
            y0 = y1;
            struct interp_attrs it = i0;
            i0 = i1;
            i1 = it;
        }
        int h = x1 - x0;

        float y = y0;

        struct interp_attrs di;
        di.z = (i1.z - i0.z) / h;
        di.w = (i1.w - i0.w) / h;
        di.s = (i1.s - i0.s) / h;
        di.t = (i1.t - i0.t) / h;
        di.r = (i1.r - i0.r) / h;
        di.g = (i1.g - i0.g) / h;
        di.b = (i1.b - i0.b) / h;

        for (int x = x0; x <= x1; x++, y += m) {
            int sx = x, sy = y;
            if (sy < 0 || sy >= NDS_SCREEN_H) continue;
            if (sx < 0) sx = 0;
            if (sx >= NDS_SCREEN_W) sx = NDS_SCREEN_W - 1;
            if (sx <= left[sy].x) {
                left[sy] = i0;
                left[sy].x = sx;
            }
            if (sx >= right[sy].x) {
                right[sy] = i0;
                right[sy].x = sx;
            }
            i0.z += di.z;
            i0.w += di.w;
            i0.s += di.s;
            i0.t += di.t;
            i0.r += di.r;
            i0.g += di.g;
            i0.b += di.b;
        }
    }
}

void render_polygon(GPU* gpu, poly* p) {

    if (p->attr.alpha == 0) {
        render_polygon_wireframe(gpu, p);
        return;
    }

    int yMin = NDS_SCREEN_H;
    int yMax = -1;
    for (int i = 0; i < p->n; i++) {
        int y = VTX_SCY(*p->p[i]);
        if (y > yMax) yMax = y;
        if (y < yMin) yMin = y;
    }
    if (yMin < 0) yMin = 0;
    if (yMax > NDS_SCREEN_H) yMax = NDS_SCREEN_H;

    struct interp_attrs left[NDS_SCREEN_H], right[NDS_SCREEN_H];
    for (int y = yMin; y < yMax; y++) {
        left[y].x = NDS_SCREEN_W;
        right[y].x = -1;
    }

    for (int i = 0; i < p->n; i++) {
        int next = (i + 1 == p->n) ? 0 : i + 1;
        render_line_attrs(gpu, p->p[i], p->p[next], left, right);
    }

    u32 base = p->texparam.offset << 3;
    u32 s_shift = p->texparam.s_size + 3;
    u32 t_shift = p->texparam.t_size + 3;
    int format = p->texparam.format;

    u32 palbase = p->pltt_base << 3;
    if (format == TEX_2BPP) palbase >>= 1;

    for (int y = yMin; y < yMax; y++) {
        int h = right[y].x - left[y].x + 1;

        struct interp_attrs i = left[y];
        struct interp_attrs di;
        di.z = (right[y].z - i.z) / h;
        di.w = (right[y].w - i.w) / h;
        di.s = (right[y].s - i.s) / h;
        di.t = (right[y].t - i.t) / h;
        di.r = (right[y].r - i.r) / h;
        di.g = (right[y].g - i.g) / h;
        di.b = (right[y].b - i.b) / h;
        for (int x = left[y].x; x <= right[y].x; x++, i.z += di.z, i.w += di.w,
                 i.s += di.s, i.t += di.t, i.r += di.r, i.g += di.g,
                 i.b += di.b) {
            bool depth_test;
            if (p->attr.depth_test) {
                if (gpu->w_buffer)
                    depth_test =
                        fabsf(1 / i.w - gpu->depth_buf[y][x]) <= 0.125f;
                else depth_test = fabsf(i.z - gpu->depth_buf[y][x]) <= 0.125f;
            } else {
                if (gpu->w_buffer) depth_test = 1 / i.w < gpu->depth_buf[y][x];
                else depth_test = i.z < gpu->depth_buf[y][x];
            }
            if (!depth_test) {
                if (!p->attr.id && p->attr.mode == POLYMODE_SHADOW) {
                    gpu->attr_buf[y][x].stencil = 1;
                }
                continue;
            }

            u16 color = 0xffff;
            u8 alpha = 31;
            if (gpu->master->io9.disp3dcnt.texture && p->texparam.format) {
                s32 ss = i.s / i.w;
                s32 tt = i.t / i.w;
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

                switch (format) {
                    case TEX_2BPP: {
                        u32 addr = base + (ofs >> 2);
                        u8 col_ind = gpu->texram[addr >> 17][addr & 0x1ffff];
                        col_ind >>= (ofs & 3) << 1;
                        col_ind &= 3;
                        if (!col_ind && p->texparam.color0) alpha = 0;
                        else {
                            u32 paladdr = palbase + col_ind;
                            color =
                                gpu->texpal[paladdr >> 13][paladdr & 0x1fff];
                        }
                        break;
                    }
                    case TEX_4BPP: {
                        u32 addr = base + (ofs >> 1);
                        u8 col_ind = gpu->texram[addr >> 17][addr & 0x1ffff];
                        col_ind >>= (ofs & 1) << 2;
                        col_ind &= 15;
                        if (!col_ind && p->texparam.color0) alpha = 0;
                        else {
                            u32 paladdr = palbase + col_ind;
                            color =
                                gpu->texpal[paladdr >> 13][paladdr & 0x1fff];
                        }
                        break;
                    }
                    case TEX_8BPP: {
                        u32 addr = base + ofs;
                        u8 col_ind = gpu->texram[addr >> 17][addr & 0x1ffff];
                        if (!col_ind && p->texparam.color0) alpha = 0;
                        else {
                            u32 paladdr = palbase + col_ind;
                            color =
                                gpu->texpal[paladdr >> 13][paladdr & 0x1fff];
                        }
                        break;
                    }
                    case TEX_A3I5: {
                        u32 addr = base + ofs;
                        u8 col_ind = gpu->texram[addr >> 17][addr & 0x1ffff];
                        alpha = col_ind >> 5;
                        alpha = alpha << 2 | alpha >> 1;
                        col_ind &= 31;
                        u32 paladdr = palbase + col_ind;
                        color = gpu->texpal[paladdr >> 13][paladdr & 0x1fff];
                        break;
                    }
                    case TEX_A5I3: {
                        u32 addr = base + ofs;
                        u8 col_ind = gpu->texram[addr >> 17][addr & 0x1ffff];
                        alpha = col_ind >> 3;
                        col_ind &= 7;
                        u32 paladdr = palbase + col_ind;
                        color = gpu->texpal[paladdr >> 13][paladdr & 0x1fff];
                        break;
                    }
                    case TEX_COMPRESS: {
                        u32 block_ofs =
                            ((tt >> 2) << (s_shift - 2)) + (ss >> 2);
                        u32 block_addr = base + (block_ofs << 2);
                        u32 row_addr = block_addr + (tt & 3);
                        u8 ind =
                            gpu->texram[row_addr >> 17][row_addr & 0x1ffff];
                        ind >>= (ss & 3) << 1;
                        ind &= 3;
                        u16 palmode =
                            *(u16*) &gpu
                                 ->texram[1][(block_addr >> 18 << 16) +
                                             ((block_addr >> 1) & 0xffff)];
                        u32 paladdr = palbase + ((palmode & 0x3fff) << 1);
                        palmode >>= 14;
                        if (palmode < 2 && ind == 3) alpha = 0;
                        else if (ind < 2 || !(palmode & 1)) {
                            paladdr += ind;
                            color =
                                gpu->texpal[paladdr >> 13][paladdr & 0x1fff];
                        } else {
                            u16 color0 =
                                gpu->texpal[paladdr >> 13][paladdr & 0x1fff];
                            paladdr++;
                            u16 color1 =
                                gpu->texpal[paladdr >> 13][paladdr & 0x1fff];
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
                        color =
                            *(u16*) &gpu->texram[addr >> 17][addr & 0x1ffff];
                        alpha = (color >> 15) ? 31 : 0;
                        break;
                    }
                }
            }

            if (alpha == 0) continue;

            u16 tr = color & 0x1f;
            u16 tg = color >> 5 & 0x1f;
            u16 tb = color >> 10 & 0x1f;

            u16 r = 0, g = 0, b = 0, a = 0;
            switch (p->attr.mode) {
                case POLYMODE_MOD:
                    r = ((i.r / i.w + 1) * (tr + 1) - 1) / 32;
                    g = ((i.g / i.w + 1) * (tg + 1) - 1) / 32;
                    b = ((i.b / i.w + 1) * (tb + 1) - 1) / 32;
                    a = ((p->attr.alpha + 1) * (alpha + 1) - 1) / 32;
                    break;
                case POLYMODE_DECAL:
                    r = (tr * alpha) + (i.r / i.w * (31 - alpha)) / 32;
                    g = (tg * alpha) + (i.g / i.w * (31 - alpha)) / 32;
                    b = (tb * alpha) + (i.b / i.w * (31 - alpha)) / 32;
                    a = p->attr.alpha;
                    break;
                case POLYMODE_TOON: {
                    u16 tooncolor =
                        gpu->master->io9.toon_table[(int) (i.r / i.w)];
                    u16 shr = tooncolor & 0x1f;
                    u16 shg = tooncolor >> 5 & 0x1f;
                    u16 shb = tooncolor >> 10 & 0x1f;
                    r = ((shr + 1) * (tr + 1) - 1) / 32;
                    g = ((shg + 1) * (tg + 1) - 1) / 32;
                    b = ((shb + 1) * (tb + 1) - 1) / 32;
                    a = ((p->attr.alpha + 1) * (alpha + 1) - 1) / 32;
                    if (gpu->master->io9.disp3dcnt.shading_mode) {
                        r += shr;
                        if (r > 31) r = 31;
                        g += shg;
                        if (g > 31) g = 31;
                        b += shb;
                        if (b > 31) b = 31;
                    }
                    break;
                    case POLYMODE_SHADOW:
                        if (p->attr.id) {
                            if (!gpu->attr_buf[y][x].stencil) continue;
                            gpu->attr_buf[y][x].stencil = 0;
                            if (gpu->polyid_buf[y][x] == p->attr.id) continue;
                            if (gpu->master->io9.disp3dcnt.texture &&
                                p->texparam.format) {
                                r = (tr * alpha) +
                                    (i.r / i.w * (31 - alpha)) / 32;
                                g = (tg * alpha) +
                                    (i.g / i.w * (31 - alpha)) / 32;
                                b = (tb * alpha) +
                                    (i.b / i.w * (31 - alpha)) / 32;
                            } else {
                                r = i.r / i.w;
                                g = i.g / i.w;
                                b = i.b / i.w;
                            }
                            a = p->attr.alpha;
                        } else {
                            continue;
                        }
                        break;
                }
            }

            if (a == 31 || p->attr.depth_transparent) {
                if (gpu->w_buffer) gpu->depth_buf[y][x] = 1 / i.w;
                else gpu->depth_buf[y][x] = i.z;
            }

            if (gpu->master->io9.disp3dcnt.alpha_blending && a < 31 &&
                (gpu->screen[y][x] & (1 << 15))) {
                if (gpu->attr_buf[y][x].blend &&
                    gpu->polyid_buf[y][x] == p->attr.id)
                    continue;
                u16 sr = gpu->screen_back[y][x] & 0x1f;
                u16 sg = gpu->screen_back[y][x] >> 5 & 0x1f;
                u16 sb = gpu->screen_back[y][x] >> 10 & 0x1f;
                r = (r * (a + 1) + sr * (31 - a)) / 32;
                g = (g * (a + 1) + sg * (31 - a)) / 32;
                b = (b * (a + 1) + sb * (31 - a)) / 32;
                gpu->attr_buf[y][x].blend = 1;
                gpu->attr_buf[y][x].fog &= p->attr.fog;
            } else {
                gpu->attr_buf[y][x].fog = p->attr.fog;
                if ((y == yMin || y == (yMax - 1) || x == left[y].x ||
                     x == right[y].x ||
                     (left[y - 1].x < x && x < left[y + 1].x) ||
                     (left[y + 1].x < x && x < left[y - 1].x) ||
                     (right[y - 1].x < x && x < right[y + 1].x) ||
                     (right[y + 1].x < x && x < right[y - 1].x))) {
                    gpu->attr_buf[y][x].edge = 1;
                }
            }

            gpu->polyid_buf[y][x] = p->attr.id;
            gpu->screen_back[y][x] = r | g << 5 | b << 10 | 1 << 15;
        }
    }
}

#define IS_SEMITRANS(p)                                                        \
    ((p).attr.alpha < 31 || (p).texparam.format == TEX_A3I5 ||                 \
     (p).texparam.format == TEX_A5I3)

void gpu_render(GPU* gpu) {
    if (gpu->master->io9.disp3dcnt.rearplane_mode) {
        for (int y = 0; y < NDS_SCREEN_H; y++) {
            for (int x = 0; x < NDS_SCREEN_W; x++) {
                gpu->screen_back[y][x] =
                    *(u16*) &gpu->texram[2][(y * NDS_SCREEN_W + x) << 1];
                float depth =
                    (*(u16*) &gpu->texram[3][(y * NDS_SCREEN_W + x) << 1] &
                     0x7fff) /
                    (float) (1 << 12);
                if (gpu->w_buffer) depth *= 0x200;
                gpu->depth_buf[y][x] = depth;
                gpu->polyid_buf[y][x] = gpu->master->io9.clear_color.id;
                gpu->attr_buf[y][x].b = 0;
                gpu->attr_buf[y][x].fog = gpu->master->io9.clear_color.fog;
            }
        }
    } else {
        u16 clear_color = gpu->master->io9.clear_color.color;
        if (gpu->master->io9.clear_color.alpha) clear_color |= 0x8000;
        float clear_depth =
            (gpu->master->io9.clear_depth & 0x7fff) / (float) (1 << 12);
        if (gpu->w_buffer) clear_depth *= 0x200;
        for (int y = 0; y < NDS_SCREEN_H; y++) {
            for (int x = 0; x < NDS_SCREEN_W; x++) {
                gpu->screen_back[y][x] = clear_color;
                gpu->depth_buf[y][x] = clear_depth;
                gpu->polyid_buf[y][x] = gpu->master->io9.clear_color.id;
                gpu->attr_buf[y][x].b = 0;
                gpu->attr_buf[y][x].fog = gpu->master->io9.clear_color.fog;
            }
        }
    }
    if (ntremu.wireframe) {
        for (int i = 0; i < gpu->n_polys_rendering; i++) {
            render_polygon_wireframe(gpu, &gpu->polygonram_rendering[i]);
        }
    } else {
        for (int i = 0; i < gpu->n_polys_rendering; i++) {
            if (!IS_SEMITRANS(gpu->polygonram_rendering[i]))
                render_polygon(gpu, &gpu->polygonram_rendering[i]);
        }
        for (int i = 0; i < gpu->n_polys_rendering; i++) {
            if (IS_SEMITRANS(gpu->polygonram_rendering[i]))
                render_polygon(gpu, &gpu->polygonram_rendering[i]);
        }
    }

    if (gpu->master->io9.disp3dcnt.edge_marking ||
        gpu->master->io9.disp3dcnt.fog_enable) {
        float fog_depth =
            (gpu->master->io9.fog_offset & 0x7fff) / (float) (1 << 12);
        if (gpu->w_buffer) fog_depth *= 0x200;
        float fog_step =
            (0x400 >> gpu->master->io9.disp3dcnt.fog_shift) / (float) (1 << 12);
        if (gpu->w_buffer) fog_step *= 0x200;
        for (int y = 0; y < NDS_SCREEN_H; y++) {
            for (int x = 0; x < NDS_SCREEN_W; x++) {
                if (gpu->attr_buf[y][x].edge &&
                    gpu->master->io9.disp3dcnt.edge_marking) {
                    if ((x > 0 &&
                         gpu->polyid_buf[y][x] != gpu->polyid_buf[y][x - 1] &&
                         gpu->depth_buf[y][x] < gpu->depth_buf[y][x - 1]) ||
                        (x < NDS_SCREEN_W - 1 &&
                         gpu->polyid_buf[y][x] != gpu->polyid_buf[y][x + 1] &&
                         gpu->depth_buf[y][x] < gpu->depth_buf[y][x + 1]) ||
                        (y > 0 &&
                         gpu->polyid_buf[y][x] != gpu->polyid_buf[y - 1][x] &&
                         gpu->depth_buf[y][x] < gpu->depth_buf[y - 1][x]) ||
                        (y < NDS_SCREEN_H - 1 &&
                         gpu->polyid_buf[y][x] != gpu->polyid_buf[y + 1][x] &&
                         gpu->depth_buf[y][x] < gpu->depth_buf[y + 1][x])) {
                        if (gpu->master->io9.disp3dcnt.anti_aliasing) {
                            u16 sr = gpu->screen_back[y][x] & 0x1f;
                            u16 sg = gpu->screen_back[y][x] >> 5 & 0x1f;
                            u16 sb = gpu->screen_back[y][x] >> 10 & 0x1f;
                            u16 edgec =
                                gpu->master->io9
                                    .edge_color[gpu->polyid_buf[y][x] >> 3];
                            u16 r = edgec & 0x1f;
                            u16 g = edgec >> 5 & 0x1f;
                            u16 b = edgec >> 10 & 0x1f;
                            r = (r + sr) / 2;
                            g = (g + sg) / 2;
                            b = (b + sb) / 2;
                            gpu->screen_back[y][x] =
                                r | g << 5 | b << 10 | 1 << 15;
                        } else {
                            gpu->screen_back[y][x] =
                                1 << 15 |
                                gpu->master->io9
                                    .edge_color[gpu->polyid_buf[y][x] >> 3];
                        }
                    }
                }

                if (gpu->attr_buf[y][x].fog &&
                    gpu->master->io9.disp3dcnt.fog_enable) {
                    float fog_ind_intr =
                        (gpu->depth_buf[y][x] - fog_depth) / fog_step;
                    if (fog_ind_intr < 0) fog_ind_intr = 0;
                    if (fog_ind_intr > 31) fog_ind_intr = 31;
                    int fog_ind = fog_ind_intr;
                    fog_ind_intr -= fog_ind;
                    u8 fog_density =
                        (gpu->master->io9.fog_table[fog_ind] & 0x7f) *
                            (1 - fog_ind_intr) +
                        (gpu->master->io9.fog_table[fog_ind + 1] & 0x7f) *
                            (fog_ind_intr);
                    if (!gpu->master->io9.disp3dcnt.fog_mode) {
                        u16 fogc = gpu->master->io9.fog_color.color;
                        u16 fr = fogc & 0x1f;
                        u16 fg = fogc >> 5 & 0x1f;
                        u16 fb = fogc >> 10 & 0x1f;
                        u16 sr = gpu->screen_back[y][x] & 0x1f;
                        u16 sg = gpu->screen_back[y][x] >> 5 & 0x1f;
                        u16 sb = gpu->screen_back[y][x] >> 10 & 0x1f;
                        u16 r =
                            (fr * fog_density + sr * (128 - fog_density)) / 128;
                        u16 g =
                            (fg * fog_density + sg * (128 - fog_density)) / 128;
                        u16 b =
                            (fb * fog_density + sb * (128 - fog_density)) / 128;
                        gpu->screen_back[y][x] = r | g << 5 | b << 10 | 1 << 15;
                    }
                }
            }
        }
    }
}