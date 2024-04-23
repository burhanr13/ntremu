#ifndef _FIFO_H
#define _FIFO_H

#include "types.h"

#define FIFO(t, n)                                                             \
    struct {                                                                   \
        t d[n];                                                                \
        u32 head;                                                              \
        u32 tail;                                                              \
        u32 size;                                                              \
    }

#define __FIFO_MAX(f) (sizeof(f).d / sizeof(f).d[0])

#define FIFO_push(f, v)                                                        \
    ((f).d[(f).tail++] = v, (f).tail &= __FIFO_MAX(f) - 1, (f).size++)
#define FIFO_pop(f, v)                                                         \
    (v = (f).d[(f).head++], (f).head &= __FIFO_MAX(f) - 1, (f).size--)

#endif