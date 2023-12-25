#ifndef GAMECARD_H
#define GAMECARD_H

#include "types.h"

typedef struct {
    char title[12];
    char gamecode[4];
    char makercode[2];
    u8 unitcode;
    u8 seed_select;
    u8 capacity;
    u8 reserved1[7];
    u8 reserved2[1];
    u8 region;
    u8 version;
    u8 autostart;
    u32 arm9_rom_offset;
    u32 arm9_entry;
    u32 arm9_ram_offset;
    u32 arm9_size;
    u32 arm7_rom_offset;
    u32 arm7_entry;
    u32 arm7_ram_offset;
    u32 arm7_size;
    u8 rest[0x1c0];
} CardHeader;

typedef struct {
    u8* rom;
    int rom_size;
} GameCard;

GameCard* create_card(char* filename);
void destroy_card(GameCard* card);

#endif