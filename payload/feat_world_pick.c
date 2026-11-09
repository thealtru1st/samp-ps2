/* Pickups and checkpoints.
 *
 * Pickups stream the same way objects do: the game has 620 slots but a pickup near the
 * player also takes an object from the pool, so only the nearest LIVE_MAX exist.
 *
 * Checkpoints: the server works out enter and leave from our position sync, so there is
 * nothing to send. The game needs the locate opcode once per frame to draw the sphere. */
#include "feat_world_int.h"
#include "bitstream.h"
#include "util.h"
#include "samp.h"

#define ID_MAX        4096  /* MAX_PICKUPS */
#define LIVE_MAX      96
#define RANGE_MAX     220.0f
#define RANGE_MID     120.0f
#define RANGE_MIN     60.0f
#define KEEP_MARGIN   20.0f
#define SCAN_MS       500
#define CREATE_PER_SCAN 8
#define RESEND_MS     1000  /* a pickup that stays is collected every frame */
#define TOUCH_R       1.5f  /* horizontal reach of a pickup we test ourselves */
#define TOUCH_DZ      2.0f
#define TYPE_IN_CAR   14
#define TYPE_LAST     23
#define FALLBACK_MODEL 1239 /* the "i": PC falls back to an added model we do not have */

#define SIZE_MIN      0.1f
#define SIZE_MAX      100.0f
#define RACE_TYPE_LAST 8
#define BLIP_RED      0
#define BLIP_SIZE     3

typedef struct {
    unsigned char used, requested;
    short model;
    int type, handle;
    float pos[3];
    unsigned sent_at;
} wpick_t;

static wpick_t picks[ID_MAX];
static unsigned short used_ids[ID_MAX];
static unsigned used_count, live_count, scan_at;

static struct { int on, blip; float pos[3], size; } cp;
static struct { int on, blip, handle; } race;

static void despawn(wpick_t *p)
{
    if (p->handle) {
        wg_pickup_delete(p->handle);
        p->handle = 0;
        live_count--;
    }
    if (p->requested) {
        wg_model_release(p->model);
        p->requested = 0;
    }
}

static void drop_record(int id)
{
    unsigned i;

    despawn(&picks[id]);
    picks[id].used = 0;
    for (i = 0; i < used_count; i++)
        if (used_ids[i] == id) {
            used_ids[i] = used_ids[--used_count];
            break;
        }
}

static void on_create(const srpc_pickup_t *c)
{
    wpick_t *p;
    int model;

    if (c->id < 0 || c->id >= ID_MAX || c->type <= 0 || c->type > TYPE_LAST || !world_pos_ok(c->pos))
        return;
    if (picks[c->id].used)
        drop_record(c->id);
    model = wg_model_map(c->model);
    if (model < 0)
        model = wg_model_map(FALLBACK_MODEL);
    if (model < 0)
        return;
    p = &picks[c->id];
    memset(p, 0, sizeof *p);
    p->used = 1;
    p->model = model;
    p->type = c->type;
    p->pos[0] = c->pos[0];
    p->pos[1] = c->pos[1];
    p->pos[2] = c->pos[2];
    used_ids[used_count++] = c->id;
}

static void on_destroy(const srpc_int_t *v)
{
    if (v->v >= 0 && v->v < ID_MAX && picks[v->v].used)
        drop_record(v->v);
}

static void send_touched(int id, unsigned now)
{
    static unsigned char buf[8];
    static int last = -1;
    wpick_t *p = &picks[id];
    bs_t b;

    if (p->sent_at && now - p->sent_at < RESEND_MS)
        return;
    if (id != last)
        slog("pick: touched pickup %d model %d type %d", id, p->model, p->type);
    last = id;
    p->sent_at = now;
    bs_wr_init(&b, buf, sizeof buf);
    srpc_enc_picked_up_pickup(&b, id);
    if (!b.err)
        world_send(SRPC_C2S_PICKED_UP_PICKUP, buf, b.bits);
}

static void report_collected(unsigned now)
{
    int h;

    while ((h = wg_pickup_collected()) != 0) {
        unsigned i;
        for (i = 0; i < used_count; i++)
            if (picks[used_ids[i]].handle == h) {
                send_touched(used_ids[i], now);
                break;
            }
    }
}

/* The game does not collect every type here: a type 1 pickup (a business or house door
 * on most servers) never reaches the ring on the PS2 build, measured standing 0.2 m from
 * one on Valrise. The PC client reports type 1 every few seconds while the player stands
 * in it, and scripts check that before letting anyone in, so test the distance ourselves.
 * Type 14 is the in-vehicle kind; everything else counts on foot. */
static void report_touching(const float *me, unsigned now)
{
    int in_car = samp_plat_in_vehicle();
    unsigned i;

    for (i = 0; i < used_count; i++) {
        int id = used_ids[i];
        const wpick_t *p = &picks[id];
        float dx, dy, dz;
        if (!p->handle || (p->type == TYPE_IN_CAR) != (in_car != 0))
            continue;
        dx = p->pos[0] - me[0];
        dy = p->pos[1] - me[1];
        dz = p->pos[2] - me[2];
        if (dx * dx + dy * dy < TOUCH_R * TOUCH_R && dz > -TOUCH_DZ && dz < TOUCH_DZ)
            send_touched(id, now);
    }
}

/* The widest of three radii that holds no more than the game should carry. */
static float pick_radius(const float *me)
{
    static const float radius[2] = {RANGE_MAX, RANGE_MID};
    unsigned i, k;

    for (k = 0; k < 2; k++) {
        unsigned count = 0;
        for (i = 0; i < used_count; i++)
            if (world_dist2(picks[used_ids[i]].pos, me) <= radius[k] * radius[k])
                count++;
        if (count <= LIVE_MAX)
            return radius[k];
    }
    return RANGE_MIN;
}

static void stream(const float *me)
{
    unsigned i, made = 0;
    float r = pick_radius(me), near2 = r * r, keep2 = (r + KEEP_MARGIN) * (r + KEEP_MARGIN);

    for (i = 0; i < used_count; i++) {
        wpick_t *p = &picks[used_ids[i]];
        float d2 = world_dist2(p->pos, me);

        if (p->handle) {
            if (d2 > keep2)
                despawn(p);
            continue;
        }
        if (d2 > near2) {
            if (p->requested)
                despawn(p);
            continue;
        }
        if (live_count >= LIVE_MAX || made >= CREATE_PER_SCAN)
            continue;
        if (!wg_model_loaded(p->model)) {
            if (!p->requested && wg_stream_room()) {
                wg_model_request(p->model);
                p->requested = 1;
            }
            continue;
        }
        p->handle = wg_pickup_create(p->model, p->type, p->pos);
        if (p->handle == -1)
            p->handle = 0;
        else
            live_count++;
        made++;
        if (p->requested) {
            wg_model_release(p->model);
            p->requested = 0;
        }
    }
}

/* ---- checkpoints ---------------------------------------------------------- */

static int make_blip(const float *pos)
{
    int b = wg_blip_coord(pos);
    if (b != -1) {
        wg_blip_colour(b, BLIP_RED);
        wg_blip_scale(b, BLIP_SIZE);
    }
    return b;
}

static void drop_blip(int *b)
{
    if (*b != -1 && *b != 0)
        wg_blip_delete(*b);
    *b = 0;
}

static void on_checkpoint(const srpc_checkpoint_t *c)
{
    if (!world_pos_ok(c->pos) || !world_float_ok(c->size))
        return;
    drop_blip(&cp.blip);
    cp.pos[0] = c->pos[0];
    cp.pos[1] = c->pos[1];
    cp.pos[2] = c->pos[2];
    cp.size = c->size < SIZE_MIN ? SIZE_MIN : c->size > SIZE_MAX ? SIZE_MAX : c->size;
    cp.on = 1;
    cp.blip = make_blip(cp.pos);
}

static void on_checkpoint_off(void)
{
    drop_blip(&cp.blip);
    cp.on = 0;
}

static void race_off(void)
{
    drop_blip(&race.blip);
    if (race.on)
        wg_race_cp_delete(race.handle);
    race.on = 0;
}

static void on_race(const srpc_race_checkpoint_t *c)
{
    float size;

    if (!world_pos_ok(c->pos) || !world_pos_ok(c->next) || !world_float_ok(c->size) ||
        c->type > RACE_TYPE_LAST)
        return;
    race_off();
    size = c->size < SIZE_MIN ? SIZE_MIN : c->size > SIZE_MAX ? SIZE_MAX : c->size;
    /* Slots run out at 20 and the game then hands back a wild index: only ever hold one. */
    race.handle = wg_race_cp_create(c->type, c->pos, c->next, size);
    race.on = 1;
    race.blip = make_blip(c->pos);
}

/* ---- entry points --------------------------------------------------------- */

void wpick_tick(unsigned now)
{
    float me[3];

    report_collected(now);
    if (wg_local_pos(me))
        report_touching(me, now);
    if (cp.on)
        wg_draw_sphere(cp.pos, cp.size);
    if (now - scan_at >= SCAN_MS) {
        scan_at = now;
        if (wg_local_pos(me))
            stream(me);
    }
}

void wpick_reset(void)
{
    while (used_count)
        drop_record(used_ids[used_count - 1]);
    on_checkpoint_off();
    race_off();
}

void wpick_init(samp_rpc_handlers_t *h)
{
    h->create_pickup = on_create;
    h->destroy_pickup = on_destroy;
    h->set_checkpoint = on_checkpoint;
    h->disable_checkpoint = on_checkpoint_off;
    h->set_race_checkpoint = on_race;
    h->disable_race_checkpoint = race_off;
}
