#include "gamecard.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "key1.h"

GameCard* create_card(char* filename) {

    int fd = open(filename, O_RDONLY);
    if (fd < 0) return NULL;

    GameCard* card = calloc(1, sizeof *card);
    
    struct stat st;
    fstat(fd, &st);
    u64 v = st.st_size;
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    v++;
    if (v < (1 << 17)) v = 1 << 17;
    card->rom_size = v;
    card->rom = mmap(NULL, card->rom_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANON, -1, 0);
    card->rom = mmap(card->rom, st.st_size, PROT_READ | PROT_WRITE,
                     MAP_FIXED | MAP_PRIVATE, fd, 0);
    close(fd);

    card->rom_filename = strdup(filename);
    int i = strrchr(filename, '.') - filename;
    card->sav_filename = malloc(i + sizeof ".sav");
    strncpy(card->sav_filename, card->rom_filename, i);
    strcpy(card->sav_filename + i, ".sav");

    fd = open(card->sav_filename, O_RDWR);
    if (fd < 0) {
        card->sav_new = true;
        card->eeprom_size = 1 << 16;
        card->eeprom = calloc(1 << 16, 1);
        card->addrtype = 2;
    } else {
        struct stat st;
        fstat(fd, &st);
        card->eeprom_size = st.st_size;
        if (card->eeprom_size == 512) card->addrtype = 1;
        else if (card->eeprom_size <= (1 << 16)) card->addrtype = 2;
        else card->addrtype = 3;
        card->eeprom_detected = true;
        card->eeprom = mmap(NULL, card->eeprom_size, PROT_READ | PROT_WRITE,
                            MAP_SHARED, fd, 0);
        close(fd);
    }

    return card;
}

void destroy_card(GameCard* card) {
    if (card->sav_new) {
        FILE* fp = fopen(card->sav_filename, "wb");
        if (fp) {
            fwrite(card->eeprom, 1, card->eeprom_size, fp);
            fclose(fp);
        }
        free(card->eeprom);
    } else {
        munmap(card->eeprom, card->eeprom_size);
    }
    munmap(card->rom, card->rom_size);

    free(card->rom_filename);
    free(card->sav_filename);
    free(card);
}

void encrypt_securearea(GameCard* card, u32* keys) {
    if (card->encrypted) return;
    card->encrypted = true;

    memcpy(&card->rom[0x4000], "encryObj", 8);

    init_keycode(*(u32*) &card->rom[0xc], 3, 2, keys);
    for (int i = 0; i < 0x800; i += 8) {
        encrypt64((u32*) &card->rom[0x4000 + i]);
    }
    init_keycode(*(u32*) &card->rom[0xc], 2, 2, keys);
    encrypt64((u32*) &card->rom[0x4000]);
}

bool card_write_command(GameCard* card, u8* command) {
    if (card->key1mode) {
        u8 dec[8];
        for (int i = 0; i < 8; i++) {
            dec[i] = command[7 - i];
        }
        decrypt64((u32*) dec);
        for (int i = 0; i < 8; i++) {
            command[i] = dec[7 - i];
        }

        switch (command[0] >> 4) {
            case 1:
                card->state = CARD_CHIPID;
                return true;
                break;
            case 2: {
                card->state = CARD_DATA;
                int block = command[2] >> 4 | command[1] << 4 |
                            (command[0] & 0xf) << 12;
                card->addr = block << 12;
                card->i = 0;
                card->len = 0x1000;
                return true;
                break;
            }
            case 4:
                return false;
                break;
            case 0xa:
                card->key1mode = false;
                return false;
                break;
            default:
                return false;
        }
    } else {
        switch (command[0]) {
            case 0x00:
                card->state = CARD_DATA;
                card->addr = 0;
                card->len = 0x200;
                card->i = 0;
                return true;
                break;
            case 0x3c:
                card->key1mode = true;
                return false;
                break;
            case 0x90:
                card->state = CARD_CHIPID;
                return true;
                break;
            case 0xb7:
                card->state = CARD_DATA;
                card->addr = command[1] << 24 | command[2] << 16 |
                             command[3] << 8 | command[4];
                card->addr &= card->rom_size - 1;
                if (card->addr < 0x8000) {
                    card->addr = 0x8000 + (card->addr & 0x1ff);
                }
                card->i = 0;
                card->len = 0x200;
                return true;
                break;
            case 0xb8:
                card->state = CARD_CHIPID;
                return true;
                break;
            default:
                return false;
        }
    }
}

bool card_read_data(GameCard* card, u32* data) {
    switch (card->state) {
        case CARD_IDLE:
            *data = -1;
            return false;
        case CARD_CHIPID:
            *data = CHIPID;
            return false;
        case CARD_DATA:
            *data = *(u32*) &card->rom[(card->addr & 0xfffff000) +
                                       ((card->addr + card->i) & 0xfff)];
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
    switch (card->eeprom_state) {
        case CARDEEPROM_IDLE:
            switch (data) {
                case 0x01:
                    card->eeprom_state = CARDEEPROM_WRSR;
                    break;
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
                    card->eepromst.addr = card->addrtype == 1;
                    card->eepromst.i = 0;
                    card->eeprom_state = CARDEEPROM_ADDR;
                    break;
                case 0x0a:
                    card->eepromst.read = false;
                    card->eepromst.addr = card->addrtype == 1;
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
                card->eeprom_state =
                    card->eepromst.read ? CARDEEPROM_READ : CARDEEPROM_WRITE;
            }
            break;
        case CARDEEPROM_READ:
            card->eepromst.i++;
            card->spidata = card->eeprom[card->eepromst.addr++];
            card->eepromst.addr &= card->eeprom_size - 1;
            break;
        case CARDEEPROM_WRITE:
            card->eepromst.i++;
            card->eeprom[card->eepromst.addr++] = data;
            card->eepromst.addr &= card->eeprom_size - 1;
            break;
        case CARDEEPROM_STAT:
            card->spidata = (card->eepromst.write_enable ? 2 : 0) |
                            (card->addrtype == 1 ? 0xf0 : 0);
            break;
        case CARDEEPROM_WRSR:
            break;
        case CARDEEPROM_ID:
            card->spidata = 0xff;
            break;
    }
    if (!hold) {
        card->eeprom_state = CARDEEPROM_IDLE;
        if (!card->eeprom_detected) {
            card->eeprom_detected = true;
            switch (card->eepromst.i) {
                case 17:
                    card->addrtype = 1;
                    card->eeprom_size = 512;
                    break;
                case 34:
                    card->addrtype = 2;
                    card->eeprom_size = 1 << 13;
                    break;
                case 130:
                    card->addrtype = 2;
                    card->eeprom_size = 1 << 16;
                    break;
                case 259:
                    card->addrtype = 3;
                    card->eeprom_size = 1 << 20;
                    break;
                default:
                    card->eeprom_detected = false;
            }
            if (card->eeprom_detected) {
                card->eeprom = realloc(card->eeprom, card->eeprom_size);
            }
        }
        card->eepromst.i = 0;
    }
}