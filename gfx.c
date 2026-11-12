// gfx.c -- clipped drawing primitives (see gfx.h)
#include "gfx.h"

#include <math.h>
#include <string.h>

#include "VGA/font_glcd.c"   // `font`: 5x7 glyphs, 5 column bytes per character

static char *fb;        // current target buffer
static int shake_dy;    // added to every y

void gfx_target(char *buffer) { fb = buffer; }
char *gfx_get_target(void) { return fb; }
void gfx_set_shake(int dy) { shake_dy = dy; }

// 4x4 ordered-dither thresholds (0..15)
static const unsigned char BAYER[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5},
};

// Unclipped pixel write. Even x lives in the low nibble, odd x in the high nibble.
static inline void put(int x, int y, char c) {
    char *p = fb + GFX_ROW_BYTES * y + (x >> 1);
    if (x & 1) *p = (char)((*p & 0x0F) | (c << 4));
    else       *p = (char)((*p & 0xF0) | c);
}

void gfx_px(int x, int y, char c) {
    y += shake_dy;
    if ((unsigned)x >= GFX_W || (unsigned)y >= GFX_H_PX) return;
    put(x, y, c);
}

void gfx_hspan(int x0, int x1, int y, char c) {
    y += shake_dy;
    if ((unsigned)y >= GFX_H_PX) return;
    if (x0 < 0) x0 = 0;
    if (x1 > GFX_W - 1) x1 = GFX_W - 1;
    if (x1 < x0) return;
    if (x0 & 1) { put(x0, y, c); x0++; }
    if (x1 >= x0 && !(x1 & 1)) { put(x1, y, c); x1--; }
    if (x1 > x0) memset(fb + GFX_ROW_BYTES * y + (x0 >> 1), c | (c << 4), (size_t)((x1 - x0 + 1) >> 1));
}

void gfx_px_mix(int x, int y, char a, char b, int level) {
    gfx_px(x, y, BAYER[y & 3][x & 3] < level ? b : a);
}

void gfx_hspan_mix(int x0, int x1, int y, char a, char b, int level) {
    if (level <= 0)  { gfx_hspan(x0, x1, y, a); return; }
    if (level >= 16) { gfx_hspan(x0, x1, y, b); return; }
    int sy = y + shake_dy;
    if ((unsigned)sy >= GFX_H_PX) return;
    if (x0 < 0) x0 = 0;
    if (x1 > GFX_W - 1) x1 = GFX_W - 1;
    const unsigned char *row = BAYER[y & 3];   // pattern follows the object, not the shake
    for (int x = x0; x <= x1; x++) put(x, sy, row[x & 3] < level ? b : a);
}

void gfx_px_blend(int x, int y, char c, int level) {
    if (BAYER[y & 3][x & 3] < level) gfx_px(x, y, c);
}

void gfx_hspan_blend(int x0, int x1, int y, char c, int level) {
    if (level <= 0) return;
    if (level >= 16) { gfx_hspan(x0, x1, y, c); return; }
    int sy = y + shake_dy;
    if ((unsigned)sy >= GFX_H_PX) return;
    if (x0 < 0) x0 = 0;
    if (x1 > GFX_W - 1) x1 = GFX_W - 1;
    const unsigned char *row = BAYER[y & 3];
    for (int x = x0; x <= x1; x++) if (row[x & 3] < level) put(x, sy, c);
}

char gfx_read(int x, int y) {
    if ((unsigned)x >= GFX_W || (unsigned)y >= GFX_H_PX) return 0;
    char b = fb[GFX_ROW_BYTES * y + (x >> 1)];
    return (char)((x & 1) ? (b >> 4) & 0x0F : b & 0x0F);
}

void gfx_rect(int x0, int y0, int x1, int y1, char c) {
    for (int y = y0; y <= y1; y++) gfx_hspan(x0, x1, y, c);
}

void gfx_ellipse(float cx, float cy, float rx, float ry, char c) {
    if (rx <= 0.0f || ry <= 0.0f) return;
    int y0 = (int)ceilf(cy - ry), y1 = (int)floorf(cy + ry);
    float inv = 1.0f / ry;
    for (int y = y0; y <= y1; y++) {
        float t = (y - cy) * inv;
        float half = rx * sqrtf(1.0f - t * t);
        gfx_hspan(gfx_round(cx - half), gfx_round(cx + half), y, c);
    }
}

void gfx_disc(float cx, float cy, float r, char c) { gfx_ellipse(cx, cy, r, r, c); }

void gfx_disc_blend(float cx, float cy, float r, char c, int level) {
    if (r <= 0.0f || level <= 0) return;
    for (int y = (int)ceilf(cy - r); y <= (int)floorf(cy + r); y++) {
        float half = sqrtf(r * r - (y - cy) * (y - cy));
        gfx_hspan_blend(gfx_round(cx - half), gfx_round(cx + half), y, c, level);
    }
}

void gfx_ellipse_rot(float cx, float cy, float rx, float ry, float co, float si, char c) {
    if (rx <= 0.0f || ry <= 0.0f) return;
    // Scanline form: for each row solve the ellipse's quadratic in x.
    //   u =  dx*co + dy*si,  v = -dx*si + dy*co,  u^2/rx^2 + v^2/ry^2 <= 1
    float ia = 1.0f / (rx * rx), ib = 1.0f / (ry * ry);
    float A = co * co * ia + si * si * ib;
    float Bh = co * si * (ia - ib);          // half of the dx*dy coefficient
    float C = si * si * ia + co * co * ib;
    float R = rx > ry ? rx : ry;
    for (int y = (int)floorf(cy - R); y <= (int)ceilf(cy + R); y++) {
        float dy = y - cy;
        float b = Bh * dy, c0 = C * dy * dy - 1.0f;
        float disc = b * b - A * c0;
        if (disc < 0.0f) continue;
        float s = sqrtf(disc);
        gfx_hspan(gfx_round(cx + (-b - s) / A), gfx_round(cx + (-b + s) / A), y, c);
    }
}

// Scanline fill of a convex polygon with n vertices.
static void convex_fill(const float (*p)[2], int n, char a, char b, int level) {
    float ymin = p[0][1], ymax = p[0][1];
    for (int i = 1; i < n; i++) {
        if (p[i][1] < ymin) ymin = p[i][1];
        if (p[i][1] > ymax) ymax = p[i][1];
    }
    for (int y = (int)ceilf(ymin); y <= (int)floorf(ymax); y++) {
        float xl = 1e9f, xr = -1e9f;
        for (int i = 0; i < n; i++) {
            int j = (i + 1 == n) ? 0 : i + 1;
            float ay = p[i][1], by = p[j][1];
            if ((y < ay && y < by) || (y > ay && y > by)) continue;
            if (ay == by) {
                xl = fminf(xl, fminf(p[i][0], p[j][0]));
                xr = fmaxf(xr, fmaxf(p[i][0], p[j][0]));
            } else {
                float x = p[i][0] + (y - ay) * (p[j][0] - p[i][0]) / (by - ay);
                xl = fminf(xl, x);
                xr = fmaxf(xr, x);
            }
        }
        if (xl <= xr) gfx_hspan_mix(gfx_round(xl), gfx_round(xr), y, a, b, level);
    }
}

void gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, char c) {
    const float p[3][2] = {{x0, y0}, {x1, y1}, {x2, y2}};
    convex_fill(p, 3, c, c, 0);
}

void gfx_quad(const float p[4][2], char c) { convex_fill(p, 4, c, c, 0); }
void gfx_quad_mix(const float p[4][2], char a, char b, int level) { convex_fill(p, 4, a, b, level); }

void gfx_line(int x0, int y0, int x1, int y1, char c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    if (dx > 4000 || dy < -4000) return;   // wildly off-screen; don't spin
    for (;;) {
        gfx_px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_capsule(float x0, float y0, float x1, float y1, float r, char c) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len > 0.5f) {
        float nx = -dy / len * r, ny = dx / len * r;
        const float q[4][2] = {
            {x0 + nx, y0 + ny}, {x1 + nx, y1 + ny}, {x1 - nx, y1 - ny}, {x0 - nx, y0 - ny},
        };
        gfx_quad(q, c);
    }
    gfx_disc(x0, y0, r, c);
    gfx_disc(x1, y1, r, c);
}

// ---------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------
#define GLYPH_W 5
#define GLYPH_H 7
#define GLYPH_ADVANCE 6

int gfx_text_width(const char *s, int scale) {
    return (int)strlen(s) * GLYPH_ADVANCE * scale - scale;
}

void gfx_text(int x, int y, const char *s, int scale, char c) {
    for (; *s; s++, x += GLYPH_ADVANCE * scale) {
        const unsigned char *g = &font[(unsigned char)*s * GLYPH_W];
        for (int col = 0; col < GLYPH_W; col++) {
            unsigned char bits = g[col];
            for (int row = 0; row < GLYPH_H; row++) {
                if (!(bits & (1 << row))) continue;
                if (scale == 1) gfx_px(x + col, y + row, c);
                else gfx_rect(x + col * scale, y + row * scale,
                              x + col * scale + scale - 1, y + row * scale + scale - 1, c);
            }
        }
    }
}

void gfx_text_outlined(int x, int y, const char *s, int scale, char c, char outline) {
    if (scale == 1) {
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if (dx || dy) gfx_text(x + dx, y + dy, s, 1, outline);
    } else {
        // big pixels: the four diagonal copies already cover the whole outline
        gfx_text(x - 1, y - 1, s, scale, outline);
        gfx_text(x + 1, y - 1, s, scale, outline);
        gfx_text(x - 1, y + 1, s, scale, outline);
        gfx_text(x + 1, y + 1, s, scale, outline);
    }
    gfx_text(x, y, s, scale, c);
}
