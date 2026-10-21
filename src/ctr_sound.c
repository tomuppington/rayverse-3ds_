// Nintendo 3DS sound output for Rayverse (uses libctru's NDSP service).
//
// The game mixes all of its music and sound effects itself into a 16-bit stereo buffer at
// 44100 Hz (see game_get_sound_samples() in sound.c). This file hands those samples to the
// 3DS's sound processor.
//
// Rather than producing a fixed amount of sound per video frame (which leaves gaps whenever a
// frame runs late), each frame we top the queue up to a steady level of about 60 ms. If a frame
// is slow, the next one simply produces more sound, so playback stays continuous.
//
// NDSP needs the DSP firmware file (sdmc:/3ds/dspfirm.cdc), which is created by the
// "DSP1" homebrew tool. If it is missing, the game still runs, just without sound.

#define CTR_SND_CHANNEL        0
#define CTR_SND_RATE           44100
#define CTR_SND_NUM_BUFS       12
#define CTR_SND_MAX_SAMPLES    4096   // per buffer
#define CTR_SND_TARGET_QUEUED  2646   // keep ~60 ms of sound queued (3.6 frames)
#define CTR_SND_FIRST_FILL     4410   // start with ~100 ms so the very first frames can't run dry

static ndspWaveBuf ctr_snd_wavebufs[CTR_SND_NUM_BUFS];
static s16* ctr_snd_memory;
static bool ctr_snd_started;
u32 ctr_snd_underruns; // times the queue ran completely dry (shown in the bottom-screen stats)

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

// How many samples are queued but not yet played.
static s32 ctr_snd_queued_samples(void) {
    s32 queued = 0;
    bool any_playing = false;
    for (s32 i = 0; i < CTR_SND_NUM_BUFS; ++i) {
        u8 status = ctr_snd_wavebufs[i].status;
        if (status == NDSP_WBUF_QUEUED || status == NDSP_WBUF_PLAYING) {
            queued += (s32)ctr_snd_wavebufs[i].nsamples;
            if (status == NDSP_WBUF_PLAYING) any_playing = true;
        }
    }
    if (any_playing) {
        queued -= (s32)ndspChnGetSamplePos(CTR_SND_CHANNEL); // part of the current buffer already played
    }
    return MAX(queued, 0);
}

static ndspWaveBuf* ctr_snd_free_buffer(void) {
    for (s32 i = 0; i < CTR_SND_NUM_BUFS; ++i) {
        u8 status = ctr_snd_wavebufs[i].status;
        if (status == NDSP_WBUF_FREE || status == NDSP_WBUF_DONE) {
            return ctr_snd_wavebufs + i;
        }
    }
    return NULL;
}

static void ctr_produce_sound_for_frame(app_state_t* app_state, game_sound_buffer_t* game_sound_buffer) {
    ctr_sound_output_t* out = &app_state->ctr.sound_output;

    if (!out->ndsp_ok) {
        // No sound output: still let the game advance its music and sound effects at the normal
        // rate, so timing-dependent logic (e.g. waiting for the intro music to end) progresses.
        game_sound_buffer->sample_count = CTR_SND_RATE / 60;
        game_get_sound_samples(game_sound_buffer);
        return;
    }

    s32 queued = ctr_snd_queued_samples();
    if (ctr_snd_started && queued == 0) {
        ++ctr_snd_underruns;
    }
    s32 target = ctr_snd_started ? CTR_SND_TARGET_QUEUED : CTR_SND_FIRST_FILL;
    s32 needed = target - queued;

    while (needed > 0) {
        ndspWaveBuf* buf = ctr_snd_free_buffer();
        if (!buf) break; // every buffer is in use; try again next frame
        s32 samples = MIN(needed, CTR_SND_MAX_SAMPLES);
        game_sound_buffer->sample_count = samples;
        game_get_sound_samples(game_sound_buffer);
        memcpy(buf->data_pcm16, game_sound_buffer->samples, samples * out->bytes_per_sample);
        buf->nsamples = samples;
        DSP_FlushDataCache(buf->data_pcm16, samples * out->bytes_per_sample);
        ndspChnWaveBufAdd(CTR_SND_CHANNEL, buf);
        needed -= samples;
    }
    ctr_snd_started = true;
}
