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
    bool adpcm_hi[16];
    float adpcm_sample[16];
    int adpcm_idx[16];
    float adpcm_sample_loopstart[16];
    int adpcm_idx_loopstart[16];
    u8 psg_ctr[6];
    u16 psg_lfsr[2];

    u32 capture_ptrs[2];

    float channel_samples[16][2];
    float cap_channel_samples[4];
    float mixer_sample[2];

} SPU;

void generate_adpcm_table();

void spu_tick_channel(SPU* spu, int i);
void spu_tick_capture(SPU* spu, int i);

void spu_sample(SPU* spu);

#endif