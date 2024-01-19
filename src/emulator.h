#ifndef EMULATOR_H
#define EMULATOR_H

#include <SDL2/SDL.h>

#include "gamecard.h"
#include "nds.h"
#include "types.h"

typedef struct {
    char* romfile;
    char* romfilenodir;

    bool running;
    bool uncap;
    bool bootbios;
    bool pause;
    bool mute;
    bool debugger;
    bool frame_adv;

    u32 breakpoint;

    NDS* nds;
    GameCard* card;
    u8* bios7;
    u8* bios9;
    u8* firmware;

} EmulatorState;

extern EmulatorState ntremu;

int emulator_init(int argc, char** argv);
void emulator_quit();

void read_args(int argc, char** argv);
void hotkey_press(SDL_KeyCode key);
void update_input_keyboard(NDS* nds);
void update_input_controller(NDS* nds, SDL_GameController* controller);
void update_input_touch(NDS* nds, SDL_Rect* ts_bounds);

#endif