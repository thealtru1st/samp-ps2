/* Stands in for payload/feat_vehicle_game.c on the host. It models what the logic relies
 * on: a 70-slot pool, handles that die, seats, model loading that takes a few frames.
 * It enforces the rules the real game punishes with a crash: using a dead handle aborts
 * the test, and so does deleting a car with someone inside. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "feat_vehicle_game.h"
#include "feat_vehicle_math.h"
#include "fake_game.h"

fake_car_t fake_cars[FAKE_POOL];
fake_ped_t fake_peds[FAKE_PEDS];
fake_stats_t fake_stats;
vg_pad_t fake_pad;
int fake_local = 1;
float fake_local_at[3];
int fake_enter_car, fake_enter_passenger, fake_leaving;
int fake_load_frames = 3;

static int model_timer[20000];
static int model_mission[20000];
static int mod_timer[20000];

#define BAD(...) do { printf("FAKE GAME VIOLATION: " __VA_ARGS__); putchar('\n'); fflush(stdout); abort(); } while (0)

void fake_frame(void)
{
    int i;
    for (i = 0; i < 20000; i++) {
        if (model_timer[i] > 0) model_timer[i]--;
        if (mod_timer[i] > 0) mod_timer[i]--;
    }
}

fake_car_t *fake_car(int h)
{
    int i = h >> 8;
    if (h <= 0 || i < 0 || i >= FAKE_POOL || !fake_cars[i].used || fake_cars[i].handle != h)
        return 0;
    return &fake_cars[i];
}

fake_ped_t *fake_ped(int h)
{
    int i = h >> 8;
    if (h <= 0 || i < 0 || i >= FAKE_PEDS || !fake_peds[i].used || fake_peds[i].handle != h)
        return 0;
    return &fake_peds[i];
}

int fake_new_ped(void)
{
    static int ref = 1;
    int i;
    for (i = 2; i < FAKE_PEDS; i++) {
        if (!fake_peds[i].used) {
            memset(&fake_peds[i], 0, sizeof fake_peds[i]);
            fake_peds[i].used = 1;
            fake_peds[i].handle = i << 8 | (ref++ & 0x7F);
            return fake_peds[i].handle;
        }
    }
    return 0;
}

void fake_init(void)
{
    memset(fake_cars, 0, sizeof fake_cars);
    memset(fake_peds, 0, sizeof fake_peds);
    memset(&fake_stats, 0, sizeof fake_stats);
    memset(model_timer, 0, sizeof model_timer);
    memset(model_mission, 0, sizeof model_mission);
    fake_peds[1].used = 1;
    fake_peds[1].handle = 1 << 8 | 1;
    fake_local = fake_peds[1].handle;
    fake_enter_car = fake_leaving = 0;
}

static fake_car_t *need_car(int h, const char *who)
{
    fake_car_t *c = fake_car(h);
    if (!c) BAD("%s on dead car %x", who, h);
    return c;
}

static fake_ped_t *need_ped(int h, const char *who)
{
    fake_ped_t *p = fake_ped(h);
    if (!p) BAD("%s on dead ped %x", who, h);
    return p;
}

int vg_model_ok(unsigned m) { return m >= 400 && m <= 20000; }
int vg_model_loaded(unsigned m)
{
    if (m >= 20000) BAD("model %u", m);
    return model_mission[m] && model_timer[m] == 0;
}
int vg_stream_over(void) { return 0; }
void vg_model_request(unsigned m)
{
    if (!model_mission[m]) {
        model_timer[m] = fake_load_frames;
        fake_stats.models_loaded++;
        if (fake_stats.models_loaded > fake_stats.models_peak)
            fake_stats.models_peak = fake_stats.models_loaded;
    }
    model_mission[m] = 1;
}
void vg_model_release(unsigned m)
{
    if (!model_mission[m]) BAD("release of a model not requested: %u", m);
    model_mission[m] = 0;
    fake_stats.models_loaded--;
}
int vg_mod_loaded(unsigned c) { return mod_timer[c] == 0; }
void vg_mod_request(unsigned c) { mod_timer[c] = 2; }
void vg_mod_release(unsigned c) { (void)c; }
int vg_mod_ok(int car, unsigned comp) { return fake_car(car) && comp >= 1000 && comp <= 1193 && comp != 1192; }

int vg_car_slots_free(void)
{
    int i, n = 0;
    for (i = 1; i < FAKE_POOL; i++)
        if (!fake_cars[i].used)
            n++;
    return n;
}

int vg_create(unsigned model, const float *pos, float angle, const char *plate)
{
    static int ref = 1;
    float h = -angle * 0.008726646f, q[4];
    int i;

    if (!vg_model_loaded(model)) BAD("create of model %u before it loaded", model);
    for (i = 1; i < FAKE_POOL; i++) {
        fake_car_t *c = &fake_cars[i];
        if (c->used)
            continue;
        memset(c, 0, sizeof *c);
        c->used = 1;
        c->handle = i << 8 | (ref++ & 0x7F);
        c->model = model;
        memcpy(c->s.pos, pos, 12);
        c->s.pos[2] += 0.25f;
        /* sin and cos of half the heading, by series: this is a test double */
        {
            float x = h, s = x - x * x * x / 6 + x * x * x * x * x / 120, co = 1 - x * x / 2 + x * x * x * x / 24;
            q[0] = co; q[1] = 0; q[2] = 0; q[3] = s;
            vm_quat_norm(q);
        }
        memcpy(c->s.quat, q, sizeof q);
        c->health = 1000;
        c->seats = 3;
        c->engine = 0;
        if (plate) strncpy(c->plate, plate, 8);
        fake_stats.created++;
        fake_stats.live++;
        if (fake_stats.live > fake_stats.live_peak)
            fake_stats.live_peak = fake_stats.live;
        return c->handle;
    }
    BAD("pool exhausted");
    return 0;
}

void vg_delete(int car)
{
    fake_car_t *c = need_car(car, "delete");
    int i;
    if (c->driver) BAD("deleted car %x with a driver", car);
    for (i = 0; i < 8; i++)
        if (c->passenger[i]) BAD("deleted car %x with a passenger", car);
    for (i = 0; i < FAKE_POOL; i++)
        if (fake_cars[i].used && (fake_cars[i].towed == car)) BAD("deleted a towed car");
    c->used = 0;
    fake_stats.deleted++;
    fake_stats.live--;
}

int vg_alive(int car) { return fake_car(car) != 0; }
int vg_wrecked(int car) { fake_car_t *c = fake_car(car); return !c || c->wrecked; }

static void fill_vectors(vg_state_t *s)
{
    float m[16];
    int i;
    vm_quat_to_mat(s->quat, m);
    for (i = 0; i < 3; i++) {
        s->right[i] = m[i];
        s->fwd[i] = m[4 + i];
    }
}

void vg_read(int car, vg_state_t *s)
{
    fake_car_t *c = fake_car(car);
    memset(s, 0, sizeof *s);
    s->quat[0] = 1;
    if (!c)
        return;
    *s = c->s;
    fill_vectors(s);
}

void vg_teleport(int car, const float *p)
{
    fake_car_t *c = need_car(car, "teleport");
    memcpy(c->s.pos, p, 12);
    memset(c->s.vel, 0, 12);
    memset(c->s.turn, 0, 12);
    c->teleports++;
}
void vg_set_heading(int car, float d) { need_car(car, "heading")->heading_set = d; }
void vg_set_rotation(int car, const float *q) { memcpy(need_car(car, "rotation")->s.quat, q, 16); }
void vg_set_velocity(int car, const float *v) { memcpy(need_car(car, "velocity")->s.vel, v, 12); }
void vg_set_turn(int car, const float *v) { memcpy(need_car(car, "turn")->s.turn, v, 12); }
float vg_health(int car) { fake_car_t *c = fake_car(car); return c ? c->health : 0; }
void vg_set_health(int car, float h) { need_car(car, "health")->health = h; }
void vg_set_colours(int car, unsigned a, unsigned b) { fake_car_t *c = need_car(car, "colours"); c->c1 = a; c->c2 = b; }
void vg_set_interior(int car, unsigned i) { need_car(car, "interior")->interior = i; }
void vg_set_paintjob(int car, int i) { need_car(car, "paintjob")->paintjob = i + 1; }
void vg_add_mod(int car, unsigned m)
{
    fake_car_t *c = need_car(car, "mod");
    if (m == 1192) BAD("illegal mod reached the game");
    if (c->n_mods < 16) c->mods[c->n_mods++] = m;
}
void vg_remove_mod(int car, unsigned m)
{
    fake_car_t *c = need_car(car, "remove mod");
    int i;
    for (i = 0; i < c->n_mods; i++)
        if (c->mods[i] == m)
            c->mods[i] = 0;
}
void vg_set_engine(int car, int on) { need_car(car, "engine")->engine = on; }
int  vg_engine(int car) { fake_car_t *c = fake_car(car); return c && c->engine; }
void vg_set_lights(int car, int on) { need_car(car, "lights")->lights = on ? 2 : 1; }
void vg_set_locked(int car, int l) { need_car(car, "locked")->locked = l; }
void vg_set_siren(int car, int on) { need_car(car, "siren")->siren = on; }
int  vg_siren(int car) { fake_car_t *c = fake_car(car); return c && c->siren; }
void vg_set_alarm(int car, int on) { need_car(car, "alarm")->alarm = on; }
void vg_set_door(int car, unsigned d, int open)
{
    if (d > 5) BAD("door %u", d);
    need_car(car, "door")->door[d] = open;
}
void vg_set_collision(int car, int on) { need_car(car, "collision")->no_collision = !on; }
int  vg_add_blip(int car) { need_car(car, "blip"); return ++fake_stats.blips; }
void vg_remove_blip(int blip) { if (blip) fake_stats.blips--; }

void vg_get_damage(int car, unsigned *p, unsigned *d, unsigned *l, unsigned *t)
{
    fake_car_t *c = need_car(car, "damage get");
    *p = c->panels; *d = c->doors; *l = c->lights_dmg; *t = c->tyres;
}
void vg_set_damage(int car, unsigned p, unsigned d, unsigned l, unsigned t)
{
    fake_car_t *c = need_car(car, "damage set");
    c->panels = p; c->doors = d; c->lights_dmg = l; c->tyres = t;
}
void vg_repair(int car)
{
    fake_car_t *c = need_car(car, "repair");
    c->panels = c->doors = c->lights_dmg = c->tyres = 0;
    c->repairs++;
}
int vg_towed(int cab) { fake_car_t *c = fake_car(cab); return c ? c->towed : 0; }
void vg_attach(int t, int cab) { need_car(t, "attach"); need_car(cab, "attach")->towed = t; }
void vg_detach(int t, int cab) { need_car(t, "detach"); need_car(cab, "detach")->towed = 0; }

int vg_driver(int car) { fake_car_t *c = fake_car(car); return c ? c->driver : 0; }
int vg_passenger(int car, unsigned i) { fake_car_t *c = fake_car(car); return c && i < 8 ? c->passenger[i] : 0; }
unsigned vg_seats(int car) { fake_car_t *c = fake_car(car); return c ? c->seats : 0; }
int vg_ped_alive(int ped) { return fake_ped(ped) != 0; }
int vg_ped_car(int ped)
{
    fake_ped_t *p = fake_ped(ped);
    if (!p || !p->car)
        return 0;
    if (!fake_car(p->car)) BAD("ped holds a pointer to a freed car");
    return p->car;
}

void vg_warp_driver(int ped, int car)
{
    fake_ped_t *p = need_ped(ped, "warp driver");
    fake_car_t *c = need_car(car, "warp driver");
    if (p->car) BAD("warp of a ped already in a car");
    if (c->driver) BAD("warp onto a taken driver seat");
    c->driver = ped;
    p->car = car;
    p->warps++;
    c->engine = 1;
}

void vg_warp_passenger(int ped, int car, unsigned i)
{
    fake_ped_t *p = need_ped(ped, "warp passenger");
    fake_car_t *c = need_car(car, "warp passenger");
    if (p->car) BAD("warp of a ped already in a car");
    if (i >= c->seats || c->passenger[i]) BAD("bad passenger seat %u", i);
    c->passenger[i] = ped;
    p->car = car;
    p->warps++;
}

void vg_warp_out(int ped, const float *pos)
{
    fake_ped_t *p = need_ped(ped, "warp out");
    fake_car_t *c = p->car ? need_car(p->car, "warp out") : 0;
    int i;
    if (c) {
        if (c->driver == ped)
            c->driver = 0;
        for (i = 0; i < 8; i++)
            if (c->passenger[i] == ped)
                c->passenger[i] = 0;
    }
    p->car = 0;
    p->outs++;
    memcpy(p->pos, pos, 12);
}

void vg_task_enter(int ped, int car, int passenger)
{
    fake_ped_t *p = need_ped(ped, "enter");
    need_car(car, "enter");
    p->enter_car = car;
    p->enter_passenger = passenger;
}
void vg_task_leave(int ped, int car) { need_car(car, "leave"); need_ped(ped, "leave")->leaving = 1; }
void vg_task_leave_any(int ped) { need_ped(ped, "leave any")->leaving = 1; }

int vg_local_ped(void) { return fake_local; }
void vg_local_pos(float *p) { memcpy(p, fake_local_at, 12); }
void vg_local_vitals(unsigned *h, unsigned *a) { *h = 100; *a = 50; }
int vg_local_entering(int *passenger) { *passenger = fake_enter_passenger; return fake_enter_car; }
int vg_local_leaving(void) { return fake_leaving; }
void vg_read_pad(vg_pad_t *p) { *p = fake_pad; }
void vg_camera_behind(void) { fake_stats.camera++; }

int vg_being_towed(int car) { return 0; }
