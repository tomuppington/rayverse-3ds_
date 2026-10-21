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

static PrintConsole ctr_console;
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
    { KEY_R,      SC_TAB },      // cancel
};

static bool ctr_key_was_down[COUNT(ctr_keymap)];

static void ctr_process_input(void) {
    hidScanInput();
    u32 held = hidKeysHeld();
    for (u32 i = 0; i < COUNT(ctr_keymap); ++i) {
        bool is_down = (held & ctr_keymap[i].buttons) != 0;
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

// Copy the game's 320x200 picture (32-bit, bytes B,G,R,A) to the centre of the 400x240 top
// screen. The 3DS framebuffer is stored rotated: column by column, bottom to top, 3 bytes (B,G,R).
static void ctr_present(surface_t* surface) {
    u8* fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    memset(fb, 0, CTR_TOP_W * CTR_TOP_H * 3);

    s32 w = MIN(surface->width, CTR_TOP_W);
    s32 h = MIN(surface->height, CTR_TOP_H);
    s32 ox = (CTR_TOP_W - w) / 2;
    s32 oy = (CTR_TOP_H - h) / 2;

    for (s32 x = 0; x < w; ++x) {
        u8* dest = fb + ((ox + x) * CTR_TOP_H + (CTR_TOP_H - 1 - oy)) * 3;
        const u8* src = surface->memory + x * 4;
        for (s32 y = 0; y < h; ++y) {
            dest[0] = src[0]; // blue
            dest[1] = src[1]; // green
            dest[2] = src[2]; // red
            dest -= 3;
            src += surface->pitch;
        }
    }
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
    ctr_present(app_state->active_surface);
    ctr_produce_sound_for_frame(app_state, &app_state->game.sound_buffer);

    gfxFlushBuffers();
    gfxSwapBuffers();
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
    consoleInit(GFX_BOTTOM, &ctr_console);
    osSetSpeedupEnable(true); // use the New 3DS's faster CPU mode (harmless on older models)
    atexit(ctr_shutdown);     // registered first, so it runs last (after the game's own cleanup)

    printf("Rayverse for 3DS (work in progress)\n");
    printf("Rayman 1 PC port - bring your own data\n\n");
    printf("Controls: B jump, Y punch, X action,\nA confirm, START menu, SELECT pause.\n");
    printf("Hold L+R+SELECT to quit.\n\n");

    if (!ctr_check_game_data()) {
        ctr_wait_for_start();
        return 0;
    }

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
