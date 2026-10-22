// Stereoscopic 3D support (added for the 3DS port). Platform-neutral part.
// (The world map is handled the same way; see the depths below.)
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
// negative = in front of it).
//  - In levels the gameplay layer pops out of the screen, the background sits deep behind it, and the
//    HUD floats just in front of the gameplay so it never looks buried.
//  - On the world map (the binoculars screen) the black binocular frame is in front, Rayman, the paths
//    and the level dots are at the screen, and the map itself is deep behind.
typedef struct stereo_depths_t {
    float back, main, front;
} stereo_depths_t;
static const stereo_depths_t stereo_depths_level = { 3.0f, -1.5f, -2.5f };
static const stereo_depths_t stereo_depths_map   = { 3.0f,  0.0f, -2.0f };

enum { STEREO_SCENE_LEVEL, STEREO_SCENE_MAP };

static u8 stereo_layer_back[STEREO_W * STEREO_H];
static u8 stereo_layer_main[STEREO_W * STEREO_H];
static bool stereo_have_back;
static bool stereo_have_main;
static s32 stereo_scene;

// Called by the game loop after the background has been drawn.
void stereo_capture_background(void) {
    if (!draw_buffer) return;
    memcpy(stereo_layer_back, draw_buffer, sizeof(stereo_layer_back));
    stereo_have_back = true;
    stereo_scene = STEREO_SCENE_LEVEL;
}

// World map: called after the map picture has been drawn (into the binoculars' work buffer).
void stereo_capture_map_background(void) {
    stereo_capture_background();
    stereo_scene = STEREO_SCENE_MAP;
}

// Called by the game loop just before the HUD is drawn (on the world map: after Rayman, the paths and
// the level dots, before the binocular frame and the level names).
void stereo_capture_before_hud(void) {
    if (!draw_buffer || !stereo_have_back) return;
    memcpy(stereo_layer_main, draw_buffer, sizeof(stereo_layer_main));
    stereo_have_main = true;
}

// True if this frame was drawn by the in-level loop or the world map (layers available). Menus etc.
// are shown flat.
bool stereo_frame_has_layers(void) {
    return stereo_have_back && stereo_have_main;
}

// True if 3D layers can be used for this final frame. If most of the picture changed after the
// snapshots (e.g. a pause screen or picture screen drawn on top), show it flat instead.
bool stereo_frame_usable(const u8* final_frame) {
    if (!stereo_frame_has_layers() || !final_frame) return false;
    if (stereo_scene == STEREO_SCENE_MAP) return true; // the black frame always covers a lot of the map
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

static inline s32 stereo_shift(float depth, float strength, s32 eye) {
    float d = depth * strength;
    return (s32)(d + (d >= 0 ? 0.5f : -0.5f)) * eye;
}

static inline u32 stereo_load4(const u8* p) {
    u32 v;
    memcpy(&v, p, 4); // unaligned 4-byte load
    return v;
}

// For 4 pixels at once: where a == b (byte by byte) take the byte of if_equal, else of if_different.
static inline u32 stereo_pick4(u32 a, u32 b, u32 if_equal, u32 if_different) {
#if defined(__3DS__)
    // ARMv6 SIMD: USUB8 of 0 - (a ^ b) sets a per-byte flag where the byte is zero; SEL picks by those flags.
    u32 r;
    __asm__("usub8 %0, %1, %2\n\tsel %0, %3, %4"
            : "=&r"(r) : "r"(0), "r"(a ^ b), "r"(if_equal), "r"(if_different) : "cc");
    return r;
#else
    u32 x = a ^ b;
    u32 nonzero = (((x & 0x7F7F7F7Fu) + 0x7F7F7F7Fu) | x) & 0x80808080u; // top bit of each nonzero byte
    u32 diff_mask = (nonzero >> 7) * 0xFFu;
    return (if_equal & ~diff_mask) | (if_different & diff_mask);
#endif
}

// Build one eye's 320x200 picture from the final frame and the snapshots.
// eye = -1 for the left eye, +1 for the right eye; strength = 0..1 (the 3D slider).
void stereo_compose_eye(u8* out, const u8* final_frame, s32 eye, float strength) {
    const stereo_depths_t* depths = (stereo_scene == STEREO_SCENE_MAP) ? &stereo_depths_map : &stereo_depths_level;
    // An object behind the screen appears further left to the left eye, so the left eye samples the
    // layer from further right: sample_x = x - eye * depth.
    s32 shift_back  = stereo_shift(depths->back,  strength, eye);
    s32 shift_main  = stereo_shift(depths->main,  strength, eye);
    s32 shift_front = stereo_shift(depths->front, strength, eye);

    // Columns for which no shifted position falls off the picture can skip the edge clamping.
    s32 max_shift = MAX(shift_back, MAX(shift_main, shift_front));
    s32 min_shift = MIN(shift_back, MIN(shift_main, shift_front));
    s32 x_lo = MAX(0, max_shift);
    s32 x_hi = MIN(STEREO_W, STEREO_W + min_shift);

    if (stereo_scene == STEREO_SCENE_MAP) {
        // World map: the game only draws the map picture around the binoculars, so outside them all
        // three layers are black. Black there must stay part of the (front) frame, or bits of the
        // map's edge would show through when the layers are shifted.
        for (s32 y = 0; y < STEREO_H; ++y) {
            const u8* fin  = final_frame + y * STEREO_W;
            const u8* main = stereo_layer_main + y * STEREO_W;
            const u8* back = stereo_layer_back + y * STEREO_W;
            u8* dst = out + y * STEREO_W;
            for (s32 x = 0; x < STEREO_W; ++x) {
                s32 xf = stereo_clamp_x(x - shift_front);
                s32 xm = stereo_clamp_x(x - shift_main);
                u8 f = fin[xf];
                if (f != main[xf] || (f == 0 && back[xf] == 0)) {
                    dst[x] = f;
                } else {
                    dst[x] = (main[xm] != back[xm]) ? main[xm] : back[stereo_clamp_x(x - shift_back)];
                }
            }
        }
        return;
    }

    for (s32 y = 0; y < STEREO_H; ++y) {
        const u8* fin  = final_frame + y * STEREO_W;
        const u8* main = stereo_layer_main + y * STEREO_W;
        const u8* back = stereo_layer_back + y * STEREO_W;
        u8* dst = out + y * STEREO_W;
        for (s32 x = 0; x < x_lo; ++x) {
            s32 xf = stereo_clamp_x(x - shift_front);
            s32 xm = stereo_clamp_x(x - shift_main);
            dst[x] = (fin[xf] != main[xf]) ? fin[xf] : (main[xm] != back[xm]) ? main[xm] : back[stereo_clamp_x(x - shift_back)];
        }
        // Interior: the three layers read through pointers pre-offset by their shifts.
        const u8* fin_f  = fin  - shift_front;
        const u8* main_f = main - shift_front;
        const u8* main_m = main - shift_main;
        const u8* back_m = back - shift_main;
        const u8* back_b = back - shift_back;
        s32 x = x_lo;
        for (; x + 4 <= x_hi; x += 4) {
            u32 f = stereo_load4(fin_f + x);
            u32 m = stereo_load4(main_m + x);
            u32 main_or_back = stereo_pick4(m, stereo_load4(back_m + x), stereo_load4(back_b + x), m);
            u32 r = stereo_pick4(f, stereo_load4(main_f + x), main_or_back, f);
            memcpy(dst + x, &r, 4);
        }
        for (; x < x_hi; ++x) {
            u8 f = fin_f[x];
            if (f != main_f[x]) {
                dst[x] = f;
            } else {
                u8 m = main_m[x];
                dst[x] = (m != back_m[x]) ? m : back_b[x];
            }
        }
        for (s32 x = MAX(x_hi, x_lo); x < STEREO_W; ++x) {
            s32 xf = stereo_clamp_x(x - shift_front);
            s32 xm = stereo_clamp_x(x - shift_main);
            dst[x] = (fin[xf] != main[xf]) ? fin[xf] : (main[xm] != back[xm]) ? main[xm] : back[stereo_clamp_x(x - shift_back)];
        }
    }
}
