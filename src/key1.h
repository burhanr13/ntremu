#ifndef KEY1_H
#define KEY1_H

#include "types.h"

void init_keycode(u32 idcode, int level, int mod, u32* keybuf);
void apply_keycode(int mod);
void encrypt64(u32* data);
void decrypt64(u32* data);
u32 bswap32(u32 data);

#endif