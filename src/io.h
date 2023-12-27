#ifndef IO_H
#define IO_H

#include "types.h"

#define IO_SIZE 0x1070

enum {
    // display control
    DISPCNT = 0x000,
    DISPSTAT = 0x004,
    VCOUNT = 0x006,
    // bg control
    BG0CNT = 0x008,
    BG1CNT = 0x00a,
    BG2CNT = 0x00c,
    BG3CNT = 0x00e,
    // text bg
    BG0HOFS = 0x010,
    BG0VOFS = 0x012,
    BG1HOFS = 0x014,
    BG1VOFS = 0x016,
    BG2HOFS = 0x018,
    BG2VOFS = 0x01a,
    BG3HOFS = 0x01c,
    BG3VOFS = 0x01e,
    // affine bg
    BG2PA = 0x020,
    BG2PB = 0x022,
    BG2PC = 0x024,
    BG2PD = 0x026,
    BG2X = 0x028,
    BG2Y = 0x02c,
    BG3PA = 0x030,
    BG3PB = 0x032,
    BG3PC = 0x034,
    BG3PD = 0x036,
    BG3X = 0x038,
    BG3Y = 0x03c,
    // window control
    WIN0H = 0x040,
    WIN1H = 0x042,
    WIN0V = 0x044,
    WIN1V = 0x046,
    WININ = 0x048,
    WINOUT = 0x04a,
    // special effects control
    MOSAIC = 0x04c,
    BLDCNT = 0x050,
    BLDALPHA = 0x052,
    BLDY = 0x054,

    MASTERBRIGHT = 0x06c,

    // dma control
    DMA0SAD = 0x0b0,
    DMA0DAD = 0x0b4,
    DMA0CNT = 0x0b8,
    DMA1SAD = 0x0bc,
    DMA1DAD = 0x0c0,
    DMA1CNT = 0x0c4,
    DMA2SAD = 0x0c8,
    DMA2DAD = 0x0cc,
    DMA2CNT = 0x0d0,
    DMA3SAD = 0x0d4,
    DMA3DAD = 0x0d8,
    DMA3CNT = 0x0dc,
    DMA0FILL = 0x0e0,
    DMA1FILL = 0x0e4,
    DMA2FILL = 0x0e8,
    DMA3FILL = 0x0ec,

    // timer control
    TM0CNT_L = 0x100,
    TM0CNT_H = 0x102,
    TM1CNT_L = 0x104,
    TM1CNT_H = 0x106,
    TM2CNT_L = 0x108,
    TM2CNT_H = 0x10a,
    TM3CNT_L = 0x10c,
    TM3CNT_H = 0x10e,

    // key control
    KEYINPUT = 0x130,
    KEYCNT = 0x132,

    // ipc
    IPCSYNC = 0x180,
    IPCFIFOCNT = 0x184,
    IPCFIFOSEND = 0x188,
    IPCFIFORECV = 0x100000,

    // system/interrupt control
    EXMEMCNT = 0x204,
    IME = 0x208,
    IE = 0x210,
    IF = 0x214,
    POSTFLG = 0x300,
    HALTCNT = 0x301,

    PPUB_OFF = 0x1000
};

typedef struct _NDS NDS;

typedef struct {
    union {
        u32 w;
        struct {
            u32 bg_mode : 3;
            u32 enable_3d : 1;
            u32 obj_mapmode : 1;
            u32 obj_bm_dim : 1;
            u32 obj_bm_mapmode : 1;
            u32 forced_blank : 1;
            u32 bg_enable : 4;
            u32 obj_enable : 1;
            u32 win_enable : 2;
            u32 winobj_enable : 1;
            u32 disp_mode : 2;
            u32 vram_block : 2;
            u32 obj_boundary : 2;
            u32 obj_bm_boundary : 1;
            u32 hblank_free : 1;
            u32 tile_base : 3;
            u32 tilemap_base : 3;
            u32 bg_extpal : 1;
            u32 obj_extpal : 1;
        };
    } dispcnt;
    u16 dispstat;
    u16 vcount;
    union {
        u16 h;
        struct {
            u16 priority : 2;
            u16 tile_base : 2;
            u16 unused : 2;
            u16 mosaic : 1;
            u16 palmode : 1;
            u16 tilemap_base : 5;
            u16 overflow : 1;
            u16 size : 2;
        };
    } bgcnt[4];
    struct {
        u16 hofs;
        u16 vofs;
    } bgtext[4];
    struct {
        s16 pa;
        s16 pb;
        s16 pc;
        s16 pd;
        s32 x;
        s32 y;
    } bgaff[2];
    union {
        u16 h;
        struct {
            u8 x2;
            u8 x1;
        };
    } winh[2];
    union {
        u16 h;
        struct {
            u8 y2;
            u8 y1;
        };
    } winv[2];
    union {
        struct {
            u16 winin;
            u16 winout;
        };
        struct {
            u8 bg_enable : 4;
            u8 obj_enable : 1;
            u8 effects_enable : 1;
            u8 unused : 2;
        } wincnt[4];
    };
    union {
        u32 w;
        struct {
            u32 bg_h : 4;
            u32 bg_v : 4;
            u32 obj_h : 4;
            u32 obj_v : 4;
        };
    } mosaic;
    union {
        u16 h;
        struct {
            u16 target1 : 6;
            u16 effect : 2;
            u16 target2 : 6;
            u16 unused : 2;
        };
    } bldcnt;
    union {
        u16 h;
        struct {
            u16 eva : 5;
            u16 unused1 : 3;
            u16 evb : 5;
            u16 unused2 : 3;
        };
    } bldalpha;
    union {
        u32 w;
        struct {
            u32 evy : 5;
            u32 unused : 27;
        };
    } bldy;
    u8 gap[MASTERBRIGHT - BLDY - 4];
    u32 masterbright;
} PPUIO;

typedef struct _IO {
    NDS* master;
    union {
        u8 b[IO_SIZE];
        u16 h[IO_SIZE >> 1];
        u32 w[IO_SIZE >> 2];
        struct {
            union {
                PPUIO ppuA;
                struct {
                    u32 dispcntA;
                    union {
                        u16 h;
                        struct {
                            u16 vblank : 1;
                            u16 hblank : 1;
                            u16 vcounteq : 1;
                            u16 vblank_irq : 1;
                            u16 hblank_irq : 1;
                            u16 vcount_irq : 1;
                            u16 unused : 1;
                            u16 lyc_hi : 1;
                            u16 lyc : 8;
                        };
                    } dispstat;
                    u16 vcount;
                    u8 pad_ppuA[MASTERBRIGHT - VCOUNT + 2];
                };
            };
            u8 gap_0xx[DMA0SAD - MASTERBRIGHT - 4];
            struct {
                u32 sad;
                u32 dad;
                union {
                    u32 w;
                    struct {
                        u32 len : 21;
                        u32 dadcnt : 2;
                        u32 sadcnt : 2;
                        u32 repeat : 1;
                        u32 wsize : 1;
                        u32 drq : 1;
                        u32 start : 2;
                        u32 irq : 1;
                        u32 enable : 1;
                    };
                } cnt;
            } dma[4];
            u32 dmafill[4];
            u8 gap_0ex[TM0CNT_L - DMA3FILL - 4];
            struct {
                u16 reload;
                union {
                    u16 h;
                    struct {
                        u16 rate : 2;
                        u16 countup : 1;
                        u16 unused : 3;
                        u16 irq : 1;
                        u16 enable : 1;
                        u16 unused1 : 8;
                    };
                } cnt;
            } tm[4];
            u8 gap11x[KEYINPUT - TM3CNT_H - 2];
            union {
                u16 h;
                struct {
                    u16 a : 1;
                    u16 b : 1;
                    u16 select : 1;
                    u16 start : 1;
                    u16 right : 1;
                    u16 left : 1;
                    u16 up : 1;
                    u16 down : 1;
                    u16 r : 1;
                    u16 l : 1;
                    u16 unused : 6;
                };
                struct {
                    u16 keys : 10;
                    u16 _unused : 6;
                };
            } keyinput;
            union {
                u16 h;
                struct {
                    u16 keys : 10;
                    u16 unused : 4;
                    u16 irq_enable : 1;
                    u16 irq_cond : 1;
                };
            } keycnt;
            u8 gap15x[IPCSYNC - KEYCNT - 2];
            union {
                u32 w;
                struct {
                    u32 in : 4;
                    u32 unused : 4;
                    u32 out : 4;
                    u32 unused2 : 1;
                    u32 irqsend : 1;
                    u32 irq : 1;
                    u32 unused3 : 17;
                };
            } ipcsync;
            union {
                u32 w;
                struct {
                    u32 sendempty : 1;
                    u32 sendfull : 1;
                    u32 send_irq : 1;
                    u32 send_clear : 1;
                    u32 unused : 4;
                    u32 recvempty : 1;
                    u32 recvfull : 1;
                    u32 recv_irq : 1;
                    u32 unused2 : 3;
                    u32 error : 1;
                    u32 fifo_enable : 1;
                    u32 unused3 : 16;
                };
            } ipcfifocnt;
            u32 ipcfifosend;
            u8 gap19x[EXMEMCNT - IPCFIFOSEND - 4];
            union {
                u32 w;
                struct {
                    u32 sram : 2;
                    u32 rom0 : 2;
                    u32 rom0s : 1;
                    u32 rom1 : 2;
                    u32 rom1s : 1;
                    u32 rom2 : 2;
                    u32 rom2s : 1;
                    u32 phi : 2;
                    u32 unused : 1;
                    u32 prefetch : 1;
                    u32 gamepaktype : 1;
                    u32 unused1 : 16;
                };
            } exmemcnt;
            u32 ime;
            u32 unused_20c;
            union {
                u32 w;
                struct {
                    u32 vblank : 1;
                    u32 hblank : 1;
                    u32 vcounteq : 1;
                    u32 timer : 4;
                    u32 serial : 1;
                    u32 dma : 4;
                    u32 keypad : 1;
                    u32 gamepak : 1;
                    u32 unused : 2;
                    u32 ipcsync : 1;
                    u32 ipcsend : 1;
                    u32 ipcrecv : 1;
                    u32 gamecardtrans : 1;
                    u32 gamecard : 1;
                    u32 geofifo : 1;
                    u32 unfold : 1;
                    u32 spi : 1;
                    u32 wifi : 1;
                    u32 unusedhi : 7;
                };
            } ie;
            union {
                u32 w;
                struct {
                    u32 vblank : 1;
                    u32 hblank : 1;
                    u32 vcounteq : 1;
                    u32 timer : 4;
                    u32 serial : 1;
                    u32 dma : 4;
                    u32 keypad : 1;
                    u32 gamepak : 1;
                    u32 unused : 2;
                    u32 ipcsync : 1;
                    u32 ipcsend : 1;
                    u32 ipcrecv : 1;
                    u32 gamecardtrans : 1;
                    u32 gamecard : 1;
                    u32 geofifo : 1;
                    u32 unfold : 1;
                    u32 spi : 1;
                    u32 wifi : 1;
                    u32 unusedhi : 7;
                };
            } ifl;
            u8 unused_2xx[PPUB_OFF - IF - 4];
            PPUIO ppuB;
        };
    };
} IO;

u8 io7_read8(IO* io, u32 addr);
void io7_write8(IO* io, u32 addr, u8 data);
u16 io7_read16(IO* io, u32 addr);
void io7_write16(IO* io, u32 addr, u16 data);
u32 io7_read32(IO* io, u32 addr);
void io7_write32(IO* io, u32 addr, u32 data);

u8 io9_read8(IO* io, u32 addr);
void io9_write8(IO* io, u32 addr, u8 data);
u16 io9_read16(IO* io, u32 addr);
void io9_write16(IO* io, u32 addr, u16 data);
u32 io9_read32(IO* io, u32 addr);
void io9_write32(IO* io, u32 addr, u32 data);

#endif