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

    return card;
}

void destroy_card(GameCard* card) {
    free(card->rom);
    free(card);
}