// game.c -- game rules: aiming, firing, scoring, level clear.
//
// No hardware calls in here, so the same code runs on the Pico and in the
// desktop test harness.
#include "game.h"
#include "physics.h"
#include "levels.h"
#include "render.h"
#include "fx.h"

#include <math.h>
#include <string.h>

typedef enum {
    STATE_AIMING,   // bird in the pouch, arc follows angle/power
    STATE_FLYING,   // bird launched
    STATE_DONE,     // shot is over; short pause, then reload
} game_state_t;

#define DONE_PAUSE_FRAMES 75    // pause before the next bird appears
#define BIRD_STILL_FRAMES 45    // bird this still for this long = shot is over
#define BIRD_STILL_SPEED  8.0f  // px/s
#define BIRD_MAX_FRAMES   600   // give up on a shot after 10 s no matter what
#define TRAIL_MAX         90
#define TRAIL_EVERY       3     // record a trail dot every N frames

#define POINTS_PIG    5000
#define POINTS_WOOD    500
#define POINTS_ICE     300
#define POINTS_STONE   800

// Stars on the level-clear banner, by number of birds used
#define STARS_3_MAX_SHOTS 4
#define STARS_2_MAX_SHOTS 6

static world_t world;           // the live world (the physics core owns it during a step)
static world_t view;            // copy of the world that the renderer draws from

static game_state_t state;
static slingshot_input_t input; // latest input, kept for drawing
static unsigned frame;          // frames since power-on
static int   state_frames;      // frames spent in the current state
static int   bird_idx;          // index of the bird in world.objs, -1 if none
static int   bird_still;        // frames the bird has been (nearly) motionless
static float fired_power;       // power of the current shot (for the band recoil)
static short trail[TRAIL_MAX][2], ghost[TRAIL_MAX][2];
static int   trail_count, ghost_count;
static int   score, shots;
static int   clear_frames;      // frames since the last pig popped, -1 while pigs remain
static int   stars;

static void set_state(game_state_t s) {
    state = s;
    state_frames = 0;
}

static obj_t *the_bird(void) {
    if (bird_idx < 0) return 0;
    obj_t *b = &world.objs[bird_idx];
    return (b->alive && b->mat == MAT_BIRD) ? b : 0;
}

static void reset_level(void) {
    level_load(&world, 1);
    fx_reset();
    bird_idx = -1;
    trail_count = ghost_count = 0;
    score = shots = 0;
    clear_frames = -1;
    stars = 0;
    set_state(STATE_AIMING);
}

void game_init(void) {
    frame = 0;
    memset(&input, 0, sizeof input);
    reset_level();
    view = world;
}

// Turn what the physics reported into effects and points.
static void handle_events(void) {
    for (int i = 0; i < world.event_count; i++) {
        const event_t *e = &world.events[i];
        fx_on_event(e);
        if (e->kind == EV_PIG_POP) {
            score += POINTS_PIG;
            fx_popup(e->x, e->y - 14, POINTS_PIG, 2);
        } else if (e->kind == EV_BLOCK_BREAK) {
            int pts = e->mat == MAT_STONE ? POINTS_STONE : e->mat == MAT_ICE ? POINTS_ICE : POINTS_WOOD;
            score += pts;
            fx_popup(e->x, e->y - 8, pts, 1);
        }
    }
    world_clear_events(&world);
}

void game_begin_frame(const slingshot_input_t *in) {
    input = *in;
    frame++;

    if (in->reset) {
        reset_level();
        view = world;
        return;
    }

    state_frames++;
    handle_events();
    obj_t *b = the_bird();

    switch (state) {
    case STATE_AIMING:
        if (in->fire) {
            body_t launch;
            physics_launch(&launch, in->angle_deg, in->power);
            bird_idx = world_add_bird(&world, &launch);
            bird_still = 0;
            fired_power = in->power;
            trail_count = 0;
            shots++;
            set_state(STATE_FLYING);
        }
        break;

    case STATE_FLYING:
        if (!b) {                              // flew off the side of the screen
            set_state(STATE_DONE);
            break;
        }
        // trail dots until the bird first hits something
        if (state_frames % TRAIL_EVERY == 0 && trail_count < TRAIL_MAX && !b->touched) {
            trail[trail_count][0] = (short)b->x;
            trail[trail_count][1] = (short)b->y;
            trail_count++;
        }
        if (b->vx * b->vx + b->vy * b->vy < BIRD_STILL_SPEED * BIRD_STILL_SPEED) bird_still++;
        else bird_still = 0;
        if (bird_still >= BIRD_STILL_FRAMES || state_frames >= BIRD_MAX_FRAMES) {
            set_state(STATE_DONE);
        }
        break;

    case STATE_DONE:
        if (state_frames >= DONE_PAUSE_FRAMES) {
            if (b) {                           // bird vanishes in a puff of feathers
                fx_bird_poof(b->x, b->y);
                b->alive = false;
            }
            bird_idx = -1;
            memcpy(ghost, trail, sizeof ghost);    // keep this shot's path as an aiming guide
            ghost_count = trail_count;
            trail_count = 0;
            set_state(STATE_AIMING);
        }
        break;
    }

    // level clear
    if (world_pigs_alive(&world) == 0) {
        if (clear_frames < 0) {
            clear_frames = 0;
            stars = shots <= STARS_3_MAX_SHOTS ? 3 : shots <= STARS_2_MAX_SHOTS ? 2 : 1;
        } else {
            clear_frames++;
        }
        if (clear_frames == 50) fx_confetti();
    }

    fx_update();
    view = world;          // snapshot for the renderer
}

void game_physics(void) {
    world_step(&world, PHYS_DT);
}

void game_draw(char *framebuffer) {
    scene_t s;
    s.world = &view;
    s.bird_idx = bird_idx;
    s.aiming = (state == STATE_AIMING);
    s.aim_angle = input.angle_deg;
    s.aim_power = input.power;

    // after a shot the empty pouch snaps forward and wobbles to a stop
    float t = (float)state_frames / 60.0f;
    s.band_kick = s.aiming ? 0.0f
                : -fired_power * SLING_MAX_PULL * expf(-7.0f * t) * cosf(42.0f * t);

    s.trail = trail;  s.trail_count = trail_count;
    s.ghost = ghost;  s.ghost_count = ghost_count;
    s.score = score;
    s.shots = shots;
    s.pigs_left = world_pigs_alive(&view);
    s.clear_frames = clear_frames;
    s.stars = stars;
    s.frame = frame;
    s.shake = fx_shake();
    render_scene(&s, framebuffer);
}
