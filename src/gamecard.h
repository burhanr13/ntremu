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

typedef enum { CARD_IDLE, CARD_CHIPID, CARD_DATA } CardState;
typedef enum {
    CARDEEPROM_IDLE,
    CARDEEPROM_ADDR,
    CARDEEPROM_READ,
    CARDEEPROM_WRITE,
    CARDEEPROM_STAT,
    CARDEEPROM_ID
} CardEepromState;

typedef struct {
    u8* rom;
    u32 rom_size;

    CardState state;

    u32 addr;
    u32 i;
    u32 len;

    u8* eeprom;
    u32 eeprom_size;
    int addrtype;

    CardEepromState eeprom_state;
    u8 spidata;

    struct {
        u32 addr;
        int i;
        bool write_enable;
        bool read;
        bool size_detected;
    } eepromst;

} GameCard;

GameCard* create_card(char* filename);
void destroy_card(GameCard* card);

bool card_write_command(GameCard* card, u8* command);
bool card_read_data(GameCard* card, u32* data);

void card_spi_write(GameCard* card, u8 data, bool hold);

#endif