// render.h -- draws one frame of the game into the VGA frame buffer
#ifndef RENDER_H
#define RENDER_H

#include "physics.h"

// Everything the renderer needs to know to draw one frame.
typedef struct {
    const world_t *world;         // bodies to draw (a snapshot; physics may be running)
    int   bird_idx;               // index of the flying bird in world->objs, or -1
    bool  aiming;                 // true while a bird is waiting in the pouch
    float aim_angle, aim_power;   // current slingshot input
    float band_kick;              // pouch recoil after a shot, px along the aim line

    const short (*trail)[2];      // dots behind the bird on this shot
    int   trail_count;
    const short (*ghost)[2];      // the previous shot's path (helps aiming)
    int   ghost_count;

    int   score, shots, pigs_left;
    int   clear_frames;           // frames since the last pig popped, -1 if pigs remain
    int   stars;                  // 1..3, shown on the level-clear banner
    unsigned frame;               // running frame counter (drives blinking etc.)
    int   shake;                  // vertical screen-shake offset, px
} scene_t;

// Paint the scenery cache. Call once at startup (takes a moment).
void render_init(void);
// Draw a whole frame into `framebuffer`.
void render_scene(const scene_t *s, char *framebuffer);

// --- background.c: static scenery, painted once and copied every frame ---
void render_background_build(void);
void render_background_blit(int shake_dy);
int  render_grass_blade(int x);     // height of the grass blade at column x
void render_hud_static(void);       // fixed labels, painted into the scenery cache

#endif
