// background.c -- the static scenery (sky, sun, clouds, mountains, hills, ground
// and the wooden slingshot frame).
//
// It never changes, so it is painted once into an off-screen copy and then
// block-copied to the screen every frame. That makes it nearly free per frame,
// which is why it can afford per-pixel shading.
#include "render.h"
#include "gfx.h"
#include "game.h"

#include <math.h>
#include <string.h>

static char bg_cache[GFX_BUF_BYTES];

// Cheap repeatable "random" number from an integer (same every run).
static unsigned hash(unsigned n) {
    n ^= n >> 15; n *= 0x2c1b3c6dU;
    n ^= n >> 12; n *= 0x297a2d39U;
    n ^= n >> 15;
    return n;
}

// ---------------------------------------------------------------------
// Sky
// ---------------------------------------------------------------------
static void sky(void) {
    // deep blue overhead, fading to a pale haze at the horizon
    for (int y = 0; y < GROUND_Y; y++) {
        if (y < 210) gfx_hspan_mix(0, GFX_W - 1, y, BLUE, LIGHT_BLUE, 2 + y * 14 / 210);
        else         gfx_hspan_mix(0, GFX_W - 1, y, LIGHT_BLUE, WHITE, (y - 210) * 9 / (GROUND_Y - 210));
    }
}

static void sun(float cx, float cy) {
    // soft glow, then the disc
    for (int y = (int)cy - 52; y <= (int)cy + 52; y++) {
        for (int x = (int)cx - 52; x <= (int)cx + 52; x++) {
            float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
            if (d < 50.0f) gfx_px_blend(x, y, d < 34.0f ? YELLOW : WHITE, (int)(11.0f * (1.0f - d / 50.0f)));
        }
    }
    gfx_disc(cx, cy, 21, ORANGE);
    gfx_disc(cx, cy, 19, YELLOW);
    gfx_disc_blend(cx - 5, cy - 6, 9, WHITE, 9);
}

// A soft blob lit from above: `top` color fading into `bottom` color.
static void lit_blob(float cx, float cy, float rx, float ry, char top, char bottom, int max_level) {
    for (int y = (int)ceilf(cy - ry); y <= (int)floorf(cy + ry); y++) {
        float t = (y - cy) / ry;                       // -1 top .. +1 bottom
        float half = rx * sqrtf(1.0f - t * t);
        int level = (int)((t + 0.15f) * max_level);    // shading starts just above the middle
        gfx_hspan_mix(gfx_round(cx - half), gfx_round(cx + half), y, top, bottom, level < 0 ? 0 : level);
    }
}

static void cloud(float x, float y, float s) {
    lit_blob(x - 22 * s, y + 2 * s, 16 * s, 10 * s, WHITE, LIGHT_BLUE, 8);
    lit_blob(x + 24 * s, y + 3 * s, 15 * s,  9 * s, WHITE, LIGHT_BLUE, 8);
    lit_blob(x - 6 * s,  y - 7 * s, 17 * s, 14 * s, WHITE, LIGHT_BLUE, 6);
    lit_blob(x + 10 * s, y - 3 * s, 14 * s, 12 * s, WHITE, LIGHT_BLUE, 6);
    lit_blob(x,          y + 5 * s, 30 * s,  8 * s, WHITE, LIGHT_BLUE, 9);
}

// ---------------------------------------------------------------------
// Land
// ---------------------------------------------------------------------

// Hazy far-off mountain ridge.
static void mountains(void) {
    for (int x = 0; x < GFX_W; x++) {
        float h = 78.0f + 26.0f * sinf(x * 0.0125f + 0.6f) + 15.0f * sinf(x * 0.031f + 2.1f)
                + 7.0f * sinf(x * 0.071f + 4.0f);
        int top = GROUND_Y - (int)h;
        for (int y = top; y < GROUND_Y; y++) {
            int depth = y - top;
            if (depth < 2)       gfx_px_mix(x, y, LIGHT_BLUE, WHITE, 6);          // lit rim
            else                 gfx_px_mix(x, y, LIGHT_BLUE, BLUE, 5 + (depth > 40 ? 0 : (40 - depth) / 8));
        }
    }
}

// Rounded grassy hill sitting on the ground line, lit from the upper left.
static void hill(float cx, float rx, float ry, char dark, char mid, char light) {
    for (int y = GROUND_Y - (int)ry; y < GROUND_Y; y++) {
        float ny = (y - GROUND_Y) / ry;                 // -1 top .. 0 base
        float half = rx * sqrtf(1.0f - ny * ny);
        for (int x = (int)(cx - half); x <= (int)(cx + half); x++) {
            float nx = (x - cx) / rx;
            float light_amt = -0.55f * nx - 0.85f * ny - 0.15f;   // facing up-left = bright
            int k = (int)(light_amt * 24.0f);
            if (k >= 8)      gfx_px_mix(x, y, mid, light, k - 8 > 12 ? 12 : k - 8);
            else if (k >= 0) gfx_px(x, y, mid);
            else             gfx_px_mix(x, y, mid, dark, -k > 14 ? 14 : -k);
        }
    }
}

static void bush(float cx, float base_y, float r) {
    hill(cx - r * 0.7f, r * 0.8f, r * 0.8f, DARK_GREEN, DARK_GREEN, MED_GREEN);
    hill(cx + r * 0.7f, r * 0.8f, r * 0.75f, DARK_GREEN, DARK_GREEN, MED_GREEN);
    hill(cx, r, r * 1.15f, DARK_GREEN, DARK_GREEN, MED_GREEN);
    (void)base_y;
}

#define GRASS_H 15   // depth of the grass strip below GROUND_Y

static void ground(void) {
    // dirt: brown (orange + black dither), darker with depth, with wavy layers
    for (int y = GROUND_Y; y < GFX_H_PX; y++) {
        for (int x = 0; x < GFX_W; x++) {
            int depth = y - GROUND_Y;
            float wave = 3.0f * sinf(x * 0.021f + depth * 0.05f) + 2.0f * sinf(x * 0.057f);
            int band = ((int)(depth + wave) / 11) & 1;
            int level = 5 + depth / 9 + band * 2;
            if ((hash((unsigned)(x * 7919 + y * 104729)) & 31) == 0) level += 4;   // grit
            gfx_px_mix(x, y, DARK_ORANGE, BLACK, level > 13 ? 13 : level);
        }
    }
    // pebbles
    for (unsigned i = 0; i < 26; i++) {
        unsigned h = hash(i + 91);
        float x = (float)(h % GFX_W), y = (float)(GROUND_Y + GRASS_H + 6 + (h >> 10) % 34);
        float rx = 2.5f + (float)((h >> 20) % 4), ry = rx * 0.6f;
        gfx_ellipse(x, y + 1, rx + 1, ry + 1, BLACK);
        gfx_ellipse(x, y, rx, ry, (h >> 24) & 1 ? ORANGE : DARK_ORANGE);
    }

    // grass strip: bright on top, darker lower down, ragged bottom edge
    for (int x = 0; x < GFX_W; x++) {
        int ragged = (int)(hash((unsigned)x / 3 + 500) % 5);
        int bottom = GROUND_Y + GRASS_H - 3 + ragged;
        for (int y = GROUND_Y; y <= bottom; y++) {
            int depth = y - GROUND_Y;
            if (y >= bottom - 1)   gfx_px(x, y, DARK_GREEN);
            else if (depth < 4)    gfx_px_mix(x, y, GREEN, MED_GREEN, depth * 2);
            else                   gfx_px_mix(x, y, MED_GREEN, DARK_GREEN, (depth - 4) * 16 / GRASS_H);
        }
        gfx_px_blend(x, bottom + 1, BLACK, 10);    // shadow under the turf
        gfx_px_blend(x, bottom + 2, BLACK, 5);
    }
    // blades sticking up behind the play field
    for (int x = 0; x < GFX_W; x++) {
        int h = render_grass_blade(x) + 2;
        for (int k = 1; k <= h; k++) gfx_px(x, GROUND_Y - k, ((x >> 1) & 1) ? GREEN : MED_GREEN);
    }
}

int render_grass_blade(int x) {
    unsigned h = hash((unsigned)(x / 2) + 77);
    return (h % 7 == 0) ? 4 : (int)(h % 3);
}

// ---------------------------------------------------------------------
// Slingshot frame (the bands and pouch move, so they are drawn each frame)
// ---------------------------------------------------------------------
#define WOOD_COLOR  DARK_ORANGE
#define WOOD_SHINE  ORANGE
#define OUTLINE     BLACK
#define TRUNK_R 7
#define ARM_R   5

static void arm(float side, float r, char c) {   // side = -1 left, +1 right
    const float ex = SLING_X + side * (SLING_ARM_DX - 3), ey = SLING_FORK_Y - 14;
    const float tx = SLING_X + side * SLING_ARM_DX,       ty = SLING_FORK_Y - SLING_ARM_DY;
    gfx_capsule(SLING_X, SLING_FORK_Y + 2, ex, ey, r, c);
    gfx_capsule(ex, ey, tx, ty, r, c);
}

static void knob(float side, float r, char c) {
    gfx_disc(SLING_X + side * SLING_ARM_DX, SLING_FORK_Y - SLING_ARM_DY - 1, r, c);
}

static void grass_tuft(float x, float h, float lean) {
    const float y = GROUND_Y + 4;
    gfx_tri(x - 3.0f, y, x + 3.0f, y, x + lean, y - h, OUTLINE);
    gfx_tri(x - 1.8f, y, x + 1.8f, y, x + lean, y - h + 2, GREEN);
}

static void slingshot_frame(void) {
    const float X = SLING_X, F = SLING_FORK_Y;
    const float base_y = GROUND_Y + 4;
    const float tip_y  = F - SLING_ARM_DY;
    const float lx = X - SLING_ARM_DX, rx = X + SLING_ARM_DX;

    // soft shadow on the grass
    for (int y = GROUND_Y + 2; y <= GROUND_Y + 9; y++) {
        float t = (y - (GROUND_Y + 5.5f)) / 4.0f;
        float half = 26.0f * sqrtf(1.0f - t * t);
        gfx_hspan_blend((int)(X + 6 - half), (int)(X + 6 + half), y, DARK_GREEN, 9);
    }

    // all outlines first, then all fills, so the joints are seamless
    gfx_capsule(X, base_y, X, F, TRUNK_R + 2, OUTLINE);
    arm(-1, ARM_R + 2, OUTLINE);
    arm(+1, ARM_R + 2, OUTLINE);
    knob(-1, ARM_R + 3, OUTLINE);
    knob(+1, ARM_R + 3, OUTLINE);

    gfx_capsule(X, base_y, X, F, TRUNK_R, WOOD_COLOR);
    arm(-1, ARM_R, WOOD_COLOR);
    arm(+1, ARM_R, WOOD_COLOR);
    knob(-1, ARM_R + 1, WOOD_COLOR);
    knob(+1, ARM_R + 1, WOOD_COLOR);

    // shaded right side (orange + black dither = brown), shiny left side
    for (int y = (int)F - 2; y <= (int)base_y - 2; y++) gfx_hspan_blend((int)X + 3, (int)X + 6, y, BLACK, 6);
    gfx_capsule(X - 3.5f, base_y - 6, X - 3.5f, F - 2, 1.5f, WOOD_SHINE);
    for (int s = -1; s <= 1; s += 2) {
        float ex = X + s * (SLING_ARM_DX - 3) - 2.5f, ey = F - 14;
        gfx_capsule(ex, ey, X + s * SLING_ARM_DX - 2.5f, tip_y, 1.2f, WOOD_SHINE);
        gfx_disc(X + s * SLING_ARM_DX - 2, tip_y - 3, 1.8f, WOOD_SHINE);
    }

    // wood knot
    gfx_ellipse(X + 2, base_y - 22, 2.5f, 3.5f, OUTLINE);
    gfx_ellipse(X + 2, base_y - 22, 1.2f, 2.0f, WOOD_SHINE);

    // rope wrapped around the fork
    for (int i = 0; i < 3; i++) {
        float y = F + 4 + i * 5;
        gfx_capsule(X - TRUNK_R - 1, y + 2, X + TRUNK_R + 1, y - 2, 2.6f, OUTLINE);
    }
    for (int i = 0; i < 3; i++) {
        float y = F + 4 + i * 5;
        gfx_capsule(X - TRUNK_R, y + 2, X + TRUNK_R, y - 2, 1.4f, YELLOW);
    }

    // band ties under each knob
    gfx_capsule(lx - ARM_R - 1, tip_y + 4, lx + ARM_R + 1, tip_y + 4, 1.6f, OUTLINE);
    gfx_capsule(rx - ARM_R - 1, tip_y + 4, rx + ARM_R + 1, tip_y + 4, 1.6f, OUTLINE);

    grass_tuft(X - 14, 9, -2);
    grass_tuft(X - 10, 13, -1);
    grass_tuft(X + 10, 13, 1);
    grass_tuft(X + 14, 9, 2);
}

// ---------------------------------------------------------------------
// Build + blit
// ---------------------------------------------------------------------
void render_background_build(void) {
    char *saved = gfx_get_target();
    gfx_target(bg_cache);
    gfx_set_shake(0);

    sky();
    sun(338, 62);
    cloud(120, 108, 1.15f);
    cloud(520, 120, 1.25f);
    cloud(255, 170, 0.70f);
    cloud(620, 62, 0.85f);
    cloud(30, 215, 0.60f);

    mountains();

    // back row of hills (darker), then the front row
    hill(330, 150, 62, DARK_GREEN, DARK_GREEN, MED_GREEN);
    hill(610, 170, 74, DARK_GREEN, DARK_GREEN, MED_GREEN);
    hill(40, 130, 50, DARK_GREEN, DARK_GREEN, MED_GREEN);
    hill(190, 175, 44, DARK_GREEN, MED_GREEN, GREEN);
    hill(480, 215, 52, DARK_GREEN, MED_GREEN, GREEN);
    hill(700, 130, 40, DARK_GREEN, MED_GREEN, GREEN);
    bush(262, GROUND_Y, 13);
    bush(22, GROUND_Y, 16);
    bush(632, GROUND_Y, 14);

    ground();
    slingshot_frame();
    render_hud_static();

    gfx_target(saved);
}

void render_background_blit(int shake_dy) {
    char *fb = gfx_get_target();
    if (shake_dy > 0) {
        memcpy(fb + shake_dy * GFX_ROW_BYTES, bg_cache, (size_t)(GFX_H_PX - shake_dy) * GFX_ROW_BYTES);
        memcpy(fb, bg_cache, (size_t)shake_dy * GFX_ROW_BYTES);                 // repeat top rows of sky
    } else if (shake_dy < 0) {
        int d = -shake_dy;
        memcpy(fb, bg_cache + d * GFX_ROW_BYTES, (size_t)(GFX_H_PX - d) * GFX_ROW_BYTES);
        memcpy(fb + (GFX_H_PX - d) * GFX_ROW_BYTES, bg_cache + (GFX_H_PX - d) * GFX_ROW_BYTES,
               (size_t)d * GFX_ROW_BYTES);                                      // repeat bottom rows of dirt
    } else {
        memcpy(fb, bg_cache, GFX_BUF_BYTES);
    }
}
