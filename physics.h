// physics.h -- 2D rigid-body physics. Pure C, no VGA / Pico calls, so the same
// code runs on the Pico and in the desktop test harness.
//
// Units: pixels and seconds. Screen coordinates, so +y is DOWN.
// Positive rotation is clockwise on screen (rolling to the right).
//
// The world is a list of rigid bodies: boxes (blocks -- they rotate, topple and
// slide) and circles (pigs, the bird). Each step finds every touching pair,
// then solves the contacts with impulses (bounce + friction) and deals damage
// based on how hard things hit.
#ifndef PHYSICS_H
#define PHYSICS_H

#include <stdbool.h>

#define GRAVITY          400.0f         // px/s^2 (downward)
#define MAX_LAUNCH_SPEED 500.0f         // px/s at 100% power
#define PHYS_DT          (1.0f / 60.0f) // one world step per VGA frame

// ---------------------------------------------------------------------
// Simple projectile (used for the aiming preview and the launch)
// ---------------------------------------------------------------------
typedef struct {
    float x, y;     // position (px)
    float vx, vy;   // velocity (px/s)
} body_t;

// Where the bird sits in the pouch for a given aim (pulled back opposite the aim).
void physics_pouch_position(float angle_deg, float power, float *x, float *y);
// Put a body at the pouch with the launch velocity for this aim.
void physics_launch(body_t *b, float angle_deg, float power);
// Advance one time step under gravity only.
void physics_step(body_t *b, float dt);

// ---------------------------------------------------------------------
// World
// ---------------------------------------------------------------------
typedef enum { MAT_WOOD, MAT_STONE, MAT_ICE, MAT_PIG, MAT_BIRD, MAT_GROUND, MAT_COUNT } material_t;
typedef enum { SHAPE_BOX, SHAPE_CIRCLE } shape_t;

typedef struct {
    shape_t    shape;
    material_t mat;
    float x, y, angle;       // centre + rotation (rad)
    float vx, vy, omega;     // velocity + spin (rad/s)
    float hw, hh;            // half width / half height (boxes)
    float r;                 // radius (circles)
    float bound;             // bounding-circle radius (quick overlap test)
    float inv_mass, inv_inertia;
    float health, max_health;
    float pending_damage;    // damage collected during the current sub-step
    float co, si;            // cos/sin of angle (kept up to date by the solver)
    bool  alive;
    bool  awake;             // sleeping bodies don't move until something disturbs them
    bool  touched;           // has hit anything since it was created (used for the bird)
    bool  resting;           // is touching something this step (for rolling friction)
    int   still_frames;      // frames spent nearly motionless (for falling asleep)
} obj_t;

typedef enum {
    EV_PIG_POP,      // a pig was destroyed
    EV_BLOCK_BREAK,  // a block was destroyed
    EV_HIT,          // a hard impact (for dust / splinters / screen shake)
} event_kind_t;

typedef struct {
    event_kind_t kind;
    material_t   mat;        // what broke / what got hit
    material_t   other;      // EV_HIT: what it hit
    float x, y;              // where
    float hw, hh, angle;     // size + rotation of the thing that broke
    float vx, vy;            // its velocity (debris inherits it)
    float strength;          // EV_HIT: how hard (damage units)
} event_t;

#define MAX_OBJS   40
#define MAX_EVENTS 24

typedef struct {
    obj_t   objs[MAX_OBJS];
    int     count;
    event_t events[MAX_EVENTS];   // filled by world_step, cleared by world_clear_events
    int     event_count;
} world_t;

void world_init(world_t *w);
// Add bodies. Blocks/pigs start asleep. Return the body index, or -1 if full.
int  world_add_box(world_t *w, material_t mat, float cx, float cy, float width, float height);
int  world_add_pig(world_t *w, float cx, float cy, float r);
int  world_add_bird(world_t *w, const body_t *launch);

// Advance the whole world by dt (internally sub-stepped).
void world_step(world_t *w, float dt);
void world_clear_events(world_t *w);
void world_wake_all(world_t *w);

int  world_pigs_alive(const world_t *w);

#endif
