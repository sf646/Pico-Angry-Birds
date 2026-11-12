// physics.c -- 2D rigid-body physics (see physics.h)
//
// Each frame is split into SUBSTEPS small steps. Every sub-step:
//   1. apply gravity
//   2. find all contacts (box-box, box-circle, circle-circle, anything-ground)
//   3. solve the contacts with impulses a few times over (bounce + friction)
//   4. move everything, then nudge overlapping bodies apart
//   5. apply impact damage, destroy what broke
//
// The box-box contact routine follows Erin Catto's Box2D-Lite (separating axis
// test, then clip the "incident" edge against the "reference" face).
#include "physics.h"
#include "game.h"

#include <math.h>
#include <string.h>

#define DEG2RAD(d) ((d) * 0.017453293f)
#define TWO_PI 6.2831853f

// ---------------------------------------------------------------------
// Tuning
// ---------------------------------------------------------------------
#define SUBSTEPS        8        // physics sub-steps per frame
#define SOLVER_ITERS    4        // impulse passes per sub-step
#define MAX_CONTACTS    192

#define POS_SLOP        0.25f    // px of overlap we tolerate (stops jitter)
#define POS_CORRECT     0.2f     // fraction of extra overlap removed per contact per sub-step
#define BOUNCE_MIN_VN   40.0f    // px/s; slower impacts don't bounce at all
#define LIN_DAMP        0.05f    // 1/s  air drag
#define ANG_DAMP        0.40f    // 1/s  spin drag
#define ROLL_DAMP       2.50f    // 1/s  extra spin drag for circles touching something
#define ROLL_BRAKE      3.00f    // rad/s^2  constant rolling resistance (so slow rolls actually stop)

#define DAMAGE_MIN_VN   55.0f    // px/s; gentler impacts do no damage
#define DAMAGE_SCALE    0.0011f  // damage = (impact - min) * effective_mass * scale
#define RUBBLE_DAMAGE   0.30f    // blocks hitting blocks / the ground do only this fraction...
#define CRUSH_DAMAGE    0.75f    // ...but pigs are squishy: they take this fraction from them
#define HIT_EVENT_MIN   4.0f     // damage needed to emit an EV_HIT
#define SMASH_KEEP      0.70f    // fraction of its speed the bird keeps when it smashes a block

#define WAKE_SPEED      14.0f    // px/s; contact this fast with an awake body wakes a sleeper
#define SLEEP_SPEED     5.0f     // px/s
#define SLEEP_SPIN      0.12f    // rad/s
#define SLEEP_FRAMES    36       // this still for this long -> fall asleep
#define OFFSCREEN_PAD   80.0f    // bodies this far off the sides are removed

typedef struct {
    float density;      // mass per px^2
    float restitution;  // bounciness 0..1
    float friction;
    float health;       // per 600 px^2 of area (boxes) / per body (pigs)
} mat_props_t;

static const mat_props_t MATS[MAT_COUNT] = {
    [MAT_WOOD]   = { 1.0f, 0.12f, 0.70f, 100.0f },
    [MAT_STONE]  = { 2.4f, 0.05f, 0.80f, 260.0f },
    [MAT_ICE]    = { 0.8f, 0.10f, 0.30f,  42.0f },
    [MAT_PIG]    = { 1.0f, 0.25f, 0.60f,  26.0f },
    [MAT_BIRD]   = { 2.2f, 0.30f, 0.60f,   0.0f },  // bird takes no damage
    [MAT_GROUND] = { 0.0f, 0.15f, 0.90f,   0.0f },
};

// ---------------------------------------------------------------------
// Projectile
// ---------------------------------------------------------------------
void physics_pouch_position(float angle_deg, float power, float *x, float *y) {
    float a = DEG2RAD(angle_deg);
    float pull = power * SLING_MAX_PULL;
    // Aim direction is (cos a, -sin a) in screen coords; pull back the opposite way.
    *x = SLING_X       - cosf(a) * pull;
    *y = SLING_POUCH_Y + sinf(a) * pull;
}

void physics_launch(body_t *b, float angle_deg, float power) {
    float a = DEG2RAD(angle_deg);
    float speed = power * MAX_LAUNCH_SPEED;
    physics_pouch_position(angle_deg, power, &b->x, &b->y);
    b->vx =  cosf(a) * speed;
    b->vy = -sinf(a) * speed;  // up is negative y
}

void physics_step(body_t *b, float dt) {
    // Semi-implicit Euler: update velocity first, then position.
    b->vy += GRAVITY * dt;
    b->x  += b->vx * dt;
    b->y  += b->vy * dt;
}

// ---------------------------------------------------------------------
// World setup
// ---------------------------------------------------------------------
static obj_t ground;   // immovable stand-in body for the ground

void world_init(world_t *w) {
    memset(w, 0, sizeof *w);
    memset(&ground, 0, sizeof ground);
    ground.mat = MAT_GROUND;
    ground.co = 1.0f;
}

static obj_t *new_obj(world_t *w) {
    obj_t *o = 0;
    for (int i = 0; i < w->count && !o; i++) {
        if (!w->objs[i].alive) o = &w->objs[i];      // reuse a dead slot
    }
    if (!o) {
        if (w->count >= MAX_OBJS) return 0;
        o = &w->objs[w->count++];
    }
    memset(o, 0, sizeof *o);
    o->co = 1.0f;
    o->alive = true;
    return o;
}

int world_add_box(world_t *w, material_t mat, float cx, float cy, float width, float height) {
    obj_t *o = new_obj(w);
    if (!o) return -1;
    float mass = width * height * MATS[mat].density;
    o->shape = SHAPE_BOX;
    o->mat = mat;
    o->x = cx; o->y = cy;
    o->hw = width * 0.5f; o->hh = height * 0.5f;
    o->bound = sqrtf(o->hw * o->hw + o->hh * o->hh);
    o->inv_mass = 1.0f / mass;
    o->inv_inertia = 12.0f / (mass * (width * width + height * height));
    o->health = o->max_health = MATS[mat].health * width * height / 600.0f;
    return (int)(o - w->objs);
}

static int add_circle(world_t *w, material_t mat, float cx, float cy, float r) {
    obj_t *o = new_obj(w);
    if (!o) return -1;
    float mass = 3.14159f * r * r * MATS[mat].density;
    o->shape = SHAPE_CIRCLE;
    o->mat = mat;
    o->x = cx; o->y = cy; o->r = r;
    o->bound = r;
    o->inv_mass = 1.0f / mass;
    o->inv_inertia = 2.0f / (mass * r * r);
    o->health = o->max_health = MATS[mat].health;
    return (int)(o - w->objs);
}

int world_add_pig(world_t *w, float cx, float cy, float r) {
    return add_circle(w, MAT_PIG, cx, cy, r);
}

int world_add_bird(world_t *w, const body_t *launch) {
    int i = add_circle(w, MAT_BIRD, launch->x, launch->y, BIRD_RADIUS);
    if (i >= 0) {
        w->objs[i].vx = launch->vx;
        w->objs[i].vy = launch->vy;
        w->objs[i].awake = true;
    }
    return i;
}

void world_clear_events(world_t *w) { w->event_count = 0; }

void world_wake_all(world_t *w) {
    for (int i = 0; i < w->count; i++) {
        if (w->objs[i].alive) { w->objs[i].awake = true; w->objs[i].still_frames = 0; }
    }
}

int world_pigs_alive(const world_t *w) {
    int n = 0;
    for (int i = 0; i < w->count; i++) {
        if (w->objs[i].alive && w->objs[i].mat == MAT_PIG) n++;
    }
    return n;
}

// ---------------------------------------------------------------------
// Events / destruction
// ---------------------------------------------------------------------
static event_t *push_event(world_t *w, event_kind_t kind) {
    if (w->event_count >= MAX_EVENTS) return 0;
    event_t *e = &w->events[w->event_count++];
    memset(e, 0, sizeof *e);
    e->kind = kind;
    return e;
}

static void destroy(world_t *w, obj_t *o) {
    if (!o->alive) return;
    o->alive = false;
    event_t *e = push_event(w, o->mat == MAT_PIG ? EV_PIG_POP : EV_BLOCK_BREAK);
    if (e) {
        e->mat = o->mat;
        e->x = o->x; e->y = o->y; e->angle = o->angle;
        e->hw = o->shape == SHAPE_BOX ? o->hw : o->r;
        e->hh = o->shape == SHAPE_BOX ? o->hh : o->r;
        e->vx = o->vx; e->vy = o->vy;
    }
    world_wake_all(w);  // whatever was resting on it should now fall
}

// ---------------------------------------------------------------------
// Contacts
// ---------------------------------------------------------------------
typedef struct {
    obj_t *a, *b;            // normal points from a to b
    float nx, ny;
    float rax, ray, rbx, rby;   // contact point relative to each body's centre
    float pen;                  // overlap depth
    float ima, imb, iia, iib;   // inverse mass / inertia actually used (0 if asleep)
    float mass_n, mass_t;       // effective mass along normal / tangent
    float bounce;               // target separation speed after the bounce
    float mu;                   // friction coefficient
    float jn, jt;               // impulses applied so far
} contact_t;

static contact_t contacts[MAX_CONTACTS];
static int n_contacts;
static float pre_vx[MAX_OBJS], pre_vy[MAX_OBJS];   // bird velocity before contact solving

static inline void wake(obj_t *o) {
    o->awake = true;
    o->still_frames = 0;
}

// Record a contact. (px, py) is the contact point in world coordinates.
static void add_contact(obj_t *a, obj_t *b, float nx, float ny, float px, float py, float pen) {
    float rax = px - a->x, ray = py - a->y;
    float rbx = px - b->x, rby = py - b->y;

    // A body moving fast enough against a sleeper wakes it up.
    if (a->awake != b->awake) {
        float rvx = (b->vx - b->omega * rby) - (a->vx - a->omega * ray);
        float rvy = (b->vy + b->omega * rbx) - (a->vy + a->omega * rax);
        if (rvx * rvx + rvy * rvy > WAKE_SPEED * WAKE_SPEED) {
            if (a != &ground && !a->awake) wake(a);
            if (b != &ground && !b->awake) wake(b);
        }
    }
    a->touched = b->touched = true;
    a->resting = b->resting = true;

    float ima = a->awake ? a->inv_mass : 0.0f, iia = a->awake ? a->inv_inertia : 0.0f;
    float imb = b->awake ? b->inv_mass : 0.0f, iib = b->awake ? b->inv_inertia : 0.0f;
    if (ima + imb <= 0.0f) return;           // neither can move
    if (n_contacts >= MAX_CONTACTS) return;

    contact_t *c = &contacts[n_contacts++];
    c->a = a; c->b = b;
    c->nx = nx; c->ny = ny;
    c->rax = rax; c->ray = ray; c->rbx = rbx; c->rby = rby;
    c->pen = pen;
    c->ima = ima; c->imb = imb; c->iia = iia; c->iib = iib;
    c->jn = c->jt = 0.0f;
}

// --- box vs box (Box2D-Lite) ------------------------------------------

typedef struct { float x, y; } v2;

static inline v2 v(float x, float y) { v2 r = {x, y}; return r; }
static inline float dot(v2 a, v2 b) { return a.x * b.x + a.y * b.y; }

static int clip_segment(v2 out[2], const v2 in[2], v2 n, float offset) {
    int num = 0;
    float d0 = dot(n, in[0]) - offset;
    float d1 = dot(n, in[1]) - offset;
    if (d0 <= 0.0f) out[num++] = in[0];
    if (d1 <= 0.0f) out[num++] = in[1];
    if (d0 * d1 < 0.0f) {
        float t = d0 / (d0 - d1);
        out[num++] = v(in[0].x + t * (in[1].x - in[0].x), in[0].y + t * (in[1].y - in[0].y));
    }
    return num;
}

// The edge of box `o` that faces most directly against `normal`.
static void incident_edge(v2 c[2], const obj_t *o, v2 normal) {
    // normal in the box's own frame, flipped
    float nx = -(normal.x * o->co + normal.y * o->si);
    float ny = -(-normal.x * o->si + normal.y * o->co);
    v2 l0, l1;
    if (fabsf(nx) > fabsf(ny)) {
        if (nx > 0.0f) { l0 = v( o->hw, -o->hh); l1 = v( o->hw,  o->hh); }
        else           { l0 = v(-o->hw,  o->hh); l1 = v(-o->hw, -o->hh); }
    } else {
        if (ny > 0.0f) { l0 = v( o->hw,  o->hh); l1 = v(-o->hw,  o->hh); }
        else           { l0 = v(-o->hw, -o->hh); l1 = v( o->hw, -o->hh); }
    }
    c[0] = v(o->x + l0.x * o->co - l0.y * o->si, o->y + l0.x * o->si + l0.y * o->co);
    c[1] = v(o->x + l1.x * o->co - l1.y * o->si, o->y + l1.x * o->si + l1.y * o->co);
}

static void collide_box_box(obj_t *A, obj_t *B) {
    // box axes in world space
    v2 ax = v(A->co, A->si), ay = v(-A->si, A->co);
    v2 bx = v(B->co, B->si), by = v(-B->si, B->co);
    v2 dp = v(B->x - A->x, B->y - A->y);
    v2 dA = v(dot(dp, ax), dot(dp, ay));      // B's centre in A's frame
    v2 dB = v(dot(dp, bx), dot(dp, by));      // A->B offset in B's frame

    // |A^T B|
    float c11 = fabsf(dot(ax, bx)), c12 = fabsf(dot(ax, by));
    float c21 = fabsf(dot(ay, bx)), c22 = fabsf(dot(ay, by));

    // separation along each of the four face normals (positive = gap)
    float fax = fabsf(dA.x) - A->hw - (c11 * B->hw + c12 * B->hh);
    if (fax > 0.0f) return;
    float fay = fabsf(dA.y) - A->hh - (c21 * B->hw + c22 * B->hh);
    if (fay > 0.0f) return;
    float fbx = fabsf(dB.x) - B->hw - (c11 * A->hw + c21 * A->hh);
    if (fbx > 0.0f) return;
    float fby = fabsf(dB.y) - B->hh - (c12 * A->hw + c22 * A->hh);
    if (fby > 0.0f) return;

    // pick the axis of least overlap (with a bias so the choice doesn't flicker)
    enum { A_X, A_Y, B_X, B_Y } axis = A_X;
    float sep = fax;
    v2 normal = dA.x > 0.0f ? ax : v(-ax.x, -ax.y);
    const float rel = 0.95f, abs_tol = 0.01f;
    if (fay > rel * sep + abs_tol * A->hh) { axis = A_Y; sep = fay; normal = dA.y > 0.0f ? ay : v(-ay.x, -ay.y); }
    if (fbx > rel * sep + abs_tol * B->hw) { axis = B_X; sep = fbx; normal = dB.x > 0.0f ? bx : v(-bx.x, -bx.y); }
    if (fby > rel * sep + abs_tol * B->hh) { axis = B_Y; sep = fby; normal = dB.y > 0.0f ? by : v(-by.x, -by.y); }

    // reference face (on one box) and incident edge (on the other)
    v2 front_n, side_n, inc[2];
    float front, neg_side, pos_side, side;
    switch (axis) {
    case A_X:
        front_n = normal; front = dot(v(A->x, A->y), front_n) + A->hw;
        side_n = ay; side = dot(v(A->x, A->y), side_n);
        neg_side = -side + A->hh; pos_side = side + A->hh;
        incident_edge(inc, B, front_n);
        break;
    case A_Y:
        front_n = normal; front = dot(v(A->x, A->y), front_n) + A->hh;
        side_n = ax; side = dot(v(A->x, A->y), side_n);
        neg_side = -side + A->hw; pos_side = side + A->hw;
        incident_edge(inc, B, front_n);
        break;
    case B_X:
        front_n = v(-normal.x, -normal.y); front = dot(v(B->x, B->y), front_n) + B->hw;
        side_n = by; side = dot(v(B->x, B->y), side_n);
        neg_side = -side + B->hh; pos_side = side + B->hh;
        incident_edge(inc, A, front_n);
        break;
    default: // B_Y
        front_n = v(-normal.x, -normal.y); front = dot(v(B->x, B->y), front_n) + B->hh;
        side_n = bx; side = dot(v(B->x, B->y), side_n);
        neg_side = -side + B->hw; pos_side = side + B->hw;
        incident_edge(inc, A, front_n);
        break;
    }

    // clip the incident edge to the sides of the reference face
    v2 c1[2], c2[2];
    if (clip_segment(c1, inc, v(-side_n.x, -side_n.y), neg_side) < 2) return;
    if (clip_segment(c2, c1, side_n, pos_side) < 2) return;

    // whatever is still behind the reference face is in contact
    for (int i = 0; i < 2; i++) {
        float s = dot(front_n, c2[i]) - front;
        if (s <= 0.0f) {
            // slide the point onto the reference face
            add_contact(A, B, normal.x, normal.y,
                        c2[i].x - s * front_n.x, c2[i].y - s * front_n.y, -s);
        }
    }
}

// --- box vs circle ----------------------------------------------------

static void collide_box_circle(obj_t *A, obj_t *C) {
    // circle centre in the box's frame
    float dx = C->x - A->x, dy = C->y - A->y;
    float lx = dx * A->co + dy * A->si;
    float ly = -dx * A->si + dy * A->co;
    // closest point on the box
    float qx = fminf(fmaxf(lx, -A->hw), A->hw);
    float qy = fminf(fmaxf(ly, -A->hh), A->hh);
    float ex = lx - qx, ey = ly - qy;
    float d2 = ex * ex + ey * ey;
    if (d2 >= C->r * C->r) return;

    float nlx, nly, pen;
    if (d2 > 1e-6f) {
        float d = sqrtf(d2);
        nlx = ex / d; nly = ey / d;
        pen = C->r - d;
    } else {
        // centre is inside the box: push out through the nearest face
        float ox = A->hw - fabsf(lx), oy = A->hh - fabsf(ly);
        if (ox < oy) { nlx = lx < 0.0f ? -1.0f : 1.0f; nly = 0.0f; qx = nlx * A->hw; pen = ox + C->r; }
        else         { nlx = 0.0f; nly = ly < 0.0f ? -1.0f : 1.0f; qy = nly * A->hh; pen = oy + C->r; }
    }
    add_contact(A, C,
                nlx * A->co - nly * A->si, nlx * A->si + nly * A->co,
                A->x + qx * A->co - qy * A->si, A->y + qx * A->si + qy * A->co, pen);
}

// --- circle vs circle -------------------------------------------------

static void collide_circle_circle(obj_t *A, obj_t *B) {
    float dx = B->x - A->x, dy = B->y - A->y;
    float rs = A->r + B->r;
    float d2 = dx * dx + dy * dy;
    if (d2 >= rs * rs || d2 < 1e-6f) return;
    float d = sqrtf(d2);
    float nx = dx / d, ny = dy / d;
    add_contact(A, B, nx, ny, A->x + nx * A->r, A->y + ny * A->r, rs - d);
}

// --- anything vs ground -----------------------------------------------

static void collide_ground(obj_t *o) {
    if (o->y + o->bound <= GROUND_Y) return;
    if (o->shape == SHAPE_CIRCLE) {
        add_contact(&ground, o, 0.0f, -1.0f, o->x, (float)GROUND_Y, o->y + o->r - GROUND_Y);
        return;
    }
    for (int i = 0; i < 4; i++) {
        float lx = (i & 1) ? o->hw : -o->hw;
        float ly = (i & 2) ? o->hh : -o->hh;
        float cx = o->x + lx * o->co - ly * o->si;
        float cy = o->y + lx * o->si + ly * o->co;
        if (cy > GROUND_Y) add_contact(&ground, o, 0.0f, -1.0f, cx, (float)GROUND_Y, cy - GROUND_Y);
    }
}

static void find_contacts(world_t *w) {
    n_contacts = 0;
    for (int i = 0; i < w->count; i++) {
        obj_t *a = &w->objs[i];
        if (!a->alive) continue;
        if (a->awake) collide_ground(a);
        for (int j = i + 1; j < w->count; j++) {
            obj_t *b = &w->objs[j];
            if (!b->alive) continue;
            if (!a->awake && !b->awake) continue;
            // quick reject with bounding circles
            float dx = b->x - a->x, dy = b->y - a->y, rs = a->bound + b->bound;
            if (dx * dx + dy * dy >= rs * rs) continue;

            if (a->shape == SHAPE_BOX && b->shape == SHAPE_BOX) collide_box_box(a, b);
            else if (a->shape == SHAPE_BOX)                     collide_box_circle(a, b);
            else if (b->shape == SHAPE_BOX)                     collide_box_circle(b, a);
            else                                                collide_circle_circle(a, b);
        }
    }
}

// ---------------------------------------------------------------------
// Solver
// ---------------------------------------------------------------------
static inline void rel_velocity(const contact_t *c, float *rvx, float *rvy) {
    const obj_t *a = c->a, *b = c->b;
    *rvx = (b->vx - b->omega * c->rby) - (a->vx - a->omega * c->ray);
    *rvy = (b->vy + b->omega * c->rbx) - (a->vy + a->omega * c->rax);
}

static inline void apply_impulse(contact_t *c, float px, float py) {
    obj_t *a = c->a, *b = c->b;
    a->vx -= c->ima * px;
    a->vy -= c->ima * py;
    a->omega -= c->iia * (c->rax * py - c->ray * px);
    b->vx += c->imb * px;
    b->vy += c->imb * py;
    b->omega += c->iib * (c->rbx * py - c->rby * px);
}

// Work out effective masses, bounce and impact damage for each contact.
static void prepare_contacts(world_t *w) {
    for (int i = 0; i < n_contacts; i++) {
        contact_t *c = &contacts[i];
        float nx = c->nx, ny = c->ny, tx = -ny, ty = nx;

        float rna = c->rax * ny - c->ray * nx, rnb = c->rbx * ny - c->rby * nx;
        float rta = c->rax * ty - c->ray * tx, rtb = c->rbx * ty - c->rby * tx;
        float kn = c->ima + c->imb + c->iia * rna * rna + c->iib * rnb * rnb;
        float kt = c->ima + c->imb + c->iia * rta * rta + c->iib * rtb * rtb;
        c->mass_n = 1.0f / kn;
        c->mass_t = 1.0f / kt;
        c->mu = sqrtf(MATS[c->a->mat].friction * MATS[c->b->mat].friction);

        float rvx, rvy;
        rel_velocity(c, &rvx, &rvy);
        float impact = -(rvx * nx + rvy * ny);      // > 0 means closing

        c->bounce = 0.0f;
        if (impact > BOUNCE_MIN_VN) {
            float ea = MATS[c->a->mat].restitution, eb = MATS[c->b->mat].restitution;
            c->bounce = (ea > eb ? ea : eb) * impact;
        }

        if (impact > DAMAGE_MIN_VN) {
            float dmg = (impact - DAMAGE_MIN_VN) * c->mass_n * DAMAGE_SCALE;
            // The bird hits at full strength. Tumbling rubble is much gentler on
            // blocks (so wreckage piles up instead of vanishing) but still squashes pigs.
            bool bird_hit = c->a->mat == MAT_BIRD || c->b->mat == MAT_BIRD;
            float da = dmg, db = dmg;
            if (!bird_hit) {
                da *= c->a->mat == MAT_PIG ? CRUSH_DAMAGE : RUBBLE_DAMAGE;
                db *= c->b->mat == MAT_PIG ? CRUSH_DAMAGE : RUBBLE_DAMAGE;
            }
            if (da > c->a->pending_damage) c->a->pending_damage = da;
            if (db > c->b->pending_damage) c->b->pending_damage = db;
            if (dmg > HIT_EVENT_MIN) {
                event_t *e = push_event(w, EV_HIT);
                if (e) {
                    e->mat = c->b->mat; e->other = c->a->mat;
                    e->x = c->b->x + c->rbx; e->y = c->b->y + c->rby;
                    e->vx = -nx * impact; e->vy = -ny * impact;
                    e->strength = dmg;
                }
            }
        }
    }
}

static void solve_contacts(void) {
    for (int it = 0; it < SOLVER_ITERS; it++) {
        for (int i = 0; i < n_contacts; i++) {
            contact_t *c = &contacts[i];
            float nx = c->nx, ny = c->ny, tx = -ny, ty = nx;
            float rvx, rvy;

            // normal: stop the bodies closing (plus the bounce)
            rel_velocity(c, &rvx, &rvy);
            float vn = rvx * nx + rvy * ny;
            float dj = c->mass_n * (c->bounce - vn);
            float j0 = c->jn;
            c->jn = j0 + dj > 0.0f ? j0 + dj : 0.0f;   // can push, never pull
            dj = c->jn - j0;
            apply_impulse(c, nx * dj, ny * dj);

            // tangent: friction, limited by how hard the bodies press together
            rel_velocity(c, &rvx, &rvy);
            float vt = rvx * tx + rvy * ty;
            float dt = c->mass_t * (-vt);
            float t0 = c->jt, lim = c->mu * c->jn;
            float t1 = t0 + dt;
            if (t1 >  lim) t1 =  lim;
            if (t1 < -lim) t1 = -lim;
            c->jt = t1;
            dt = t1 - t0;
            apply_impulse(c, tx * dt, ty * dt);
        }
    }
}

static void push_apart(void) {
    for (int i = 0; i < n_contacts; i++) {
        contact_t *c = &contacts[i];
        float extra = c->pen - POS_SLOP;
        if (extra <= 0.0f) continue;
        float corr = POS_CORRECT * extra / (c->ima + c->imb);
        c->a->x -= c->nx * corr * c->ima;
        c->a->y -= c->ny * corr * c->ima;
        c->b->x += c->nx * corr * c->imb;
        c->b->y += c->ny * corr * c->imb;
    }
}

// ---------------------------------------------------------------------
// Step
// ---------------------------------------------------------------------
void world_step(world_t *w, float dt) {
    const float h = dt / SUBSTEPS;
    const float lin_keep = 1.0f - LIN_DAMP * h;
    const float ang_keep = 1.0f - ANG_DAMP * h;
    const float roll_keep = 1.0f - ROLL_DAMP * h;

    for (int s = 0; s < SUBSTEPS; s++) {
        // forces
        for (int i = 0; i < w->count; i++) {
            obj_t *o = &w->objs[i];
            if (!o->alive || !o->awake) continue;
            o->vy += GRAVITY * h;
            o->vx *= lin_keep; o->vy *= lin_keep;
            o->omega *= ang_keep;
            if (o->shape == SHAPE_CIRCLE && o->resting) {
                o->omega *= roll_keep;
                float brake = ROLL_BRAKE * h;
                if (fabsf(o->omega) <= brake) o->omega = 0.0f;
                else o->omega -= copysignf(brake, o->omega);
            }
            o->resting = false;
        }

        find_contacts(w);
        prepare_contacts(w);

        // remember how fast each bird was going before the contacts slow it down
        for (int i = 0; i < w->count; i++) {
            if (w->objs[i].mat == MAT_BIRD) { pre_vx[i] = w->objs[i].vx; pre_vy[i] = w->objs[i].vy; }
        }
        solve_contacts();

        // move
        for (int i = 0; i < w->count; i++) {
            obj_t *o = &w->objs[i];
            if (!o->alive || !o->awake) continue;
            o->x += o->vx * h;
            o->y += o->vy * h;
            o->angle += o->omega * h;
        }
        push_apart();
        for (int i = 0; i < w->count; i++) {
            obj_t *o = &w->objs[i];
            if (!o->alive || !o->awake) continue;
            if (o->angle >  TWO_PI) o->angle -= TWO_PI;
            if (o->angle < -TWO_PI) o->angle += TWO_PI;
            o->co = cosf(o->angle);
            o->si = sinf(o->angle);
        }

        // damage
        for (int i = 0; i < w->count; i++) {
            obj_t *o = &w->objs[i];
            if (!o->alive || o->pending_damage <= 0.0f) continue;
            if (o->mat != MAT_BIRD) {
                o->health -= o->pending_damage;
                if (o->health <= 0.0f) destroy(w, o);
            }
            o->pending_damage = 0.0f;
        }

        // smash-through: a bird that just broke what it hit keeps most of its speed
        // instead of stopping dead against something that is no longer there
        for (int i = 0; i < n_contacts; i++) {
            contact_t *c = &contacts[i];
            obj_t *bird = c->a->mat == MAT_BIRD ? c->a : c->b->mat == MAT_BIRD ? c->b : 0;
            obj_t *other = bird == c->a ? c->b : c->a;
            if (!bird || other == &ground || other->alive) continue;
            int k = (int)(bird - w->objs);
            bird->vx = pre_vx[k] * SMASH_KEEP + bird->vx * (1.0f - SMASH_KEEP);
            bird->vy = pre_vy[k] * SMASH_KEEP + bird->vy * (1.0f - SMASH_KEEP);
            pre_vx[k] = bird->vx; pre_vy[k] = bird->vy;      // only once per sub-step
        }
    }

    // once per frame: sleeping, and removing things that left the screen
    for (int i = 0; i < w->count; i++) {
        obj_t *o = &w->objs[i];
        if (!o->alive) continue;
        if (o->x < -OFFSCREEN_PAD || o->x > SCREEN_W + OFFSCREEN_PAD) {
            if (o->mat == MAT_PIG) destroy(w, o);   // knocked off screen counts
            else o->alive = false;
            continue;
        }
        if (!o->awake || o->mat == MAT_BIRD) continue;
        bool still = o->vx * o->vx + o->vy * o->vy < SLEEP_SPEED * SLEEP_SPEED &&
                     fabsf(o->omega) < SLEEP_SPIN;
        if (still) {
            if (++o->still_frames >= SLEEP_FRAMES) {
                o->awake = false;
                o->vx = o->vy = o->omega = 0.0f;
            }
        } else {
            o->still_frames = 0;
        }
    }
}
