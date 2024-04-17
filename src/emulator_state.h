#ifndef EMULATOR_STATE_H
#define EMULATOR_STATE_H

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
    bool abs_touch;

    u32 breakpoint;

    NDS* nds;
    GameCard* card;
    u8* bios7;
    u8* bios9;
    u8* firmware;

    bool wireframe;
    bool freecam;
    mat4 freecam_mtx;

} EmulatorState;

extern EmulatorState ntremu;

#endif