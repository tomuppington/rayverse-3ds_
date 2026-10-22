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

// Pause menu on the bottom screen (BOTTOM_MENU=1): the frozen game picture for the top screen, and
// the bottom screen picture, saved next to each screenshot taken while the menu is open.
static u8 headless_saved_frame[SCREEN_WIDTH_MAX * SCREEN_HEIGHT_MAX];
static rgb_t headless_saved_palette[256];
static s32 headless_saved_w, headless_saved_h;

static void headless_save_rgb(const char* name, const char* suffix, const u32* px, s32 w, s32 h) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s_%s.ppm", headless_out_dir, name, suffix);
    FILE* fp = fopen(path, "wb");
    if (!fp) return;
    fprintf(fp, "P6 %d %d 255\n", w, h);
    for (s32 i = 0; i < w * h; ++i) {
        fputc((px[i] >> 16) & 0xFF, fp); fputc((px[i] >> 8) & 0xFF, fp); fputc(px[i] & 0xFF, fp);
    }
    fclose(fp);
}

static void headless_save_bottom_menu(app_state_t* app_state, const char* name) {
    game_state_t* game = &app_state->game;
    const u8* src = (draw_buffer && draw_buffer != game->draw_buffer.memory) ? draw_buffer : game->draw_buffer.memory;
    static u32 bottom[320 * 240];
    bottom_menu_render(bottom, src, SCREEN_WIDTH, SCREEN_HEIGHT, game->draw_palette.colors);
    headless_save_rgb(name, "bottom", bottom, 320, 240);
    static u32 top[SCREEN_WIDTH_MAX * SCREEN_HEIGHT_MAX];
    for (s32 i = 0; i < headless_saved_w * headless_saved_h; ++i) {
        rgb_t c = headless_saved_palette[headless_saved_frame[i]];
        u32 g = ((u32)c.r + c.g + c.b) / 3; // black and white while paused, as on the 3DS
        top[i] = (g << 16) | (g << 8) | g;
    }
    headless_save_rgb(name, "top", top, headless_saved_w, headless_saved_h);
}

// Title-screen menus on two screens (MENU_RAYS=1): the top and bottom screen pictures, saved next to each
// screenshot as name_rays.ppm and name_raysbottom.ppm.
static u32 headless_rays[SCREEN_WIDTH_ORIGINAL * SCREEN_HEIGHT_ORIGINAL];
static u32 headless_bottom_level[320 * 240]; // STATUS_PANEL: the bottom screen during levels
static u32 headless_rays_bottom[320 * 240];
static u32 headless_rays_right[SCREEN_WIDTH_ORIGINAL * SCREEN_HEIGHT_ORIGINAL]; // 3D (STEREO_TEST): right eye
static bool headless_rays_shown;

static void headless_render_rays(app_state_t* app_state) {
    game_state_t* game = &app_state->game;
    const u8* src = (draw_buffer && draw_buffer != game->draw_buffer.memory) ? draw_buffer : game->draw_buffer.memory;
    headless_rays_shown = menu_rays_active();
    bool map_dialog = !headless_rays_shown && menu_map_dialog_active();
    if (map_dialog || (menu_rays_holding(src, SCREEN_WIDTH, SCREEN_HEIGHT) && menu_rays_held_on_map() && !headless_rays_shown)) {
        // world map question: top = the map in black and white (EffetBufferNormal), bottom = the question
        if (map_dialog) menu_rays_render(NULL, NULL, headless_rays_bottom, src, SCREEN_WIDTH, SCREEN_HEIGHT, game->draw_palette.colors, 0.0f);
        else menu_rays_render_held(NULL, NULL, headless_rays_bottom, game->draw_palette.colors, 0.0f);
        for (s32 i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) { rgb_t c = game->draw_palette.colors[EffetBufferNormal[i]]; headless_rays[i] = ((u32)c.r << 16) | ((u32)c.g << 8) | c.b; }
        headless_rays_shown = true;
    } else if (headless_rays_shown) {
        menu_rays_render(headless_rays, getenv("STEREO_TEST") ? headless_rays_right : NULL, headless_rays_bottom, src, SCREEN_WIDTH, SCREEN_HEIGHT, game->draw_palette.colors, 1.0f);
    } else if (menu_rays_holding(src, SCREEN_WIDTH, SCREEN_HEIGHT)) {
        headless_rays_shown = true;
        menu_rays_render_held(headless_rays, getenv("STEREO_TEST") ? headless_rays_right : NULL, headless_rays_bottom, game->draw_palette.colors, 1.0f);
    }
    if (getenv("TRACE_RAYS")) {
        // which frames are shown plainly (and whether they are black), to spot flashes between menus
        u32 lit = 0;
        for (s32 i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i += 7) { rgb_t c = game->draw_palette.colors[src[i]]; lit += (c.r + c.g + c.b) > 30; }
        fprintf(headless_log, "[frame %lld] RAYS %s lit %u\n", (long long)headless_frame, headless_rays_shown ? "split" : "plain", lit);
    }
    if (getenv("TRACE_RAYS")) {
        // which frames are shown plainly (and whether they are black), to spot flashes between menus
        u32 lit = 0;
        for (s32 i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i += 7) { rgb_t c = game->draw_palette.colors[src[i]]; lit += (c.r + c.g + c.b) > 30; }
        fprintf(headless_log, "[frame %lld] RAYS %s lit %u\n", (long long)headless_frame, headless_rays_shown ? "split" : "plain", lit);
    }
    menu_rays_background = NULL;
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

// Display modes for the graphics details menu page (the same list as the 3DS; only 1:1 changes anything here).
static const char* headless_mode_names[3] = {"fullscreen", "original", "stretch"};
static const char* headless_mode_descriptions[3] = {"/see more of/each level/", "/original picture/at its real size/",
    "/original picture/zoomed to fill/"};
static s32 headless_display_mode = 2;
s32 platform_display_mode_count(void) { return 3; }
s32 platform_display_mode_get(void) { return headless_display_mode; }
const char* platform_display_mode_name(s32 mode) { return headless_mode_names[mode]; }
const char* platform_display_mode_description(s32 mode) { return headless_mode_descriptions[mode]; }
void platform_settings_changed(void) {
    fprintf(headless_log, "[frame %lld] settings saved: picture %d, music %s\n", (long long)headless_frame, headless_display_mode, ps1_music_preferred ? "ps1" : "dos");
}
void platform_display_mode_set(s32 mode) {
    headless_display_mode = mode;
    widescreen_enabled = (mode == 0);
    fprintf(headless_log, "[frame %lld] display mode %d (%s)\n", (long long)headless_frame, mode, headless_mode_names[mode]);
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
    static u8 left[SCREEN_WIDTH_MAX * SCREEN_HEIGHT_MAX], right[SCREEN_WIDTH_MAX * SCREEN_HEIGHT_MAX];
    stereo_compose_eye(left, final_frame, -1, 1.0f);
    stereo_compose_eye(right, final_frame, +1, 1.0f);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s_3d.ppm", headless_out_dir, name);
    FILE* fp = fopen(path, "wb");
    if (!fp) return;
    fprintf(fp, "P6 %d %d 255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    for (s32 i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
        rgb_t l = game->draw_palette.colors[left[i]];
        rgb_t r = game->draw_palette.colors[right[i]];
        fputc(l.r, fp); fputc(r.g, fp); fputc(r.b, fp);
    }
    fclose(fp);
    // Also each eye on its own (for close inspection).
    for (s32 e = 0; e < 2; ++e) {
        snprintf(path, sizeof(path), "%s/%s_%c.ppm", headless_out_dir, name, e ? 'R' : 'L');
        fp = fopen(path, "wb");
        if (!fp) return;
        fprintf(fp, "P6 %d %d 255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
        const u8* eye = e ? right : left;
        for (s32 i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
            rgb_t c = game->draw_palette.colors[eye[i]];
            fputc(c.r, fp); fputc(c.g, fp); fputc(c.b, fp);
        }
        fclose(fp);
    }
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
            if (bottom_menu_active()) headless_save_bottom_menu(app_state, e->arg);
            if (getenv("STATUS_PANEL") && (RaymanDansUneMapDuJeu || bottom_menu_active())) {
                headless_save_rgb(e->arg, "panel", headless_bottom_level, 320, 240);
            }
            if (headless_rays_shown) {
                headless_save_rgb(e->arg, "rays", headless_rays, SCREEN_WIDTH_ORIGINAL, SCREEN_HEIGHT_ORIGINAL);
                headless_save_rgb(e->arg, "raysbottom", headless_rays_bottom, 320, 240);
                if (getenv("STEREO_TEST")) headless_save_rgb(e->arg, "raysR", headless_rays_right, SCREEN_WIDTH_ORIGINAL, SCREEN_HEIGHT_ORIGINAL);
            }
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
    if (bottom_menu_enabled && !bottom_menu_active() && RaymanDansUneMapDuJeu) {
        // keep the latest game picture for the top screen while the menu is on the bottom screen
        game_state_t* game = &app_state->game;
        const u8* src = (draw_buffer && draw_buffer != game->draw_buffer.memory) ? draw_buffer : game->draw_buffer.memory;
        headless_saved_w = SCREEN_WIDTH;
        headless_saved_h = SCREEN_HEIGHT;
        memcpy(headless_saved_frame, src, SCREEN_WIDTH * SCREEN_HEIGHT);
        memcpy(headless_saved_palette, game->draw_palette.colors, sizeof(headless_saved_palette));
    }
    headless_render_rays(app_state);
    if (getenv("STATUS_PANEL") && (RaymanDansUneMapDuJeu || bottom_menu_active())) {
        // the bottom screen during levels, every frame as on the 3DS
        const u8* src = (draw_buffer && draw_buffer != app_state->game.draw_buffer.memory) ? draw_buffer : app_state->game.draw_buffer.memory;
        bottom_level_render(headless_bottom_level, bottom_menu_active(), src, SCREEN_WIDTH, SCREEN_HEIGHT, app_state->game.draw_palette.colors);
    }
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
    if (widescreen_enabled) headless_display_mode = 0;
    bottom_menu_enabled = getenv("BOTTOM_MENU") != NULL;
    menu_rays_enabled = getenv("MENU_RAYS") != NULL;
    stereo_wanted = getenv("STEREO_TEST") != NULL; // record 3D layers for the anaglyph test pictures
    return main_Ray(argc, argv);
}
