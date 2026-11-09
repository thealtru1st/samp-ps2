/* PS2 side of feat_vehicle_game.h, SLES-52927 v1.00. Offsets and how each was found are
 * in wip/vehicle/re-vehicle.md. Where a game function is called directly its arguments
 * are all ints, which the payload's N32 and the game's EABI pass the same way. */
#include "feat_vehicle_game.h"
#include "feat_vehicle_math.h"
#include "scm.h"
#include "ui_game.h"
#include "util.h"

#define GP            0x0066B170u
#define PED_POOL      (GP + 0x828)
#define CAR_POOL      (GP + 0x82C)
#define PED_STRIDE    0x810u
#define CAR_STRIDE    0xA80u
#define PLAYERS       0x00709650u
#define MODEL_INFO    0x007A8800u /* CModelInfo pointers by model id */
#define MODEL_STATE   0x007C3CF6u /* CStreamingInfo load state, stride 12, 1 = loaded */
#define MODEL_MODS    134         /* CVehicleModelInfo: 18 shorts, the car's upgrades */
#define MOD_SLOTS     18

/* CEntity, CPhysical: same as PC */
#define E_FLAGS       0x1C        /* bit 2 static */
#define E_MATRIX      0x14
#define E_AREA        0x2F
#define E_STATUS      0x36        /* top 5 bits */
#define P_MOVE        0x44
#define P_TURN        0x50
/* CVehicle */
#define V_COLOUR      0x43C
#define V_FLAGS0      0x430       /* bit 4 engine on */
#define V_FLAGS3      0x433       /* bit 6 drowned */
#define V_FLAGS5      0x435       /* bit 7 siren */
#define V_ALARM       0x464
#define V_DRIVER      0x468
#define V_PASSENGERS  0x46C
#define V_SEATS       0x490
#define V_HEALTH      0x4C8
#define V_TOWING      0x4CC       /* the cab pulling this car */
#define V_TOWED       0x4D0
#define V_CLASS       0x5CC       /* 0 car, 5 boat, 6 train, 9 bike, 10 bmx, 11 trailer */
#define V_SUBCLASS    0x5D0       /* 3 heli, 4 plane, 6 train */
#define V_DAMAGE      0x5E0       /* CDamageManager, automobile family */
#define V_NODES       0x688       /* RwFrame pointers by node */
#define V_BIKE_WHEELS 0x698
#define DM_WHEELS     5
#define DM_DOORS      9
#define DM_LIGHTS     16
#define DM_PANELS     20
#define WRECKED       5
#define CLASS_BOAT    5
#define CLASS_TRAIN   6
#define CLASS_BIKE    9
#define CLASS_BMX     10
/* CPed */
#define PED_MATRIX    0x14
#define PED_FLAGS     0x4A5       /* bit 0 in a vehicle */
#define PED_INTEL     0x4B4
#define PED_CAR       0x5CC
#define PED_HEALTH    0x580
#define PED_ARMOUR    0x588
/* CVehicle virtuals, byte offsets into the vtable: same order as PC */
#define VT_SET_STATIC 24
#define VT_TELEPORT   64

#define FN_ADD_MOVING 0x0024C360u /* (CPhysical*) */
#define FN_FIND_TASK  0x0045FBC0u /* (CTaskManager*, type) */
#define FN_MOD_LOADED 0x002C1E10u /* (model) */
#define FN_SET_PANEL  0x00162520u /* (CDamageManager*, panel, status) */
#define FN_SET_DOOR   0x001625A0u /* (CDamageManager*, door, status) */
#define FN_NODE_STATE 0x001714E0u /* (CVehicle*, RwFrame*, state): 0 gone, 1 ok, 2 damaged */

#define TASK_ENTER_PASSENGER 700
#define TASK_ENTER_DRIVER    701
#define TASK_LEAVE_CAR       704

#define OP_CREATE_CAR     0x00A5
#define OP_DELETE_CAR     0x00A6
#define OP_SET_HEADING    0x0175
#define OP_ADD_BLIP       0x0186
#define OP_REMOVE_BLIP    0x0164
#define OP_LOCK_DOORS     0x020A
#define OP_COLOUR         0x0229
#define OP_REQUEST_MODEL  0x0247
#define OP_RELEASE_MODEL  0x0249
#define OP_WARP_OUT       0x0362
#define OP_WARP_DRIVER    0x036A
#define OP_CAMERA_BEHIND  0x0373
#define OP_RESTORE_CAMERA 0x02EB
#define OP_SIREN          0x0397
#define OP_WARP_PASSENGER 0x0430
#define OP_ENTER_PASSENGER 0x05CA
#define OP_ENTER_DRIVER   0x05CB
#define OP_LEAVE_CAR      0x05CD
#define OP_LEAVE_ANY      0x0633
#define OP_PLATE          0x0674
#define OP_FORCE_LIGHTS   0x067F
#define OP_POP_DOOR       0x0689
#define OP_FIX_DOOR       0x068A
#define OP_POP_PANEL      0x0697
#define OP_FIX_PANEL      0x0698
#define OP_ADD_MOD        0x06E7
#define OP_REMOVE_MOD     0x06E8
#define OP_REQUEST_MOD    0x06E9
#define OP_RELEASE_MOD    0x06EB
#define OP_PAINTJOB       0x06ED
#define OP_DETACH         0x07AC
#define OP_AREA           0x0840
#define OP_ATTACH         0x0893
#define OP_OPEN_DOOR_BIT  0x08A6
#define OP_ENGINE         0x0918
#define OP_COLLISION      0x099A
#define OP_FIX_CAR        0x0A30

#define L_OUT 24 /* script local for results; game.c uses 0-3 */

#define U8(a)  (*(volatile unsigned char *)(a))
#define U16(a) (*(volatile unsigned short *)(a))
#define U32(a) (*(volatile unsigned *)(a))
#define F32(a) (*(volatile float *)(a))

/* Node of each panel; the rear wings have none, so the pop and fix opcodes must never
 * see them: both index the node array with the -1. */
static const signed char panel_node[7] = {15, 14, -1, -1, 18, 12, 13};
static const signed char door_node[6] = {16, 17, 10, 8, 11, 9};

static unsigned pool_at(unsigned var, int h, unsigned stride)
{
    unsigned pool = U32(var), i;

    if (!pool || h <= 0 || (h & 0x80))
        return 0;
    i = (unsigned)h >> 8;
    if (i >= U32(pool + 8) || U8(U32(pool + 4) + i) != (h & 0xFF))
        return 0;
    return U32(pool) + i * stride;
}

static int pool_handle(unsigned var, unsigned p, unsigned stride)
{
    unsigned pool = U32(var), i;

    if (!pool || p < U32(pool))
        return 0;
    i = (p - U32(pool)) / stride;
    if (i >= U32(pool + 8))
        return 0;
    return (int)(i << 8 | U8(U32(pool + 4) + i));
}

static unsigned car_at(int h) { return pool_at(CAR_POOL, h, CAR_STRIDE); }
static unsigned ped_at(int h) { return pool_at(PED_POOL, h, PED_STRIDE); }
static int car_handle(unsigned p) { return p ? pool_handle(CAR_POOL, p, CAR_STRIDE) : 0; }
static int ped_handle(unsigned p) { return p ? pool_handle(PED_POOL, p, PED_STRIDE) : 0; }

/* Automobile family: only these have the damage manager and door code. */
static int has_body(unsigned v)
{
    unsigned c = U32(v + V_CLASS);
    return c != CLASS_BOAT && c != CLASS_TRAIN && c != CLASS_BIKE && c != CLASS_BMX;
}

static void op1(unsigned op, int a)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, op); scm_i32(&s, a);
    scm_run(&s);
}

static void op2(unsigned op, int a, int b)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, op); scm_i32(&s, a); scm_i32(&s, b);
    scm_run(&s);
}

static void op3(unsigned op, int a, int b, int c)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, op); scm_i32(&s, a); scm_i32(&s, b); scm_i32(&s, c);
    scm_run(&s);
}

/* ---- streaming ------------------------------------------------------------ */

int vg_model_ok(unsigned model) { return model < 20000 && U32(MODEL_INFO + model * 4) != 0; }
int vg_model_loaded(unsigned model) { return (U8(MODEL_STATE + model * 12) & 7) == 1; }
void vg_model_request(unsigned model) { op1(OP_REQUEST_MODEL, model); }
/* Streaming memory past its limit: a new model would only push it further. */
int game_stream_over(void);   /* game.c: model budget or heap nearly used up */
int vg_stream_over(void) { return game_stream_over(); }
void vg_model_release(unsigned model) { op1(OP_RELEASE_MODEL, model); }

int vg_mod_loaded(unsigned comp) { return ((int (*)(int))FN_MOD_LOADED)(comp); }
void vg_mod_request(unsigned comp) { op1(OP_REQUEST_MOD, comp); }
void vg_mod_release(unsigned comp) { op1(OP_RELEASE_MOD, comp); }

/* A component the car has no frame for can crash AddVehicleUpgrade, and a server need
 * not be honest, so each one is checked against the game's own carmods data first. */
int vg_mod_ok(int car, unsigned comp)
{
    unsigned v = car_at(car), info;
    int i;

    if (!v || comp < 1000 || comp > 1193 || !vg_model_ok(comp))
        return 0;
    if (U32(v + V_CLASS) != 0 || U32(v + V_SUBCLASS) == 3 || U32(v + V_SUBCLASS) == 4)
        return 0;
    /* Wheels, nitro, stereo and hydraulics fit every car and carmods doesn't list them. */
    if ((comp >= 1008 && comp <= 1010) || comp == 1025 || (comp >= 1073 && comp <= 1087) ||
        (comp >= 1096 && comp <= 1098))
        return 1;
    info = U32(MODEL_INFO + (unsigned)*(volatile short *)(v + 0x22) * 4);
    if (!info)
        return 0;
    for (i = 0; i < MOD_SLOTS; i++)
        if ((unsigned)*(volatile short *)(info + MODEL_MODS + i * 2) == comp)
            return 1;
    return 0;
}

int vg_car_slots_free(void)
{
    unsigned pool = U32(CAR_POOL), i, n = 0;

    if (!pool)
        return 0;
    for (i = 0; i < U32(pool + 8); i++)
        if (U8(U32(pool + 4) + i) & 0x80)
            n++;
    return n;
}

/* ---- lifetime ------------------------------------------------------------- */

int vg_create(unsigned model, const float *pos, float angle, const char *plate)
{
    scm_t s;
    float p[3];
    int car;

    if (plate && plate[0]) { /* applies to the next car of this model */
        scm_begin(&s);
        scm_op(&s, OP_PLATE); scm_i32(&s, model); scm_str(&s, plate);
        scm_run(&s);
    }
    scm_begin(&s);
    scm_op(&s, OP_CREATE_CAR); scm_i32(&s, model);
    scm_f(&s, pos[0]); scm_f(&s, pos[1]); scm_f(&s, pos[2]);
    scm_local(&s, L_OUT);
    scm_op(&s, OP_SET_HEADING); scm_local(&s, L_OUT); scm_f(&s, angle);
    scm_run(&s);
    car = scm_get_local(L_OUT);
    if (!car_at(car))
        return 0;
    /* CREATE_CAR adds the height of the model and, outside a mission thread, drops the
     * car onto the nearest road. The server's position is exact, so put it back. */
    p[0] = pos[0];
    p[1] = pos[1];
    p[2] = pos[2] + 0.25f;
    vg_teleport(car, p);
    return car;
}

void vg_delete(int car)
{
    if (car_at(car))
        op1(OP_DELETE_CAR, car);
}

int vg_alive(int car) { return car_at(car) != 0; }

int vg_wrecked(int car)
{
    unsigned v = car_at(car);
    return !v || (U8(v + E_STATUS) >> 3) == WRECKED || (U8(v + V_FLAGS3) & 0x40);
}

/* ---- motion --------------------------------------------------------------- */

void vg_read(int car, vg_state_t *s)
{
    unsigned v = car_at(car), m;
    int i;

    memset(s, 0, sizeof *s);
    s->quat[0] = 1.0f;
    if (!v)
        return;
    m = U32(v + E_MATRIX);
    if (m) {
        float f[16];
        for (i = 0; i < 16; i++)
            f[i] = F32(m + i * 4);
        vm_mat_to_quat(f, s->quat);
        for (i = 0; i < 3; i++) {
            s->right[i] = f[i];
            s->fwd[i] = f[4 + i];
            s->pos[i] = f[12 + i];
        }
    }
    for (i = 0; i < 3; i++) {
        s->vel[i] = F32(v + P_MOVE + i * 4);
        s->turn[i] = F32(v + P_TURN + i * 4);
    }
}

/* Asleep cars are skipped by physics, so anything we move has to be woken up. */
static void wake(unsigned v)
{
    if (U8(v + E_FLAGS) & 4) {
        void (*set_static)(unsigned, int) = (void (*)(unsigned, int))U32(U32(v) + VT_SET_STATIC);
        set_static(v, 0);
        ((void (*)(unsigned))FN_ADD_MOVING)(v);
    }
}

void vg_teleport(int car, const float *pos)
{
    unsigned v = car_at(car);
    float p[3];

    if (!v)
        return;
    p[0] = pos[0];
    p[1] = pos[1];
    p[2] = pos[2];
    wake(v);
    ((void (*)(unsigned, float *, int))U32(U32(v) + VT_TELEPORT))(v, p, 0);
}

void vg_set_heading(int car, float degrees)
{
    scm_t s;
    if (!car_at(car))
        return;
    scm_begin(&s);
    scm_op(&s, OP_SET_HEADING); scm_i32(&s, car); scm_f(&s, degrees);
    scm_run(&s);
}

void vg_set_rotation(int car, const float *quat)
{
    unsigned v = car_at(car), m;
    float f[16];
    int i;

    if (!v || !(m = U32(v + E_MATRIX)))
        return;
    vm_quat_to_mat(quat, f);
    for (i = 0; i < 3; i++) {
        F32(m + i * 4) = f[i];
        F32(m + 16 + i * 4) = f[4 + i];
        F32(m + 32 + i * 4) = f[8 + i];
    }
    wake(v);
}

void vg_set_velocity(int car, const float *vel)
{
    unsigned v = car_at(car);
    int i;

    if (!v)
        return;
    for (i = 0; i < 3; i++)
        F32(v + P_MOVE + i * 4) = vel[i];
    wake(v);
}

void vg_set_turn(int car, const float *t)
{
    unsigned v = car_at(car);
    int i;

    if (!v)
        return;
    for (i = 0; i < 3; i++)
        F32(v + P_TURN + i * 4) = t[i];
    wake(v);
}

/* ---- state ---------------------------------------------------------------- */

float vg_health(int car)
{
    unsigned v = car_at(car);
    return v ? F32(v + V_HEALTH) : 0.0f;
}

void vg_set_health(int car, float h)
{
    unsigned v = car_at(car);
    if (v)
        F32(v + V_HEALTH) = h;
}

void vg_set_colours(int car, unsigned c1, unsigned c2)
{
    if (car_at(car))
        op3(OP_COLOUR, car, c1, c2);
}

void vg_set_interior(int car, unsigned interior)
{
    if (car_at(car))
        op2(OP_AREA, car, interior);
}

void vg_set_paintjob(int car, int index)
{
    if (car_at(car))
        op2(OP_PAINTJOB, car, index);
}

void vg_add_mod(int car, unsigned comp)
{
    scm_t s;
    if (!car_at(car))
        return;
    scm_begin(&s);
    scm_op(&s, OP_ADD_MOD); scm_i32(&s, car); scm_i32(&s, comp); scm_local(&s, L_OUT);
    scm_run(&s);
}

void vg_remove_mod(int car, unsigned comp)
{
    if (car_at(car))
        op2(OP_REMOVE_MOD, car, comp);
}

void vg_set_engine(int car, int on)
{
    if (car_at(car))
        op2(OP_ENGINE, car, on);
}

int vg_engine(int car)
{
    unsigned v = car_at(car);
    return v && (U8(v + V_FLAGS0) & 0x10);
}

void vg_set_lights(int car, int on)
{
    if (car_at(car))
        op2(OP_FORCE_LIGHTS, car, on ? 2 : 1);
}

void vg_set_locked(int car, int locked)
{
    if (car_at(car))
        op2(OP_LOCK_DOORS, car, locked ? 2 : 1);
}

void vg_set_siren(int car, int on)
{
    if (car_at(car))
        op2(OP_SIREN, car, on);
}

int vg_siren(int car)
{
    unsigned v = car_at(car);
    return v && (U8(v + V_FLAGS5) & 0x80);
}

void vg_set_alarm(int car, int on)
{
    unsigned v = car_at(car);
    if (v)
        U16(v + V_ALARM) = on ? 20000 : 0; /* milliseconds left to sound */
}

void vg_set_door(int car, unsigned door, int open)
{
    unsigned v = car_at(car);
    scm_t s;

    if (!v || !has_body(v) || door > 5)
        return;
    scm_begin(&s);
    scm_op(&s, OP_OPEN_DOOR_BIT); scm_i32(&s, car); scm_i32(&s, door); scm_f(&s, open ? 1.0f : 0.0f);
    scm_run(&s);
}

void vg_set_collision(int car, int on)
{
    if (car_at(car))
        op2(OP_COLLISION, car, on);
}

int vg_add_blip(int car)
{
    scm_t s;
    if (!car_at(car))
        return 0;
    scm_begin(&s);
    scm_op(&s, OP_ADD_BLIP); scm_i32(&s, car); scm_local(&s, L_OUT);
    scm_run(&s);
    return scm_get_local(L_OUT);
}

void vg_remove_blip(int blip)
{
    if (blip)
        op1(OP_REMOVE_BLIP, blip);
}

/* ---- damage --------------------------------------------------------------- */

void vg_get_damage(int car, unsigned *panels, unsigned *doors, unsigned *lights, unsigned *tyres)
{
    unsigned v = car_at(car), c, d, i;

    *panels = *doors = *lights = *tyres = 0;
    if (!v)
        return;
    c = U32(v + V_CLASS);
    if (c == CLASS_BIKE || c == CLASS_BMX) {
        for (i = 0; i < 2; i++)
            if (U8(v + V_BIKE_WHEELS + i))
                *tyres |= 1u << (1 - i);
        return;
    }
    if (!has_body(v))
        return;
    d = v + V_DAMAGE;
    *panels = U32(d + DM_PANELS);
    *lights = U32(d + DM_LIGHTS) & 0xFF;
    for (i = 0; i < 4; i++) {
        *doors |= (unsigned)U8(d + DM_DOORS + i) << (i * 8);
        if (U8(d + DM_WHEELS + i))
            *tyres |= 1u << (3 - i);
    }
}

static void set_node(unsigned v, int node, int state)
{
    ((void (*)(unsigned, unsigned, int))FN_NODE_STATE)(v, U32(v + V_NODES + node * 4), state);
}

static void set_panel(int car, unsigned v, unsigned p, unsigned want)
{
    unsigned cur = (U32(v + V_DAMAGE + DM_PANELS) >> (p * 4)) & 15;
    int node = panel_node[p];

    if (want == cur)
        return;
    if (node >= 0 && want == 0) {
        op2(OP_FIX_PANEL, car, p);
    } else if (node >= 0 && want >= 3) {
        op3(OP_POP_PANEL, car, p, 0);
    } else {
        ((void (*)(unsigned, unsigned, unsigned))FN_SET_PANEL)(v + V_DAMAGE, p, want);
        if (node >= 0)
            set_node(v, node, 2);
    }
}

static void set_door(int car, unsigned v, unsigned door, unsigned want)
{
    unsigned cur = U8(v + V_DAMAGE + DM_DOORS + door);

    if (want == cur)
        return;
    if (want == 0) {
        op2(OP_FIX_DOOR, car, door);
    } else if (want >= 4) {
        op3(OP_POP_DOOR, car, door, 0);
    } else {
        ((void (*)(unsigned, unsigned, unsigned))FN_SET_DOOR)(v + V_DAMAGE, door, want);
        if (want >= 2)
            set_node(v, door_node[door], 2);
    }
}

void vg_set_damage(int car, unsigned panels, unsigned doors, unsigned lights, unsigned tyres)
{
    unsigned v = car_at(car), c, i;

    if (!v)
        return;
    c = U32(v + V_CLASS);
    if (c == CLASS_BIKE || c == CLASS_BMX) {
        for (i = 0; i < 2; i++)
            U8(v + V_BIKE_WHEELS + i) = (tyres >> (1 - i)) & 1;
        return;
    }
    if (!has_body(v))
        return;
    for (i = 0; i < 7; i++)
        set_panel(car, v, i, (panels >> (i * 4)) & 15);
    for (i = 0; i < 4; i++)
        set_door(car, v, i, (doors >> (i * 8)) & 7);
    U32(v + V_DAMAGE + DM_LIGHTS) = (U32(v + V_DAMAGE + DM_LIGHTS) & ~0xFFu) | (lights & 0xFF);
    for (i = 0; i < 4; i++)
        U8(v + V_DAMAGE + DM_WHEELS + i) = (tyres >> (3 - i)) & 1;
}

void vg_repair(int car)
{
    unsigned v = car_at(car);
    float h;

    if (!v)
        return;
    h = F32(v + V_HEALTH);
    op1(OP_FIX_CAR, car); /* also sets 1000 */
    F32(v + V_HEALTH) = h;
}

/* ---- trailers ------------------------------------------------------------- */

int vg_towed(int cab)
{
    unsigned v = car_at(cab);
    return v ? car_handle(U32(v + V_TOWED)) : 0;
}

int vg_being_towed(int car)
{
    unsigned v = car_at(car);
    return v && U32(v + V_TOWING);
}

void vg_attach(int trailer, int cab)
{
    unsigned t = car_at(trailer);

    if (t && car_at(cab)) {
        /* An asleep trailer stays put, and the game breaks the link at the first pull. */
        wake(t);
        op2(OP_ATTACH, trailer, cab);
    }
}

void vg_detach(int trailer, int cab)
{
    if (car_at(trailer) && car_at(cab))
        op2(OP_DETACH, trailer, cab);
}

/* ---- seats and peds ------------------------------------------------------- */

int vg_driver(int car)
{
    unsigned v = car_at(car);
    return v ? ped_handle(U32(v + V_DRIVER)) : 0;
}

int vg_passenger(int car, unsigned index)
{
    unsigned v = car_at(car);
    return v && index < 8 ? ped_handle(U32(v + V_PASSENGERS + index * 4)) : 0;
}

unsigned vg_seats(int car)
{
    unsigned v = car_at(car);
    return v ? U8(v + V_SEATS) : 0;
}

int vg_ped_alive(int ped) { return ped_at(ped) != 0; }

int vg_ped_car(int ped)
{
    unsigned p = ped_at(ped);
    return p && (U8(p + PED_FLAGS) & 1) ? car_handle(U32(p + PED_CAR)) : 0;
}

void vg_warp_driver(int ped, int car)
{
    if (ped_at(ped) && car_at(car))
        op2(OP_WARP_DRIVER, ped, car);
}

void vg_warp_passenger(int ped, int car, unsigned index)
{
    if (ped_at(ped) && car_at(car))
        op3(OP_WARP_PASSENGER, ped, car, index);
}

void vg_warp_out(int ped, const float *pos)
{
    scm_t s;
    if (!ped_at(ped))
        return;
    scm_begin(&s);
    scm_op(&s, OP_WARP_OUT); scm_i32(&s, ped);
    scm_f(&s, pos[0]); scm_f(&s, pos[1]); scm_f(&s, pos[2]);
    scm_run(&s);
}

void vg_task_enter(int ped, int car, int passenger)
{
    scm_t s;
    if (!ped_at(ped) || !car_at(car))
        return;
    scm_begin(&s);
    /* Time -2 means never give up and teleport in, which sync would do anyway. */
    if (passenger) {
        scm_op(&s, OP_ENTER_PASSENGER); scm_i32(&s, ped); scm_i32(&s, car); scm_i32(&s, -2); scm_i32(&s, -1);
    } else {
        scm_op(&s, OP_ENTER_DRIVER); scm_i32(&s, ped); scm_i32(&s, car); scm_i32(&s, -2);
    }
    scm_run(&s);
}

void vg_task_leave(int ped, int car)
{
    if (ped_at(ped) && car_at(car))
        op2(OP_LEAVE_CAR, ped, car);
}

void vg_task_leave_any(int ped)
{
    if (ped_at(ped))
        op1(OP_LEAVE_ANY, ped);
}

/* ---- local player --------------------------------------------------------- */

static unsigned local_ped(void) { return U32(PLAYERS); }

int vg_local_ped(void) { return ped_handle(local_ped()); }

void vg_local_pos(float *pos)
{
    unsigned p = local_ped(), m = p ? U32(p + PED_MATRIX) : 0;
    int i;

    for (i = 0; i < 3; i++)
        pos[i] = m ? F32(m + 0x30 + i * 4) : 0.0f;
}

static unsigned clamp_byte(float v) { return v < 0.0f ? 0 : v > 255.0f ? 255 : (unsigned)v; }

void vg_local_vitals(unsigned *health, unsigned *armour)
{
    unsigned p = local_ped();
    *health = p ? clamp_byte(F32(p + PED_HEALTH)) : 0;
    *armour = p ? clamp_byte(F32(p + PED_ARMOUR)) : 0;
}

static unsigned find_task(int type)
{
    unsigned p = local_ped();
    if (!p || !U32(p + PED_INTEL))
        return 0;
    return ((unsigned (*)(unsigned, int))FN_FIND_TASK)(U32(p + PED_INTEL) + 4, type);
}

int vg_local_entering(int *passenger)
{
    unsigned t = find_task(TASK_ENTER_DRIVER);

    *passenger = 0;
    if (!t) {
        t = find_task(TASK_ENTER_PASSENGER);
        *passenger = 1;
    }
    return t ? car_handle(U32(t + 12)) : 0;
}

int vg_local_leaving(void) { return find_task(TASK_LEAVE_CAR) != 0; }

void vg_read_pad(vg_pad_t *p)
{
    volatile short *n = (volatile short *)(CPAD_ARRAY + CPAD_NEWSTATE);

    p->lx = n[0];
    p->ly = n[1];
    p->l1 = n[4] != 0;
    p->l2 = n[5] != 0;
    p->r1 = n[6] != 0;
    p->r2 = n[7] != 0;
    p->dpad_right = n[11] != 0;
    p->square = n[14] != 0;
    p->triangle = n[15] != 0;
    p->cross = n[16] != 0;
    p->circle = n[17] != 0;
    p->l3 = n[18] != 0;
    p->r3 = n[19] != 0;
}

void vg_camera_behind(void)
{
    scm_t s;
    scm_begin(&s);
    /* The jump cut also retargets it: from car to car it would stay on the old one. */
    scm_op(&s, OP_RESTORE_CAMERA);
    scm_op(&s, OP_CAMERA_BEHIND);
    scm_run(&s);
}
