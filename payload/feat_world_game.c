/* The game side of the world stream. Addresses are from RE of SLES-52927 v1.00; the
 * evidence for each is in wip/world/re-world.md. */
#include "feat_world_game.h"
#include "feat_world_models.h"
#include "scm.h"
#include "ui_game.h"
#include "util.h"

#define U8(a)  (*(volatile unsigned char *)(a))
#define U32(a) (*(volatile unsigned *)(a))
#define F32(a) (*(volatile float *)(a))

/* Pools: words holding a CPool*, whose fields are objects, flags, size, top. */
#define PED_POOL      0x0066B998u
#define CAR_POOL      0x0066B99Cu
#define BUILDING_POOL 0x0066B9A0u
#define OBJECT_POOL   0x0066B9A4u
#define DUMMY_POOL    0x0066B9A8u
#define ENTITY_STRIDE 0x38u

#define MODEL_INFO    0x007A8800u /* CModelInfo pointers */
#define MODEL_COUNT   15080
#define MODEL_STATE   0x007C3CF6u /* stride 12, low three bits: 1 loaded */
#define MODELS_QUEUED 0x0066BE88u
#define STREAM_BACKLOG 12u

#define ENT_PLACE     0x04
#define ENT_MATRIX    0x14
#define ENT_FLAGS     0x1C /* bit 0 collision, bit 7 visible */
#define ENT_MODEL     0x22

#define PLAYERS       0x00709650u
#define CAMERA_MATRIX 0x006FE5C4u
#define CAMERA_PLACE  0x006FE5B4u

#define PICKUP_RING   0x008081B0u /* 20 handles, collected and not yet asked about */
#define PICKUP_RING_N 20

#define CLOCK_HOUR    0x0066B82Cu
#define CLOCK_MINUTE  0x0066B830u
#define CLOCK_FORMAT  0x005FE9E0u /* the HUD clock's "%02d:%02d" */

#define HUD_OFF       0x0066BFF8u
#define CURRENT_AREA  0x0066B8E8u
#define HELP_SET      0x002A9200u /* the three setters of the help box */
#define HELP_SET_NUM  0x002A93C0u
#define HELP_SET_STAT 0x002A9550u
#define HELP_TEXT     0x007C3010u
#define HELP_LAST     0x007C3110u
#define HELP_PRINT    0x007C3210u
#define OP_JR_RA      0x03E00008u

#define FN_CALC_SCREEN 0x0011D0E0u /* (in, out, *w, *h, far check, near check) */
#define FN_RADAR_AREA  0x0026E5A0u /* (rect, rgba, in menu) */

#define OP_CREATE_OBJECT  0x029B
#define OP_DELETE_OBJECT  0x0108
#define OP_OBJECT_COORDS  0x01BC
#define OP_OBJECT_ROT     0x0453
#define OP_OBJECT_AREA    0x0566
#define OP_OBJECT_DYNAMIC 0x0392
#define OP_ATTACH_CAR     0x0681
#define OP_ATTACH_OBJECT  0x069A
#define OP_ATTACH_CHAR    0x069B
#define OP_OBJECT_COLLISION 0x0382
#define OP_GET_CAR_COORDS 0x00AA
#define OP_REQUEST_MODEL  0x0247
#define OP_RELEASE_MODEL  0x0249
#define OP_CREATE_PICKUP  0x0213
#define OP_REMOVE_PICKUP  0x0215
#define OP_BLIP_COORD     0x018A
#define OP_BLIP_COLOUR    0x0165
#define OP_BLIP_SCALE     0x0168
#define OP_BLIP_REMOVE    0x0164
#define OP_LOCATE_CHAR    0x00FE
#define OP_CP_CREATE      0x06D5
#define OP_CP_DELETE      0x06D6
#define OP_SET_TIME       0x00C0
#define OP_SET_WEATHER    0x01B6
#define OP_SOUND          0x018C
#define OP_EXPLOSION      0x0948
#define OP_ENTRY_EXITS    0x08E7
#define OP_ZONE_NAMES     0x09BA
#define OP_CHAR_COORDS    0x00A1
#define OP_AREA_VISIBLE   0x04BB
#define OP_CHAR_AREA      0x0860
#define OP_LINE_OF_SIGHT  0x06BD
#define OP_CREATE_CHAR    0x009A
#define OP_DELETE_CHAR    0x009B
#define OP_CHAR_HEADING   0x0173
#define OP_CHAR_HEALTH    0x0223
#define OP_GET_HEALTH     0x0226
#define OP_CHAR_PROOFS    0x02AB
#define OP_FREEZE_CHAR    0x04D7
#define OP_HIT_BY_CHAR    0x051A
#define OP_CLEAR_DAMAGE   0x0467
#define OP_GET_WEAPON     0x0470
#define OP_CLEAR_TASKS    0x0792

#define PED_TYPE_CIV  4
#define PED_LIFT      1.0f
#define AREA_ANY      13     /* an entity in area 13 shows in every interior */
#define GROUND_Z      (-99.0f) /* the opcodes treat anything at -100 or lower as "find the ground" */

#define L_CHAR  1            /* script locals: 0 and 1 are set up by game_boot */
#define L_OUT   12
#define L_FLAGS 34           /* the word at thread+0xC4; the compare flag is its byte +0xC5 */
#define L_EXTERNAL 35        /* the word at thread+0xC8. Byte +0xC9 is the script's external */
#define EXTERNAL_NONE 0xFF00 /* type: -1 gives a plain mission object, 0 a brain's object */

/* 0.008 is an immediate in 24 places and a literal in 9 more. The PC game shares one
 * literal between them, so SetGravity changes all of these there too. */
static const struct { unsigned lui, ori; } gravity_code[] = {
    {0x149090, 0x149098}, {0x149AF8, 0x149B00}, {0x149FE8, 0x149FF0}, {0x14A050, 0x14A058},
    {0x160530, 0x160534}, {0x16195C, 0x161964}, {0x162FF8, 0x163000}, {0x166120, 0x166124},
    {0x1B665C, 0x1B6664}, {0x23278C, 0x232790}, {0x24E374, 0x24E378}, {0x253D84, 0x253D8C},
    {0x2548D0, 0x2548D8}, {0x2549D0, 0x2549D8}, {0x2574B0, 0x2574B8}, {0x2575E0, 0x2575E8},
    {0x25CA18, 0x25CA20}, {0x2834B0, 0x2834B8}, {0x324BC8, 0x324BD0}, {0x3262D0, 0x3262D4},
    {0x328618, 0x32861C}, {0x328778, 0x32877C}, {0x3DC198, 0x3DC19C}, {0x4A25B0, 0x4A25B4},
};
static const unsigned gravity_data[] = {
    0x663380, 0x663688, 0x663710, 0x663744, 0x66376C, 0x663794, 0x6638D0, 0x6639D0, 0x663C1C,
};

float wg_sqrt(float v)
{
    float r;
    if (v <= 0.0f)
        return 0.0f;
    __asm__("sqrt.s %0, %1" : "=f"(r) : "f"(v));
    return r;
}

static int compare_flag(void) { return (scm_get_local(L_FLAGS) >> 8) & 1; }

static int pool_alive(unsigned var, int h)
{
    unsigned pool = U32(var), i;
    if (!pool || h <= 0 || (h & 0x80))
        return 0;
    i = (unsigned)h >> 8;
    return i < U32(pool + 8) && U8(U32(pool + 4) + i) == (unsigned)(h & 0xFF);
}

static int pool_free(unsigned var)
{
    unsigned pool = U32(var), i, n;
    int count = 0;
    if (!pool)
        return 0;
    n = U32(pool + 8);
    for (i = 0; i < n; i++)
        if (U8(U32(pool + 4) + i) & 0x80)
            count++;
    return count;
}

int wg_object_alive(int h) { return pool_alive(OBJECT_POOL, h); }
int wg_car_alive(int h)    { return pool_alive(CAR_POOL, h); }
int wg_char_alive(int h)   { return pool_alive(PED_POOL, h); }
int wg_object_slots(void)  { return pool_free(OBJECT_POOL); }
int wg_ped_slots(void)     { return pool_free(PED_POOL); }

static int model_exists(int m) { return m >= 0 && m < MODEL_COUNT && U32(MODEL_INFO + m * 4) != 0; }

int wg_model_map(int pc)
{
    int m = world_model_lookup(pc);
    return model_exists(m) ? m : -1;
}

int wg_ped_model_ok(int m) { return m > 0 && m < 300 && model_exists(m); }
int wg_model_loaded(int m) { return (U8(MODEL_STATE + m * 12) & 7) == 1; }

static void op_i(unsigned op, int v)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, op); scm_i32(&s, v);
    scm_run(&s);
}

static void op_ii(unsigned op, int a, int b)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, op); scm_i32(&s, a); scm_i32(&s, b);
    scm_run(&s);
}

static void op_if3(unsigned op, int a, const float *v)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, op); scm_i32(&s, a);
    scm_f(&s, v[0]); scm_f(&s, v[1]); scm_f(&s, v[2]);
    scm_run(&s);
}

void wg_model_request(int m) { op_i(OP_REQUEST_MODEL, m); }
void wg_model_release(int m) { op_i(OP_RELEASE_MODEL, m); }

int game_stream_over(void);   /* game.c: model budget or heap nearly used up */
int wg_stream_room(void)
{
    /* Only the queue is worth limiting. The game keeps streaming memory nearly full on
     * purpose (13458 of 13550 KB measured in play) and evicts by itself to make room,
     * so waiting for free memory would mean never loading anything. */
    /* Past the limit everything resident is in use (see game_stream_over in game.c). */
    return U32(MODELS_QUEUED) < STREAM_BACKLOG && !game_stream_over();
}

int wg_local_char(void) { return scm_get_local(L_CHAR); }

static void entity_pos(unsigned e, float *p)
{
    unsigned m = U32(e + ENT_MATRIX), src = m ? m + 0x30 : e + ENT_PLACE;
    p[0] = F32(src);
    p[1] = F32(src + 4);
    p[2] = F32(src + 8);
}

int wg_local_pos(float *p)
{
    unsigned ped = U32(PLAYERS);
    if (!ped)
        return 0;
    entity_pos(ped, p);
    return 1;
}

int wg_camera_pos(float *p)
{
    unsigned m = U32(CAMERA_MATRIX), src = m ? m + 0x30 : CAMERA_PLACE;
    p[0] = F32(src);
    p[1] = F32(src + 4);
    p[2] = F32(src + 8);
    return 1;
}

int wg_menu_open(void) { return U8(MENU_ACTIVE) != 0; }

/* ---- objects ---- */

int wg_object_create(int model, const float *p, const float *r)
{
    scm_t s;
    int h;
    float z = p[2] < GROUND_Z ? GROUND_Z : p[2];

    scm_set_local(L_OUT, 0);
    scm_set_local(L_EXTERNAL, EXTERNAL_NONE);
    scm_begin(&s);
    scm_op(&s, OP_CREATE_OBJECT); scm_i32(&s, model);
    scm_f(&s, p[0]); scm_f(&s, p[1]); scm_f(&s, z); scm_local(&s, L_OUT);
    scm_run(&s);
    scm_set_local(L_EXTERNAL, 0);
    h = scm_get_local(L_OUT);
    if (!wg_object_alive(h))
        return 0;
    scm_begin(&s);
    scm_op(&s, OP_OBJECT_ROT); scm_i32(&s, h); scm_f(&s, r[0]); scm_f(&s, r[1]); scm_f(&s, r[2]);
    scm_op(&s, OP_OBJECT_AREA); scm_i32(&s, h); scm_i32(&s, AREA_ANY);
    scm_op(&s, OP_OBJECT_DYNAMIC); scm_i32(&s, h); scm_i8(&s, 0);
    scm_run(&s);
    return h;
}

void wg_object_delete(int h) { if (wg_object_alive(h)) op_i(OP_DELETE_OBJECT, h); }

void wg_object_move(int h, const float *p)
{
    float v[3];
    if (!wg_object_alive(h))
        return;
    v[0] = p[0];
    v[1] = p[1];
    v[2] = p[2] < GROUND_Z ? GROUND_Z : p[2];
    op_if3(OP_OBJECT_COORDS, h, v);
}

void wg_object_rotate(int h, const float *r) { if (wg_object_alive(h)) op_if3(OP_OBJECT_ROT, h, r); }

static void attach(unsigned op, int h, int to, const float *o, const float *r)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, op); scm_i32(&s, h); scm_i32(&s, to);
    scm_f(&s, o[0]); scm_f(&s, o[1]); scm_f(&s, o[2]);
    scm_f(&s, r[0]); scm_f(&s, r[1]); scm_f(&s, r[2]);
    scm_run(&s);
}

void wg_object_attach_car(int h, int to, const float *o, const float *r)
{
    if (wg_object_alive(h) && wg_car_alive(to))
        attach(OP_ATTACH_CAR, h, to, o, r);
}

void wg_object_attach_object(int h, int to, const float *o, const float *r)
{
    if (wg_object_alive(h) && wg_object_alive(to))
        attach(OP_ATTACH_OBJECT, h, to, o, r);
}

void wg_object_attach_char(int h, int to, const float *o, const float *r)
{
    if (wg_object_alive(h) && wg_char_alive(to)) {
        scm_t s;
        attach(OP_ATTACH_CHAR, h, to, o, r);
        /* Solid, it would push the camera right up against the player. */
        scm_begin(&s);
        scm_op(&s, OP_OBJECT_COLLISION); scm_i32(&s, h); scm_i8(&s, 0);
        scm_run(&s);
    }
}

int wg_car_pos(int h, float *p)
{
    scm_t s;
    union { int i; float f; } v;
    int i;

    if (!wg_car_alive(h))
        return 0;
    scm_begin(&s);
    scm_op(&s, OP_GET_CAR_COORDS); scm_i32(&s, h);
    scm_local(&s, L_OUT); scm_local(&s, L_OUT + 1); scm_local(&s, L_OUT + 2);
    scm_run(&s);
    for (i = 0; i < 3; i++) {
        v.i = scm_get_local(L_OUT + i);
        p[i] = v.f;
    }
    return 1;
}

/* ---- pickups ---- */

int wg_pickup_create(int model, int type, const float *p)
{
    scm_t s;
    scm_set_local(L_OUT, -1);
    scm_begin(&s);
    scm_op(&s, OP_CREATE_PICKUP); scm_i32(&s, model); scm_i32(&s, type);
    scm_f(&s, p[0]); scm_f(&s, p[1]); scm_f(&s, p[2]); scm_local(&s, L_OUT);
    scm_run(&s);
    return scm_get_local(L_OUT);
}

void wg_pickup_delete(int h) { op_i(OP_REMOVE_PICKUP, h); }

/* Polling 0214 per pickup would cost one script run each; the game's own ring holds the
 * same answer. */
int wg_pickup_collected(void)
{
    int i;
    for (i = 0; i < PICKUP_RING_N; i++) {
        unsigned a = PICKUP_RING + i * 4, h = U32(a);
        if (h) {
            U32(a) = 0;
            return (int)h;
        }
    }
    return 0;
}

/* ---- radar ---- */

static int blip_op(unsigned op, const float *p, int sprite)
{
    scm_t s;
    scm_set_local(L_OUT, -1);
    scm_begin(&s);
    scm_op(&s, op);
    scm_f(&s, p[0]); scm_f(&s, p[1]); scm_f(&s, p[2]);
    if (sprite >= 0)
        scm_i32(&s, sprite);
    scm_local(&s, L_OUT);
    scm_run(&s);
    return scm_get_local(L_OUT);
}

int wg_blip_coord(const float *p) { return blip_op(OP_BLIP_COORD, p, -1); }

int wg_blip_sprite(int style, const float *p, int sprite)
{
    /* MAPICON_LOCAL, _GLOBAL, _LOCAL_CHECKPOINT, _GLOBAL_CHECKPOINT */
    static const unsigned short ops[4] = {0x04CE, 0x02A8, 0x0570, 0x02A7};
    return blip_op(ops[style & 3], p, sprite);
}

void wg_blip_colour(int b, unsigned c) { op_ii(OP_BLIP_COLOUR, b, (int)c); }
void wg_blip_scale(int b, int size)    { op_ii(OP_BLIP_SCALE, b, size); }
void wg_blip_delete(int b)             { op_i(OP_BLIP_REMOVE, b); }

/* The radar sets its scale and rotation in the HUD pass, which ran before the draw hook. */
void wg_radar_area(const float *rect, const unsigned char *rgba)
{
    if (U8(HUD_OFF))
        return;
    ((void (*)(const float *, const unsigned char *, int))FN_RADAR_AREA)(rect, rgba, 0);
}

/* ---- checkpoints ---- */

void wg_draw_sphere(const float *p, float size)
{
    scm_t s;
    int ped = wg_local_char();
    if (!wg_char_alive(ped))
        return;
    scm_begin(&s);
    scm_op(&s, OP_LOCATE_CHAR); scm_i32(&s, ped);
    scm_f(&s, p[0]); scm_f(&s, p[1]); scm_f(&s, p[2]);
    scm_f(&s, size); scm_f(&s, size); scm_f(&s, size); scm_i8(&s, 1);
    scm_run(&s);
}

int wg_race_cp_create(int type, const float *p, const float *n, float size)
{
    scm_t s;
    scm_set_local(L_OUT, -1);
    scm_begin(&s);
    scm_op(&s, OP_CP_CREATE); scm_i32(&s, type);
    scm_f(&s, p[0]); scm_f(&s, p[1]); scm_f(&s, p[2]);
    scm_f(&s, n[0]); scm_f(&s, n[1]); scm_f(&s, n[2]);
    scm_f(&s, size); scm_local(&s, L_OUT);
    scm_run(&s);
    return scm_get_local(L_OUT);
}

void wg_race_cp_delete(int h) { op_i(OP_CP_DELETE, h); }

/* ---- world ---- */

void wg_clock_hold(unsigned hour, unsigned minute)
{
    U8(CLOCK_HOUR) = hour;
    U8(CLOCK_MINUTE) = minute;
}

void wg_clock_visible(int on) { U8(CLOCK_FORMAT) = on ? '%' : 0; }

void wg_gravity(float g)
{
    union { float f; unsigned u; } v;
    unsigned i;

    v.f = g;
    for (i = 0; i < sizeof gravity_code / sizeof gravity_code[0]; i++) {
        unsigned lui = U32(gravity_code[i].lui), ori = U32(gravity_code[i].ori);
        U32(gravity_code[i].lui) = (lui & 0xFFFF0000u) | (v.u >> 16);
        U32(gravity_code[i].ori) = (ori & 0xFFFF0000u) | (v.u & 0xFFFF);
    }
    for (i = 0; i < sizeof gravity_data / sizeof gravity_data[0]; i++)
        U32(gravity_data[i]) = v.u;
}

void wg_entry_exits(int enabled)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_ENTRY_EXITS); scm_i8(&s, !enabled);
    scm_run(&s);
}

void wg_zone_names(int on)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_ZONE_NAMES); scm_i8(&s, on != 0);
    scm_run(&s);
}

void wg_set_time(unsigned hour, unsigned minute) { op_ii(OP_SET_TIME, hour, minute); }
void wg_set_weather(unsigned id) { op_i(OP_SET_WEATHER, id); }

void wg_sound(unsigned id, const float *p)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_SOUND);
    scm_f(&s, p[0]); scm_f(&s, p[1]); scm_f(&s, p[2]); scm_i32(&s, id);
    scm_run(&s);
}

void wg_explosion(const float *p, unsigned type)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_EXPLOSION);
    scm_f(&s, p[0]); scm_f(&s, p[1]); scm_f(&s, p[2]); scm_i32(&s, type); scm_f(&s, 1.0f);
    scm_run(&s);
}

/* All three setters of the help box return at once. This also wipes what game_boot's
 * clothes already queued: clothes give respect, which is the box people saw. */
void wg_kill_help(void)
{
    static const unsigned setters[3] = {HELP_SET, HELP_SET_NUM, HELP_SET_STAT};
    int i;

    for (i = 0; i < 3; i++) {
        U32(setters[i]) = OP_JR_RA;
        U32(setters[i] + 4) = 0;
    }
    U8(HELP_TEXT) = 0;
    U8(HELP_LAST) = 0;
    U8(HELP_PRINT) = 0;
}

/* b is max x, min x, max y, min y. Straight into the matrix: SET_CHAR_COORDS lifts the ped a
 * little each time, so he would hop along the edge. The speed is turned back, as on a wall. */
void wg_keep_inside(const float *b)
{
    unsigned ped = U32(PLAYERS), who, m;
    volatile float *pos, *speed;
    int i;

    if (!ped)
        return;
    who = (U8(ped + 0x4A5) & 1) && U32(ped + 0x5CC) ? U32(ped + 0x5CC) : ped;
    m = U32(who + 0x14);
    if (!m)
        return;
    pos = (volatile float *)(m + 0x30);
    speed = (volatile float *)(who + 0x44);
    for (i = 0; i < 2; i++) {
        float hi = b[i * 2], lo = b[i * 2 + 1];
        if (pos[i] > hi) {
            pos[i] = hi;
            if (speed[i] > 0.0f)
                speed[i] *= -0.5f;
        } else if (pos[i] < lo) {
            pos[i] = lo;
            if (speed[i] < 0.0f)
                speed[i] *= -0.5f;
        }
    }
}

void wg_move_local(const float *p)
{
    int ped = wg_local_char();
    if (wg_char_alive(ped))
        op_if3(OP_CHAR_COORDS, ped, p);
}

void wg_set_area(unsigned area)
{
    scm_t s;
    int ped = wg_local_char();

    scm_begin(&s);
    scm_op(&s, OP_AREA_VISIBLE); scm_i32(&s, area);
    if (wg_char_alive(ped)) {
        scm_op(&s, OP_CHAR_AREA); scm_i32(&s, ped); scm_i32(&s, area);
    }
    scm_run(&s);
}

int wg_get_area(void) { return (int)U32(CURRENT_AREA); }

int wg_line_clear(const float *a, const float *b)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_LINE_OF_SIGHT);
    scm_f(&s, a[0]); scm_f(&s, a[1]); scm_f(&s, a[2]);
    scm_f(&s, b[0]); scm_f(&s, b[1]); scm_f(&s, b[2]);
    scm_i8(&s, 1); scm_i8(&s, 0); scm_i8(&s, 0); scm_i8(&s, 1); scm_i8(&s, 0);
    scm_run(&s);
    return compare_flag();
}

/* Every streamed IPL creates its entities again, so hiding is a loop, not an event. */
static void hide(unsigned e) { U8(e + ENT_FLAGS) &= 0x7E; }
static void show(unsigned e) { U8(e + ENT_FLAGS) |= 0x81; }

static unsigned scan_pool(unsigned var, unsigned at, unsigned count, wg_entity_cb cb, void (*fn)(unsigned))
{
    unsigned pool = U32(var), n, i;

    if (!pool)
        return 0;
    n = U32(pool + 8);
    for (i = 0; i < count && i < n; i++, at++) {
        unsigned e;
        float p[3];
        if (at >= n)
            at = 0;
        if (U8(U32(pool + 4) + at) & 0x80)
            continue;
        e = U32(pool) + at * ENTITY_STRIDE;
        entity_pos(e, p);
        if (cb(*(volatile short *)(e + ENT_MODEL), p))
            fn(e);
    }
    return at;
}

void wg_scan_entities(unsigned count, wg_entity_cb cb)
{
    static unsigned building, dummy;
    building = scan_pool(BUILDING_POOL, building, count, cb, hide);
    dummy = scan_pool(DUMMY_POOL, dummy, count / 4, cb, hide);
}

void wg_show_all(wg_entity_cb cb)
{
    scan_pool(BUILDING_POOL, 0, 0xFFFFFFFFu, cb, show);
    scan_pool(DUMMY_POOL, 0, 0xFFFFFFFFu, cb, show);
}

/* ---- actors ---- */

int wg_actor_create(int skin, const float *p, float angle)
{
    scm_t s;
    int h;

    scm_set_local(L_OUT, 0);
    scm_begin(&s);
    scm_op(&s, OP_CREATE_CHAR); scm_i8(&s, PED_TYPE_CIV); scm_i32(&s, skin);
    /* The opcode adds a ped's height, and a frozen actor never comes back down from it. */
    scm_f(&s, p[0]); scm_f(&s, p[1]); scm_f(&s, p[2] - PED_LIFT); scm_local(&s, L_OUT);
    scm_run(&s);
    h = scm_get_local(L_OUT);
    if (!wg_char_alive(h))
        return 0;
    scm_begin(&s);
    scm_op(&s, OP_CHAR_HEADING); scm_i32(&s, h); scm_f(&s, angle);
    scm_op(&s, OP_FREEZE_CHAR); scm_i32(&s, h); scm_i8(&s, 1);
    scm_run(&s);
    return h;
}

void wg_actor_delete(int h) { if (wg_char_alive(h)) op_i(OP_DELETE_CHAR, h); }
void wg_actor_place(int h, const float *p)
{
    float q[3];

    q[0] = p[0];
    q[1] = p[1];
    q[2] = p[2] - PED_LIFT;
    if (wg_char_alive(h))
        op_if3(OP_CHAR_COORDS, h, q);
}

void wg_actor_angle(int h, float a)
{
    scm_t s;
    if (!wg_char_alive(h))
        return;
    scm_begin(&s);
    scm_op(&s, OP_CHAR_HEADING); scm_i32(&s, h); scm_f(&s, a);
    scm_run(&s);
}

void wg_actor_health(int h, float v)
{
    if (wg_char_alive(h))
        op_ii(OP_CHAR_HEALTH, h, (int)v);
}

void wg_actor_invulnerable(int h, int on)
{
    scm_t s;
    if (!wg_char_alive(h))
        return;
    scm_begin(&s);
    scm_op(&s, OP_CHAR_PROOFS); scm_i32(&s, h);
    scm_i8(&s, on); scm_i8(&s, on); scm_i8(&s, on); scm_i8(&s, on); scm_i8(&s, on);
    scm_run(&s);
}

float wg_actor_get_health(int h)
{
    scm_t s;
    if (!wg_char_alive(h))
        return 0.0f;
    scm_begin(&s);
    scm_op(&s, OP_GET_HEALTH); scm_i32(&s, h); scm_local(&s, L_OUT);
    scm_run(&s);
    return (float)scm_get_local(L_OUT);
}

int wg_actor_hit_by_local(int h)
{
    scm_t s;
    int ped = wg_local_char(), hit;

    if (!wg_char_alive(h) || !wg_char_alive(ped))
        return 0;
    scm_begin(&s);
    scm_op(&s, OP_HIT_BY_CHAR); scm_i32(&s, h); scm_i32(&s, ped);
    scm_run(&s);
    hit = compare_flag();
    if (hit)
        op_i(OP_CLEAR_DAMAGE, h);
    return hit;
}

int wg_local_weapon(void)
{
    scm_t s;
    int ped = wg_local_char();
    if (!wg_char_alive(ped))
        return 0;
    scm_begin(&s);
    scm_op(&s, OP_GET_WEAPON); scm_i32(&s, ped); scm_local(&s, L_OUT);
    scm_run(&s);
    return scm_get_local(L_OUT);
}

void wg_actor_clear_tasks(int h) { if (wg_char_alive(h)) op_i(OP_CLEAR_TASKS, h); }

/* ---- drawing ---- */

int wg_world_to_screen(const float *p, float *x, float *y)
{
    float out[3], w, h;
    int (*calc)(const float *, float *, float *, float *, int, int) =
        (int (*)(const float *, float *, float *, float *, int, int))FN_CALC_SCREEN;

    /* out is already in 640x448 space: the function multiplies by those literals. */
    if (!calc(p, out, &w, &h, 0, 1))
        return 0;
    *x = out[0];
    *y = out[1];
    return *x > -64.0f && *x < UI_SCREEN_W + 64.0f && *y > -32.0f && *y < UI_SCREEN_H + 32.0f;
}

float wg_text_width(const char *t) { return CFont_GetStringWidth(t, 1, 0); }

void wg_text_begin(void)
{
    static const ui_rgba black = {0, 0, 0, 255};
    CFont_SetFontStyle(UI_FONT_SUBTITLES);
    CFont_SetScale(0.38f, 0.76f);
    CFont_SetOrientation(UI_ALIGN_LEFT);
    CFont_SetProportional(1);
    CFont_SetJustify(0);
    CFont_SetSlant(0.0f);
    CFont_SetWrapx(UI_SCREEN_W + 256.0f);
    CFont_SetAlphaFade(255.0f);
    CFont_SetDropColor(&black);
    CFont_SetEdge(1);
    CFont_SetBackground(0, 0);
}

void wg_text_colour(unsigned c)
{
    ui_rgba v;
    v.r = c >> 24;
    v.g = c >> 16;
    v.b = c >> 8;
    v.a = c;
    CFont_SetColor(&v);
}

void wg_text_print(float x, float y, const char *t)
{
    char b[128];
    unsigned n = 0;

    while (t[n] && n < sizeof b - 1) {
        b[n] = t[n];
        n++;
    }
    /* The game draws nothing at all for a string that ends in a space. */
    while (n && b[n - 1] == ' ')
        n--;
    b[n] = 0;
    if (n)
        ui_PrintString(x, y, b);
}
void wg_text_end(void) { CFont_DrawFonts(); }
