/* Vehicles. Owned by one work stream: see feat.h.
 *
 * The server streams vehicles to us by id. Every one gets a record here, and only the
 * nearest few become game cars: the game's vehicle pool has 70 slots and streaming
 * memory is tight, so a record is created in the game when the player comes near and
 * deleted again, with its state saved, when he leaves.
 *
 * All game access is in feat_vehicle_game.c, which keeps this file testable on the host.
 * Nothing in this file touches a float from the network without vm_vec_ok first. */
#include "feat.h"
#include "fps.h"
#include "feat_vehicle.h"
#include "feat_vehicle_game.h"
#include "feat_vehicle_math.h"
#include "samp.h"
#include "rak.h"
#include "util.h"

#define VEH_IDS       2000
#define PLAYER_IDS    1000
#define MODEL_FIRST   400
#define MODEL_LAST    611
#define MOD_SLOTS     16
#define MOD_FIRST     1000
#define MOD_LAST      1193

/* What the game holds at once. The pool has 70 slots; models are the real limit, about
 * a dozen is what the game itself keeps loaded in traffic. */
#define MAX_LIVE      24
#define MAX_MODELS    10
#define MAX_LOADING   2
#define POOL_RESERVE  6
#define LIVE_IN2      (170.0f * 170.0f)
#define LIVE_OUT2     (230.0f * 230.0f)
#define EVICT_MARGIN  1.5f   /* on squared distance: a swap must gain something */
#define LOAD_DELAY_MS 100    /* the plate RPC comes just behind the stream-in */
#define LOAD_LIMIT_MS 8000
#define RETRY_MS      5000
#define STREAM_MS     100

#define DRIVER_MS     50     /* SA-MP's in-car rate is 30-40 ms; this suits a 20 Hz frame budget */
#define PASSENGER_MS  100
#define UNOCCUPIED_MS 100
#define DAMAGE_MS     500
#define ENGINE_MS     1000
#define WARP_MS       300
#define SYNC_LOST_MS  1500
#define ENTER_MS      6000
#define LEAVE_MS      4000
#define PUT_LIMIT_MS  8000
#define KICK_MS       3000

#define SNAP_DIST2    (10.0f * 10.0f)
#define SETTLED_DIST2 (0.05f * 0.05f)
#define SETTLED_SPEED2 (0.002f * 0.002f)
#define CATCH_UP      0.1f   /* share of the error removed per step, about 200 ms */
#define TURN_RATE     0.35f  /* share of the rotation error removed per frame */
#define PREDICT_MS    300    /* never extrapolate an old packet further than this */
#define UNOCC_DIST2   (60.0f * 60.0f)
#define UNOCC_SETTLE_MS 3000 /* new cars are still dropping onto their wheels */
#define UNOCC_QUIET_MS 1000  /* somebody else is already reporting this car */
#define MOVING2       (0.003f * 0.003f)
#define PASSENGER_REACH2 (6.0f * 6.0f)

#define K_ACTION      1
#define K_CROUCH      2
#define K_FIRE        4
#define K_SPRINT      8
#define K_SECONDARY   16
#define K_JUMP        32
#define K_LOOK_RIGHT  64
#define K_HANDBRAKE   128
#define K_LOOK_LEFT   256
#define K_SUBMISSION  512

#define UNSET         255    /* the "leave it as it is" value of vehicle params */
#define SCM_PAINTJOB  1
#define SCM_ADD_MOD   2
#define SCM_COLOUR    3

enum { R_NONE, R_RECORD, R_LOADING, R_LIVE };
enum { T_NONE, T_DRIVEN, T_FREE };

typedef struct {
    unsigned char state, no_game, force, dead_sent, moving, collide_off, t_mode;
    unsigned char c1, c2, interior, lights_dmg, tyres, siren_on;
    signed char paintjob;
    unsigned char engine, lights, alarm, locked, bonnet, boot, objective, siren;
    unsigned char door[4];
    unsigned short model, list_index, want_trailer; /* want_trailer is id + 1 */
    unsigned short mods[MOD_SLOTS];
    unsigned mod_todo;
    int handle, blip;
    float pos[3], angle, quat[4], health;
    int has_quat;
    unsigned panels, doors;
    unsigned sent_panels, sent_doors, sent_lights, sent_tyres;
    unsigned load_at, retry_at, born;
    float t_pos[3], t_quat[4], t_vel[3], t_turn[3];
    unsigned t_time;
    char plate[9];
} veh_t;

typedef struct {
    unsigned short veh1; /* id + 1 of the vehicle the player is in, by sync */
    unsigned char seat, held;
    unsigned last, warp_at, enter_until, leave_until;
} remote_t;

typedef struct {
    int in_car, enter_car, seat, exit_sent, forced_exit;
    unsigned veh, car, sync_at, damage_at, engine_at, kick_at;
    unsigned put1, put_seat, put_at;
    int dpad;
} local_t;

/* Provided by game.c once the Integration notes are applied. Until then remote peds
 * can't be seated, though their cars still move. */
extern int  game_remote_handle(unsigned id) __attribute__((weak));
extern void game_remote_hold(unsigned id, int on) __attribute__((weak));
extern void feat_world_car_going(unsigned id) __attribute__((weak));

static veh_t V[VEH_IDS];
static unsigned short ids[VEH_IDS];
static unsigned n_ids;
static remote_t R[PLAYER_IDS];
static local_t L;
static unsigned short model_use[MODEL_LAST - MODEL_FIRST + 1];
static unsigned n_models, n_live, n_loading;
static unsigned tick_now, stream_at, unocc_at, unocc_cursor;
static unsigned mod_wait, mod_wait_id, mod_wait_at;
static int no_collide, was_spawned;
static unsigned log_budget = 24;

/* ---- records -------------------------------------------------------------- */

static veh_t *rec(unsigned id) { return id < VEH_IDS && V[id].state ? &V[id] : 0; }

static veh_t *live(unsigned id)
{
    veh_t *r = rec(id);
    return r && r->state == R_LIVE ? r : 0;
}

static int id_of(int car)
{
    unsigned i;
    if (!car)
        return -1;
    for (i = 0; i < n_ids; i++)
        if (V[ids[i]].state == R_LIVE && V[ids[i]].handle == car)
            return ids[i];
    return -1;
}

static void model_ref(unsigned model)
{
    if (model_use[model - MODEL_FIRST]++ == 0) {
        vg_model_request(model);
        n_models++;
    }
}

static void model_unref(unsigned model)
{
    if (--model_use[model - MODEL_FIRST] == 0) {
        vg_model_release(model);
        n_models--;
    }
}

static int local_uses(unsigned id)
{
    return (L.in_car && L.veh == id) || (L.put1 == id + 1);
}

/* Warps everyone out of a car about to go, or their ped keeps a pointer into freed
 * memory. The destructor doesn't deal with occupants. */
static void eject_all(veh_t *r)
{
    vg_state_t st;
    float at[3];
    unsigned i, seats;
    int p;

    vg_read(r->handle, &st);
    for (i = 0; i < 3; i++)
        at[i] = st.pos[i] - st.right[i] * 2.5f;
    if ((p = vg_driver(r->handle)))
        vg_warp_out(p, at);
    seats = vg_seats(r->handle);
    for (i = 0; i < seats && i < 8; i++)
        if ((p = vg_passenger(r->handle, i)))
            vg_warp_out(p, at);
}

static void unhook_trailers(veh_t *r)
{
    unsigned i;
    int t = vg_towed(r->handle);

    if (t)
        vg_detach(t, r->handle);
    for (i = 0; i < n_ids; i++) {
        veh_t *o = &V[ids[i]];
        if (o != r && o->state == R_LIVE && vg_towed(o->handle) == r->handle)
            vg_detach(r->handle, o->handle);
    }
}

/* Saves what the game knows, so the car comes back as it was left. */
static void save_state(veh_t *r)
{
    vg_state_t st;
    unsigned lights, tyres;

    vg_read(r->handle, &st);
    if (vm_vec_ok(st.pos, 3, VM_WORLD_LIMIT) && vm_quat_norm(st.quat)) {
        memcpy(r->pos, st.pos, sizeof r->pos);
        memcpy(r->quat, st.quat, sizeof r->quat);
        r->has_quat = 1;
    }
    r->health = vg_health(r->handle);
    vg_get_damage(r->handle, &r->panels, &r->doors, &lights, &tyres);
    r->lights_dmg = lights;
    r->tyres = tyres;
}

/* Takes the car out of the game but keeps the record. */
static void unload(veh_t *r)
{
    if (r->state == R_LOADING) {
        n_loading--;
        model_unref(r->model);
    } else if (r->state == R_LIVE) {
        if (vg_alive(r->handle)) {
            save_state(r);
            eject_all(r);
            unhook_trailers(r);
            vg_remove_blip(r->blip);
            if (feat_world_car_going)   /* objects on it first, see feat_world_obj.c */
                feat_world_car_going((unsigned)(r - V));
            vg_delete(r->handle);
        }
        r->handle = 0;
        r->blip = 0;
        n_live--;
        model_unref(r->model);
    }
    r->state = R_RECORD;
    r->t_mode = T_NONE;
    r->collide_off = 0;
}

static void forget(unsigned id)
{
    veh_t *r = &V[id];

    if (!r->state)
        return;
    unload(r);
    n_ids--;
    ids[r->list_index] = ids[n_ids];
    V[ids[r->list_index]].list_index = r->list_index;
    r->state = R_NONE;
}

static void apply_params(veh_t *r)
{
    int h = r->handle;
    unsigned i;

    if (r->engine != UNSET) vg_set_engine(h, r->engine);
    if (r->lights != UNSET) vg_set_lights(h, r->lights);
    if (r->alarm != UNSET) vg_set_alarm(h, r->alarm);
    /* Unset still has to be written: the game makes cop cars locked to the player. */
    vg_set_locked(h, r->locked != UNSET && r->locked);
    if (r->siren != UNSET) vg_set_siren(h, r->siren);
    if (r->bonnet != UNSET) vg_set_door(h, 0, r->bonnet);
    if (r->boot != UNSET) vg_set_door(h, 1, r->boot);
    for (i = 0; i < 4; i++)
        if (r->door[i] != UNSET)
            vg_set_door(h, 2 + i, r->door[i]);
    if (r->objective == 1 && !r->blip) {
        r->blip = vg_add_blip(h);
    } else if (r->objective != 1 && r->blip) {
        vg_remove_blip(r->blip);
        r->blip = 0;
    }
}

static void note_damage_sent(veh_t *r, unsigned panels, unsigned doors, unsigned lights, unsigned tyres)
{
    r->sent_panels = panels;
    r->sent_doors = doors;
    r->sent_lights = lights;
    r->sent_tyres = tyres;
}

static void apply_all(veh_t *r)
{
    int h = r->handle;
    unsigned i;

    if (r->has_quat)
        vg_set_rotation(h, r->quat);
    if (r->c1 != UNSET && r->c2 != UNSET)
        vg_set_colours(h, r->c1, r->c2);
    vg_set_health(h, r->health);
    if (r->interior)
        vg_set_interior(h, r->interior);
    if (r->panels || r->doors || r->lights_dmg || r->tyres)
        vg_set_damage(h, r->panels, r->doors, r->lights_dmg, r->tyres);
    note_damage_sent(r, r->panels, r->doors, r->lights_dmg, r->tyres);
    if (r->paintjob >= 0)
        vg_set_paintjob(h, r->paintjob);
    apply_params(r);
    r->mod_todo = 0;
    for (i = 0; i < MOD_SLOTS; i++)
        if (r->mods[i])
            r->mod_todo |= 1u << i;
}

static void make_car(unsigned id, veh_t *r)
{
    int h;

    n_loading--;
    if (vg_car_slots_free() <= POOL_RESERVE) {
        r->state = R_RECORD;
        r->retry_at = tick_now + RETRY_MS;
        model_unref(r->model);
        return;
    }
    h = vg_create(r->model, r->pos, r->angle, r->plate);
    if (!h) {
        slog("veh: create %d model %d failed", id, r->model);
        r->state = R_RECORD;
        r->retry_at = tick_now + RETRY_MS;
        model_unref(r->model);
        return;
    }
    r->handle = h;
    r->state = R_LIVE;
    r->born = tick_now;
    r->force = 0;
    n_live++;
    apply_all(r);
    if (log_budget) { /* the log is 4 KB for everybody and doesn't wrap */
        log_budget--;
        slog("veh: %d live, model %d handle %x, %d live %d models", id, r->model, h, n_live, n_models);
    }
}

/* Only cars nobody is using can go. */
static int evictable(unsigned id, veh_t *r)
{
    unsigned i, seats;

    if (local_uses(id) || r->t_mode != T_NONE || r->want_trailer)
        return 0;
    if (vg_driver(r->handle) || vg_towed(r->handle))
        return 0;
    seats = vg_seats(r->handle);
    for (i = 0; i < seats && i < 8; i++)
        if (vg_passenger(r->handle, i))
            return 0;
    return 1;
}

static void stream_step(void)
{
    float me[3], best_d = 0.0f, far_d = 0.0f;
    veh_t *best = 0, *far = 0;
    unsigned i;

    vg_local_pos(me);
    for (i = 0; i < n_ids; i++) {
        unsigned id = ids[i];
        veh_t *r = &V[id];
        float d;

        if (r->no_game)
            continue;
        if (r->state == R_LOADING) {
            if (tick_now - r->load_at > LOAD_LIMIT_MS) {
                slog("veh: model %d never loaded", r->model);
                unload(r);
                r->retry_at = tick_now + RETRY_MS;
            }
            continue;
        }
        if (r->state == R_LIVE) {
            vg_state_t st;
            static unsigned told;
            /* diagnosis (Valrise render freeze): a model the game dropped while we draw it */
            if (!vg_model_loaded(r->model) && told < 30) {
                told++;
                slog("diag: car %d model %d not loaded while live", (int)id, r->model);
            }
            vg_read(r->handle, &st);
            if (vm_vec_ok(st.pos, 3, VM_WORLD_LIMIT))
                memcpy(r->pos, st.pos, sizeof r->pos);
            d = vm_dist2(r->pos, me);
            if (d > far_d && evictable(id, r)) {
                far = r;
                far_d = d;
            }
            continue;
        }
        if ((int)(tick_now - r->retry_at) < 0 || tick_now - r->born < LOAD_DELAY_MS)
            continue;
        d = vm_dist2(r->pos, me);
        if (r->force)
            d = 0.0f;
        else if (d > LIVE_IN2)
            continue;
        if (!best || d < best_d) {
            best = r;
            best_d = d;
        }
    }
    if (far && far_d > LIVE_OUT2) {
        unload(far);
        far = 0;
    }
    if (!best)
        return;
    if (n_live + n_loading < MAX_LIVE && n_loading < MAX_LOADING &&
        (model_use[best->model - MODEL_FIRST] || (n_models < MAX_MODELS && !vg_stream_over()))) {
        best->state = R_LOADING;
        best->load_at = tick_now;
        n_loading++;
        model_ref(best->model);
    } else if (far && far_d > best_d * EVICT_MARGIN) {
        unload(far);
    }
}

static void load_step(void)
{
    unsigned i;

    for (i = 0; i < n_ids; i++) {
        unsigned id = ids[i];
        veh_t *r = &V[id];
        if (r->state == R_LOADING && vg_model_loaded(r->model)) {
            make_car(id, r);
            return; /* one per frame: each costs a noticeable slice of it */
        }
    }
}

static void mod_step(void)
{
    unsigned i, b;

    if (mod_wait) {
        veh_t *r = live(mod_wait_id);
        if (vg_mod_loaded(mod_wait)) {
            if (r)
                vg_add_mod(r->handle, mod_wait);
        } else if (tick_now - mod_wait_at < LOAD_LIMIT_MS) {
            return;
        }
        vg_mod_release(mod_wait);
        mod_wait = 0;
        return;
    }
    for (i = 0; i < n_ids; i++) {
        veh_t *r = &V[ids[i]];
        if (r->state != R_LIVE || !r->mod_todo)
            continue;
        for (b = 0; !(r->mod_todo & (1u << b)); b++)
            ;
        r->mod_todo &= ~(1u << b);
        if (!vg_mod_ok(r->handle, r->mods[b])) {
            slog("veh: mod %d refused on model %d", r->mods[b], r->model);
            continue;
        }
        mod_wait = r->mods[b];
        mod_wait_id = ids[i];
        mod_wait_at = tick_now;
        vg_mod_request(mod_wait);
        return;
    }
}

/* ---- server RPCs ---------------------------------------------------------- */

static void on_stream_in(const srpc_vehicle_t *v)
{
    veh_t *r;
    unsigned i;

    if (!v->id || v->id >= VEH_IDS || !vm_vec_ok(v->pos, 3, VM_WORLD_LIMIT) ||
        !vm_vec_ok(&v->angle, 1, 100000.0f) || !vm_vec_ok(&v->health, 1, 10000000.0f))
        return;
    forget(v->id);
    r = &V[v->id];
    memset(r, 0, sizeof *r);
    r->state = R_RECORD;
    r->list_index = n_ids;
    ids[n_ids++] = v->id;
    r->model = v->model > 0xFFFF ? 0 : v->model;
    memcpy(r->pos, v->pos, sizeof r->pos);
    r->angle = v->angle;
    r->health = v->health < 0.0f ? 0.0f : v->health;
    r->interior = v->interior;
    /* 0 to 127 is the colour table; -1 means the car keeps the colours it was made with */
    r->c1 = v->body_color1 >= 0 ? v->body_color1 & 127 : v->color1 < 128 ? v->color1 : UNSET;
    r->c2 = v->body_color2 >= 0 ? v->body_color2 & 127 : v->color2 < 128 ? v->color2 : UNSET;
    r->panels = v->panel_damage;
    r->doors = v->door_damage;
    r->lights_dmg = v->light_damage;
    r->tyres = v->tyre_damage;
    r->paintjob = v->paintjob >= 1 && v->paintjob <= 3 ? (int)v->paintjob - 1 : -1;
    for (i = 0; i < SRPC_CAR_MODS; i++)
        r->mods[i] = v->mods[i] >= MOD_FIRST && v->mods[i] <= MOD_LAST ? v->mods[i] : 0;
    r->engine = r->lights = r->alarm = r->locked = UNSET;
    r->bonnet = r->boot = r->objective = r->siren = UNSET;
    memset(r->door, UNSET, sizeof r->door);
    r->born = tick_now;
    r->retry_at = tick_now;
    /* Trains run on rails under their own control and need 5 models at once. Records
     * are kept, so the ids stay known, but no car is made. See the report. */
    if (r->model < MODEL_FIRST || r->model > MODEL_LAST || !vg_model_ok(r->model) ||
        r->model == 449 || r->model == 537 || r->model == 538 || r->model == 569 || r->model == 570 ||
        r->model == 590)
        r->no_game = 1;
}

static void on_stream_out(const srpc_id_t *v) { if (v->id < VEH_IDS) forget(v->id); }

static void clear_local_car(void)
{
    L.in_car = 0;
    L.car = 0;
    L.forced_exit = 0;
    L.kick_at = 0;
}

static void put_local(veh_t *r, unsigned seat)
{
    int ped = vg_local_ped();
    int in = vg_ped_car(ped);
    vg_state_t st;

    if (in == r->handle && seat == 0 && vg_driver(in) == ped)
        return;
    if (in) {
        vg_read(in, &st);
        vg_warp_out(ped, st.pos);
    }
    if (seat == 0) {
        if (!vg_driver(r->handle))
            vg_warp_driver(ped, r->handle);
    } else if (seat - 1 < vg_seats(r->handle) && !vg_passenger(r->handle, seat - 1)) {
        vg_warp_passenger(ped, r->handle, seat - 1);
    }
    vg_camera_behind();
}

static void on_put_in(const srpc_put_in_vehicle_t *v)
{
    veh_t *r = rec(v->vehicle);

    if (!r || r->no_game)
        return;
    if (r->state == R_LIVE) {
        put_local(r, v->seat);
        return;
    }
    /* A script usually does CreateVehicle and PutPlayerInVehicle back to back. */
    r->force = 1;
    L.put1 = v->vehicle + 1;
    L.put_seat = v->seat;
    L.put_at = tick_now;
}

static void on_remove_from(void)
{
    int ped = vg_local_ped();

    L.put1 = 0;
    if (!vg_ped_car(ped))
        return;
    L.forced_exit = 1;
    L.kick_at = tick_now + KICK_MS;
    vg_task_leave_any(ped);
}

static void remote_seat(unsigned player, unsigned vehicle, unsigned seat)
{
    remote_t *p = &R[player];

    p->last = tick_now;
    if ((int)(p->leave_until - tick_now) > 0)
        return; /* last packets before the exit animation shows them out */
    p->veh1 = vehicle + 1;
    p->seat = seat;
    p->enter_until = 0;
}

static int remote_ped(unsigned player) { return game_remote_handle ? game_remote_handle(player) : 0; }

static void on_enter_car(const srpc_enter_vehicle_t *v)
{
    veh_t *r = live(v->vehicle);
    int ped;

    if (!r || v->player >= PLAYER_IDS || v->player == samp_stats.player_id)
        return;
    ped = remote_ped(v->player);
    if (ped && !vg_ped_car(ped)) {
        vg_task_enter(ped, r->handle, v->passenger != 0);
        R[v->player].enter_until = tick_now + ENTER_MS;
        R[v->player].last = tick_now;
    }
}

static void on_exit_car(const srpc_exit_vehicle_t *v)
{
    int ped, car;

    if (v->player >= PLAYER_IDS || v->player == samp_stats.player_id)
        return;
    ped = remote_ped(v->player);
    car = ped ? vg_ped_car(ped) : 0;
    R[v->player].veh1 = 0;
    R[v->player].enter_until = 0;
    if (car) {
        vg_task_leave(ped, car);
        R[v->player].leave_until = tick_now + LEAVE_MS;
    }
}

static void on_params(const srpc_vehicle_params_t *v)
{
    veh_t *r = rec(v->id);

    if (!r)
        return;
    r->engine = v->engine;
    r->lights = v->lights;
    r->alarm = v->alarm;
    r->locked = v->doors;
    r->bonnet = v->bonnet;
    r->boot = v->boot;
    r->objective = v->objective;
    r->siren = v->siren;
    memcpy(r->door, v->door, sizeof r->door);
    if (r->state == R_LIVE)
        apply_params(r);
}

static void on_params_old(const srpc_vehicle_params_old_t *v)
{
    veh_t *r = rec(v->id);

    if (!r)
        return;
    r->objective = v->objective != 0;
    r->locked = v->doors != 0;
    if (r->state == R_LIVE)
        apply_params(r);
}

static void on_health(const srpc_id_float_t *v)
{
    veh_t *r = rec(v->id);

    if (!r || !vm_vec_ok(&v->v, 1, 10000000.0f))
        return;
    r->health = v->v < 0.0f ? 0.0f : v->v;
    if (r->state == R_LIVE)
        vg_set_health(r->handle, r->health);
}

static void on_pos(const srpc_id_vec3_t *v)
{
    veh_t *r = rec(v->id);

    if (!r || !vm_vec_ok(v->v, 3, VM_WORLD_LIMIT))
        return;
    memcpy(r->pos, v->v, sizeof r->pos);
    r->t_mode = T_NONE;
    if (r->state == R_LIVE)
        vg_teleport(r->handle, v->v);
}

static void on_angle(const srpc_id_float_t *v)
{
    veh_t *r = rec(v->id);

    if (!r || !vm_vec_ok(&v->v, 1, 100000.0f))
        return;
    r->angle = v->v;
    r->has_quat = 0;
    if (r->state == R_LIVE)
        vg_set_heading(r->handle, v->v);
}

/* Only ever about the car the local player is in: the server sends it to the driver. */
static void on_velocity(const srpc_vehicle_velocity_t *v)
{
    if (!L.in_car)
        return;
    if (v->type == 0 && vm_vec_ok(v->v, 3, VM_SPEED_LIMIT))
        vg_set_velocity(L.car, v->v);
    else if (v->type == 1 && vm_vec_ok(v->v, 3, VM_TURN_LIMIT))
        vg_set_turn(L.car, v->v);
}

static void on_damage(const srpc_vehicle_damage_t *v)
{
    veh_t *r = rec(v->id);

    if (!r)
        return;
    r->panels = v->panels;
    r->doors = v->doors;
    r->lights_dmg = v->lights;
    r->tyres = v->tyres;
    note_damage_sent(r, v->panels, v->doors, v->lights, v->tyres);
    if (r->state != R_LIVE)
        return;
    /* RepairVehicle arrives as all-clear damage; a full fix also resets the frames. */
    if (!v->panels && !v->doors && !v->lights && !v->tyres)
        vg_repair(r->handle);
    else
        vg_set_damage(r->handle, v->panels, v->doors, v->lights, v->tyres);
}

static void on_tyres(const srpc_id_val_t *v)
{
    veh_t *r = rec(v->id);

    if (!r)
        return;
    r->tyres = v->v & 15;
    if (r->state == R_LIVE)
        vg_set_damage(r->handle, r->panels, r->doors, r->lights_dmg, r->tyres);
}

static void remember_mod(veh_t *r, unsigned comp)
{
    unsigned i, free_slot = MOD_SLOTS;

    if (comp < MOD_FIRST || comp > MOD_LAST)
        return;
    for (i = 0; i < MOD_SLOTS; i++) {
        if (r->mods[i] == comp)
            return;
        if (!r->mods[i] && free_slot == MOD_SLOTS)
            free_slot = i;
    }
    if (free_slot == MOD_SLOTS)
        return;
    r->mods[free_slot] = comp;
    if (r->state == R_LIVE)
        r->mod_todo |= 1u << free_slot;
}

static void on_remove_mod(const srpc_component_t *v)
{
    veh_t *r = rec(v->id);
    unsigned i;

    if (!r || v->component < MOD_FIRST || v->component > MOD_LAST)
        return;
    for (i = 0; i < MOD_SLOTS; i++) {
        if (r->mods[i] == v->component) {
            r->mods[i] = 0;
            r->mod_todo &= ~(1u << i);
        }
    }
    if (r->state == R_LIVE && vg_mod_ok(r->handle, v->component))
        vg_remove_mod(r->handle, v->component);
}

static void on_scm_event(const srpc_scm_event_t *v)
{
    veh_t *r = rec(v->vehicle);

    if (!r)
        return;
    switch (v->event) {
    case SCM_PAINTJOB:
        r->paintjob = v->arg1 <= 2 ? (int)v->arg1 : -1;
        if (r->state == R_LIVE)
            vg_set_paintjob(r->handle, r->paintjob);
        break;
    case SCM_ADD_MOD:
        remember_mod(r, v->arg1);
        break;
    case SCM_COLOUR:
        r->c1 = v->arg1 & 127;
        r->c2 = v->arg2 & 127;
        if (r->state == R_LIVE)
            vg_set_colours(r->handle, r->c1, r->c2);
        break;
    }
}

static int plate_char(int c)
{
    if (c >= 'a' && c <= 'z')
        return c - 32;
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        return c;
    return ' ';
}

static void on_plate(const srpc_plate_t *v)
{
    veh_t *r = rec(v->id);
    char t[9];
    unsigned i;

    if (!r)
        return;
    for (i = 0; i < 8 && v->plate[i]; i++)
        t[i] = plate_char((unsigned char)v->plate[i]);
    t[i] = 0;
    if (!memcmp(r->plate, t, i + 1))
        return;
    memcpy(r->plate, t, sizeof t);
    /* A plate only reaches a car when it is created. */
    if (r->state == R_LIVE && evictable(v->id, r)) {
        r->force = 1;
        unload(r);
    }
}

static void on_interior(const srpc_id_val_t *v)
{
    veh_t *r = rec(v->id);

    if (!r)
        return;
    r->interior = v->v;
    if (r->state == R_LIVE)
        vg_set_interior(r->handle, v->v);
}

static void attach_now(veh_t *cab, veh_t *trailer)
{
    if (cab->state == R_LIVE && trailer->state == R_LIVE && vg_towed(cab->handle) != trailer->handle)
        vg_attach(trailer->handle, cab->handle);
}

static void on_attach(const srpc_trailer_t *v)
{
    veh_t *cab = rec(v->vehicle), *t = rec(v->trailer);

    if (!cab || !t)
        return;
    cab->want_trailer = v->trailer + 1;
    attach_now(cab, t);
}

static void on_detach(const srpc_id_t *v)
{
    veh_t *cab = rec(v->id);
    int t;

    if (!cab)
        return;
    cab->want_trailer = 0;
    if (cab->state == R_LIVE && (t = vg_towed(cab->handle)))
        vg_detach(t, cab->handle);
}

static void on_no_collide(const srpc_int_t *v) { no_collide = v->v != 0; }

/* ---- sync from other players ---------------------------------------------- */

static int local_drives(unsigned id) { return L.in_car && L.veh == id && L.seat == 0; }

static void set_target(veh_t *r, const float *pos, const float *quat, const float *vel,
                       const float *turn, int mode)
{
    static const float zero[3];

    memcpy(r->t_pos, pos, sizeof r->t_pos);
    memcpy(r->t_quat, quat, sizeof r->t_quat);
    memcpy(r->t_vel, vel, sizeof r->t_vel);
    memcpy(r->t_turn, turn ? turn : zero, sizeof r->t_turn);
    memcpy(r->pos, pos, sizeof r->pos);
    r->t_time = tick_now;
    r->t_mode = mode;
}

static void on_sync_vehicle(const srpc_vehicle_sync_t *s)
{
    veh_t *r = rec(s->vehicle);
    float q[4];

    if (!r || s->player >= PLAYER_IDS || s->player == samp_stats.player_id || local_drives(s->vehicle))
        return;
    memcpy(q, s->quat, sizeof q);
    if (!vm_vec_ok(s->pos, 3, VM_WORLD_LIMIT) || !vm_vec_ok(s->velocity, 3, VM_SPEED_LIMIT) || !vm_quat_norm(q))
        return;
    set_target(r, s->pos, q, s->velocity, 0, T_DRIVEN);
    memcpy(r->quat, q, sizeof q);
    r->has_quat = 1;
    r->health = s->health;
    if (r->state == R_LIVE) {
        vg_set_health(r->handle, r->health);
        if (!vg_siren(r->handle) != !s->siren)
            vg_set_siren(r->handle, s->siren);
    } else if (!r->no_game) {
        r->force = 1; /* a car somebody is driving at us has to exist */
    }
    if (s->has_trailer) {
        veh_t *t = rec(s->trailer);
        if (t) {
            r->want_trailer = s->trailer + 1;
            attach_now(r, t);
        }
    } else if (r->want_trailer) {
        srpc_id_t d;
        d.id = s->vehicle;
        on_detach(&d);
    }
    remote_seat(s->player, s->vehicle, 0);
}

static void on_sync_passenger(const srpc_passenger_sync_t *s)
{
    veh_t *r = rec(s->vehicle);

    if (!r || s->player >= PLAYER_IDS || s->player == samp_stats.player_id || !s->seat)
        return;
    if (vm_vec_ok(s->pos, 3, VM_WORLD_LIMIT) && r->t_mode == T_NONE && !local_uses(s->vehicle))
        memcpy(r->pos, s->pos, sizeof r->pos);
    remote_seat(s->player, s->vehicle, s->seat);
}

static void on_sync_unoccupied(const srpc_unoccupied_sync_t *s)
{
    veh_t *r = rec(s->vehicle);
    float a[3], b[3], q[4];

    if (!r || s->player == samp_stats.player_id || local_drives(s->vehicle))
        return;
    memcpy(a, s->roll, sizeof a);
    memcpy(b, s->direction, sizeof b);
    if (!vm_vec_ok(s->pos, 3, VM_WORLD_LIMIT) || !vm_vec_ok(s->velocity, 3, VM_SPEED_LIMIT) ||
        !vm_vec_ok(s->turn, 3, VM_TURN_LIMIT) || !vm_orthonormal(a, b))
        return;
    vm_vectors_to_quat(a, b, q);
    if (!vm_quat_norm(q))
        return;
    set_target(r, s->pos, q, s->velocity, s->turn, T_FREE);
    memcpy(r->quat, q, sizeof q);
    r->has_quat = 1;
    if (vm_vec_ok(&s->health, 1, 10000000.0f) && s->seat) {
        r->health = s->health;
        if (r->state == R_LIVE)
            vg_set_health(r->handle, r->health);
    }
}

static void on_sync_trailer(const srpc_trailer_sync_t *s)
{
    veh_t *r = rec(s->vehicle);
    float q[4];

    if (!r || s->player == samp_stats.player_id)
        return;
    memcpy(q, s->quat, sizeof q);
    if (!vm_vec_ok(s->pos, 3, VM_WORLD_LIMIT) || !vm_vec_ok(s->velocity, 3, VM_SPEED_LIMIT) ||
        !vm_vec_ok(s->turn, 3, VM_TURN_LIMIT) || !vm_quat_norm(q))
        return;
    set_target(r, s->pos, q, s->velocity, s->turn, T_FREE);
    memcpy(r->quat, q, sizeof q);
    r->has_quat = 1;
}

/* ---- per frame: cars ------------------------------------------------------ */

/* The car keeps running through the game's physics, so it rides its suspension and
 * hits things. We steer its speed towards where the packets say it should be, and only
 * teleport when it has fallen too far behind. */
static void drive_car(veh_t *r)
{
    vg_state_t st;
    float age, want[3], speed[3], q[4], err2;
    unsigned ms = tick_now - r->t_time;
    int i;

    if (ms > SYNC_LOST_MS) {
        r->t_mode = T_NONE;
        return;
    }
    age = (float)(ms > PREDICT_MS ? PREDICT_MS : ms) / VM_STEP_MS;
    vg_read(r->handle, &st);
    for (i = 0; i < 3; i++)
        want[i] = r->t_pos[i] + r->t_vel[i] * age;
    err2 = vm_dist2(want, st.pos);
    if (err2 > SNAP_DIST2) {
        vg_teleport(r->handle, want);
        vg_set_rotation(r->handle, r->t_quat);
        vg_set_velocity(r->handle, r->t_vel);
        vg_set_turn(r->handle, r->t_turn);
        return;
    }
    if (err2 < SETTLED_DIST2 && vm_dot3(r->t_vel, r->t_vel) < SETTLED_SPEED2)
        return; /* parked: let it sleep */
    for (i = 0; i < 3; i++)
        speed[i] = r->t_vel[i] + (want[i] - st.pos[i]) * CATCH_UP;
    vg_set_velocity(r->handle, speed);
    /* Same share per second at 50 fps: 1 - (1 - 0.35)^(1/2). */
    vm_quat_nlerp(st.quat, r->t_quat, fps_high ? 0.194f : TURN_RATE, q);
    vg_set_rotation(r->handle, q);
    vg_set_turn(r->handle, r->t_turn);
}

static void cars_tick(void)
{
    unsigned i;

    for (i = 0; i < n_ids; i++) {
        unsigned id = ids[i];
        veh_t *r = &V[id];
        int off;

        if (r->state != R_LIVE)
            continue;
        if (!vg_alive(r->handle)) { /* the game removed it, so nothing is left to clean up */
            r->handle = 0;
            r->blip = 0;
            r->state = R_RECORD;
            r->t_mode = T_NONE;
            r->retry_at = tick_now + RETRY_MS;
            n_live--;
            model_unref(r->model);
            continue;
        }
        if (vg_wrecked(r->handle)) {
            if (!r->dead_sent) {
                static unsigned char buf[8];
                bs_t b;
                r->dead_sent = 1;
                r->t_mode = T_NONE;
                bs_wr_init(&b, buf, sizeof buf);
                srpc_enc_vehicle_destroyed(&b, id);
                rak_rpc(SRPC_C2S_VEHICLE_DESTROYED, buf, b.bits, RAK_RELIABLE, 0);
                slog("veh: %d wrecked", id);
            }
            continue;
        }
        if (r->want_trailer) {
            veh_t *t = rec(r->want_trailer - 1);
            if (t)
                attach_now(r, t);
            else
                r->want_trailer = 0;
        }
        off = no_collide && r->t_mode == T_DRIVEN;
        if (off != r->collide_off) {
            vg_set_collision(r->handle, !off);
            r->collide_off = off;
        }
        if (r->t_mode != T_NONE && !local_drives(id))
            drive_car(r);
    }
}

/* ---- per frame: remote players -------------------------------------------- */

static void set_hold(unsigned player, remote_t *p, int on)
{
    if (p->held != on && game_remote_hold) {
        game_remote_hold(player, on);
        p->held = on;
    }
}

static void put_out(int ped, int car)
{
    vg_state_t st;
    float at[3];
    int i;

    vg_read(car, &st);
    for (i = 0; i < 3; i++)
        at[i] = st.pos[i] - st.right[i] * 2.5f;
    vg_warp_out(ped, at);
}

static void seat_remote(remote_t *p, veh_t *r, int ped, int in)
{
    if (in == r->handle || tick_now - p->warp_at < WARP_MS)
        return;
    p->warp_at = tick_now;
    if (in)
        put_out(ped, in);
    if (p->seat == 0) {
        if (!vg_driver(r->handle))
            vg_warp_driver(ped, r->handle);
    } else if (p->seat - 1u < vg_seats(r->handle) && !vg_passenger(r->handle, p->seat - 1)) {
        vg_warp_passenger(ped, r->handle, p->seat - 1);
    }
}

static void remotes_tick(void)
{
    unsigned i;

    for (i = 0; i < PLAYER_IDS; i++) {
        remote_t *p = &R[i];
        int ped, in, leaving, entering;
        veh_t *r;

        if (!p->veh1 && !p->held && !p->leave_until && !p->enter_until)
            continue;
        ped = remote_ped(i);
        if (!vg_ped_alive(ped)) {
            memset(p, 0, sizeof *p); /* streamed out: game.c has deleted the ped */
            continue;
        }
        in = vg_ped_car(ped);
        leaving = (int)(p->leave_until - tick_now) > 0;
        entering = (int)(p->enter_until - tick_now) > 0;
        if (p->leave_until && !leaving) {
            p->leave_until = 0;
            if (in) { /* the animation never finished */
                put_out(ped, in);
                in = 0;
            }
        }
        if (p->enter_until && !entering)
            p->enter_until = 0;
        r = p->veh1 ? rec(p->veh1 - 1) : 0;
        if (p->veh1 && (!r || tick_now - p->last > SYNC_LOST_MS)) {
            p->veh1 = 0;
            if (in && !leaving) {
                put_out(ped, in);
                in = 0;
            }
        }
        if (p->veh1 && r->state == R_LIVE && !leaving)
            seat_remote(p, r, ped, in);
        set_hold(i, p, p->veh1 || entering || leaving);
    }
}

/* ---- per frame: the local player ------------------------------------------ */

static unsigned read_keys(const vg_pad_t *p)
{
    unsigned k = 0;

    if (p->cross) k |= K_SPRINT;
    if (p->square) k |= K_JUMP;
    if (p->r1) k |= K_HANDBRAKE;
    if (p->circle) k |= K_FIRE;
    if (p->l1) k |= K_ACTION;
    if (p->l3) k |= K_CROUCH;
    if (p->triangle) k |= K_SECONDARY;
    if (p->l2) k |= K_LOOK_LEFT;
    if (p->r2) k |= K_LOOK_RIGHT;
    if (p->r3) k |= K_SUBMISSION;
    return k;
}

static void send_trailer(int t, unsigned id)
{
    static unsigned char buf[64];
    srpc_out_trailer_t o;
    vg_state_t st;
    bs_t b;

    vg_read(t, &st);
    memset(&o, 0, sizeof o);
    o.vehicle = id;
    memcpy(o.pos, st.pos, sizeof o.pos);
    memcpy(o.quat, st.quat, sizeof o.quat);
    memcpy(o.velocity, st.vel, sizeof o.velocity);
    memcpy(o.turn, st.turn, sizeof o.turn);
    bs_wr_init(&b, buf, sizeof buf);
    srpc_enc_sync_trailer(&b, &o);
    if (!b.err)
        rak_send(buf, b.bits, RAK_UNRELIABLE_SEQ, 0);
}

static void send_driver(veh_t *r)
{
    static unsigned char buf[80];
    srpc_out_vehicle_t o;
    vg_state_t st;
    vg_pad_t pad;
    bs_t b;
    int t, tid;

    vg_read(r->handle, &st);
    vg_read_pad(&pad);
    memset(&o, 0, sizeof o);
    o.vehicle = L.veh;
    o.lr = (unsigned short)pad.lx;
    o.ud = (unsigned short)pad.ly;
    o.keys = read_keys(&pad);
    memcpy(o.quat, st.quat, sizeof o.quat);
    memcpy(o.pos, st.pos, sizeof o.pos);
    memcpy(o.velocity, st.vel, sizeof o.velocity);
    o.health = vg_health(r->handle);
    vg_local_vitals(&o.player_health, &o.player_armour);
    o.siren = vg_siren(r->handle) != 0;
    t = vg_towed(r->handle);
    tid = id_of(t);
    if (tid > 0)
        o.trailer = tid;
    bs_wr_init(&b, buf, sizeof buf);
    srpc_enc_sync_vehicle(&b, &o);
    if (!b.err && rak_send(buf, b.bits, RAK_UNRELIABLE_SEQ, 0) == 0)
        samp_stats.sync_out++;
    if (tid > 0)
        send_trailer(t, tid);
    memcpy(r->pos, st.pos, sizeof r->pos);
}

static void send_unoccupied(veh_t *r, unsigned id, unsigned seat)
{
    static unsigned char buf[80];
    srpc_out_unoccupied_t o;
    vg_state_t st;
    bs_t b;

    vg_read(r->handle, &st);
    memset(&o, 0, sizeof o);
    if (!vm_orthonormal(st.right, st.fwd))
        return;
    o.vehicle = id;
    o.seat = seat;
    memcpy(o.roll, st.right, sizeof o.roll);
    memcpy(o.direction, st.fwd, sizeof o.direction);
    memcpy(o.pos, st.pos, sizeof o.pos);
    memcpy(o.velocity, st.vel, sizeof o.velocity);
    memcpy(o.turn, st.turn, sizeof o.turn);
    o.health = vg_health(r->handle);
    bs_wr_init(&b, buf, sizeof buf);
    srpc_enc_sync_unoccupied(&b, &o);
    if (!b.err)
        rak_send(buf, b.bits, RAK_UNRELIABLE_SEQ, 0);
}

static void send_passenger(veh_t *r)
{
    static unsigned char buf[32];
    srpc_out_passenger_t o;
    vg_state_t st;
    vg_pad_t pad;
    bs_t b;

    vg_read(r->handle, &st);
    vg_read_pad(&pad);
    memset(&o, 0, sizeof o);
    o.vehicle = L.veh;
    o.seat_flags = L.seat > 3 ? 3 : L.seat; /* the wire has 2 bits for it */
    vg_local_vitals(&o.health, &o.armour);
    o.lr = (unsigned short)pad.lx;
    o.ud = (unsigned short)pad.ly;
    o.keys = read_keys(&pad);
    memcpy(o.pos, st.pos, sizeof o.pos);
    bs_wr_init(&b, buf, sizeof buf);
    srpc_enc_sync_passenger(&b, &o);
    if (!b.err && rak_send(buf, b.bits, RAK_UNRELIABLE_SEQ, 0) == 0)
        samp_stats.sync_out++;
    if (!vg_driver(r->handle))
        send_unoccupied(r, L.veh, o.seat_flags);
}

static void send_damage(veh_t *r)
{
    static unsigned char buf[16];
    unsigned p, d, l, t;
    bs_t b;

    vg_get_damage(r->handle, &p, &d, &l, &t);
    if (p == r->sent_panels && d == r->sent_doors && l == r->sent_lights && t == r->sent_tyres)
        return;
    note_damage_sent(r, p, d, l, t);
    bs_wr_init(&b, buf, sizeof buf);
    srpc_enc_vehicle_damage(&b, L.veh, p, d, l, t);
    rak_rpc(SRPC_C2S_VEHICLE_DAMAGE, buf, b.bits, RAK_RELIABLE, 0);
}

static int find_seat(int ped, int car)
{
    unsigned i, seats = vg_seats(car);

    if (vg_driver(car) == ped)
        return 0;
    for (i = 0; i < seats && i < 8; i++)
        if (vg_passenger(car, i) == ped)
            return i + 1;
    return 1;
}

static void send_enter(unsigned id, int passenger)
{
    static unsigned char buf[8];
    bs_t b;

    bs_wr_init(&b, buf, sizeof buf);
    srpc_enc_enter_vehicle(&b, id, passenger);
    rak_rpc(SRPC_C2S_ENTER_VEHICLE, buf, b.bits, RAK_RELIABLE, 0);
}

static void send_exit(unsigned id)
{
    static unsigned char buf[8];
    bs_t b;

    bs_wr_init(&b, buf, sizeof buf);
    srpc_enc_exit_vehicle(&b, id);
    rak_rpc(SRPC_C2S_EXIT_VEHICLE, buf, b.bits, RAK_RELIABLE, 0);
}

/* Foot passengers have no button of their own in the PS2 game: SA-MP's G becomes
 * D-pad right, which does nothing on foot once the story is off. */
static void passenger_button(int ped)
{
    vg_pad_t pad;
    unsigned i;
    int was = L.dpad, best = -1;
    float me[3], best_d = PASSENGER_REACH2;

    vg_read_pad(&pad);
    L.dpad = pad.dpad_right;
    if (!pad.dpad_right || was)
        return;
    vg_local_pos(me);
    for (i = 0; i < n_ids; i++) {
        veh_t *r = &V[ids[i]];
        float d;
        if (r->state != R_LIVE || r->locked == 1 || vg_wrecked(r->handle) || !vg_seats(r->handle))
            continue;
        d = vm_dist2(r->pos, me);
        if (d < best_d) {
            best_d = d;
            best = ids[i];
        }
    }
    if (best >= 0)
        vg_task_enter(ped, V[best].handle, 1);
}

static void local_in_car(int ped, int car, unsigned id)
{
    veh_t *r = &V[id];

    if (!L.in_car || L.car != (unsigned)car) {
        unsigned p, d, l, t;
        L.in_car = 1;
        L.car = car;
        L.veh = id;
        L.exit_sent = 0;
        L.enter_car = 0;
        L.sync_at = 0;
        vg_get_damage(car, &p, &d, &l, &t);
        note_damage_sent(r, p, d, l, t);
        r->t_mode = T_NONE;
        slog("veh: in %d seat %d", id, find_seat(ped, car));
    }
    L.seat = find_seat(ped, car);
    if (!L.exit_sent && vg_local_leaving()) {
        L.exit_sent = 1;
        if (!L.forced_exit)
            send_exit(id);
    }
    if (L.kick_at && (int)(tick_now - L.kick_at) >= 0) {
        L.kick_at = 0;
        put_out(ped, car);
        return;
    }
    if (tick_now - L.sync_at + 5 >= (L.seat ? PASSENGER_MS : samp_stats.car_ms ? samp_stats.car_ms : DRIVER_MS)) {
        L.sync_at = tick_now;
        if (L.seat)
            send_passenger(r);
        else
            send_driver(r);
    }
    if (L.seat == 0 && tick_now - L.damage_at >= DAMAGE_MS) {
        L.damage_at = tick_now;
        send_damage(r);
    }
    /* Getting in starts the engine, which a script may have switched off. */
    if (r->engine == 0 && tick_now - L.engine_at >= ENGINE_MS) {
        L.engine_at = tick_now;
        if (vg_engine(car))
            vg_set_engine(car, 0);
    }
}

static void local_on_foot(int ped)
{
    int passenger, target, id;

    if (L.in_car)
        clear_local_car();
    target = vg_local_entering(&passenger);
    if (target && target != L.enter_car) {
        id = id_of(target);
        if (id >= 0)
            send_enter(id, passenger);
    }
    L.enter_car = target;
    if (!target)
        passenger_button(ped);
}

static void local_tick(void)
{
    int ped = vg_local_ped(), car, id;

    if (!ped)
        return;
    if (L.put1) {
        veh_t *r = rec(L.put1 - 1);
        if (!r || tick_now - L.put_at > PUT_LIMIT_MS) {
            L.put1 = 0;
        } else if (r->state == R_LIVE) {
            put_local(r, L.put_seat);
            L.put1 = 0;
        }
    }
    car = vg_ped_car(ped);
    id = id_of(car);
    if (car && id >= 0)
        local_in_car(ped, car, id);
    else
        local_on_foot(ped);
}

/* Empty cars the player knocks about. The server takes this from whoever is closest,
 * so a report from anyone else is simply dropped. */
static void unoccupied_tick(void)
{
    float me[3];
    unsigned i, tries = n_ids < 32 ? n_ids : 32;

    vg_local_pos(me);
    for (i = 0; i < tries; i++) {
        unsigned id;
        veh_t *r;
        vg_state_t st;
        int moving;

        if (++unocc_cursor >= n_ids)
            unocc_cursor = 0;
        id = ids[unocc_cursor];
        r = &V[id];
        if (r->state != R_LIVE || local_uses(id) || r->dead_sent || r->t_mode != T_NONE ||
            tick_now - r->t_time < UNOCC_QUIET_MS || tick_now - r->born < UNOCC_SETTLE_MS ||
            vg_driver(r->handle) || vg_being_towed(r->handle) || vm_dist2(r->pos, me) > UNOCC_DIST2)
            continue; /* a towed trailer goes in 210: an unoccupied report makes the server detach it */
        vg_read(r->handle, &st);
        moving = vm_dot3(st.vel, st.vel) > MOVING2;
        if (!moving && !r->moving)
            continue;
        r->moving = moving; /* a moving car gets one more report once it has stopped */
        send_unoccupied(r, id, 0);
        return;
    }
}

/* ---- module --------------------------------------------------------------- */

void feat_vehicle_reset(void)
{
    while (n_ids)
        forget(ids[n_ids - 1]);
    memset(R, 0, sizeof R);
    memset(&L, 0, sizeof L);
    if (mod_wait) {
        vg_mod_release(mod_wait);
        mod_wait = 0;
    }
    no_collide = 0;
}

int feat_vehicle_in_car(void) { return L.in_car; }

/* SCM handle of a server vehicle that exists in the game right now, or 0. */
int feat_vehicle_handle(unsigned id)
{
    veh_t *r = live(id);
    return r ? r->handle : 0;
}

/* SetPlayerPos on someone in a car takes him out of it, while the game's SET_CHAR_COORDS
 * would carry the car along. */
void feat_vehicle_eject(const float *pos)
{
    int ped = vg_local_ped();

    if (vg_ped_car(ped))
        vg_warp_out(ped, pos);
}

void feat_vehicle_foot_sync(unsigned player)
{
    remote_t *p;
    int ped, in;

    if (player >= PLAYER_IDS)
        return;
    p = &R[player];
    if (!p->veh1)
        return;
    p->veh1 = 0;
    ped = remote_ped(player);
    in = ped ? vg_ped_car(ped) : 0;
    if (in && (int)(p->leave_until - tick_now) <= 0)
        put_out(ped, in);
}

static void (*next_restart)(void); /* other modules share this slot */

static void on_restart(void)
{
    feat_vehicle_reset();
    if (next_restart)
        next_restart();
}

void feat_vehicle_init(samp_rpc_handlers_t *h)
{
    h->vehicle_stream_in = on_stream_in;
    h->vehicle_stream_out = on_stream_out;
    h->put_in_vehicle = on_put_in;
    h->remove_from_vehicle = on_remove_from;
    h->enter_vehicle = on_enter_car;
    h->exit_vehicle = on_exit_car;
    h->set_vehicle_params = on_params;
    h->set_vehicle_params_old = on_params_old;
    h->set_vehicle_health = on_health;
    h->set_vehicle_pos = on_pos;
    h->set_vehicle_angle = on_angle;
    h->set_vehicle_velocity = on_velocity;
    h->set_vehicle_damage = on_damage;
    h->set_tyre_status = on_tyres;
    h->remove_component = on_remove_mod;
    h->scm_event = on_scm_event;
    h->set_plate = on_plate;
    h->link_vehicle_interior = on_interior;
    h->attach_trailer = on_attach;
    h->detach_trailer = on_detach;
    h->disable_remote_vehicle_collisions = on_no_collide;
    next_restart = h->game_mode_restart;
    h->game_mode_restart = on_restart;
    h->sync_vehicle = on_sync_vehicle;
    h->sync_passenger = on_sync_passenger;
    h->sync_unoccupied = on_sync_unoccupied;
    h->sync_trailer = on_sync_trailer;
}

void feat_vehicle_tick(unsigned now)
{
    tick_now = now;
    if (samp_state() != SAMP_SPAWNED) {
        if (was_spawned)
            feat_vehicle_reset();
        was_spawned = 0;
        return;
    }
    was_spawned = 1;
    if (now - stream_at >= STREAM_MS) {
        stream_at = now;
        stream_step();
    }
    load_step();
    mod_step();
    local_tick();
    cars_tick();
    remotes_tick();
    if (now - unocc_at >= UNOCCUPIED_MS) {
        unocc_at = now;
        unoccupied_tick();
    }
}

void feat_vehicle_draw(void) {}
