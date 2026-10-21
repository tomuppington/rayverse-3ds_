// Nintendo 3DS sound output for Rayverse (uses libctru's NDSP service).
//
// The game mixes all of its music and sound effects itself, once per frame, into a
// 16-bit stereo buffer at 44100 Hz (see game_get_sound_samples() in sound.c).
// All this file does is hand those samples to the 3DS's sound chip.
//
// NDSP needs the DSP firmware file (sdmc:/3ds/dspfirm.cdc), which is created by the
// "DSP1" homebrew tool. If it is missing, the game still runs, just without sound.

#define CTR_SND_CHANNEL        0
#define CTR_SND_RATE           44100
#define CTR_SND_NUM_BUFS       8
#define CTR_SND_MAX_SAMPLES    1024   // per buffer (one frame is ~735 samples)
#define CTR_SND_TARGET_QUEUED  3      // aim to keep about 3 frames (~50 ms) queued

static ndspWaveBuf ctr_snd_wavebufs[CTR_SND_NUM_BUFS];
static s16* ctr_snd_memory;

static bool ctr_init_sound(app_state_t* app_state) {
    ctr_sound_output_t* out = &app_state->ctr.sound_output;
    out->samples_per_second = CTR_SND_RATE;
    out->bytes_per_sample = sizeof(s16) * 2;
    out->ndsp_ok = false;

    if (R_FAILED(ndspInit())) {
        printf("Sound disabled: NDSP could not start.\n");
        printf("(Is sdmc:/3ds/dspfirm.cdc missing?\n Run the DSP1 homebrew once.)\n");
        return false;
    }

    ctr_snd_memory = (s16*)linearAlloc(CTR_SND_NUM_BUFS * CTR_SND_MAX_SAMPLES * out->bytes_per_sample);
    if (!ctr_snd_memory) {
        printf("Sound disabled: out of linear memory.\n");
        ndspExit();
        return false;
    }
    memset(ctr_snd_memory, 0, CTR_SND_NUM_BUFS * CTR_SND_MAX_SAMPLES * out->bytes_per_sample);

    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(CTR_SND_CHANNEL);
    ndspChnSetInterp(CTR_SND_CHANNEL, NDSP_INTERP_LINEAR);
    ndspChnSetRate(CTR_SND_CHANNEL, (float)CTR_SND_RATE);
    ndspChnSetFormat(CTR_SND_CHANNEL, NDSP_FORMAT_STEREO_PCM16);

    memset(ctr_snd_wavebufs, 0, sizeof(ctr_snd_wavebufs));
    for (s32 i = 0; i < CTR_SND_NUM_BUFS; ++i) {
        ctr_snd_wavebufs[i].data_vaddr = ctr_snd_memory + i * CTR_SND_MAX_SAMPLES * 2;
        ctr_snd_wavebufs[i].status = NDSP_WBUF_DONE;
    }

    out->ndsp_ok = true;
    return true;
}

static void ctr_shutdown_sound(app_state_t* app_state) {
    if (app_state->ctr.sound_output.ndsp_ok) {
        ndspChnWaveBufClear(CTR_SND_CHANNEL);
        ndspExit();
        app_state->ctr.sound_output.ndsp_ok = false;
    }
    if (ctr_snd_memory) {
        linearFree(ctr_snd_memory);
        ctr_snd_memory = NULL;
    }
}

static void ctr_produce_sound_for_frame(app_state_t* app_state, game_sound_buffer_t* game_sound_buffer) {
    ctr_sound_output_t* out = &app_state->ctr.sound_output;

    // The game assumes 60 frames per second (735 samples per frame), while the 3DS screen
    // refreshes at about 59.83 Hz. To avoid the queue slowly running dry (clicks) or piling up
    // (growing delay), we nudge the number of samples produced per frame. The output rate stays
    // at exactly 44100 Hz, so the pitch of music and sound effects is unaffected.
    s32 queued = 0;
    s32 free_index = -1;
    if (out->ndsp_ok) {
        for (s32 i = 0; i < CTR_SND_NUM_BUFS; ++i) {
            u8 status = ctr_snd_wavebufs[i].status;
            if (status == NDSP_WBUF_QUEUED || status == NDSP_WBUF_PLAYING) {
                ++queued;
            } else if (free_index < 0) {
                free_index = i;
            }
        }
    }

    u32 samples = CTR_SND_RATE / 60; // 735
    if (out->ndsp_ok) {
        if (queued < CTR_SND_TARGET_QUEUED - 1) {
            samples += 40;
        } else if (queued > CTR_SND_TARGET_QUEUED + 1) {
            samples -= 40;
        }
    }
    game_sound_buffer->sample_count = samples;

    // Always let the game advance its music and sound effects, even with no sound output,
    // so that timing-dependent logic (e.g. waiting for the intro music) still progresses.
    game_get_sound_samples(game_sound_buffer);

    if (!out->ndsp_ok || free_index < 0) {
        return; // no sound, or everything already queued (drop this frame's audio)
    }

    ndspWaveBuf* buf = &ctr_snd_wavebufs[free_index];
    memcpy(buf->data_pcm16, game_sound_buffer->samples, samples * out->bytes_per_sample);
    buf->nsamples = samples;
    DSP_FlushDataCache(buf->data_pcm16, samples * out->bytes_per_sample);
    ndspChnWaveBufAdd(CTR_SND_CHANNEL, buf);
}
