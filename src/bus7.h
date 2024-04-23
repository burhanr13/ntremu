#ifndef _BUS7_H
#define _BUS7_H

#include "types.h"

typedef struct _NDS NDS;

u8 bus7_read8(NDS* nds, u32 addr);
u16 bus7_read16(NDS* nds, u32 addr);
u32 bus7_read32(NDS* nds, u32 addr);

void bus7_write8(NDS* nds, u32 addr, u8 data);
void bus7_write16(NDS* nds, u32 addr, u16 data);
void bus7_write32(NDS* nds, u32 addr, u32 data);

#endif