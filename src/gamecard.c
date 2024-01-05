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

    card->eeprom = calloc(1 << 16, 1);
    card->eeprom_size = 1 << 16;
    card->addr_bytes = 2;

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
    switch (card->spi_state) {
        case CARDSPI_IDLE:
            switch (data) {
                case 0:
                    card->spidata = card->eeprom[card->eeprom_addr++];
                    break;
                case 0x06:
                    card->write_enable = true;
                    break;
                case 0x04:
                    card->write_enable = false;
                    break;
                case 0x05:
                    card->spi_state = CARDSPI_STAT;
                    break;
                case 0x03:
                    card->eeprom_read = true;
                    card->eeprom_addr = 0;
                    card->addr_cur_bytes = 0;
                    card->spi_state = CARDSPI_ADDR;
                    break;
                case 0x02:
                    card->eeprom_read = false;
                    card->eeprom_addr = 0;
                    card->addr_cur_bytes = 0;
                    card->spi_state = CARDSPI_ADDR;
                    break;
                case 0x0b:
                    card->eeprom_read = true;
                    card->eeprom_addr = (card->addr_bytes == 1) ? 1 : 0;
                    card->addr_cur_bytes = 0;
                    card->spi_state = CARDSPI_ADDR;
                    break;
                case 0x0a:
                    card->eeprom_read = false;
                    card->eeprom_addr = (card->addr_bytes == 1) ? 1 : 0;
                    card->addr_cur_bytes = 0;
                    card->spi_state = CARDSPI_ADDR;
                    break;
                case 0x9f:
                    card->spi_state = CARDSPI_ID;
                    break;
            }
            break;
        case CARDSPI_ADDR:
            card->eeprom_addr <<= 8;
            card->eeprom_addr |= data;
            if (++card->addr_cur_bytes == card->addr_bytes) {
                card->spi_state = card->eeprom_read ? CARDSPI_READ : CARDSPI_WRITE;
            }
            break;
        case CARDSPI_READ:
            card->spidata = card->eeprom[card->eeprom_addr++];
            break;
        case CARDSPI_WRITE:
            card->eeprom[card->eeprom_addr++] = data;
            break;
        case CARDSPI_STAT:
            card->spidata = card->write_enable ? 2 : 0;
            break;
        case CARDSPI_ID:
            card->spidata = 0xff;
            break;
    }
    if (!hold) {
        card->spi_state = CARDSPI_IDLE;
    }
}

u8 card_spi_read(GameCard* card) {
    return card->spidata;
}