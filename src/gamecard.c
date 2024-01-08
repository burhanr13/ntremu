#include "gamecard.h"

#include <stdio.h>
#include <stdlib.h>

GameCard* create_card(char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) return NULL;

    GameCard* card = calloc(1, sizeof *card);

    fseek(fp, 0, SEEK_END);
    card->rom_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (card->rom_size < 0x200) card->rom_size = 0x200;
    card->rom = malloc(card->rom_size);
    fread(card->rom, 1, card->rom_size, fp);
    fclose(fp);

    card->eeprom = calloc(1 << 20, 1);
    card->eeprom_size = 1 << 16;
    card->addrtype = 3;

    return card;
}

void destroy_card(GameCard* card) {
    free(card->eeprom);
    free(card->rom);
    free(card);
}

bool card_write_command(GameCard* card, u8* command) {
    if (command[0] == 0xb8) {
        card->state = CARD_CHIPID;
        return true;
    }
    if (command[0] == 0xb7) {
        card->state = CARD_DATA;
        card->addr = command[1] << 24 | command[2] << 16 | command[3] << 8 | command[4];
        card->i = 0;
        return true;
    }
    return false;
}

bool card_read_data(GameCard* card, u32* data) {
    switch (card->state) {
        case CARD_IDLE:
            *data = -1;
            return false;
        case CARD_CHIPID:
            *data = 0x00001fc2;
            return false;
        case CARD_DATA:
            *data = *(u32*) &card->rom[(card->addr + card->i) % card->rom_size];
            card->i += 4;
            if (card->i < card->len) {
                return true;
            } else {
                card->state = CARD_IDLE;
                return false;
            }
        default:
            return false;
    }
}

void card_spi_write(GameCard* card, u8 data, bool hold) {
    // printf("%02x/", data);
    switch (card->eeprom_state) {
        case CARDEEPROM_IDLE:
            switch (data) {
                case 0x06:
                    card->eepromst.write_enable = true;
                    break;
                case 0x04:
                    card->eepromst.write_enable = false;
                    break;
                case 0x05:
                    card->eeprom_state = CARDEEPROM_STAT;
                    break;
                case 0x03:
                    card->eepromst.read = true;
                    card->eepromst.addr = 0;
                    card->eepromst.i = 0;
                    card->eeprom_state = CARDEEPROM_ADDR;
                    break;
                case 0x02:
                    card->eepromst.read = false;
                    card->eepromst.addr = 0;
                    card->eepromst.i = 0;
                    card->eeprom_state = CARDEEPROM_ADDR;
                    break;
                case 0x0b:
                    card->eepromst.read = true;
                    card->eepromst.addr = (card->addrtype == 1) ? 1 : 0;
                    card->eepromst.i = 0;
                    card->eeprom_state = CARDEEPROM_ADDR;
                    break;
                case 0x0a:
                    card->eepromst.read = false;
                    card->eepromst.addr = (card->addrtype == 1) ? 1 : 0;
                    card->eepromst.i = 0;
                    card->eeprom_state = CARDEEPROM_ADDR;
                    break;
                case 0x9f:
                    card->eeprom_state = CARDEEPROM_ID;
                    break;
            }
            break;
        case CARDEEPROM_ADDR:
            card->eepromst.addr <<= 8;
            card->eepromst.addr |= data;
            if (++card->eepromst.i == card->addrtype) {
                card->eepromst.i = 0;
                card->eeprom_state = card->eepromst.read ? CARDEEPROM_READ : CARDEEPROM_WRITE;
            }
            break;
        case CARDEEPROM_READ:
            card->spidata = card->eeprom[card->eepromst.addr++];
            break;
        case CARDEEPROM_WRITE:
            card->eeprom[card->eepromst.addr++] = data;
            break;
        case CARDEEPROM_STAT:
            card->spidata = card->eepromst.write_enable ? 2 : 0;
            break;
        case CARDEEPROM_ID:
            card->spidata = 0xff;
            break;
    }
    // printf("%02x ", card->spidata);
    if (!hold) {
        card->eeprom_state = CARDEEPROM_IDLE;
        // printf("\n");
    }
}