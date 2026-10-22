// Nintendo 3DS platform layer for Rayverse (libctru).
//
// This replaces linux_main.c / linux_opengl.c / win32_main.c on the 3DS. The game itself
// (everything outside the platform files) is unchanged: it draws each frame into a 320x200
// image and reads input as DOS keyboard scancodes, exactly like the original RAYMAN.EXE.
//
// Game data: copy the contents of your Rayman 1 PC (v1.21) folder to sdmc:/3ds/rayverse/
// (so that e.g. sdmc:/3ds/rayverse/RAY.LNG and sdmc:/3ds/rayverse/PCMAP/ exist).

#define CTR_DATA_DIR "sdmc:/3ds/rayverse"
#define CTR_TOP_W    400
#define CTR_TOP_H    240

#include "input_assist.c"

static PrintConsole ctr_console;       // messages (top 26 rows of the bottom screen)
static PrintConsole ctr_stats_console; // speed readout (bottom 4 rows), so messages can't scroll it away
static s64 ctr_compose_ns; // 3D: time spent building the two eye pictures this frame
static bool ctr_gfx_ready;

//----------------------------------------------------------------------------------------------
// Timing

s64 get_clock(void) {
    // System ticks -> nanoseconds, split up to avoid 64-bit overflow.
    const u64 hz = SYSCLOCK_ARM11;
    u64 ticks = svcGetSystemTick();
    return (s64)((ticks / hz) * 1000000000ull + ((ticks % hz) * 1000000000ull) / hz);
}

float get_seconds_elapsed(s64 start, s64 end) {
    return (float)(end - start) / 1e9f;
}

//----------------------------------------------------------------------------------------------
// Messages, errors and shutdown

void message_box(const char* message) {
    printf("\n%s\n", message);
}

static void ctr_shutdown(void) {
    ctr_shutdown_sound(&global_app_state);
    if (ctr_gfx_ready) {
        ctr_gfx_ready = false;
        gfxExit();
    }
}

static void ctr_wait_for_start(void) {
    printf("\nPress START to exit.\n");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) {
            break;
        }
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
}

// Called by fatal_error() (see common.h). On a PC the game would abort(); on the 3DS that would
// just freeze the console, so we show the message and let the player exit cleanly instead.
void ctr_fatal_error(void) {
    static bool in_fatal_error;
    if (in_fatal_error) {
        _Exit(1);
    }
    in_fatal_error = true;
    printf("\nRayverse stopped because of an error.\n");
    ctr_wait_for_start();
    exit(1);
}

//----------------------------------------------------------------------------------------------
// Input: 3DS buttons -> DOS keyboard scancodes (the game's own key handler does the rest)

typedef struct ctr_keymap_t {
    u32 buttons;
    u8 scancode;
} ctr_keymap_t;

static const ctr_keymap_t ctr_keymap[] = {
    { KEY_UP,     SC_UP },       // D-pad or Circle Pad
    { KEY_DOWN,   SC_DOWN },
    { KEY_LEFT,   SC_LEFT },
    { KEY_RIGHT,  SC_RIGHT },
    { KEY_B,      SC_CONTROL },  // jump   (default PC key: Ctrl)
    { KEY_Y,      SC_ALT },      // punch  (default PC key: Alt)
    { KEY_X,      SC_X },        // action (default PC key: X)
    { KEY_A,      SC_ENTER },    // confirm in menus
    { KEY_START,  SC_ESCAPE },   // game menu / back
    { KEY_SELECT, SC_NUMLOCK },  // pause
};

static bool ctr_key_was_down[COUNT(ctr_keymap)];
static void ctr_cycle_display_mode(void);

static void ctr_process_input(void) {
    hidScanInput();
    if (hidKeysDown() & KEY_TOUCH) {
        ctr_cycle_display_mode(); // tap the touch screen to change how the picture fills the screen
    }
    u32 held = hidKeysHeld();

    // Crawling: Down + Left/Right (see input_assist.c for how this maps onto the PC controls).
    u32 effective = held;
    crawl_assist_t assist = crawl_assist((held & KEY_DOWN) != 0, (held & KEY_LEFT) != 0,
                                         (held & KEY_RIGHT) != 0, (held & KEY_UP) != 0);
    if (assist.hide_direction) effective &= ~(KEY_LEFT | KEY_RIGHT);
    if (assist.add_jump) effective |= KEY_B;

    for (u32 i = 0; i < COUNT(ctr_keymap); ++i) {
        bool is_down = (effective & ctr_keymap[i].buttons) != 0;
        if (is_down != ctr_key_was_down[i]) {
            ctr_key_was_down[i] = is_down;
            u8 dos_scancode = ctr_keymap[i].scancode;
            if (!is_down) {
                dos_scancode |= 0x80; // key released
            }
            PC_keyboard_interrupt_handler(dos_scancode);
        }
    }
    // Emergency exit: hold L + R + SELECT
    if ((held & (KEY_L | KEY_R | KEY_SELECT)) == (KEY_L | KEY_R | KEY_SELECT)) {
        global_app_state.running = false;
    }
}

//----------------------------------------------------------------------------------------------
// Video

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
        u8* new_ptr = (u8*) realloc(surface->memory, memory_needed);
        if (!new_ptr) {
            fatal_error();
        }
        surface->memory = new_ptr;
        surface->memory_size = memory_needed;
    }
    surface_clear(surface);
}

// Picture modes (chosen in Options > Graphics details, or tap the touch screen to cycle):
//   FULLSCREEN - in levels the game draws a 400x240 picture, the top screen's own resolution: one game
//                pixel per screen pixel, and more of each level visible in every direction (see
//                SCREEN_WIDTH in types.h). Menus and small levels keep the original 320x200 picture,
//                filling the screen; the world map is shown unscaled in the middle.
//   ORIGINAL   - the original 320x200 picture, one game pixel per screen pixel, centred.
//   STRETCH    - the original picture zoomed to fill the screen, using the New 3DS's 800x240 "wide" mode
//                for twice the horizontal sharpness.
// Scaling uses area-weighted sampling: each screen pixel takes the colours of the game pixels it
// covers, in proportion. Pixels stay sharp; only the screen pixels that straddle two game pixels get
// blended, which avoids the uneven, shimmering look of plain pixel-repeating at non-whole scales.
// The chosen mode is remembered in RAY3DS.CFG in the game folder.
enum { CTR_DISPLAY_FULLSCREEN, CTR_DISPLAY_ORIGINAL, CTR_DISPLAY_STRETCH, CTR_DISPLAY_MODE_COUNT };
typedef struct ctr_display_mode_t {
    const char* name;
    bool wide;
    s16 out_w, out_h; // in framebuffer pixels
    bool wide_view;   // levels drawn at 400x240 (more of the level visible)
    const char* menu_name;        // for the graphics details menu page, in the game's font
    const char* menu_description; // ('/' starts a new centred line)
} ctr_display_mode_t;
static const ctr_display_mode_t ctr_display_modes[CTR_DISPLAY_MODE_COUNT] = {
    { "Fullscreen (1:1 pixels, 400x240)", false, 400, 240, true,  "fullscreen", "/see more of/each level/" },
    { "Original (1:1 pixels, 320x200)",   false, 320, 200, false, "original",   "/original picture/at its real size/" },
    { "Stretch (zoomed to fill, sharp)",  true,  800, 240, false, "stretch",    "/original picture/zoomed to fill/" },
};
static s32 ctr_display_mode = CTR_DISPLAY_FULLSCREEN;
static s32 ctr_fb_w = CTR_TOP_W; // current top framebuffer width (400, or 800 in wide mode)

// Stereoscopic 3D: active whenever the 3D slider is up. The 3DS can't combine 3D with the 800-pixel
// wide mode, so in 3D the wide modes are shown at half their width (800x240 -> 400x240 etc.).
// The depth comes from stereo.c; the slider position scales it.
static bool ctr_stereo_on;          // slider up this frame
static bool ctr_stereo_configured;  // display currently set up for 3D
static bool ctr_map_unscaled;       // set up for the world map shown unscaled (1:1 mode)

typedef struct ctr_scale_axis_t {
    s32 out_size;
    s32 src_size;
    s16 src_index[800];   // first source pixel covered by each output pixel
    u16 weight[800];      // share of that first source pixel: 64, 128, 192 or 256 (the rest is the next one)
} ctr_scale_axis_t;

static ctr_scale_axis_t ctr_axis_x, ctr_axis_y;

static void ctr_build_axis(ctr_scale_axis_t* axis, s32 src_size, s32 out_size) {
    axis->out_size = out_size;
    axis->src_size = src_size;
    for (s32 i = 0; i < out_size; ++i) {
        // Output pixel i covers source interval [a, b), measured in units of 1/out_size source pixels.
        s32 a = i * src_size;
        s32 b = a + src_size;
        s32 first = a / out_size;
        s32 first_end = (first + 1) * out_size;
        s32 weight = 256;
        if (b > first_end) {
            weight = ((first_end - a) * 256 + src_size / 2) / src_size;
            // Round to quarters, so that blending is just one or two byte-average instructions
            // (the horizontal shares are exact quarters already; vertically the difference is invisible).
            weight = ((weight + 32) / 64) * 64;
            if (weight == 0) {
                first += 1; // (almost) entirely the next pixel
                weight = 256;
            }
        }
        axis->src_index[i] = (s16)MIN(first, src_size - 1);
        axis->weight[i] = (u16)weight;
    }
}

static void ctr_setup_display_mode(s32 src_w, s32 src_h) {
    const ctr_display_mode_t* mode = &ctr_display_modes[ctr_display_mode];
    bool wide = mode->wide && !ctr_stereo_on;
    if (ctr_stereo_on) {
        if (gfxIsWide()) gfxSetWide(false); // wide mode must be off before 3D is turned on
        gfxSet3D(true);
    } else {
        gfxSet3D(false);
        if (gfxIsWide() != wide) gfxSetWide(wide);
    }
    ctr_stereo_configured = ctr_stereo_on;
    ctr_fb_w = wide ? 800 : CTR_TOP_W;
    s32 out_w = (mode->wide && !wide) ? mode->out_w / 2 : mode->out_w;
    s32 out_h = mode->out_h;
    ctr_map_unscaled = mode->wide_view && dans_la_map_monde;
    if (ctr_map_unscaled) {
        // 1:1 mode, world map: show the original picture unscaled in the middle of the screen (the
        // black around the binoculars simply continues to the edges). Also a plain copy, so fast.
        out_w = src_w;
        out_h = src_h;
    }
    ctr_build_axis(&ctr_axis_x, src_w, MIN(out_w, ctr_fb_w));
    ctr_build_axis(&ctr_axis_y, src_h, MIN(out_h, CTR_TOP_H));
}

// Average of four byte pairs at once, rounding down (ARMv6 SIMD instruction UHADD8 on the 3DS).
static inline u32 ctr_avg8(u32 a, u32 b) {
#if defined(__3DS__)
    u32 r;
    __asm__("uhadd8 %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
    return r;
#else
    return (a & b) + (((a ^ b) & 0xFEFEFEFEu) >> 1);
#endif
}

// Blend four byte pairs; w = share of a out of 256, one of 64, 128 or 192.
static inline u32 ctr_mix8(u32 a, u32 b, u32 w) {
    u32 m = ctr_avg8(a, b);
    if (w == 128) return m;
    return (w > 128) ? ctr_avg8(a, m) : ctr_avg8(m, b);
}

#define CTR_CONFIG_FILE "RAY3DS.CFG"

static void ctr_apply_display_mode(void) {
    ctr_axis_x.out_size = 0; // force rebuild
    widescreen_enabled = ctr_display_modes[ctr_display_mode].wide_view;
}

// For the graphics details menu page (menu.c).
s32 platform_display_mode_count(void) { return CTR_DISPLAY_MODE_COUNT; }
s32 platform_display_mode_get(void) { return ctr_display_mode; }
const char* platform_display_mode_name(s32 mode) { return ctr_display_modes[mode].menu_name; }
const char* platform_display_mode_description(s32 mode) { return ctr_display_modes[mode].menu_description; }
static void ctr_set_display_mode(s32 mode);
void platform_display_mode_set(s32 mode) { ctr_set_display_mode(mode); }

static void ctr_cycle_display_mode(void) {
    ctr_set_display_mode((ctr_display_mode + 1) % CTR_DISPLAY_MODE_COUNT);
}

static void ctr_set_display_mode(s32 mode) {
    ctr_display_mode = mode;
    ctr_apply_display_mode();
    printf("Display: %s\n", ctr_display_modes[ctr_display_mode].name);
    if (widescreen_enabled != (SCREEN_WIDTH != SCREEN_WIDTH_ORIGINAL) && RaymanDansUneMapDuJeu) {
        printf(" (takes effect when the next level\n  starts)\n");
    }
    platform_settings_changed();
}

// Save the 3DS settings (picture mode, soundtrack) to RAY3DS.CFG in the game folder.
void platform_settings_changed(void) {
    FILE* fp = fopen(CTR_CONFIG_FILE, "w");
    if (fp) {
        fprintf(fp, "picture=%d\n", (int)ctr_display_mode);
        fprintf(fp, "music=%s\n", ps1_music_preferred ? "ps1" : "dos");
        fclose(fp);
    }
}

static void ctr_load_config(void) {
    FILE* fp = fopen(CTR_CONFIG_FILE, "r");
    if (!fp) {
        ctr_apply_display_mode(); // defaults
        return;
    }
    char line[64];
    while (fgets(line, sizeof(line), fp)) {
        int value = 0;
        if (sscanf(line, "picture=%d", &value) == 1 && value >= 0 && value < CTR_DISPLAY_MODE_COUNT) {
            ctr_display_mode = value;
        } else if (strncmp(line, "music=", 6) == 0) {
            ps1_music_preferred = strncmp(line + 6, "dos", 3) != 0;
        } else if (sscanf(line, "display_mode=%d", &value) == 1) {
            // Earlier builds had five modes: 4 was 1:1, 2 pixel perfect, the others zoomed to fill.
            ctr_display_mode = (value == 4) ? CTR_DISPLAY_FULLSCREEN : (value == 2) ? CTR_DISPLAY_ORIGINAL : CTR_DISPLAY_STRETCH;
        }
    }
    fclose(fp);
    ctr_apply_display_mode();
    printf("Display: %s\n", ctr_display_modes[ctr_display_mode].name);
}

// Copy the game's 320x200, 256-colour picture to the top screen, scaling it on the way.
// The 3DS framebuffer is stored rotated: column by column, bottom to top, 3 bytes (B,G,R).
//
// Speed: instead of computing every screen pixel separately (192,000 of them in wide mode), each of
// the 320 game columns is scaled vertically once, straight into framebuffer byte order. Screen
// columns that show a single game column are then plain memcpy()s of that cached column (in wide
// mode that is 4 out of every 5), and only the columns straddling two game columns are blended.
#define CTR_MAX_SRC_W SCREEN_WIDTH_MAX
static u8 ctr_col_cache[CTR_MAX_SRC_W][CTR_TOP_H * 3] __attribute__((aligned(4)));

static void ctr_scale_to_fb(const u8* src, s32 src_w, s32 src_h, const u32* pal, u8* fb) {
    s32 w = ctr_axis_x.out_size;
    s32 h = ctr_axis_y.out_size;
    if (w < ctr_fb_w || h < CTR_TOP_H) {
        memset(fb, 0, ctr_fb_w * CTR_TOP_H * 3);
    }
    s32 ox = (ctr_fb_w - w) / 2;
    s32 oy = (CTR_TOP_H - h) / 2;
    s32 col_bytes = h * 3;
    s32 first_px = CTR_TOP_H - oy - h; // framebuffer index (from the bottom) of the lowest picture pixel

    // 1:1 (the native-resolution mode): no scaling at all, convert each pixel straight into the
    // framebuffer. Four pixels (12 bytes) are written as three 32-bit words.
    if (w == src_w && h == src_h && (h & 3) == 0) {
        for (s32 sx = 0; sx < src_w; ++sx) {
            const u8* col = src + (src_h - 1) * src_w + sx; // bottom pixel of this column
            u32* d = (u32*)(fb + ((ox + sx) * CTR_TOP_H + first_px) * 3);
            for (s32 q = 0; q < h; q += 4) {
                u32 c0 = pal[col[0]];
                u32 c1 = pal[col[-src_w]];
                u32 c2 = pal[col[-2 * src_w]];
                u32 c3 = pal[col[-3 * src_w]];
                col -= 4 * src_w;
                d[0] = c0 | (c1 << 24);
                d[1] = (c1 >> 8) | (c2 << 16);
                d[2] = (c2 >> 16) | (c3 << 8);
                d += 3;
            }
        }
        return;
    }

    // Row tables, bottom row first (the framebuffer's order): source offsets and blend shares.
    static s32 row_off0[CTR_TOP_H], row_off1[CTR_TOP_H];
    static u16 row_w[CTR_TOP_H];
    for (s32 q = 0; q < h; ++q) {
        s32 y = h - 1 - q;
        s32 sy0 = ctr_axis_y.src_index[y];
        row_off0[q] = sy0 * src_w;
        row_off1[q] = MIN(sy0 + 1, src_h - 1) * src_w;
        row_w[q] = ctr_axis_y.weight[y];
    }

    // 1. Scale each game column vertically, into framebuffer order (bottom row first).
    for (s32 sx = 0; sx < src_w; ++sx) {
        const u8* col = src + sx;
        u8* out = ctr_col_cache[sx];
        for (s32 q = 0; q < h; ++q) {
            u32 c = pal[col[row_off0[q]]];
            u32 wy = row_w[q];
            if (wy != 256) {
                c = ctr_mix8(c, pal[col[row_off1[q]]], wy);
            }
            out[0] = (u8)(c);
            out[1] = (u8)(c >> 8);
            out[2] = (u8)(c >> 16);
            out += 3;
        }
    }

    // 2. Copy (or blend) cached columns into the framebuffer. Each column is a multiple of 4 bytes and
    // starts on a 4-byte boundary, so blending works on 4 bytes at a time.
    s32 col_words = col_bytes / 4;
    for (s32 x = 0; x < w; ++x) {
        s32 sx0 = ctr_axis_x.src_index[x];
        u32 wx = ctr_axis_x.weight[x];
        u8* dest = fb + ((ox + x) * CTR_TOP_H + first_px) * 3;
        if (wx >= 256 || sx0 + 1 >= src_w) {
            memcpy(dest, ctr_col_cache[sx0], col_bytes);
        } else {
            const u32* a = (const u32*)ctr_col_cache[sx0];
            const u32* b = (const u32*)ctr_col_cache[sx0 + 1];
            u32* d = (u32*)dest;
            if (wx == 128) {
                for (s32 i = 0; i < col_words; ++i) d[i] = ctr_avg8(a[i], b[i]);
            } else if (wx > 128) {
                for (s32 i = 0; i < col_words; ++i) d[i] = ctr_avg8(a[i], ctr_avg8(a[i], b[i]));
            } else {
                for (s32 i = 0; i < col_words; ++i) d[i] = ctr_avg8(ctr_avg8(a[i], b[i]), b[i]);
            }
        }
    }
}

static void ctr_present(app_state_t* app_state) {
    game_state_t* game = &app_state->game;
    image_t* image = &game->draw_buffer;
    // The game sometimes points draw_buffer at a different buffer (effects); honour that, as engine.c does.
    const u8* src = (draw_buffer && draw_buffer != image->memory) ? draw_buffer : image->memory;
    s32 src_w = MIN(image->width, CTR_MAX_SRC_W), src_h = image->height;

    float slider = osGet3DSliderState();
    ctr_stereo_on = slider > 0.05f;
    stereo_wanted = ctr_stereo_on; // record 3D layers in the next frames only while the slider is up
    if (ctr_axis_x.out_size == 0 || ctr_axis_x.src_size != src_w || ctr_axis_y.src_size != src_h ||
        ctr_stereo_configured != ctr_stereo_on ||
        ctr_map_unscaled != (ctr_display_modes[ctr_display_mode].wide_view && dans_la_map_monde)) {
        ctr_setup_display_mode(src_w, src_h);
    }

    // This frame's palette as packed B,G,R pixels.
    static u32 pal[256];
    for (s32 i = 0; i < 256; ++i) {
        rgb_t c = game->draw_palette.colors[i];
        pal[i] = (u32)c.b | ((u32)c.g << 8) | ((u32)c.r << 16);
    }

    if (!ctr_stereo_on) {
        ctr_scale_to_fb(src, src_w, src_h, pal, gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL));
    } else if (src_w == SCREEN_WIDTH && src_h == SCREEN_HEIGHT && stereo_frame_usable(src)) {
        static u8 eye_buffer[SCREEN_WIDTH_MAX * SCREEN_HEIGHT_MAX];
        s64 t0 = get_clock();
        stereo_compose_eye(eye_buffer, src, -1, slider);
        ctr_compose_ns += get_clock() - t0;
        ctr_scale_to_fb(eye_buffer, src_w, src_h, pal, gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL));
        t0 = get_clock();
        stereo_compose_eye(eye_buffer, src, +1, slider);
        ctr_compose_ns += get_clock() - t0;
        ctr_scale_to_fb(eye_buffer, src_w, src_h, pal, gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL));
    } else {
        // Slider up, but this screen has no depth layers (menus, map, ...): show it flat to both eyes.
        ctr_scale_to_fb(src, src_w, src_h, pal, gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL));
        ctr_scale_to_fb(src, src_w, src_h, pal, gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL));
    }
    stereo_end_frame();
}

//----------------------------------------------------------------------------------------------
// Frame loop (called by the game via advance_frame() in engine.c)

void ctr_advance_frame(app_state_t* app_state) {
    if (!aptMainLoop()) {
        app_state->running = false; // the HOME menu asked us to close
    }
    if (!app_state->running) {
        exit(0);
    }

    ctr_process_input();
    s64 present_start = get_clock();
    ctr_present(app_state);
    static s64 present_ns_total;
    present_ns_total += get_clock() - present_start;
    ctr_produce_sound_for_frame(app_state, &app_state->game.sound_buffer);

    gfxFlushBuffers();
    gfxSwapBuffers();

    // Speed readout on the bottom screen, updated once a second: frames per second, how much of
    // each frame's time budget the console is using, and how often the sound queue ran dry.
    static s64 busy_ns_total, stats_start;
    static s32 stats_frames;
    static u32 underruns_at_start;
    s64 before_wait = get_clock();
    busy_ns_total += before_wait - app_state->flip_clock;
    ++stats_frames;
    if (stats_start == 0) stats_start = before_wait;
    if (before_wait - stats_start >= 1000000000ll) {
        float seconds = (float)(before_wait - stats_start) / 1e9f;
        s32 fps = (s32)((float)stats_frames / seconds + 0.5f);
        s32 load = (s32)(100.0f * (float)busy_ns_total / (float)(before_wait - stats_start) + 0.5f);
        consoleSelect(&ctr_stats_console);
        printf("\x1b[1;0HScreen: %4.1f ms/frame%s", (double)present_ns_total / 1e6 / stats_frames,
               ctr_stereo_on ? " (3D)" : "     ");
        if (ctr_stereo_on) printf(", 3D layers %4.1f ms  ", (double)ctr_compose_ns / 1e6 / stats_frames);
        else printf("                      ");
        printf("\x1b[2;0HSpeed: %2d fps  CPU: %3d%%  sound gaps: %lu   ", fps, load,
               (unsigned long)(ctr_snd_underruns - underruns_at_start));
        consoleSelect(&ctr_console);
        present_ns_total = 0;
        ctr_compose_ns = 0;
        busy_ns_total = 0;
        stats_frames = 0;
        stats_start = before_wait;
        underruns_at_start = ctr_snd_underruns;
    }

    gspWaitForVBlank(); // ~59.8 frames per second, close to the game's 60

    app_state->flip_clock = get_clock();
}

//----------------------------------------------------------------------------------------------
// Startup

static bool ctr_file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static bool ctr_check_game_data(void) {
    if (chdir(CTR_DATA_DIR) != 0) {
        printf("Game data folder not found:\n  %s\n", CTR_DATA_DIR "/");
        goto missing;
    }
    if (!ctr_file_exists("RAY.LNG") || !ctr_file_exists("PCMAP")) {
        printf("The folder %s/\nexists, but the Rayman files are not\nin it (RAY.LNG and PCMAP were not\nfound).\n", CTR_DATA_DIR);
        goto missing;
    }
    if (!ctr_file_exists("Music") && !ctr_file_exists("../Music")) {
        printf("Note: no Music folder found, so the\nCD music will be silent.\n\n");
    }
    return true;

missing:
    printf("\nCopy the files from your Rayman 1 PC\n(version 1.21) game folder into it,\n"
           "e.g. RAY.LNG, VIGNET.DAT, SNDH8B.DAT,\nSNDD8B.DAT, SNDVIG.DAT and the PCMAP\n"
           "folder (plus Music for the music).\n");
    return false;
}

int main(int argc, char** argv) {
    app_state_t* app_state = &global_app_state;

    gfxInitDefault();
    ctr_gfx_ready = true;
    consoleInit(GFX_BOTTOM, &ctr_stats_console);
    consoleInit(GFX_BOTTOM, &ctr_console);
    consoleSetWindow(&ctr_console, 0, 0, 40, 26);
    consoleSetWindow(&ctr_stats_console, 0, 26, 40, 4);
    consoleSelect(&ctr_console);
    osSetSpeedupEnable(true); // use the New 3DS's faster CPU mode (harmless on older models)
    atexit(ctr_shutdown);     // registered first, so it runs last (after the game's own cleanup)

    printf("Rayverse for 3DS (work in progress)\n");
    printf("Rayman 1 PC port - bring your own data\n\n");
    printf("Controls: B jump, Y punch, X action,\nDown+Left/Right crawl, A confirm,\nSTART menu, SELECT pause.\n");
    printf("Hold L+R+SELECT to quit.\n");
    printf("Tap the touch screen to change the\npicture size. Slide the 3D slider up\nfor 3D.\n\n");

    if (!ctr_check_game_data()) {
        ctr_wait_for_start();
        return 0;
    }
    ctr_load_config(); // (in the game folder)

    // The game's 320x200 picture, as 32-bit pixels (same as the PC version)
    memset(&app_state->game_surface, 0, sizeof(app_state->game_surface));
    surface_resize(&app_state->game_surface, 320, 200);
    app_state->active_surface = &app_state->game_surface;

    app_state->target_game_hz = 60;
    app_state->target_seconds_per_frame = 1.0f / (float)app_state->target_game_hz;
    app_state->client_width = CTR_TOP_W;
    app_state->client_height = CTR_TOP_H;
    app_state->display_scale_factor = 1.0f;
    app_state->display_points_per_pixel = 1.0f;

    ctr_init_sound(app_state);
    game_init_sound(&app_state->game.sound_buffer, CTR_SND_RATE);

    if (!app_state->game.initialized) {
        game_init(&app_state->game);
    }

    app_state->flip_clock = get_clock();
    app_state->frame_clock = app_state->flip_clock;
    app_state->running = true;
    return main_Ray(argc, argv); // run the game!
}
