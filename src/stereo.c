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

#define STEREO_W SCREEN_WIDTH // the picture width (320, or wider in the wide view)
#define STEREO_H SCREEN_HEIGHT

// Depth of each layer at full 3D slider, in game pixels per eye (positive = behind the screen,
// negative = in front of it).
//  - In levels the gameplay layer pops out of the screen, the background sits deep behind it, and the
//    HUD floats just in front of the gameplay so it never looks buried.
//  - On the world map (the binoculars screen) only the black binocular frame and the names come forward;
//    everything seen through the binoculars stays at the screen.
typedef struct stereo_depths_t {
    float back, main, front;
} stereo_depths_t;
static const stereo_depths_t stereo_depths_level = { 3.0f, -1.5f, -2.5f };
static const stereo_depths_t stereo_depths_map   = { 0.0f,  0.0f, -2.0f };

enum { STEREO_SCENE_LEVEL, STEREO_SCENE_MAP };

static u8 stereo_layer_back[SCREEN_WIDTH_MAX * SCREEN_HEIGHT_MAX];
static u8 stereo_layer_main[SCREEN_WIDTH_MAX * SCREEN_HEIGHT_MAX];
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
static u8 stereo_drawn[SCREEN_WIDTH_MAX * SCREEN_HEIGHT_MAX];
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

// World map: the binoculars' work buffer (EffetBufferNormal) is not laid out like the screen: its top-left
// pixel is the screen's (JumelleXMin, JumelleYMin). This copies it into screen positions, so it can be
// compared with the final picture pixel by pixel (the circles on screen are copied from it, so inside
// them the two always match - including the edge rows).
static void stereo_align_from_jumelle(u8* dst, const u8* work) {
    for (s32 y = 0; y < STEREO_H; ++y) {
        u8* d = dst + y * STEREO_W;
        s32 sy = y - JumelleYMin;
        if (sy < 0 || sy >= STEREO_H) {
            memset(d, 0, STEREO_W);
            continue;
        }
        const u8* s = work + sy * STEREO_W;
        for (s32 x = 0; x < STEREO_W; ++x) {
            s32 sx = x - JumelleXMin;
            d[x] = (sx >= 0 && sx < STEREO_W) ? s[sx] : 0;
        }
    }
}

// World map: called after the map picture has been drawn (into the binoculars' work buffer).
void stereo_capture_map_background(void) {
    // Only the frame comes forward, so no background snapshot or pixel recording is needed.
    if (!draw_buffer || !stereo_wanted) return;
    stereo_have_back = true;
    stereo_scene = STEREO_SCENE_MAP;
}

// Called by the game loop just before the HUD is drawn (on the world map: after Rayman, the paths and
// the level dots, before the binocular frame and the level names).
void stereo_capture_before_hud(void) {
    if (!draw_buffer || !stereo_have_back) return;
    if (stereo_scene == STEREO_SCENE_MAP) {
        // World map: work buffer and recorded pixels to screen positions (see stereo_align_from_jumelle).
        stereo_align_from_jumelle(stereo_layer_main, draw_buffer);
    } else {
        memcpy(stereo_layer_main, draw_buffer, STEREO_W * STEREO_H);
    }
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
        // World map: a pixel is part of the frame (or the names) if it differs from what the binoculars
        // show there, or if it is black outside the part of the map the binoculars show; the frame is
        // shifted, everything seen through it is not. (Black pixels of the map itself, like the outlines
        // of the dotted paths, match the map and stay flat.)
        s32 view_x0 = JumelleXMin, view_x1 = JumelleXMin + LargeurJumelle + 2;
        s32 view_y0 = JumelleYMin, view_y1 = JumelleYMin + HauteurJumelle + 2;
        for (s32 y = 0; y < STEREO_H; ++y) {
            const u8* fin  = final_frame + y * STEREO_W;
            const u8* main = stereo_layer_main + y * STEREO_W;
            u8* dst = out + y * STEREO_W;
            bool row_in_view = y >= view_y0 && y < view_y1;
            for (s32 x = 0; x < STEREO_W; ++x) {
                s32 xf = stereo_clamp_x(x - shift_front);
                u8 f = fin[xf];
                bool in_view = row_in_view && xf >= view_x0 && xf < view_x1;
                dst[x] = (f != main[xf] || (f == 0 && !in_view)) ? f : fin[x];
            }
        }
        return;
    }

    // 4:3 window (see play_window_boxed): near its edges, the shifted background and main layers must
    // come from inside the window (not from the black sides, nor the edge of the background beyond it),
    // and the sides stay black apart from the counters (the front layer). The fast 4-pixel loop then
    // only covers the columns well inside the window; the sides and the few columns at the window's
    // edges are done separately (each column once).
    bool boxed = play_window_boxed;
    s32 wx0 = Bloc_lim_W1, wx1 = Bloc_lim_W2;
    s32 margin = MAX(MAX(abs(shift_back), abs(shift_main)), abs(shift_front)) + 1;
    s32 in_lo = x_lo, in_hi = x_hi; // the columns done by the fast loop
    if (boxed) {
        if (wx0 < 0) wx0 = 0;
        if (wx1 > STEREO_W) wx1 = STEREO_W;
        in_lo = MAX(in_lo, wx0 + margin);
        in_hi = MIN(in_hi, wx1 - margin);
        if (in_hi < in_lo) in_hi = in_lo;
    }

    for (s32 y = 0; y < STEREO_H; ++y) {
        const u8* fin  = final_frame + y * STEREO_W;
        const u8* main = stereo_layer_main + y * STEREO_W;
        const u8* back = stereo_layer_back + y * STEREO_W;
        const u8* drawn = stereo_drawn + y * STEREO_W;
        u8* dst = out + y * STEREO_W;
        // The three layers read through pointers pre-offset by their shifts.
        const u8* fin_f  = fin  - shift_front;
        const u8* main_f = main - shift_front;
        const u8* main_m = main - shift_main;
        const u8* back_m = back - shift_main;
        const u8* back_b = back - shift_back;
        const u8* drawn_f = drawn - shift_front;
        const u8* drawn_m = drawn - shift_main;
        if (!boxed) {
            for (s32 x = 0; x < x_lo; ++x) {
                s32 xf = stereo_clamp_x(x - shift_front);
                s32 xm = stereo_clamp_x(x - shift_main);
                dst[x] = (fin[xf] != main[xf] || (drawn[xf] & 2)) ? fin[xf] : (main[xm] != back[xm] || (drawn[xm] & 1)) ? main[xm] : back[stereo_clamp_x(x - shift_back)];
            }
        }
        s32 x = in_lo;
        for (; x + 4 <= in_hi; x += 4) {
            u32 f = stereo_load4(fin_f + x);
            u32 m = stereo_load4(main_m + x);
            u32 is_main = (m ^ stereo_load4(back_m + x)) | (stereo_load4(drawn_m + x) & 0x01010101u);
            u32 main_or_back = stereo_pick4(is_main, stereo_load4(back_b + x), m);
            u32 is_front = (f ^ stereo_load4(main_f + x)) | (stereo_load4(drawn_f + x) & 0x02020202u);
            u32 r = stereo_pick4(is_front, main_or_back, f);
            memcpy(dst + x, &r, 4);
        }
        for (; x < in_hi; ++x) {
            u8 f = fin_f[x];
            if (f != main_f[x] || (drawn_f[x] & 2)) {
                dst[x] = f;
            } else {
                u8 m = main_m[x];
                dst[x] = (m != back_m[x] || (drawn_m[x] & 1)) ? m : back_b[x];
            }
        }
        if (!boxed) {
            for (s32 x = MAX(x_hi, x_lo); x < STEREO_W; ++x) {
                s32 xf = stereo_clamp_x(x - shift_front);
                s32 xm = stereo_clamp_x(x - shift_main);
                dst[x] = (fin[xf] != main[xf] || (drawn[xf] & 2)) ? fin[xf] : (main[xm] != back[xm] || (drawn[xm] & 1)) ? main[xm] : back[stereo_clamp_x(x - shift_back)];
            }
            continue;
        }
        // Boxed: the black sides (only the front layer, the counters, shows there), 4 pixels at a time
        // where the shifted front layer stays on the picture...
        for (s32 side = 0; side < 2; ++side) {
            s32 s0 = side ? wx1 : 0, s1 = side ? STEREO_W : wx0;
            s32 f0 = MAX(s0, shift_front), f1 = MIN(s1, STEREO_W + shift_front); // xf = x - shift_front stays on the picture
            s32 sx = s0;
            for (; sx < s1 && sx < f0; ++sx) {
                s32 xf = stereo_clamp_x(sx - shift_front);
                dst[sx] = (fin[xf] != main[xf] || (drawn[xf] & 2)) ? fin[xf] : 0;
            }
            for (; sx + 4 <= f1; sx += 4) {
                u32 f = stereo_load4(fin_f + sx);
                u32 is_front = (f ^ stereo_load4(main_f + sx)) | (stereo_load4(drawn_f + sx) & 0x02020202u);
                u32 r = stereo_pick4(is_front, 0, f);
                memcpy(dst + sx, &r, 4);
            }
            for (; sx < s1; ++sx) {
                s32 xf = stereo_clamp_x(sx - shift_front);
                dst[sx] = (fin[xf] != main[xf] || (drawn[xf] & 2)) ? fin[xf] : 0;
            }
        }
        // ... and the columns just inside the window's edges (not done by the fast loop above), with the
        // shifted positions kept inside the window.
        for (s32 edge = 0; edge < 2; ++edge) {
            s32 e0 = edge ? MAX(in_hi, wx0) : wx0, e1 = edge ? wx1 : MIN(in_lo, wx1);
            if (edge && e0 < MIN(in_lo, wx1)) e0 = MIN(in_lo, wx1); // (narrow window: no column twice)
            for (s32 ex = e0; ex < e1; ++ex) {
                s32 xf = stereo_clamp_x(ex - shift_front);
                if (fin[xf] != main[xf] || (drawn[xf] & 2)) {
                    dst[ex] = fin[xf];
                } else {
                    s32 xm = ex - shift_main, xb = ex - shift_back;
                    xm = xm < wx0 ? wx0 : (xm >= wx1 ? wx1 - 1 : xm);
                    xb = xb < wx0 ? wx0 : (xb >= wx1 ? wx1 - 1 : xb);
                    dst[ex] = (main[xm] != back[xm] || (drawn[xm] & 1)) ? main[xm] : back[xb];
                }
            }
        }
    }
}

