#ifndef _KEY1_H_
#define _KEY1_H_

#include "types.h"

void init_keycode(u32 idcode, int level, int mod, u32* keybuf);
void apply_keycode(int mod);
void encrypt64(u32* data);
void decrypt64(u32* data);
u32 bswap32(u32 data);

#endif