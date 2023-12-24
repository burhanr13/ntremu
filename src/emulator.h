#ifndef EMULATOR_H
#define EMULATOR_H

#include <SDL2/SDL.h>

#include "gba.h"
#include "types.h"

typedef struct {
    bool running;
    char* romfile;
    char* romfilenodir;
    char* biosfile;
    bool uncap;
    bool bootbios;
    bool filter;
    bool pause;
    bool mute;
    bool debugger;

    NDS* gba;
    Cartridge* cart;
    u8* bios;

} EmulatorState;

extern EmulatorState agbemu;

int emulator_init(int argc, char** argv);
void emulator_quit();

void read_args(int argc, char** argv);
void hotkey_press(SDL_KeyCode key);
void update_input_keyboard(NDS* gba);
void update_input_controller(NDS* gba, SDL_GameController* controller);
void init_color_lookups();
void gba_convert_screen(u16* gba_screen, Uint32* screen);

#endif