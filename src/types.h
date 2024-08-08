#ifndef TYPES_H
#define TYPES_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define eprintf(format, ...) fprintf(stderr, format __VA_OPT__(, ) __VA_ARGS__)

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef int64_t s64;

#define BIT(n) (1 << (n))

#define FIFO(T, N)                                                             \
    struct {                                                                   \
        T d[N];                                                                \
        u32 head;                                                              \
        u32 tail;                                                              \
        u32 size;                                                              \
    }

#define FIFO_MAX(f) (sizeof(f).d / sizeof(f).d[0])

#define FIFO_push(f, v)                                                        \
    ((f).d[(f).tail++] = v, (f).tail &= FIFO_MAX(f) - 1, (f).size++)
#define FIFO_pop(f, v)                                                         \
    (v = (f).d[(f).head++], (f).head &= FIFO_MAX(f) - 1, (f).size--)
#define FIFO_peek(f) ((f).d[(f).head])
#define FIFO_foreach(i, f)                                                     \
    for (u32 _i = 0, i = (f).head; _i < (f).size;                              \
         _i++, i = (i + 1) & (FIFO_MAX(f) - 1))
#define FIFO_clear(f) ((f).d[0] = (f).head = (f).tail = (f).size = 0)

#define Vector(T)                                                              \
    struct {                                                                   \
        T* d;                                                                  \
        size_t size;                                                           \
        size_t cap;                                                            \
    }

#define Vec_init(v) ((v).d = NULL, (v).size = 0, (v).cap = 0)
#define Vec_assn(v1, v2)                                                       \
    ((v1).d = (v2).d, (v1).size = (v2).size, (v1).cap = (v2).cap)
#define Vec_free(v) (free((v).d))

#define Vec_push(v, e)                                                         \
    ({                                                                         \
        if ((v).size == (v).cap) {                                             \
            (v).cap = (v).cap ? 2 * (v).cap : 8;                               \
            (v).d = realloc((v).d, (v).cap * sizeof *(v).d);                   \
        }                                                                      \
        (v).d[(v).size++] = (e);                                               \
        (v).size - 1;                                                          \
    })

#endif