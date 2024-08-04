#ifndef BUS9_H
#define BUS9_H

#include "types.h"

typedef struct _NDS NDS;

u8 bus9_read8(NDS* nds, u32 addr);
u16 bus9_read16(NDS* nds, u32 addr);
u32 bus9_read32(NDS* nds, u32 addr);

void bus9_write8(NDS* nds, u32 addr, u8 data);
void bus9_write16(NDS* nds, u32 addr, u16 data);
void bus9_write32(NDS* nds, u32 addr, u32 data);

#endif