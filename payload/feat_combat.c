/* Weapons, health, damage, death, animations and local sync detail. Owned by one work
 * stream: see feat.h. Logic only: everything that touches the game is in
 * feat_combat_game.c, so this file also runs on the host for the tests in wip/combat.
 *
 * Who decides what, as on the PC client:
 *  - Our own health is ours. Whoever shot us told the server with a bullet sync, the
 *    server relayed it, and we take the damage here and report it with 115.
 *  - A remote ped's health in our game means nothing. It is held high so the ped never
 *    dies locally, and the loss it shows is what we report as our hit on that player.
 *  - Death goes out as 53. The game runs its own wasted screen and restart, pointed at
 *    the server's spawn point, then we send 52. Sync stops in between, because the server
 *    treats any foot sync as being back on foot. */
#include "feat.h"
#include "feat_combat.h"
#include "feat_combat_game.h"
#include "game.h"
#include "rak.h"
#include "util.h"

/* From feat_vehicle.c, when it has them. Weak so the payload links either way: spectating
 * a car just does nothing until then. */
int feat_vehicle_handle(unsigned id) __attribute__((weak));
void feat_vehicle_eject(const float *pos) __attribute__((weak));

#define MAX_PLAYERS     1000
#define INVALID_ID      0xFFFFu
#define REMOTE_HEALTH   1000.0f /* a remote ped must survive anything our game does to it */
#define WORLD_LIMIT     20000.0f

/* SA-MP's key bits are the pad's buttons: see closure_player_key_codec in the reference. */
#define KEY_ACTION      1
#define KEY_CROUCH      2
#define KEY_FIRE        4
#define KEY_SPRINT      8
#define KEY_SECONDARY   16
#define KEY_JUMP        32
#define KEY_LOOK_RIGHT  64
#define KEY_AIM         128     /* KEY_HANDBRAKE */
#define KEY_LOOK_LEFT   256
#define KEY_SUBMISSION  512
#define KEY_WALK        1024
#define KEY_ANALOG_UP   2048
#define KEY_ANALOG_DOWN 4096
#define KEY_ANALOG_LEFT 8192
#define KEY_ANALOG_RIGHT 16384

#define STICK_ON        64      /* right stick past this counts as an analog key */
#define WALK_STICK      60      /* left stick below this walks: the game scales by 1/60 */

/* special actions */
#define SA_NONE     0
#define SA_DUCK     1
#define SA_JETPACK  2
#define SA_DANCE1   5
#define SA_DANCE4   8
#define SA_HANDSUP  10

/* body part and weapon numbers on the wire */
#define PART_TORSO      3
#define WEAPON_UNKNOWN  255
#define WEAPON_EXPLOSION 51
#define HIT_NONE        0
#define HIT_PLAYER      1

/* PED animation ids, in SA-MP's sorted list. Only a state guess: see local_anim(). */
#define ANIM_IDLE       1188
#define ANIM_WALK       1256
#define ANIM_RUN        1223
#define ANIM_SPRINT     1245
#define ANIM_CROUCH     1273

/* game move states for TASK_GO_STRAIGHT_TO_COORD */
#define MOVE_WALK       4
#define MOVE_RUN        6
#define MOVE_SPRINT     7

#define STATS_MS        1000
#define WEAPONS_MS      500
#define DAMAGE_MS       250
#define TASK_MS         200
#define AIM_FRESH_MS    500
#define SHOT_FRESH_MS   300
#define RESPAWN_MAX_MS  20000
#define ARM_DELAY_MS    200
#define MELEE_RANGE2    (1.8f * 1.8f)
#define MELEE_MS        400
#define MISS_RANGE      60.0f
#define MAX_SHOTS_TICK  8
#define ANIM_QUEUE      8
#define ANIM_WAIT_MS    5000
#define ANIM_ONCE_MS    2000    /* ped left alone this long for a one-shot of unknown length */
#define ANIM_LOOP_MS    3600000u /* until the player moves or the server clears it */
#define ANIM_NAME_MAX   24      /* the buffers TASK_PLAY_ANIM reads into */
#define ANIM_LIB_MAX    16

typedef struct {
    int valid;
    unsigned skin;
    float pos[3], angle;
    unsigned weapons[3], ammo[3];
} spawn_t;

typedef struct {
    float from[3], to[3];
    unsigned start, time, cut;
    int active;
} cam_move_t;

typedef struct {
    int used;
    unsigned id;     /* MAX_PLAYERS means the local ped */
    char lib[ANIM_LIB_MAX], name[ANIM_NAME_MAX];
    float delta;
    int loop, lock_x, lock_y, freeze, time;
    unsigned deadline;
} anim_job_t;

typedef struct {
    unsigned char used, dying, synced, weapon, armed, special, shown_special;
    unsigned char health, armour, style, skill;
    unsigned short keys, old_keys, skin;
    int handle;
    float pos[3], fwd[2];
    float aim_front[3], aim_pos[3], shot[3];
    unsigned aim_ms, shot_ms, task_ms, melee_ms, anim_ms;
} remote_t;

static remote_t remotes[MAX_PLAYERS];

static struct {
    int seen;           /* samp_state() has reached SAMP_SPAWNED, one frame ago at least */
    int spawned;        /* the first spawn is done */
    int wasted;         /* 53 is out, waiting for the game to bring us back */
    int reselect;       /* go through class selection at the next death */
    int spectating;
    unsigned wasted_ms;
    float hp, armour;   /* what we last accounted for */
    unsigned special, wanted, drunk;
    unsigned aim_rate;
    /* damage waiting to go out, and who last hurt us for the death message */
    float pend;
    unsigned pend_issuer, pend_weapon, pend_ms;
    unsigned killer, reason;
    /* timers */
    unsigned stats_ms, weapons_ms, aim_ms, spec_ms;
    /* shots */
    unsigned shot_weapon, shot_ammo;
    unsigned hits[MAX_SHOTS_TICK], nhits;
    float hit_pos[MAX_SHOTS_TICK][3];
    unsigned hit_weapon;
    cg_slot_t slots[CG_SLOTS];
    int area;
} me;

static spawn_t spawn;
static int restarted;   /* game mode restart: main.c won't move us at the next spawn */
static cam_move_t cam_pos, cam_look;
static anim_job_t jobs[ANIM_QUEUE];
static unsigned tick_now;

/* The same slots may interest other streams, so each one keeps whoever was there. */
static void (*next_stream_in)(const srpc_player_stream_in_t *);
static void (*next_stream_out)(const srpc_id_t *);
static void (*next_quit)(const srpc_player_quit_t *);
static void (*next_init)(const srpc_init_game_t *);
static void (*next_restart)(void);
static void (*next_foot)(const srpc_foot_sync_t *);
static void (*next_death)(const srpc_id_t *);

static float fabs_f(float v) { return v < 0.0f ? -v : v; }

/* NaN fails every comparison, so this rejects it as well as anything off the map. */
static int pos_ok(const float *p)
{
    int i;
    for (i = 0; i < 3; i++)
        if (!(p[i] > -WORLD_LIMIT && p[i] < WORLD_LIMIT))
            return 0;
    return 1;
}

static int unit_ok(const float *v)
{
    int i;
    for (i = 0; i < 3; i++)
        if (!(v[i] >= -2.0f && v[i] <= 2.0f))
            return 0;
    return 1;
}

static unsigned my_id(void) { return samp_stats.player_id; }

static int handle_of(unsigned id) { return game_remote_handle(id); }

static remote_t *remote(unsigned id)
{
    return id < MAX_PLAYERS && remotes[id].used ? &remotes[id] : 0;
}

/* ---- weapons ------------------------------------------------------------- */

unsigned feat_combat_weapon_ok(unsigned w) { return w <= 18 || (w >= 22 && w <= 46); }

static int is_gun(unsigned w) { return (w >= 22 && w <= 34) || w == 38; }
static int is_melee(unsigned w) { return w <= 15; }
static int is_weapon(unsigned w) { return feat_combat_weapon_ok(w) && w != 0; }

/* Damage of one hit, from the 0.3.7 weapon data the PC client applies. Shotguns are a
 * whole blast here, since one bullet sync stands for the shot. */
float feat_combat_weapon_damage(unsigned w)
{
    switch (w) {
    case 16: case 35: case 36: case 39: return 82.5f;
    case 17: return 0.33f;
    case 22: return 8.25f;
    case 23: return 13.2f;
    case 24: return 46.2f;
    case 25: case 26: return 33.0f;
    case 27: return 39.6f;
    case 28: case 32: return 6.6f;
    case 29: return 8.25f;
    case 30: case 31: return 9.9f;
    case 33: return 24.75f;
    case 34: return 41.25f;
    case 38: return 46.2f;
    }
    return 1.32f;
}

static void send_rpc(unsigned id, const bs_t *b) { rak_rpc(id, b->d, b->bits, RAK_RELIABLE, 0); }
static void send_sync(const bs_t *b) { rak_send(b->d, b->bits, RAK_UNRELIABLE_SEQ, 0); }

static unsigned spawn_armed, arm_at;
static int arm_due;

/* Weapons whose model is still streaming in, given in order once it is there. */
#define GIVE_QUEUE 16
static struct { unsigned w, ammo; int arm; } give_q[GIVE_QUEUE];
static int give_n;

static void give_local(unsigned w, unsigned ammo, int arm)
{
    if (!is_weapon(w))
        return;
    if (ammo > 99999)
        ammo = 99999;
    if (give_n || !cg_load_weapon(w)) {
        if (give_n < GIVE_QUEUE) {
            give_q[give_n].w = w;
            give_q[give_n].ammo = ammo;
            give_q[give_n].arm = arm;
            give_n++;
        }
        return;
    }
    cg_give_weapon(cg_local_handle(), w, ammo);
    if (arm) {
        cg_arm_weapon(cg_local_handle(), w);
        /* Around a spawn this can land in the frame the skin changes, and not take. */
        spawn_armed = w;
        arm_due = 1;
    }
}

static void give_queue_tick(void)
{
    int i;
    while (give_n && cg_load_weapon(give_q[0].w)) {
        unsigned w = give_q[0].w, ammo = give_q[0].ammo;
        int arm = give_q[0].arm;
        for (i = 1; i < give_n; i++)
            give_q[i - 1] = give_q[i];
        give_n--;
        cg_give_weapon(cg_local_handle(), w, ammo);
        if (arm) {
            cg_arm_weapon(cg_local_handle(), w);
            spawn_armed = w;
            arm_due = 1;
        }
    }
}

static void on_give_weapon(const srpc_weapon_t *w) { give_local(w->weapon, w->ammo, 1); }
static void on_reset_weapons(void) { give_n = 0; cg_remove_weapons(cg_local_handle()); }

static void on_set_armed(const srpc_id_t *w)
{
    cg_slot_t s[CG_SLOTS];
    int i;
    if (!feat_combat_weapon_ok(w->id))
        return;
    /* The game would switch to the slot even with nothing in it. */
    cg_read_slots(s);
    for (i = 0; i < CG_SLOTS; i++)
        if (s[i].weapon == w->id || w->id == 0) {
            cg_arm_weapon(cg_local_handle(), w->id);
            arm_due = 0;
            return;
        }
}

static void on_set_ammo(const srpc_weapon_t *w)
{
    if (is_weapon(w->weapon))
        cg_set_ammo(cg_local_handle(), w->weapon, w->ammo > 99999 ? 99999 : w->ammo);
}

/* ---- local sync detail --------------------------------------------------- */

unsigned feat_combat_keys(const void *pad, int lr, int ud)
{
    const cg_pad_t *p = pad;
    unsigned k = 0;
    int m = (lr < 0 ? -lr : lr) > (ud < 0 ? -ud : ud) ? (lr < 0 ? -lr : lr) : (ud < 0 ? -ud : ud);

    if (p->l1) k |= KEY_ACTION;
    if (p->l3) k |= KEY_CROUCH;
    if (p->circle) k |= KEY_FIRE;
    if (p->cross) k |= KEY_SPRINT;
    if (p->triangle) k |= KEY_SECONDARY;
    if (p->square) k |= KEY_JUMP;
    if (p->r2) k |= KEY_LOOK_RIGHT;
    if (p->r1) k |= KEY_AIM;
    if (p->l2) k |= KEY_LOOK_LEFT;
    if (p->r3) k |= KEY_SUBMISSION;
    /* The pad has no walk button: a small stick push is how you walk here. */
    if (m > 0 && m < WALK_STICK) k |= KEY_WALK;
    if (p->ry < -STICK_ON) k |= KEY_ANALOG_UP;
    if (p->ry > STICK_ON) k |= KEY_ANALOG_DOWN;
    if (p->rx < -STICK_ON) k |= KEY_ANALOG_LEFT;
    if (p->rx > STICK_ON) k |= KEY_ANALOG_RIGHT;
    return k;
}

/* An estimate from what the ped is doing, not the real animation: reading that needs the
 * ped's blend tree. Flags stay 0, and receivers only store this for scripts. */
static unsigned local_anim(const cg_ped_t *p, unsigned keys)
{
    float v2 = p->vel[0] * p->vel[0] + p->vel[1] * p->vel[1];

    if (p->in_car)
        return 0;
    if (p->ducking)
        return ANIM_CROUCH;
    if (v2 < 0.0004f)
        return ANIM_IDLE;
    if (keys & KEY_SPRINT)
        return ANIM_SPRINT;
    return keys & KEY_WALK ? ANIM_WALK : ANIM_RUN;
}

void feat_combat_local(samp_local_t *l)
{
    cg_pad_t pad;
    cg_ped_t p;
    int lr = 0, ud = 0;
    unsigned keys;

    if (!cg_read_local(&p))
        return;
    cg_pad(&pad, &lr, &ud);
    keys = feat_combat_keys(&pad, lr, ud);
    l->lr = (unsigned short)lr;
    l->ud = (unsigned short)ud;
    l->keys = (unsigned short)keys;
    /* The server drops the whole packet for a weapon id it does not know. */
    l->weapon = feat_combat_weapon_ok(p.weapon) ? p.weapon : 0;
    l->special = p.ducking ? SA_DUCK : me.special;
#ifdef SAMP_LOCAL_HAS_COMBAT
    l->anim_id = local_anim(&p, keys);
    l->anim_flags = 0;
    l->hold = me.wasted || me.spectating;
#endif
}

/* ---- spawn --------------------------------------------------------------- */

static void store_spawn(unsigned skin, const float *pos, float angle, const unsigned *w, const unsigned *a)
{
    int i;
    if (!pos_ok(pos))
        return;
    spawn.valid = 1;
    spawn.skin = feat_combat_safe_skin(skin);
    spawn.angle = angle;
    for (i = 0; i < 3; i++) {
        spawn.pos[i] = pos[i];
        spawn.weapons[i] = w[i];
        spawn.ammo[i] = a[i];
    }
}

static void on_class(const srpc_class_t *c) { store_spawn(c->skin, c->spawn, c->angle, c->weapons, c->ammo); }
static void on_spawn_info(const srpc_spawn_info_t *c)
{
    int i;
    store_spawn(c->skin, c->spawn, c->angle, c->weapons, c->ammo);
    /* The PC client spawns at the last spawn info, whichever RPC brought it, and main.c
     * places the first spawn from samp_stats. */
    if (!pos_ok(c->spawn))
        return;
    samp_stats.skin = c->skin;
    for (i = 0; i < 3; i++)
        samp_stats.spawn[i] = c->spawn[i];
}

static void track_health(void)
{
    cg_ped_t p;
    if (cg_read_local(&p)) {
        me.hp = p.health;
        me.armour = p.armour;
    }
}

/* What the PC client does on spawn from its stored spawn info. The server only knows
 * about weapons after this: class weapons are given by the client. */
static void prepare_body(void)
{
    int i;
    unsigned last = 0;

    cg_no_death_penalty();
    cg_set_bullet_proof(1);
    cg_remove_weapons(cg_local_handle());
    cg_set_health(100.0f);
    cg_set_armour(0.0f);
    if (spawn.valid) {
        cg_set_heading(spawn.angle);
        for (i = 0; i < 3; i++)
            if (spawn.ammo[i] && is_weapon(spawn.weapons[i])) {
                give_local(spawn.weapons[i], spawn.ammo[i], 0);
                last = spawn.weapons[i];
            }
    }
    cg_arm_weapon(cg_local_handle(), last);
    spawn_armed = last;
    me.special = SA_NONE;
    me.killer = INVALID_ID;
    me.reason = WEAPON_UNKNOWN;
    me.pend = 0.0f;
    track_health();
}

/* SET_PLAYER_MODEL puts the fists back in his hand, and arming him in the same frame does
 * not take. So it is done a little later, if nothing else has armed him by then. */
void feat_combat_skin_set(void) { arm_due = 1; }

static void arm_tick(const cg_ped_t *p, unsigned now)
{
    if (arm_due == 1) {
        arm_due = 2;
        arm_at = now + ARM_DELAY_MS;
    } else if (arm_due == 2 && (int)(now - arm_at) >= 0) {
        arm_due = 0;
        if (!p->weapon && spawn_armed)
            cg_arm_weapon(cg_local_handle(), spawn_armed);
    }
}

static void respawn_here(void)
{
    if (spawn.valid) {
        game_set_skin(spawn.skin);
        game_teleport(spawn.pos[0], spawn.pos[1], spawn.pos[2]);
    }
    cg_camera_behind();
    prepare_body();
    feat_combat_skin_set();
}

static int spec_kind;

static void on_spawn_reply(const srpc_id_t *r)
{
    /* A spawn ends spectating, as in the PC client. Login screens often spectate, then
     * SpawnPlayer() without turning it off, and a spectator sends no foot sync. */
    if (r->id && me.spectating) {
        me.spectating = 0;
        spec_kind = 0;
        cg_set_control(1);
        cg_camera_behind();
        slog("combat: spawn ends spectating");
    }
    /* samp.c sends 52 itself. An answer after the first spawn means the server wants us
     * back at the spawn point right now: SpawnPlayer(), or the end of class selection. */
    if (r->id && me.spawned && !me.wasted)
        respawn_here();
    /* First spawn: reset now, while samp.c has yet to send 52. The server hands out
     * weapons the moment it sees 52, so a reset after that would wipe them. */
    else if (r->id && !me.spawned && !restarted) {
        me.spawned = 1;
        prepare_body();
    }
}

static void on_force_class(void) { me.reselect = 1; }

/* ---- damage and death ---------------------------------------------------- */

static int remote_by_ped(unsigned ped)
{
    unsigned i;
    cg_ped_t p;

    if (!ped)
        return INVALID_ID;
    for (i = 0; i < MAX_PLAYERS; i++)
        if (remotes[i].used && remotes[i].handle && cg_read_ped(remotes[i].handle, &p) && p.ped == ped)
            return i;
    return INVALID_ID;
}

static void send_damage(int taking, unsigned player, float amount, unsigned weapon)
{
    static unsigned char buf[32];
    bs_t b;
    bs_wr_init(&b, buf, sizeof buf);
    srpc_enc_give_take_damage(&b, taking, player, amount, weapon, PART_TORSO);
    if (!b.err)
        send_rpc(SRPC_C2S_GIVE_TAKE_DAMAGE, &b);
}

static void note_damage(unsigned issuer, unsigned weapon, float amount)
{
    /* Fire and falling tick every frame, so small hits are added up before going out. */
    if (me.pend > 0.0f && (issuer != me.pend_issuer || weapon != me.pend_weapon)) {
        send_damage(1, me.pend_issuer, me.pend, me.pend_weapon);
        me.pend = 0.0f;
    }
    me.pend += amount;
    me.pend_issuer = issuer;
    me.pend_weapon = weapon;
    /* A loss the game can't name doesn't clear who shot us a moment ago. */
    if (weapon != WEAPON_UNKNOWN) {
        me.killer = issuer;
        me.reason = weapon;
    }
}

static void flush_damage(unsigned now)
{
    if (me.pend > 0.0f && now - me.pend_ms >= DAMAGE_MS) {
        send_damage(1, me.pend_issuer, me.pend, me.pend_weapon);
        me.pend = 0.0f;
        me.pend_ms = now;
    }
}

/* Damage from a player, worked out here because their bullet exists only on their screen. */
static void take_hit(unsigned issuer, unsigned weapon, float amount)
{
    float a = me.armour;

    if (me.wasted || !(amount > 0.0f))
        return;
    if (a > 0.0f) {
        float soak = a < amount ? a : amount;
        a -= soak;
        amount -= soak;
        cg_set_armour(a);
    }
    note_damage(issuer, weapon, me.armour - a + amount);
    me.armour = a;
    me.hp -= amount;
    if (me.hp < 0.0f)
        me.hp = 0.0f;
    cg_set_health(me.hp);
}

static void track_damage(const cg_ped_t *p, unsigned now)
{
    float loss = me.hp + me.armour - p->health - p->armour;

    if (loss > 0.01f) {
        int id = remote_by_ped(p->last_source);
        note_damage(id, p->last_weapon < 0 ? WEAPON_UNKNOWN : (unsigned)p->last_weapon, loss);
        /* Read once: the next loss must not take this weapon's name by default. */
        cg_ped_clear_damage(cg_local_handle());
    }
    me.hp = p->health;
    me.armour = p->armour;
    flush_damage(now);
}

static void send_death(void)
{
    static unsigned char buf[8];
    bs_t b;
    bs_wr_init(&b, buf, sizeof buf);
    srpc_enc_death(&b, me.reason, me.killer);
    send_rpc(SRPC_C2S_DEATH, &b);
    slog("combat: dead, reason %d killer %d", me.reason, me.killer);
}

static void on_death(unsigned now)
{
    if (me.pend > 0.0f) {
        send_damage(1, me.pend_issuer, me.pend, me.pend_weapon);
        me.pend = 0.0f;
    }
    send_death();
    me.wasted = 1;
    me.wasted_ms = now;
    /* Armed here, because the override is used up by the next restart. */
    if (spawn.valid)
        cg_override_restart(spawn.pos, spawn.angle);
}

static void send_spawn(void)
{
    rak_rpc(SRPC_C2S_SPAWN, 0, 0, RAK_RELIABLE, 0);
    samp_spawn_sent();
}

static void back_from_dead(void)
{
    me.wasted = 0;
    if (me.reselect) {
        /* samp.c answers the class reply with 129, then 52, and on_spawn_reply puts us there.
         * No foot sync until then, or the server takes us as spawned and ignores the 129. */
        me.reselect = 0;
        prepare_body();
        samp_reselect_class();
        return;
    }
    if (spawn.valid)
        game_set_skin(spawn.skin);
    prepare_body();
    feat_combat_skin_set();
    send_spawn();
    slog("combat: respawned");
}

static void wasted_tick(const cg_ped_t *p, unsigned now)
{
    if (!p->dead && !cg_restart_pending() && p->health > 0.0f)
        back_from_dead();
    else if (now - me.wasted_ms > RESPAWN_MAX_MS) {
        /* Something held the game's restart up. Don't leave the player stuck and silent. */
        slog("combat: no restart after %d ms", RESPAWN_MAX_MS);
        me.wasted_ms = now;
    }
}

static void on_set_health(const srpc_float_t *v)
{
    float h = v->v;
    if (!(h >= 0.0f))
        h = 0.0f;
    if (h > 100000.0f)
        h = 100000.0f;
    /* Already counted, so that track_damage doesn't report the server's own change. */
    me.hp = h;
    cg_set_health(h);
    if (h <= 0.0f) {
        me.killer = INVALID_ID;
        me.reason = WEAPON_UNKNOWN;
    }
}

static void on_set_armour(const srpc_float_t *v)
{
    float a = v->v;
    if (!(a >= 0.0f))
        a = 0.0f;
    if (a > 100000.0f)
        a = 100000.0f;
    me.armour = a;
    cg_set_armour(a);
}

/* ---- our shots ----------------------------------------------------------- */

static void send_bullet(unsigned type, unsigned id, const float *origin, const float *hit,
                        const float *offset, unsigned weapon)
{
    static unsigned char buf[64];
    bs_t b;
    bs_wr_init(&b, buf, sizeof buf);
    srpc_enc_sync_bullet(&b, type, id, origin, hit, offset, weapon);
    if (!b.err)
        send_sync(&b);
}

static void track_shots(const cg_ped_t *p)
{
    static const float zero[3] = {0, 0, 0};
    unsigned shots = 0, i;
    float origin[3], aim[3];
    cg_camera_t c;

    if (p->weapon == me.shot_weapon && p->ammo < me.shot_ammo)
        shots = me.shot_ammo - p->ammo;
    me.shot_weapon = p->weapon;
    me.shot_ammo = p->ammo;
    /* A hit we know of with no ammo change is a melee blow or an explosion: no bullet. */
    if (!shots || !is_gun(p->weapon)) {
        me.nhits = 0;
        return;
    }
    if (shots > MAX_SHOTS_TICK)
        shots = MAX_SHOTS_TICK;
    origin[0] = p->pos[0];
    origin[1] = p->pos[1];
    origin[2] = p->pos[2] + 0.5f;
    if (!cg_read_camera(&c) || !unit_ok(c.front)) {
        me.nhits = 0;
        return;
    }
    for (i = 0; i < 3; i++)
        aim[i] = c.pos[i] + c.front[i] * MISS_RANGE;
    for (i = 0; i < shots; i++) {
        if (i < me.nhits)
            send_bullet(HIT_PLAYER, me.hits[i], origin, me.hit_pos[i], zero, p->weapon);
        else
            send_bullet(HIT_NONE, INVALID_ID, origin, aim, aim, p->weapon);
    }
    me.nhits = 0;
}

static void note_hit(unsigned id, const float *pos, float amount, unsigned weapon)
{
    int i;
    send_damage(0, id, amount, weapon);
    if (me.nhits >= MAX_SHOTS_TICK)
        return;
    me.hits[me.nhits] = id;
    for (i = 0; i < 3; i++)
        me.hit_pos[me.nhits][i] = pos[i];
    me.nhits++;
}

static int aim_camera(unsigned mode)
{
    return mode == 7 || mode == 8 || mode == 16 || mode == 46 || mode == 51 || mode == 53 || mode == 55 || mode == 65;
}

static float asin_f(float x)
{
    if (x > 1.0f) x = 1.0f;
    if (x < -1.0f) x = -1.0f;
    return x + x * x * x / 6.0f + 3.0f * x * x * x * x * x / 40.0f;
}

static void aim_tick(const cg_ped_t *p, unsigned now)
{
    static unsigned char buf[48];
    unsigned keys = 0, state, zoom;
    float fov;
    cg_camera_t c;
    cg_pad_t pad;
    int lr, ud;
    bs_t b;

    if (now - me.aim_ms < me.aim_rate || p->in_car || !cg_read_camera(&c) || !unit_ok(c.front))
        return;
    if (cg_pad(&pad, &lr, &ud))
        keys = feat_combat_keys(&pad, lr, ud);
    if (!(keys & (KEY_AIM | KEY_FIRE)) && !aim_camera(c.mode))
        return;
    me.aim_ms = now;
    state = p->wstate == 2 ? 3 : p->clip == 0 ? 0 : p->clip == 1 ? 1 : 2;
    fov = cg_camera_fov();
    zoom = fov > 35.0f && fov < 105.0f ? (unsigned)((fov - 35.0f) * 1.8f) : 63;
    bs_wr_init(&b, buf, sizeof buf);
    srpc_enc_sync_aim(&b, c.mode, c.front, c.pos, asin_f(c.front[2]), zoom, state, 85);
    if (!b.err)
        send_sync(&b);
}

static void weapons_tick(unsigned now)
{
    static unsigned char buf[64];
    srpc_out_weapon_t out[CG_SLOTS];
    cg_slot_t s[CG_SLOTS];
    unsigned n = 0;
    int i;
    bs_t b;

    if (now - me.weapons_ms < WEAPONS_MS)
        return;
    me.weapons_ms = now;
    cg_read_slots(s);
    for (i = 0; i < CG_SLOTS; i++) {
        if (s[i].weapon == me.slots[i].weapon && s[i].ammo == me.slots[i].ammo)
            continue;
        me.slots[i] = s[i];
        out[n].slot = i;
        out[n].weapon = feat_combat_weapon_ok(s[i].weapon) ? s[i].weapon : 0;
        out[n].ammo = s[i].ammo > 0xFFFF ? 0xFFFF : s[i].ammo;
        n++;
    }
    if (!n)
        return;
    bs_wr_init(&b, buf, sizeof buf);
    srpc_enc_sync_weapons(&b, INVALID_ID, INVALID_ID, out, n);
    if (!b.err)
        send_sync(&b);
}

static void stats_tick(unsigned now)
{
    static unsigned char buf[16];
    bs_t b;
    int area;

    if (now - me.stats_ms < STATS_MS)
        return;
    me.stats_ms = now;
    bs_wr_init(&b, buf, sizeof buf);
    srpc_enc_sync_stats(&b, cg_money(), (int)me.drunk);
    send_sync(&b);
    area = cg_get_area();
    if (area != me.area) {
        me.area = area;
        bs_wr_init(&b, buf, sizeof buf);
        srpc_enc_interior_change(&b, (unsigned)area);
        send_rpc(SRPC_C2S_INTERIOR_CHANGE, &b);
    }
}

/* ---- animations ---------------------------------------------------------- */

static int copy_name(char *out, const char *in, unsigned cap)
{
    unsigned i;
    for (i = 0; in[i]; i++) {
        if (i + 1 >= cap)
            return 0;
        out[i] = in[i];
    }
    out[i] = 0;
    return i != 0;
}

static void queue_anim(unsigned id, const char *lib, const char *name, float delta, int loop,
                       int lx, int ly, int freeze, int time)
{
    unsigned block = cg_anim_block(lib);
    anim_job_t *j = 0;
    int i;

    if (!block)
        return;
    for (i = 0; i < ANIM_QUEUE; i++) {
        if (jobs[i].used && jobs[i].id == id) { /* the newest one wins */
            j = &jobs[i];
            break;
        }
        if (!jobs[i].used && !j)
            j = &jobs[i];
    }
    if (!j)
        return;
    if (!copy_name(j->lib, lib, sizeof j->lib) || !copy_name(j->name, name, sizeof j->name)) {
        j->used = 0;
        return;
    }
    j->used = 1;
    j->id = id;
    j->delta = delta >= 0.0f && delta <= 100.0f ? delta : 4.0f;
    j->loop = loop;
    j->lock_x = lx;
    j->lock_y = ly;
    j->freeze = freeze;
    j->time = time;
    j->deadline = tick_now + ANIM_WAIT_MS;
    if (!cg_anim_ready(block))
        cg_anim_request(lib);
}

static int job_handle(unsigned id)
{
    return id == MAX_PLAYERS ? cg_local_handle() : handle_of(id);
}

static void anim_tick(unsigned now)
{
    int i;
    for (i = 0; i < ANIM_QUEUE; i++) {
        anim_job_t *j = &jobs[i];
        unsigned block;
        int h;
        if (!j->used)
            continue;
        block = cg_anim_block(j->lib);
        if (!block || (int)(now - j->deadline) > 0) {
            j->used = 0;
            continue;
        }
        if (!cg_anim_ready(block))
            continue;
        j->used = 0;
        h = job_handle(j->id);
        /* An unknown name inside a real file would be a null animation in the task. */
        if (!h || !cg_anim_exists(block, j->name))
            continue;
        cg_anim_play(h, j->name, j->lib, j->delta, j->loop, j->lock_x, j->lock_y, j->freeze, j->time);
        if (j->id < MAX_PLAYERS && remotes[j->id].used)
            remotes[j->id].anim_ms = now + (j->loop ? ANIM_LOOP_MS : j->time > 0 ? (unsigned)j->time : ANIM_ONCE_MS);
    }
}

static void on_apply_anim(const srpc_id_anim_t *a)
{
    unsigned who = a->id == my_id() ? MAX_PLAYERS : a->id;
    if (who > MAX_PLAYERS || (who < MAX_PLAYERS && !remote(who)))
        return;
    queue_anim(who, a->anim.lib, a->anim.name, a->anim.delta, a->anim.loop, a->anim.lock_x,
               a->anim.lock_y, a->anim.freeze, (int)a->anim.time);
}

static void on_clear_anim(const srpc_id_t *a)
{
    int i;
    unsigned who = a->id == my_id() ? MAX_PLAYERS : a->id;
    for (i = 0; i < ANIM_QUEUE; i++)
        if (jobs[i].used && jobs[i].id == who)
            jobs[i].used = 0;
    if (who == MAX_PLAYERS)
        cg_ped_clear_tasks(cg_local_handle());
    else if (remote(who)) {
        remotes[who].anim_ms = 0;
        if (handle_of(who))
            cg_ped_clear_tasks(handle_of(who));
    }
}

/* Actions with a game task. Dances are looping animations. The rest only ride along in
 * the sync, because they need attached objects this stream cannot place yet. */
static void show_special(unsigned who, int handle, unsigned action, unsigned was)
{
    static const char *const dances[] = {"DAN_LOOP_A", "DNCE_M_A", "DNCE_M_B", "DNCE_M_C"};

    if (was == SA_DUCK && action != SA_DUCK)
        cg_ped_duck(handle, 0);
    if ((was == SA_HANDSUP || (was >= SA_DANCE1 && was <= SA_DANCE4)) && action == SA_NONE)
        cg_ped_clear_tasks(handle);
    if (action == SA_DUCK)
        cg_ped_duck(handle, 1);
    else if (action == SA_HANDSUP)
        cg_ped_hands_up(handle, 60000);
    else if (action == SA_JETPACK) {
        cg_ped_jetpack(handle);
    } else if (action >= SA_DANCE1 && action <= SA_DANCE4)
        queue_anim(who, "DANCING", dances[action - SA_DANCE1], 4.0f, 1, 0, 0, 0, 0);
}

static void on_special_action(const srpc_id_t *a)
{
    if (a->id > 255)
        return;
    show_special(MAX_PLAYERS, cg_local_handle(), a->id, me.special);
    me.special = a->id;
}

static void on_fight_style(const srpc_id_val_t *v)
{
    int h = v->id == my_id() ? cg_local_handle() : handle_of(v->id);
    if (v->id < MAX_PLAYERS && remotes[v->id].used)
        remotes[v->id].style = v->v;
    if (h && v->v <= 26)
        cg_set_fight_style(h, v->v);
}

static void on_skill(const srpc_skill_t *s)
{
    int h;
    if (s->skill >= SRPC_SKILLS || s->level > 999)
        return;
    if (s->id == my_id()) {
        cg_set_float_stat(69 + s->skill, (float)s->level);
        return;
    }
    /* A remote ped has one skill for all weapons: 2 is what dual wielding needs. */
    h = handle_of(s->id);
    if (h)
        cg_set_weapon_skill(h, s->level >= 999 ? 2 : s->level >= 200 ? 1 : 0);
}

/* ---- remote players ------------------------------------------------------ */

static void on_stream_in(const srpc_player_stream_in_t *s)
{
    if (s->id < MAX_PLAYERS && s->id != my_id()) {
        remote_t *r = &remotes[s->id];
        memset(r, 0, sizeof *r);
        r->used = 1;
        r->health = 100;
        r->skin = s->skin;
        r->style = s->fight_style;
        r->pos[0] = s->pos[0];
        r->pos[1] = s->pos[1];
        r->pos[2] = s->pos[2];
    }
    if (next_stream_in)
        next_stream_in(s);
}

static void forget(unsigned id)
{
    int i;
    if (id >= MAX_PLAYERS)
        return;
    remotes[id].used = 0;
    for (i = 0; i < ANIM_QUEUE; i++)
        if (jobs[i].used && jobs[i].id == id)
            jobs[i].used = 0;
}

static void on_stream_out(const srpc_id_t *s)
{
    forget(s->id);
    if (next_stream_out)
        next_stream_out(s);
}

static void on_quit(const srpc_player_quit_t *q)
{
    forget(q->id);
    if (next_quit)
        next_quit(q);
}

static void on_player_death(const srpc_id_t *s)
{
    remote_t *r = remote(s->id);
    if (r) {
        int h = handle_of(s->id);
        r->dying = 1;
        r->anim_ms = 0;
        if (h) {
            /* The game would leave its gun on the ground as a pickup for us alone. */
            cg_remove_weapons(h);
            /* The ped was held at 1000 health: the die task is what kills it. */
            cg_ped_health(h, 0.0f);
            cg_ped_die(h);
        }
    }
    if (next_death)
        next_death(s);
}

static void on_set_skin(const srpc_id_val_t *v)
{
    remote_t *r = remote(v->id);
    unsigned skin;
    if (!r || v->id == my_id())
        return;
    skin = feat_combat_safe_skin(v->v);
    if (skin == r->skin)
        return;
    r->skin = skin;
    if (handle_of(v->id)) {
        /* No opcode swaps a ped's model: replace it, and the tick sets the new one up. */
        game_remote_del(v->id);
        game_remote_add(v->id, skin, r->pos, 0.0f);
        r->handle = 0;
    }
}

static void on_foot(const srpc_foot_sync_t *f)
{
    remote_t *r = remote(f->player);
    if (r) {
        const float *q = f->quat;
        r->synced = 1;
        r->old_keys = r->keys;
        r->keys = f->keys;
        r->health = f->health;
        r->armour = f->armour;
        if (f->lr || f->ud)
            r->anim_ms = 0;
        if (r->dying && f->health > 0)
            r->dying = 0; /* back without a restream: the gate replaces the body */
        r->weapon = feat_combat_weapon_ok(f->weapon) ? f->weapon : 0;
        r->special = f->special_action;
        if (pos_ok(f->pos)) {
            r->pos[0] = f->pos[0];
            r->pos[1] = f->pos[1];
            r->pos[2] = f->pos[2];
        }
        /* same forward vector as game_remote_sync */
        r->fwd[0] = 2.0f * (q[1] * q[2] + q[0] * q[3]);
        r->fwd[1] = 1.0f - 2.0f * (q[1] * q[1] + q[3] * q[3]);
    }
    if (next_foot)
        next_foot(f);
}

static void on_aim(const srpc_aim_sync_t *a)
{
    remote_t *r = remote(a->player);
    int i;
    if (!r || !pos_ok(a->cam_pos) || !unit_ok(a->cam_front))
        return;
    for (i = 0; i < 3; i++) {
        r->aim_front[i] = a->cam_front[i];
        r->aim_pos[i] = a->cam_pos[i];
    }
    r->aim_ms = tick_now;
}

static void on_bullet(const srpc_bullet_sync_t *s)
{
    remote_t *r = remote(s->player);
    int i;
    if (!r || !pos_ok(s->hit))
        return;
    for (i = 0; i < 3; i++)
        r->shot[i] = s->hit[i];
    r->shot_ms = tick_now;
    if (s->hit_type == HIT_PLAYER && s->hit_id == my_id() && is_gun(s->weapon))
        take_hit(s->player, s->weapon, feat_combat_weapon_damage(s->weapon));
}

static void aim_point(const remote_t *r, float *at)
{
    int i;
    if (tick_now - r->aim_ms < AIM_FRESH_MS) {
        for (i = 0; i < 3; i++)
            at[i] = r->aim_pos[i] + r->aim_front[i] * MISS_RANGE;
    } else {
        at[0] = r->pos[0] + r->fwd[0] * 10.0f;
        at[1] = r->pos[1] + r->fwd[1] * 10.0f;
        at[2] = r->pos[2];
    }
}

static int remote_busy(const remote_t *r)
{
    if ((int)(r->anim_ms - tick_now) > 0)
        return 1;
    return is_gun(r->weapon) && (r->keys & (KEY_AIM | KEY_FIRE));
}

/* A PC player's fist never reaches us as a bullet: their client only reports the hit it
 * sees on its own screen, and our copy of them does not swing. So judge it here. */
static void melee_check(remote_t *r, unsigned id, const cg_ped_t *me_ped)
{
    float dx = me_ped->pos[0] - r->pos[0], dy = me_ped->pos[1] - r->pos[1];
    float dz = fabs_f(me_ped->pos[2] - r->pos[2]);

    if (!(r->keys & KEY_FIRE) || (r->old_keys & KEY_FIRE) || !is_melee(r->weapon))
        return;
    if (tick_now - r->melee_ms < MELEE_MS || dz > 1.5f || dx * dx + dy * dy > MELEE_RANGE2)
        return;
    if (dx * r->fwd[0] + dy * r->fwd[1] <= 0.0f)
        return;
    r->melee_ms = tick_now;
    take_hit(id, r->weapon, feat_combat_weapon_damage(r->weapon));
}

static void remote_tasks(remote_t *r, unsigned id, int h, unsigned now)
{
    float at[3];

    /* Not armed until the model is resident: tried again next frame. */
    if (r->weapon != r->armed && (!is_weapon(r->weapon) || cg_load_weapon(r->weapon))) {
        if (is_weapon(r->weapon))
            cg_give_weapon(h, r->weapon, 30000);
        cg_arm_weapon(h, r->weapon);
        r->armed = r->weapon;
    }
    if (r->special != r->shown_special) {
        show_special(id, h, r->special, r->shown_special);
        r->shown_special = r->special;
    }
    if ((r->keys & KEY_JUMP) && !(r->old_keys & KEY_JUMP))
        cg_ped_jump(h);
    if ((int)(now - r->task_ms) < 0 || !is_gun(r->weapon))
        return;
    if (now - r->shot_ms < SHOT_FRESH_MS) {
        cg_ped_shoot(h, r->shot, 400);
        r->task_ms = now + TASK_MS;
    } else if (r->keys & KEY_FIRE) {
        aim_point(r, at);
        cg_ped_shoot(h, at, 400);
        r->task_ms = now + TASK_MS;
    } else if (r->keys & KEY_AIM) {
        aim_point(r, at);
        cg_ped_aim(h, at, 400);
        r->task_ms = now + TASK_MS;
    }
}

static void remote_tick(unsigned now, const cg_ped_t *me_ped)
{
    unsigned i;

    for (i = 0; i < MAX_PLAYERS; i++) {
        remote_t *r = &remotes[i];
        cg_ped_t p;
        int h;

        if (!r->used)
            continue;
        h = handle_of(i);
        if (!h || !cg_read_ped(h, &p)) {
            r->handle = 0;
            continue;
        }
        if (h != r->handle) {
            /* new, or recreated after the game took the old one */
            r->handle = h;
            r->armed = 0;
            r->shown_special = 0;
            cg_ped_prepare(h);
            cg_ped_health(h, REMOTE_HEALTH);
            if (r->style)
                cg_set_fight_style(h, r->style);
            p.health = REMOTE_HEALTH;
        }
        if (r->dying || p.dead)
            continue;
        if (p.health < REMOTE_HEALTH) {
            /* The source can't be cleared safely, it is a registered reference. The weapon
             * can, so a stale source with weapon -1, or a fall, is not read as our hit. */
            if (p.last_source == me_ped->ped && p.last_source && p.last_weapon >= 0) {
                unsigned w = p.last_weapon == WEAPON_EXPLOSION ? me_ped->weapon : (unsigned)p.last_weapon;
                /* What the game took off this ped is its single player damage, not the
                 * 0.3.7 figure that a PC client would report. */
                if (feat_combat_weapon_ok(w))
                    note_hit(i, p.pos, feat_combat_weapon_damage(w), w);
            }
            cg_ped_health(h, REMOTE_HEALTH);
            cg_ped_clear_damage(h);
        }
        if (r->synced) {
            remote_tasks(r, i, h, now);
            melee_check(r, i, me_ped);
            r->old_keys = r->keys;
        }
    }
}

int feat_combat_remote_gate(unsigned id, int handle)
{
    cg_ped_t p;
    remote_t *r = id < MAX_PLAYERS ? &remotes[id] : 0;

    if (!cg_read_ped(handle, &p))
        return COMBAT_REMOTE_LOST;
    if (r && r->used && r->dying)
        return COMBAT_REMOTE_SKIP;
    if (p.dead)
        return COMBAT_REMOTE_LOST;
    if (r && r->used && remote_busy(r))
        return COMBAT_REMOTE_HOLD;
    return COMBAT_REMOTE_NORMAL;
}

int feat_combat_remote_move(unsigned id, int fallback)
{
    remote_t *r = remote(id);
    if (!r || !r->synced)
        return fallback;
    if (r->keys & KEY_SPRINT)
        return MOVE_SPRINT;
    return r->keys & KEY_WALK ? MOVE_WALK : MOVE_RUN;
}

int feat_combat_remote_health(unsigned id, unsigned *health, unsigned *armour)
{
    remote_t *r = remote(id);
    if (!r)
        return 0;
    *health = r->health;
    *armour = r->armour;
    return 1;
}

unsigned feat_combat_safe_skin(unsigned skin)
{
    return skin < 300 && cg_model_ok(skin) ? skin : 0;
}

/* ---- player state RPCs --------------------------------------------------- */

static void on_set_pos_find_z(const srpc_vec3_t *v)
{
    float z;
    if (!pos_ok(v->v))
        return;
    if (feat_vehicle_eject)
        feat_vehicle_eject(v->v);
    /* The collision has to be in before the ground can be found. */
    game_teleport(v->v[0], v->v[1], v->v[2]);
    z = cg_ground_z(v->v[0], v->v[1], v->v[2] + 100.0f);
    if (z > -100.0f && z < WORLD_LIMIT)
        game_teleport(v->v[0], v->v[1], z + 1.0f);
}

static void on_facing(const srpc_float_t *v)
{
    if (v->v > -WORLD_LIMIT && v->v < WORLD_LIMIT)
        cg_set_heading(v->v);
}

static void on_velocity(const srpc_vec3_t *v)
{
    int i;
    for (i = 0; i < 3; i++)
        if (!(v->v[i] > -100.0f && v->v[i] < 100.0f))
            return;
    cg_set_velocity(v->v);
}

static void on_controllable(const srpc_int_t *v) { cg_set_control(v->v); }

static void on_give_money(const srpc_int_t *v) { cg_give_money(v->v); }
static void on_reset_money(void) { cg_give_money(-cg_money()); }

static void on_interior(const srpc_id_t *v)
{
    if (v->id > 255)
        return;
    cg_set_area(v->id);
    me.area = (int)v->id; /* the server said so, no need to report it back */
}

static void on_wanted(const srpc_id_t *v) { me.wanted = v->id > 6 ? 6 : v->id; }

static void on_drunk(const srpc_int_t *v)
{
    int d = v->v < 0 ? 0 : v->v > 50000 ? 50000 : v->v;
    me.drunk = (unsigned)d;
    cg_set_drunk(d > 255 ? 255 : d);
}

static void apply_time(unsigned hour, unsigned minute) { if (hour < 24 && minute < 60) cg_set_time(hour, minute); }
static void on_time(const srpc_time_t *t) { apply_time(t->hour, t->minute); }
static void on_world_time(const srpc_id_t *t) { apply_time(t->id, 0); }

/* Ids past the last real one index off the game's weather tables. */
static void apply_weather(unsigned id) { if (id <= 22) cg_set_weather(id); }
static void on_weather(const srpc_id_t *w) { apply_weather(w->id); }


static void on_init(const srpc_init_game_t *g)
{
    me.aim_rate = g->weapon_rate < 40 ? 40 : g->weapon_rate > 1000 ? 1000 : g->weapon_rate;
    apply_time(g->hour, 0);
    apply_weather(g->weather);
    if (next_init)
        next_init(g);
}

/* ---- camera -------------------------------------------------------------- */

static float cam_target[3];
static int cam_target_set;

#define SPEC_PLAYER 1
#define SPEC_CAR    2
#define SPEC_BACK   5.0f
#define SPEC_UP     2.0f
#define SPEC_BELOW  30.0f
static int spec_kind;
static unsigned spec_id;

static void cancel_moves(void) { cam_pos.active = cam_look.active = 0; }

static void on_cam_pos(const srpc_vec3_t *v)
{
    if (pos_ok(v->v)) {
        cam_pos.active = 0;
        cg_camera_at(v->v);
    }
}

static unsigned cut_of(unsigned cut) { return cut == 1 ? 1 : 2; }

static void on_cam_look(const srpc_look_at_t *v)
{
    if (pos_ok(v->pos)) {
        cam_look.active = 0;
        memcpy(cam_target, v->pos, sizeof cam_target);
        cam_target_set = 1;
        cg_camera_look(v->pos, cut_of(v->cut));
    }
}

static void on_cam_behind(void)
{
    cancel_moves();
    cg_camera_behind();
}

/* The game has no way to glide a fixed camera to a place and stop there, so this moves
 * it every frame. */
static void on_cam_move(const srpc_camera_move_t *m)
{
    cam_move_t *c = m->pos_set ? &cam_pos : &cam_look;
    int i;
    if (!pos_ok(m->from) || !pos_ok(m->to))
        return;
    for (i = 0; i < 3; i++) {
        c->from[i] = m->from[i];
        c->to[i] = m->to[i];
    }
    c->time = m->time ? m->time : 1;
    c->cut = cut_of(m->cut);
    c->start = tick_now;
    c->active = 1;
}

static void step_move(cam_move_t *c, unsigned now, int look)
{
    float t, p[3];
    int i;
    if (!c->active)
        return;
    t = (float)(now - c->start) / (float)c->time;
    if (t >= 1.0f) {
        t = 1.0f;
        c->active = 0;
    }
    for (i = 0; i < 3; i++)
        p[i] = c->from[i] + (c->to[i] - c->from[i]) * t;
    if (look) {
        memcpy(cam_target, p, sizeof cam_target);
        cam_target_set = 1;
        cg_camera_look(p, 2);
    } else {
        cg_camera_at(p);
        /* The new position only takes when the camera is pointed again. */
        if (cam_target_set)
            cg_camera_look(cam_target, 2);
    }
}

/* ---- spectating ---------------------------------------------------------- */

static void on_spectating(const srpc_int_t *v)
{
    int on = v->v != 0;
    if (on == me.spectating)
        return;
    me.spectating = on;
    spec_kind = 0;
    cg_set_control(!on);
    if (on)
        return;
    /* Leaving spectate is a spawn, and the server expects 52 for it. */
    respawn_here();
    send_spawn();
}

/* POINT_CAMERA_AT_CHAR does nothing on a non-player here, and the game keeps the player
 * visible whatever SET_CHAR_VISIBLE says. So the camera is a fixed one that is moved every
 * frame, and our ped waits out of sight under the target, which also keeps the map loaded
 * around it. */
static void on_spectate_player(const srpc_spectate_t *s)
{
    spec_kind = SPEC_PLAYER;
    spec_id = s->id;
}

static void on_spectate_car(const srpc_spectate_t *s)
{
    spec_kind = SPEC_CAR;
    spec_id = s->id;
}

static void spectate_view(void)
{
    float t[3], f[2], cam[3], at[3];
    cg_ped_t p;
    int h = 0;

    if (spec_kind == SPEC_PLAYER) {
        h = handle_of(spec_id);
        if (!h || !cg_read_ped(h, &p))
            return;
        memcpy(t, p.pos, sizeof t);
        memcpy(f, p.fwd, sizeof f);
    } else if (spec_kind == SPEC_CAR) {
        h = feat_vehicle_handle ? feat_vehicle_handle(spec_id) : 0;
        if (!h || !cg_read_car(h, t, f))
            return;
    } else {
        return;
    }
    cam[0] = t[0] - f[0] * SPEC_BACK;
    cam[1] = t[1] - f[1] * SPEC_BACK;
    cam[2] = t[2] + SPEC_UP;
    at[0] = t[0];
    at[1] = t[1];
    at[2] = t[2] + 0.7f;
    cg_camera_at(cam);
    cg_camera_look(at, 2);
    at[2] = t[2] - SPEC_BELOW;
    cg_move_local(at);
}

static void spectate_tick(const cg_ped_t *p, unsigned now)
{
    static unsigned char buf[32];
    cg_camera_t c;
    cg_pad_t pad;
    int lr = 0, ud = 0;
    bs_t b;

    if (now - me.spec_ms < 100)
        return;
    me.spec_ms = now;
    cg_pad(&pad, &lr, &ud);
    bs_wr_init(&b, buf, sizeof buf);
    srpc_enc_sync_spectator(&b, (unsigned short)lr, (unsigned short)ud, feat_combat_keys(&pad, lr, ud),
                            cg_read_camera(&c) ? c.pos : p->pos);
    if (!b.err)
        send_sync(&b);
}

/* ---- session ------------------------------------------------------------- */

static void reset_session(void)
{
    unsigned aim = me.aim_rate;
    int i;
    memset(&me, 0, sizeof me);
    memset(jobs, 0, sizeof jobs);
    for (i = 0; i < MAX_PLAYERS; i++)
        remotes[i].used = 0;
    cancel_moves();
    me.aim_rate = aim ? aim : 100;
    me.killer = INVALID_ID;
    me.reason = WEAPON_UNKNOWN;
}

/* A new server starts from nothing: it only ever adds to what the player holds. */
void feat_combat_connecting(void)
{
    cg_give_money(-cg_money());
    me.wanted = 0;
    give_n = 0;
}

static void on_restart(void)
{
    /* Everything is rebuilt after the InitGame that follows. */
    reset_session();
    spawn.valid = 0;
    restarted = 1;
    if (next_restart)
        next_restart();
}

void feat_combat_tick(unsigned now)
{
    int state = samp_state();
    cg_ped_t p;

    tick_now = now;
    if (state != SAMP_SPAWNED) {
        /* Class selection in mid-session keeps everything: the peds are still there. */
        if (me.seen && state != SAMP_CLASS && state != SAMP_SPAWNING)
            reset_session();
        return;
    }
    if (!cg_read_local(&p))
        return;
    if (!me.seen) {
        /* main.c does the first move and skin next frame: start after that. */
        me.seen = 1;
        return;
    }
    if (!me.spawned) {
        me.spawned = 1;
        if (restarted)
            respawn_here();
        else
            prepare_body();
        restarted = 0;
        cg_read_local(&p);
    }
    give_queue_tick();
    arm_tick(&p, now);
    anim_tick(now);
    step_move(&cam_pos, now, 0);
    step_move(&cam_look, now, 1);
    if (me.spectating) {
        spectate_view();
        spectate_tick(&p, now);
        remote_tick(now, &p);
        return;
    }
    if (me.wasted) {
        wasted_tick(&p, now);
        return;
    }
    track_damage(&p, now);
    if (p.dead) {
        on_death(now);
        return;
    }
    remote_tick(now, &p);
    track_shots(&p);
    aim_tick(&p, now);
    weapons_tick(now);
    stats_tick(now);
}

void feat_combat_init(samp_rpc_handlers_t *h)
{
    reset_session();

    next_stream_in = h->player_stream_in;   h->player_stream_in = on_stream_in;
    next_stream_out = h->player_stream_out; h->player_stream_out = on_stream_out;
    next_quit = h->player_quit;             h->player_quit = on_quit;
    next_init = h->init_game;               h->init_game = on_init;
    next_restart = h->game_mode_restart;    h->game_mode_restart = on_restart;
    next_foot = h->sync_foot;               h->sync_foot = on_foot;
    next_death = h->player_death;           h->player_death = on_player_death;

    h->give_weapon = on_give_weapon;
    h->reset_weapons = on_reset_weapons;
    h->set_armed_weapon = on_set_armed;
    h->set_ammo = on_set_ammo;
    h->set_health = on_set_health;
    h->set_armour = on_set_armour;
    h->request_class_reply = on_class;
    h->set_spawn_info = on_spawn_info;
    h->request_spawn_reply = on_spawn_reply;
    h->force_class_selection = on_force_class;
    h->sync_aim = on_aim;
    h->sync_bullet = on_bullet;
    h->apply_animation = on_apply_anim;
    h->clear_animations = on_clear_anim;
    h->set_special_action = on_special_action;
    h->set_fighting_style = on_fight_style;
    h->set_skill_level = on_skill;
    h->set_skin = on_set_skin;
    h->set_pos_find_z = on_set_pos_find_z;
    h->set_facing_angle = on_facing;
    h->set_velocity = on_velocity;
    h->toggle_controllable = on_controllable;
    h->give_money = on_give_money;
    h->reset_money = on_reset_money;
    h->set_interior = on_interior;
    h->set_wanted_level = on_wanted;
    h->set_drunk_level = on_drunk;
    h->set_time = on_time;
    h->set_world_time = on_world_time;
    h->set_weather = on_weather;
    h->set_camera_pos = on_cam_pos;
    h->set_camera_look_at = on_cam_look;
    h->set_camera_behind = on_cam_behind;
    h->interpolate_camera = on_cam_move;
    h->toggle_spectating = on_spectating;
    h->spectate_player = on_spectate_player;
    h->spectate_vehicle = on_spectate_car;
}

void feat_combat_draw(void)
{
    if (me.spawned && me.wanted)
        cg_draw_wanted(me.wanted);
}
