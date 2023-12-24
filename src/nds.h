#ifndef NDS_H
#define HDS_H

#include "arm7tdmi.h"
#include "arm946e.h"

#define RAMSIZE (1<<22)

#define 
#define WRAMSIZE (1<<16)


typedef struct _NDS {
    Arm7TDMI cpu7;
    Arm946E cpu9;

    union {
        u8 b[RAMSIZE];
        u16 h[RAMSIZE>>1];
        u32 w[RAMSIZE>>2];
    } ram;

    union {
        u8 b[]
    }


} NDS;

#endif