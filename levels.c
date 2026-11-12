// levels.c -- level layouts
//
// Pieces are placed by LEFT edge and BOTTOM edge (in screen pixels), which makes
// stacking easy: a piece sitting on another uses the lower piece's top as its
// bottom. GROUND_Y is the top of the grass (420). Screen is 640 x 480.
//
//   BOX(material, left, bottom, width, height)
//   PIG(centre_x, bottom, radius)
#include "levels.h"
#include "game.h"

typedef struct {
    bool       is_pig;
    material_t mat;
    float      left, bottom, w, h;   // box
    float      cx, r;                // pig (uses bottom too)
} piece_t;

#define BOX(m, l, b, w_, h_) { false, (m), (l), (b), (w_), (h_), 0, 0 }
#define PIG(x, b, r_)        { true,  MAT_PIG, 0, (b), 0, 0, (x), (r_) }

// ---------------------------------------------------------------------
// Level 1 -- a wooden lookout plus a three-storey fort
//
//                                 [ice]            <- 220
//                            [==stone cap==]       <- 240
//                             |   (BOSS)  |        <- top tower, 250-290
//                   [=====wood=====][=====wood=====]  <- 290
//                    ice   (pig)   ice   (pig)   ice    <- floor 2, 300-350
//                   [=====wood=====][=====wood=====]  <- 350
//   lookout          stone (pig)  stone  (pig)  stone  <- floor 1, 360-420
//  [=(pig)=]
//   |     |
// ---------------------------------------------------------------- 420 ground
//  335-385          415 ........................... 605
// ---------------------------------------------------------------------
static const piece_t LEVEL_1[] = {
    // --- wooden lookout out front ---
    BOX(MAT_WOOD,  340, 420, 10, 40),   // left leg
    BOX(MAT_WOOD,  370, 420, 10, 40),   // right leg
    BOX(MAT_WOOD,  335, 380, 50,  8),   // platform      (top at 372)
    PIG(360, 372, 9),                   // lookout pig

    // --- fort, floor 1: stone pillars ---
    BOX(MAT_STONE, 420, 420, 12, 60),   // left pillar   (top at 360)
    BOX(MAT_STONE, 504, 420, 12, 60),   // middle pillar
    BOX(MAT_STONE, 588, 420, 12, 60),   // right pillar
    PIG(468, 420, 11),                  // ground floor pigs
    PIG(552, 420, 12),
    BOX(MAT_WOOD,  415, 360, 95, 10),   // floor planks  (top at 350)
    BOX(MAT_WOOD,  510, 360, 95, 10),

    // --- fort, floor 2: ice pillars ---
    BOX(MAT_ICE,   430, 350, 10, 50),   // left pillar   (top at 300)
    BOX(MAT_ICE,   505, 350, 10, 50),   // middle pillar (holds up both planks)
    BOX(MAT_ICE,   580, 350, 10, 50),   // right pillar
    PIG(472, 350, 10),                  // second floor pigs
    PIG(548, 350, 10),
    BOX(MAT_WOOD,  425, 300, 85, 10),   // roof planks   (top at 290)
    BOX(MAT_WOOD,  510, 300, 85, 10),

    // --- fort, top tower ---
    BOX(MAT_WOOD,  480, 290, 10, 40),   // tower walls   (top at 250)
    BOX(MAT_WOOD,  530, 290, 10, 40),
    PIG(510, 290, 13),                  // boss pig
    BOX(MAT_STONE, 475, 250, 70, 10),   // stone cap     (top at 240)
    BOX(MAT_ICE,   500, 240, 20, 20),   // ice block on top
};

static void build(world_t *w, const piece_t *pieces, int n) {
    for (int i = 0; i < n; i++) {
        const piece_t *p = &pieces[i];
        if (p->is_pig) {
            world_add_pig(w, p->cx, p->bottom - p->r, p->r);
        } else {
            world_add_box(w, p->mat, p->left + p->w * 0.5f, p->bottom - p->h * 0.5f, p->w, p->h);
        }
    }
}

void level_load(world_t *w, int n) {
    (void)n;  // only one level so far
    world_init(w);
    build(w, LEVEL_1, (int)(sizeof LEVEL_1 / sizeof LEVEL_1[0]));
}
