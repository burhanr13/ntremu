#include "key1.h"

#include <string.h>

u32 keybuf[0x412];
u32 keycode[3];

void init_keycode(u32 idcode, int level, int mod, u32* init_keys) {
    memcpy(keybuf, init_keys, sizeof keybuf);
    keycode[0] = idcode;
    keycode[1] = idcode >> 1;
    keycode[2] = idcode << 1;
    if (level >= 1) apply_keycode(mod);
    if (level >= 2) apply_keycode(mod);
    keycode[1] <<= 1;
    keycode[2] >>= 1;
    if (level >= 3) apply_keycode(mod);
}

void apply_keycode(int mod) {
    encrypt64(keycode + 1);
    encrypt64(keycode);
    u32 scratch[2] = {};
    for (int i = 0; i < 0x12; i++) {
        keybuf[i] ^= bswap32(keycode[i % mod]);
    }
    for (int i = 0; i < 0x412; i += 2) {
        encrypt64(scratch);
        keybuf[i] = scratch[1];
        keybuf[i + 1] = scratch[0];
    }
}

void encrypt64(u32* data) {
    u32 y = data[0];
    u32 x = data[1];
    for (int i = 0; i < 0x10; i++) {
        u32 z = keybuf[i] ^ x;
        x = keybuf[0x12 + ((z >> 24) & 0xff)];
        x += keybuf[0x112 + ((z >> 16) & 0xff)];
        x ^= keybuf[0x212 + ((z >> 8) & 0xff)];
        x += keybuf[0x312 + ((z >> 0) & 0xff)];
        x ^= y;
        y = z;
    }
    data[0] = x ^ keybuf[0x10];
    data[1] = y ^ keybuf[0x11];
}

void decrypt64(u32* data) {
    u32 y = data[0];
    u32 x = data[1];
    for (int i = 0x11; i > 0x1; i--) {
        u32 z = keybuf[i] ^ x;
        x = keybuf[0x12 + ((z >> 24) & 0xff)];
        x += keybuf[0x112 + ((z >> 16) & 0xff)];
        x ^= keybuf[0x212 + ((z >> 8) & 0xff)];
        x += keybuf[0x312 + ((z >> 0) & 0xff)];
        x ^= y;
        y = z;
    }
    data[0] = x ^ keybuf[1];
    data[1] = y ^ keybuf[0];
}

u32 bswap32(u32 data) {
    data = (data & 0xffff0000) >> 16 | (data & 0x0000ffff) << 16;
    data = (data & 0xff00ff00) >> 8 | (data & 0x00ff00ff) << 8;
    return data;
}