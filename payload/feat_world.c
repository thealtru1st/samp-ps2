/* Objects, pickups, checkpoints, map icons, labels and world state. Owned by one work
 * stream: see feat.h. This file holds the world state; the rest is in feat_world_*.c.
 *
 * feat_world_init runs last in feat_init, so a slot another stream filled first is
 * chained rather than replaced. Combat owns time, weather and interior as commands;
 * this file adds what they lack: a clock that stays where the server put it, gravity
 * on every site and the game-level init flags. */
#include "feat_world_int.h"
#include "bitstream.h"
#include "rak.h"
#include "samp.h"
#include "util.h"

#define GRAVITY_DEFAULT 0.008f
#define GRAVITY_MAX     1.0f
#define LAST_WEATHER    22    /* later ids index off the timecyc tables */
#define LAST_EXPLOSION  12
#define NO_BOUNDS       20000.0f
#define AREA_MAX        256

#define REMOVE_MAX      1024  /* the PC client's limit per player */
#define REMOVE_ANY_MAX  64
#define REMOVE_PER_TICK 800   /* of 12000 buildings: a full pass takes 15 frames */
#define MODEL_BITS      15360

typedef struct { int model; float pos[3], radius2; } removal_t;

static struct {
    unsigned hour, minute;
    int hold_clock, clock_shown;
    float bounds[4];  /* max x, min x, max y, min y */
    int has_bounds;
    int started, area;
} world;

static removal_t removals[REMOVE_MAX];
static unsigned removal_count, removal_any;
static unsigned char removed_model[MODEL_BITS / 8];

static void (*next_init)(const srpc_init_game_t *);
static void (*next_restart)(void);
static void (*next_time)(const srpc_time_t *);
static void (*next_world_time)(const srpc_id_t *);
static void (*next_weather)(const srpc_id_t *);
static void (*next_gravity)(const srpc_float_t *);
static void (*next_bounds)(const srpc_bounds_t *);
static void (*next_interior)(const srpc_id_t *);

/* ---- helpers shared by the world files ----------------------------------- */

int world_float_ok(float v)
{
    union { float f; unsigned u; } x;
    x.f = v;
    return (x.u & 0x7F800000u) != 0x7F800000u;
}

int world_pos_ok(const float *p)
{
    int i;
    for (i = 0; i < 3; i++)
        if (!world_float_ok(p[i]) || p[i] < -WORLD_LIMIT || p[i] > WORLD_LIMIT)
            return 0;
    return 1;
}

float world_dist2(const float *a, const float *b)
{
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return x * x + y * y + z * z;
}

int world_player_char(unsigned id)
{
    if (id == samp_stats.player_id)
        return wg_local_char();
    return game_remote_handle ? game_remote_handle(id) : 0;
}

void world_send(int rpc, const void *data, int bits) { rak_rpc(rpc, data, bits, RAK_RELIABLE, 0); }

/* ---- time and weather ----------------------------------------------------- */

static void hold_time(unsigned hour, unsigned minute)
{
    if (hour > 23 || minute > 59)
        return;
    world.hour = hour;
    world.minute = minute;
}

static void on_time(const srpc_time_t *t)
{
    hold_time(t->hour, t->minute);
    if (next_time)
        next_time(t);
    else if (t->hour < 24 && t->minute < 60)
        wg_set_time(t->hour, t->minute);
}

static void on_world_time(const srpc_id_t *t)
{
    hold_time(t->id, 0);
    if (next_world_time)
        next_world_time(t);
    else if (t->id < 24)
        wg_set_time(t->id, 0);
}

static void on_toggle_clock(const srpc_int_t *v)
{
    world.hold_clock = !v->v;
    world.clock_shown = v->v != 0;
    wg_clock_visible(world.clock_shown);
}

static void on_weather(const srpc_id_t *w)
{
    if (next_weather)
        next_weather(w);
    else if (w->id <= LAST_WEATHER)
        wg_set_weather(w->id);
}

/* ---- interior: combat does this when it is on ------------------------------ */

static void on_interior(const srpc_id_t *v)
{
    if (next_interior)
        next_interior(v);
    else if (v->id < AREA_MAX) {
        wg_set_area(v->id);
        world.area = (int)v->id; /* the server said so, no need to report it back */
    }
}

static void report_area(void)
{
    static unsigned char buf[4];
    int area;
    bs_t b;

    if (next_interior)
        return;
    area = wg_get_area();
    if (area == world.area)
        return;
    world.area = area;
    bs_wr_init(&b, buf, sizeof buf);
    srpc_enc_interior_change(&b, (unsigned)area);
    if (!b.err)
        world_send(SRPC_C2S_INTERIOR_CHANGE, buf, b.bits);
}

/* ---- gravity -------------------------------------------------------------- */

static void apply_gravity(float g)
{
    if (world_float_ok(g) && g > -GRAVITY_MAX && g < GRAVITY_MAX)
        wg_gravity(g);
}

static void on_gravity(const srpc_float_t *g)
{
    if (next_gravity)
        next_gravity(g);
    apply_gravity(g->v);
}

/* ---- sounds and explosions ------------------------------------------------ */

static void on_sound(const srpc_sound_t *s)
{
    float here[3];
    /* The PC scripts use 0,0,0 for "wherever the player is". */
    if (s->pos[0] == 0.0f && s->pos[1] == 0.0f && s->pos[2] == 0.0f) {
        if (wg_local_pos(here))
            wg_sound(s->sound, here);
    } else if (world_pos_ok(s->pos)) {
        wg_sound(s->sound, s->pos);
    }
}

static void on_explosion(const srpc_explosion_t *e)
{
    if (e->type <= LAST_EXPLOSION && world_pos_ok(e->pos))
        wg_explosion(e->pos, e->type);
}

/* Needs HTTP, an MP3 decoder and a way into audio the IOP owns: none of it exists here. */
static void on_audio(const srpc_audio_t *a)
{
    (void)a;
    slog("world: audio stream refused, not possible on the EE");
}

/* ---- world bounds --------------------------------------------------------- */

static void on_bounds(const srpc_bounds_t *b)
{
    if (next_bounds)
        next_bounds(b);
    if (!world_float_ok(b->max_x) || !world_float_ok(b->min_x) ||
        !world_float_ok(b->max_y) || !world_float_ok(b->min_y) ||
        b->min_x >= b->max_x || b->min_y >= b->max_y)
        return;
    world.bounds[0] = b->max_x;
    world.bounds[1] = b->min_x;
    world.bounds[2] = b->max_y;
    world.bounds[3] = b->min_y;
    world.has_bounds = b->max_x < NO_BOUNDS || b->min_x > -NO_BOUNDS ||
                       b->max_y < NO_BOUNDS || b->min_y > -NO_BOUNDS;
}

static void keep_in_bounds(void)
{
    if (world.has_bounds)
        wg_keep_inside(world.bounds);
}

/* ---- RemoveBuilding ------------------------------------------------------- */

static int removal_hit(const removal_t *r, const float *pos) { return world_dist2(r->pos, pos) <= r->radius2; }

static int should_hide(int model, const float *pos)
{
    unsigned i;

    if (model >= 0 && model < MODEL_BITS && (removed_model[model >> 3] >> (model & 7)) & 1) {
        for (i = 0; i < removal_count; i++)
            if (removals[i].model == model && removal_hit(&removals[i], pos))
                return 1;
    }
    if (removal_any)
        for (i = 0; i < removal_count; i++)
            if (removals[i].model < 0 && removal_hit(&removals[i], pos))
                return 1;
    return 0;
}

static void on_remove_building(const srpc_remove_building_t *r)
{
    removal_t *n;
    int model = (int)r->model;

    if (removal_count == REMOVE_MAX || !world_pos_ok(r->pos) || !world_float_ok(r->radius) ||
        r->radius < 0.0f || r->radius > WORLD_LIMIT)
        return;
    if (model != -1) {
        model = wg_model_map(model);
        if (model < 0 || model >= MODEL_BITS)
            return; /* not on the disc, so nothing here to remove */
        removed_model[model >> 3] |= 1 << (model & 7);
    } else if (removal_any == REMOVE_ANY_MAX) {
        return;
    } else {
        removal_any++;
    }
    n = &removals[removal_count++];
    n->model = model;
    n->pos[0] = r->pos[0];
    n->pos[1] = r->pos[1];
    n->pos[2] = r->pos[2];
    n->radius2 = r->radius * r->radius;
}

/* ---- connection ----------------------------------------------------------- */

static void on_init_game(const srpc_init_game_t *g)
{
    if (next_init)
        next_init(g);
    else {
        if (g->hour < 24)
            wg_set_time(g->hour, 0);
        if (g->weather <= LAST_WEATHER)
            wg_set_weather(g->weather);
    }
    hold_time(g->hour, 0);
    apply_gravity(g->gravity);
    wg_entry_exits(!g->no_enex);
    wg_zone_names(g->zone_names);
}

void feat_world_reset(void)
{
    wobj_reset();
    wpick_reset();
    wradar_reset();
    wlabel_reset();
    wactor_reset();
    if (removal_count)
        wg_show_all(should_hide);
    removal_count = removal_any = 0;
    memset(removed_model, 0, sizeof removed_model);
    wg_gravity(GRAVITY_DEFAULT);
    wg_clock_visible(0);
    world.hold_clock = 1;
    world.clock_shown = 0;
    world.has_bounds = 0;
}

static void on_restart(void)
{
    if (next_restart)
        next_restart();
    feat_world_reset();
}

void feat_world_init(samp_rpc_handlers_t *h)
{
    next_init = h->init_game;
    next_restart = h->game_mode_restart;
    next_time = h->set_time;
    next_world_time = h->set_world_time;
    next_weather = h->set_weather;
    next_gravity = h->set_gravity;
    next_bounds = h->set_world_bounds;
    next_interior = h->set_interior;

    h->init_game = on_init_game;
    h->game_mode_restart = on_restart;
    h->set_time = on_time;
    h->set_world_time = on_world_time;
    h->toggle_clock = on_toggle_clock;
    h->set_weather = on_weather;
    h->set_gravity = on_gravity;
    h->set_world_bounds = on_bounds;
    h->set_interior = on_interior;
    h->play_sound = on_sound;
    h->create_explosion = on_explosion;
    h->play_audio_stream = on_audio;
    h->remove_building = on_remove_building;

    wobj_init(h);
    wpick_init(h);
    wradar_init(h);
    wlabel_init(h);
    wactor_init(h);

    world.hour = 12;
    world.hold_clock = 1;
    wg_kill_help();
    wg_clock_visible(0);
}

void feat_world_tick(unsigned now)
{
    int state = samp_state();

    /* A new session with leftovers, for builds that do not call feat_world_reset. */
    if (state == SAMP_CONNECTING) {
        if (world.started) {
            world.started = 0;
            feat_world_reset();
        }
        return;
    }
    if (state != SAMP_SPAWNED && state != SAMP_SPAWNING && state != SAMP_CLASS)
        return;
    world.started = 1;
    if (world.hold_clock)
        wg_clock_hold(world.hour, world.minute);
    keep_in_bounds();
    report_area();
    if (removal_count)
        wg_scan_entities(REMOVE_PER_TICK, should_hide);
    wobj_tick(now);
    wpick_tick(now);
    wradar_tick(now);
    wlabel_tick(now);
    wactor_tick(now);
}

void feat_world_draw(void)
{
    wradar_draw();
    wlabel_draw();
}
