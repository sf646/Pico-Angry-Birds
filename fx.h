// fx.h -- visual-only effects: debris, dust, feathers, smoke puffs, score
// popups, confetti and screen shake. None of this affects the physics.
#ifndef FX_H
#define FX_H

#include "physics.h"

void fx_reset(void);
void fx_update(void);            // advance every effect by one frame
void fx_draw(void);              // draw particles + popups (call after the world)

// React to something the physics reported (impact, block break, pig pop).
void fx_on_event(const event_t *e);

void fx_popup(float x, float y, int points, int scale);   // floating score text
void fx_bird_poof(float x, float y);                      // bird disappears in feathers
void fx_confetti(void);                                   // level cleared!

int  fx_shake(void);             // vertical screen offset for this frame (px)

#endif
