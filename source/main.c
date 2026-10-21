// Rayverse 3DS - Stage 1: pipeline test.
// Draws a fake "320x200, 256-colour" picture (like Rayman 1's screen) in the
// middle of the 3DS top screen, with a colour palette that cycles each frame.
// Press START to quit.

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define GAME_W   320
#define GAME_H   200
#define SCREEN_W 400
#define SCREEN_H 240

static uint8_t indexed[GAME_W * GAME_H]; // one palette number per pixel
static uint8_t palette[256][3];          // red, green, blue for each number

static void make_test_picture(void)
{
    for (int i = 0; i < 256; i++) {
        palette[i][0] = (uint8_t)i;
        palette[i][1] = (uint8_t)(i * 5);
        palette[i][2] = (uint8_t)(255 - i);
    }
    for (int y = 0; y < GAME_H; y++)
        for (int x = 0; x < GAME_W; x++)
            indexed[y * GAME_W + x] = (uint8_t)((x / 20) + (y / 20) * 16);
}

// The 3DS screen memory is stored sideways (columns, bottom to top, blue-green-red).
static void draw_frame(uint8_t *fb, unsigned frame)
{
    memset(fb, 0, SCREEN_W * SCREEN_H * 3);
    const int ox = (SCREEN_W - GAME_W) / 2;
    const int oy = (SCREEN_H - GAME_H) / 2;
    for (int y = 0; y < GAME_H; y++) {
        for (int x = 0; x < GAME_W; x++) {
            const uint8_t *c = palette[(indexed[y * GAME_W + x] + frame) & 255];
            uint8_t *p = fb + ((ox + x) * SCREEN_H + (SCREEN_H - 1 - (oy + y))) * 3;
            p[0] = c[2];
            p[1] = c[1];
            p[2] = c[0];
        }
    }
}

int main(void)
{
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);

    bool isNew = false;
    APT_CheckNew3DS(&isNew);
    osSetSpeedupEnable(true); // faster CPU mode on New 3DS (harmless on old models)

    printf("Rayverse 3DS - Stage 1 test\n\n");
    printf("Console: %s\n\n", isNew ? "New 3DS" : "Old 3DS");
    printf("Top screen should show a colourful\n");
    printf("grid with moving colours.\n\n");
    printf("Press START to quit.\n");

    make_test_picture();

    unsigned frame = 0;
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START)
            break;

        uint8_t *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        draw_frame(fb, frame++);

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    gfxExit();
    return 0;
}
