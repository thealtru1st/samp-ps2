/* Server objects. A server may place thousands; the game's object pool holds 250 and is
 * shared with the map's own dynamic objects and every visible pickup, so only the nearest
 * few exist in the game at any time. The rest wait here as records.
 *
 * Every SCAN_MS the records are bucketed by distance to find the radius that holds
 * LIVE_MAX objects. Records inside it are wanted, nearest first. A live object is dropped
 * only past that radius plus a margin, so nothing flickers at the edge. */
#include "feat_world_int.h"
#include "bitstream.h"
#include "samp.h"
#include "util.h"

#define ID_MAX        2048  /* open.mp's pool is 2000, stock 1000 */
#define LIVE_MAX      130
#define POOL_RESERVE  56    /* free pool entries left for the game itself */
#define RANGE_MAX     300.0f
#define BUCKET_M      10.0f
#define BUCKETS       30
#define KEEP_MARGIN   25.0f
#define SCAN_MS       250
#define QUEUE_MAX     24
#define CREATE_PER_TICK 2
#define EVICT_PER_SCAN 8
#define NO_ROT        (-1000.0f)  /* MoveObject's "leave the rotation alone" */
#define ATTACH_NONE   0xFFFFu

#define ATT_MAX       96    /* SetPlayerAttachedObject records: 10 per streamed player */
#define ATT_LIVE_MAX  24
#define ATT_SLOTS     10
#define BONE_MAX      18

enum { TO_NONE, TO_CAR, TO_OBJECT, TO_PLAYER };

typedef struct {
    unsigned char used, attach, requested, moving, rotating;
    short model;              /* PS2 id, -1 when the disc has none */
    int handle;
    float pos[3], rot[3], range, dist;
    unsigned attach_id;
    int hung_to;            /* handle it was attached to: if that changes, it must go */
    float off[3], off_rot[3];
    float to[3], rot_from[3], rot_to[3], speed;
    unsigned moved_at, move_ms, move_total;
    float move_from[3];
} wobj_t;

typedef struct {
    unsigned char used, requested;
    unsigned char slot;
    unsigned short player;
    short model;
    int handle, on_char;
    unsigned bone;
    float off[3], rot[3];
    unsigned color1, color2, pc_model;
    float scale[3];
} watt_t;

static wobj_t objs[ID_MAX];
static unsigned short used_ids[ID_MAX];
static unsigned used_count, moving_count, live_count;
static unsigned short queue[QUEUE_MAX];
static unsigned queue_len, scan_at;

static watt_t atts[ATT_MAX];
static unsigned att_live;

/* Where each SA-MP bone sits on a standing ped, from the ped's root. There is no opcode
 * for a bone matrix, so an attached object rides the ped and does not follow the limb. */
static const float bone_at[BONE_MAX + 1][3] = {
    {0, 0, 0},
    {0, 0, 0.20f},        /* spine */
    {0, 0.02f, 0.62f},    /* head */
    {-0.17f, 0, 0.42f},   /* left upper arm */
    {0.17f, 0, 0.42f},    /* right upper arm */
    {-0.25f, 0.10f, -0.08f}, /* left hand */
    {0.25f, 0.10f, -0.08f},  /* right hand */
    {-0.10f, 0, -0.10f},  /* left thigh */
    {0.10f, 0, -0.10f},   /* right thigh */
    {-0.10f, 0.05f, -0.90f}, /* left foot */
    {0.10f, 0.05f, -0.90f},  /* right foot */
    {0.10f, 0, -0.50f},   /* right calf */
    {-0.10f, 0, -0.50f},  /* left calf */
    {-0.22f, 0.05f, 0.15f},  /* left forearm */
    {0.22f, 0.05f, 0.15f},   /* right forearm */
    {-0.10f, 0, 0.47f},   /* left clavicle */
    {0.10f, 0, 0.47f},    /* right clavicle */
    {0, 0, 0.52f},        /* neck */
    {0, 0.05f, 0.56f},    /* jaw */
};

static wobj_t *get(unsigned id) { return id < ID_MAX && objs[id].used ? &objs[id] : 0; }

static void vcopy(float *d, const float *s) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }

/* Car parts (models 1000-1193) are shared with the tuning system: the car code requests a
 * part, fits it and releases it, and a car leaving the game lets its parts go. An object
 * built from the same part (Valrise makes its custom cars that way) then sits on a model
 * the game has dropped: measured live with the model unloaded, and the frame that draws
 * it sends the GPU to freed memory and hangs. So a part stays requested while any of our
 * objects uses it, asserted again every second in case the car code released it. */
#define PART_FIRST 1000
#define PART_N     194
#define PART_REFRESH_MS 1000
static unsigned short part_pin[PART_N];

static int is_part(int m) { return m >= PART_FIRST && m < PART_FIRST + PART_N; }

static void part_hold(int m)
{
    if (is_part(m) && part_pin[m - PART_FIRST]++ == 0)
        wg_model_request(m);
}

static void part_drop(int m)
{
    if (is_part(m) && part_pin[m - PART_FIRST] && --part_pin[m - PART_FIRST] == 0)
        wg_model_release(m);
}

static void part_refresh(unsigned now)
{
    static unsigned at;
    unsigned i;
    if (now - at < PART_REFRESH_MS)
        return;
    at = now;
    for (i = 0; i < PART_N; i++)
        if (part_pin[i])
            wg_model_request(PART_FIRST + i);
}

static void forget_request(wobj_t *o)
{
    if (o->requested) {
        wg_model_release(o->model);
        o->requested = 0;
    }
}

/* An attached object must go before what it hangs from. Left on a deleted car or ped, the
 * game keeps reading that entity's freed matrix; the garbage position (x = 9.9e10 measured
 * on Valrise) sends the collision code through billions of sectors, a hard freeze. So
 * whatever hangs from an object goes with it. */
static void despawn(wobj_t *o)
{
    unsigned i, id = (unsigned)(o - objs);

    if (o->handle) {
        int h = o->handle;
        o->handle = o->hung_to = 0;   /* first, so a loop of attachments ends */
        live_count--;
        part_drop(o->model);
        for (i = 0; i < used_count; i++) {
            wobj_t *c = &objs[used_ids[i]];
            if (c->handle && c->attach == TO_OBJECT && c->attach_id == id)
                despawn(c);
        }
        wg_object_delete(h);
    }
    forget_request(o);
}

/* Called just before a car or a player's ped leaves the game. */
static void despawn_hanging(unsigned kind, unsigned id)
{
    unsigned i;
    for (i = 0; i < used_count; i++) {
        wobj_t *o = &objs[used_ids[i]];
        if (o->handle && o->attach == kind && o->attach_id == id)
            despawn(o);
    }
}

static void drop_record(unsigned id)
{
    wobj_t *o = &objs[id];
    unsigned i;

    despawn(o);
    if (o->moving)
        moving_count--;
    o->used = 0;
    for (i = 0; i < used_count; i++)
        if (used_ids[i] == id) {
            used_ids[i] = used_ids[--used_count];
            break;
        }
}

static void place_record(wobj_t *o, const srpc_object_t *c, int model)
{
    memset(o, 0, sizeof *o);
    o->used = 1;
    o->model = model;
    vcopy(o->pos, c->pos);
    vcopy(o->rot, c->rot);
    o->range = c->distance > 0.0f && c->distance < RANGE_MAX && world_float_ok(c->distance)
                   ? c->distance : RANGE_MAX;
    if (c->attach_vehicle != ATTACH_NONE && c->attach_vehicle != 0) {
        o->attach = TO_CAR;
        o->attach_id = c->attach_vehicle;
    } else if (c->attach_object != ATTACH_NONE && c->attach_object != 0) {
        o->attach = TO_OBJECT;
        o->attach_id = c->attach_object;
    }
    if (o->attach) {
        vcopy(o->off, c->offset);
        vcopy(o->off_rot, c->offset_rot);
    }
}

static int vec_ok(const float *v)
{
    return world_float_ok(v[0]) && world_float_ok(v[1]) && world_float_ok(v[2]);
}

static void on_create(const srpc_object_t *c)
{
    int model;

    if (c->id >= ID_MAX || !world_pos_ok(c->pos) || !vec_ok(c->rot))
        return;
    if ((c->attach_vehicle != ATTACH_NONE || c->attach_object != ATTACH_NONE) &&
        (!vec_ok(c->offset) || !vec_ok(c->offset_rot)))
        return;
    if (objs[c->id].used)
        drop_record(c->id);
    model = wg_model_map(c->model);
    if (model < 0) { /* big servers have hundreds: name a few, then just count */
        static unsigned missing;
        if (++missing <= 5)
            slog("world: object %d model %d is not on PS2", c->id, c->model);
        else if (missing % 100 == 0)
            slog("world: %d objects so far are not on PS2", missing);
    }
    place_record(&objs[c->id], c, model);
    used_ids[used_count++] = c->id;
}

static void on_destroy(const srpc_id_t *v)
{
    if (get(v->id))
        drop_record(v->id);
}

static void on_pos(const srpc_id_vec3_t *v)
{
    wobj_t *o = get(v->id);
    if (!o || !world_pos_ok(v->v))
        return;
    vcopy(o->pos, v->v);
    if (o->handle)
        wg_object_move(o->handle, o->pos);
}

static void on_rot(const srpc_id_vec3_t *v)
{
    wobj_t *o = get(v->id);
    if (!o || !vec_ok(v->v))
        return;
    vcopy(o->rot, v->v);
    if (o->handle)
        wg_object_rotate(o->handle, o->rot);
}

/* ---- moving objects ------------------------------------------------------- */

static void on_move(const srpc_object_move_t *m)
{
    wobj_t *o = get(m->id);
    float dist;

    if (!o || !world_pos_ok(m->from) || !world_pos_ok(m->to) || !vec_ok(m->rot) ||
        !world_float_ok(m->speed) || m->speed <= 0.0f)
        return;
    /* Time is what the server counts, so the move is timed and not stepped. */
    dist = wg_sqrt(world_dist2(m->from, m->to));
    vcopy(o->pos, m->from);
    vcopy(o->move_from, m->from);
    vcopy(o->to, m->to);
    vcopy(o->rot_from, o->rot);
    vcopy(o->rot_to, m->rot);
    o->rotating = m->rot[0] > NO_ROT && m->rot[1] > NO_ROT && m->rot[2] > NO_ROT;
    o->move_total = (unsigned)(dist / m->speed * 1000.0f);
    o->move_ms = 0;
    o->moved_at = 0;
    if (!o->moving) {
        o->moving = 1;
        moving_count++;
    }
}

static void stop_moving(wobj_t *o)
{
    if (o->moving) {
        o->moving = 0;
        moving_count--;
    }
}

static void on_stop(const srpc_id_t *v)
{
    wobj_t *o = get(v->id);
    if (o)
        stop_moving(o);
}

static void step_move(wobj_t *o, unsigned now)
{
    float t;
    int i;

    if (!o->moved_at)
        o->moved_at = now;
    o->move_ms += now - o->moved_at;
    o->moved_at = now;
    t = o->move_total ? (float)o->move_ms / (float)o->move_total : 1.0f;
    if (t >= 1.0f) {
        t = 1.0f;
        stop_moving(o);
    }
    for (i = 0; i < 3; i++) {
        o->pos[i] = o->move_from[i] + (o->to[i] - o->move_from[i]) * t;
        if (o->rotating)
            o->rot[i] = o->rot_from[i] + (o->rot_to[i] - o->rot_from[i]) * t;
    }
    if (o->handle) {
        wg_object_move(o->handle, o->pos);
        if (o->rotating)
            wg_object_rotate(o->handle, o->rot);
    }
}

/* ---- attachment ----------------------------------------------------------- */

static int car_handle(unsigned id) { return feat_vehicle_handle ? feat_vehicle_handle(id) : 0; }

/* What an attached record hangs from, or 0 when that is not in the game right now. */
static int attach_target(const wobj_t *o)
{
    const wobj_t *t;
    switch (o->attach) {
    case TO_CAR:    return car_handle(o->attach_id);
    case TO_PLAYER: return world_player_char(o->attach_id);
    case TO_OBJECT:
        t = get(o->attach_id);
        return t ? t->handle : 0;
    }
    return 0;
}

static void hang(wobj_t *o, int target)
{
    o->hung_to = target;
    switch (o->attach) {
    case TO_CAR:    wg_object_attach_car(o->handle, target, o->off, o->off_rot); break;
    case TO_PLAYER: wg_object_attach_char(o->handle, target, o->off, o->off_rot); break;
    case TO_OBJECT: wg_object_attach_object(o->handle, target, o->off, o->off_rot); break;
    }
}

static void on_attach_player(const srpc_object_attach_player_t *a)
{
    wobj_t *o = get(a->object);
    if (!o || !vec_ok(a->offset) || !vec_ok(a->rot))
        return;
    o->attach = TO_PLAYER;
    o->attach_id = a->player;
    vcopy(o->off, a->offset);
    vcopy(o->off_rot, a->rot);
    despawn(o); /* the next pass creates it attached */
}

/* ---- the streamer --------------------------------------------------------- */

static int capacity(void)
{
    int spare = wg_object_slots() - POOL_RESERVE + (int)live_count;
    return spare > LIVE_MAX ? LIVE_MAX : spare < 0 ? 0 : spare;
}

static void queue_add(unsigned id, float dist)
{
    unsigned i = queue_len;

    if (queue_len == QUEUE_MAX) {
        if (objs[queue[QUEUE_MAX - 1]].dist <= dist)
            return;
        i = QUEUE_MAX - 1;
    } else {
        queue_len++;
    }
    while (i > 0 && objs[queue[i - 1]].dist > dist) {
        queue[i] = queue[i - 1];
        i--;
    }
    queue[i] = id;
}

/* The margin can leave far objects holding every slot while nearer ones wait. */
static void evict_for_queue(void)
{
    unsigned n, i;

    for (n = 0; n < EVICT_PER_SCAN; n++) {
        int cap = capacity(), over = (int)live_count > cap;
        wobj_t *far = 0;
        if (!over && !(queue_len && (int)live_count == cap))
            return;
        for (i = 0; i < used_count; i++) {
            wobj_t *o = &objs[used_ids[i]];
            if (o->handle && !o->attach && (!far || o->dist > far->dist))
                far = o;
        }
        if (!far || (!over && far->dist <= objs[queue[0]].dist))
            return;
        despawn(far);
    }
}

static void scan(const float *me)
{
    unsigned hist[BUCKETS], i, total = 0, cap = capacity();
    float cutoff = RANGE_MAX, keep;

    memset(hist, 0, sizeof hist);
    for (i = 0; i < used_count; i++) {
        wobj_t *o = &objs[used_ids[i]];
        int b;
        o->dist = o->attach ? 0.0f : wg_sqrt(world_dist2(o->pos, me));
        if (o->model < 0 || o->dist > o->range)
            continue;
        b = (int)(o->dist / BUCKET_M);
        hist[b >= BUCKETS ? BUCKETS - 1 : b]++;
    }
    for (i = 0; i < BUCKETS; i++) {
        total += hist[i];
        if (total > cap) {
            cutoff = (float)(i + 1) * BUCKET_M; /* the bucket that overflows still fills up */
            break;
        }
    }
    keep = cutoff + KEEP_MARGIN;
    queue_len = 0;
    for (i = 0; i < used_count; i++) {
        unsigned id = used_ids[i];
        wobj_t *o = &objs[id];
        if (o->model < 0)
            continue;
        if (o->attach) {
            int target = attach_target(o);
            if (!target || (o->handle && target != o->hung_to)) {
                despawn(o);
                continue;
            }
        } else if (o->handle ? (o->dist > keep || o->dist > o->range + KEEP_MARGIN)
                             : (o->dist > cutoff || o->dist > o->range)) {
            despawn(o);
            continue;
        }
        if (!o->handle)
            queue_add(id, o->dist);
    }
    evict_for_queue();
}

static void spawn_one(wobj_t *o)
{
    int target = 0;

    if (o->attach && !(target = attach_target(o)))
        return;
    if (!wg_model_loaded(o->model)) {
        if (!o->requested && wg_stream_room()) {
            wg_model_request(o->model);
            o->requested = 1;
        }
        return;
    }
    o->handle = wg_object_create(o->model, o->pos, o->rot);
    if (o->handle) {
        live_count++;
        part_hold(o->model);
        if (o->attach)
            hang(o, target);
    }
    /* The object holds the model now; once it goes, the streamer may reclaim it. */
    forget_request(o);
}

static void step_queue(void)
{
    unsigned i, done = 0, left = 0;

    if ((int)live_count >= capacity() || wg_object_slots() <= POOL_RESERVE)
        return;
    for (i = 0; i < queue_len; i++) {
        wobj_t *o = &objs[queue[i]];
        if (done < CREATE_PER_TICK && !o->handle && o->used) {
            int loaded = o->model >= 0 && wg_model_loaded(o->model);
            spawn_one(o);
            if (loaded)
                done++;
        }
        if (o->used && !o->handle)
            queue[left++] = queue[i];
    }
    queue_len = left;
}

/* ---- attached objects ----------------------------------------------------- */

static watt_t *att_find(unsigned player, unsigned slot)
{
    unsigned i;
    for (i = 0; i < ATT_MAX; i++)
        if (atts[i].used && atts[i].player == player && atts[i].slot == slot)
            return &atts[i];
    return 0;
}

static void att_despawn(watt_t *a)
{
    if (a->handle) {
        part_drop(a->model);
        wg_object_delete(a->handle);
        a->handle = 0;
        att_live--;
    }
    if (a->requested) {
        wg_model_release(a->model);
        a->requested = 0;
    }
}

static void on_attached(const srpc_attached_object_t *s)
{
    watt_t *a = att_find(s->player, s->index);
    unsigned i;
    int model;

    if (s->index >= ATT_SLOTS)
        return;
    if (a) {
        att_despawn(a);
        a->used = 0;
    }
    if (!s->create || !vec_ok(s->offset) || !vec_ok(s->rot) || !vec_ok(s->scale))
        return;
    model = wg_model_map(s->model);
    if (model < 0 || s->bone > BONE_MAX)
        return;
    for (i = 0; i < ATT_MAX && atts[i].used; i++)
        ;
    if (i == ATT_MAX)
        return;
    a = &atts[i];
    memset(a, 0, sizeof *a);
    a->used = 1;
    a->player = s->player;
    a->slot = s->index;
    a->model = model;
    a->pc_model = s->model;
    a->bone = s->bone;
    a->color1 = s->color1;
    a->color2 = s->color2;
    vcopy(a->off, s->offset);
    vcopy(a->rot, s->rot);
    vcopy(a->scale, s->scale);
}

static void att_tick(void)
{
    unsigned i, done = 0;
    static const float nothing[3] = {0, 0, 0};

    for (i = 0; i < ATT_MAX; i++) {
        watt_t *a = &atts[i];
        int ped;
        float off[3], here[3];

        if (!a->used)
            continue;
        ped = world_player_char(a->player);
        if (!ped || !wg_char_alive(ped)) {
            att_despawn(a);
            continue;
        }
        if (a->handle && a->on_char != ped)
            att_despawn(a); /* the player's ped was remade under the same id */
        if (a->handle && !wg_model_loaded(a->model))
            att_despawn(a); /* the game dropped its model: see part_hold */
        if (a->handle || done || att_live >= ATT_LIVE_MAX || wg_object_slots() <= POOL_RESERVE)
            continue;
        if (!wg_model_loaded(a->model)) {
            if (!a->requested && wg_stream_room()) {
                wg_model_request(a->model);
                a->requested = 1;
            }
            continue;
        }
        if (!wg_local_pos(here))
            continue;
        a->handle = wg_object_create(a->model, here, nothing);
        if (a->requested) {
            wg_model_release(a->model);
            a->requested = 0;
        }
        done = 1;
        if (!a->handle)
            continue;
        att_live++;
        part_hold(a->model);
        a->on_char = ped;
        off[0] = bone_at[a->bone][0] + a->off[0];
        off[1] = bone_at[a->bone][1] + a->off[1];
        off[2] = bone_at[a->bone][2] + a->off[2];
        wg_object_attach_char(a->handle, ped, off, a->rot);
    }
}

/* The PC editors need a mouse. A cancel keeps a server from waiting on one for ever. */
static void on_edit_object(const srpc_object_edit_t *e)
{
    static unsigned char buf[64];
    static const float zero[3] = {0, 0, 0};
    bs_t b;

    bs_wr_init(&b, buf, sizeof buf);
    srpc_enc_edit_object(&b, e->player_object, e->object, 0, zero, zero);
    if (!b.err)
        world_send(SRPC_C2S_EDIT_OBJECT, buf, b.bits);
}

static void on_edit_attached(const srpc_id_t *v)
{
    static unsigned char buf[96];
    static const float zero[3] = {0, 0, 0}, one[3] = {1, 1, 1};
    watt_t *a = att_find(samp_stats.player_id, v->id);
    bs_t b;

    bs_wr_init(&b, buf, sizeof buf);
    if (a)
        srpc_enc_edit_attached_object(&b, 0, v->id, a->pc_model, a->bone, a->off, a->rot, a->scale,
                                      a->color1, a->color2);
    else
        srpc_enc_edit_attached_object(&b, 0, v->id, 0, 0, zero, zero, one, 0, 0);
    if (!b.err)
        world_send(SRPC_C2S_EDIT_ATTACHED_OBJECT, buf, b.bits);
}

/* ---- entry points --------------------------------------------------------- */

void feat_world_car_going(unsigned id)  { despawn_hanging(TO_CAR, id); }
void feat_world_char_going(unsigned id) { despawn_hanging(TO_PLAYER, id); }

/* Every frame, not just on a scan: a target remade under a new handle (a car streamed
 * back in, a ped recreated) would otherwise keep this object on the deleted one. */
static void check_hanging(void)
{
    static unsigned told;
    unsigned i;
    for (i = 0; i < used_count; i++) {
        wobj_t *o = &objs[used_ids[i]];
        /* Never draw an object whose model the game has dropped: see part_hold. */
        if (o->handle && o->model >= 0 && !wg_model_loaded(o->model)) {
            if (told < 30) {
                told++;
                slog("world: object %d model %d was unloaded under it, removed", (int)used_ids[i], o->model);
            }
            despawn(o);
            continue;
        }
        if (o->handle && o->attach && attach_target(o) != o->hung_to)
            despawn(o);
    }
}

void wobj_tick(unsigned now)
{
    float me[3];
    unsigned i;

    check_hanging();
    part_refresh(now);
    if (moving_count)
        for (i = 0; i < used_count; i++)
            if (objs[used_ids[i]].moving)
                step_move(&objs[used_ids[i]], now);
    if (!wg_local_pos(me))
        return;
    if (now - scan_at >= SCAN_MS) {
        scan_at = now;
        scan(me);
    }
    step_queue();
    att_tick();
}

void wobj_reset(void)
{
    unsigned i;

    while (used_count)
        drop_record(used_ids[used_count - 1]);
    for (i = 0; i < ATT_MAX; i++)
        if (atts[i].used) {
            att_despawn(&atts[i]);
            atts[i].used = 0;
        }
    queue_len = 0;
}

void wobj_init(samp_rpc_handlers_t *h)
{
    h->create_object = on_create;
    h->destroy_object = on_destroy;
    h->set_object_pos = on_pos;
    h->set_object_rot = on_rot;
    h->move_object = on_move;
    h->stop_object = on_stop;
    h->attach_object_to_player = on_attach_player;
    h->set_attached_object = on_attached;
    h->edit_object = on_edit_object;
    h->edit_attached_object = on_edit_attached;
    /* set_object_material stays empty. Text needs Windows fonts drawn into a texture;
     * textures need RE of PS2 materials. Objects keep their stock look. */
}
