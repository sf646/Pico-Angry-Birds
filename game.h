// game.h -- shared constants, plus the game's top-level functions (game.c)
#ifndef GAME_H
#define GAME_H

// Screen (VGA library is fixed at 640x480, 16 colors)
#define SCREEN_W 640
#define SCREEN_H 480

// Top of the ground, in screen pixels (y grows downward)
#define GROUND_Y 420

// Slingshot: base of the trunk sits on the ground at SLING_X.
// SLING_FORK_Y is where the two arms split; the pouch rests between the arm tips.
#define SLING_X       110
#define SLING_FORK_Y  374
#define SLING_ARM_DX  16   // horizontal offset of each arm tip from SLING_X
#define SLING_ARM_DY  36   // arm tips are this far above the fork
#define SLING_POUCH_Y (SLING_FORK_Y - SLING_ARM_DY + 4)

// How far (pixels) the bird is pulled back at 100% power
#define SLING_MAX_PULL 30.0f

// Bird
#define BIRD_RADIUS 12

// Aim limits
#define ANGLE_MIN_DEG  0.0f
#define ANGLE_MAX_DEG 90.0f

// ---------------------------------------------------------------------
// Game loop (game.c). One frame is:
//
//   game_begin_frame(input)   game rules; hands the world to the physics
//   game_physics()            advance the physics one frame   (second core)
//   game_draw(framebuffer)    draw the frame                  (first core)
//
// game_physics() and game_draw() may run at the same time on different cores:
// drawing uses a snapshot taken in game_begin_frame(). game_begin_frame() must
// only be called while game_physics() is NOT running.
// ---------------------------------------------------------------------
#include "input.h"

void game_init(void);
void game_begin_frame(const slingshot_input_t *in);
void game_physics(void);
void game_draw(char *framebuffer);

#endif
