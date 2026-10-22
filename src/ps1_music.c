// PS1 soundtrack support (added for the 3DS port).
//
// The PC version plays one CD track per world. The PlayStation version has its own soundtrack with
// music chosen per level, plus music for death, level completion, game over and so on. If the folder
// MusicPS1/ exists next to the game data (OGG files converted from the player's own PS1 disc, named
// after the music files on that disc, e.g. MusicPS1/JUNGLE1.ogg), this file plays the PS1 music
// instead, following the PS1 game's own logic.
//
// The table below is the level music table from the European PS1 executable (SLES-000.49), which
// matches the US one documented by the rayman-ps1-decomp project (PS1_LevelMusicTable) except for
// one entry. Each level has up to 4 commands, run in order:
//   cmd 1: play a track (param = index into ps1_track_names).
//          flag 0x20: play an 18.8 s excerpt starting 4/8/12/16 s in (used for ambient tracks)
//          flag 0x40: fade in
//   cmd 3: jump to command <param> (used to loop)
//   cmd 0: stop
// When a track (or excerpt) ends, the next command runs - e.g. Jungle level 1 plays JUNGLE1, then an
// excerpt of the jungle ambience, then loops.

typedef struct ps1_music_cmd_t {
    u8 cmd_and_flags;
    u8 param;
} ps1_music_cmd_t;

static const ps1_music_cmd_t ps1_level_music[7][22][4] = {
    { // world 0 (special: death, map, intro, ...)
        {{0x01, 6}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 0
        {{0x01, 7}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 1
        {{0x01, 5}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 2
        {{0x01, 2}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 3
        {{0x01, 47}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 4
        {{0x01, 4}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 5
        {{0x01, 1}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 6
        {{0x01, 48}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 7
        {{0x01, 29}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 8
        {{0x01, 29}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 9
        {{0x01, 38}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 10
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 11
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 12
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 13
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 14
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 15
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 16
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 17
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 18
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 19
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 20
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 21
    },
    { // world 1
        {{0x35, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 0
        {{0x81, 8}, {0xE1, 35}, {0x03, 0}, {0x00, 0}}, // 1
        {{0x81, 35}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 2
        {{0x81, 0}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 3
        {{0x81, 9}, {0xE1, 35}, {0x03, 0}, {0x00, 0}}, // 4
        {{0x81, 8}, {0x81, 9}, {0x03, 0}, {0x00, 0}}, // 5
        {{0x81, 35}, {0x03, 0}, {0x01, 10}, {0x03, 2}}, // 6
        {{0x81, 11}, {0x03, 0}, {0x01, 12}, {0x03, 2}}, // 7
        {{0x81, 0}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 8
        {{0x81, 35}, {0x03, 0}, {0x81, 46}, {0x03, 2}}, // 9
        {{0x81, 9}, {0x81, 8}, {0x03, 0}, {0x00, 0}}, // 10
        {{0x81, 9}, {0x81, 11}, {0x03, 0}, {0x00, 0}}, // 11
        {{0x81, 8}, {0xA1, 35}, {0x03, 0}, {0x00, 0}}, // 12
        {{0x81, 35}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 13
        {{0x81, 1}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 14
        {{0x81, 11}, {0xA1, 35}, {0x03, 0}, {0x00, 0}}, // 15
        {{0x81, 49}, {0x03, 0}, {0x03, 0}, {0x03, 0}}, // 16
        {{0x81, 0}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 17
        {{0x81, 3}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 18
        {{0x81, 3}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 19
        {{0x81, 3}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 20
        {{0x81, 3}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 21
    },
    { // world 2
        {{0x35, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 0
        {{0x81, 14}, {0xA1, 36}, {0x03, 0}, {0x00, 0}}, // 1
        {{0x81, 15}, {0xA1, 36}, {0x03, 0}, {0x00, 0}}, // 2
        {{0x81, 16}, {0xA1, 36}, {0x03, 0}, {0x00, 0}}, // 3
        {{0x81, 37}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 4
        {{0x81, 15}, {0x81, 16}, {0x03, 0}, {0x00, 0}}, // 5
        {{0x81, 50}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 6
        {{0xC1, 36}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 7
        {{0x81, 14}, {0xE1, 36}, {0x03, 0}, {0x00, 0}}, // 8
        {{0x81, 36}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 9
        {{0x81, 20}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 10
        {{0x81, 0}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 11
        {{0x81, 17}, {0x81, 18}, {0x03, 0}, {0x00, 0}}, // 12
        {{0x81, 1}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 13
        {{0x81, 14}, {0x81, 20}, {0x03, 0}, {0x00, 0}}, // 14
        {{0x81, 51}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 15
        {{0x81, 51}, {0x03, 0}, {0x03, 0}, {0x03, 0}}, // 16
        {{0x81, 3}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 17
        {{0x81, 3}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 18
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 19
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 20
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 21
    },
    { // world 3
        {{0x35, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 0
        {{0xC1, 38}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 1
        {{0xC1, 38}, {0x03, 0}, {0x81, 23}, {0x03, 0}}, // 2
        {{0x81, 21}, {0x81, 22}, {0x03, 0}, {0x00, 0}}, // 3
        {{0x81, 22}, {0xE1, 38}, {0x03, 0}, {0x00, 0}}, // 4
        {{0x81, 38}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 5
        {{0x81, 38}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 6
        {{0x81, 1}, {0x03, 0}, {0xC1, 38}, {0x03, 2}}, // 7
        {{0x81, 13}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 8
        {{0x81, 39}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 9
        {{0x81, 23}, {0x81, 23}, {0x03, 0}, {0x03, 0}}, // 10
        {{0x81, 0}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 11
        {{0x81, 3}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 12
        {{0x81, 3}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 13
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 14
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 15
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 16
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 17
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 18
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 19
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 20
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 21
    },
    { // world 4
        {{0x35, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 0
        {{0xC1, 43}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 1
        {{0x81, 24}, {0xE1, 40}, {0x03, 0}, {0x00, 0}}, // 2
        {{0x81, 40}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 3
        {{0x81, 26}, {0x03, 0}, {0x03, 0}, {0x03, 0}}, // 4
        {{0x81, 24}, {0xE1, 40}, {0x03, 0}, {0x00, 0}}, // 5
        {{0x81, 24}, {0x81, 25}, {0x03, 0}, {0x00, 0}}, // 6
        {{0xC1, 40}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 7
        {{0x81, 25}, {0x81, 24}, {0x03, 0}, {0x00, 0}}, // 8
        {{0xC1, 37}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 9
        {{0x81, 1}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 10
        {{0x81, 27}, {0x03, 0}, {0x03, 0}, {0x03, 0}}, // 11
        {{0x81, 3}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 12
        {{0x81, 3}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 13
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 14
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 15
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 16
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 17
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 18
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 19
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 20
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 21
    },
    { // world 5
        {{0x35, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 0
        {{0x81, 28}, {0xE1, 32}, {0x03, 0}, {0x00, 0}}, // 1
        {{0x81, 52}, {0xA1, 32}, {0x03, 0}, {0x00, 0}}, // 2
        {{0x81, 29}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 3
        {{0xC1, 32}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 4
        {{0x81, 52}, {0x81, 28}, {0x03, 0}, {0x00, 0}}, // 5
        {{0xC1, 32}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 6
        {{0x81, 52}, {0xA1, 32}, {0x03, 0}, {0x00, 0}}, // 7
        {{0x81, 29}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 8
        {{0x81, 30}, {0xE1, 32}, {0x03, 0}, {0x00, 0}}, // 9
        {{0x81, 31}, {0xE1, 32}, {0x03, 0}, {0x00, 0}}, // 10
        {{0x81, 31}, {0xE1, 32}, {0x03, 0}, {0x03, 0}}, // 11
        {{0x81, 3}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 12
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 13
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 14
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 15
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 16
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 17
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 18
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 19
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 20
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 21
    },
    { // world 6
        {{0x35, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 0
        {{0x81, 33}, {0xE1, 42}, {0x03, 0}, {0x00, 0}}, // 1
        {{0x81, 42}, {0x03, 0}, {0x00, 0}, {0x00, 0}}, // 2
        {{0x81, 34}, {0xC1, 42}, {0x03, 0}, {0x00, 0}}, // 3
        {{0x81, 19}, {0x03, 0}, {0x03, 0}, {0x03, 0}}, // 4
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 5
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 6
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 7
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 8
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 9
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 10
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 11
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 12
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 13
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 14
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 15
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 16
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 17
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 18
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 19
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 20
        {{0x00, 0}, {0x00, 0}, {0x00, 0}, {0x00, 0}}, // 21
    },
};

static const char* ps1_track_names[53] = {
    "FEE",
    "SUSPENC",
    "RAP",
    "MAGIC",
    "REPLAY",
    "GAMEOV",
    "GAGNE",
    "PERDU",
    "JUNGLE1",
    "JUNGLE2",
    "JUNMOST",
    "JUNMOS2",
    "JUNMOSB",
    "PLUIEEAU",
    "MUSTHEM",
    "MUSPER1",
    "MUSPER2",
    "MUSCHIN",
    "MUS_ZEN",
    "MUSDARK",
    "MUS_SAX",
    "MONTAGN",
    "MONTAG1",
    "MONSTON",
    "IMAJOCD",
    "IMAJOC1",
    "IMABATO",
    "IMASPAC",
    "CAVJOE1",
    "CAVJOE2",
    "CAVGROT",
    "CAVSKOP",
    "CAV_AMB",
    "GATCIRQ",
    "GATGATO",
    "JUN_AMB",
    "MUS_AMB",
    "PLUIEORG",
    "MON_AMB",
    "MONRAFA",
    "IMA_AMB",
    "EAUMONT",
    "GAT_AMB",
    "ENCRE",
    "EAUPUIT",
    "TESMORT",
    "EAUSUSP",
    "INTRO_JP",
    "GENE",
    "MOUSTIC",
    "DRUMS",
    "MR_SAX",
    "GROTTE2"
};

static const s32 ps1_excerpt_offsets[4] = {300, 900, 600, 1200}; // in CD sectors (1/75 s), from the PS1 code
#define PS1_EXCERPT_SECTORS 1410
#define PS1_SAMPLES_PER_SECTOR 588 // 44100 Hz / 75 sectors per second
#define PS1_FADE_IN_SAMPLES (44100 * 3 / 2)

bool ps1_music_enabled;
static s16 ps1_music_world;
static s16 ps1_music_level;
static s16 ps1_music_cmd;
static s16 ps1_excerpt_index;
static bool ps1_scroll_locked_audio; // PS1_ScrollLockedAudio
static bool ps1_seed_audio;          // D_801E64B0 in the PS1 code: flood music already started
static bool ps1_death_music_armed = true; // PS1_CanPlayDeathMusic
static bool ps1_level_music_after_jingle; // restart the level music once the death jingle ends

void ps1_music_init(void) {
    FILE* fp = fopen("MusicPS1/JUNGLE1.ogg", "rb");
    if (!fp) fp = fopen("MusicPS1/GENE.ogg", "rb");
    ps1_music_enabled = (fp != NULL);
    if (fp) fclose(fp);
    printf("Music: %s\n", ps1_music_enabled ? "PlayStation soundtrack (MusicPS1 folder)" : "PC CD soundtrack");
}

static bool ps1_open_track(s32 index, bool excerpt, bool fade_in) {
    stop_ogg(&ogg_cd_track);
    if (index < 0 || index >= (s32)COUNT(ps1_track_names)) return false;
    char filename[64];
    snprintf(filename, sizeof(filename), "MusicPS1/%s.ogg", ps1_track_names[index]);
    FILE* fp = fopen(filename, "rb");
    if (!fp) return false; // e.g. the 3 tracks that only exist on some versions of the disc
    setvbuf(fp, NULL, _IOFBF, 32 * 1024);
    int error = 0;
    stb_vorbis* decoder = stb_vorbis_open_file(fp, 1, &error, NULL);
    if (!decoder) {
        fclose(fp);
        return false;
    }
    ogg_t result = {0};
    result.decoder = decoder;
    result.sample_count = stb_vorbis_stream_length_in_samples(decoder);
    result.ps1 = true;
    result.samples_left = -1;
    if (excerpt) {
        s32 start = ps1_excerpt_offsets[ps1_excerpt_index] * PS1_SAMPLES_PER_SECTOR;
        ps1_excerpt_index = (ps1_excerpt_index + 1) % (s16)COUNT(ps1_excerpt_offsets);
        if ((u32)start < result.sample_count) {
            stb_vorbis_seek(decoder, (unsigned int)start);
        }
        result.samples_left = PS1_EXCERPT_SECTORS * PS1_SAMPLES_PER_SECTOR;
    }
    if (fade_in) {
        result.fade_total = PS1_FADE_IN_SAMPLES;
    }
    ogg_cd_track = result;
    is_ogg_playing = true;
    return true;
}

// Run commands from the next one until a track starts or the list stops. Returns true if playing.
static bool ps1_music_run(void) {
    if (++ps1_music_cmd >= 4) ps1_music_cmd = 0;
    for (s32 guard = 0; guard < 8; ++guard) {
        const ps1_music_cmd_t* c = &ps1_level_music[ps1_music_world][ps1_music_level][ps1_music_cmd];
        switch (c->cmd_and_flags & 0xF) {
            case 1:
                if (ps1_open_track(c->param, (c->cmd_and_flags & 0x20) != 0, (c->cmd_and_flags & 0x40) != 0)) {
                    return true;
                }
                is_ogg_playing = false;
                return false;
            case 3:
                ps1_music_cmd = c->param;
                if (ps1_music_cmd >= 4) ps1_music_cmd = 0;
                continue;
            default: // 0: stop (2: synthesized music, not available as CD audio)
                stop_ogg(&ogg_cd_track);
                is_ogg_playing = false;
                return false;
        }
    }
    is_ogg_playing = false;
    return false;
}

// Start the music for a world/level (world 0 = special music, see the table).
void ps1_play_music(s16 world, s16 level) {
    if (world < 0 || world >= 7 || level < 0 || level >= 22) return;
    ps1_music_world = world;
    ps1_music_level = level;
    ps1_music_cmd = -1;
    ps1_scroll_locked_audio = false;
    if (world != 0) {
        ps1_seed_audio = false;
        ps1_level_music_after_jingle = false;
        ps1_death_music_armed = true;
    }
    MusicCdActive = true;
    ps1_music_run();
}

// Called by play_ogg() when the current track or excerpt has finished.
bool ps1_music_next_segment(void) {
    if (ps1_level_music_after_jingle && ps1_music_world == 0 && ps1_music_level == 1) {
        // The death jingle has finished and Rayman is already back in the level.
        ps1_level_music_after_jingle = false;
        ps1_play_music(num_world, num_level);
        return is_ogg_playing;
    }
    return ps1_music_run();
}

static bool ps1_death_jingle_playing(void) {
    return ps1_music_world == 0 && ps1_music_level == 1 && is_ogg_playing && ogg_cd_track.decoder;
}

// Called each time Rayman (re)starts a level attempt, i.e. after every death too. The PS1 restarts the
// level music from the beginning after a death, once the death jingle has finished (DEPART_DEAD_LOOP
// and PS1_StopLevelMusic in the PS1 code). The first attempt's music is started by TestCdLoop.
void ps1_music_level_attempt_start(void) {
    if (!ps1_death_music_armed) {
        ps1_death_music_armed = true;
        if (ps1_death_jingle_playing()) {
            ps1_level_music_after_jingle = true;
        } else {
            ps1_play_music(num_world, num_level);
        }
    }
}

// The PS1 starts the flood music in Tarayzan's level the first time Rayman plants a magic seed while
// the water is there (in RAY_SWIP / FUN_8013202c in the PS1 code): the level's second music section.
void ps1_music_seed_planted(void) {
    if (!ps1_seed_audio) {
        ps1_seed_audio = true;
        ps1_music_cmd = 1;
        ps1_music_run();
    }
}

// Boss beaten: the PS1 fades the music out over 90 frames (FUN_80131e40 / PS1_Music_Apply_Fade).
void ps1_music_fade_out(void) {
    if (ogg_cd_track.decoder && ogg_cd_track.fade_out_total == 0) {
        ogg_cd_track.fade_out_total = 44100 * 90 / 60;
        ogg_cd_track.fade_out_left = ogg_cd_track.fade_out_total;
    }
}

// Called every frame during a level. When the screen scroll locks (boss arenas), the PS1 switches to the level's second music section
// (change_audio_track_puit in the PS1 code), once per level start.
void ps1_music_update_in_level(bool rayman_dying) {
    // Death music: the PS1 main loop starts it as soon as Rayman starts dying (dead_time != 64).
    if (rayman_dying && ps1_death_music_armed) {
        ps1_death_music_armed = false;
        ps1_level_music_after_jingle = false;
        ps1_play_music(0, 1);
        return;
    }
    if (!ps1_death_music_armed) return; // don't switch sections during the death jingle
    if (scrollLocked && !ps1_scroll_locked_audio) {
        ps1_scroll_locked_audio = true;
        ps1_music_cmd = 1;
        ps1_music_run();
    }
}

// Moskito ride: the PS1 switches to faster music when the mosquito accelerates, and back.
void ps1_music_moskito(bool accelerate) {
    ps1_music_cmd = accelerate ? 1 : -1;
    ps1_music_run();
}
