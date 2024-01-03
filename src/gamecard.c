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

    card->state = CARD_IDLE;

    return card;
}

void destroy_card(GameCard* card) {
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
            *data = 0x00000fc2;
            return true;
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