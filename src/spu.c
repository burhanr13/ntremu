#include "spu.h"

#include "bus7.h"
#include "io.h"
#include "nds.h"

float adpcm_table[89];
const int adpcm_ind_table[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

void generate_adpcm_table() {
    float x = 0x000776d2 / (float) (u32) (1 << 31);
    for (int i = 0; i < 89; i++) {
        adpcm_table[i] = x;
        x += x / 10;
    }
}

void spu_reload_channel(SPU* spu, int i) {
    u32 loopstart = (spu->master->io7.sound[i].sad & 0x7fffffc) +
                    (spu->master->io7.sound[i].pnt << 2);
    u32 loopend = loopstart + ((spu->master->io7.sound[i].len << 2) & 0xffffff);

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
            if(data & 8) {
                cur_sample -= diff;
            }else{
                cur_sample += diff;
            }
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
                }
            }
            break;
    }
    if (spu->sample_ptrs[i] >= loopend) {
        if (spu->master->io7.sound[i].cnt.repeat == REP_LOOP) {
            spu->sample_ptrs[i] = loopstart;
        }
        if (spu->master->io7.sound[i].cnt.repeat == REP_ONESHOT) {
            spu->master->io7.sound[i].cnt.start = 0;
        }
    }

    int vol_div = spu->master->io7.sound[i].cnt.volume_div;
    cur_sample /= (1 << vol_div);
    if (vol_div == 3) cur_sample /= 2;
    cur_sample *= spu->master->io7.sound[i].cnt.volume / (float) 128;

    spu->channel_samples[i] = cur_sample;

    int tmr = 0x10000 - spu->master->io7.sound[i].tmr;
    if (spu->master->io7.sound[i].cnt.start) {
        add_event(&spu->master->sched, EVENT_SPU_CH0 + i,
                  spu->master->sched.now + 2 * tmr);
    }
}

void spu_sample(SPU* spu) {
    if (spu->master->io7.soundcnt.enable) {

        float l_sample = 0, r_sample = 0;

        for (int i = 0; i < 16; i++) {
            if (!spu->master->io7.sound[i].cnt.start) continue;

            float pan = spu->master->io7.sound[i].cnt.pan / (float) 128;
            l_sample += spu->channel_samples[i] * (1 - pan);
            r_sample += spu->channel_samples[i] * pan;
        }

        l_sample /= 16;
        r_sample /= 16;

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