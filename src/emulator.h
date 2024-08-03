#ifndef EMULATOR_H
#define EMULATOR_H

#include <SDL2/SDL.h>

#include "emulator_state.h"
#include "gamecard.h"
#include "nds.h"
#include "types.h"

int emulator_init(int argc, char** argv);
void emulator_quit();

void emulator_reset();

void read_args(int argc, char** argv);
void hotkey_press(SDL_KeyCode key);
void update_input_keyboard(NDS* nds);
void update_input_controller(NDS* nds, SDL_GameController* controller);
void update_input_touch(NDS* nds, SDL_Rect* ts_bounds,
                        SDL_GameController* controller);

void update_input_freecam();

#endif