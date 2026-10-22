// Stereoscopic 3D support (added for the 3DS port). Platform-neutral part.
//
// The game draws each in-level frame in layers, in this order (DO_MAIN_LOOP_PC_NORMAL in pcmain.c):
//   1. the scrolling background, 2. the level tiles, 3. objects/characters/effects, 4. the HUD.
// We take a snapshot of the 320x200 picture right after the background, and again just before the
// HUD. Comparing the snapshots tells, for each pixel, which layer it belongs to:
//   - final != before-HUD        -> HUD          (shown in front of the gameplay)
//   - before-HUD != background   -> gameplay     (tiles + characters, together on one plane so
//                                                 Rayman stays level with the platforms he stands
//                                                 on; shown in front of the screen)
//   - otherwise                  -> background   (shown deep behind the screen)
// Each eye's picture is then built by shifting each layer horizontally by its own amount.
//
// Limitation: a gameplay pixel that happens to have exactly the same colour as the background pixel
// behind it is treated as background. Its colour is then taken from a few pixels to the side, which
// is usually the same or very similar, so this is rarely visible.

#define STEREO_W 320
#define STEREO_H 200

// Depth of each layer at full 3D slider, in game pixels per eye (positive = behind the screen,
// negative = in front of it). The gameplay layer pops out of the screen, the background sits
// deep behind it, and the HUD floats just in front of the gameplay so it never looks buried.
#define STEREO_DEPTH_BACK  3.0f
#define STEREO_DEPTH_MAIN -1.5f
#define STEREO_DEPTH_HUD  -2.5f

static u8 stereo_layer_back[STEREO_W * STEREO_H];
static u8 stereo_layer_main[STEREO_W * STEREO_H];
static bool stereo_have_back;
static bool stereo_have_main;

// Called by the game loop after the background has been drawn.
void stereo_capture_background(void) {
    if (!draw_buffer) return;
    memcpy(stereo_layer_back, draw_buffer, sizeof(stereo_layer_back));
    stereo_have_back = true;
}

// Called by the game loop just before the HUD is drawn.
void stereo_capture_before_hud(void) {
    if (!draw_buffer || !stereo_have_back) return;
    memcpy(stereo_layer_main, draw_buffer, sizeof(stereo_layer_main));
    stereo_have_main = true;
}

// True if this frame was drawn by the in-level loop (layers available). Menus, the world map etc.
// are shown flat.
bool stereo_frame_has_layers(void) {
    return stereo_have_back && stereo_have_main;
}

// True if 3D layers can be used for this final frame. If most of the picture changed after the
// snapshots (e.g. a pause screen or picture screen drawn on top), show it flat instead.
bool stereo_frame_usable(const u8* final_frame) {
    if (!stereo_frame_has_layers() || !final_frame) return false;
    s32 changed = 0;
    for (s32 i = 0; i < STEREO_W * STEREO_H; i += 4) {
        changed += (final_frame[i] != stereo_layer_main[i]);
    }
    return changed < (STEREO_W * STEREO_H / 4) / 4; // less than 25% of sampled pixels changed
}

// Called once the frame has been presented.
void stereo_end_frame(void) {
    stereo_have_back = false;
    stereo_have_main = false;
}

static inline s32 stereo_clamp_x(s32 x) {
    return x < 0 ? 0 : (x >= STEREO_W ? STEREO_W - 1 : x);
}

// Build one eye's 320x200 picture from the final frame and the snapshots.
// eye = -1 for the left eye, +1 for the right eye; strength = 0..1 (the 3D slider).
void stereo_compose_eye(u8* out, const u8* final_frame, s32 eye, float strength) {
    // An object behind the screen appears further left to the left eye, so the left eye samples the
    // layer from further right: sample_x = x - eye * depth.
    s32 shift_back = (s32)(STEREO_DEPTH_BACK * strength + 0.5f) * eye;
    s32 shift_main = (s32)(STEREO_DEPTH_MAIN * strength + (STEREO_DEPTH_MAIN >= 0 ? 0.5f : -0.5f)) * eye;
    s32 shift_hud  = (s32)(STEREO_DEPTH_HUD  * strength + (STEREO_DEPTH_HUD  >= 0 ? 0.5f : -0.5f)) * eye;

    for (s32 y = 0; y < STEREO_H; ++y) {
        const u8* fin  = final_frame + y * STEREO_W;
        const u8* main = stereo_layer_main + y * STEREO_W;
        const u8* back = stereo_layer_back + y * STEREO_W;
        u8* dst = out + y * STEREO_W;
        for (s32 x = 0; x < STEREO_W; ++x) {
            s32 xh = stereo_clamp_x(x - shift_hud);
            if (fin[xh] != main[xh]) {
                dst[x] = fin[xh];
                continue;
            }
            s32 xm = stereo_clamp_x(x - shift_main);
            if (main[xm] != back[xm]) {
                dst[x] = main[xm];
                continue;
            }
            dst[x] = back[stereo_clamp_x(x - shift_back)];
        }
    }
}
