#include "ppu.h"

#include <stdio.h>
#include <string.h>

#include "io.h"
#include "nds.h"
#include "scheduler.h"

const int SCLAYOUT[4][2][2] = {
    {{0, 0}, {0, 0}}, {{0, 1}, {0, 1}}, {{0, 0}, {1, 1}}, {{0, 1}, {2, 3}}};

// size: sqr, short, long
const int OBJLAYOUT[4][3] = {{8, 8, 16}, {16, 8, 32}, {32, 16, 32}, {64, 32, 64}};

void render_bg_line_text(PPU* ppu, int bg) {
    if (!(ppu->io->dispcnt.bg_enable & (1 << bg))) return;
    ppu->draw_bg[bg] = true;

    u32 map_start =
        ppu->io->dispcnt.tilemap_base * 0x10000 + ppu->io->bgcnt[bg].tilemap_base * 0x800;
    u32 tile_start = ppu->io->dispcnt.tile_base * 0x10000 + ppu->io->bgcnt[bg].tile_base * 0x4000;

    u16 sy;
    if (ppu->io->bgcnt[bg].mosaic) {
        sy = (ppu->bgmos_y + ppu->io->bgtext[bg].vofs) % 512;
    } else {
        sy = (ppu->ly + ppu->io->bgtext[bg].vofs) % 512;
    }
    u16 scy = sy >> 8;
    u16 ty = (sy >> 3) & 0b11111;
    u16 fy = sy & 0b111;
    u16 sx = ppu->io->bgtext[bg].hofs;
    u16 scx = sx >> 8;
    u16 tx = (sx >> 3) & 0b11111;
    u16 fx = sx & 0b111;
    u8 scs[2] = {SCLAYOUT[ppu->io->bgcnt[bg].size][scy & 1][0],
                 SCLAYOUT[ppu->io->bgcnt[bg].size][scy & 1][1]};
    u32 map_addr = map_start + 0x800 * scs[scx & 1] + 32 * 2 * ty + 2 * tx;
    BgTile tile = {vram_read16(ppu->master, ppu->bgReg, map_addr)};
    if (ppu->io->bgcnt[bg].palmode) {
        u32 tile_addr = tile_start + 64 * tile.num;
        u16 tmpfy = fy;
        if (tile.vflip) tmpfy = 7 - fy;
        u64 row = vram_read32(ppu->master, ppu->bgReg, tile_addr + 8 * tmpfy);
        row |= (u64) vram_read32(ppu->master, ppu->bgReg, tile_addr + 8 * tmpfy + 4) << 32;
        if (tile.hflip) {
            row = (row & 0xffffffff00000000) >> 32 | (row & 0x00000000ffffffff) << 32;
            row = (row & 0xffff0000ffff0000) >> 16 | (row & 0x0000ffff0000ffff) << 16;
            row = (row & 0xff00ff00ff00ff00) >> 8 | (row & 0x00ff00ff00ff00ff) << 8;
        }
        row >>= 8 * fx;
        for (int x = 0; x < NDS_SCREEN_W; x++) {
            u8 col_ind = row & 0xff;
            if (col_ind) ppu->layerlines[bg][x] = ppu->pal[col_ind] & ~(1 << 15);
            else ppu->layerlines[bg][x] = 1 << 15;

            row >>= 8;
            fx++;
            if (fx == 8) {
                fx = 0;
                tx++;
                if (tx == 32) {
                    tx = 0;
                    scx++;
                    map_addr = map_start + 0x800 * scs[scx & 1] + 32 * 2 * ty;
                } else {
                    map_addr += 2;
                }
                tile.h = vram_read16(ppu->master, ppu->bgReg, map_addr);
                tile_addr = tile_start + 64 * tile.num;
                tmpfy = fy;
                if (tile.vflip) tmpfy = 7 - fy;
                row = vram_read32(ppu->master, ppu->bgReg, tile_addr + 8 * tmpfy);
                row |= (u64) vram_read32(ppu->master, ppu->bgReg, tile_addr + 8 * tmpfy + 4) << 32;
                if (tile.hflip) {
                    row = (row & 0xffffffff00000000) >> 32 | (row & 0x00000000ffffffff) << 32;
                    row = (row & 0xffff0000ffff0000) >> 16 | (row & 0x0000ffff0000ffff) << 16;
                    row = (row & 0xff00ff00ff00ff00) >> 8 | (row & 0x00ff00ff00ff00ff) << 8;
                }
            }
        }
    } else {
        u32 tile_addr = tile_start + 32 * tile.num;
        u16 tmpfy = fy;
        if (tile.vflip) tmpfy = 7 - fy;
        u32 row = vram_read32(ppu->master, ppu->bgReg, tile_addr + 4 * tmpfy);
        if (tile.hflip) {
            row = (row & 0xffff0000) >> 16 | (row & 0x0000ffff) << 16;
            row = (row & 0xff00ff00) >> 8 | (row & 0x00ff00ff) << 8;
            row = (row & 0xf0f0f0f0) >> 4 | (row & 0x0f0f0f0f) << 4;
        }
        row >>= 4 * fx;
        for (int x = 0; x < NDS_SCREEN_W; x++) {
            u8 col_ind = row & 0xf;
            if (col_ind) {
                col_ind |= tile.palette << 4;
                ppu->layerlines[bg][x] = ppu->pal[col_ind] & ~(1 << 15);
            } else ppu->layerlines[bg][x] = 1 << 15;

            row >>= 4;
            fx++;
            if (fx == 8) {
                fx = 0;
                tx++;
                if (tx == 32) {
                    tx = 0;
                    scx++;
                    map_addr = map_start + 0x800 * scs[scx & 1] + 32 * 2 * ty;
                } else {
                    map_addr += 2;
                }
                tile.h = vram_read16(ppu->master, ppu->bgReg, map_addr);
                tile_addr = tile_start + 32 * tile.num;
                tmpfy = fy;
                if (tile.vflip) tmpfy = 7 - fy;
                row = vram_read32(ppu->master, ppu->bgReg, tile_addr + 4 * tmpfy);
                if (tile.hflip) {
                    row = (row & 0xffff0000) >> 16 | (row & 0x0000ffff) << 16;
                    row = (row & 0xff00ff00) >> 8 | (row & 0x00ff00ff) << 8;
                    row = (row & 0xf0f0f0f0) >> 4 | (row & 0x0f0f0f0f) << 4;
                }
            }
        }
    }
}

void render_bg_line_aff(PPU* ppu, int bg) {
    if (!(ppu->io->dispcnt.bg_enable & (1 << bg))) return;
    ppu->draw_bg[bg] = true;

    u32 map_start =
        ppu->io->dispcnt.tilemap_base * 0x10000 + ppu->io->bgcnt[bg].tilemap_base * 0x800;
    u32 tile_start = ppu->io->dispcnt.tile_base * 0x10000 + ppu->io->bgcnt[bg].tile_base * 0x4000;

    s32 x0, y0;
    if (ppu->io->bgcnt[bg].mosaic) {
        x0 = ppu->bgaffintr[bg - 2].mosx;
        y0 = ppu->bgaffintr[bg - 2].mosy;
    } else {
        x0 = ppu->bgaffintr[bg - 2].x;
        y0 = ppu->bgaffintr[bg - 2].y;
    }

    u16 size = 1 << (7 + ppu->io->bgcnt[bg].size);

    for (int x = 0; x < NDS_SCREEN_W;
         x++, x0 += ppu->io->bgaff[bg - 2].pa, y0 += ppu->io->bgaff[bg - 2].pc) {
        u32 sx = x0 >> 8;
        u32 sy = y0 >> 8;
        if ((sx >= size || sy >= size) && !ppu->io->bgcnt[bg].overflow) {
            ppu->layerlines[bg][x] = 1 << 15;
            continue;
        }

        u8 col_ind = 0;
        sx &= size - 1;
        sy &= size - 1;
        u16 tilex = sx >> 3;
        u16 tiley = sy >> 3;
        u16 finex = sx & 0b111;
        u16 finey = sy & 0b111;
        u8 tile = vram_read8(ppu->master, ppu->bgReg, map_start + tiley * (size >> 3) + tilex);
        col_ind = vram_read8(ppu->master, ppu->bgReg, tile_start + 64 * tile + finey * 8 + finex);

        if (col_ind) ppu->layerlines[bg][x] = ppu->pal[col_ind] & ~(1 << 15);
        else ppu->layerlines[bg][x] = 1 << 15;
    }
}

void render_bg_line_aff_ext(PPU* ppu, int bg) {
    render_bg_line_text(ppu, bg);
}

void render_bgs(PPU* ppu) {
    int mode = ppu->io->dispcnt.bg_mode;
    if (mode != 6) {
        render_bg_line_text(ppu, 0);
        render_bg_line_text(ppu, 1);
        switch (mode) {
            case 0:
                render_bg_line_text(ppu, 2);
                render_bg_line_text(ppu, 3);
                break;
            case 1:
                render_bg_line_text(ppu, 2);
                render_bg_line_aff(ppu, 3);
                break;
            case 2:
                render_bg_line_aff(ppu, 2);
                render_bg_line_aff(ppu, 3);
                break;
            case 3:
                render_bg_line_text(ppu, 2);
                render_bg_line_aff_ext(ppu, 3);
                break;
            case 4:
                render_bg_line_aff(ppu, 2);
                render_bg_line_aff_ext(ppu, 3);
                break;
            case 5:
                render_bg_line_aff_ext(ppu, 2);
                render_bg_line_aff_ext(ppu, 3);
                break;
        }
    }
}

void render_obj_line(PPU* ppu, int i) {
    ObjAttr o = ppu->oam[i];
    u8 w, h;
    switch (o.shape) {
        case OBJ_SHAPE_SQR:
            w = h = OBJLAYOUT[o.size][0];
            break;
        case OBJ_SHAPE_HORZ:
            w = OBJLAYOUT[o.size][2];
            h = OBJLAYOUT[o.size][1];
            break;
        case OBJ_SHAPE_VERT:
            w = OBJLAYOUT[o.size][1];
            h = OBJLAYOUT[o.size][2];
            break;
        default:
            return;
    }
    if (o.disable_double) {
        if (o.aff) {
            w *= 2;
            h *= 2;
        } else return;
    }

    u8 yofs = ppu->ly - (u8) o.y;
    if (yofs >= h) return;

    if (o.mosaic) {
        yofs = ppu->objmos_y - (u8) o.y;
        ppu->obj_mos = true;
    }
    if (o.mode == OBJ_MODE_SEMITRANS) ppu->obj_semitrans = true;

    u32 tile_start =
        o.tilenum * (32 << (ppu->io->dispcnt.obj_mapmode ? ppu->io->dispcnt.obj_boundary : 0));

    if (ppu->io->dispcnt.bg_mode > 2 && tile_start < 0x4000) return;

    if (o.aff) {
        ppu->obj_cycles -= 10 + 2 * w;

        u8 ow = w;
        u8 oh = h;
        if (o.disable_double) {
            ow /= 2;
            oh /= 2;
        }

        s16 pa = ppu->oam[4 * o.affparamind + 0].affparam;
        s16 pb = ppu->oam[4 * o.affparamind + 1].affparam;
        s16 pc = ppu->oam[4 * o.affparamind + 2].affparam;
        s16 pd = ppu->oam[4 * o.affparamind + 3].affparam;

        s32 x0 = pa * (-w / 2) + pb * (yofs - h / 2) + ((ow / 2) << 8);
        s32 y0 = pc * (-w / 2) + pd * (yofs - h / 2) + ((oh / 2) << 8);

        for (int x = 0; x < w; x++, x0 += pa, y0 += pc) {
            u16 sx = (o.x + x) % 512;
            if (sx >= NDS_SCREEN_W) continue;

            u16 ty = y0 >> 11;
            u16 fy = (y0 >> 8) & 0b111;
            u16 tx = x0 >> 11;
            u16 fx = (x0 >> 8) & 0b111;

            u16 col;

            if (ty >= oh / 8 || tx >= ow / 8) {
                col = 1 << 15;
            } else {
                if (o.palmode) {
                    u32 tile_addr =
                        tile_start + ty * 64 * (ppu->io->dispcnt.obj_mapmode ? ow / 8 : 16);
                    tile_addr += 64 * tx + 8 * fy + fx;
                    u8 col_ind = vram_read8(ppu->master, ppu->objReg, tile_addr);
                    if (col_ind) {
                        col = ppu->pal[0x100 + col_ind] & ~(1 << 15);
                    } else col = 1 << 15;
                } else {
                    u32 tile_addr =
                        tile_start + ty * 32 * (ppu->io->dispcnt.obj_mapmode ? ow / 8 : 32);
                    tile_addr += 32 * tx + 4 * fy + fx / 2;
                    u8 col_ind = vram_read8(ppu->master, ppu->objReg, tile_addr);
                    if (fx & 1) col_ind >>= 4;
                    else col_ind &= 0b1111;
                    if (col_ind) {
                        col_ind |= o.palette << 4;
                        col = ppu->pal[0x100 + col_ind] & ~(1 << 15);
                    } else col = 1 << 15;
                }
            }
            if (o.mode == OBJ_MODE_OBJWIN) {
                if (ppu->io->dispcnt.winobj_enable && !(col & (1 << 15))) {
                    ppu->window[sx] = WOBJ;
                }
            } else if (o.priority < ppu->objdotattrs[sx].priority ||
                       (ppu->layerlines[LOBJ][sx] & (1 << 15))) {
                if (!(col & (1 << 15))) {
                    ppu->draw_obj = true;
                    ppu->layerlines[LOBJ][sx] = col;
                    ppu->objdotattrs[sx].semitrans = (o.mode == OBJ_MODE_SEMITRANS) ? 1 : 0;
                    ppu->objdotattrs[sx].mosaic = o.mosaic;
                    ppu->objdotattrs[sx].priority = o.priority;
                }
            }
        }
    } else {
        ppu->obj_cycles -= w;

        u32 tile_addr = tile_start;

        if (o.vflip) yofs = h - 1 - yofs;
        u8 ty = yofs >> 3;
        u8 fy = yofs & 0b111;

        u8 fx = 0;
        if (o.palmode) {
            tile_addr += ty * 64 * (ppu->io->dispcnt.obj_mapmode ? w / 8 : 16);
            u64 row;
            if (o.hflip) {
                tile_addr += 64 * (w / 8 - 1);
                row = vram_read32(ppu->master, ppu->objReg, tile_addr + 8 * fy);
                row |= (u64) vram_read32(ppu->master, ppu->objReg, tile_addr + 8 * fy + 4) << 32;
                row = (row & 0xffffffff00000000) >> 32 | (row & 0x00000000ffffffff) << 32;
                row = (row & 0xffff0000ffff0000) >> 16 | (row & 0x0000ffff0000ffff) << 16;
                row = (row & 0xff00ff00ff00ff00) >> 8 | (row & 0x00ff00ff00ff00ff) << 8;
            } else {
                row = vram_read32(ppu->master, ppu->objReg, tile_addr + 8 * fy);
                row |= (u64) vram_read32(ppu->master, ppu->objReg, tile_addr + 8 * fy + 4) << 32;
            }
            for (int x = 0; x < w; x++) {
                int sx = (o.x + x) % 512;
                if (sx < NDS_SCREEN_W) {
                    u8 col_ind = row & 0xff;
                    if (o.mode == OBJ_MODE_OBJWIN) {
                        if (ppu->io->dispcnt.winobj_enable && col_ind) {
                            ppu->window[sx] = WOBJ;
                        }
                    } else if (o.priority < ppu->objdotattrs[sx].priority ||
                               (ppu->layerlines[LOBJ][sx] & (1 << 15))) {
                        if (col_ind) {
                            u16 col = ppu->pal[0x100 + col_ind];
                            ppu->draw_obj = true;
                            ppu->layerlines[LOBJ][sx] = col & ~(1 << 15);
                            ppu->objdotattrs[sx].semitrans = (o.mode == OBJ_MODE_SEMITRANS) ? 1 : 0;
                            ppu->objdotattrs[sx].mosaic = o.mosaic;
                            ppu->objdotattrs[sx].priority = o.priority;
                        }
                    }
                }

                row >>= 8;
                fx++;
                if (fx == 8) {
                    fx = 0;
                    if (o.hflip) {
                        tile_addr -= 64;
                        row = vram_read32(ppu->master, ppu->objReg, tile_addr + 8 * fy);
                        row |= (u64) vram_read32(ppu->master, ppu->objReg, tile_addr + 8 * fy + 4)
                               << 32;
                        row = (row & 0xffffffff00000000) >> 32 | (row & 0x00000000ffffffff) << 32;
                        row = (row & 0xffff0000ffff0000) >> 16 | (row & 0x0000ffff0000ffff) << 16;
                        row = (row & 0xff00ff00ff00ff00) >> 8 | (row & 0x00ff00ff00ff00ff) << 8;
                    } else {
                        tile_addr += 64;
                        row = vram_read32(ppu->master, ppu->objReg, tile_addr + 8 * fy);
                        row |= (u64) vram_read32(ppu->master, ppu->objReg, tile_addr + 8 * fy + 4)
                               << 32;
                    }
                }
            }
        } else {
            tile_addr += ty * 32 * (ppu->io->dispcnt.obj_mapmode ? w / 8 : 32);
            u32 row;
            if (o.hflip) {
                tile_addr += 32 * (w / 8 - 1);
                row = vram_read32(ppu->master, ppu->objReg, tile_addr + 4 * fy);
                row = (row & 0xffff0000) >> 16 | (row & 0x0000ffff) << 16;
                row = (row & 0xff00ff00) >> 8 | (row & 0x00ff00ff) << 8;
                row = (row & 0xf0f0f0f0) >> 4 | (row & 0x0f0f0f0f) << 4;
            } else {
                row = vram_read32(ppu->master, ppu->objReg, tile_addr + 4 * fy);
            }
            for (int x = 0; x < w; x++) {
                int sx = (o.x + x) % 512;

                if (sx < NDS_SCREEN_W) {
                    u8 col_ind = row & 0xf;
                    if (o.mode == OBJ_MODE_OBJWIN) {
                        if (ppu->io->dispcnt.winobj_enable && col_ind) {
                            ppu->window[sx] = WOBJ;
                        }
                    } else if (o.priority < ppu->objdotattrs[sx].priority ||
                               (ppu->layerlines[LOBJ][sx] & (1 << 15))) {
                        if (col_ind) {
                            col_ind |= o.palette << 4;
                            u16 col = ppu->pal[0x100 + col_ind];
                            ppu->draw_obj = true;
                            ppu->layerlines[LOBJ][sx] = col & ~(1 << 15);
                            ppu->objdotattrs[sx].semitrans = (o.mode == OBJ_MODE_SEMITRANS) ? 1 : 0;
                            ppu->objdotattrs[sx].mosaic = o.mosaic;
                            ppu->objdotattrs[sx].priority = o.priority;
                        }
                    }
                }
                row >>= 4;
                fx++;
                if (fx == 8) {
                    fx = 0;
                    if (o.hflip) {
                        tile_addr -= 32;
                        row = vram_read32(ppu->master, ppu->objReg, tile_addr + 4 * fy);
                        row = (row & 0xffff0000) >> 16 | (row & 0x0000ffff) << 16;
                        row = (row & 0xff00ff00) >> 8 | (row & 0x00ff00ff) << 8;
                        row = (row & 0xf0f0f0f0) >> 4 | (row & 0x0f0f0f0f) << 4;
                    } else {
                        tile_addr += 32;
                        row = vram_read32(ppu->master, ppu->objReg, tile_addr + 4 * fy);
                    }
                }
            }
        }
    }
}

void render_objs(PPU* ppu) {
    if (!ppu->io->dispcnt.obj_enable) return;

    for (int x = 0; x < NDS_SCREEN_W; x++) {
        ppu->layerlines[LOBJ][x] = 1 << 15;
    }

    ppu->obj_cycles = (ppu->io->dispcnt.hblank_free ? NDS_SCREEN_W : DOTS_W) * 6;

    for (int i = 0; i < 128; i++) {
        render_obj_line(ppu, i);
        if (ppu->obj_cycles <= 0) break;
    }
}

void render_windows(PPU* ppu) {
    if (!ppu->io->dispcnt.win_enable) return;

    for (int i = 1; i >= 0; i--) {
        if (!(ppu->io->dispcnt.win_enable & (1 << i)) || !ppu->in_win[i]) continue;

        u8 x1 = ppu->io->winh[i].x1;
        u8 x2 = ppu->io->winh[i].x2;
        for (u8 x = x1; x != x2; x++) {
            ppu->window[x] = i;
        }
    }
}

void hmosaic_bg(PPU* ppu, int bg) {
    u8 mos_ct = -1;
    u8 mos_x = 0;
    for (int x = 0; x < NDS_SCREEN_W; x++) {
        ppu->layerlines[bg][x] = ppu->layerlines[bg][mos_x];
        if (++mos_ct == ppu->io->mosaic.bg_h) {
            mos_ct = -1;
            mos_x = x + 1;
        }
    }
}

void hmosaic_obj(PPU* ppu) {
    u8 mos_ct = -1;
    u8 mos_x = 0;
    bool prev_mos = false;
    for (int x = 0; x < NDS_SCREEN_W; x++) {
        if (++mos_ct == ppu->io->mosaic.obj_h || !ppu->objdotattrs[x].mosaic || !prev_mos) {
            mos_ct = -1;
            mos_x = x;
            prev_mos = ppu->objdotattrs[x].mosaic;
        }
        ppu->layerlines[LOBJ][x] = ppu->layerlines[LOBJ][mos_x];
    }
}

void compose_lines(PPU* ppu) {
    u8 sorted_bgs[4];
    u8 bg_prios[4];
    u8 bgs = 0;
    for (int i = 0; i < 4; i++) {
        if (!ppu->draw_bg[i]) continue;
        sorted_bgs[bgs] = i;
        bg_prios[bgs] = ppu->io->bgcnt[i].priority;
        int j = bgs;
        bgs++;
        while (j > 0 && bg_prios[j] < bg_prios[j - 1]) {
            u8 tmp = sorted_bgs[j];
            sorted_bgs[j] = sorted_bgs[j - 1];
            sorted_bgs[j - 1] = tmp;
            tmp = bg_prios[j];
            bg_prios[j] = bg_prios[j - 1];
            bg_prios[j - 1] = tmp;
            j--;
        }
    }

    u8 effect = ppu->io->bldcnt.effect;
    u8 eva = ppu->io->bldalpha.eva;
    u8 evb = ppu->io->bldalpha.evb;
    u8 evy = ppu->io->bldy.evy;
    if (eva > 16) eva = 16;
    if (evb > 16) evb = 16;
    if (evy > 16) evy = 16;

    if (effect || ppu->obj_semitrans) {
        for (int x = 0; x < NDS_SCREEN_W; x++) {
            u8 layers[6];
            bool win_ena = ppu->io->dispcnt.win_enable || ppu->io->dispcnt.winobj_enable;
            u8 win = ppu->window[x];
            int l = 0;
            bool put_obj = ppu->draw_obj && !(ppu->layerlines[LOBJ][x] & (1 << 15)) &&
                           (!win_ena || (ppu->io->wincnt[win].obj_enable));
            for (int i = 0; i < bgs && l < 2; i++) {
                if (put_obj && ppu->objdotattrs[x].priority <= bg_prios[i]) {
                    put_obj = false;
                    layers[l++] = LOBJ;
                }
                if (!(ppu->layerlines[sorted_bgs[i]][x] & (1 << 15)) &&
                    (!win_ena || (ppu->io->wincnt[win].bg_enable & (1 << sorted_bgs[i])))) {
                    layers[l++] = sorted_bgs[i];
                }
            }
            if (put_obj) {
                layers[l++] = LOBJ;
            }
            layers[l++] = LBD;

            u16 color1 = ppu->layerlines[layers[0]][x];

            if (layers[0] == LOBJ && ppu->objdotattrs[x].semitrans && l > 1 &&
                (ppu->io->bldcnt.target2 & (1 << layers[1]))) {
                u8 r1 = color1 & 0x1f;
                u8 g1 = (color1 >> 5) & 0x1f;
                u8 b1 = (color1 >> 10) & 0x1f;
                u16 color2 = ppu->layerlines[layers[1]][x];
                u8 r2 = color2 & 0x1f;
                u8 g2 = (color2 >> 5) & 0x1f;
                u8 b2 = (color2 >> 10) & 0x1f;
                r1 = (eva * r1 + evb * r2) / 16;
                if (r1 > 31) r1 = 31;
                g1 = (eva * g1 + evb * g2) / 16;
                if (g1 > 31) g1 = 31;
                b1 = (eva * b1 + evb * b2) / 16;
                if (b1 > 31) b1 = 31;
                ppu->screen[ppu->ly][x] = (b1 << 10) | (g1 << 5) | r1;
            } else if ((ppu->io->bldcnt.target1 & (1 << layers[0])) &&
                       (!win_ena || ppu->io->wincnt[win].effects_enable)) {
                u8 r1 = color1 & 0x1f;
                u8 g1 = (color1 >> 5) & 0x1f;
                u8 b1 = (color1 >> 10) & 0x1f;
                switch (effect) {
                    case EFF_ALPHA: {
                        if (l == 1 || !(ppu->io->bldcnt.target2 & (1 << layers[1]))) break;
                        u16 color2 = ppu->layerlines[layers[1]][x];
                        u8 r2 = color2 & 0x1f;
                        u8 g2 = (color2 >> 5) & 0x1f;
                        u8 b2 = (color2 >> 10) & 0x1f;
                        r1 = (eva * r1 + evb * r2) / 16;
                        if (r1 > 31) r1 = 31;
                        g1 = (eva * g1 + evb * g2) / 16;
                        if (g1 > 31) g1 = 31;
                        b1 = (eva * b1 + evb * b2) / 16;
                        if (b1 > 31) b1 = 31;
                        break;
                    }
                    case EFF_BINC: {
                        r1 += (31 - r1) * evy / 16;
                        g1 += (31 - g1) * evy / 16;
                        b1 += (31 - b1) * evy / 16;
                        break;
                    }
                    case EFF_BDEC: {
                        r1 -= r1 * evy / 16;
                        g1 -= g1 * evy / 16;
                        b1 -= b1 * evy / 16;
                        break;
                    }
                }
                ppu->screen[ppu->ly][x] = (b1 << 10) | (g1 << 5) | r1;
            } else {
                ppu->screen[ppu->ly][x] = color1;
            }
        }
    } else {
        for (int x = 0; x < NDS_SCREEN_W; x++) {
            u8 layers[6];
            bool win_ena = ppu->io->dispcnt.win_enable || ppu->io->dispcnt.winobj_enable;
            u8 win = ppu->window[x];
            int l = 0;
            bool put_obj = ppu->draw_obj && !(ppu->layerlines[LOBJ][x] & (1 << 15)) &&
                           (!win_ena || (ppu->io->wincnt[win].obj_enable));

            for (int i = 0; i < bgs; i++) {
                if (put_obj && ppu->objdotattrs[x].priority <= bg_prios[i]) {
                    put_obj = false;
                    layers[l++] = LOBJ;
                    break;
                }
                if (!(ppu->layerlines[sorted_bgs[i]][x] & (1 << 15)) &&
                    (!win_ena || (ppu->io->wincnt[win].bg_enable & (1 << sorted_bgs[i])))) {
                    layers[l++] = sorted_bgs[i];
                    break;
                }
            }
            if (put_obj) {
                layers[l++] = LOBJ;
            }
            layers[l++] = LBD;

            ppu->screen[ppu->ly][x] = ppu->layerlines[layers[0]][x];
        }
    }
}

void draw_scanline_normal(PPU* ppu) {
    for (int x = 0; x < NDS_SCREEN_W; x++) {
        ppu->layerlines[LBD][x] = ppu->pal[0];
    }

    ppu->draw_bg[0] = false;
    ppu->draw_bg[1] = false;
    ppu->draw_bg[2] = false;
    ppu->draw_bg[3] = false;
    ppu->draw_obj = false;
    ppu->obj_mos = false;
    ppu->obj_semitrans = false;

    if (ppu->io->dispcnt.win_enable || ppu->io->dispcnt.winobj_enable)
        memset(ppu->window, WOUT, NDS_SCREEN_W);
    render_bgs(ppu);
    render_objs(ppu);
    render_windows(ppu);

    if (ppu->io->bgcnt[0].mosaic) hmosaic_bg(ppu, 0);
    if (ppu->io->bgcnt[1].mosaic) hmosaic_bg(ppu, 1);
    if (ppu->io->bgcnt[2].mosaic) hmosaic_bg(ppu, 2);
    if (ppu->io->bgcnt[3].mosaic) hmosaic_bg(ppu, 3);
    if (ppu->obj_mos) hmosaic_obj(ppu);

    compose_lines(ppu);
}

void draw_scanline(PPU* ppu) {
    if (ppu->io->dispcnt.forced_blank) {
        memset(ppu->screen[ppu->ly], 0, sizeof ppu->screen[0]);
        return;
    }

    switch (ppu->io->dispcnt.disp_mode) {
        case 0:
            memset(ppu->screen[ppu->ly], 0, sizeof ppu->screen[0]);
            break;
        case 1:
            draw_scanline_normal(ppu);
            break;
        case 2:
            memcpy(ppu->screen[ppu->ly],
                   &ppu->master->vrambanks[ppu->io->dispcnt.vram_block][2 * NDS_SCREEN_W * ppu->ly],
                   sizeof ppu->screen[0]);
            break;
        case 3:
            break;
    }
}

void ppu_check_window(PPU* ppu) {
    for (int i = 0; i < 2; i++) {
        if ((u8) ppu->ly == ppu->io->winv[i].y1) ppu->in_win[i] = true;
        if ((u8) ppu->ly == ppu->io->winv[i].y2) ppu->in_win[i] = false;
    }
}

void lcd_hdraw(NDS* nds) {
    nds->io7.vcount++;
    if (nds->io7.vcount == LINES_H) {
        nds->io7.vcount = 0;
    }
    nds->io9.vcount = nds->io7.vcount;
    nds->ppuA.ly = nds->io7.vcount;
    nds->ppuB.ly = nds->io7.vcount;

    nds->io7.dispstat.hblank = 0;
    nds->io9.dispstat.hblank = 0;

    if (nds->io7.vcount == (nds->io7.dispstat.lyc | nds->io7.dispstat.lyc_hi << 8)) {
        nds->io7.dispstat.vcounteq = 1;
        if (nds->io7.dispstat.vcount_irq) nds->io7.ifl.vcounteq = 1;
    } else nds->io7.dispstat.vcounteq = 0;
    if (nds->io9.vcount == (nds->io9.dispstat.lyc | nds->io9.dispstat.lyc_hi << 8)) {
        nds->io9.dispstat.vcounteq = 1;
        if (nds->io9.dispstat.vcount_irq) nds->io9.ifl.vcounteq = 1;
    } else nds->io9.dispstat.vcounteq = 0;

    ppu_check_window(&nds->ppuA);
    ppu_check_window(&nds->ppuB);

    if (nds->io7.vcount < NDS_SCREEN_H) {
        draw_scanline(&nds->ppuA);
        draw_scanline(&nds->ppuB);

        for (int i = 0; i < 4; i++) {
            if (nds->io9.dma[i].cnt.mode == DMA9_DISPLAY) {
                dma9_activate(&nds->dma9, i);
            }
        }
    } else if (nds->io7.vcount == NDS_SCREEN_H) {
        lcd_vblank(nds);
        nds->frame_complete = true;
    } else if (nds->io7.vcount == LINES_H - 1) {
        nds->io7.dispstat.vblank = 0;
        nds->io9.dispstat.vblank = 0;
    }

    add_event(&nds->sched, EVENT_LCD_HBLANK, nds->sched.now + 6 * NDS_SCREEN_W + 70);

    add_event(&nds->sched, EVENT_LCD_HDRAW, nds->sched.now + 6 * DOTS_W);
}

void ppu_vblank(PPU* ppu) {
    ppu->bgaffintr[0].x = ppu->io->bgaff[0].x;
    ppu->bgaffintr[0].y = ppu->io->bgaff[0].y;
    ppu->bgaffintr[1].x = ppu->io->bgaff[1].x;
    ppu->bgaffintr[1].y = ppu->io->bgaff[1].y;
    ppu->bgaffintr[0].mosx = ppu->bgaffintr[0].x;
    ppu->bgaffintr[0].mosy = ppu->bgaffintr[0].y;
    ppu->bgaffintr[1].mosx = ppu->bgaffintr[1].x;
    ppu->bgaffintr[1].mosy = ppu->bgaffintr[1].y;

    ppu->bgmos_y = 0;
    ppu->bgmos_ct = -1;
    ppu->objmos_y = 0;
    ppu->objmos_ct = -1;
}

void lcd_vblank(NDS* nds) {
    nds->io7.dispstat.vblank = 1;
    nds->io9.dispstat.vblank = 1;
    if (nds->io7.dispstat.vblank_irq) nds->io7.ifl.vblank = 1;
    if (nds->io9.dispstat.vblank_irq) nds->io9.ifl.vblank = 1;

    ppu_vblank(&nds->ppuA);
    ppu_vblank(&nds->ppuB);

    for (int i = 0; i < 4; i++) {
        if (nds->io7.dma[i].cnt.mode == DMA7_VBLANK) {
            dma9_activate(&nds->dma7, i);
        }
    }
    for (int i = 0; i < 4; i++) {
        if (nds->io9.dma[i].cnt.mode == DMA9_VBLANK) {
            dma9_activate(&nds->dma9, i);
        }
    }
}

void ppu_hblank(PPU* ppu) {
    ppu->bgaffintr[0].x += ppu->io->bgaff[0].pb;
    ppu->bgaffintr[0].y += ppu->io->bgaff[0].pd;
    ppu->bgaffintr[1].x += ppu->io->bgaff[1].pb;
    ppu->bgaffintr[1].y += ppu->io->bgaff[1].pd;

    if (++ppu->bgmos_ct == ppu->io->mosaic.bg_v) {
        ppu->bgmos_ct = -1;
        ppu->bgmos_y = ppu->ly + 1;
        ppu->bgaffintr[0].mosx = ppu->bgaffintr[0].x;
        ppu->bgaffintr[0].mosy = ppu->bgaffintr[0].y;
        ppu->bgaffintr[1].mosx = ppu->bgaffintr[1].x;
        ppu->bgaffintr[1].mosy = ppu->bgaffintr[1].y;
    }
    if (++ppu->objmos_ct == ppu->io->mosaic.obj_v) {
        ppu->objmos_ct = -1;
        ppu->objmos_y = ppu->ly + 1;
    }
}

void lcd_hblank(NDS* nds) {
    nds->io7.dispstat.hblank = 1;
    nds->io9.dispstat.hblank = 1;
    if (nds->io7.dispstat.hblank_irq) nds->io7.ifl.hblank = 1;
    if (nds->io9.dispstat.hblank_irq) nds->io9.ifl.hblank = 1;

    if (nds->io7.vcount < NDS_SCREEN_H) {
        ppu_hblank(&nds->ppuA);
        ppu_hblank(&nds->ppuB);
        for (int i = 0; i < 4; i++) {
            if (nds->io9.dma[i].cnt.mode == DMA9_HBLANK) {
                dma9_activate(&nds->dma9, i);
            }
        }
    }
}