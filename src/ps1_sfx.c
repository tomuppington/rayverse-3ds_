// 3DS port: PlayStation sound effects.
//
// If a SoundPS1 folder exists next to the game data, holding the sound banks converted from the player's own
// PS1 disc (FIX.SFX: the sounds used everywhere, plus the PS1 game's list of sounds; JUN/MUS/MON/IMG/CAV/CAK.SFX:
// each world's own sounds), the game plays those recordings instead of the PC ones, the way the PS1 does:
//  - the PS1 list of sounds (which recording, note and volume each of the game's 256 sounds uses) replaces the
//    PC one. Both versions number their recordings the same way; the PS1 one differs in some notes, volumes
//    and flags. Sounds the PS1 list leaves without a recording (it plays them another way, or not at all)
//    keep the PC recording.
//  - pitch, volume and left/right position are worked out as the PS1 sound chip does it: the note is played
//    relative to the recording's own centre note; the volume is the game's volume for the sound times the
//    instrument's and the bank's volume settings; sounds are placed left or right like on the PS1.
// Without the folder (or for any recording missing from it), the PC sounds are used as before.
// The converted files are made from the player's own disc and never go into the repository.

typedef struct ps1_sfx_tone_t {
    u8 vag, center, shift, vol, pan, min_note, max_note, pad;
    u16 adsr1, adsr2;
} ps1_sfx_tone_t;

typedef struct ps1_sfx_vag_t {
    s32 offset, length, loop_start;
} ps1_sfx_vag_t;

typedef struct ps1_sfx_bank_t {
    u8* file;            // the whole file (NULL: not loaded)
    u8 master_vol;
    u8 n_tones[128];
    u8 prog_vol[128];
    ps1_sfx_tone_t* tones; // 128 x 16
    ps1_sfx_vag_t* vags;
    s32 n_vags;
    s16* pcm;
    s32 pcm_count;
} ps1_sfx_bank_t;

static ps1_sfx_bank_t ps1_sfx_fixed, ps1_sfx_world;
static s32 ps1_sfx_world_loaded = -1;
bool ps1_sfx_enabled;            // the PS1 sounds are in use (FIX.SFX loaded)
static bool ps1_sfx_pc_sound[256]; // these sounds keep the PC recording (none in the PS1 list)
static sound_table_entry_t ps1_sfx_pc_table[256]; // the PC list, as it was
s16 keyon_snd = -1;              // set by the caller just before KeyOn(): which of the game's sounds it is

// Overall loudness of the PS1 sounds (1.0 = as the PS1 sound chip mixes them with the CD music).
#define PS1_SFX_GAIN 1.0f

static void ps1_sfx_free(ps1_sfx_bank_t* b) {
    if (b->file) free(b->file);
    memset(b, 0, sizeof(*b));
}

static bool ps1_sfx_load_file(ps1_sfx_bank_t* b, const char* name, u8* table_out) {
    ps1_sfx_free(b);
    char path[64];
    snprintf(path, sizeof(path), "SoundPS1/%s.SFX", name);
    FILE* fp = fopen(path, "rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    u8* data = (u8*)malloc(size);
    if (!data || fread(data, 1, size, fp) != (size_t)size || size < 12 || memcmp(data, "RAYSFX1", 8) != 0) {
        free(data);
        fclose(fp);
        return false;
    }
    fclose(fp);
    b->file = data;
    b->master_vol = data[8];
    u8 has_table = data[9];
    b->n_vags = data[10] | (data[11] << 8);
    s32 pos = 12;
    static ps1_sfx_tone_t tone_buf[2][128 * 16];
    b->tones = tone_buf[b == &ps1_sfx_world];
    for (s32 i = 0; i < 128; ++i) {
        b->n_tones[i] = data[pos];
        b->prog_vol[i] = data[pos + 1];
        pos += 4;
        for (s32 t = 0; t < 16; ++t) {
            memcpy(&b->tones[i * 16 + t], data + pos, sizeof(ps1_sfx_tone_t));
            pos += 12;
        }
    }
    b->vags = (ps1_sfx_vag_t*)(data + pos); // (4-byte aligned: 12 + 128 * 196)
    pos += b->n_vags * 12;
    if (has_table) {
        if (table_out) memcpy(table_out, data + pos, 256 * 5);
        pos += 256 * 5;
    }
    b->pcm = (s16*)(data + pos);
    b->pcm_count = (s32)((size - pos) / 2);
    return true;
}

// Stops the sounds playing from a bank about to be replaced.
static void ps1_sfx_stop_voices(ps1_sfx_bank_t* b) {
    if (!b->file) return;
    for (s32 i = 0; i < COUNT(digi_voices); ++i) {
        u8* d = digi_voices[i].data;
        if (digi_voices[i].is_playing && d >= (u8*)b->pcm && d < (u8*)(b->pcm + b->pcm_count)) {
            digi_voices[i].is_playing = false;
        }
    }
}

// With LoadBnkFixe(): the sounds used everywhere, and the PS1 list of sounds.
void ps1_sfx_load_fixed(void) {
    if (ps1_sfx_fixed.file) return; // (loaded once)
    static bool pc_table_saved;
    if (!pc_table_saved) {
        memcpy(ps1_sfx_pc_table, hard_sound_table, sizeof(ps1_sfx_pc_table));
        pc_table_saved = true;
    }
    u8 table[256 * 5];
    ps1_sfx_enabled = ps1_sfx_load_file(&ps1_sfx_fixed, "FIX", table);
    printf("Sound effects: %s\n", ps1_sfx_enabled ? "PlayStation (SoundPS1 folder)" : "PC");
    if (!ps1_sfx_enabled) return;
    FILE* yeah = fopen("MusicPS1/YEAH.ogg", "rb");
    bool yeah_jingle = yeah != NULL;
    if (yeah) fclose(yeah);
    for (s32 i = 0; i < 256; ++i) {
        const u8* e = table + i * 5; // note, recording, tone, volume, flags
        if (e[1] == 255 && ps1_sfx_pc_table[i].prog != 255) {
            // Sounds 104-109 are Rayman's voice at the end of a level (e.g. 107, a record scratch, with his
            // victory dance). The PS1 plays none of them: its level-won jingle ("Yeah!", MusicPS1/YEAH.ogg)
            // has his voice instead. With that jingle there, they stay silent, as on the PS1.
            if (i >= 104 && i <= 109 && yeah_jingle) {
                hard_sound_table[i].prog = 255;
                continue;
            }
            ps1_sfx_pc_sound[i] = true; // keep the PC recording
            continue;
        }
        hard_sound_table[i].note = e[0];
        hard_sound_table[i].prog = e[1];
        hard_sound_table[i].tone = e[2];
        hard_sound_table[i].volume = e[3];
        sound_table[i] = e[4];
    }
}

// With LoadBnkWorld(): the world's own sounds.
void ps1_sfx_load_world(s16 world) {
    if (!ps1_sfx_enabled || world == ps1_sfx_world_loaded) return;
    static const char* names[7] = {NULL, "JUN", "MUS", "MON", "IMG", "CAV", "CAK"};
    ps1_sfx_stop_voices(&ps1_sfx_world);
    ps1_sfx_world_loaded = -1;
    if (world >= 1 && world <= 6 && ps1_sfx_load_file(&ps1_sfx_world, names[world], NULL)) {
        ps1_sfx_world_loaded = world;
    }
}

// Fills in snd for a PS1 recording; false if this sound should use the PC recording.
bool ps1_sfx_setup(snd_t* snd, u8 bank, u8 prog, u8 tone, u8 note, u8 volume, u8 pan, bool loop_allowed) {
    if (!ps1_sfx_enabled || prog >= 128 || tone >= 16) return false;
    if (keyon_snd >= 0 && keyon_snd < 256 && ps1_sfx_pc_sound[keyon_snd]) return false;
    // The recording is in the bank the game says (fixed or world), or else in the other one.
    ps1_sfx_bank_t* banks[2] = {bank == 1 ? &ps1_sfx_world : &ps1_sfx_fixed, bank == 1 ? &ps1_sfx_fixed : &ps1_sfx_world};
    for (s32 k = 0; k < 2; ++k) {
        ps1_sfx_bank_t* b = banks[k];
        if (!b->file || tone >= b->n_tones[prog]) continue;
        ps1_sfx_tone_t* tn = b->tones + prog * 16 + tone;
        if (tn->vag == 0 || tn->vag >= b->n_vags) continue;
        ps1_sfx_vag_t* v = b->vags + tn->vag;
        if (v->length < 2 || v->offset + v->length > b->pcm_count) continue;
        snd->data = (u8*)(b->pcm + v->offset);
        snd->size = v->length * 2;
        snd->sample_count = v->length;
        snd->bytes_per_sample = 2;
        // The PS1 sound chip plays a recording at 44100 Hz for its centre note (shift: 1/128ths of a semitone).
        float semitones = (float)note - (float)tn->center - (float)tn->shift / 128.0f;
        snd->sample_rate = (s32)(44100.0f * powf(2.0f, semitones / 12.0f));
        if (loop_allowed && v->loop_start >= 0 && v->loop_start < v->length - 1) {
            snd->loop_start = v->loop_start;
            snd->loop_end = v->length;
        }
        snd->ps1_gain = (float)tn->vol / 127.0f * (float)b->prog_vol[prog] / 127.0f * (float)b->master_vol / 127.0f * PS1_SFX_GAIN;
        snd->volume = (float)volume / 127.0f * snd->ps1_gain;
        snd->ps1_pan = true;
        snd->pan_l = (float)snd_sqrt_table[127 - (pan & 127)] / 256.0f;
        snd->pan_r = (float)snd_sqrt_table[pan & 127] / 256.0f;
        return true;
    }
    return false;
}
