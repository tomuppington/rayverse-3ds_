// Pause menu on the bottom screen (added for the 3DS port). Platform-neutral part.
//
// When the game's in-game options menu is open (OptionGame), the platform layer can show it on a
// second screen: the top screen keeps the frozen game picture, and the menu is drawn here over an
// animated background recreated from the PlayStation version's menus (DISPLAY_FOND_MENU in the PS1
// code, rayman-ps1-decomp): 16 wedges rotating around the centre, alternating blue and magenta, each
// fading from black at the centre to a bright band (whose radius pulses between 30 and 180 pixels) and
// back to black at 200 pixels.
//
// For this, the game draws the menu over an empty picture (colour 0, see DISPLAY_FOND_MENU in frame.c)
// instead of the frozen game picture, and any pixel it leaves at 0 shows the background.

#define BOTTOM_W 320
#define BOTTOM_H 240
// The menu's see-through box (CadreTrans in box.c) fills empty pixels with this colour; here it shows
// the background, darkened.
#define BOTTOM_PANEL_COLOUR 0xC8

bool bottom_menu_enabled; // set by the platform layer

bool bottom_menu_active(void) {
    return bottom_menu_enabled && OptionGame;
}

static u16 bottom_angle[BOTTOM_W * BOTTOM_H];  // angle of each pixel around the centre (0..4095 = 360 degrees)
static u8 bottom_radius[BOTTOM_W * BOTTOM_H];  // distance from the centre (capped at 255)
static bool bottom_tables_ready;
static u32 bottom_wedge_colour[256 * 4];       // colour across 4 wedges (the pattern repeats every 4)
static u16 bottom_spin;                        // rotation (D_801CF092 in the PS1 code)
static u16 bottom_pulse;                       // pulse phase (D_801CF094)

static void bottom_menu_init_tables(void) {
    for (s32 y = 0; y < BOTTOM_H; ++y) {
        for (s32 x = 0; x < BOTTOM_W; ++x) {
            float dx = (float)x + 0.5f - BOTTOM_W / 2;
            float dy = (float)y + 0.5f - BOTTOM_H / 2;
            float a = atan2f(dy, dx);
            if (a < 0) a += 2.0f * 3.14159265f;
            bottom_angle[y * BOTTOM_W + x] = (u16)((s32)(a * (4096.0f / (2.0f * 3.14159265f))) & 4095);
            float r = sqrtf(dx * dx + dy * dy);
            bottom_radius[y * BOTTOM_W + x] = (u8)(r > 255.0f ? 255 : r);
        }
    }
    // The PS1 colours (D_801C7EAC): wedge i blends from colour[i & 3] to colour[(i + 1) & 3].
    static const u8 colours[4][3] = {{0, 0, 255}, {255, 20, 255}, {255, 20, 255}, {0, 0, 255}};
    for (s32 i = 0; i < 256 * 4; ++i) {
        s32 w = i / 256, f = i % 256;
        const u8* c0 = colours[w];
        const u8* c1 = colours[(w + 1) & 3];
        u32 r = (c0[0] * (256 - f) + c1[0] * f) >> 8;
        u32 g = (c0[1] * (256 - f) + c1[1] * f) >> 8;
        u32 b = (c0[2] * (256 - f) + c1[2] * f) >> 8;
        bottom_wedge_colour[i] = (r << 16) | (g << 8) | b;
    }
    bottom_tables_ready = true;
}

// Draws one frame of the bottom screen into out (0x00RRGGBB, row by row, 320x240): the animated
// background, with the menu from the game's picture (src, src_w x src_h, centred) on top.
void bottom_menu_render(u32* out, const u8* src, s32 src_w, s32 src_h, const rgb_t* palette) {
    if (!bottom_tables_ready) bottom_menu_init_tables();

    // Radius of the bright band this frame, and the brightness (0..256) at each distance.
    s32 band = ((s32)(sinf((float)bottom_pulse * (2.0f * 3.14159265f / 4096.0f)) * 75.0f)) + 105;
    const s32 outer = 200;
    static u16 brightness[256];
    for (s32 r = 0; r < 256; ++r) {
        if (r < band) brightness[r] = (u16)(r * 256 / band);
        else if (r < outer) brightness[r] = (u16)((outer - r) * 256 / (outer - band));
        else brightness[r] = 0;
    }

    // Centre the menu on the screen: find the box around everything the game drew (the menu box, or
    // while it opens and closes, its pixels flying in and out around the middle of the picture).
    s32 min_x = src_w, max_x = -1, min_y = src_h, max_y = -1;
    if (src) {
        for (s32 y = 0; y < src_h; ++y) {
            const u8* row = src + y * src_w;
            for (s32 x = 0; x < src_w; ++x) {
                if (row[x]) {
                    if (x < min_x) min_x = x;
                    if (x > max_x) max_x = x;
                    if (y < min_y) min_y = y;
                    if (y > max_y) max_y = y;
                }
            }
        }
    }
    s32 ox, oy;
    if (max_x >= 0) {
        ox = (min_x + max_x + 1) / 2 - BOTTOM_W / 2;
        oy = (min_y + max_y + 1) / 2 - BOTTOM_H / 2;
    } else {
        ox = (src_w - BOTTOM_W) / 2;
        oy = (src_h - BOTTOM_H) / 2;
    }
    for (s32 y = 0; y < BOTTOM_H; ++y) {
        s32 sy = y + oy;
        const u8* row = (src && sy >= 0 && sy < src_h) ? src + sy * src_w : NULL;
        for (s32 x = 0; x < BOTTOM_W; ++x) {
            s32 i = y * BOTTOM_W + x;
            s32 sx = x + ox;
            u8 p = (row && sx >= 0 && sx < src_w) ? row[sx] : 0;
            if (p != 0 && p != BOTTOM_PANEL_COLOUR) {
                rgb_t c = palette[p];
                out[i] = ((u32)c.r << 16) | ((u32)c.g << 8) | c.b;
            } else {
                u32 c = bottom_wedge_colour[(u16)(bottom_angle[i] - bottom_spin) & 1023];
                u32 k = brightness[bottom_radius[i]];
                if (p == BOTTOM_PANEL_COLOUR) k = k * 3 / 8; // inside the menu's see-through box: darker
                out[i] = ((((c >> 16) & 0xFF) * k >> 8) << 16) | ((((c >> 8) & 0xFF) * k >> 8) << 8) | ((c & 0xFF) * k >> 8);
            }
        }
    }
    bottom_spin += 10;
    bottom_pulse += 10;
}

// Darkens a 0x00RRGGBB colour by k/256.
static inline u32 menu_rays_darken_any(u32 c, u32 k) {
    return ((((c >> 16) & 0xFF) * k >> 8) << 16) | ((((c >> 8) & 0xFF) * k >> 8) << 8) | ((c & 0xFF) * k >> 8);
}

// The rays alone at half resolution (160x120, 0x00RRGGBB), for the bottom screen during play: a
// quarter of the work, so it costs little time per frame (each pixel is shown as 2x2).
void bottom_rays_render_small(u32* out) {
    if (!bottom_tables_ready) bottom_menu_init_tables();
    s32 band = ((s32)(sinf((float)bottom_pulse * (2.0f * 3.14159265f / 4096.0f)) * 75.0f)) + 105;
    const s32 outer = 200;
    static u16 brightness[256];
    for (s32 r = 0; r < 256; ++r) {
        if (r < band) brightness[r] = (u16)(r * 256 / band);
        else if (r < outer) brightness[r] = (u16)((outer - r) * 256 / (outer - band));
        else brightness[r] = 0;
    }
    for (s32 y = 0; y < BOTTOM_H / 2; ++y) {
        s32 ti = (2 * y) * BOTTOM_W;
        u32* o = out + y * (BOTTOM_W / 2);
        for (s32 x = 0; x < BOTTOM_W / 2; ++x, ti += 2) {
            u32 c = bottom_wedge_colour[(u16)(bottom_angle[ti] - bottom_spin) & 1023];
            u32 k = brightness[bottom_radius[ti]];
            o[x] = ((((c >> 16) & 0xFF) * k >> 8) << 16) | ((((c >> 8) & 0xFF) * k >> 8) << 8) | ((c & 0xFF) * k >> 8);
        }
    }
    bottom_spin += 10;
    bottom_pulse += 10;
}

//----------------------------------------------------------------------------------------------
// Status panel for the bottom screen during play: the world and level names, the cages freed in this
// part of the world (shown with the Electoon medallion), lives, hit points, tings and the cages freed in
// the whole game, over dark, slowly turning rays. Drawn with the game's own font and sprites.
//
// The panel is redrawn only when something on it changes (status_panel_update); every frame only the
// rays move behind it (status_panel_render).

static u8 status_panel[BOTTOM_W * BOTTOM_H]; // colour indices of the game's palette (0: rays show through)
static u32 status_panel_key = 0xFFFFFFFF;
static u16 status_spin, status_pulse;
// When a cage is freed, the panel's medallion plays the Electoon's arrival (its flash and twinkle, as on
// the main screen), then rests: the animation frame being shown, or -1.
#define STATUS_ARRIVAL_FIRST_FRAME 56 // (just before the flash: the Electoon arrives at about frame 60)
static s32 status_anim_frame = -1;
static s32 status_cages_shown = -1;  // cages shown last time, for this place on the map
static s32 status_place_shown = -1;

static obj_t* status_find_object(s16 type) {
    for (s32 i = 0; i < level.nb_objects; ++i) {
        if (level.objects[i].type == type) return level.objects + i;
    }
    return NULL;
}

// Draws a value 0-99 with the status bar's digits.
static void status_digits(obj_t* sbar, s32 value, s16 x, s16 y) {
    value = MAX(0, MIN(99, value));
    display_sprite_NoClip(sbar, 28 + value / 10, x, y, 1);
    display_sprite_NoClip(sbar, 28 + value % 10, x + 15, y, 1);
}

// The box around an animation frame (all its sprites together), relative to the object's position.
static void status_frame_box(obj_t* obj, anim_t* anim, s32 frame, s32* x0, s32* y0, s32* x1, s32* y1) {
    u16 layers = anim->layers_per_frame & 0x3FFF;
    anim_layer_t* layer = anim->layers + frame * layers;
    *x0 = *y0 = 0x7FFF;
    *x1 = *y1 = -0x7FFF;
    for (s32 i = 0; i < layers; ++i) {
        if (layer[i].sprite_index == 0) continue;
        sprite_t* sprite = obj->sprites + layer[i].sprite_index;
        if (sprite->id == 0) continue;
        *x0 = MIN(*x0, layer[i].x);
        *x1 = MAX(*x1, layer[i].x + sprite->outer_width);
        *y0 = MIN(*y0, layer[i].y);
        *y1 = MAX(*y1, layer[i].y + sprite->outer_height);
    }
}

static s32 status_frame_width(obj_t* obj, anim_t* anim, s32 frame) {
    s32 x0, y0, x1, y1;
    status_frame_box(obj, anim, frame, &x0, &y0, &x1, &y1);
    return x1 - x0;
}

// A frame of the medallion's animation where it is at rest (round, facing the screen, with the newest
// Electoon in its place). The resting medallion is the narrowest frame; it starts widening when it
// opens near the end. Some frames have a twinkle on an Electoon: of the last resting frames, the one
// with the fewest near-white pixels is used.
static s32 status_medal_rest_frame_find(obj_t* obj, anim_t* anim, bool before_electoon);

static s32 status_medal_rest_frame(obj_t* obj, anim_t* anim, bool before_electoon) {
    // (Cached: finding it draws several frames.)
    static s16 cache[2][256];
    static bool cache_ready;
    static const anim_t* cache_animations;
    if (!cache_ready || cache_animations != obj->animations) {
        memset(cache, 0xFF, sizeof(cache));
        cache_ready = true;
        cache_animations = obj->animations;
    }
    s16* entry = &cache[before_electoon ? 1 : 0][obj->anim_index];
    if (*entry < 0) *entry = (s16)status_medal_rest_frame_find(obj, anim, before_electoon);
    return *entry;
}

static s32 status_medal_rest_frame_find(obj_t* obj, anim_t* anim, bool before_electoon) {
    s32 n = anim->frames_count;
    if (n <= 0) return 0;
    s32 last = n - 1, first;
    s32 rest_width = status_frame_width(obj, anim, MIN(50, n - 1));
    if (before_electoon) {
        first = MIN(42, last); // (the halves have come together; the first Electoon arrives at about 60)
        last = MIN(66, last);
    } else {
        for (s32 f = n / 2; f < n; ++f) {
            if (status_frame_width(obj, anim, f) > rest_width + 1) {
                last = MAX(0, f - 4);
                break;
            }
        }
        first = MAX(0, last - 30);
    }
    // Draw each candidate frame into a scratch picture and count its near-white pixels.
    static u8 scratch[160 * 160];
    const rgb_t* pal = rvb[current_pal_id].colors;
    u8* saved_buffer = draw_buffer;
    s32 saved_w = screen_width, saved_h = screen_height;
    s32 cx0, cx1, cy0, cy1;
    get_sprite_clipping(&cx0, &cx1, &cy0, &cy1);
    screen_width = 160;
    screen_height = 160;
    draw_buffer = scratch;
    sprite_clipping(0, 160, 0, 160);
    s32 best = last, best_white = 0x7FFFFFFF;
    for (s32 f = last; f >= first; --f) {
        if (status_frame_width(obj, anim, f) > rest_width + 1) continue; // not closed yet / opening
        obj_t m = *obj;
        m.anim_frame = (u8)f;
        s32 x0, y0, x1, y1;
        status_frame_box(&m, anim, f, &x0, &y0, &x1, &y1);
        m.screen_x = 80 - (x0 + x1) / 2;
        m.screen_y = 80 - (y0 + y1) / 2;
        memset(scratch, 0, sizeof(scratch));
        display2(&m);
        s32 white = 0;
        for (s32 i = 0; i < 160 * 160; ++i) {
            const rgb_t* c = pal + scratch[i];
            if (scratch[i] && c->g > 180 && c->b > 200) ++white; // (white or pale blue: a twinkle)
        }
        if (white < best_white) {
            best_white = white;
            best = f;
        }
    }
    draw_buffer = saved_buffer;
    screen_width = saved_w;
    screen_height = saved_h;
    sprite_clipping(cx0, cx1, cy0, cy1);
    return best;
}

// Redraws the panel if anything on it changed. Returns true if it did.
bool status_panel_update(void) {
    world_info_t* wi = t_world_info + num_world_choice;
    obj_t* sbar = (sbar_obj_id >= 0 && sbar_obj_id < level.nb_objects) ? level.objects + sbar_obj_id : NULL;
    u32 key = (u32)num_world_choice | ((u32)wi->nb_cages << 5) | ((u32)Nb_total_cages << 8) |
              ((u32)(status_bar.lives & 0x7F) << 15) | ((u32)status_bar.num_wiz << 22) |
              ((u32)(ray.hit_points & 7) << 29);
    key ^= (u32)status_bar.max_hitp * 0x9E3779B1u ^ (u32)language * 0x85EBCA6Bu ^ (u32)(num_world * 16 + num_level) * 0xC2B2AE35u;
    if (!sbar) return false;
    // (Redrawn every frame while the extra-life or power sparkle plays on the counters, and once after.)
    static bool sparkle_shown;
    bool sparkle = id_Cling_1up != -1 || id_Cling_Pow != -1;
    if (key == status_panel_key && status_anim_frame < 0 && !sparkle && !sparkle_shown) return false;
    sparkle_shown = sparkle;
    status_panel_key = key;
    // A cage was just freed here: play the arrival.
    s32 cages_now = MIN(6, wi->nb_cages);
    if (status_place_shown == num_world_choice && cages_now > status_cages_shown && cages_now > 0) {
        status_anim_frame = STATUS_ARRIVAL_FIRST_FRAME;
    }
    status_place_shown = num_world_choice;
    status_cages_shown = cages_now;

    // Draw with the game's routines into the panel: point the drawing at it for a moment.
    u8* saved_buffer = draw_buffer;
    s32 saved_w = screen_width, saved_h = screen_height;
    s32 cx0, cx1, cy0, cy1;
    get_sprite_clipping(&cx0, &cx1, &cy0, &cy1);
    screen_width = BOTTOM_W;
    screen_height = BOTTOM_H;
    draw_buffer = status_panel;
    memset(status_panel, 0, sizeof(status_panel));
    sprite_clipping(0, BOTTOM_W, 0, BOTTOM_H);

    // World and level names, in the colours the world map uses.
    static const s8 world_colours[7] = {5, 5, 3, 0, 2, 1, 1};
    s32 world = wi->world;
    if (world >= 1 && world <= 6) {
        display_text(language_txt[23 + world], BOTTOM_W / 2, 26, 1, world_colours[world]);
    }
    if (wi->text) {
        display_text(wi->text, BOTTOM_W / 2, 48, 2, wi->color);
    }

    // Cages of this part of the world: the medallion with its Electoons (as when a cage is freed).
    obj_t* medal = status_find_object(TYPE_197_MEDAILLON_TOON);
    s32 cages = MIN(6, wi->nb_cages);
    if (medal) {
        obj_t m = *medal;
        m.main_etat = m.init_main_etat;
        m.sub_etat = (u8)(32 + MAX(0, cages - 1));
        m.anim_index = m.eta[m.main_etat][m.sub_etat].anim_index;
        anim_t* anim = m.animations + m.anim_index;
        // The animation flies the medallion in, rests (while an Electoon joins), then opens and flies off.
        // Show it at rest: before the Electoon arrives for 0 cages, otherwise just before it opens.
        s32 rest = status_medal_rest_frame(&m, anim, cages == 0);
        m.flags.flip_x = 0; // (keeps the object's own scale setting, 0: drawn as is, as in the level)
        s32 x0, y0, x1, y1;
        status_frame_box(&m, anim, rest, &x0, &y0, &x1, &y1); // (placed by the resting medallion)
        if (status_anim_frame >= 0 && status_anim_frame < rest) {
            m.anim_frame = (u8)status_anim_frame++; // (one frame of the arrival per game frame, as in the level)
        } else {
            m.anim_frame = (u8)rest;
            status_anim_frame = -1;
        }
        m.screen_x = 92 - (x0 + x1) / 2; // centred on (92, 124)
        m.screen_y = 124 - (y0 + y1) / 2;
        display2(&m);
        char text[32];
        snprintf(text, sizeof(text), "/%d of 6/", (int)cages);
        display_text(text, 92, 200, 2, 5);
    }

    // Lives and hit points, tings: the status bar's own sprites, laid out as on the main screen.
    s16 sx = 182, sy = 82;
    display_sprite_NoClip(sbar, 27, sx, sy, 1);
    status_digits(sbar, status_bar.lives, sx + 39, sy);
    if (ray.hit_points != (u8)-1) {
        display_sprite_NoClip(sbar, 17 + status_bar.hp_sprites[1], sx + 19, sy + 22, 1);
        if (status_bar.max_hitp == 4) {
            display_sprite_NoClip(sbar, 20 + status_bar.hp_sprites[0], sx + 43, sy + 22, 1);
        }
    }
    // The sparkle over the lives counter (extra life) or the hit points (power), placed as DISPLAY_CLING
    // places it on the main screen's counters (whose Rayman head is at 16,5).
    if (id_Cling_1up != -1 && level.objects[id_Cling_1up].timer == 0) {
        obj_t c = level.objects[id_Cling_1up];
        c.screen_x = sx + (61 - 16) - c.offset_bx;
        c.screen_y = sy - c.offset_hy;
        display2(&c);
    }
    if (id_Cling_Pow != -1 && level.objects[id_Cling_Pow].timer == 0) {
        obj_t c = level.objects[id_Cling_Pow];
        c.screen_x = sx + (66 - 16) - c.offset_bx;
        c.screen_y = sy + 9 - c.offset_hy;
        display2(&c);
    }
    s16 tx = 186, ty = 136;
    display_sprite_NoClip(sbar, 56, tx, ty, 1);
    status_digits(sbar, status_bar.num_wiz, tx + 32, ty);

    // Cages freed in the whole game.
    char total[32];
    snprintf(total, sizeof(total), "/cages/%d of 102/", (int)Nb_total_cages);
    display_text(total, 228, 182, 2, 1);

    draw_buffer = saved_buffer;
    screen_width = saved_w;
    screen_height = saved_h;
    sprite_clipping(cx0, cx1, cy0, cy1);
    return true;
}

// Forget the panel (e.g. a new level), so that it is redrawn.
void status_panel_reset(void) {
    status_panel_key = 0xFFFFFFFF;
    status_anim_frame = -1;
}

// Draws the bottom screen during play (0x00RRGGBB, 320x240): the panel over dark, slow rays (computed
// at half resolution).
void status_panel_render(u32* out, const rgb_t* palette) {
    if (!bottom_tables_ready) bottom_menu_init_tables();
    s32 band = ((s32)(sinf((float)status_pulse * (2.0f * 3.14159265f / 4096.0f)) * 75.0f)) + 105;
    const s32 outer = 200;
    static u16 brightness[256];
    for (s32 r = 0; r < 256; ++r) {
        u32 k;
        if (r < band) k = (u32)(r * 256 / band);
        else if (r < outer) k = (u32)((outer - r) * 256 / (outer - band));
        else k = 0;
        brightness[r] = (u16)(k * 90 >> 8); // dark: about a third of the menus' brightness
    }
    for (s32 y = 0; y < BOTTOM_H; y += 2) {
        for (s32 x = 0; x < BOTTOM_W; x += 2) {
            s32 i = y * BOTTOM_W + x;
            u32 c = bottom_wedge_colour[(u16)(bottom_angle[i] - status_spin) & 1023];
            u32 ray = menu_rays_darken_any(c, brightness[bottom_radius[i]]);
            u32* o = out + i;
            const u8* p = status_panel + i;
            #define STATUS_PIXEL(k, off) { u8 q = p[off]; if (q) { rgb_t pc = palette[q]; o[off] = ((u32)pc.r << 16) | ((u32)pc.g << 8) | pc.b; } else o[off] = ray; }
            STATUS_PIXEL(0, 0) STATUS_PIXEL(1, 1) STATUS_PIXEL(2, BOTTOM_W) STATUS_PIXEL(3, BOTTOM_W + 1)
            #undef STATUS_PIXEL
        }
    }
    status_spin += 4;
    status_pulse += 4;
}

//----------------------------------------------------------------------------------------------
// The title-screen menus (main menu, its options pages, "choose a game") on two screens: the top screen
// shows the title (the RAYMAN logo over the same animated rays), the bottom screen the menu over the rays.
//
// Those screens draw the menu over a still picture of pink rays: the main menu over picture 13 of
// VIGNET.DAT (rays with the RAYMAN logo), the options pages and "choose a game" over picture 12 (the
// same rays without the logo). The game sets menu_rays_background to that picture whenever it draws
// it for the frame; every pixel still showing it is background, the rest is the menu. The logo for the
// top screen is found once by comparing picture 13 with picture 12.
//
// A different logo can be used instead: a picture called LOGO.PNG in the game folder (with a
// transparent background) replaces the original logo. It is not part of this project; it is the
// player's own file, like the game data.

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb_image.h" // PNG reader (public domain, https://github.com/nothings/stb)
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#define MENU_LOGO_FILE "LOGO.PNG"
#define MENU_LOGO_MAX_W 300   // the original logo is about 310x100 pixels
#define MENU_LOGO_MAX_H 100
static u32 menu_logo[MENU_LOGO_MAX_W * MENU_LOGO_MAX_H]; // 0x00RRGGBB, with bit 24 set where the logo is opaque
static s32 menu_logo_w, menu_logo_h;
static bool menu_logo_tried;

// Loads LOGO.PNG (once) and shrinks it to the size of the original logo. Edges are kept hard (each
// pixel is either logo or background), like the game's own pictures.
static void menu_logo_load(void) {
    menu_logo_tried = true;
    FILE* fp = open_data_file(MENU_LOGO_FILE, false);
    if (!fp) return;
    int sw = 0, sh = 0, channels = 0;
    u8* png = stbi_load_from_file(fp, &sw, &sh, &channels, 4);
    fclose(fp);
    if (!png) {
        printf("%s: can't read it (%s)\n", MENU_LOGO_FILE, stbi_failure_reason());
        return;
    }
    s32 tw = MENU_LOGO_MAX_W, th = (s32)((s64)sh * tw / sw);
    if (th > MENU_LOGO_MAX_H) {
        th = MENU_LOGO_MAX_H;
        tw = (s32)((s64)sw * th / sh);
    }
    if (tw < 1 || th < 1) { stbi_image_free(png); return; }
    for (s32 y = 0; y < th; ++y) {
        s32 y0 = y * sh / th, y1 = MAX(y0 + 1, (y + 1) * sh / th);
        for (s32 x = 0; x < tw; ++x) {
            s32 x0 = x * sw / tw, x1 = MAX(x0 + 1, (x + 1) * sw / tw);
            // Average of the source pixels this pixel covers, weighted by their opacity.
            u32 r = 0, g = 0, b = 0, a = 0, count = 0;
            for (s32 sy = y0; sy < y1; ++sy) {
                const u8* p = png + (sy * sw + x0) * 4;
                for (s32 sx = x0; sx < x1; ++sx, p += 4) {
                    r += p[0] * p[3]; g += p[1] * p[3]; b += p[2] * p[3]; a += p[3]; ++count;
                }
            }
            u32 c = 0;
            if (a >= count * 128) {
                c = (1u << 24) | ((r / a) << 16) | ((g / a) << 8) | (b / a);
            }
            menu_logo[y * tw + x] = c;
        }
    }
    stbi_image_free(png);
    menu_logo_w = tw;
    menu_logo_h = th;
    printf("Using %s for the title logo\n", MENU_LOGO_FILE);
}

bool menu_rays_enabled;               // set by the platform layer
const u8* menu_rays_background;       // this frame's background picture (cleared by the platform layer each frame)

// The top screen's picture over the rays (0x00RRGGBB, with bit 24 set where there is something): the
// logo, and the copyright line from the main menu picture. Built when the main menu is first shown.
static u32 menu_top_overlay[SCREEN_WIDTH_ORIGINAL * SCREEN_HEIGHT_ORIGINAL];
static bool menu_top_overlay_ready;
#define MENU_COPYRIGHT_TOP 175 // rows below this in the main menu picture: the copyright line

// Called after the main menu picture (with the logo) has been loaded into EffetBufferNormal.
void menu_rays_main_menu_loaded(void) {
    if (!menu_rays_enabled || menu_top_overlay_ready) return;
    if (!menu_logo_tried) menu_logo_load();
    s32 w0 = SCREEN_WIDTH_ORIGINAL, n = SCREEN_WIDTH_ORIGINAL * SCREEN_HEIGHT_ORIGINAL;
    u8* plain = (u8*)malloc(n);
    if (!plain) return;
    s16 w = 0, h = 0;
    rgb_palette_t palette = rvb_plan3; // (loading a picture also overwrites the palette it is given)
    LoadPcxOptions(plain, 12, &w, &h, &palette);
    if (w == SCREEN_WIDTH_ORIGINAL && h == SCREEN_HEIGHT_ORIGINAL) {
        const rgb_t* c = rvb_plan3.colors;
        memset(menu_top_overlay, 0, sizeof(menu_top_overlay));
        for (s32 i = 0; i < n; ++i) {
            u8 a = EffetBufferNormal[i], b = plain[i];
            // The logo and copyright line are what differs between the two pictures. A few ray pixels
            // differ slightly, so only clearly different colours count. The dark bar behind the letters
            // (colours 110 and 191) is left out, so the rays show there.
            s32 diff = abs(c[a].r - c[b].r) + abs(c[a].g - c[b].g) + abs(c[a].b - c[b].b);
            bool keep = a != b && diff > 40 && a != 110 && a != 191;
            if (menu_logo_w > 0 && i < MENU_COPYRIGHT_TOP * w0) keep = false; // LOGO.PNG replaces the logo
            if (keep) menu_top_overlay[i] = (1u << 24) | ((u32)c[a].r << 16) | ((u32)c[a].g << 8) | c[a].b;
        }
        if (menu_logo_w > 0) {
            // LOGO.PNG in the middle of the top screen.
            s32 lx = (w0 - menu_logo_w) / 2;
            s32 ly = (SCREEN_HEIGHT_ORIGINAL - menu_logo_h) / 2;
            for (s32 y = 0; y < menu_logo_h; ++y) {
                for (s32 x = 0; x < menu_logo_w; ++x) {
                    u32 lc = menu_logo[y * menu_logo_w + x];
                    if (lc) menu_top_overlay[(ly + y) * w0 + lx + x] = lc;
                }
            }
        }
        menu_top_overlay_ready = true;
    }
    free(plain);
}

// Called after a picture without the logo has been loaded into EffetBufferNormal.
void menu_rays_other_menu_loaded(void) {
}

// Darkens a 0x00RRGGBB colour by k/256.
static inline u32 menu_rays_darken(u32 c, u32 k) {
    return ((((c >> 16) & 0xFF) * k >> 8) << 16) | ((((c >> 8) & 0xFF) * k >> 8) << 8) | ((c & 0xFF) * k >> 8);
}

// The menu layer of the last title-menu frame: 0x100 | colour for menu pixels, 0 for background. Kept,
// with a copy of that frame, for the frames in between where the game shows the same picture without
// drawing it again (fading out when leaving a menu): those are shown the same way.
static u16 menu_layer[SCREEN_WIDTH_ORIGINAL * SCREEN_HEIGHT_ORIGINAL];
static u8 menu_layer_frame[SCREEN_WIDTH_ORIGINAL * SCREEN_HEIGHT_ORIGINAL];
static bool menu_layer_valid;
static bool menu_layer_on_map; // the last one was a question on the world map (see menu_map_dialog_active)

static void menu_rays_compose(u32* top, u32* top_right, u32* bottom, s32 w, s32 h, const rgb_t* palette, float strength);

// Draws both screens for a title-screen menu (0x00RRGGBB, row by row):
// - top (320x200, the game's picture size; not drawn if top is NULL): the animated rays with the logo. For 3D (top_right not
//   NULL, strength = 3D slider), top gets the left eye's picture and top_right the right eye's: the
//   rays deep behind the screen, the logo in front of it;
// - bottom (320x240): the menu from the game's picture src (320x200, in the middle), over the rays.
// Pixels of src still showing this frame's background picture count as background.
void menu_rays_render(u32* top, u32* top_right, u32* bottom, const u8* src, s32 w, s32 h, const rgb_t* palette, float strength) {
    const u8* bg = menu_rays_background;
    if (!bg || w != SCREEN_WIDTH_ORIGINAL || h != SCREEN_HEIGHT_ORIGINAL) return;
    s32 n = w * h;
    for (s32 i = 0; i < n; ++i) {
        menu_layer[i] = (src[i] != bg[i]) ? (u16)(0x100 | src[i]) : 0;
    }
    memcpy(menu_layer_frame, src, n);
    menu_layer_valid = true;
    menu_layer_on_map = dans_la_map_monde;
    menu_rays_compose(top, top_right, bottom, w, h, palette, strength);
}

// True if this frame shows the same picture as the last title-menu frame (see menu_layer).
bool menu_rays_holding(const u8* src, s32 w, s32 h) {
    return menu_rays_enabled && menu_layer_valid && !OptionGame && !RaymanDansUneMapDuJeu &&
           w == SCREEN_WIDTH_ORIGINAL && h == SCREEN_HEIGHT_ORIGINAL &&
           memcmp(src, menu_layer_frame, w * h) == 0;
}

// Draws both screens again from the last title-menu frame (with this frame's palette and rays).
void menu_rays_render_held(u32* top, u32* top_right, u32* bottom, const rgb_t* palette, float strength) {
    menu_rays_compose(top, top_right, bottom, SCREEN_WIDTH_ORIGINAL, SCREEN_HEIGHT_ORIGINAL, palette, strength);
}

// 3D depths of the title screen, in pixels at full slider (positive: behind the screen), and the
// shift of a layer for one eye (eye -1: left, +1: right), the same way as stereo.c.
#define MENU_RAYS_DEPTH 4.0f
#define MENU_LOGO_DEPTH (-2.5f)
static inline s32 menu_rays_shift(float depth, float strength, s32 eye) {
    float d = depth * strength;
    return (s32)(d + (d >= 0 ? 0.5f : -0.5f)) * eye;
}

static void menu_rays_compose(u32* top, u32* top_right, u32* bottom, s32 w, s32 h, const rgb_t* palette, float strength) {
    if (!bottom_tables_ready) bottom_menu_init_tables();

    // The menus fade in and out by darkening the palette: darken everything else the same way.
    const rgb_t* full = rvb[current_pal_id].colors;
    u32 now = 0, normal = 0;
    for (s32 i = 0; i < 256; ++i) {
        now += palette[i].r + palette[i].g + palette[i].b;
        normal += full[i].r + full[i].g + full[i].b;
    }
    u32 fade = (normal == 0 || now >= normal - normal / 32) ? 256 : (u32)((u64)now * 256 / normal);

    s32 band = ((s32)(sinf((float)bottom_pulse * (2.0f * 3.14159265f / 4096.0f)) * 75.0f)) + 105;
    const s32 outer = 200;
    static u16 brightness[256];
    for (s32 r = 0; r < 256; ++r) {
        u32 k;
        if (r < band) k = (u32)(r * 256 / band);
        else if (r < outer) k = (u32)((outer - r) * 256 / (outer - band));
        else k = 0;
        brightness[r] = (u16)(k * fade >> 8);
    }

    // Top: rays (the middle 320x200 of the 320x240 tables) and the logo; in 3D once for each eye, each
    // layer sampled from x - shift.
    s32 tx = (BOTTOM_W - w) / 2, ty = (BOTTOM_H - h) / 2;
    for (s32 eye = (top_right ? -1 : 0); top && eye <= (top_right ? 1 : 0); eye += 2) {
        u32* out = (eye > 0) ? top_right : top;
        s32 shift_rays = menu_rays_shift(MENU_RAYS_DEPTH, strength, eye);
        s32 shift_logo = menu_rays_shift(MENU_LOGO_DEPTH, strength, eye);
        for (s32 y = 0; y < h; ++y) {
            s32 ti = (y + ty) * BOTTOM_W + tx;
            const u32* ov = menu_top_overlay + y * w;
            u32* o = out + y * w;
            for (s32 x = 0; x < w; ++x) {
                s32 lx = x - shift_logo;
                u32 lc = (menu_top_overlay_ready && lx >= 0 && lx < w) ? ov[lx] : 0;
                if (lc) {
                    o[x] = menu_rays_darken(lc, fade);
                } else {
                    s32 rx = MAX(0, MIN(w - 1, x - shift_rays));
                    u32 c = bottom_wedge_colour[(u16)(bottom_angle[ti + rx] - bottom_spin) & 1023];
                    o[x] = menu_rays_darken(c, brightness[bottom_radius[ti + rx]]);
                }
            }
        }
        if (eye == 0) break;
    }

    // Bottom: the menu, in the middle, over the rays.
    for (s32 y = 0; y < BOTTOM_H; ++y) {
        s32 sy = y - ty;
        bool in = sy >= 0 && sy < h;
        const u16* layer_row = menu_layer + sy * w;
        u32* o = bottom + y * BOTTOM_W;
        for (s32 x = 0; x < BOTTOM_W; ++x) {
            s32 sx = x - tx;
            u16 m = (in && sx >= 0 && sx < w) ? layer_row[sx] : 0;
            if (m) {
                rgb_t c = palette[m & 0xFF];
                o[x] = ((u32)c.r << 16) | ((u32)c.g << 8) | c.b;
            } else {
                s32 i = y * BOTTOM_W + x;
                u32 c = bottom_wedge_colour[(u16)(bottom_angle[i] - bottom_spin) & 1023];
                o[x] = menu_rays_darken(c, brightness[bottom_radius[i]]);
            }
        }
    }
    bottom_spin += 10;
    bottom_pulse += 10;
}

// True when this frame is a title-screen menu to show this way (at the original picture size).
bool menu_rays_active(void) {
    return menu_rays_enabled && menu_rays_background && !OptionGame && !dans_la_map_monde &&
           SCREEN_WIDTH == SCREEN_WIDTH_ORIGINAL && SCREEN_HEIGHT == SCREEN_HEIGHT_ORIGINAL;
}

// True when this frame is a question on the world map (quit the game? / erase the saved game?): drawn
// the same way as the title menus, over the map in black and white (menu_rays_background). The map
// stays on the top screen and the question goes on the bottom screen, like the pause menu.
bool menu_map_dialog_active(void) {
    return menu_rays_enabled && menu_rays_background && !OptionGame && dans_la_map_monde &&
           SCREEN_WIDTH == SCREEN_WIDTH_ORIGINAL && SCREEN_HEIGHT == SCREEN_HEIGHT_ORIGINAL;
}

// True if the last menu frame kept by menu_rays_holding was a question on the world map.
bool menu_rays_held_on_map(void) {
    return menu_layer_on_map;
}
