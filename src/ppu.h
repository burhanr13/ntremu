#ifndef _PPU_H_
#define _PPU_H_

#include "io.h"
#include "types.h"

#define NDS_SCREEN_W 256
#define NDS_SCREEN_H_ 192
#define DOTS_W 355
#define LINES_H 263

typedef union {
    u16 h;
    struct {
        u16 num : 10;
        u16 hflip : 1;
        u16 vflip : 1;
        u16 palette : 4;
    };
} BgTile;

enum { OBJ_MODE_NORMAL, OBJ_MODE_SEMITRANS, OBJ_MODE_OBJWIN, OBJ_MODE_BITMAP };
enum { OBJ_SHAPE_SQR, OBJ_SHAPE_H_ORZ, OBJ_SHAPE_VERT };

typedef struct {
    union {
        u16 attr0;
        struct {
            u16 y : 8;
            u16 aff : 1;
            u16 disable_double : 1;
            u16 mode : 2;
            u16 mosaic : 1;
            u16 palmode : 1;
            u16 shape : 2;
        };
    };
    union {
        u16 attr1;
        struct {
            u16 x : 9;
            u16 unused : 3;
            u16 hflip : 1;
            u16 vflip : 1;
            u16 size : 2;
        };
        struct {
            u16 _x : 9;
            u16 affparamind : 5;
            u16 _size : 2;
        };
    };
    union {
        u16 attr2;
        struct {
            u16 tilenum : 10;
            u16 priority : 2;
            u16 palette : 4;
        };
    };
    s16 affparam;
} ObjAttr;

enum { WIN0, WIN1, WOUT, WOBJ };
enum { LBG0, LBG1, LBG2, LBG3, LOBJ, LBD, LMAX };

enum { EFF_NONE, EFF_ALPHA, EFF_BINC, EFF_BDEC };

typedef enum { VRAMBGA, VRAMBGB, VRAMOBJA, VRAMOBJB } VRAMRegion;

typedef struct _NDS NDS;

typedef struct {
    NDS* master;

    PPUIO* io;
    u16* pal;
    u16* extPalBg[4];
    u16* extPalObj;
    ObjAttr* oam;
    VRAMRegion bgReg;
    VRAMRegion objReg;

    u16 (*screen)[NDS_SCREEN_W];
    u16 cur_line[NDS_SCREEN_W];
    u16 ly;

    u16 layerlines[LMAX][NDS_SCREEN_W];
    struct {
        u8 priority : 2;
        u8 semitrans : 1;
        u8 mosaic : 1;
        u8 pad : 4;
    } objdotattrs[NDS_SCREEN_W];
    u8 window[NDS_SCREEN_W];

    struct {
        u32 x;
        u32 y;
        u32 mosx;
        u32 mosy;
    } bgaffintr[2];

    u8 bgmos_y;
    u8 bgmos_ct;
    u8 objmos_y;
    u8 objmos_ct;

    bool draw_bg[4];
    bool draw_obj;
    bool in_win[2];
    bool obj_semitrans;
    bool obj_mos;
    bool bg0_3d;

} PPU;

void draw_scanline(PPU* ppu);

void lcd_hdraw(NDS* nds);
void lcd_vblank(NDS* nds);
void lcd_hblank(NDS* nds);

void apply_screenswap(NDS* nds);

#endif