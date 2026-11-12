// gfx.h -- drawing primitives for the 640x480, 16-color VGA frame buffer.
//
// Everything here is clipped to the screen and writes straight into a frame
// buffer (2 pixels per byte, 320 bytes per row), so it is safe for shapes that
// hang off any edge. The VGA library's own shape functions are not used: they
// don't clip everywhere and can write outside the buffer.
#ifndef GFX_H
#define GFX_H

#include "VGA/vga16_graphics_v3.h"   // color names (BLACK ... WHITE)

// Round to nearest int. (lroundf() is a slow library call on the Pico.)
static inline int gfx_round(float x) { return (int)(x + (x >= 0.0f ? 0.5f : -0.5f)); }

#define GFX_W 640
#define GFX_H_PX 480
#define GFX_ROW_BYTES 320
#define GFX_BUF_BYTES (GFX_ROW_BYTES * GFX_H_PX)

// Choose the buffer all drawing goes to (the VGA draw buffer, or an off-screen cache).
void gfx_target(char *buffer);
char *gfx_get_target(void);

// Vertical offset added to everything drawn (screen shake). 0 = none.
void gfx_set_shake(int dy);

// --- pixels and spans ---
void gfx_px(int x, int y, char c);
void gfx_hspan(int x0, int x1, int y, char c);                      // inclusive ends
// Blend of two colors by ordered dithering. level 0 = all a ... 16 = all b.
void gfx_px_mix(int x, int y, char a, char b, int level);
void gfx_hspan_mix(int x0, int x1, int y, char a, char b, int level);
// See-through color: only some pixels are drawn. level 0 = invisible ... 16 = solid.
void gfx_px_blend(int x, int y, char c, int level);
void gfx_hspan_blend(int x0, int x1, int y, char c, int level);
// Read a pixel back (returns 0 if off-screen).
char gfx_read(int x, int y);

// --- shapes ---
void gfx_rect(int x0, int y0, int x1, int y1, char c);              // inclusive corners
void gfx_ellipse(float cx, float cy, float rx, float ry, char c);
void gfx_disc(float cx, float cy, float r, char c);
void gfx_disc_blend(float cx, float cy, float r, char c, int level);   // see-through disc
void gfx_ellipse_rot(float cx, float cy, float rx, float ry, float co, float si, char c);
void gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, char c);
// Convex 4-sided polygon; points in order around the shape.
void gfx_quad(const float p[4][2], char c);
void gfx_quad_mix(const float p[4][2], char a, char b, int level);
void gfx_line(int x0, int y0, int x1, int y1, char c);              // 1 px
void gfx_capsule(float x0, float y0, float x1, float y1, float r, char c);  // thick, round ends

// --- text (5x7 font, transparent background) ---
int  gfx_text_width(const char *s, int scale);
void gfx_text(int x, int y, const char *s, int scale, char c);
// Text with a 1-pixel outline around it.
void gfx_text_outlined(int x, int y, const char *s, int scale, char c, char outline);

#endif
