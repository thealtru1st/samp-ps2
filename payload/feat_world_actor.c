/* Server actors. The server streams them by itself, so a record here is an actor it has
 * told us about. The game's ped pool holds only 74, shared with remote players, so actors
 * are capped and wait for room. Animations load in the background and start when ready. */
#include "feat_world_int.h"
#include "feat_combat_game.h"
#include "bitstream.h"
#include "util.h"

#define ID_MAX       1000
#define LIVE_MAX     16
#define PED_RESERVE  12
#define FALLBACK_SKIN 7   /* model 0 is the player's and cannot be a plain ped */
#define HIT_MS       250
#define NAME_MAX     SRPC_ANIM_MAX
#define BODY_TORSO   3

typedef struct {
    unsigned char used, invulnerable, requested, anim_wait;
    short skin;
    int handle;
    float pos[3], angle, health, seen_health;
    srpc_anim_t anim;
} wactor_t;

static wactor_t actors[ID_MAX];
static unsigned short used_ids[LIVE_MAX * 8];
static unsigned used_count, live_count, hit_at;

static wactor_t *get(unsigned id) { return id < ID_MAX && actors[id].used ? &actors[id] : 0; }

static void despawn(wactor_t *a)
{
    if (a->handle) {
        wg_actor_delete(a->handle);
        a->handle = 0;
        live_count--;
    }
    if (a->requested) {
        wg_model_release(a->skin);
        a->requested = 0;
    }
}

static void on_in(const srpc_actor_t *c)
{
    wactor_t *a;
    unsigned i;

    if (c->id >= ID_MAX || !world_pos_ok(c->pos) || !world_float_ok(c->angle) || !world_float_ok(c->health))
        return;
    a = &actors[c->id];
    if (a->used) {
        despawn(a);
    } else {
        if (used_count == sizeof used_ids / sizeof used_ids[0])
            return;
        used_ids[used_count++] = c->id;
    }
    memset(a, 0, sizeof *a);
    a->used = 1;
    a->skin = wg_ped_model_ok(c->skin) ? (short)c->skin : FALLBACK_SKIN;
    a->invulnerable = c->invulnerable != 0;
    a->angle = c->angle;
    a->health = a->seen_health = c->health;
    for (i = 0; i < 3; i++)
        a->pos[i] = c->pos[i];
}

static void on_out(const srpc_id_t *v)
{
    wactor_t *a = get(v->id);
    unsigned i;

    if (!a)
        return;
    despawn(a);
    a->used = 0;
    for (i = 0; i < used_count; i++)
        if (used_ids[i] == v->id) {
            used_ids[i] = used_ids[--used_count];
            break;
        }
}

static void on_anim(const srpc_id_anim_t *v)
{
    wactor_t *a = get(v->id);
    if (a) {
        a->anim = v->anim;
        a->anim_wait = 1;
    }
}

static void on_clear(const srpc_id_t *v)
{
    wactor_t *a = get(v->id);
    if (!a)
        return;
    a->anim_wait = 0;
    if (a->handle)
        wg_actor_clear_tasks(a->handle);
}

static void on_angle(const srpc_id_float_t *v)
{
    wactor_t *a = get(v->id);
    if (!a || !world_float_ok(v->v))
        return;
    a->angle = v->v;
    if (a->handle)
        wg_actor_angle(a->handle, v->v);
}

static void on_pos(const srpc_id_vec3_t *v)
{
    wactor_t *a = get(v->id);
    unsigned i;
    if (!a || !world_pos_ok(v->v))
        return;
    for (i = 0; i < 3; i++)
        a->pos[i] = v->v[i];
    if (a->handle)
        wg_actor_place(a->handle, a->pos);
}

static void on_health(const srpc_id_float_t *v)
{
    wactor_t *a = get(v->id);
    if (!a || !world_float_ok(v->v) || v->v < 0.0f)
        return;
    a->health = a->seen_health = v->v;
    if (a->handle)
        wg_actor_health(a->handle, v->v);
}

static void spawn(wactor_t *a)
{
    if (live_count >= LIVE_MAX || wg_ped_slots() <= PED_RESERVE)
        return;
    if (!wg_model_loaded(a->skin)) {
        if (!a->requested && wg_stream_room()) {
            wg_model_request(a->skin);
            a->requested = 1;
        }
        return;
    }
    a->handle = wg_actor_create(a->skin, a->pos, a->angle);
    if (a->requested) {
        wg_model_release(a->skin);
        a->requested = 0;
    }
    if (!a->handle)
        return;
    live_count++;
    wg_actor_health(a->handle, a->health);
    wg_actor_invulnerable(a->handle, a->invulnerable);
}

/* The library may take frames to load; the name is checked once it has. */
static void step_anim(wactor_t *a)
{
    unsigned block = cg_anim_block(a->anim.lib);

    if (!block) {
        a->anim_wait = 0;
        return;
    }
    if (!cg_anim_ready(block)) {
        cg_anim_request(a->anim.lib);
        return;
    }
    a->anim_wait = 0;
    if (cg_anim_exists(block, a->anim.name) && world_float_ok(a->anim.delta))
        cg_anim_play(a->handle, a->anim.name, a->anim.lib, a->anim.delta, a->anim.loop,
                     a->anim.lock_x, a->anim.lock_y, a->anim.freeze, (int)a->anim.time);
}

static void report_hit(unsigned id, wactor_t *a)
{
    static unsigned char buf[16];
    float now_hp = wg_actor_get_health(a->handle);
    bs_t b;

    if (!wg_actor_hit_by_local(a->handle) || now_hp >= a->seen_health) {
        a->seen_health = now_hp;
        return;
    }
    bs_wr_init(&b, buf, sizeof buf);
    srpc_enc_actor_damage(&b, id, a->seen_health - now_hp, wg_local_weapon(), BODY_TORSO);
    a->seen_health = now_hp;
    if (!b.err)
        world_send(SRPC_C2S_ACTOR_DAMAGE, buf, b.bits);
}

void wactor_tick(unsigned now)
{
    int check = now - hit_at >= HIT_MS;
    unsigned i;

    if (check)
        hit_at = now;
    for (i = 0; i < used_count; i++) {
        unsigned id = used_ids[i];
        wactor_t *a = &actors[id];

        if (a->handle && !wg_char_alive(a->handle)) {
            a->handle = 0;
            live_count--;
        }
        if (!a->handle) {
            spawn(a);
            continue;
        }
        if (a->anim_wait)
            step_anim(a);
        if (check && !a->invulnerable)
            report_hit(id, a);
    }
}

void wactor_reset(void)
{
    unsigned i;
    for (i = 0; i < used_count; i++) {
        despawn(&actors[used_ids[i]]);
        actors[used_ids[i]].used = 0;
    }
    used_count = 0;
}

void wactor_init(samp_rpc_handlers_t *h)
{
    h->actor_stream_in = on_in;
    h->actor_stream_out = on_out;
    h->apply_actor_animation = on_anim;
    h->clear_actor_animations = on_clear;
    h->set_actor_angle = on_angle;
    h->set_actor_pos = on_pos;
    h->set_actor_health = on_health;
}
