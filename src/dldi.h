#ifndef DLDI_H
#define DLDI_H

#include "types.h"

#define SECTOR_SIZE 0x200

enum { DLDI_CTRL = 0xfff444, DLDI_DATA = 0xfff448 };

#define DLDI_ID 0xbf8da5ed
#define DLDI_MAGIC " Chishm"

typedef struct {
    u32 id;      // 0xbf8da5ed
    u8 magic[8]; // " Chishm"
    u8 version;
    u8 size;
    u8 fix;
    u8 space_avail;
    u8 name[48];
    u32 text_start;
    u32 data_end;
    u32 glue_start;
    u32 glue_end;
    u32 got_start;
    u32 got_end;
    u32 bss_start;
    u32 bss_end;
    u8 dev[4];
    u32 flags;
    u32 startup;
    u32 isInserted;
    u32 readSectors;
    u32 writeSectors;
    u32 clearStatus;
    u32 shutdown;
} DLDIHeader;

void dldi_patch_binary(u8* b, u32 len);

u32 dldi_get_status();
void dldi_write_addr(u32 addr);
void dldi_write_data(u32 data);
u32 dldi_read_data();

#endif
