#ifndef _TYPES_H
#define _TYPES_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define eprintf(...) fprintf(stderr, __VA_ARGS__)

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef int64_t s64;

#define FIFO(T, N)                                                             \
    struct {                                                                   \
        T d[N];                                                                \
        u32 head;                                                              \
        u32 tail;                                                              \
        u32 size;                                                              \
    }

#define __FIFO_MAX(f) (sizeof(f).d / sizeof(f).d[0])

#define FIFO_push(f, v)                                                        \
    ((f).d[(f).tail++] = v, (f).tail &= __FIFO_MAX(f) - 1, (f).size++)
#define FIFO_pop(f, v)                                                         \
    (v = (f).d[(f).head++], (f).head &= __FIFO_MAX(f) - 1, (f).size--)
#define FIFO_peek(f) ((f).d[(f).head])
#define FIFO_foreach(i, f)                                                     \
    for (u32 _i = 0, i = (f).head; _i < (f).size;                              \
         _i++, i = (i + 1) & (__FIFO_MAX(f) - 1))
#define FIFO_clear(f) ((f).d[0] = (f).head = (f).tail = (f).size = 0)

#endif