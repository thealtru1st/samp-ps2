/* Game access for feat_combat, SLES-52927 v1.00. See feat_combat_game.h. Nothing here
 * dereferences a game pointer that did not come from the game itself, and a ped handle
 * is always looked up through the pool first: script opcodes do not check for a stale
 * handle, and a null ped there means a store into low memory. */
#include "feat_combat_game.h"
#include "scm.h"
#include "util.h"

#ifdef CG_HOST
/* Host tests: the same code over a fake image of game memory. */
extern unsigned char cg_mem[];
extern int cg_host_lr, cg_host_ud, cg_host_anim_found, cg_host_not_resident;
#define ADDR(a) (cg_mem + (a))
#else
#include "ui_game.h"
#define ADDR(a) ((unsigned char *)(a))
#endif
#define R8(a)   (*(volatile unsigned char *)ADDR(a))
#define R16(a)  (*(volatile short *)ADDR(a))
#define R32(a)  (*(volatile unsigned *)ADDR(a))
#define RF(a)   (*(volatile float *)ADDR(a))

#define GP              0x0066B170u
#define PED_POOL        (GP + 0x828)
#define CAR_POOL        (GP + 0x82C)
#define CAR_STRIDE      0xA80u
#define GAME_STATE      (GP + 0xAB4)  /* 0 playing, 1 wasted, 2 busted */
#define ANIM_BLOCK_N    (GP + 0x0B8)
#define ANIM_BLOCKS     0x00677E30u   /* stride 0x20: name[16], loaded byte at +0x10 */
#define ANIM_STRIDE     0x20u
#define PLAYERS         0x00709650u
#define PLAYER_MONEY    0xE4
#define CAMERA          0x006FE5B0u
#define CAM_ACTIVE      0x59
#define CAM_STRIDE      0x238u
#define CAM_MODE        0x180
#define CAM_FRONT       0x304
#define CAM_SOURCE      0x310
#define CPAD            0x00700BC0u
#define CPAD_DISABLED   0x6E
#define PAD_WALK_LR     0x002491D0u   /* short (CPad*) */
#define PAD_WALK_UD     0x00249300u
#define ANIM_GET        0x00107A90u   /* void *(const char *name, void *block) */
#define MODEL_INFO      0x007A8800u   /* CBaseModelInfo *[], read by IS_MODEL_AVAILABLE */
#define MODEL_ON_DISC   0x002C1480u   /* int (int id), called by IS_MODEL_IN_CDIMAGE */

#define POOL_STRIDE     0x810u
#define PED_MATRIX      0x014
#define PED_POS         0x004         /* placement, when an entity has no matrix */
#define PED_SPEED       0x044
#define PED_FLAGS_CAR   0x4A5         /* bit 0 */
#define PED_FLAGS_DUCK  0x4A7         /* bit 2 */
#define PED_STATE       0x568         /* 54 die, 55 dead, 56 die by stealth */
#define PED_HEALTH      0x580
#define PED_ARMOUR      0x588
#define PED_VEHICLE     0x5CC
#define PED_WEAPONS     0x5E0         /* 13 x 0x1C: type, state, clip, total */
#define WEAPON_STRIDE   0x1C
#define PED_SLOT        0x758
#define PED_LAST_WEAPON 0x7A0
#define PED_LAST_SOURCE 0x7A4

#define OP_ADD_SCORE        0x0109
#define OP_OVERRIDE_RESTART 0x016E
#define OP_SET_TIME         0x00C0
#define OP_FIXED_CAMERA     0x015F
#define OP_POINT_AT_POINT   0x0160
#define OP_POINT_AT_CAR     0x0158
#define OP_POINT_AT_CHAR    0x0159
#define OP_SET_CHAR_COORDS  0x00A1
#define OP_SET_CHAR_HEADING 0x0173
#define OP_SET_CHAR_AMMO    0x017B
#define OP_PLAYER_CONTROL   0x01B4
#define OP_GIVE_WEAPON      0x01B2
#define OP_FORCE_WEATHER    0x01B6
#define OP_ARM_WEAPON       0x01B9
#define OP_SET_CHAR_HEALTH  0x0223
#define OP_REQUEST_MODEL    0x0247
#define OP_ONLY_BY_PLAYER   0x02A9
#define OP_CHAR_PROOFS      0x02AB
#define OP_GROUND_Z         0x02CE
#define OP_CHAR_ACCURACY    0x02E2
#define OP_RESTORE_CAMERA   0x02EB
#define OP_CHAR_VISIBLE     0x0337
#define OP_CAMERA_BEHIND    0x0373
#define OP_LOAD_ALL_MODELS  0x038B
#define OP_CRITICAL_HITS    0x0446
#define OP_CLEAR_LAST_WEAPON 0x0467
#define OP_REMOVE_WEAPONS   0x048F
#define OP_AREA_VISIBLE     0x04BB
#define OP_DROWNS           0x04D8
#define OP_TOGGLE_DUCK      0x04EB
#define OP_REQUEST_ANIM     0x04ED
#define OP_DRUNK            0x052C
#define OP_TASK_JUMP        0x05BC
#define OP_TASK_DIE         0x05BE
#define OP_TASK_HANDS_UP    0x05C4
#define OP_TASK_PLAY_ANIM   0x0605
#define OP_LOAD_DECISION    0x060A
#define OP_SET_DECISION     0x060B
#define OP_SET_FLOAT_STAT   0x062A
#define OP_TASK_AIM_COORD   0x0667
#define OP_TASK_SHOOT_COORD 0x0668
#define OP_GET_AREA         0x077E
#define OP_WEAPON_MODEL     0x0781
#define OP_CLEAR_TASKS_NOW  0x0792
#define OP_TASK_JETPACK     0x07A7
#define OP_MELEE_STYLE      0x07FE
#define OP_CAMERA_FOV       0x0801
#define OP_WEAPON_SKILL     0x081A
#define OP_CHAR_VELOCITY    0x083C
#define OP_CHAR_AREA        0x0860
#define OP_DEATH_PENALTIES  0x08DD

#define L_PLAYER 0   /* set by game_boot */
#define L_CHAR   1   /* set by game_boot */
#define L_A      2   /* scratch, set right before each use */
#define L_B      3

#define MODEL_JETPACK 370
#define DM_EMPTY 0   /* m_empty.ped, first entry of the table at 0x625670 */

static int decision_maker = -1;

static unsigned ped_of(int handle)
{
    unsigned pool = R32(PED_POOL), slot = (unsigned)handle >> 8;
    if (!pool || handle <= 0 || slot >= R32(pool + 8))
        return 0;
    if (R8(R32(pool + 4) + slot) != ((unsigned)handle & 0xFF))
        return 0;
    return R32(pool) + slot * POOL_STRIDE;
}

static void read_ped(unsigned ped, cg_ped_t *o)
{
    unsigned slot = R8(ped + PED_SLOT), m = R32(ped + PED_MATRIX), base = m ? m + 0x30 : ped + PED_POS;
    unsigned state = R32(ped + PED_STATE), w;
    int i;

    o->ped = ped;
    for (i = 0; i < 3; i++) {
        o->pos[i] = RF(base + i * 4);
        o->vel[i] = RF(ped + PED_SPEED + i * 4);
    }
    o->fwd[0] = m ? RF(m + 0x10) : 0.0f;
    o->fwd[1] = m ? RF(m + 0x14) : 1.0f;
    o->health = RF(ped + PED_HEALTH);
    o->armour = RF(ped + PED_ARMOUR);
    o->dead = state == 54 || state == 55 || state == 56;
    o->in_car = (R8(ped + PED_FLAGS_CAR) & 1) && R32(ped + PED_VEHICLE);
    o->ducking = (R8(ped + PED_FLAGS_DUCK) >> 2) & 1;
    if (slot >= CG_SLOTS)
        slot = 0;
    w = ped + PED_WEAPONS + slot * WEAPON_STRIDE;
    o->weapon = R32(w);
    o->wstate = R32(w + 4);
    o->clip = R32(w + 8);
    o->ammo = R32(w + 12);
    o->last_weapon = (signed char)R8(ped + PED_LAST_WEAPON);
    o->last_source = R32(ped + PED_LAST_SOURCE);
}

int cg_local_handle(void) { return scm_get_local(L_CHAR); }

int cg_read_local(cg_ped_t *o)
{
    unsigned ped = R32(PLAYERS);
    if (!ped)
        return 0;
    read_ped(ped, o);
    return 1;
}

int cg_read_ped(int handle, cg_ped_t *o)
{
    unsigned ped = ped_of(handle);
    if (!ped)
        return 0;
    read_ped(ped, o);
    return 1;
}

int cg_handle_valid(int handle) { return ped_of(handle) != 0; }

int cg_read_car(int handle, float *pos, float *fwd)
{
    unsigned pool = R32(CAR_POOL), slot = (unsigned)handle >> 8, v, m;
    int i;
    if (!pool || handle <= 0 || slot >= R32(pool + 8) ||
        R8(R32(pool + 4) + slot) != ((unsigned)handle & 0xFF))
        return 0;
    v = R32(pool) + slot * CAR_STRIDE;
    m = R32(v + PED_MATRIX);
    if (!m)
        return 0;
    for (i = 0; i < 3; i++)
        pos[i] = RF(m + 0x30 + i * 4);
    fwd[0] = RF(m + 0x10);
    fwd[1] = RF(m + 0x14);
    return 1;
}

void cg_read_slots(cg_slot_t *s)
{
    unsigned ped = R32(PLAYERS), w;
    int i;
    for (i = 0; i < CG_SLOTS; i++) {
        s[i].weapon = s[i].ammo = 0;
        if (!ped)
            continue;
        w = ped + PED_WEAPONS + i * WEAPON_STRIDE;
        s[i].weapon = R32(w);
        s[i].ammo = R32(w + 12);
    }
}

int cg_pad(cg_pad_t *o, int *lr, int *ud)
{
    memcpy(o, (const void *)ADDR(CPAD), sizeof *o);
    if (R16(CPAD + CPAD_DISABLED)) {
        memset(o, 0, sizeof *o);
        *lr = *ud = 0;
        return 0;
    }
#ifdef CG_HOST
    *lr = cg_host_lr;
    *ud = cg_host_ud;
#else
    {
        extern short samp_raw_l1;
        o->l1 = samp_raw_l1;
    }
    /* The game's own walk getters: they pick stick or D-pad for the current pad setup. */
    *lr = ((short (*)(unsigned))PAD_WALK_LR)(CPAD);
    *ud = ((short (*)(unsigned))PAD_WALK_UD)(CPAD);
#endif
    return 1;
}

int cg_read_camera(cg_camera_t *o)
{
    unsigned active = R8(CAMERA + CAM_ACTIVE), c;
    int i;
    if (active > 2)
        return 0;
    c = CAMERA + active * CAM_STRIDE;
    o->mode = (unsigned short)R16(c + CAM_MODE);
    for (i = 0; i < 3; i++) {
        o->front[i] = RF(c + CAM_FRONT + i * 4);
        o->pos[i] = RF(c + CAM_SOURCE + i * 4);
    }
    return 1;
}

int cg_model_ok(unsigned id)
{
    if (id >= 20000 || !R32(MODEL_INFO + id * 4))
        return 0;
#ifdef CG_HOST
    return 1;
#else
    return ((int (*)(int))MODEL_ON_DISC)(id) != 0;
#endif
}

/* Stream info, stride 12: low three bits of +6 are 1 once the model is resident. */
int cg_model_loaded(unsigned id)
{
#ifdef CG_HOST
    return !cg_host_not_resident;
#else
    return id < 20000 && (R8(0x007C3CF6u + id * 12) & 7) == 1;
#endif
}

int cg_money(void) { return (int)R32(PLAYERS + PLAYER_MONEY); }
int cg_restart_pending(void) { return R8(GAME_STATE) != 0; }

/* ---- local ped ---- */

static void op_char(scm_t *s, unsigned op, int local) { scm_op(s, op); scm_local(s, local); }

void cg_set_health(float v)
{
    unsigned ped = R32(PLAYERS);
    scm_t s;
    if (!ped)
        return;
    if (v > 0.0f) {
        RF(ped + PED_HEALTH) = v;
        return;
    }
    /* A plain store leaves the ped standing. The opcode with 0 raises the die event, and
     * the store goes first so the die task's own drop to 0 can't read as fresh damage. */
    RF(ped + PED_HEALTH) = 0.0f;
    scm_begin(&s);
    op_char(&s, OP_SET_CHAR_HEALTH, L_CHAR); scm_i8(&s, 0);
    scm_run(&s);
}

void cg_set_armour(float v)
{
    unsigned ped = R32(PLAYERS);
    if (ped)
        RF(ped + PED_ARMOUR) = v;
}

void cg_set_control(int on)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_PLAYER_CONTROL); scm_local(&s, L_PLAYER); scm_i8(&s, on != 0);
    scm_run(&s);
}

void cg_move_local(const float *p)
{
    scm_t s;
    scm_begin(&s);
    op_char(&s, OP_SET_CHAR_COORDS, L_CHAR); scm_f(&s, p[0]); scm_f(&s, p[1]); scm_f(&s, p[2]);
    scm_run(&s);
}

void cg_set_heading(float a)
{
    scm_t s;
    scm_begin(&s);
    op_char(&s, OP_SET_CHAR_HEADING, L_CHAR); scm_f(&s, a);
    scm_run(&s);
}

void cg_set_velocity(const float *v)
{
    scm_t s;
    scm_begin(&s);
    /* The opcode takes metres per second and divides by 50. SA-MP's unit is the raw one. */
    op_char(&s, OP_CHAR_VELOCITY, L_CHAR);
    scm_f(&s, v[0] * 50.0f); scm_f(&s, v[1] * 50.0f); scm_f(&s, v[2] * 50.0f);
    scm_run(&s);
}

void cg_set_visible(int on)
{
    scm_t s;
    scm_begin(&s);
    op_char(&s, OP_CHAR_VISIBLE, L_CHAR); scm_i8(&s, on != 0);
    scm_run(&s);
}

void cg_set_bullet_proof(int on)
{
    scm_t s;
    scm_begin(&s);
    op_char(&s, OP_CHAR_PROOFS, L_CHAR);
    scm_i8(&s, on != 0); scm_i8(&s, 0); scm_i8(&s, 0); scm_i8(&s, 0); scm_i8(&s, 0);
    scm_run(&s);
}

void cg_override_restart(const float *p, float angle)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_OVERRIDE_RESTART);
    scm_f(&s, p[0]); scm_f(&s, p[1]); scm_f(&s, p[2]); scm_f(&s, angle);
    scm_run(&s);
}

void cg_no_death_penalty(void)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_DEATH_PENALTIES); scm_i8(&s, 0);
    scm_run(&s);
}

void cg_give_money(int amount)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_ADD_SCORE); scm_local(&s, L_PLAYER); scm_i32(&s, amount);
    scm_run(&s);
}

void cg_set_area(unsigned area)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_AREA_VISIBLE); scm_i32(&s, area);
    op_char(&s, OP_CHAR_AREA, L_CHAR); scm_i32(&s, area);
    scm_run(&s);
}

int cg_get_area(void)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_GET_AREA); scm_local(&s, L_A);
    scm_run(&s);
    return scm_get_local(L_A);
}

void cg_set_drunk(int level)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_DRUNK); scm_local(&s, L_PLAYER); scm_i32(&s, level);
    scm_run(&s);
}

void cg_set_float_stat(unsigned stat, float v)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_SET_FLOAT_STAT); scm_i32(&s, stat); scm_f(&s, v);
    scm_run(&s);
}

float cg_ground_z(float x, float y, float z)
{
    union { int i; float f; } r;
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_GROUND_Z); scm_f(&s, x); scm_f(&s, y); scm_f(&s, z); scm_local(&s, L_A);
    scm_run(&s);
    r.i = scm_get_local(L_A);
    return r.f;
}

/* ---- weapons ---- */

/* Never LOAD_ALL_MODELS here: when objects have filled streaming memory the game can't
 * make room, and that opcode waits for good. The request loads over the next frames. */
int cg_load_weapon(unsigned weapon)
{
    scm_t s;
    int model;
    scm_begin(&s);
    scm_op(&s, OP_WEAPON_MODEL); scm_i32(&s, weapon); scm_local(&s, L_A);
    scm_run(&s);
    model = scm_get_local(L_A);
    if (model <= 0)
        return 1; /* fists and the like have none */
    if (cg_model_loaded(model))
        return 1;
    scm_begin(&s);
    scm_op(&s, OP_REQUEST_MODEL); scm_i32(&s, model);
    scm_run(&s);
    return 0;
}

static void char_i2(unsigned op, int handle, int a, int b)
{
    scm_t s;
    scm_set_local(L_B, handle);
    scm_begin(&s);
    op_char(&s, op, L_B); scm_i32(&s, a); scm_i32(&s, b);
    scm_run(&s);
}

static void char_i1(unsigned op, int handle, int a)
{
    scm_t s;
    scm_set_local(L_B, handle);
    scm_begin(&s);
    op_char(&s, op, L_B); scm_i32(&s, a);
    scm_run(&s);
}

static void char_i0(unsigned op, int handle)
{
    scm_t s;
    scm_set_local(L_B, handle);
    scm_begin(&s);
    op_char(&s, op, L_B);
    scm_run(&s);
}

void cg_give_weapon(int h, unsigned w, unsigned ammo) { if (ped_of(h)) char_i2(OP_GIVE_WEAPON, h, w, ammo); }
void cg_set_ammo(int h, unsigned w, unsigned ammo)    { if (ped_of(h)) char_i2(OP_SET_CHAR_AMMO, h, w, ammo); }
void cg_arm_weapon(int h, unsigned w)                 { if (ped_of(h)) char_i1(OP_ARM_WEAPON, h, w); }
void cg_remove_weapons(int h)                         { if (ped_of(h)) char_i0(OP_REMOVE_WEAPONS, h); }
void cg_set_fight_style(int h, unsigned style)        { if (ped_of(h)) char_i2(OP_MELEE_STYLE, h, style, 6); }
void cg_set_weapon_skill(int h, unsigned skill)       { if (ped_of(h)) char_i1(OP_WEAPON_SKILL, h, skill); }

/* ---- remote peds ---- */

void cg_ped_prepare(int h)
{
    scm_t s;
    if (!ped_of(h))
        return;
    if (decision_maker < 0) {
        scm_begin(&s);
        scm_op(&s, OP_LOAD_DECISION); scm_i8(&s, DM_EMPTY); scm_local(&s, L_A);
        scm_run(&s);
        decision_maker = scm_get_local(L_A);
    }
    scm_set_local(L_A, decision_maker);
    scm_set_local(L_B, h);
    scm_begin(&s);
    /* A SA-MP player is not a civilian: it must not flee, panic or react on its own. */
    op_char(&s, OP_SET_DECISION, L_B); scm_local(&s, L_A);
    op_char(&s, OP_CRITICAL_HITS, L_B); scm_i8(&s, 0);
    op_char(&s, OP_DROWNS, L_B); scm_i8(&s, 0);
    op_char(&s, OP_ONLY_BY_PLAYER, L_B); scm_i8(&s, 1);
    op_char(&s, OP_CHAR_ACCURACY, L_B); scm_i8(&s, 100);
    scm_run(&s);
}

void cg_ped_health(int h, float v)
{
    unsigned ped = ped_of(h);
    if (ped)
        RF(ped + PED_HEALTH) = v;
}

void cg_ped_clear_damage(int h) { if (ped_of(h)) char_i0(OP_CLEAR_LAST_WEAPON, h); }
void cg_ped_die(int h)          { if (ped_of(h)) char_i0(OP_TASK_DIE, h); }
void cg_ped_jump(int h)         { if (ped_of(h)) char_i1(OP_TASK_JUMP, h, 1); }
void cg_ped_duck(int h, int on) { if (ped_of(h)) char_i1(OP_TOGGLE_DUCK, h, on != 0); }
void cg_ped_hands_up(int h, int ms) { if (ped_of(h)) char_i1(OP_TASK_HANDS_UP, h, ms); }
void cg_ped_clear_tasks(int h)  { if (ped_of(h)) char_i0(OP_CLEAR_TASKS_NOW, h); }

void cg_ped_jetpack(int h)
{
    scm_t s;
    if (!ped_of(h) || !cg_model_ok(MODEL_JETPACK))
        return;
    if (!cg_model_loaded(MODEL_JETPACK)) { /* see cg_load_weapon: no blocking load */
        scm_begin(&s);
        scm_op(&s, OP_REQUEST_MODEL); scm_i32(&s, MODEL_JETPACK);
        scm_run(&s);
        return;
    }
    char_i0(OP_TASK_JETPACK, h);
}
static void ped_at(unsigned op, int h, const float *at, int ms)
{
    scm_t s;
    if (!ped_of(h))
        return;
    scm_set_local(L_B, h);
    scm_begin(&s);
    op_char(&s, op, L_B); scm_f(&s, at[0]); scm_f(&s, at[1]); scm_f(&s, at[2]); scm_i32(&s, ms);
    scm_run(&s);
}

void cg_ped_aim(int h, const float *at, int ms)   { ped_at(OP_TASK_AIM_COORD, h, at, ms); }
void cg_ped_shoot(int h, const float *at, int ms) { ped_at(OP_TASK_SHOOT_COORD, h, at, ms); }

/* ---- animations ---- */

static int name_is(const volatile unsigned char *a, const char *b)
{
    unsigned i;
    for (i = 0; i < 16; i++) {
        unsigned x = a[i], y = (unsigned char)b[i];
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y)
            return 0;
        if (!x)
            return 1;
    }
    return 0;
}

unsigned cg_anim_block(const char *lib)
{
    unsigned n = R32(ANIM_BLOCK_N), i;
    if (n > 256)
        n = 256;
    for (i = 0; i < n; i++)
        if (name_is(ADDR(ANIM_BLOCKS + i * ANIM_STRIDE), lib))
            return ANIM_BLOCKS + i * ANIM_STRIDE;
    return 0;
}

void cg_anim_request(const char *lib)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_REQUEST_ANIM); scm_str(&s, lib);
    scm_run(&s);
}

int cg_anim_ready(unsigned block) { return R8(block + 0x10) == 1; }

int cg_anim_exists(unsigned block, const char *name)
{
#ifdef CG_HOST
    return cg_host_anim_found;
#else
    return ((void *(*)(const char *, unsigned))ANIM_GET)(name, block) != 0;
#endif
}

void cg_anim_play(int h, const char *name, const char *lib, float delta,
                  int loop, int lock_x, int lock_y, int freeze, int ms)
{
    scm_t s;
    if (!ped_of(h))
        return;
    scm_set_local(L_B, h);
    scm_begin(&s);
    op_char(&s, OP_TASK_PLAY_ANIM, L_B);
    scm_str(&s, name); scm_str(&s, lib); scm_f(&s, delta);
    scm_i8(&s, loop != 0); scm_i8(&s, lock_x != 0); scm_i8(&s, lock_y != 0); scm_i8(&s, freeze != 0);
    scm_i32(&s, ms);
    scm_run(&s);
}

/* ---- camera and world ---- */

void cg_camera_at(const float *p)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_FIXED_CAMERA);
    scm_f(&s, p[0]); scm_f(&s, p[1]); scm_f(&s, p[2]);
    scm_f(&s, 0.0f); scm_f(&s, 0.0f); scm_f(&s, 0.0f);
    scm_run(&s);
}

void cg_camera_look(const float *at, unsigned cut)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_POINT_AT_POINT);
    scm_f(&s, at[0]); scm_f(&s, at[1]); scm_f(&s, at[2]); scm_i8(&s, cut);
    scm_run(&s);
}

void cg_camera_behind(void)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_RESTORE_CAMERA);
    scm_op(&s, OP_CAMERA_BEHIND);
    scm_run(&s);
}

void cg_camera_follow_ped(int h, unsigned cut)
{
    scm_t s;
    if (!ped_of(h))
        return;
    scm_set_local(L_B, h);
    scm_begin(&s);
    op_char(&s, OP_POINT_AT_CHAR, L_B); scm_i8(&s, 4); scm_i8(&s, cut);
    scm_run(&s);
}

void cg_camera_follow_car(int h, unsigned cut)
{
    scm_t s;
    scm_set_local(L_B, h);
    scm_begin(&s);
    op_char(&s, OP_POINT_AT_CAR, L_B); scm_i8(&s, 18); scm_i8(&s, cut);
    scm_run(&s);
}

float cg_camera_fov(void)
{
    union { int i; float f; } r;
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_CAMERA_FOV); scm_local(&s, L_A);
    scm_run(&s);
    r.i = scm_get_local(L_A);
    return r.f;
}

void cg_set_time(unsigned hour, unsigned minute)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_SET_TIME); scm_i8(&s, hour); scm_i8(&s, minute);
    scm_run(&s);
}

void cg_set_weather(unsigned id)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_FORCE_WEATHER); scm_i8(&s, id);
    scm_run(&s);
}

void cg_draw_wanted(unsigned level)
{
#ifndef CG_HOST
    /* ']' is the star glyph in the HUD font. */
    static const ui_rgba gold = {0xC8, 0x90, 0x20, 0xFF}, black = {0, 0, 0, 0xFF};
    char stars[8];
    unsigned i;

    if (!level)
        return;
    for (i = 0; i < level && i < 6; i++)
        stars[i] = ']';
    stars[i] = 0;
    CFont_SetFontStyle(UI_FONT_PRICEDOWN);
    CFont_SetScale(0.7f, 1.4f);
    CFont_SetAlphaFade(255.0f);
    CFont_SetColor(&gold);
    CFont_SetDropColor(&black);
    CFont_SetDropShadow(0);
    CFont_SetEdge(1);
    CFont_SetProportional(1);
    CFont_SetJustify(0);
    CFont_SetOrientation(UI_ALIGN_RIGHT);
    CFont_SetRightWrap(0.0f);
    ui_PrintString(620.0f, 92.0f, stars);
    CFont_SetEdge(0);
#else
    (void)level;
#endif
}
