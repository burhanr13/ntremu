#include "spu.h"

#include "bus7.h"
#include "io.h"
#include "nds.h"

float adpcm_table[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,
    19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
    876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
    5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
const int adpcm_ind_table[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

#define CLAMP_SAMPLE(x) (x = (x < -1) ? -1 : ((x > 1) ? 1 : x))

void generate_adpcm_table() {
    for (int i = 0; i < 89; i++) {
        adpcm_table[i] /= 0x8000;
    }
}

void spu_tick_channel(SPU* spu, int i) {
    if (!spu->master->io7.sound[i].cnt.start) {
        if (!spu->master->io7.sound[i].cnt.hold) {
            if (i < 4) spu->cap_channel_samples[i] = 0;
            spu->channel_samples[i][0] = 0;
            spu->channel_samples[i][1] = 0;
        }
        return;
    }

    u32 loopstart = (spu->master->io7.sound[i].sad & 0x7fffffc) +
                    (spu->master->io7.sound[i].pnt << 2);
    u32 loopend = loopstart + ((spu->master->io7.sound[i].len << 2) & 0xffffff);

    if (spu->sample_ptrs[i] == loopstart && !spu->adpcm_hi[i]) {
        spu->adpcm_sample_loopstart[i] = spu->adpcm_sample[i];
        spu->adpcm_idx_loopstart[i] = spu->adpcm_idx[i];
    }

    float cur_sample = 0;

    switch (spu->master->io7.sound[i].cnt.format) {
        case SND_PCM8:
            cur_sample = (s8) bus7_read8(spu->master, spu->sample_ptrs[i]) /
                         (float) 0x80;
            spu->sample_ptrs[i] += 1;
            break;
        case SND_PCM16:
            cur_sample = (s16) bus7_read16(spu->master, spu->sample_ptrs[i]) /
                         (float) 0x8000;
            spu->sample_ptrs[i] += 2;
            break;
        case SND_ADPCM: {
            u8 data = bus7_read8(spu->master, spu->sample_ptrs[i]);
            if (spu->adpcm_hi[i]) {
                spu->adpcm_hi[i] = false;
                spu->sample_ptrs[i] += 1;
                data >>= 4;
            } else {
                spu->adpcm_hi[i] = true;
                data &= 0xf;
            }

            float diff = ((data & 7) * 2 + 1) * adpcm_table[spu->adpcm_idx[i]];
            cur_sample = spu->adpcm_sample[i];
            if (data & 8) {
                cur_sample -= diff;
            } else {
                cur_sample += diff;
            }
            CLAMP_SAMPLE(cur_sample);
            spu->adpcm_idx[i] += adpcm_ind_table[data & 7];
            if (spu->adpcm_idx[i] < 0) spu->adpcm_idx[i] = 0;
            if (spu->adpcm_idx[i] > 88) spu->adpcm_idx[i] = 88;
            spu->adpcm_sample[i] = cur_sample;
            break;
        }
        case SND_PSG:
            if (i >= 8) {
                if (i < 14) {
                    cur_sample = ((spu->psg_ctr[i - 8]++ & 7) <=
                                  spu->master->io7.sound[i].cnt.duty)
                                     ? 1
                                     : -1;
                } else {
                    if (spu->psg_lfsr[i - 14] & 1) {
                        cur_sample = -1;
                        spu->psg_lfsr[i - 14] >>= 1;
                        spu->psg_lfsr[i - 14] ^= 0x6000;
                    } else {
                        cur_sample = 1;
                        spu->psg_lfsr[i - 14] >>= 1;
                    }
                }
            }
            break;
    }
    if (spu->sample_ptrs[i] >= loopend) {
        if (spu->master->io7.sound[i].cnt.repeat == REP_LOOP) {
            spu->sample_ptrs[i] = loopstart;
            spu->adpcm_sample[i] = spu->adpcm_sample_loopstart[i];
            spu->adpcm_idx[i] = spu->adpcm_idx_loopstart[i];
        }
        if (spu->master->io7.sound[i].cnt.repeat == REP_ONESHOT) {
            spu->master->io7.sound[i].cnt.start = 0;
        }
    }

    int vol_div = spu->master->io7.sound[i].cnt.volume_div;
    cur_sample /= (1 << vol_div);
    if (vol_div == 3) cur_sample /= 2;
    cur_sample *= spu->master->io7.sound[i].cnt.volume / (float) 128;

    if (i == 0 && spu->master->io7.sndcapcnt[0].add &&
        spu->master->io7.sndcapcnt[0].start) {
        cur_sample += spu->cap_channel_samples[1];
        CLAMP_SAMPLE(cur_sample);
    }
    if (i == 2 && spu->master->io7.sndcapcnt[1].add &&
        spu->master->io7.sndcapcnt[1].start) {
        cur_sample += spu->cap_channel_samples[3];
        CLAMP_SAMPLE(cur_sample);
    }

    if (i < 4) spu->cap_channel_samples[i] = cur_sample;

    float pan = spu->master->io7.sound[i].cnt.pan / (float) 128;
    spu->channel_samples[i][0] = cur_sample * (1 - pan);
    spu->channel_samples[i][1] = cur_sample * pan;

    int tmr = 0x10000 - spu->master->io7.sound[i].tmr;
    add_event(&spu->master->sched, EVENT_SPU_CH0 + i,
              spu->master->sched.now + 2 * tmr);
}

void spu_tick_capture(SPU* spu, int i) {
    u32 loopstart = spu->master->io7.sndcap[i].dad & 0x7fffffc;
    u32 loopend = loopstart + ((spu->master->io7.sndcap[i].len << 2) & 0x3ffff);

    float sample = spu->master->io7.sndcapcnt[i].src
                       ? spu->cap_channel_samples[i << 1]
                       : spu->mixer_sample[i];
    int pcm = sample * 0x8000;
    if (pcm > 0x7fff) pcm = 0x7fff;
    if (pcm < -0x8000) pcm = -0x8000;
    if (spu->master->io7.sndcapcnt[i].format) {
        bus7_write8(spu->master, spu->capture_ptrs[i], pcm >> 8);
        spu->capture_ptrs[i] += 1;
    } else {
        bus7_write16(spu->master, spu->capture_ptrs[i], pcm);
        spu->capture_ptrs[i] += 2;
    }
    if (spu->capture_ptrs[i] >= loopend) {
        if (spu->master->io7.sndcapcnt[i].repeat) {
            spu->master->io7.sndcapcnt[i].start = 0;
        } else {
            spu->capture_ptrs[i] = loopstart;
        }
    }

    int tmr = 0x10000 - spu->master->io7.sound[2 * i + 1].tmr;
    if (spu->master->io7.sndcapcnt[i].start) {
        add_event(&spu->master->sched, EVENT_SPU_CAP0 + i,
                  spu->master->sched.now + 2 * tmr);
    }
}

void spu_sample(SPU* spu) {
    if (spu->master->io7.soundcnt.enable) {

        spu->mixer_sample[0] = 0;
        spu->mixer_sample[1] = 0;
        for (int i = 0; i < 16; i++) {
            if (!spu->master->io7.sound[i].cnt.start) continue;
            if (i == 1 && spu->master->io7.soundcnt.ch1) continue;
            if (i == 3 && spu->master->io7.soundcnt.ch3) continue;

            spu->mixer_sample[0] += spu->channel_samples[i][0];
            spu->mixer_sample[1] += spu->channel_samples[i][1];
        }
        CLAMP_SAMPLE(spu->mixer_sample[0]);
        CLAMP_SAMPLE(spu->mixer_sample[1]);

        float l_sample = spu->mixer_sample[0], r_sample = spu->mixer_sample[1];
        switch (spu->master->io7.soundcnt.left) {
            case 0:
                l_sample = spu->mixer_sample[0];
                break;
            case 1:
                l_sample = spu->channel_samples[1][0];
                break;
            case 2:
                l_sample = spu->channel_samples[3][0];
                break;
            case 3:
                l_sample =
                    spu->channel_samples[1][0] + spu->channel_samples[3][0];
                CLAMP_SAMPLE(l_sample);
                break;
        }
        switch (spu->master->io7.soundcnt.right) {
            case 0:
                r_sample = spu->mixer_sample[1];
                break;
            case 1:
                r_sample = spu->channel_samples[1][1];
                break;
            case 2:
                r_sample = spu->channel_samples[3][1];
                break;
            case 3:
                r_sample =
                    spu->channel_samples[1][1] + spu->channel_samples[3][1];
                CLAMP_SAMPLE(r_sample);
                break;
        }

        l_sample *= spu->master->io7.soundcnt.volume / (float) 128;
        r_sample *= spu->master->io7.soundcnt.volume / (float) 128;

        spu->sample_buf[spu->sample_idx++] = l_sample;
        spu->sample_buf[spu->sample_idx++] = r_sample;

    } else {
        spu->sample_buf[spu->sample_idx++] = 0;
        spu->sample_buf[spu->sample_idx++] = 0;
    }
    if (spu->sample_idx == SAMPLE_BUF_LEN) {
        spu->sample_idx = 0;
        spu->master->samples_full = true;
    }
    add_event(&spu->master->sched, EVENT_SPU_SAMPLE,
              spu->master->sched.now + BUS_CLK / SAMPLE_FREQ);
}