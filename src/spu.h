#ifndef SPU_H
#define SPU_H

#include "types.h"

#define SAMPLE_BUF_LEN 1024
#define SAMPLE_FREQ (1 << 15)

#define BUS_CLK (1 << 25)

enum { REP_MANUAL, REP_LOOP, REP_ONESHOT };
enum { SND_PCM8, SND_PCM16, SND_ADPCM, SND_PSG };

typedef struct _NDS NDS;
typedef struct {
    NDS* master;

    float sample_buf[SAMPLE_BUF_LEN];
    int sample_idx;

    u32 sample_ptrs[16];
    float channel_samples[16];

} SPU;

void spu_reload_channel(SPU* spu, int i);
void spu_sample(SPU* spu);

#endif