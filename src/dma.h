#ifndef _DMA_H
#define _DMA_H

#include "types.h"

enum { DMA_ADCNT_INC, DMA_ADCNT_DEC, DMA_ADCNT_FIX, DMA_ADCNT_INR };

enum { DMA7_IMM = 0, DMA7_VBLANK = 2, DMA7_DSCARD = 4, DMA7_SPEC = 6 };

enum {
    DMA9_IMM,
    DMA9_VBLANK,
    DMA9_HBLANK,
    DMA9_DISPLAY,
    DMA9_MMDISP,
    DMA9_DSCARD,
    DMA9_GBACART,
    DMA9_GXFIFO
};

typedef struct _NDS NDS;

typedef struct {
    NDS* master;

    struct {
        u32 sptr;
        u32 dptr;
        u32 len;
    } dma[4];
} DMAController;

void dma7_enable(DMAController* dmac, int i);
void dma7_activate(DMAController* dmac, int i);

void dma7_run(DMAController* dmac, int i);

void dma7_trans16(DMAController* dmac, int i, u32 daddr, u32 saddr);
void dma7_trans32(DMAController* dmac, int i, u32 daddr, u32 saddr);

void dma9_enable(DMAController* dmac, int i);
void dma9_activate(DMAController* dmac, int i);

void dma9_run(DMAController* dmac, int i);

void dma9_trans16(DMAController* dmac, int i, u32 daddr, u32 saddr);
void dma9_trans32(DMAController* dmac, int i, u32 daddr, u32 saddr);

#endif