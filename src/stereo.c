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
// Pixels drawn in exactly the same colour as what was already there can't be told apart by comparing
// snapshots, so the drawing routines also record which pixels they draw (stereo_drawn below).

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

// Set by the platform layer: whether the next frames will be shown in 3D. When not, none of the 3D
// bookkeeping below is done.
bool stereo_wanted;

// Which pixels the game drew after the background snapshot, recorded by the sprite and tile drawing
// routines in lowlevel.c (bit 0: gameplay layer, bit 1: HUD/front layer). Comparing colours with the
// snapshots alone gets a pixel wrong whenever something is drawn over the background in exactly the
// same colour as what was there; the background then "bled through" the foreground in 3D.
static u8 stereo_drawn[STEREO_W * STEREO_H];
static u8* stereo_mark_buffer; // the picture being recorded (NULL: not recording)
static u8 stereo_mark_bits;

// For the drawing routines: where to record the pixels drawn at draw_pos, or NULL if this drawing isn't
// being recorded (another buffer, or no 3D).
static inline u8* stereo_mask_at(const u8* draw_buf, const u8* draw_pos) {
    if (!stereo_mark_buffer || draw_buf != stereo_mark_buffer) return NULL;
    return stereo_drawn + (draw_pos - draw_buf);
}

// Called by the game loop after the background has been drawn.
void stereo_capture_background(void) {
    if (!draw_buffer || !stereo_wanted) return;
    memcpy(stereo_layer_back, draw_buffer, STEREO_W * STEREO_H);
    memset(stereo_drawn, 0, STEREO_W * STEREO_H);
    stereo_mark_buffer = draw_buffer;
    stereo_mark_bits = 1;
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
    memcpy(stereo_layer_main, draw_buffer, STEREO_W * STEREO_H);
    stereo_have_main = true;
    stereo_mark_bits = 2; // what is drawn from now on (HUD, text) is the front layer
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
    stereo_mark_buffer = NULL;
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

// For 4 pixels at once: where the byte of x is zero take the byte of if_zero, else of if_nonzero.
static inline u32 stereo_pick4(u32 x, u32 if_zero, u32 if_nonzero) {
#if defined(__3DS__)
    // ARMv6 SIMD: USUB8 of 0 - x sets a per-byte flag where the byte is zero; SEL picks by those flags.
    u32 r;
    __asm__("usub8 %0, %1, %2\n\tsel %0, %3, %4"
            : "=&r"(r) : "r"(0), "r"(x), "r"(if_zero), "r"(if_nonzero) : "cc");
    return r;
#else
    u32 nonzero = (((x & 0x7F7F7F7Fu) + 0x7F7F7F7Fu) | x) & 0x80808080u; // top bit of each nonzero byte
    u32 diff_mask = (nonzero >> 7) * 0xFFu;
    return (if_zero & ~diff_mask) | (if_nonzero & diff_mask);
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
            const u8* drawn = stereo_drawn + y * STEREO_W;
            u8* dst = out + y * STEREO_W;
            for (s32 x = 0; x < STEREO_W; ++x) {
                s32 xf = stereo_clamp_x(x - shift_front);
                s32 xm = stereo_clamp_x(x - shift_main);
                u8 f = fin[xf];
                if (f != main[xf] || (f == 0 && back[xf] == 0)) {
                    dst[x] = f;
                } else {
                    dst[x] = (main[xm] != back[xm] || (drawn[xm] & 1)) ? main[xm] : back[stereo_clamp_x(x - shift_back)];
                }
            }
        }
        return;
    }

    for (s32 y = 0; y < STEREO_H; ++y) {
        const u8* fin  = final_frame + y * STEREO_W;
        const u8* main = stereo_layer_main + y * STEREO_W;
        const u8* back = stereo_layer_back + y * STEREO_W;
        const u8* drawn = stereo_drawn + y * STEREO_W;
        u8* dst = out + y * STEREO_W;
        for (s32 x = 0; x < x_lo; ++x) {
            s32 xf = stereo_clamp_x(x - shift_front);
            s32 xm = stereo_clamp_x(x - shift_main);
            dst[x] = (fin[xf] != main[xf] || (drawn[xf] & 2)) ? fin[xf] : (main[xm] != back[xm] || (drawn[xm] & 1)) ? main[xm] : back[stereo_clamp_x(x - shift_back)];
        }
        // Interior: the three layers read through pointers pre-offset by their shifts.
        const u8* fin_f  = fin  - shift_front;
        const u8* main_f = main - shift_front;
        const u8* main_m = main - shift_main;
        const u8* back_m = back - shift_main;
        const u8* back_b = back - shift_back;
        const u8* drawn_f = drawn - shift_front;
        const u8* drawn_m = drawn - shift_main;
        s32 x = x_lo;
        for (; x + 4 <= x_hi; x += 4) {
            u32 f = stereo_load4(fin_f + x);
            u32 m = stereo_load4(main_m + x);
            u32 is_main = (m ^ stereo_load4(back_m + x)) | (stereo_load4(drawn_m + x) & 0x01010101u);
            u32 main_or_back = stereo_pick4(is_main, stereo_load4(back_b + x), m);
            u32 is_front = (f ^ stereo_load4(main_f + x)) | (stereo_load4(drawn_f + x) & 0x02020202u);
            u32 r = stereo_pick4(is_front, main_or_back, f);
            memcpy(dst + x, &r, 4);
        }
        for (; x < x_hi; ++x) {
            u8 f = fin_f[x];
            if (f != main_f[x] || (drawn_f[x] & 2)) {
                dst[x] = f;
            } else {
                u8 m = main_m[x];
                dst[x] = (m != back_m[x] || (drawn_m[x] & 1)) ? m : back_b[x];
            }
        }
        for (s32 x = MAX(x_hi, x_lo); x < STEREO_W; ++x) {
            s32 xf = stereo_clamp_x(x - shift_front);
            s32 xm = stereo_clamp_x(x - shift_main);
            dst[x] = (fin[xf] != main[xf] || (drawn[xf] & 2)) ? fin[xf] : (main[xm] != back[xm] || (drawn[xm] & 1)) ? main[xm] : back[stereo_clamp_x(x - shift_back)];
        }
    }
}
