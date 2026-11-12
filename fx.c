// fx.c -- particles, score popups and screen shake (see fx.h)
#include "fx.h"
#include "gfx.h"
#include "game.h"

#include <math.h>
#include <stdio.h>

typedef enum {
    P_SPLINTER,   // wood: spinning sliver
    P_SHARD,      // ice: spinning triangle
    P_CHUNK,      // stone: spinning little block
    P_PUFF,       // smoke / dust: growing, fading cloud
    P_FEATHER,    // bird feather: flutters down
    P_SPARK,      // impact star: quick flash
    P_BIT,        // round blob (pig bits)
    P_CONFETTI,   // level-clear confetti
} ptype_t;

typedef struct {
    unsigned char alive, type, color;
    short life, max_life;        // frames left / total
    float x, y, vx, vy;          // px, px/frame
    float angle, spin;           // rad, rad/frame
    float size;
} particle_t;

typedef struct {
    short life;
    short points, scale;
    float x, y;
} popup_t;

#define MAX_PARTICLES 128
#define MAX_POPUPS    8
#define POPUP_FRAMES  55

static particle_t parts[MAX_PARTICLES];
static popup_t popups[MAX_POPUPS];
static float shake_amp;
static unsigned frame_no;

// ---------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------
static unsigned rng_state = 0x1234abcdU;
static unsigned rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
static float frand(float lo, float hi) {            // uniform in [lo, hi)
    return lo + (hi - lo) * (float)(rnd() & 0xFFFF) * (1.0f / 65536.0f);
}

static particle_t *spawn(ptype_t type, float x, float y, float vx, float vy, int life, float size, char color) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particle_t *p = &parts[i];
        if (p->alive) continue;
        p->alive = 1; p->type = (unsigned char)type; p->color = (unsigned char)color;
        p->life = p->max_life = (short)life;
        p->x = x; p->y = y; p->vx = vx; p->vy = vy;
        p->angle = frand(0.0f, 6.28f);
        p->spin = frand(-0.35f, 0.35f);
        p->size = size;
        return p;
    }
    return 0;
}

static void add_shake(float amount) {
    shake_amp += amount;
    if (shake_amp > 7.0f) shake_amp = 7.0f;
}

// One piece of debris in the style of a material.
static void debris(material_t mat, float x, float y, float vx, float vy) {
    switch (mat) {
    case MAT_WOOD:
        spawn(P_SPLINTER, x, y, vx, vy, (int)frand(28, 50), frand(3.0f, 6.5f), (rnd() & 1) ? DARK_ORANGE : ORANGE);
        break;
    case MAT_ICE:
        spawn(P_SHARD, x, y, vx, vy, (int)frand(24, 44), frand(2.5f, 5.0f), (rnd() & 1) ? CYAN : WHITE);
        break;
    case MAT_STONE:
        spawn(P_CHUNK, x, y, vx, vy, (int)frand(30, 52), frand(1.8f, 3.6f), WHITE);
        break;
    case MAT_PIG:
        spawn(P_BIT, x, y, vx, vy, (int)frand(22, 40), frand(1.5f, 3.0f), GREEN);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------
// Spawning
// ---------------------------------------------------------------------
void fx_reset(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) parts[i].alive = 0;
    for (int i = 0; i < MAX_POPUPS; i++) popups[i].life = 0;
    shake_amp = 0.0f;
}

void fx_popup(float x, float y, int points, int scale) {
    for (int i = 0; i < MAX_POPUPS; i++) {
        if (popups[i].life > 0) continue;
        popups[i].life = POPUP_FRAMES;
        popups[i].points = (short)points;
        popups[i].scale = (short)scale;
        popups[i].x = x; popups[i].y = y;
        return;
    }
}

void fx_on_event(const event_t *e) {
    switch (e->kind) {
    case EV_BLOCK_BREAK: {
        // scatter pieces from all over the block, carrying some of its motion
        float co = cosf(e->angle), si = sinf(e->angle);
        int n = 6 + (int)(e->hw * e->hh / 40.0f);
        if (n > 14) n = 14;
        for (int i = 0; i < n; i++) {
            float lx = frand(-e->hw, e->hw), ly = frand(-e->hh, e->hh);
            float x = e->x + lx * co - ly * si, y = e->y + lx * si + ly * co;
            debris(e->mat, x, y,
                   e->vx / 120.0f + frand(-2.2f, 2.2f), e->vy / 120.0f + frand(-3.2f, 0.8f));
        }
        spawn(P_PUFF, e->x, e->y, 0, -0.2f, 16, 5.0f, WHITE);
        add_shake(2.0f);
        break;
    }
    case EV_PIG_POP: {
        for (int i = 0; i < 8; i++) {                 // ring of smoke
            float a = i * 0.785f + frand(-0.2f, 0.2f);
            spawn(P_PUFF, e->x + cosf(a) * e->hw * 0.4f, e->y + sinf(a) * e->hw * 0.4f,
                  cosf(a) * 1.3f, sinf(a) * 1.3f - 0.2f, (int)frand(18, 26), 4.0f, WHITE);
        }
        for (int i = 0; i < 7; i++) debris(MAT_PIG, e->x, e->y, frand(-2.5f, 2.5f), frand(-3.5f, 0.5f));
        for (int i = 0; i < 4; i++) spawn(P_SPARK, e->x + frand(-8, 8), e->y + frand(-8, 8), 0, 0, 10, 5.0f, YELLOW);
        add_shake(3.5f);
        break;
    }
    case EV_HIT: {
        int n = (int)(e->strength / 7.0f);
        if (n > 4) n = 4;
        material_t chip = e->mat;
        if (chip == MAT_BIRD || chip == MAT_GROUND || chip == MAT_PIG) chip = e->other;
        for (int i = 0; i < n; i++) {
            debris(chip, e->x, e->y, e->vx / 160.0f + frand(-1.6f, 1.6f), e->vy / 160.0f + frand(-2.4f, 0.4f));
        }
        if (e->mat == MAT_BIRD || e->other == MAT_BIRD) {
            spawn(P_SPARK, e->x, e->y, 0, 0, 8, 6.0f, WHITE);
            int feathers = e->strength > 12.0f ? 3 : 1;
            for (int i = 0; i < feathers; i++) {
                spawn(P_FEATHER, e->x, e->y, frand(-1.5f, 1.5f), frand(-2.0f, -0.4f), (int)frand(45, 70), 3.5f, RED);
            }
        }
        if (e->mat == MAT_GROUND || e->other == MAT_GROUND) {
            for (int i = 0; i < 2; i++) {
                spawn(P_PUFF, e->x + frand(-6, 6), GROUND_Y - 2, frand(-0.8f, 0.8f), frand(-0.6f, -0.1f), 18, 3.0f, WHITE);
            }
        }
        add_shake(e->strength * 0.09f);
        break;
    }
    }
}

void fx_bird_poof(float x, float y) {
    for (int i = 0; i < 6; i++) {
        float a = i * 1.047f;
        spawn(P_PUFF, x, y, cosf(a) * 1.0f, sinf(a) * 1.0f - 0.2f, 18, 4.0f, WHITE);
    }
    for (int i = 0; i < 6; i++) {
        spawn(P_FEATHER, x, y, frand(-2.0f, 2.0f), frand(-2.6f, -0.6f), (int)frand(50, 80), 3.5f, RED);
    }
}

void fx_confetti(void) {
    static const char colors[] = { RED, YELLOW, GREEN, CYAN, MAGENTA, ORANGE, WHITE, PINK };
    for (int i = 0; i < 70; i++) {
        spawn(P_CONFETTI, frand(40, GFX_W - 40), frand(-140, -5), frand(-0.6f, 0.6f), frand(1.0f, 2.2f),
              (int)frand(150, 230), frand(2.0f, 3.5f), colors[rnd() % sizeof colors]);
    }
}

// ---------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------
void fx_update(void) {
    frame_no++;
    shake_amp *= 0.82f;
    if (shake_amp < 0.5f) shake_amp = 0.0f;

    for (int i = 0; i < MAX_PARTICLES; i++) {
        particle_t *p = &parts[i];
        if (!p->alive) continue;
        if (--p->life <= 0) { p->alive = 0; continue; }

        switch (p->type) {
        case P_PUFF:
            p->vx *= 0.92f; p->vy *= 0.92f;
            p->size += 0.28f;
            break;
        case P_SPARK:
            break;
        case P_FEATHER:
            p->vy += 0.035f;                                  // very light
            p->vx = p->vx * 0.95f + 0.25f * sinf(p->life * 0.25f);   // side-to-side flutter
            if (p->vy > 0.9f) p->vy = 0.9f;
            p->angle += 0.12f * sinf(p->life * 0.2f);
            break;
        case P_CONFETTI:
            p->vx += 0.06f * sinf(p->life * 0.13f + p->size);
            p->vx *= 0.97f;
            p->angle += p->spin;
            break;
        default:                                              // solid debris
            p->vy += 0.16f;
            p->angle += p->spin;
            if (p->y > GROUND_Y - 1 && p->vy > 0.0f) {        // bounce off the ground, then slide
                p->y = GROUND_Y - 1;
                p->vy = -p->vy * 0.35f;
                p->vx *= 0.6f;
                p->spin *= 0.5f;
            }
            break;
        }
        p->x += p->vx;
        p->y += p->vy;
        if (p->x < -20 || p->x > GFX_W + 20 || p->y > GFX_H_PX + 20) p->alive = 0;
    }

    for (int i = 0; i < MAX_POPUPS; i++) {
        if (popups[i].life > 0) {
            popups[i].life--;
            popups[i].y -= popups[i].life > POPUP_FRAMES - 12 ? 1.6f : 0.35f;   // jump up, then drift
        }
    }
}

int fx_shake(void) {
    int a = (int)shake_amp;
    return (frame_no & 1) ? a : -a;
}

// ---------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------
static void outline_poly(const float (*q)[2], int n) {
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        gfx_line(gfx_round(q[i][0]), gfx_round(q[i][1]), gfx_round(q[j][0]), gfx_round(q[j][1]), BLACK);
    }
}

void fx_draw(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        const particle_t *p = &parts[i];
        if (!p->alive) continue;
        float co = cosf(p->angle), si = sinf(p->angle), s = p->size;
        float left = (float)p->life / (float)p->max_life;    // 1 = fresh, 0 = gone

        switch (p->type) {
        case P_SPLINTER: {
            float dx = co * s, dy = si * s;
            gfx_capsule(p->x - dx, p->y - dy, p->x + dx, p->y + dy, 2.0f, BLACK);
            gfx_capsule(p->x - dx, p->y - dy, p->x + dx, p->y + dy, 1.0f, (char)p->color);
            break;
        }
        case P_SHARD: {
            const float q[3][2] = {
                {p->x + co * s * 1.4f, p->y + si * s * 1.4f},
                {p->x - co * s - si * s * 0.7f, p->y - si * s + co * s * 0.7f},
                {p->x - co * s + si * s * 0.7f, p->y - si * s - co * s * 0.7f},
            };
            gfx_tri(q[0][0], q[0][1], q[1][0], q[1][1], q[2][0], q[2][1], (char)p->color);
            outline_poly(q, 3);
            break;
        }
        case P_CHUNK: {
            const float q[4][2] = {
                {p->x + (co - si) * s, p->y + (si + co) * s}, {p->x + (-co - si) * s, p->y + (-si + co) * s},
                {p->x + (-co + si) * s, p->y + (-si - co) * s}, {p->x + (co + si) * s, p->y + (si - co) * s},
            };
            gfx_quad_mix(q, WHITE, BLACK, 7);
            outline_poly(q, 4);
            break;
        }
        case P_PUFF:
            gfx_disc_blend(p->x, p->y, s, (char)p->color, (int)(14.0f * left));
            break;
        case P_FEATHER:
            gfx_ellipse_rot(p->x, p->y, s + 1.0f, s * 0.45f + 1.0f, co, si, BLACK);
            gfx_ellipse_rot(p->x, p->y, s, s * 0.45f, co, si, (char)p->color);
            break;
        case P_SPARK: {
            float r = s * (1.3f - left) + 2.0f;               // grows as it fades
            for (int k = 0; k < 4; k++) {
                float a = k * 0.785f + 0.39f;
                gfx_line((int)(p->x - cosf(a) * r), (int)(p->y - sinf(a) * r),
                         (int)(p->x + cosf(a) * r), (int)(p->y + sinf(a) * r), (char)p->color);
            }
            break;
        }
        case P_BIT:
            gfx_disc(p->x, p->y, s + 1.0f, BLACK);
            gfx_disc(p->x, p->y, s, (char)p->color);
            break;
        case P_CONFETTI: {
            float w = s * fabsf(co) + 0.6f;                    // "flips" as it spins
            gfx_rect((int)(p->x - w), (int)(p->y - s * 0.6f), (int)(p->x + w), (int)(p->y + s * 0.6f), (char)p->color);
            break;
        }
        }
    }

    for (int i = 0; i < MAX_POPUPS; i++) {
        const popup_t *u = &popups[i];
        if (u->life <= 0) continue;
        if (u->life < 10 && (u->life & 1)) continue;          // blink out at the end
        char text[12];
        snprintf(text, sizeof text, "%d", u->points);
        int w = gfx_text_width(text, u->scale);
        gfx_text_outlined((int)u->x - w / 2, (int)u->y, text, u->scale, u->scale > 1 ? YELLOW : WHITE, BLACK);
    }
}
