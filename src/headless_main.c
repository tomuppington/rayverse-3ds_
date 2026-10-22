// Headless test platform for Rayverse (no window, no audio device).
//
// Used for automated testing on a PC: the game runs as fast as possible, button presses come from
// a script, and screenshots / audio output are written to files. It implements the same small
// interface as the 3DS layer (ctr_advance_frame etc.), so the game code is exercised the same way.
//
// Build (from the repository root), e.g.:
//   gcc -O1 -g -std=gnu11 -fsigned-char -DRAYVERSE_HEADLESS src/rayverse.c src/stb_vorbis.c -lm -o rayverse_headless
// Run:
//   ./rayverse_headless <game data folder> <script file> <output folder>
//
// Script lines (frame numbers count up from 0; '#' starts a comment):
//   <frame> down <KEY>      press and hold a key
//   <frame> up <KEY>        release a key
//   <frame> tap <KEY>       press for 4 frames
//   <frame> shot <name>     save a screenshot as <output>/<name>.ppm
//   <frame> mark <text>     write a marker line to the log
//   <frame> end             stop
// KEY is one of: UP DOWN LEFT RIGHT JUMP PUNCH ACTION ENTER ESCAPE PAUSE TAB SPACE, or a letter A-Z

#define CTR_TOP_W 400
#define CTR_TOP_H 240
#define HEADLESS_RATE 44100

typedef struct headless_event_t {
    s32 frame;
    char kind[8];
    char arg[64];
} headless_event_t;

static headless_event_t headless_events[4096];
#include "input_assist.c"

static s32 headless_event_count;
static s32 headless_next_event;
static s64 headless_frame;
static const char* headless_out_dir = ".";
static FILE* headless_audio_file;
static FILE* headless_log;
static s32 headless_release_at[256];
static bool headless_voice_was_playing[64];
static u8* headless_voice_data[64];

s64 get_clock(void) {
    return headless_frame * (1000000000ll / 60);
}

float get_seconds_elapsed(s64 start, s64 end) {
    return (float)(end - start) / 1e9f;
}

void message_box(const char* message) {
    fprintf(headless_log, "[frame %lld] MESSAGE: %s\n", (long long)headless_frame, message);
    printf("MESSAGE: %s\n", message);
}

void ctr_fatal_error(void) {
    fprintf(headless_log, "[frame %lld] FATAL ERROR\n", (long long)headless_frame);
    fflush(headless_log);
    printf("FATAL ERROR at frame %lld\n", (long long)headless_frame);
    _Exit(2);
}

void surface_clear(surface_t* surface) {
    memset(surface->memory, 0, surface->memory_size);
}

void surface_resize(surface_t* surface, int width, int height) {
    surface->width = width;
    surface->height = height;
    surface->bytes_per_pixel = 4;
    surface->width_pow2 = width;
    surface->height_pow2 = height;
    surface->pitch = surface->width * surface->bytes_per_pixel;
    u32 memory_needed = surface->width * surface->height * surface->bytes_per_pixel;
    if (memory_needed > surface->memory_size) {
        surface->memory = (u8*)realloc(surface->memory, memory_needed);
        surface->memory_size = memory_needed;
    }
    surface_clear(surface);
}

static u8 headless_key_scancode(const char* name) {
    static const struct { const char* name; u8 sc; } keys[] = {
        {"UP", SC_UP}, {"DOWN", SC_DOWN}, {"LEFT", SC_LEFT}, {"RIGHT", SC_RIGHT},
        {"JUMP", SC_CONTROL}, {"PUNCH", SC_ALT}, {"ACTION", SC_X}, {"ENTER", SC_ENTER},
        {"ESCAPE", SC_ESCAPE}, {"PAUSE", SC_NUMLOCK}, {"TAB", SC_TAB}, {"SPACE", SC_SPACE},
    };
    for (u32 i = 0; i < COUNT(keys); ++i) {
        if (strcasecmp(keys[i].name, name) == 0) return keys[i].sc;
    }
    if (strlen(name) == 1 && name[0] >= 'A' && name[0] <= 'Z') {
        // Single letters, for typing cheat codes (DOS scancodes follow the QWERTY layout)
        static const u8 letters[26] = {SC_A, SC_B, SC_C, SC_D, SC_E, SC_F, SC_G, SC_H, SC_I, SC_J, SC_K, SC_L, SC_M,
                                       SC_N, SC_O, SC_P, SC_Q, SC_R, SC_S, SC_T, SC_U, SC_V, SC_W, SC_X, SC_Y, SC_Z};
        return letters[name[0] - 'A'];
    }
    printf("Unknown key '%s' in script\n", name);
    exit(1);
}

static void headless_save_screenshot(surface_t* surface, const char* name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", headless_out_dir, name);
    FILE* fp = fopen(path, "wb");
    if (!fp) return;
    fprintf(fp, "P6 %d %d 255\n", surface->width, surface->height);
    for (s32 y = 0; y < surface->height; ++y) {
        u8* row = surface->memory + y * surface->pitch;
        for (s32 x = 0; x < surface->width; ++x) {
            fputc(row[x * 4 + 2], fp);
            fputc(row[x * 4 + 1], fp);
            fputc(row[x * 4 + 0], fp);
        }
    }
    fclose(fp);
    fprintf(headless_log, "[frame %lld] screenshot %s\n", (long long)headless_frame, name);
}

static bool headless_script_held[128]; // keys held according to the script
static bool headless_sent_held[128];   // keys the game currently sees as held

// Red/cyan anaglyph of the stereoscopic 3D output (for checking the 3D layers on a PC).
static void headless_save_anaglyph(app_state_t* app_state, const char* name) {
    game_state_t* game = &app_state->game;
    const u8* final_frame = (draw_buffer && draw_buffer != game->draw_buffer.memory) ? draw_buffer : game->draw_buffer.memory;
    if (!stereo_frame_usable(final_frame)) {
        fprintf(headless_log, "[frame %lld] anaglyph %s: no 3D layers this frame\n", (long long)headless_frame, name);
        return;
    }
    static u8 left[SCREEN_WIDTH_MAX * 200], right[SCREEN_WIDTH_MAX * 200];
    stereo_compose_eye(left, final_frame, -1, 1.0f);
    stereo_compose_eye(right, final_frame, +1, 1.0f);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s_3d.ppm", headless_out_dir, name);
    FILE* fp = fopen(path, "wb");
    if (!fp) return;
    fprintf(fp, "P6 %d 200 255\n", SCREEN_WIDTH);
    for (s32 i = 0; i < SCREEN_WIDTH * 200; ++i) {
        rgb_t l = game->draw_palette.colors[left[i]];
        rgb_t r = game->draw_palette.colors[right[i]];
        fputc(l.r, fp); fputc(r.g, fp); fputc(r.b, fp);
    }
    fclose(fp);
}

static void headless_key(u8 sc, bool down) {
    headless_script_held[sc & 0x7F] = down;
}

// Apply the same crawl assist as the 3DS (if CRAWL_ASSIST is set), then send any changes to the game.
static void headless_flush_keys(void) {
    bool eff[128];
    memcpy(eff, headless_script_held, sizeof(eff));
    if (getenv("CRAWL_ASSIST")) {
        crawl_assist_t a = crawl_assist(eff[SC_DOWN], eff[SC_LEFT], eff[SC_RIGHT], eff[SC_UP]);
        if (a.hide_direction) eff[SC_LEFT] = eff[SC_RIGHT] = false;
        if (a.add_jump) eff[SC_CONTROL] = true;
    }
    for (s32 sc = 1; sc < 128; ++sc) {
        if (eff[sc] != headless_sent_held[sc]) {
            headless_sent_held[sc] = eff[sc];
            PC_keyboard_interrupt_handler(eff[sc] ? (u8)sc : (u8)(sc | 0x80));
        }
    }
}

static void headless_run_script(app_state_t* app_state) {
    // Scheduled releases from 'tap'
    for (s32 sc = 0; sc < 256; ++sc) {
        if (headless_release_at[sc] && headless_release_at[sc] == headless_frame) {
            headless_key((u8)sc, false);
            headless_release_at[sc] = 0;
        }
    }
    while (headless_next_event < headless_event_count && headless_events[headless_next_event].frame <= headless_frame) {
        headless_event_t* e = headless_events + headless_next_event++;
        if (strcmp(e->kind, "down") == 0) {
            headless_key(headless_key_scancode(e->arg), true);
        } else if (strcmp(e->kind, "up") == 0) {
            headless_key(headless_key_scancode(e->arg), false);
        } else if (strcmp(e->kind, "tap") == 0) {
            u8 sc = headless_key_scancode(e->arg);
            headless_key(sc, true);
            headless_release_at[sc] = (s32)headless_frame + 4;
        } else if (strcmp(e->kind, "shot") == 0) {
            headless_save_screenshot(app_state->active_surface, e->arg);
            if (getenv("STEREO_TEST")) headless_save_anaglyph(app_state, e->arg);
        } else if (strcmp(e->kind, "mark") == 0) {
            fprintf(headless_log, "[frame %lld] MARK %s\n", (long long)headless_frame, e->arg);
        } else if (strcmp(e->kind, "end") == 0) {
            fprintf(headless_log, "[frame %lld] end of script\n", (long long)headless_frame);
            fclose(headless_log);
            if (headless_audio_file) fclose(headless_audio_file);
            _Exit(0);
        }
    }
}

static void headless_log_voices(void) {
    for (s32 i = 0; i < (s32)COUNT(digi_voices) && i < 64; ++i) {
        snd_t* v = digi_voices + i;
        bool started = v->is_playing && (!headless_voice_was_playing[i] || headless_voice_data[i] != v->data);
        if (started) {
            fprintf(headless_log, "[frame %lld] voice %d start: size %d rate %d vol %.2f\n",
                    (long long)headless_frame, i, v->size, v->sample_rate, v->volume);
        }
        headless_voice_was_playing[i] = v->is_playing;
        headless_voice_data[i] = v->data;
    }
}

void ctr_advance_frame(app_state_t* app_state) {
    headless_run_script(app_state);
    headless_flush_keys();

    game_sound_buffer_t* sb = &app_state->game.sound_buffer;
    sb->sample_count = HEADLESS_RATE / 60;
    game_get_sound_samples(sb);
    headless_log_voices();
    if (headless_audio_file) {
        fwrite(sb->samples, sizeof(s16) * 2, sb->sample_count, headless_audio_file);
    }

    stereo_end_frame();
    ++headless_frame;
    app_state->flip_clock = get_clock();
    if (!app_state->running) {
        exit(0);
    }
}

static void headless_load_script(const char* path) {
    FILE* fp = fopen(path, "r");
    if (!fp) {
        printf("Cannot open script %s\n", path);
        exit(1);
    }
    char line[256];
    while (fgets(line, sizeof(line), fp) && headless_event_count < (s32)COUNT(headless_events)) {
        char* hash = strchr(line, '#');
        if (hash) *hash = 0;
        headless_event_t e = {0};
        int n = sscanf(line, "%d %7s %63[^\n]", &e.frame, e.kind, e.arg);
        if (n >= 2) {
            headless_events[headless_event_count++] = e;
        }
    }
    fclose(fp);
    // Process events in frame order regardless of their order in the file (stable insertion sort).
    for (s32 i = 1; i < headless_event_count; ++i) {
        headless_event_t e = headless_events[i];
        s32 j = i - 1;
        while (j >= 0 && headless_events[j].frame > e.frame) {
            headless_events[j + 1] = headless_events[j];
            --j;
        }
        headless_events[j + 1] = e;
    }
}

int main(int argc, char** argv) {
    if (argc < 4) {
        printf("usage: %s <game data folder> <script> <output folder>\n", argv[0]);
        return 1;
    }
    headless_load_script(argv[2]);
    headless_out_dir = argv[3];
    char path[512];
    snprintf(path, sizeof(path), "%s/log.txt", headless_out_dir);
    headless_log = fopen(path, "w");
    snprintf(path, sizeof(path), "%s/audio_s16le_44100_stereo.raw", headless_out_dir);
    headless_audio_file = fopen(path, "wb");
    if (!headless_log || chdir(argv[1]) != 0) {
        printf("Cannot open output folder or game data folder\n");
        return 1;
    }
    setvbuf(headless_log, NULL, _IOLBF, 0);

    app_state_t* app_state = &global_app_state;
    memset(&app_state->game_surface, 0, sizeof(app_state->game_surface));
    surface_resize(&app_state->game_surface, 320, 200);
    app_state->active_surface = &app_state->game_surface;
    app_state->target_game_hz = 60;
    app_state->target_seconds_per_frame = 1.0f / 60.0f;
    app_state->client_width = CTR_TOP_W;
    app_state->client_height = CTR_TOP_H;
    app_state->display_scale_factor = 1.0f;
    app_state->display_points_per_pixel = 1.0f;

    game_init_sound(&app_state->game.sound_buffer, HEADLESS_RATE);
    if (!app_state->game.initialized) {
        game_init(&app_state->game);
    }
    app_state->running = true;
    widescreen_enabled = getenv("WIDESCREEN") != NULL; // test the wide view (3DS display option)
    return main_Ray(argc, argv);
}
