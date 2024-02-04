#include "spu.h"

#include "bus7.h"
#include "io.h"
#include "nds.h"

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
        case SND_ADPCM:
            break;
        case SND_PSG:
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
            l_sample /= 2;
            r_sample /= 2;
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