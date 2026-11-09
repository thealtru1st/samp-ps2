/* Host tests for feat_vehicle.c. The file is included so the tests can see its counters;
 * the game is fake_game.c, and the wire format is the real samp_rpc.c and bitstream.c.
 * Built with ASan and UBSan by run_tests.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include "fake_game.h"
#include "../../payload/feat_vehicle.c"

samp_stats_t samp_stats;
static int state = SAMP_SPAWNED;
int samp_state(void) { return state; }

void slog(const char *f, ...) { (void)f; }

#define OUT_MAX 512
typedef struct { int rpc, id, bits; unsigned char d[128]; } sent_t;
static sent_t sent[OUT_MAX];
static int n_sent;

static void capture(int rpc, int id, const void *d, int bits)
{
    sent_t *s = &sent[n_sent < OUT_MAX ? n_sent++ : OUT_MAX - 1];
    s->rpc = rpc;
    s->id = id;
    s->bits = bits;
    memcpy(s->d, d, (bits + 7) / 8 > 128 ? 128 : (bits + 7) / 8);
}

int rak_send(const void *d, int bits, int rel, int ch)
{
    (void)rel; (void)ch;
    capture(0, ((const unsigned char *)d)[0], d, bits);
    return 0;
}

int rak_rpc(int id, const void *d, int bits, int rel, int ch)
{
    (void)rel; (void)ch;
    capture(1, id, d, bits);
    return 0;
}

static int remote_handles[PLAYER_IDS], remote_held[PLAYER_IDS];
int game_remote_handle(unsigned id) { return id < PLAYER_IDS ? remote_handles[id] : 0; }
void game_remote_hold(unsigned id, int on) { remote_held[id] = on; }

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __func__, __LINE__, #c); } } while (0)
#define NEAR(a, b, e) (fabsf((a) - (b)) <= (e))

static unsigned clock_ms = 1000;

static void frames(int n)
{
    while (n--) {
        clock_ms += 33;
        fake_frame();
        feat_vehicle_tick(clock_ms);
    }
}

static void wait_ms(unsigned ms) { frames(ms / 33 + 1); }

static int count_sent(int rpc, int id)
{
    int i, n = 0;
    for (i = 0; i < n_sent; i++)
        if (sent[i].rpc == rpc && sent[i].id == id)
            n++;
    return n;
}

static const sent_t *last_sent(int rpc, int id)
{
    int i;
    for (i = n_sent - 1; i >= 0; i--)
        if (sent[i].rpc == rpc && sent[i].id == id)
            return &sent[i];
    return 0;
}

static samp_rpc_handlers_t handlers;

static void reset_all(void)
{
    feat_vehicle_reset();
    fake_init();
    memset(remote_handles, 0, sizeof remote_handles);
    memset(remote_held, 0, sizeof remote_held);
    memset(fake_local_at, 0, sizeof fake_local_at);
    memset(&fake_pad, 0, sizeof fake_pad);
    n_sent = 0;
    state = SAMP_SPAWNED;
    samp_stats.player_id = 5;
    n_models = n_live = n_loading = 0;
    memset(model_use, 0, sizeof model_use);
    wait_ms(10);
}

static void stream_in(unsigned id, unsigned model, float x, float y)
{
    srpc_vehicle_t v;
    memset(&v, 0, sizeof v);
    v.id = id;
    v.model = model;
    v.pos[0] = x;
    v.pos[1] = y;
    v.pos[2] = 10;
    v.angle = 90;
    v.color1 = 3;
    v.color2 = 6;
    v.health = 1000;
    v.body_color1 = v.body_color2 = -1;
    on_stream_in(&v);
}

static fake_car_t *car_of(unsigned id)
{
    return V[id].state == R_LIVE ? fake_car(V[id].handle) : 0;
}

static void test_math(void)
{
    float m[16] = {0}, q[4], m2[16] = {0};
    float h;
    int i, n;

    /* Heading 90 means facing -x: right is +y and forward is -x. */
    m[0] = 0; m[1] = 1; m[2] = 0;
    m[4] = -1; m[5] = 0; m[6] = 0;
    m[8] = 0; m[9] = 0; m[10] = 1;
    vm_mat_to_quat(m, q);
    CHECK(NEAR(q[0], 0.70710678f, 1e-5f));
    CHECK(NEAR(q[3], -0.70710678f, 1e-5f)); /* the wire is the conjugate */
    vm_quat_to_mat(q, m2);
    for (i = 0; i < 11; i++)
        if (i % 4 != 3)
            CHECK(NEAR(m[i], m2[i], 1e-5f));
    for (n = 0; n < 20000; n++) {
        float a[4], back[4], d;
        for (i = 0; i < 4; i++)
            a[i] = (float)rand() / RAND_MAX * 2 - 1;
        if (!vm_quat_norm(a))
            continue;
        vm_quat_to_mat(a, m);
        vm_mat_to_quat(m, back);
        d = fabsf(a[0] * back[0] + a[1] * back[1] + a[2] * back[2] + a[3] * back[3]);
        CHECK(d > 0.999f);
    }
    /* the server's limits on what it will accept in 209 */
    for (n = 0; n < 200000; n++) {
        float r[3], f[3];
        for (i = 0; i < 3; i++) {
            r[i] = (float)rand() / RAND_MAX * 2 - 1;
            f[i] = (float)rand() / RAND_MAX * 2 - 1;
        }
        if (!vm_orthonormal(r, f))
            continue;
        if (fabsf(1.0f - sqrtf(vm_dot3(r, r))) >= 0.000001f ||
            fabsf(1.0f - sqrtf(vm_dot3(f, f))) >= 0.000001f || fabsf(vm_dot3(r, f)) >= 0.000001f) {
            CHECK(!"orthonormal outside the server's limit");
            break;
        }
    }
    h = 0;
    union { unsigned u; float f; } bad[3];
    bad[0].u = 0x7F800000; bad[1].u = 0xFF800000; bad[2].u = 0x7FC00000;
    for (i = 0; i < 3; i++) {
        CHECK(!vm_finite(bad[i].f));
        CHECK(!vm_vec_ok(&bad[i].f, 1, 1e30f));
    }
    CHECK(vm_vec_ok(&h, 1, 1.0f));
}

static void test_cap(void)
{
    int i, pool_ok = 1;

    reset_all();
    /* 300 cars of 30 models in a line, one every 2 units */
    for (i = 1; i <= 300; i++)
        stream_in(i, 400 + i % 30, i * 2.0f, 0);
    for (i = 0; i < 400; i++) {
        frames(5);
        if (fake_stats.live > MAX_LIVE || fake_stats.models_loaded > MAX_MODELS)
            pool_ok = 0;
    }
    CHECK(pool_ok);
    CHECK(fake_stats.live_peak <= MAX_LIVE);
    CHECK(fake_stats.models_peak <= MAX_MODELS);
    CHECK(fake_stats.live > 8);
    /* all live cars are near: player at x=0 */
    for (i = 1; i <= 300; i++)
        if (V[i].state == R_LIVE)
            CHECK(V[i].pos[0] < 340);
    /* walk to the far end and the cars must follow */
    fake_local_at[0] = 600;
    wait_ms(60000);
    CHECK(fake_stats.live_peak <= MAX_LIVE);
    {
        int near = 0, far = 0;
        for (i = 1; i <= 300; i++) {
            if (V[i].state != R_LIVE) continue;
            if (V[i].pos[0] > 400) near++; else far++;
        }
        CHECK(near > 8);
        CHECK(far == 0);
    }
    /* leaving everything behind empties the game */
    fake_local_at[0] = 5000;
    wait_ms(30000);
    CHECK(fake_stats.live == 0);
    CHECK(fake_stats.models_loaded == 0);
    CHECK(n_live == 0 && n_models == 0 && n_loading == 0);
}

static void test_state_kept(void)
{
    srpc_vehicle_damage_t d = {7, 0x3311, 0x02000104, 0x45, 0xA};
    srpc_vehicle_params_t p;
    srpc_plate_t pl;
    srpc_scm_event_t ev;
    srpc_vehicle_t v;
    fake_car_t *c;

    reset_all();
    memset(&v, 0, sizeof v);
    v.id = 7; v.model = 411; v.pos[2] = 10; v.health = 800; v.color1 = 1; v.color2 = 2;
    v.body_color1 = 44; v.body_color2 = 55; v.interior = 3; v.paintjob = 2;
    v.mods[0] = 1010; v.mods[1] = 1192; v.mods[2] = 999; v.panel_damage = 0x10; v.tyre_damage = 1;
    on_stream_in(&v);
    memset(&pl, 0, sizeof pl);
    pl.id = 7;
    strcpy(pl.plate, "ab_c-12345");
    on_plate(&pl);
    wait_ms(1500);
    c = car_of(7);
    CHECK(c != 0);
    if (!c)
        return;
    CHECK(!strcmp(c->plate, "AB C 123"));
    CHECK(c->c1 == 44 && c->c2 == 55);
    CHECK(c->health == 800);
    CHECK(c->interior == 3);
    CHECK(c->paintjob == 2); /* game index 1, fake stores index + 1 */
    CHECK(c->panels == 0x10 && c->tyres == 1);
    CHECK(c->n_mods == 1 && c->mods[0] == 1010); /* 1192 refused, 999 dropped on the wire */
    on_damage(&d);
    CHECK(c->panels == 0x3311 && c->doors == 0x02000104 && c->lights_dmg == 0x45 && c->tyres == 0xA);
    memset(&p, UNSET, sizeof p);
    p.id = 7; p.engine = 1; p.doors = 1; p.lights = 0; p.objective = 1; p.bonnet = 1; p.door[2] = 1;
    on_params(&p);
    CHECK(c->engine == 1 && c->locked == 1 && c->lights == 1 && c->door[0] == 1 && c->door[4] == 1 && c->door[1] == 0);
    CHECK(fake_stats.blips == 1);
    p.objective = 0;
    on_params(&p);
    CHECK(fake_stats.blips == 0);
    memset(&ev, 0, sizeof ev);
    ev.event = SCM_COLOUR; ev.vehicle = 7; ev.arg1 = 9; ev.arg2 = 10;
    on_scm_event(&ev);
    CHECK(c->c1 == 9 && c->c2 == 10);
    ev.event = SCM_ADD_MOD; ev.arg1 = 1087;
    on_scm_event(&ev);
    wait_ms(500);
    CHECK(c->n_mods == 2 && c->mods[1] == 1087);
    ev.event = SCM_PAINTJOB; ev.arg1 = 0;
    on_scm_event(&ev);
    CHECK(c->paintjob == 1);
    ev.arg1 = 3;
    on_scm_event(&ev);
    CHECK(c->paintjob == 0);
    /* out of range and back: everything comes back */
    c->s.pos[0] = 12;
    c->health = 555;
    fake_local_at[0] = 2000;
    wait_ms(1000);
    CHECK(V[7].state == R_RECORD);
    CHECK(V[7].health == 555 && NEAR(V[7].pos[0], 12, 0.01f));
    fake_local_at[0] = 0;
    wait_ms(1500);
    c = car_of(7);
    CHECK(c != 0);
    if (c) {
        CHECK(c->health == 555 && c->panels == 0x3311 && c->tyres == 0xA);
        CHECK(c->c1 == 9);
        CHECK(!strcmp(c->plate, "AB C 123"));
        CHECK(c->n_mods == 2);
    }
    /* repair */
    if (c) {
        srpc_vehicle_damage_t z = {7, 0, 0, 0, 0};
        srpc_id_float_t h = {7, 1000};
        on_damage(&z);
        on_health(&h);
        CHECK(c->repairs == 1 && c->panels == 0 && c->health == 1000);
    }
    /* a plate change on a live car rebuilds it */
    memset(&pl, 0, sizeof pl);
    pl.id = 7;
    strcpy(pl.plate, "NEW");
    on_plate(&pl);
    wait_ms(1500);
    c = car_of(7);
    CHECK(c && !strcmp(c->plate, "NEW"));
}

static void test_local(void)
{
    srpc_put_in_vehicle_t put = {9, 0};
    fake_car_t *c;
    fake_ped_t *me;
    bs_t b;
    float f;

    reset_all();
    me = fake_ped(fake_local);
    stream_in(9, 411, 5, 0);
    stream_in(10, 411, 8, 0);
    /* PutPlayerInVehicle right behind the stream-in, before the model has loaded */
    on_put_in(&put);
    CHECK(V[9].state != R_LIVE);
    wait_ms(1000);
    c = car_of(9);
    CHECK(c != 0);
    if (!c)
        return;
    CHECK(me->car == c->handle && c->driver == fake_local);
    CHECK(feat_vehicle_in_car());
    CHECK(fake_stats.camera == 1);
    CHECK(count_sent(1, SRPC_C2S_ENTER_VEHICLE) == 0); /* not when the server did it */

    /* driving: packets flow at the rate and carry the car's state */
    c->s.pos[0] = 44; c->s.pos[1] = 55; c->s.pos[2] = 6;
    c->s.vel[0] = 0.5f;
    c->health = 777;
    fake_pad.cross = 1; fake_pad.r1 = 1; fake_pad.lx = -128;
    n_sent = 0;
    wait_ms(1000);
    CHECK(count_sent(0, SRPC_sync_vehicle) >= 15 && count_sent(0, SRPC_sync_vehicle) <= 25);
    {
        const sent_t *s = last_sent(0, SRPC_sync_vehicle);
        unsigned lr, ud, keys;
        CHECK(s != 0);
        if (s) {
            bs_rd_init(&b, s->d, s->bits);
            CHECK(bs_r8(&b) == SRPC_sync_vehicle);
            CHECK(bs_r16(&b) == 9);
            lr = bs_r16(&b); ud = bs_r16(&b); keys = bs_r16(&b);
            CHECK(lr == 0xFF80 && ud == 0);
            CHECK(keys == (K_SPRINT | K_HANDBRAKE));
            bs_rf(&b); bs_rf(&b); bs_rf(&b); bs_rf(&b);
            f = bs_rf(&b);
            CHECK(f == 44);
            bs_rf(&b); bs_rf(&b);
            f = bs_rf(&b);
            CHECK(f == 0.5f);
            bs_rf(&b); bs_rf(&b);
            f = bs_rf(&b);
            CHECK(f == 777);
            CHECK(bs_r8(&b) == 100);
            CHECK(bs_r8(&b) == 50);
            CHECK(!b.err);
        }
    }

    /* damage reports only on change */
    n_sent = 0;
    wait_ms(1000);
    CHECK(count_sent(1, SRPC_C2S_VEHICLE_DAMAGE) == 0);
    c->panels = 0x30;
    wait_ms(1200);
    CHECK(count_sent(1, SRPC_C2S_VEHICLE_DAMAGE) == 1);

    /* a server order never comes back as a report */
    {
        srpc_vehicle_damage_t d = {9, 0x33, 0, 0, 0};
        on_damage(&d);
        n_sent = 0;
        wait_ms(1200);
        CHECK(count_sent(1, SRPC_C2S_VEHICLE_DAMAGE) == 0);
    }

    /* engine switched off by script stays off */
    {
        srpc_vehicle_params_t p;
        memset(&p, UNSET, sizeof p);
        p.id = 9; p.engine = 0;
        on_params(&p);
        c->engine = 1;
        wait_ms(1200);
        CHECK(c->engine == 0);
    }

    /* velocity and pos RPCs */
    {
        srpc_vehicle_velocity_t v = {0, {0.1f, 0.2f, 0.3f}};
        srpc_id_vec3_t p = {9, {1, 2, 3}};
        on_velocity(&v);
        CHECK(c->s.vel[1] == 0.2f);
        v.type = 1;
        on_velocity(&v);
        CHECK(c->s.turn[2] == 0.3f);
        on_pos(&p);
        CHECK(c->s.pos[1] == 2 && c->teleports >= 1);
    }

    /* the player leaves on his own: one 154, then foot sync is allowed once he is out */
    fake_leaving = 1;
    n_sent = 0;
    frames(5);
    CHECK(count_sent(1, SRPC_C2S_EXIT_VEHICLE) == 1);
    CHECK(feat_vehicle_in_car());
    vg_warp_out(fake_local, c->s.pos);
    fake_leaving = 0;
    frames(3);
    CHECK(!feat_vehicle_in_car());

    /* walking to a car: one 26 per attempt */
    n_sent = 0;
    fake_enter_car = c->handle;
    fake_enter_passenger = 0;
    frames(30);
    CHECK(count_sent(1, SRPC_C2S_ENTER_VEHICLE) == 1);
    fake_enter_car = 0;
    frames(3);
    fake_enter_car = c->handle;
    fake_enter_passenger = 1;
    frames(3);
    CHECK(count_sent(1, SRPC_C2S_ENTER_VEHICLE) == 2);
    fake_enter_car = 0;

    /* RemovePlayerFromVehicle sends no 154, and a stuck exit is forced */
    vg_warp_driver(fake_local, c->handle);
    frames(2);
    on_remove_from();
    CHECK(me->leaving == 1);
    n_sent = 0;
    fake_leaving = 1;
    wait_ms(KICK_MS + 200);
    fake_leaving = 0;
    CHECK(count_sent(1, SRPC_C2S_EXIT_VEHICLE) == 0);
    CHECK(me->car == 0);

    /* passenger */
    c->driver = 0;
    n_sent = 0;
    vg_warp_passenger(fake_local, c->handle, 1);
    wait_ms(1000);
    CHECK(count_sent(0, SRPC_sync_passenger) >= 8);
    CHECK(count_sent(0, SRPC_sync_vehicle) == 0);
    CHECK(count_sent(0, SRPC_sync_unoccupied) >= 8); /* no driver: the passenger reports the car */
    {
        const sent_t *s = last_sent(0, SRPC_sync_passenger);
        bs_rd_init(&b, s->d, s->bits);
        bs_r8(&b); bs_r16(&b);
        CHECK(bs_r16(&b) == 2);
    }
    /* streaming the car out from under him must not leave a dangling pointer */
    {
        srpc_id_t o = {9};
        on_stream_out(&o);
        CHECK(me->car == 0);
        frames(3);
        CHECK(!feat_vehicle_in_car());
    }

    /* the D-pad gets him into the nearest car as a passenger, and locked ones are skipped */
    wait_ms(1500);
    {
        fake_car_t *near = car_of(10);
        CHECK(near != 0);
        if (near) {
            memcpy(fake_local_at, near->s.pos, 12);
            V[10].locked = 1;
            fake_pad.dpad_right = 1;
            frames(2);
            CHECK(me->enter_car == 0);
            fake_pad.dpad_right = 0;
            frames(2);
            V[10].locked = UNSET;
            fake_pad.dpad_right = 1;
            frames(2);
            CHECK(me->enter_car == near->handle && me->enter_passenger == 1);
            fake_pad.dpad_right = 0;
        }
    }
}

static void remote_car_sync(unsigned player, unsigned id, float x, float y)
{
    srpc_vehicle_sync_t s;
    memset(&s, 0, sizeof s);
    s.player = player;
    s.vehicle = id;
    s.quat[0] = 0.70710678f;
    s.quat[3] = -0.70710678f;
    s.pos[0] = x;
    s.pos[1] = y;
    s.pos[2] = 10;
    s.velocity[1] = 0.2f;
    s.health = 900;
    on_sync_vehicle(&s);
}

static void test_remote(void)
{
    fake_car_t *c;
    fake_ped_t *p;
    int ped, ped2, i;

    reset_all();
    ped = fake_new_ped();
    ped2 = fake_new_ped();
    remote_handles[3] = ped;
    remote_handles[4] = ped2;
    p = fake_ped(ped);
    stream_in(20, 411, 10, 0);
    wait_ms(1000);
    c = car_of(20);
    CHECK(c != 0);
    if (!c)
        return;

    /* 26: the animation runs and game.c is told to keep off the ped */
    {
        srpc_enter_vehicle_t e = {3, 20, 0};
        on_enter_car(&e);
        frames(2);
        CHECK(p->enter_car == c->handle);
        CHECK(remote_held[3]);
    }
    /* 200: into the seat, car follows the packets */
    remote_car_sync(3, 20, 12, 0);
    frames(2);
    CHECK(p->car == c->handle && c->driver == ped);
    CHECK(c->health == 900);
    for (i = 0; i < 90; i++) {
        float y = 0.2f * (i * 33.0f / 20.0f);
        if (i % 3 == 0)
            remote_car_sync(3, 20, 12, y);
        frames(1);
        /* fake physics: the car moves by the speed we gave it */
        c->s.pos[0] += c->s.vel[0] * 1.65f;
        c->s.pos[1] += c->s.vel[1] * 1.65f;
        c->s.pos[2] += c->s.vel[2] * 1.65f;
    }
    CHECK(c->teleports <= 2); /* the first snap, then smooth */
    CHECK(NEAR(c->s.pos[1], 0.2f * 90 * 33 / 20, 0.8f));
    CHECK(NEAR(c->s.pos[0], 12, 0.5f));
    CHECK(NEAR(c->s.quat[3], -0.70710678f, 0.01f));
    /* far jump: teleport */
    remote_car_sync(3, 20, 100, 100);
    frames(1);
    CHECK(NEAR(c->s.pos[0], 100, 1.0f));
    CHECK(c->teleports >= 1);

    /* 211: a second player rides along in seat 1, game index 0 */
    {
        srpc_passenger_sync_t s;
        memset(&s, 0, sizeof s);
        s.player = 4; s.vehicle = 20; s.seat = 1;
        on_sync_passenger(&s);
        remote_car_sync(3, 20, 100, 100);
        frames(2);
        CHECK(c->passenger[0] == ped2);
        CHECK(remote_held[4]);
    }

    /* 154: the leave task runs, late packets don't pull him back in */
    {
        srpc_exit_vehicle_t e = {3, 20};
        on_exit_car(&e);
        CHECK(p->leaving == 1);
        remote_car_sync(3, 20, 100, 100);
        frames(20);
        CHECK(p->car == c->handle); /* still animating */
        wait_ms(LEAVE_MS + 300);
        CHECK(p->car == 0); /* the animation never finished */
        CHECK(!remote_held[3]);
    }
    /* sync stops: ped put out */
    wait_ms(SYNC_LOST_MS + 200);
    CHECK(fake_ped(ped2)->car == 0);
    CHECK(!remote_held[4]);

    /* foot packet ends it straight away */
    remote_car_sync(3, 20, 100, 100);
    frames(12);
    CHECK(p->car == c->handle);
    feat_vehicle_foot_sync(3);
    CHECK(p->car == 0);
    frames(2);
    CHECK(!remote_held[3]);

    /* ped streamed out while driving: nothing touches the dead handle */
    remote_car_sync(3, 20, 100, 100);
    frames(12);
    CHECK(p->car == c->handle);
    c->driver = 0;
    fake_peds[ped >> 8].used = 0;
    remote_handles[3] = 0;
    remote_car_sync(3, 20, 100, 100);
    frames(20);
    CHECK(R[3].veh1 == 0);

    /* car streamed out with people inside */
    ped = fake_new_ped();
    remote_handles[3] = ped;
    remote_car_sync(3, 20, 100, 100);
    frames(12);
    CHECK(fake_ped(ped)->car == c->handle);
    {
        srpc_id_t o = {20};
        on_stream_out(&o);
        CHECK(fake_ped(ped)->car == 0);
        frames(5);
    }

    /* our own sync echoed back, or unknown ids, do nothing */
    stream_in(21, 411, 10, 0);
    wait_ms(1000);
    c = car_of(21);
    remote_car_sync(5, 21, 50, 50);
    remote_car_sync(3, 1999, 50, 50);
    remote_car_sync(3, 5000, 50, 50);
    remote_car_sync(5000, 21, 50, 50);
    frames(3);
    CHECK(c && NEAR(c->s.pos[0], 10, 0.01f));
}

static void test_unoccupied(void)
{
    fake_car_t *c;
    srpc_unoccupied_sync_t s;
    srpc_trailer_sync_t t;
    bs_t b;

    reset_all();
    stream_in(30, 411, 5, 0);
    wait_ms(1000);
    c = car_of(30);
    CHECK(c != 0);
    if (!c)
        return;
    n_sent = 0;
    wait_ms(UNOCC_SETTLE_MS + 500);
    CHECK(count_sent(0, SRPC_sync_unoccupied) == 0); /* parked cars say nothing */
    c->s.vel[0] = 0.2f;
    wait_ms(1000);
    CHECK(count_sent(0, SRPC_sync_unoccupied) >= 5);
    {
        const sent_t *p = last_sent(0, SRPC_sync_unoccupied);
        float r[3], f[3];
        int i;
        bs_rd_init(&b, p->d, p->bits);
        CHECK(bs_r8(&b) == SRPC_sync_unoccupied);
        CHECK(bs_r16(&b) == 30);
        CHECK(bs_r8(&b) == 0);
        for (i = 0; i < 3; i++) r[i] = bs_rf(&b);
        for (i = 0; i < 3; i++) f[i] = bs_rf(&b);
        CHECK(fabsf(1.0f - sqrtf(vm_dot3(r, r))) < 0.000001f);
        CHECK(fabsf(vm_dot3(r, f)) < 0.000001f);
    }
    c->s.vel[0] = 0;
    wait_ms(500);
    n_sent = 0;
    wait_ms(1000);
    CHECK(count_sent(0, SRPC_sync_unoccupied) == 0); /* one last report, then quiet */

    /* from someone else: applied and we keep quiet */
    memset(&s, 0, sizeof s);
    s.player = 2; s.vehicle = 30;
    s.roll[0] = 1; s.direction[1] = 1;
    s.pos[0] = 6; s.pos[2] = 10;
    s.velocity[0] = 0.3f;
    c->s.vel[0] = 0.3f;
    n_sent = 0;
    on_sync_unoccupied(&s);
    frames(3);
    CHECK(c->s.vel[0] > 0.2f);
    CHECK(count_sent(0, SRPC_sync_unoccupied) == 0);
    /* a collapsed matrix is refused */
    s.direction[0] = 1; s.direction[1] = 0;
    s.pos[0] = 99;
    on_sync_unoccupied(&s);
    frames(1);
    CHECK(c->s.pos[0] < 50);

    /* trailers: 148 for cars not there yet is remembered */
    stream_in(31, 403, 20, 0);
    stream_in(32, 435, 25, 0);
    {
        srpc_trailer_t a = {32, 31};
        on_attach(&a);
    }
    wait_ms(1500);
    CHECK(car_of(31) && car_of(32) && car_of(31)->towed == car_of(32)->handle);
    memset(&t, 0, sizeof t);
    t.player = 2; t.vehicle = 32;
    t.quat[0] = 1; t.pos[0] = 26; t.pos[2] = 10; t.velocity[0] = 0.4f;
    on_sync_trailer(&t);
    frames(1);
    CHECK(car_of(32)->s.vel[0] > 0.3f);
    /* a towing truck reports the trailer, and only then 210 */
    {
        srpc_put_in_vehicle_t put = {31, 0};
        on_put_in(&put);
        n_sent = 0;
        wait_ms(500);
        CHECK(count_sent(0, SRPC_sync_trailer) >= 5);
        CHECK(count_sent(0, SRPC_sync_trailer) <= count_sent(0, SRPC_sync_vehicle));
    }
    {
        srpc_id_t d = {31};
        on_detach(&d);
        CHECK(car_of(31)->towed == 0);
        n_sent = 0;
        wait_ms(300);
        CHECK(count_sent(0, SRPC_sync_trailer) == 0);
    }
    /* wrecked: told once */
    n_sent = 0;
    car_of(32)->wrecked = 1;
    wait_ms(500);
    CHECK(count_sent(1, SRPC_C2S_VEHICLE_DESTROYED) == 1);
}

static void test_reset(void)
{
    srpc_put_in_vehicle_t put = {40, 0};

    reset_all();
    stream_in(40, 411, 1, 0);
    stream_in(41, 500, 1, 3);
    on_put_in(&put);
    wait_ms(1000);
    CHECK(feat_vehicle_in_car());
    state = SAMP_CLASS; /* game mode restart or a lost connection */
    frames(2);
    CHECK(n_ids == 0 && fake_stats.live == 0 && !feat_vehicle_in_car());
    CHECK(fake_stats.models_loaded == 0);
    state = SAMP_SPAWNED;
    stream_in(40, 411, 1, 0);
    wait_ms(1000);
    CHECK(car_of(40) != 0);
    on_restart();
    CHECK(n_ids == 0 && fake_stats.live == 0);
}

/* Random bytes through the real decoders into the real handlers. */
static void test_fuzz(void)
{
    static const unsigned char rpcs[] = {24, 26, 57, 65, 70, 71, 91, 96, 98, 106, 123, 147, 148, 149, 154,
                                         159, 160, 161, 164, 165, 167};
    static const unsigned char syncs[] = {200, 209, 210, 211};
    unsigned char buf[300];
    union { float f; unsigned u; } bits;
    int i, n;

    reset_all();
    remote_handles[1] = fake_new_ped();
    fake_load_frames = 1;
    for (n = 0; n < 200000; n++) {
        bs_t b;
        int len = rand() % sizeof buf;
        for (i = 0; i < len; i++)
            buf[i] = rand() & 3 ? rand() : 0;
        /* plausible ids and float specials are where the bugs would be */
        if (len > 8 && (rand() & 1)) {
            buf[0] = 1; buf[1] = 0; buf[2] = rand() & 63; buf[3] = 0;
            bits.u = rand() & 1 ? 0x7FC00000 : 0x7F800000;
            memcpy(buf + 4, &bits, 4);
        }
        bs_rd_init(&b, buf, len * 8);
        if (rand() & 1)
            samp_rpc_dispatch(rpcs[rand() % sizeof rpcs], &b);
        else
            samp_sync_dispatch(syncs[rand() % sizeof syncs], &b);
        if ((n & 255) == 0) {
            frames(2);
            n_sent = 0;
        }
        CHECK(n_live <= MAX_LIVE);
    }
    fake_load_frames = 3;
    frames(10);
    feat_vehicle_reset();
    CHECK(n_ids == 0);
    printf("fuzz: %u decoded, %u truncated\n", samp_rpc_stats.decoded, samp_rpc_stats.truncated);
}

int main(void)
{
    feat_vehicle_init(&handlers);
    samp_rpc_set_handlers(&handlers);
    fake_init();
    test_math();
    test_cap();
    test_state_kept();
    test_local();
    test_remote();
    test_unoccupied();
    test_reset();
    test_fuzz();
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
