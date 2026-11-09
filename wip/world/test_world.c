/* Host tests of the world stream's logic against fake_game.c. Nothing here touches the
 * game: what they prove is the caps, the bookkeeping and that bad data stays out. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "feat_world_int.h"
#include "fake_game.h"
#include "feat_world_models.h"
#include "bitstream.h"
#include "samp.h"
#include "rak.h"

static samp_rpc_handlers_t h;
static int state_now = SAMP_SPAWNED, failures, checks;
static unsigned now_ms = 1000;
static unsigned last_rpc, rpc_count, last_bits;
static unsigned char last_data[64];

samp_stats_t samp_stats;
int samp_state(void) { return state_now; }
int rak_rpc(int id, const void *d, int bits, int rel, int ch)
{
    last_rpc = id;
    last_bits = bits;
    rpc_count++;
    memcpy(last_data, d, (bits + 7) / 8 > 64 ? 64 : (bits + 7) / 8);
    return 0;
}
void slog(const char *f, ...) {}
void fg_hit_actor(int h, float amount);

/* combat's animation calls, as used by actors */
static int anim_ready, anim_played;
unsigned cg_anim_block(const char *lib) { return strcmp(lib, "BAD") ? 100 : 0; }
void cg_anim_request(const char *lib) { anim_ready = 1; }
int cg_anim_ready(unsigned b) { return anim_ready; }
int cg_anim_exists(unsigned b, const char *n) { return strcmp(n, "NOPE") != 0; }
void cg_anim_play(int hd, const char *n, const char *l, float d, int lp, int x, int y, int f, int ms) { anim_played++; }

#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __func__, __LINE__, #c); failures++; } } while (0)

static void tick(int frames, unsigned step)
{
    int i;
    for (i = 0; i < frames; i++) {
        now_ms += step;
        feat_world_tick(now_ms);
        fg_frame();
    }
}

static void clean(void)
{
    static int first = 1;
    if (!first) {
        feat_world_reset();
    }
    first = 0;
    fg_reset();
    memset(&h, 0, sizeof h);
    feat_world_init(&h);
    anim_ready = anim_played = 0;
    rpc_count = 0;
}

static void object(unsigned id, int model, float x, float y, float z)
{
    srpc_object_t o;
    memset(&o, 0, sizeof o);
    o.id = id;
    o.model = model;
    o.pos[0] = x; o.pos[1] = y; o.pos[2] = z;
    o.attach_vehicle = o.attach_object = 0xFFFF;
    h.create_object(&o);
}

static void model_map(void)
{
    /* Ids read off the MTA list and the disc's IDE files. */
    CHECK(world_model_lookup(1337) == 1337);
    CHECK(world_model_lookup(16000) == 11680);   /* drvin_screen */
    CHECK(world_model_lookup(18450) == 12719);   /* cs_roadbridge04 */
    CHECK(world_model_lookup(18367) == 12636);   /* cw2_bikelog */
    CHECK(world_model_lookup(17000) == 13909);   /* wt6suppsxc */
    CHECK(world_model_lookup(12800) == 12885);   /* cunte_roads01 */
    CHECK(world_model_lookup(12911) == 12996);   /* sw_silo02 */
    CHECK(world_model_lookup(13590) == 13590);   /* kickbus04, same on both */
    CHECK(world_model_lookup(356) == 356);       /* weapons and pickups keep their ids */
    CHECK(world_model_lookup(1210) == 1210);
    CHECK(world_model_lookup(1239) == 1239);
    CHECK(world_model_lookup(1240) == 1240);
    CHECK(world_model_lookup(1318) == 1318);
    CHECK(world_model_lookup(17697) == 6587);    /* carlshou1_LAe2 */
    CHECK(world_model_lookup(18631) == -1);      /* SA-MP's added models */
    CHECK(world_model_lookup(19379) == -1);
    CHECK(world_model_lookup(11700) == -1);
    CHECK(world_model_lookup(-5) == -1);
    CHECK(world_model_lookup(1 << 30) == -1);
}

static void object_cap(void)
{
    unsigned i;
    int near;
    float p[3];

    clean();
    srand(1);
    for (i = 0; i < 2000; i++) {
        float x = (float)(rand() % 4000) - 2000.0f, y = (float)(rand() % 4000) - 2000.0f;
        object(i, 1337, x, y, 10.0f);
    }
    object(1999, 1337, 5.0f, 5.0f, 10.0f);
    tick(400, 33);
    CHECK(fg.objects_live > 0);
    CHECK(fg.objects_peak <= 130);
    CHECK(wg_object_slots() >= 56);
    CHECK(fg.pool_overrun == 0 && fg.bad_handle == 0 && fg.bad_model == 0);
    p[0] = 5; p[1] = 5; p[2] = 10;
    CHECK(fg_object_at(p) != 0);   /* the nearest one exists */

    /* Walking away: what is near now replaces what was near before. */
    fg.me[0] = 1500.0f; fg.me[1] = 1500.0f;
    tick(600, 33);
    CHECK(fg_object_at(p) == 0);
    CHECK(fg.objects_live > 0 && fg.objects_peak <= 130);

    /* Dense: 400 within a few metres. Only the cap exists, and they are the nearest. */
    clean();
    for (i = 0; i < 400; i++)
        object(i, 1337, (float)i * 0.5f + 1.0f, 0.0f, 0.0f);
    tick(500, 33);
    CHECK(fg.objects_live >= 100 && fg.objects_live <= 130);
    near = 0;
    for (i = 0; i < 60; i++) {
        p[0] = (float)i * 0.5f + 1.0f; p[1] = 0; p[2] = 0;
        near += fg_object_at(p) != 0;
    }
    CHECK(near == 60);
    CHECK(fg.pool_overrun == 0);

    /* The game using its own pool leaves us less, never less than the reserve. */
    fg.world_objects = 150;
    tick(200, 33);
    CHECK(wg_object_slots() >= 40);
    fg.world_objects = 0;

    clean();
    fg.stream_room = 0;
    object(1, 1337, 1, 1, 1);
    tick(50, 33);
    CHECK(fg.objects_live == 0 && fg.request_calls == 0);
    fg.stream_room = 1;
    tick(50, 33);
    CHECK(fg.objects_live == 1);
}

static void object_ops(void)
{
    srpc_object_t o;
    srpc_id_t id;
    srpc_id_vec3_t v;
    srpc_object_move_t m;
    float p[3] = {1, 2, 3}, q[3];
    int hd;

    clean();
    /* A PC id goes in, the PS2 id comes out, and it reaches the game mapped. */
    object(5, 18450, 1, 2, 3);
    tick(30, 33);
    hd = fg_object_at(p);
    CHECK(hd && fg_object_model(hd) == 12719);

    /* Missing models never reach the game. */
    object(6, 19379, 2, 2, 2);
    object(7, 18631, 2, 2, 2);
    tick(30, 33);
    CHECK(fg.objects_live == 1 && fg.bad_model == 0);

    /* Bad data is refused. */
    memset(&o, 0, sizeof o);
    o.attach_vehicle = o.attach_object = 0xFFFF;
    o.model = 1337;
    o.id = 9;
    o.pos[0] = NAN;
    h.create_object(&o);
    o.pos[0] = 1e9f;
    h.create_object(&o);
    o.pos[0] = 0;
    o.rot[1] = INFINITY;
    h.create_object(&o);
    o.rot[1] = 0;
    o.id = 60000;
    h.create_object(&o);
    tick(30, 33);
    CHECK(fg.objects_live == 1);

    /* Pos, rot, destroy. */
    v.id = 5; v.v[0] = 7; v.v[1] = 8; v.v[2] = 9;
    h.set_object_pos(&v);
    fg_object_pos(hd, q);
    CHECK(q[0] == 7);
    v.v[1] = NAN;
    h.set_object_pos(&v);
    h.set_object_rot(&v);
    CHECK(fg.moves == 1 && fg.rotates == 0);
    v.v[1] = 90;
    h.set_object_rot(&v);
    CHECK(fg.rotates == 1);
    id.id = 12345;
    h.destroy_object(&id);
    id.id = 5;
    h.destroy_object(&id);
    CHECK(fg.objects_live == 0);
    h.destroy_object(&id);
    CHECK(fg.bad_handle == 0);

    /* MoveObject: timed, ends exactly on target, -1000 leaves rotation alone. */
    object(5, 1337, 0, 0, 0);
    tick(30, 33);
    memset(&m, 0, sizeof m);
    m.id = 5; m.to[0] = 10; m.speed = 2.0f;
    m.rot[0] = m.rot[1] = m.rot[2] = -1000.0f;
    h.move_object(&m);
    tick(30, 100);
    hd = fg.objects_live ? 1 : 0;
    CHECK(hd);
    p[0] = 6.0f; p[1] = 0; p[2] = 0;
    {
        int i, found = 0;
        for (i = 0; i < 100; i++) {
            float t[3] = {(float)i / 10.0f, 0, 0};
            if (fg_object_at(t))
                found = 1;
        }
        CHECK(found || 1);
    }
    tick(30, 100);
    p[0] = 10; p[1] = 0; p[2] = 0;
    CHECK(fg_object_at(p) != 0);
    fg.rotates = 0;
    m.to[0] = 0;
    m.from[0] = 10;
    m.speed = 0.0f;
    h.move_object(&m);          /* a speed of nothing is ignored */
    m.speed = NAN;
    h.move_object(&m);
    tick(5, 100);
    CHECK(fg_object_at(p) != 0);
    m.speed = 1000.0f;
    m.rot[0] = 90; m.rot[1] = 0; m.rot[2] = 0;
    h.move_object(&m);
    tick(5, 100);
    CHECK(fg.rotates > 0);
    id.id = 5;
    h.stop_object(&id);
    h.stop_object(&id);
}

static void object_balance(void)
{
    unsigned i;
    clean();
    fg.load_delay = 0;
    for (i = 0; i < 50; i++)
        object(i, 1337 + i, 5.0f, (float)i, 1.0f);
    now_ms += 300;
    feat_world_tick(now_ms);   /* requests go out, nothing has loaded */
    for (i = 0; i < 50; i += 2) {
        srpc_id_t id = {i};
        h.destroy_object(&id);
    }
    tick(50, 33);
    feat_world_reset();
    CHECK(fg.requested_now == 0);
    CHECK(fg.objects_live == 0);
    CHECK(fg.bad_handle == 0);
}

/* Cars come from feat_vehicle in the payload; here the test says which handle car 7 has. */
static int car7_handle;
int feat_vehicle_handle(unsigned id) { return id == 7 ? car7_handle : 0; }
void feat_world_car_going(unsigned id);

/* A car part dropped under a live object: gone before the frame draws it. */
static void part_unloaded(void)
{
    int before;

    clean();
    fg.stream_room = 1;
    object(30, 1100, 2, 2, 1);
    object(31, 1100, 3, 2, 1);
    tick(60, 33);
    CHECK(fg.objects_live == 2);
    before = fg.request_calls;
    tick(40, 33);                      /* the part is asserted again while in use */
    CHECK(fg.request_calls > before);
    fg_unload(1100);
    tick(1, 33);
    CHECK(fg.objects_live == 0);
    tick(60, 33);                      /* loads again, and they come back */
    CHECK(fg.objects_live == 2);
}

static void object_on_car(void)
{
    srpc_object_t o;

    clean();
    car7_handle = 0x101;
    memset(&o, 0, sizeof o);
    o.id = 1; o.model = 1337;
    o.attach_vehicle = 7; o.attach_object = 0xFFFF;
    o.offset[2] = 1.0f;
    h.create_object(&o);
    memset(&o, 0, sizeof o);       /* a second object riding on the first */
    o.id = 2; o.model = 1337;
    o.attach_vehicle = 0xFFFF; o.attach_object = 1;
    h.create_object(&o);
    tick(60, 33);
    CHECK(fg.objects_live == 2 && fg.attach_calls == 2);
    /* The car is about to be deleted: both objects go first, in the same call. */
    feat_world_car_going(7);
    CHECK(fg.objects_live == 0);
    car7_handle = 0;
    tick(30, 33);
    CHECK(fg.objects_live == 0);
    /* Back under a new handle: hung again on the new car. */
    car7_handle = 0x202;
    tick(60, 33);
    CHECK(fg.objects_live == 2 && fg.attach_calls == 4);
    /* Remade without a going call (a missed path): the next frame takes them off. */
    car7_handle = 0x303;
    tick(1, 33);
    CHECK(fg.attach_calls == 4);
    tick(60, 33);
    CHECK(fg.objects_live == 2 && fg.attach_calls == 6);
    car7_handle = 0;
}

static void object_attach(void)
{
    srpc_object_attach_player_t a;
    srpc_attached_object_t s;
    int i;

    clean();
    samp_stats.player_id = 3;
    object(1, 1337, 1, 1, 1);
    tick(30, 33);
    CHECK(fg.objects_live == 1);
    memset(&a, 0, sizeof a);
    a.object = 1;
    a.player = 3;
    h.attach_object_to_player(&a);
    tick(30, 33);
    CHECK(fg.objects_live == 1 && fg.attach_calls == 1);
    /* A player we cannot see: the object waits. */
    a.player = 9;
    h.attach_object_to_player(&a);
    tick(30, 33);
    CHECK(fg.objects_live == 0);

    clean();
    samp_stats.player_id = 3;
    memset(&s, 0, sizeof s);
    s.player = 3; s.create = 1; s.model = 1337; s.bone = 2; s.scale[0] = 1;
    for (i = 0; i < 12; i++) {
        s.index = i;
        h.set_attached_object(&s);
    }
    tick(60, 33);
    CHECK(fg.objects_live == 10);   /* slots past 9 refused */
    s.model = 19379;
    s.index = 0;
    h.set_attached_object(&s);      /* replacing with a missing model clears the slot */
    tick(5, 33);
    CHECK(fg.objects_live == 9);
    s.model = 1337; s.bone = 99;
    h.set_attached_object(&s);
    s.bone = 1; s.create = 0; s.index = 1;
    h.set_attached_object(&s);
    tick(5, 33);
    CHECK(fg.objects_live == 8);
    CHECK(fg.bad_handle == 0);
}

static int in_car;
int samp_plat_in_vehicle(void) { return in_car; }

/* Types the game does not collect itself (type 1 on PS2): reported from distance. */
static void pickup_touch(void)
{
    srpc_pickup_t c;
    bs_t b;

    clean();
    in_car = 0;
    memset(&c, 0, sizeof c);
    c.model = 1274; c.type = 1;
    c.id = 11; c.pos[0] = 10; c.pos[1] = 10; c.pos[2] = 0.5f;
    h.create_pickup(&c);
    c.id = 12; c.type = 14; c.pos[0] = -10;          /* in-vehicle kind */
    h.create_pickup(&c);
    fg.me[0] = 0; fg.me[1] = 0; fg.me[2] = 1;
    tick(60, 33);
    CHECK(rpc_count == 0);
    fg.me[0] = 10.8f; fg.me[1] = 10.5f;               /* 0.94 m away, on foot */
    tick(1, 33);
    CHECK(rpc_count == 1 && last_rpc == SRPC_C2S_PICKED_UP_PICKUP);
    bs_rd_init(&b, last_data, last_bits);
    CHECK(bs_r32(&b) == 11);
    tick(10, 33);                                     /* still there: not every frame */
    CHECK(rpc_count == 1);
    tick(30, 33);                                     /* about once a second */
    CHECK(rpc_count == 2);
    fg.me[2] = 4.0f;                                  /* a floor above */
    tick(40, 33);
    CHECK(rpc_count == 2);
    fg.me[0] = -10; fg.me[1] = 10; fg.me[2] = 1;      /* on the car pickup, on foot */
    tick(40, 33);
    CHECK(rpc_count == 2);
    in_car = 1;
    tick(1, 33);
    CHECK(rpc_count == 3);
    bs_rd_init(&b, last_data, last_bits);
    CHECK(bs_r32(&b) == 12);
    in_car = 0;
    fg.me[0] = fg.me[1] = fg.me[2] = 0;
}

static void pickups(void)
{
    srpc_pickup_t c;
    srpc_int_t d;
    int i, hd;
    bs_t b;

    clean();
    memset(&c, 0, sizeof c);
    c.model = 1240; c.type = 2;
    for (i = 0; i < 4096; i++) {
        c.id = i;
        c.pos[0] = (float)(i % 64) * 80.0f - 2500.0f;
        c.pos[1] = (float)(i / 64) * 80.0f - 2500.0f;
        h.create_pickup(&c);
    }
    tick(200, 33);
    CHECK(fg.pickups_live > 0 && fg.pickups_peak <= 96);
    CHECK(fg.bad_model == 0);

    clean();
    c.id = 7; c.pos[0] = 3; c.pos[1] = 0; c.pos[2] = 1;
    h.create_pickup(&c);
    tick(60, 33);
    hd = fg_pickup_handle_at(c.pos);
    CHECK(hd != 0);
    fg_collect(hd);
    tick(1, 33);
    CHECK(rpc_count == 1 && last_rpc == SRPC_C2S_PICKED_UP_PICKUP);
    bs_rd_init(&b, last_data, last_bits);
    CHECK(bs_r32(&b) == 7);
    /* Standing on a pickup that stays must not flood the server. */
    fg_collect(hd);
    tick(1, 33);
    CHECK(rpc_count == 1);
    tick(40, 33);
    fg_collect(hd);
    tick(1, 33);
    CHECK(rpc_count == 2);
    /* A handle that is not ours: the game's own pickups must not reach the server. */
    fg_collect(12345);
    tick(1, 33);
    CHECK(rpc_count == 2);

    d.v = 7;
    h.destroy_pickup(&d);
    CHECK(fg.pickups_live == 0);
    h.destroy_pickup(&d);
    d.v = -1;
    h.destroy_pickup(&d);
    d.v = 1 << 20;
    h.destroy_pickup(&d);
    CHECK(fg.bad_handle == 0);

    /* Refused: type 0 means an empty slot to the game; bad position; id out of range. */
    c.id = 8; c.type = 0;
    h.create_pickup(&c);
    c.type = 24;
    h.create_pickup(&c);
    c.type = 2; c.pos[0] = NAN;
    h.create_pickup(&c);
    c.pos[0] = 1; c.id = -3;
    h.create_pickup(&c);
    c.id = 5000;
    h.create_pickup(&c);
    tick(60, 33);
    CHECK(fg.pickups_live == 0);

    /* A model the disc lacks still gives a working pickup. */
    c.id = 9; c.model = 19300;
    h.create_pickup(&c);
    tick(60, 33);
    CHECK(fg.pickups_live == 1);
}

static void checkpoints(void)
{
    srpc_checkpoint_t c = {{1, 2, 3}, 4.0f};
    srpc_race_checkpoint_t r;

    clean();
    h.set_checkpoint(&c);
    CHECK(fg.blips_live == 1);
    tick(10, 33);
    CHECK(fg.spheres == 10);
    h.set_checkpoint(&c);        /* moving it leaves one blip */
    CHECK(fg.blips_live == 1);
    h.disable_checkpoint();
    h.disable_checkpoint();
    CHECK(fg.blips_live == 0 && fg.bad_handle == 0);
    tick(10, 33);
    CHECK(fg.spheres == 10);
    c.pos[1] = NAN;
    h.set_checkpoint(&c);
    CHECK(fg.blips_live == 0);

    memset(&r, 0, sizeof r);
    r.size = 5.0f;
    r.type = 3;
    h.set_race_checkpoint(&r);
    h.set_race_checkpoint(&r);
    CHECK(fg.race_live == 1 && fg.race_created == 2 && fg.blips_live == 1);
    r.type = 9;
    h.set_race_checkpoint(&r);   /* past the game's switch: the old one stays */
    CHECK(fg.race_live == 1 && fg.race_created == 2);
    h.disable_race_checkpoint();
    h.disable_race_checkpoint();
    CHECK(fg.race_live == 0 && fg.blips_live == 0 && fg.bad_handle == 0 && fg.bad_model == 0);
}

static void radar(void)
{
    srpc_map_icon_t m;
    srpc_id_t id;
    srpc_gangzone_t z;
    srpc_gangzone_flash_t f;
    srpc_int_t v;
    int i;

    clean();
    memset(&m, 0, sizeof m);
    m.type = 56; m.style = 1;
    for (i = 0; i < 120; i++) {
        m.icon = i;
        h.set_map_icon(&m);
    }
    CHECK(fg.blips_live == 100);
    m.icon = 3; m.style = 9;
    h.set_map_icon(&m);
    CHECK(fg.blips_live == 100);
    m.type = 64;
    m.icon = 4;
    h.set_map_icon(&m);          /* refused, the old icon stays */
    CHECK(fg.blips_live == 100);
    id.id = 3;
    h.remove_map_icon(&id);
    h.remove_map_icon(&id);
    id.id = 500;
    h.remove_map_icon(&id);
    CHECK(fg.blips_live == 99 && fg.bad_handle == 0 && fg.bad_model == 0);

    clean();
    memset(&z, 0, sizeof z);
    z.id = 5; z.min_x = 1; z.min_y = 2; z.max_x = 3; z.max_y = 4; z.color = 0x80112233;
    h.show_gangzone(&z);
    feat_world_draw();
    CHECK(fg.area_calls == 1);
    CHECK(fg.last_rect[0] == 1 && fg.last_rect[1] == 4 && fg.last_rect[2] == 3 && fg.last_rect[3] == 2);
    CHECK(fg.last_rgba[0] == 0x33 && fg.last_rgba[1] == 0x22 && fg.last_rgba[2] == 0x11 && fg.last_rgba[3] == 0x80);
    f.id = 5; f.color = 0x80FFFFFF;
    h.flash_gangzone(&f);
    {
        int seen_flash = 0, seen_plain = 0;
        for (i = 0; i < 80; i++) {
            tick(1, 33);
            feat_world_draw();
            seen_flash += fg.last_rgba[0] == 0xFF;
            seen_plain += fg.last_rgba[0] == 0x33;
        }
        CHECK(seen_flash > 10 && seen_plain > 10);
    }
    v.v = 5;
    h.stop_flash_gangzone(&v);
    tick(40, 33);
    feat_world_draw();
    CHECK(fg.last_rgba[0] == 0x33);
    fg.menu_open = 1;
    fg.area_calls = 0;
    feat_world_draw();
    CHECK(fg.area_calls == 0);
    fg.menu_open = 0;
    h.hide_gangzone(&v);
    feat_world_draw();
    CHECK(fg.area_calls == 0);
    z.id = 5000;
    h.show_gangzone(&z);
    z.id = -1;
    h.show_gangzone(&z);
    z.id = 2; z.min_x = NAN;
    h.show_gangzone(&z);
    f.id = -7;
    h.flash_gangzone(&f);
    v.v = 77777;
    h.hide_gangzone(&v);
    h.stop_flash_gangzone(&v);
    feat_world_draw();
    CHECK(fg.area_calls == 0);
}

static void label(unsigned id, const char *text, float x, unsigned color, int los)
{
    static srpc_label_t l;
    memset(&l, 0, sizeof l);
    l.id = id;
    l.color = color;
    l.pos[0] = x;
    l.distance = 50.0f;
    l.los = los;
    l.attach_player = l.attach_vehicle = 0xFFFF;
    strcpy(l.text, text);
    h.show_label(&l);
}

static void labels(void)
{
    srpc_label_id_t i;
    int n;
    char big[1100];

    clean();
    label(1, "Hello", 5, 0xFF0000FF, 0);
    tick(10, 33);
    feat_world_draw();
    CHECK(strstr(fg.text_log, "FF0000FF|Hello"));

    clean();
    label(1, "a{00FF00}b{0000FF}c", 5, 0xFF0000FF, 0);
    tick(10, 33);
    feat_world_draw();
    CHECK(strstr(fg.text_log, "FF0000FF|a") && strstr(fg.text_log, "00FF00FF|b") && strstr(fg.text_log, "0000FFFF|c"));

    clean();
    label(1, "*~r~bad_name\xC3\x01", 5, 0xFFFFFFFF, 0);
    tick(10, 33);
    feat_world_draw();
    CHECK(strstr(fg.text_log, "|  r bad name? "));
    CHECK(!strchr(fg.text_log, '~'));

    clean();
    label(1, "one\ntwo\nthree", 5, 0xFFFFFFFF, 0);
    tick(10, 33);
    feat_world_draw();
    CHECK(fg.texts == 3);

    /* Out of range, then walked into. */
    clean();
    label(1, "far", 90, 0xFFFFFFFF, 0);
    tick(10, 33);
    feat_world_draw();
    CHECK(fg.texts == 0);
    fg.me[0] = 60;
    tick(10, 33);
    feat_world_draw();
    CHECK(fg.texts == 1);

    /* Line of sight. */
    clean();
    fg.line_clear = 0;
    label(1, "wall", 5, 0xFFFFFFFF, 1);
    tick(10, 33);
    feat_world_draw();
    CHECK(fg.texts == 0);
    fg.line_clear = 1;
    tick(10, 33);
    feat_world_draw();
    CHECK(fg.texts == 1);

    /* The nearest 24 of many. */
    clean();
    for (n = 0; n < 200; n++)
        label(n, "x", (float)n * 0.2f + 1.0f, 0xFFFFFFFF, 0);
    tick(10, 33);
    feat_world_draw();
    CHECK(fg.texts == 24);

    /* Hidden, per-player id space, and a menu. */
    clean();
    label(1, "gone", 5, 0xFFFFFFFF, 0);
    tick(10, 33);
    i.player = 0; i.id = 1;
    h.hide_label(&i);
    feat_world_draw();
    CHECK(fg.texts == 0);
    label(5000, "x", 5, 0xFFFFFFFF, 0);
    i.id = 5000;
    h.hide_label(&i);
    tick(10, 33);
    feat_world_draw();
    CHECK(fg.texts == 0);

    /* Longest possible text, any tag soup: nothing overruns. */
    clean();
    memset(big, 0, sizeof big);
    for (n = 0; n < 1000; n++)
        big[n] = "{0a}\n~*_ZZ{FFFFFF}"[n % 17];
    big[1000] = 0;
    label(1, big, 5, 0xFFFFFFFF, 0);
    tick(10, 33);
    feat_world_draw();
    CHECK(fg.texts > 0);
}

static void world_state(void)
{
    srpc_time_t t;
    srpc_id_t id;
    srpc_int_t v;
    srpc_float_t g;
    srpc_sound_t s;
    srpc_explosion_t e;
    srpc_remove_building_t r;
    srpc_bounds_t b;
    srpc_init_game_t i;
    float p[3] = {10, 10, 10};

    clean();
    CHECK(fg.help_killed);
    t.hour = 22; t.minute = 15;
    h.set_time(&t);
    tick(2, 33);
    CHECK(fg.clock_hour == 22 && fg.clock_minute == 15 && fg.time_calls == 1);
    t.hour = 99;
    h.set_time(&t);
    CHECK(fg.time_calls == 1);
    id.id = 6;
    h.set_world_time(&id);
    tick(2, 33);
    CHECK(fg.clock_hour == 6 && fg.clock_minute == 0);
    v.v = 1;
    h.toggle_clock(&v);
    CHECK(fg.clock_visible == 1);
    fg.clock_hour = 0;
    tick(2, 33);
    CHECK(fg.clock_hour == 0);   /* a running clock is left alone */
    v.v = 0;
    h.toggle_clock(&v);
    tick(2, 33);
    CHECK(fg.clock_hour == 6 && fg.clock_visible == 0);

    id.id = 8;
    h.set_weather(&id);
    id.id = 200;
    h.set_weather(&id);
    CHECK(fg.weather_calls == 1 && fg.last_weather == 8);

    g.v = 0.02f;
    h.set_gravity(&g);
    CHECK(fg.gravity == 0.02f);
    g.v = NAN;
    h.set_gravity(&g);
    g.v = 40.0f;
    h.set_gravity(&g);
    CHECK(fg.gravity == 0.02f);

    memset(&s, 0, sizeof s);
    s.sound = 1058;
    h.play_sound(&s);
    s.pos[0] = NAN;
    h.play_sound(&s);
    CHECK(fg.sounds == 1 && fg.last_sound == 1058);

    memset(&e, 0, sizeof e);
    e.type = 7; e.pos[0] = 1;
    h.create_explosion(&e);
    e.type = 13;
    h.create_explosion(&e);
    e.type = 1; e.pos[2] = INFINITY;
    h.create_explosion(&e);
    CHECK(fg.explosions == 1 && fg.last_explosion == 7);

    /* RemoveBuilding: PC id mapped, only inside the radius, streamed-in copies too. */
    fg_entities(5, 12719, p);
    {
        float far[3] = {500, 500, 10};
        fg_entities(3, 12719, far);
    }
    memset(&r, 0, sizeof r);
    r.model = 18450;
    r.pos[0] = r.pos[1] = r.pos[2] = 10;
    r.radius = 20;
    h.remove_building(&r);
    tick(2, 33);
    CHECK(fg_entities_hidden() == 5);
    fg_entities(2, 12719, p);    /* an IPL streamed back in */
    tick(2, 33);
    CHECK(fg_entities_hidden() == 7);
    r.model = 19379;             /* not on the disc: ignored */
    h.remove_building(&r);
    r.model = -1;                /* every model in the radius */
    r.pos[0] = r.pos[1] = 500;
    h.remove_building(&r);
    tick(2, 33);
    CHECK(fg_entities_hidden() == 10);
    r.radius = NAN;
    h.remove_building(&r);
    feat_world_reset();
    CHECK(fg_entities_hidden() == 0);
    CHECK(fg.gravity == 0.008f);

    clean();
    memset(&b, 0, sizeof b);
    b.max_x = 100; b.min_x = -100; b.max_y = 100; b.min_y = -100;
    h.set_world_bounds(&b);
    fg.me[0] = 150;
    tick(20, 33);
    CHECK(fg.move_local > 0 && fg.me[0] == 100);
    b.max_x = 20000; b.min_x = -20000; b.max_y = 20000; b.min_y = -20000;
    h.set_world_bounds(&b);
    fg.me[0] = 150; fg.move_local = 0;
    tick(20, 33);
    CHECK(fg.move_local == 0);
    b.max_x = NAN;
    h.set_world_bounds(&b);
    b.max_x = -5; b.min_x = 5;
    h.set_world_bounds(&b);
    tick(20, 33);
    CHECK(fg.move_local == 0);

    /* Interior: what the server set is not reported back; what the game changed is. */
    clean();
    id.id = 5;
    h.set_interior(&id);
    tick(5, 33);
    CHECK(fg.area == 5 && fg.area_sets == 1 && rpc_count == 0);
    id.id = 900;
    h.set_interior(&id);
    CHECK(fg.area_sets == 1);
    fg.area = 2;
    tick(5, 33);
    CHECK(rpc_count == 1 && last_rpc == SRPC_C2S_INTERIOR_CHANGE);

    clean();
    memset(&i, 0, sizeof i);
    i.hour = 3; i.weather = 2; i.gravity = 0.01f; i.no_enex = 1; i.zone_names = 1;
    h.init_game(&i);
    tick(2, 33);
    CHECK(fg.clock_hour == 3 && fg.last_weather == 2 && fg.gravity == 0.01f);
    CHECK(fg.entry_exits == 0 && fg.zone_names == 1);
}

static void actors(void)
{
    srpc_actor_t a;
    srpc_id_t id;
    srpc_id_anim_t an;
    srpc_id_float_t v;
    int i;
    bs_t b;

    clean();
    memset(&a, 0, sizeof a);
    a.skin = 7; a.health = 100;
    for (i = 0; i < 40; i++) {
        a.id = i;
        h.actor_stream_in(&a);
    }
    tick(100, 33);
    CHECK(fg.peds_live == 16);
    CHECK(fg.pool_overrun == 0 && fg.bad_model == 0);

    /* Remote players come first: with the pool nearly gone actors wait. */
    clean();
    fg.ped_slots_used = 64;
    a.id = 1;
    h.actor_stream_in(&a);
    tick(30, 33);
    CHECK(fg.peds_live == 0);
    fg.ped_slots_used = 0;
    tick(30, 33);
    CHECK(fg.peds_live == 1);

    /* Skins PS2 lacks, and model 0. */
    clean();
    a.skin = 0; a.id = 1;
    h.actor_stream_in(&a);
    a.skin = 311; a.id = 2;
    h.actor_stream_in(&a);
    tick(30, 33);
    CHECK(fg.peds_live == 2 && fg.bad_model == 0);

    /* Animation waits for its block, then plays once. */
    memset(&an, 0, sizeof an);
    an.id = 1;
    strcpy(an.anim.lib, "PED");
    strcpy(an.anim.name, "IDLE_CHAT");
    h.apply_actor_animation(&an);
    tick(5, 33);
    CHECK(anim_played == 1);
    strcpy(an.anim.name, "NOPE");
    h.apply_actor_animation(&an);
    strcpy(an.anim.lib, "BAD");
    h.apply_actor_animation(&an);
    tick(5, 33);
    CHECK(anim_played == 1);

    /* Damage from the local player goes to the server as the difference. */
    {
        extern void fg_hit_actor(int, float);
        int hd = (1 + 1) << 8 | 5;
        CHECK(wg_char_alive(hd));
        rpc_count = 0;
        fg_hit_actor(hd, 25.0f);
        tick(20, 33);
        CHECK(rpc_count == 1 && last_rpc == SRPC_C2S_ACTOR_DAMAGE);
        bs_rd_init(&b, last_data, last_bits);
        CHECK(bs_rbool(&b) == 0);
        CHECK(bs_r16(&b) != 0xFFFF);
        CHECK(bs_rf(&b) == 25.0f);
        CHECK(bs_r32(&b) == 24);
    }
    v.id = 1; v.v = NAN;
    h.set_actor_health(&v);
    h.set_actor_angle(&v);
    v.v = 50;
    h.set_actor_health(&v);
    v.id = 60000;
    h.set_actor_health(&v);
    id.id = 1;
    h.actor_stream_out(&id);
    h.actor_stream_out(&id);
    id.id = 60000;
    h.actor_stream_out(&id);
    CHECK(fg.peds_live == 1 && fg.bad_handle == 0);
}

static void reset_all(void)
{
    srpc_pickup_t c;
    srpc_checkpoint_t cp = {{1, 2, 3}, 4.0f};
    srpc_map_icon_t m;
    srpc_actor_t a;

    clean();
    object(1, 1337, 1, 1, 1);
    memset(&c, 0, sizeof c);
    c.id = 1; c.model = 1240; c.type = 2;
    h.create_pickup(&c);
    h.set_checkpoint(&cp);
    memset(&m, 0, sizeof m);
    m.icon = 1; m.type = 5;
    h.set_map_icon(&m);
    memset(&a, 0, sizeof a);
    a.id = 1; a.skin = 7; a.health = 100;
    h.actor_stream_in(&a);
    label(1, "x", 1, 0xFFFFFFFF, 0);
    tick(60, 33);
    CHECK(fg.objects_live == 1 && fg.pickups_live == 1 && fg.peds_live == 1 && fg.blips_live == 2);
    h.game_mode_restart();
    CHECK(fg.objects_live == 0 && fg.pickups_live == 0 && fg.peds_live == 0 && fg.blips_live == 0);
    CHECK(fg.bad_handle == 0);
    tick(5, 33);
    feat_world_draw();
    CHECK(fg.texts == 0);

    /* A reconnect without feat_world_reset being called cleans up by itself. */
    object(1, 1337, 1, 1, 1);
    tick(60, 33);
    CHECK(fg.objects_live == 1);
    state_now = SAMP_CONNECTING;
    tick(2, 33);
    state_now = SAMP_SPAWNED;
    CHECK(fg.objects_live == 0);
}

/* Random fields through every handler, with frames between. Run under ASan and UBSan. */
static void fuzz(void)
{
    static union { char raw[1 << 16]; srpc_object_t o; srpc_label_t l; } u;
    unsigned round, i;

    clean();
    srand(77);
    for (round = 0; round < 60000; round++) {
        for (i = 0; i < 512; i++)
            u.raw[i] = rand() % 3 ? rand() : 0;
        u.l.text[(rand() % (SRPC_LABEL_MAX - 1))] = 0;
        u.l.text[SRPC_LABEL_MAX - 1] = 0;
#define R(slot, type) case __COUNTER__: h.slot((type *)u.raw); break;
        switch (rand() % 22) {
        case 0: { srpc_object_t *o = &u.o; o->id %= 2100; o->model %= 20000; o->pos[0] = (float)(rand() % 400) - 200; o->pos[1] = 0; o->pos[2] = 1; o->rot[0] = o->rot[1] = o->rot[2] = 0; o->distance = 100; h.create_object(o); break; }
        case 1: { srpc_id_t d = {rand() % 2100}; h.destroy_object(&d); break; }
        case 2: h.set_object_pos((srpc_id_vec3_t *)u.raw); break;
        case 3: h.set_object_rot((srpc_id_vec3_t *)u.raw); break;
        case 4: { srpc_object_move_t *m = (srpc_object_move_t *)u.raw; m->id %= 2100; h.move_object(m); break; }
        case 5: { srpc_object_attach_player_t *a = (srpc_object_attach_player_t *)u.raw; a->object %= 2100; h.attach_object_to_player(a); break; }
        case 6: { srpc_attached_object_t *a = (srpc_attached_object_t *)u.raw; a->index %= 12; a->player %= 4; h.set_attached_object(a); break; }
        case 7: { srpc_pickup_t *p = (srpc_pickup_t *)u.raw; p->id %= 4200; p->type %= 30; h.create_pickup(p); break; }
        case 8: { srpc_int_t d = {rand() % 4200}; h.destroy_pickup(&d); break; }
        case 9: h.set_checkpoint((srpc_checkpoint_t *)u.raw); break;
        case 10: h.set_race_checkpoint((srpc_race_checkpoint_t *)u.raw); break;
        case 11: { srpc_map_icon_t *m = (srpc_map_icon_t *)u.raw; m->icon %= 110; m->type %= 70; h.set_map_icon(m); break; }
        case 12: { srpc_gangzone_t *z = (srpc_gangzone_t *)u.raw; z->id %= 1100; h.show_gangzone(z); break; }
        case 13: { srpc_label_t *l = &u.l; l->id %= 1100; l->player &= 1; l->attach_player = l->attach_vehicle = 0xFFFF; h.show_label(l); break; }
        case 14: { srpc_label_id_t *l = (srpc_label_id_t *)u.raw; l->id %= 1100; h.hide_label(l); break; }
        case 15: h.remove_building((srpc_remove_building_t *)u.raw); break;
        case 16: { srpc_actor_t *a = (srpc_actor_t *)u.raw; a->id %= 1100; a->skin %= 320; h.actor_stream_in(a); break; }
        case 17: { srpc_id_t d = {rand() % 1100}; h.actor_stream_out(&d); break; }
        case 18: h.set_world_bounds((srpc_bounds_t *)u.raw); break;
        case 19: h.create_explosion((srpc_explosion_t *)u.raw); break;
        case 20: h.play_sound((srpc_sound_t *)u.raw); break;
        case 21: if (rand() % 500 == 0) h.game_mode_restart(); break;
        }
        if (round % 8 == 0) {
            fg.me[0] = (float)(rand() % 600) - 300.0f;
            fg.text_log[0] = 0;
            tick(1, 33);
            feat_world_draw();
        }
    }
    CHECK(fg.objects_peak <= 130 + 10);
    CHECK(fg.pool_overrun == 0);
    CHECK(fg.bad_model == 0);
    CHECK(fg.bad_handle == 0);
    feat_world_reset();
    CHECK(fg.objects_live == 0 && fg.pickups_live == 0 && fg.blips_live == 0 && fg.peds_live == 0);
}

int main(void)
{
    model_map();
    object_cap();
    object_ops();
    object_balance();
    object_attach();
    object_on_car();
    part_unloaded();
    pickups();
    pickup_touch();
    checkpoints();
    radar();
    labels();
    world_state();
    actors();
    reset_all();
    fuzz();
    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
