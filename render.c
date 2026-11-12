// render.c -- draws the moving parts of the game on top of the cached scenery:
// blocks, pigs, bird, slingshot bands, effects and the on-screen display.
//
// Style: flat colors, black outlines (a shape is drawn slightly larger in black,
// then again in color on top). Blocks and characters can be drawn at any angle.
#include "render.h"
#include "gfx.h"
#include "game.h"
#include "fx.h"

#include <math.h>
#include <stdio.h>

#define OUTLINE BLACK

// ---------------------------------------------------------------------
// Body-space helpers: (lx, ly) is a point relative to a body's centre, in the
// body's own rotated frame.
// ---------------------------------------------------------------------
typedef struct { float x, y, co, si; } xf_t;

static inline float wx(const xf_t *t, float lx, float ly) { return t->x + lx * t->co - ly * t->si; }
static inline float wy(const xf_t *t, float lx, float ly) { return t->y + lx * t->si + ly * t->co; }

static void lline(const xf_t *t, float lx0, float ly0, float lx1, float ly1, char c) {
    gfx_line(gfx_round(wx(t, lx0, ly0)), gfx_round(wy(t, lx0, ly0)),
             gfx_round(wx(t, lx1, ly1)), gfx_round(wy(t, lx1, ly1)), c);
}

static void ldisc(const xf_t *t, float lx, float ly, float r, char c) {
    gfx_disc(wx(t, lx, ly), wy(t, lx, ly), r, c);
}

static void ltri(const xf_t *t, float x0, float y0, float x1, float y1, float x2, float y2, char c) {
    gfx_tri(wx(t, x0, y0), wy(t, x0, y0), wx(t, x1, y1), wy(t, x1, y1), wx(t, x2, y2), wy(t, x2, y2), c);
}

// Corners of a centred box with half-size (hw, hh).
static void lbox(const xf_t *t, float hw, float hh, float q[4][2]) {
    q[0][0] = wx(t, -hw, -hh); q[0][1] = wy(t, -hw, -hh);
    q[1][0] = wx(t,  hw, -hh); q[1][1] = wy(t,  hw, -hh);
    q[2][0] = wx(t,  hw,  hh); q[2][1] = wy(t,  hw,  hh);
    q[3][0] = wx(t, -hw,  hh); q[3][1] = wy(t, -hw,  hh);
}

static unsigned hash(unsigned n) {
    n ^= n >> 15; n *= 0x2c1b3c6dU;
    n ^= n >> 12; n *= 0x297a2d39U;
    n ^= n >> 15;
    return n;
}

// ---------------------------------------------------------------------
// Blocks
// ---------------------------------------------------------------------

// A jagged crack from one long edge toward the other. `seed` picks where.
static void crack(const xf_t *t, float hw, float hh, unsigned seed, char c) {
    unsigned h = hash(seed);
    bool along_x = hw >= hh;                       // cracks run across the short side
    float span = along_x ? hw : hh, thick = along_x ? hh : hw;
    float p0 = ((float)(h % 200) / 100.0f - 1.0f) * span * 0.75f;
    float j1 = ((float)((h >> 8) % 100) / 50.0f - 1.0f) * 3.0f;
    float j2 = ((float)((h >> 16) % 100) / 50.0f - 1.0f) * 3.0f;
    float a = -thick, b = -thick * 0.25f, d = thick * 0.35f, e = thick;
    if (h & 0x1000000) { a = -a; b = -b; d = -d; e = -e; }   // start from either edge
    if (along_x) {
        lline(t, p0, a, p0 + j1, b, c);
        lline(t, p0 + j1, b, p0 + j2, d, c);
        if (h & 0x2000000) lline(t, p0 + j2, d, p0 + j1 - j2, e, c);
    } else {
        lline(t, a, p0, b, p0 + j1, c);
        lline(t, b, p0 + j1, d, p0 + j2, c);
        if (h & 0x2000000) lline(t, d, p0 + j2, e, p0 + j1 - j2, c);
    }
}

static void draw_block(const obj_t *o, int idx) {
    const xf_t t = { o->x, o->y, o->co, o->si };
    float q[4][2];

    lbox(&t, o->hw, o->hh, q);
    gfx_quad(q, OUTLINE);

    float hw = o->hw - 1.5f, hh = o->hh - 1.5f;       // inside the outline
    if (hw < 0.5f) hw = 0.5f;
    if (hh < 0.5f) hh = 0.5f;
    lbox(&t, hw, hh, q);
    bool wide = hw >= hh;
    char crack_color = OUTLINE;

    switch (o->mat) {
    case MAT_WOOD:
        gfx_quad(q, DARK_ORANGE);
        // lit top + left edges
        lline(&t, -hw + 1, -hh + 1, hw - 1, -hh + 1, ORANGE);
        lline(&t, -hw + 1, -hh + 1, -hw + 1, hh - 1, ORANGE);
        // wood grain: dashes along the length
        if (wide) {
            for (float u = -hw + 5; u < hw - 6; u += 9) {
                float v = ((int)(u + hw) / 9 & 1) ? hh * 0.3f : -hh * 0.1f;
                lline(&t, u, v, u + 4, v, ORANGE);
            }
        } else {
            for (float v = -hh + 5; v < hh - 6; v += 9) {
                float u = ((int)(v + hh) / 9 & 1) ? hw * 0.3f : -hw * 0.1f;
                lline(&t, u, v, u, v + 4, ORANGE);
            }
        }
        break;

    case MAT_STONE:
        gfx_quad_mix(q, WHITE, BLACK, 6);              // black/white dither reads as grey
        lline(&t, -hw + 1, -hh + 1, hw - 1, -hh + 1, WHITE);
        lline(&t, -hw + 1, -hh + 1, -hw + 1, hh - 1, WHITE);
        lline(&t, -hw + 1, hh - 1, hw - 1, hh - 1, OUTLINE);   // shaded bottom + right
        lline(&t, hw - 1, -hh + 1, hw - 1, hh - 1, OUTLINE);
        for (unsigned k = 0; k < 3; k++) {             // a few pits
            unsigned h = hash((unsigned)idx * 31 + k);
            float u = ((float)(h % 200) / 100.0f - 1.0f) * (hw - 3);
            float v = ((float)((h >> 9) % 200) / 100.0f - 1.0f) * (hh - 3);
            lline(&t, u, v, u + 1, v, OUTLINE);
        }
        break;

    case MAT_ICE:
        gfx_quad(q, CYAN);
        lline(&t, -hw + 1, hh - 1, hw - 1, hh - 1, LIGHT_BLUE);    // shaded bottom + right
        lline(&t, hw - 1, -hh + 1, hw - 1, hh - 1, LIGHT_BLUE);
        lline(&t, -hw + 1, -hh + 1, hw - 1, -hh + 1, WHITE);       // shiny top + left
        lline(&t, -hw + 1, -hh + 1, -hw + 1, hh - 1, WHITE);
        {   // diagonal glints near the top-left corner
            float g = (hw < hh ? hw : hh) * 1.3f;
            if (g > 5.0f) {
                lline(&t, -hw + 2, -hh + 2 + g, -hw + 2 + g, -hh + 2, WHITE);
                lline(&t, -hw + 2, -hh + 5 + g, -hw + 5 + g, -hh + 2, WHITE);
            }
        }
        crack_color = BLUE;
        break;

    default:
        break;
    }

    // cracks show up as it takes damage
    float hp = o->health / o->max_health;
    if (hp < 0.75f) crack(&t, hw, hh, (unsigned)idx * 7 + 1, crack_color);
    if (hp < 0.50f) crack(&t, hw, hh, (unsigned)idx * 7 + 2, crack_color);
    if (hp < 0.25f) crack(&t, hw, hh, (unsigned)idx * 7 + 3, crack_color);
}

// ---------------------------------------------------------------------
// Pigs
// ---------------------------------------------------------------------
typedef struct {
    float look_x, look_y;   // where to look (world coords)
    bool  has_target;       // look at (look_x, look_y) and act worried
    bool  blink;
} mood_t;

static void draw_pig(const obj_t *o, const mood_t *m) {
    const xf_t t = { o->x, o->y, o->co, o->si };
    const float r = o->r;
    const bool hurt = o->health < o->max_health * 0.6f;
    const bool boss = r >= 12.5f;

    // ears + head outline
    ldisc(&t, -0.62f * r, -0.80f * r, 0.30f * r + 1.5f, OUTLINE);
    ldisc(&t,  0.62f * r, -0.80f * r, 0.30f * r + 1.5f, OUTLINE);
    gfx_disc(o->x, o->y, r + 1.5f, OUTLINE);
    ldisc(&t, -0.62f * r, -0.80f * r, 0.30f * r, GREEN);
    ldisc(&t,  0.62f * r, -0.80f * r, 0.30f * r, GREEN);
    ldisc(&t, -0.62f * r, -0.80f * r, 0.13f * r, MED_GREEN);
    ldisc(&t,  0.62f * r, -0.80f * r, 0.13f * r, MED_GREEN);

    // head: darker rim on the lower right, lit from the upper left
    gfx_disc(o->x, o->y, r, MED_GREEN);
    gfx_disc(o->x - 0.09f * r, o->y - 0.11f * r, 0.90f * r, GREEN);

    // crown for the big one
    if (boss) {
        ltri(&t, -0.55f * r, -0.88f * r, 0.55f * r, -0.88f * r, 0.0f, -1.62f * r, OUTLINE);
        ltri(&t, -0.62f * r, -0.84f * r, -0.05f * r, -0.84f * r, -0.50f * r, -1.50f * r, OUTLINE);
        ltri(&t,  0.62f * r, -0.84f * r,  0.05f * r, -0.84f * r,  0.50f * r, -1.50f * r, OUTLINE);
        ltri(&t, -0.42f * r, -0.92f * r, 0.42f * r, -0.92f * r, 0.0f, -1.46f * r, YELLOW);
        ltri(&t, -0.52f * r, -0.90f * r, -0.12f * r, -0.90f * r, -0.44f * r, -1.36f * r, YELLOW);
        ltri(&t,  0.52f * r, -0.90f * r,  0.12f * r, -0.90f * r,  0.44f * r, -1.36f * r, YELLOW);
    }

    // eyes
    float ex = 0.40f * r, ey = -0.22f * r;
    float er = (m->has_target ? 0.31f : 0.27f) * r;        // wide-eyed when worried
    if (er < 2.2f) er = 2.2f;
    float lx = 0.0f, ly = 0.0f;                             // pupil offset (world)
    if (m->has_target) {
        float dx = m->look_x - o->x, dy = m->look_y - o->y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d > 1.0f) { lx = dx / d * er * 0.42f; ly = dy / d * er * 0.42f; }
    }
    if (hurt) gfx_disc_blend(wx(&t, ex, ey), wy(&t, ex, ey), er + 2.5f, MAGENTA, 8);   // black eye
    for (int side = -1; side <= 1; side += 2) {
        float cx = wx(&t, side * ex, ey), cy = wy(&t, side * ex, ey);
        bool closed = m->blink || (hurt && side > 0);
        if (closed) {
            lline(&t, side * ex - er, ey, side * ex + er, ey, OUTLINE);
            lline(&t, side * ex - er, ey + 1, side * ex + er, ey + 1, OUTLINE);
        } else {
            gfx_disc(cx, cy, er + 0.8f, OUTLINE);
            gfx_disc(cx, cy, er, WHITE);
            gfx_disc(cx + lx, cy + ly, er * 0.50f, OUTLINE);
        }
    }

    // snout
    float sx = wx(&t, 0.0f, 0.32f * r), sy = wy(&t, 0.0f, 0.32f * r);
    gfx_ellipse_rot(sx, sy, 0.46f * r + 1.2f, 0.31f * r + 1.2f, t.co, t.si, OUTLINE);
    gfx_ellipse_rot(sx, sy, 0.46f * r, 0.31f * r, t.co, t.si, GREEN);
    float nr = 0.09f * r < 1.0f ? 1.0f : 0.09f * r;
    ldisc(&t, -0.17f * r, 0.32f * r, nr, DARK_GREEN);
    ldisc(&t,  0.17f * r, 0.32f * r, nr, DARK_GREEN);

    // little "o" mouth when worried
    if (m->has_target) ldisc(&t, 0.0f, 0.76f * r, 0.13f * r < 1.2f ? 1.2f : 0.13f * r, OUTLINE);
}

// ---------------------------------------------------------------------
// Bird
// ---------------------------------------------------------------------
typedef enum { BIRD_IDLE, BIRD_BLINK, BIRD_FLYING, BIRD_OUCH } bird_face_t;

static void draw_bird(float x, float y, float rot, bird_face_t face) {
    const float R = BIRD_RADIUS;
    const xf_t t = { x, y, cosf(rot), sinf(rot) };

    // tail feathers (behind body)
    gfx_capsule(wx(&t, -R + 2, 1), wy(&t, -R + 2, 1), wx(&t, -R - 5, -3), wy(&t, -R - 5, -3), 1.5f, OUTLINE);
    gfx_capsule(wx(&t, -R + 2, 2), wy(&t, -R + 2, 2), wx(&t, -R - 6,  2), wy(&t, -R - 6,  2), 1.5f, OUTLINE);
    gfx_capsule(wx(&t, -R + 2, 3), wy(&t, -R + 2, 3), wx(&t, -R - 4,  7), wy(&t, -R - 4,  7), 1.5f, OUTLINE);

    // head tuft + body outline
    ldisc(&t, -2, -R - 1, 4.0f, OUTLINE);
    ldisc(&t,  3, -R,     3.5f, OUTLINE);
    gfx_disc(x, y, R + 2, OUTLINE);
    ldisc(&t, -2, -R - 1, 2.5f, RED);
    ldisc(&t,  3, -R,     2.0f, RED);

    // body: shaded lower right, lit upper left
    gfx_disc(x, y, R, RED);
    gfx_disc_blend(x, y, R, OUTLINE, 5);
    gfx_disc(x - 1.2f, y - 1.5f, R - 1.8f, RED);

    // belly
    gfx_ellipse_rot(wx(&t, 1, 6.5f), wy(&t, 1, 6.5f), 6.5f, 4.0f, t.co, t.si, LIGHT_PINK);

    // eyes
    ldisc(&t, 2, -3, 5.0f, OUTLINE);
    ldisc(&t, 8, -3, 4.5f, OUTLINE);
    if (face == BIRD_BLINK) {
        ldisc(&t, 2, -3, 4.0f, RED);
        ldisc(&t, 8, -3, 3.5f, RED);
        lline(&t, -1, -3, 5, -3, OUTLINE);
        lline(&t, 6, -3, 11, -3, OUTLINE);
    } else if (face == BIRD_OUCH) {                    // squeezed shut:  > <
        ldisc(&t, 2, -3, 4.0f, WHITE);
        ldisc(&t, 8, -3, 3.5f, WHITE);
        lline(&t, 0, -5, 4, -3, OUTLINE);  lline(&t, 4, -3, 0, -1, OUTLINE);
        lline(&t, 10, -5, 7, -3, OUTLINE); lline(&t, 7, -3, 10, -1, OUTLINE);
    } else {
        float look = face == BIRD_FLYING ? 1.0f : 0.0f;     // pupils forward in flight
        ldisc(&t, 2, -3, 4.0f, WHITE);
        ldisc(&t, 8, -3, 3.5f, WHITE);
        ldisc(&t, 3.5f + look, -2.5f, 2.0f, OUTLINE);
        ldisc(&t, 9.0f + look * 0.6f, -2.5f, 1.8f, OUTLINE);
        gfx_px(gfx_round(wx(&t, 3 + look, -4)), gfx_round(wy(&t, 3 + look, -4)), WHITE);
        gfx_px(gfx_round(wx(&t, 8 + look, -4)), gfx_round(wy(&t, 8 + look, -4)), WHITE);
    }

    // beak
    ltri(&t, 7, -0.5f, 7, 7.5f, 18, 3.5f, OUTLINE);
    ltri(&t, 8,  1.0f, 8, 6.0f, 16, 3.5f, YELLOW);
    if (face == BIRD_FLYING) lline(&t, 9, 3.5f, 15, 3.5f, ORANGE);   // beak open a crack
}

// ---------------------------------------------------------------------
// Slingshot bands + pouch (the wooden frame is part of the background)
// ---------------------------------------------------------------------
#define BAND_COLOR BLACK
#define ARM_R 5

// Returns the pouch-side position of the bird through (*bx, *by).
static void draw_bands(float angle_deg, float power, float kick) {
    const float tip_y = SLING_FORK_Y - SLING_ARM_DY;
    const float lx = SLING_X - SLING_ARM_DX, rx = SLING_X + SLING_ARM_DX;
    float bx, by;
    physics_pouch_position(angle_deg, power, &bx, &by);
    float a = angle_deg * 0.017453293f;
    float ax = cosf(a), ay = -sinf(a);                   // aim direction (screen coords)
    float px = bx - ax * (BIRD_RADIUS - 2) + ax * kick;  // pouch centre
    float py = by - ay * (BIRD_RADIUS - 2) + ay * kick;

    // stretched bands get thinner
    float thick = 2.0f - power * 0.7f;
    gfx_capsule(lx, tip_y + 4, px, py, thick, BAND_COLOR);
    gfx_capsule(rx, tip_y + 4, px, py, thick, BAND_COLOR);
    gfx_capsule(px + ay * 9, py - ax * 9, px - ay * 9, py + ax * 9, 3.0f, BAND_COLOR);
    gfx_capsule(px + ay * 7, py - ax * 7, px - ay * 7, py + ax * 7, 1.2f, DARK_ORANGE);   // leather
}

// ---------------------------------------------------------------------
// Aiming arc + trails
// ---------------------------------------------------------------------
// Would a bird centred at (x, y) be touching this body?
static bool bird_would_hit(const obj_t *o, float x, float y) {
    float dx = x - o->x, dy = y - o->y, reach = o->bound + BIRD_RADIUS;
    if (dx * dx + dy * dy >= reach * reach) return false;
    if (o->shape == SHAPE_CIRCLE) return true;
    // closest point on the (rotated) box
    float lx = dx * o->co + dy * o->si, ly = -dx * o->si + dy * o->co;
    float ex = fabsf(lx) - o->hw, ey = fabsf(ly) - o->hh;
    if (ex < 0.0f) ex = 0.0f;
    if (ey < 0.0f) ey = 0.0f;
    return ex * ex + ey * ey < (float)(BIRD_RADIUS * BIRD_RADIUS);
}

// Dotted preview of the flight, using the same physics as the real flight.
// It stops at the first thing the bird would hit.
static void draw_aim_arc(const world_t *w, float angle_deg, float power) {
    if (power <= 0.0f) return;
    body_t b;
    physics_launch(&b, angle_deg, power);
    int dot = 0;
    for (int step = 0; step < 600; step++) {             // up to 10 s of flight
        physics_step(&b, PHYS_DT);
        if (b.y + BIRD_RADIUS > GROUND_Y || b.x > SCREEN_W || b.x < 0) break;
        if (b.x > SLING_X + 60) {                        // nothing to hit near the slingshot
            bool hit = false;
            for (int i = 0; i < w->count && !hit; i++) {
                if (w->objs[i].alive && w->objs[i].mat != MAT_BIRD) hit = bird_would_hit(&w->objs[i], b.x, b.y);
            }
            if (hit) break;
        }
        if (step % 5 == 4) {
            float r = 3.2f - dot * 0.07f;
            if (r < 1.6f) r = 1.6f;
            gfx_disc(b.x, b.y, r + 1.0f, BLUE);
            gfx_disc(b.x, b.y, r, WHITE);
            dot++;
        }
    }
}

static void draw_trail(const short (*pts)[2], int n, bool ghost) {
    for (int i = 0; i < n; i++) {
        if (ghost) gfx_disc_blend(pts[i][0], pts[i][1], 2.0f, WHITE, 9);
        else       gfx_disc(pts[i][0], pts[i][1], (i & 1) ? 1.6f : 2.6f, WHITE);
    }
}

// Blades of grass in front of everything standing on the ground.
static void draw_front_grass(void) {
    for (int x = 0; x < SCREEN_W; x++) {
        int h = render_grass_blade(x);
        char c = ((x >> 1) & 1) ? GREEN : MED_GREEN;
        for (int k = 0; k < h; k++) gfx_px(x, GROUND_Y + 1 - k, c);
    }
}

// ---------------------------------------------------------------------
// On-screen display
// ---------------------------------------------------------------------
static void round_rect(int x0, int y0, int x1, int y1, int r, char c) {
    gfx_rect(x0 + r, y0, x1 - r, y1, c);
    gfx_rect(x0, y0 + r, x1, y1 - r, c);
    gfx_disc(x0 + r, y0 + r, r, c); gfx_disc(x1 - r, y0 + r, r, c);
    gfx_disc(x0 + r, y1 - r, r, c); gfx_disc(x1 - r, y1 - r, r, c);
}

static void star_shape(float cx, float cy, float R, char c) {
    float px[10], py[10];
    for (int i = 0; i < 10; i++) {
        float a = -1.5708f + i * 0.62832f, rad = (i & 1) ? R * 0.42f : R;
        px[i] = cx + cosf(a) * rad; py[i] = cy + sinf(a) * rad;
    }
    for (int i = 0; i < 10; i++) {
        int j = (i + 1) % 10;
        gfx_tri(cx, cy, px[i], py[i], px[j], py[j], c);
    }
}

static void star(float cx, float cy, float R, bool earned) {
    star_shape(cx, cy, R + 3.0f, OUTLINE);
    star_shape(cx, cy, R, earned ? YELLOW : BLUE);
    if (earned) star_shape(cx - R * 0.08f, cy - R * 0.1f, R * 0.45f, WHITE);
}

// Where things sit on screen
#define HUD_BAR_X  200
#define HUD_BAR_Y  451
#define HUD_BAR_W  110
#define HUD_BAR_H  15

// The parts of the display that never change. These are painted into the
// scenery cache once (see background.c) instead of being redrawn every frame.
void render_hud_static(void) {
    gfx_text_outlined(14, 10, "SCORE", 1, YELLOW, OUTLINE);
    gfx_text_outlined(14, 50, "BIRDS USED", 1, WHITE, OUTLINE);
    gfx_text_outlined(14, 452, "ANGLE", 2, WHITE, OUTLINE);
    gfx_text_outlined(132, 452, "POWER", 2, WHITE, OUTLINE);
    gfx_rect(HUD_BAR_X - 2, HUD_BAR_Y - 2, HUD_BAR_X + HUD_BAR_W + 1, HUD_BAR_Y + HUD_BAR_H + 1, OUTLINE);
    gfx_rect(HUD_BAR_X, HUD_BAR_Y, HUD_BAR_X + HUD_BAR_W - 1, HUD_BAR_Y + HUD_BAR_H - 1, DARK_BLUE);
    gfx_text_outlined(424, 462, "serial:  angle  power  fire  reset", 1, YELLOW, OUTLINE);
}

// Small pig face for the "pigs left" counter.
static void pig_icon(float x, float y) {
    gfx_disc(x - 4.5f, y - 5.5f, 3.2f, OUTLINE);
    gfx_disc(x + 4.5f, y - 5.5f, 3.2f, OUTLINE);
    gfx_disc(x, y, 8.5f, OUTLINE);
    gfx_disc(x - 4.5f, y - 5.5f, 1.8f, GREEN);
    gfx_disc(x + 4.5f, y - 5.5f, 1.8f, GREEN);
    gfx_disc(x, y, 7.0f, GREEN);
    gfx_rect((int)x - 4, (int)y - 3, (int)x - 2, (int)y - 1, WHITE);
    gfx_rect((int)x + 2, (int)y - 3, (int)x + 4, (int)y - 1, WHITE);
    gfx_px((int)x - 3, (int)y - 2, OUTLINE);
    gfx_px((int)x + 3, (int)y - 2, OUTLINE);
    gfx_rect((int)x - 3, (int)y + 1, (int)x + 3, (int)y + 4, OUTLINE);
    gfx_rect((int)x - 2, (int)y + 2, (int)x + 2, (int)y + 3, MED_GREEN);
}

// The numbers that change.
static void draw_hud(const scene_t *s) {
    char buf[24];

    snprintf(buf, sizeof buf, "%d", s->score);
    gfx_text_outlined(14, 22, buf, 3, WHITE, OUTLINE);
    snprintf(buf, sizeof buf, "%d", s->shots);
    gfx_text_outlined(80, 50, buf, 1, WHITE, OUTLINE);

    for (int i = 0; i < s->pigs_left && i < 10; i++) pig_icon((float)(SCREEN_W - 20 - i * 22), 22.0f);

    snprintf(buf, sizeof buf, "%d", (int)(s->aim_angle + 0.5f));
    gfx_text_outlined(86, 452, buf, 2, WHITE, OUTLINE);

    // power bar: green -> yellow -> red
    int fill = (int)(s->aim_power * HUD_BAR_W);
    int g = HUD_BAR_W * 5 / 10, y2 = HUD_BAR_W * 8 / 10;
    int y0 = HUD_BAR_Y, y1 = HUD_BAR_Y + HUD_BAR_H - 1;
    if (fill > 0)  gfx_rect(HUD_BAR_X, y0, HUD_BAR_X + (fill < g ? fill : g) - 1, y1, GREEN);
    if (fill > g)  gfx_rect(HUD_BAR_X + g, y0, HUD_BAR_X + (fill < y2 ? fill : y2) - 1, y1, YELLOW);
    if (fill > y2) gfx_rect(HUD_BAR_X + y2, y0, HUD_BAR_X + fill - 1, y1, RED);
    if (fill > 0)  gfx_hspan(HUD_BAR_X, HUD_BAR_X + fill - 1, y0 + 2, WHITE);          // glossy stripe
    for (int k = 1; k < 4; k++) gfx_rect(HUD_BAR_X + HUD_BAR_W * k / 4, y0, HUD_BAR_X + HUD_BAR_W * k / 4, y1, OUTLINE);
    snprintf(buf, sizeof buf, "%d%%", (int)(s->aim_power * 100.0f + 0.5f));
    gfx_text_outlined(HUD_BAR_X + HUD_BAR_W + 8, 452, buf, 2, WHITE, OUTLINE);
}

static void draw_level_clear(const scene_t *s) {
    const int delay = 50;                       // let the last pop play out first
    int f = s->clear_frames - delay;
    if (f < 0) return;

    // panel drops in from the top and settles
    const int w = 340, h = 190, x0 = (SCREEN_W - w) / 2;
    float tt = f < 18 ? (float)f / 18.0f : 1.0f;
    int y0 = 110 - (int)((1.0f - tt) * (1.0f - tt) * 320.0f);
    int x1 = x0 + w - 1, y1 = y0 + h - 1;

    for (int y = y0 + 8; y <= y1 + 8; y++) gfx_hspan_blend(x0 + 8, x1 + 8, y, OUTLINE, 8);   // shadow
    round_rect(x0 - 3, y0 - 3, x1 + 3, y1 + 3, 14, OUTLINE);
    round_rect(x0, y0, x1, y1, 12, WHITE);
    round_rect(x0 + 4, y0 + 4, x1 - 4, y1 - 4, 9, DARK_BLUE);
    for (int y = y0 + 4; y < y0 + 60; y++) gfx_hspan_blend(x0 + 8, x1 - 8, y, BLUE, 16 - (y - y0 - 4) * 16 / 56);

    const char *title = "LEVEL CLEAR!";
    gfx_text_outlined(SCREEN_W / 2 - gfx_text_width(title, 4) / 2, y0 + 16, title, 4, YELLOW, OUTLINE);

    // stars pop in one at a time
    for (int i = 0; i < 3; i++) {
        int sf = f - 24 - i * 16;
        float R = 20.0f;
        bool earned = i < s->stars;
        if (earned) {
            if (sf < 0) { earned = false; }
            else if (sf < 8) R = 20.0f * (0.4f + 1.0f * (float)sf / 8.0f);     // overshoot...
            else if (sf < 14) R = 20.0f * (1.4f - 0.4f * (float)(sf - 8) / 6.0f);   // ...and settle
        }
        star((float)(SCREEN_W / 2 + (i - 1) * 62), (float)(y0 + 86 - (i == 1 ? 6 : 0)), R, earned);
    }

    char buf[48];
    snprintf(buf, sizeof buf, "SCORE %d", s->score);
    gfx_text_outlined(SCREEN_W / 2 - gfx_text_width(buf, 2) / 2, y0 + 122, buf, 2, WHITE, OUTLINE);
    snprintf(buf, sizeof buf, "BIRDS USED %d", s->shots);
    gfx_text_outlined(SCREEN_W / 2 - gfx_text_width(buf, 1) / 2, y0 + 146, buf, 1, CYAN, OUTLINE);
    const char *hint = "type  reset  to play again";
    if ((s->frame / 30) & 1) gfx_text(SCREEN_W / 2 - gfx_text_width(hint, 1) / 2, y0 + 166, hint, 1, WHITE);
}

// ---------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------
void render_init(void) {
    render_background_build();
}

void render_scene(const scene_t *s, char *framebuffer) {
    const world_t *w = s->world;
    gfx_target(framebuffer);

    // scenery (shifted a few rows when the screen shakes)
    gfx_set_shake(0);
    render_background_blit(s->shake);
    gfx_set_shake(s->shake);

    const obj_t *bird = (s->bird_idx >= 0 && w->objs[s->bird_idx].alive) ? &w->objs[s->bird_idx] : 0;

    draw_trail(s->ghost, s->ghost_count, true);

    // blocks first, then pigs on top
    for (int i = 0; i < w->count; i++) {
        const obj_t *o = &w->objs[i];
        if (o->alive && o->shape == SHAPE_BOX) draw_block(o, i);
    }
    for (int i = 0; i < w->count; i++) {
        const obj_t *o = &w->objs[i];
        if (!o->alive || o->mat != MAT_PIG) continue;
        mood_t m = {0};
        if (bird) { m.has_target = true; m.look_x = bird->x; m.look_y = bird->y; }
        m.blink = ((s->frame + (unsigned)i * 53u) % 190u) < 7u;
        draw_pig(o, &m);
    }

    // slingshot + bird
    if (s->aiming) {
        float bx, by;
        physics_pouch_position(s->aim_angle, s->aim_power, &bx, &by);
        draw_aim_arc(w, s->aim_angle, s->aim_power);
        draw_bands(s->aim_angle, s->aim_power, 0.0f);
        draw_bird(bx, by, 0.0f, (s->frame % 210u) < 8u ? BIRD_BLINK : BIRD_IDLE);
    } else {
        draw_trail(s->trail, s->trail_count, false);
        draw_bands(s->aim_angle, 0.0f, s->band_kick);
        if (bird) draw_bird(bird->x, bird->y, bird->angle, bird->touched ? BIRD_OUCH : BIRD_FLYING);
    }

    draw_front_grass();
    fx_draw();

    draw_hud(s);
    gfx_set_shake(0);          // the level-clear banner stays put
    if (s->clear_frames >= 0) draw_level_clear(s);
}
