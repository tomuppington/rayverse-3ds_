
// NOTE: unsure about the source filename map.c (in the function order, should come after neige.c and before ngawe.c)


//5C4A0
void deter_vide_plein_panach(void* a1, s16 a2) {
    print_once("Not implemented: deter_vide_plein_panach"); //stub
}

//5C4DC
void deter_nb_blocks(void* a1, s16 a2, s16 a3, s32* a4, s32* a5) {
    print_once("Not implemented: deter_nb_blocks"); //stub
}

//5C564
void Copy_Blocks(mem_t* buffer, void* a2, s16 a3, s16 a4) {
    print_once("Not implemented: Copy_Blocks"); //stub
}

//5C82C
void construct_MAP(mem_t* mem, big_map_t* big_map, void* map_blocks) {
    big_map->map = mp.map;
    big_map->tile_texture_offsets = (u8**)block_malloc(mem, mp.length * sizeof(u8*));
    big_map->map_blocks = (u8*)map_blocks;
    big_map->display_map = (display_map_t*)block_malloc(mem, sizeof(display_map_t));
    for (s32 i = 0; i < mp.length; ++i) {
        s32 texture_offset = block_add[big_map->map[i].texture_id];
        big_map->tile_texture_offsets[i] = big_map->map_blocks + texture_offset;
        u32 v11;
        if (texture_offset > 288 * nb_blocks_plein) {
            v11 = *(u32*)(big_map->tile_texture_offsets[i] + 512);
        } else {
            v11 = *(u32*)(big_map->tile_texture_offsets[i] + 256);
        }
        u8 transparency;
        if (v11 == 0xAAAAAAAA) {
            transparency = 1;
        } else if (v11 == 0x55555555) {
            transparency = 0;
        } else {
            transparency = 2;
        }
        big_map->map[i].transparency = transparency;
    }
}

//5C8FC
void init_build_map(big_map_t* a1) {
    a1->width = mp.width;
    a1->height = mp.height;
    update_display_map(a1);
}

//5C914
void update_display_map(big_map_t* a1) {
    display_map_t* display_map = a1->display_map;

    s32 y = Bloc_lim_H1 + ymap;
    if (y < 0) {
        y = 0; // added bounds clip
    }
    s32 x = Bloc_lim_W1 + xmap - 8;
    if (x < 0) {
        x = 0; // added bounds clip
    }
    display_map->x = Bloc_lim_W1 - (x & 0xF);
    display_map->tile_index = (a1->width * (y >> 4)) + (x >> 4);
    display_map->y= Bloc_lim_H1 - (y & 0xF);

    s32 v6 = Bloc_lim_W2 - display_map->x;
    s32 v7 = Bloc_lim_H2 - display_map->y;
    display_map->width_in_tiles = (v6 >> 4) + ((v6 & 0xF) != 0);
    display_map->height_in_tiles = (v7 >> 4) + ((v7 & 0xF) != 0);

    BufferNormalDeplt = 0;
    // NOTE: in the PC version, there is a strange -4 here (not in the Android version). Also see INIT_GAME_MODE_NORMAL()
    DrawBufferNormal = DrawBufferNormalIni;
    draw_buffer = DrawBufferNormalIni;
}

// 3DS port: a 4:3 play area (320x240) in the middle of the 400x240 picture, for levels where the
// full width doesn't suit (as Rayman Redemption does for its boss levels). The picture stays 1:1 and
// the lives and tings counters stay in the screen's corners; the sides are black.
bool play_window_boxed;

// Width of each black side (0 when the play area fills the picture).
s32 play_window_border(void) {
    return play_window_boxed ? (SCREEN_WIDTH - SCREEN_WIDTH_ORIGINAL) / 2 : 0;
}

// The play window for screen size setting 'size' (the PC version's smaller-window option; 0 = full).
void set_play_window(s32 size) {
    s32 border = play_window_border();
    set_xywhmap(TabW1[size] + border, (TabW2[size] + SCREEN_WIDTH - SCREEN_WIDTH_ORIGINAL - border),
                TabH1[size], (TabH2[size] + SCREEN_HEIGHT - SCREEN_HEIGHT_ORIGINAL));
}

//5C9F0
void set_default_Bloc_clipping(void) {
    Bloc_lim_H1 = 0;
    Bloc_lim_H2 = SCREEN_HEIGHT;
#ifdef FIX_BLACK_BAR_ON_THE_LEFT
    Bloc_lim_W1 = 0;
#else
    Bloc_lim_W1 = 4;
#endif
    Bloc_lim_W2 = SCREEN_WIDTH;
    Bloc_lim_W1 += play_window_border();
    Bloc_lim_W2 -= play_window_border();
}


// Native resolution (3DS port): called once a level has been loaded. Uses the 400x240 picture if the
// 1:1 display mode is on and the level is big enough; otherwise the original 320x200.
void widescreen_level_start(void) {
    s32 w = SCREEN_WIDTH_ORIGINAL;
    // Betilla's levels (cutscenes) keep the original picture: their text is laid out for it.
    bool fairy_level = (num_world == 1 && (num_level == 3 || num_level == 8 || num_level == 17)) ||
                       (num_world == 2 && num_level == 11) || (num_world == 3 && num_level == 11);
    // Levels whose background scrolls in vertical strips (type 1) are laid out for 320 pixels across.
    bool strip_background = Type_Scroll[10 * (num_world - 1) + Num_Fond] == 1;
    s32 h = SCREEN_HEIGHT_ORIGINAL;
    // Levels played in a 4:3 window in the middle of the picture (see play_window_boxed): Moskito's
    // Nest, where the full width is too slow, and levels narrower than the screen (small boss arenas).
    bool heavy_level = num_world == 1 && (num_level == 14 || num_level == 15);
    bool narrow_level = 16 * mp.width < SCREEN_WIDTH_MAX && 16 * mp.width >= SCREEN_WIDTH_ORIGINAL;
    play_window_boxed = false;
    if (widescreen_enabled && GameModeVideo == MODE_NORMAL && !fairy_level && !strip_background &&
        16 * mp.height >= SCREEN_HEIGHT_MAX && (16 * mp.width >= SCREEN_WIDTH_MAX || narrow_level)) {
        w = SCREEN_WIDTH_MAX;
        h = SCREEN_HEIGHT_MAX;
        play_window_boxed = heavy_level || narrow_level;
    }
    screen_width = w;
    screen_height = h;
    set_default_Bloc_clipping();
    sprite_clipping(Bloc_lim_W1, Bloc_lim_W2, Bloc_lim_H1, Bloc_lim_H2);
    if (play_window_boxed) {
        memset(DrawBufferNormal, 0, SCREEN_WIDTH * SCREEN_HEIGHT); // black sides
    }
    if (GameModeVideo == MODE_NORMAL) {
        calc_bande_scroll_y(plan0_height); // the background's vertical scrolling depends on the screen height
    }
}

// Back to the original 320 pixels (world map, menus).
void widescreen_level_end(void) {
    play_window_boxed = false;
    screen_width = SCREEN_WIDTH_ORIGINAL;
    screen_height = SCREEN_HEIGHT_ORIGINAL;
    set_default_Bloc_clipping();
}

//5CA24
void set_xymapini(void) {
    xmapmax = 16 * mp.width - Bloc_lim_W2;
    ymapmax = 16 * mp.height - Bloc_lim_H2;
    xmapmaxini = 16 * mp.width - SCREEN_WIDTH;
    ymapmaxini = 16 * mp.height - SCREEN_HEIGHT;
    // (3DS port: in a 4:3 window, a map narrower than the window sits at the window's right edge, i.e.
    // in the middle of the picture, like in the PC version's smaller-window mode.)
    if (xmapmax < 0 && !play_window_boxed) {
        xmapmax = 0;
    }
    if (ymapmax < 0) {
        ymapmax = 0;
    }
    scroll_start_x = 8 - Bloc_lim_W1;
    scroll_end_x = xmapmax + 8;
    scroll_start_y = -Bloc_lim_H1;
    scroll_end_y = ymapmax;
}

//5CAD0
void set_xymap(void) {
    s32 v0 = xmap;
    if (xmap < -Bloc_lim_W1) {
        xmap = -Bloc_lim_W1;
    }
    if (xmap > xmapmax) {
        xmap = xmapmax;
    }
    if (ymap < -Bloc_lim_H1) {
        ymap = -Bloc_lim_H1;
    }
    if (ymap > ymapmax) {
        ymap = ymapmax;
    }
}

//5CB40
void set_whmap(s32 a1, s32 a2, void* a3) {
    print_once("Not implemented: set_whmap"); //stub
}

//5CD64
void recaleRaysurlesBords(void) {
    if (ray.x + ray.offset_bx <= xmap + Bloc_lim_W1 +16) {
        ray.x = xmap + Bloc_lim_W1 - ray.offset_bx + 16;
        ray.screen_x = ray.x - scroll_start_x + 8;
    }
    if (ray.x + ray.offset_bx >= xmap + Bloc_lim_W2 - 23) {
        ray.x = xmap + Bloc_lim_W2 - 32;
        ray.screen_x = ray.x - xmap + 8;
    }
}

//5CE08
void set_xywhmap(s16 xmin, s16 xmax, s16 ymin, s16 ymax) {
    s32 delta_W1 = xmin - Bloc_lim_W1;
    s32 delta_W2 = Bloc_lim_W2 - xmax;
    s32 delta_H1 = ymin - Bloc_lim_H1;
    s32 delta_H2 = Bloc_lim_H2 - ymax;

    Bloc_lim_H1 = ymin;
    Bloc_lim_H2 = ymax;
    Bloc_lim_W1 = xmin;
    Bloc_lim_W2 = xmax;

    if (ymax > SCREEN_HEIGHT) {
        Bloc_lim_H2 = SCREEN_HEIGHT;
    }
    if (ymin < 0) {
        Bloc_lim_H1 = 0;
    }
    if (xmax > SCREEN_WIDTH) {
        Bloc_lim_W2 = SCREEN_WIDTH;
    }
    if (xmin < 0) {
        Bloc_lim_W1 = 0;
    }

    if (Bloc_lim_W1 >= 4) {
        scroll_start_x -= delta_W1;
        scroll_end_x += delta_W2;
        if (scroll_start_x > 12) {
            scroll_start_x = 12;
        }
        if (scroll_end_x < xmapmaxini) {
            scroll_end_x = xmapmaxini + 8;
        }
        xmapmax += delta_W2;
        if (xmapmax < xmapmaxini) {
            xmapmax = xmapmaxini;
        }
        if (xmapmax < xmap) {
            xmapmax = xmap;
        }
    }

    if (Bloc_lim_H1 >= 0) {
        scroll_start_y -= delta_H1;
        scroll_end_y += delta_H2;
        if (scroll_start_y > 0) {
            scroll_start_y = 0;
        }
        if (scroll_end_y < ymapmaxini) {
            scroll_end_y = ymapmaxini;
        }
        ymapmax += delta_H2;
        if (ymapmax < ymapmaxini) {
            ymapmax = ymapmaxini;
        }
        if (ymapmax < ymap) {
            ymapmax = ymap;
        }
    }

    if (bossScrollStartX != 0) {
        bossScrollStartX = scroll_start_x;
        bossScrollEndX = scroll_end_x;
    }
    if (mp.width <= 20 && Bloc_lim_W1 == 4) {
        scroll_start_x = scroll_end_x;
        xmap = scroll_end_x;
    }
    set_xymap();
    if (scrollLocked) {
        if (!((num_world == world_3_mountain && num_level == 10) || (num_world == world_2_music && num_level == 15))) {
            scroll_end_x = xmap;
            scroll_start_x = xmap;
        }
    }

    if (xmapmax + Bloc_lim_W2 > 16 * mp.width) {
        xmapmax = 16 * mp.width - Bloc_lim_W2;
    }
    if (ymapmax + Bloc_lim_H2 > 16 * mp.height) {
        ymapmax = 16 * mp.height - Bloc_lim_H2;
    }
    Bloc_lim_W1_Aff = Bloc_lim_W1;
    Bloc_lim_W2_Aff = Bloc_lim_W2;
    Bloc_lim_H1_Aff = Bloc_lim_H1;
    Bloc_lim_H2_Aff = Bloc_lim_H2;
    if (OptionGame) {
        recaleRaysurlesBords();
    }
}

//5D144
void MaskScrollDiffSprites(mem_t* buffer) {
    for (s32 i = 1; i < ScrollDiffSprites->nb_sprites; ++i) {
        sprite_t* sprite = ScrollDiffSprites->sprites + i;
        Scroll_Masque[i] = block_malloc(buffer, sprite->outer_height * sprite->inner_height);
        u8* masque_pos = Scroll_Masque[i];
        u8* image_pos = ScrollDiffSprites->img_buffer + sprite->offset_in_atlas;
        for (s32 y = 0; y < sprite->outer_height; ++y) {
            for (s32 x = 0; x < sprite->outer_width; ++x) {
                *masque_pos = *image_pos == 0 ? 0 : 0xFF;
                ++masque_pos;
                ++image_pos;
            }
        }
    }
}

//5D230
void DRAW_MAP(u8* draw_buf, big_map_t* big_map) {
    display_map_t* display_map = big_map->display_map;
    if (display_map->height_in_tiles > 1 && display_map->width_in_tiles > 1) {
        u8* draw_pos = draw_buf + SCREEN_WIDTH * display_map->y + display_map->x;
        for (s32 tile_y = 0; tile_y < display_map->height_in_tiles; ++tile_y) {
            for (s32 tile_x = 0; tile_x < display_map->width_in_tiles; ++tile_x) {
                s32 tile_index = display_map->tile_index + tile_y * big_map->width + tile_x;
                map_tile_t* tile = big_map->map + tile_index;
                s32 x = display_map->x + 16 * tile_x;
                s32 y = display_map->y + 16 * tile_y;
                if (tile->transparency != 0) {
                    vec2b_t size = {16, 16};
                    if (tile->transparency == 1) {
                        // NOTE: We don't need to do fancy optimized drawing routines here, the regular one also works fine.
                        DrawSpriteNormal256(x, 0, y, size, draw_buf, big_map->tile_texture_offsets[tile_index]);
                        //Display_Bloc_Plein_Clippe(...);
                    } else {
                        DrawSpriteNormal256(x, 0, y, size, draw_buf, big_map->tile_texture_offsets[tile_index]);
                        //Display_Bloc_Clippe(...);
                    }
                }
            }
        }

    }

    // Optionally draw collision blocks
    if (debug_show_block_types && display_map->height_in_tiles > 1 && display_map->width_in_tiles > 1) {
        for (s32 tile_y = 0; tile_y < display_map->height_in_tiles; ++tile_y) {
            for (s32 tile_x = 0; tile_x < display_map->width_in_tiles; ++tile_x) {
                s32 tile_index = display_map->tile_index + tile_y * big_map->width + tile_x;
                map_tile_t* tile = big_map->map + tile_index;
                s32 x = display_map->x + 16 * tile_x;
                s32 y = display_map->y + 16 * tile_y;
                if (tile->tile_type != 0) {
                    vec2b_t size = { 16, 16 };
                    DrawSpriteNormal256(x, 0, y, size, draw_buf, &block_types_img_data[tile->tile_type * 16 * 16]);
                }
            }
        }
    }
}

//5D5B0
void INIT_GAME_MODE_NORMAL(void) {
    sprite_clipping(Bloc_lim_W1, Bloc_lim_W2, Bloc_lim_H1, Bloc_lim_H2);
    DrawBufferNormalIni = DrawBufferNormal;
    //DrawBufferNormal += 4; //? Maybe some strange workaround for something? Doesn't make sense, commenting out for now
    // also see update_display_map()
    Copy_Plan0_To_Buf = Copy_Plan0Diff_To_Buf;
    init_build_map(&BIG_MAP);
    N_CLRSCR(DrawBufferNormal);
}

//5D620
void FIN_GAME_MODE_NORMAL(void) {
    DrawBufferNormal = DrawBufferNormalIni;
    draw_buffer = DrawBufferNormalIni;
}
