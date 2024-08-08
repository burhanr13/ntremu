#include "dma.h"

#include "bus7.h"
#include "bus9.h"
#include "nds.h"

void update_addr(u32* addr, int adcnt, int wsize) {
    switch (adcnt) {
        case DMA_ADCNT_INC:
        case DMA_ADCNT_INR:
            (*addr) += wsize;
            break;
        case DMA_ADCNT_DEC:
            (*addr) -= wsize;
            break;
    }
}

void dma7_enable(DMAController* dmac, int i) {
    dmac->dma[i].sptr = dmac->master->io7.dma[i].sad;
    dmac->dma[i].dptr = dmac->master->io7.dma[i].dad;
    dmac->dma[i].sptr &= ~1;
    dmac->dma[i].dptr &= ~1;

    dmac->dma[i].len = dmac->master->io7.dma[i].cnt.len;
    if (i < 3) {
        dmac->dma[i].len %= 0x4000;
        if (dmac->dma[i].len == 0) dmac->dma[i].len = 0x4000;
    } else dmac->dma[i].len %= 0x10000;

    if (dmac->master->io7.dma[i].cnt.mode == DMA7_IMM) {
        dma7_run(dmac, i);
    }
}

void dma7_activate(DMAController* dmac, int i) {
    if (!dmac->master->io7.dma[i].cnt.enable) return;

    if (dmac->master->io7.dma[i].cnt.dadcnt == DMA_ADCNT_INR) {
        dmac->dma[i].dptr = dmac->master->io7.dma[i].dad;
        dmac->dma[i].dptr &= ~1;
    }
    dmac->dma[i].len = dmac->master->io7.dma[i].cnt.len;
    if (i < 3) {
        dmac->dma[i].len %= 0x4000;
        if (dmac->dma[i].len == 0) dmac->dma[i].len = 0x4000;
    } else dmac->dma[i].len %= 0x10000;

    dma7_run(dmac, i);
}

void dma7_run(DMAController* dmac, int i) {
    if (i > 0) dmac->dma[i].sptr %= 1 << 28;
    else dmac->dma[i].sptr %= 1 << 27;
    if (i < 3) dmac->dma[i].dptr %= 1 << 27;
    else dmac->dma[i].dptr %= 1 << 28;

    if (dmac->master->io7.dma[i].cnt.wsize) {
        do {
            dma7_trans32(dmac, i, dmac->dma[i].dptr, dmac->dma[i].sptr);
            update_addr(&dmac->dma[i].sptr, dmac->master->io7.dma[i].cnt.sadcnt,
                        4);
            update_addr(&dmac->dma[i].dptr, dmac->master->io7.dma[i].cnt.dadcnt,
                        4);
            dmac->master->sched.now += 1;
        } while (--dmac->dma[i].len);
    } else {
        do {
            dma7_trans16(dmac, i, dmac->dma[i].dptr, dmac->dma[i].sptr);
            update_addr(&dmac->dma[i].sptr, dmac->master->io7.dma[i].cnt.sadcnt,
                        2);
            update_addr(&dmac->dma[i].dptr, dmac->master->io7.dma[i].cnt.dadcnt,
                        2);
            dmac->master->sched.now += 1;
        } while (--dmac->dma[i].len);
    }

    if (!dmac->master->io7.dma[i].cnt.repeat) {
        dmac->master->io7.dma[i].cnt.enable = 0;
    }

    if (dmac->master->io7.dma[i].cnt.irq) dmac->master->io7.ifl.dma |= BIT(i);
}

void dma7_trans16(DMAController* dmac, int i, u32 daddr, u32 saddr) {
    u16 data = bus7_read16(dmac->master, saddr);
    bus7_write16(dmac->master, daddr, data);
}

void dma7_trans32(DMAController* dmac, int i, u32 daddr, u32 saddr) {
    u32 data = bus7_read32(dmac->master, saddr);
    bus7_write32(dmac->master, daddr, data);
}

void dma9_enable(DMAController* dmac, int i) {
    dmac->dma[i].sptr = dmac->master->io9.dma[i].sad;
    dmac->dma[i].dptr = dmac->master->io9.dma[i].dad;
    dmac->dma[i].sptr &= ~1;
    dmac->dma[i].dptr &= ~1;

    dmac->dma[i].len = dmac->master->io9.dma[i].cnt.len;

    if (dmac->master->io9.dma[i].cnt.mode == DMA9_IMM ||
        dmac->master->io9.dma[i].cnt.mode == DMA9_GXFIFO) {
        dma9_run(dmac, i);
    }
}

void dma9_activate(DMAController* dmac, int i) {
    if (!dmac->master->io9.dma[i].cnt.enable) return;

    if (dmac->master->io9.dma[i].cnt.dadcnt == DMA_ADCNT_INR) {
        dmac->dma[i].dptr = dmac->master->io9.dma[i].dad;
        dmac->dma[i].dptr &= ~1;
    }
    dmac->dma[i].len = dmac->master->io9.dma[i].cnt.len;

    dma9_run(dmac, i);
}

void dma9_run(DMAController* dmac, int i) {
    dmac->dma[i].sptr %= 1 << 28;
    dmac->dma[i].dptr %= 1 << 28;

    if (dmac->master->io9.dma[i].cnt.wsize) {
        do {
            dma9_trans32(dmac, i, dmac->dma[i].dptr, dmac->dma[i].sptr);
            update_addr(&dmac->dma[i].sptr, dmac->master->io9.dma[i].cnt.sadcnt,
                        4);
            update_addr(&dmac->dma[i].dptr, dmac->master->io9.dma[i].cnt.dadcnt,
                        4);
            dmac->master->sched.now += 1;
        } while (--dmac->dma[i].len);
    } else {
        do {
            dma9_trans16(dmac, i, dmac->dma[i].dptr, dmac->dma[i].sptr);
            update_addr(&dmac->dma[i].sptr, dmac->master->io9.dma[i].cnt.sadcnt,
                        2);
            update_addr(&dmac->dma[i].dptr, dmac->master->io9.dma[i].cnt.dadcnt,
                        2);
            dmac->master->sched.now += 1;
        } while (--dmac->dma[i].len);
    }

    if (!dmac->master->io9.dma[i].cnt.repeat) {
        dmac->master->io9.dma[i].cnt.enable = 0;
    }

    if (dmac->master->io9.dma[i].cnt.irq) dmac->master->io9.ifl.dma |= BIT(i);
}

void dma9_trans16(DMAController* dmac, int i, u32 daddr, u32 saddr) {
    u16 data = bus9_read16(dmac->master, saddr);
    bus9_write16(dmac->master, daddr, data);
}

void dma9_trans32(DMAController* dmac, int i, u32 daddr, u32 saddr) {
    u32 data = bus9_read32(dmac->master, saddr);
    bus9_write32(dmac->master, daddr, data);
}
