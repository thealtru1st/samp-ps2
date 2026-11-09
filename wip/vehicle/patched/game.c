/* Glue to GTA SA PS2, SLES-52927 v1.00. All addresses found by RE of that ELF. */
#include "game.h"
#include "scm.h"
#include "util.h"

#define PLAYERS        0x00709650u /* CWorld::Players, stride 0x1BC, ped at +0 */

#define PED_MATRIX     0x014
#define PED_MOVE_SPEED 0x044 /* 083C SET_CHAR_VELOCITY writes +0x44 */
#define PED_HEALTH     0x580
#define PED_ARMOUR     0x588

#define OP_GET_PLAYER_CHAR   0x01F5
#define OP_SET_CHAR_COORDS   0x00A1
#define OP_SET_PLAYER_CTRL   0x01B4
#define OP_DO_FADE           0x016A
#define OP_RESTORE_CAMERA    0x02EB
#define OP_CAMERA_BEHIND     0x0373
#define OP_LOAD_SCENE        0x03CB
#define OP_REQUEST_COLLISION 0x04E4
#define OP_MAX_WANTED        0x01F0
#define OP_CLEAR_WANTED      0x0110
#define OP_PED_DENSITY       0x03DE
#define OP_CAR_DENSITY       0x01EB
#define OP_CLEAR_HELP        0x03E6
#define OP_GIVE_CLOTHES      0x087B
#define OP_ZONE_NAMES        0x09BA
#define OP_BUILD_PLAYER      0x070D
#define OP_REQUEST_MODEL     0x0247
#define OP_LOAD_ALL_MODELS   0x038B
#define OP_SET_PLAYER_MODEL  0x09C7

#define OP_CREATE_CHAR       0x009A
#define OP_DELETE_CHAR       0x009B
#define OP_SET_CHAR_HEADING  0x0173
#define OP_GO_STRAIGHT       0x05D3

#define PED_TYPE_CIV  4
#define MOVE_WALK     4
#define MOVE_RUN      6
#define MAX_REMOTE    64
#define SNAP_DIST2    (6.0f * 6.0f)
#define STOP_DIST2    (0.6f * 0.6f)

#define L_PLAYER 0
#define L_CHAR   1
#define L_TMP    2


static float fsqrt(float v)
{
    float r;
    __asm__("sqrt.s %0, %1" : "=f"(r) : "f"(v));
    return r;
}

static unsigned ped_ptr(void) { return *(volatile unsigned *)PLAYERS; }

int game_has_player(void) { return ped_ptr() != 0; }

int game_in_vehicle(void)
{
    unsigned ped = ped_ptr();
    return ped && (*(unsigned char *)(ped + 0x4A5) & 1) && *(unsigned *)(ped + 0x5CC);
}

void game_boot(void)
{
    scm_t s;

    /* main.scm itself created the player and stopped before any mission (see the
     * stop stub in tools/mkpnach.py). It left the screen faded out. */
    scm_set_local(L_PLAYER, 0);
    scm_begin(&s);
    scm_op(&s, OP_GET_PLAYER_CHAR); scm_i8(&s, 0); scm_local(&s, L_CHAR);
    scm_op(&s, OP_SET_PLAYER_CTRL); scm_i8(&s, 0); scm_i8(&s, 1);
    /* SA-MP worlds have no police, peds or traffic: the server owns all of that. */
    scm_op(&s, OP_MAX_WANTED); scm_i8(&s, 0);
    scm_op(&s, OP_CLEAR_WANTED); scm_i8(&s, 0);
    scm_op(&s, OP_ZONE_NAMES); scm_i8(&s, 0);
    scm_op(&s, OP_PED_DENSITY); scm_f(&s, 0.0f);
    scm_op(&s, OP_CAR_DENSITY); scm_f(&s, 0.0f);
    /* The intro normally dresses CJ; without a build step he has no visible body. */
    scm_op(&s, OP_GIVE_CLOTHES); scm_i8(&s, 0); scm_str(&s, "vest"); scm_str(&s, "vest"); scm_i8(&s, 0);
    scm_op(&s, OP_GIVE_CLOTHES); scm_i8(&s, 0); scm_str(&s, "JEANSDENIM"); scm_str(&s, "JEANS"); scm_i8(&s, 2);
    scm_op(&s, OP_GIVE_CLOTHES); scm_i8(&s, 0); scm_str(&s, "SNEAKERBINCBLK"); scm_str(&s, "SNEAKER"); scm_i8(&s, 3);
    scm_op(&s, OP_BUILD_PLAYER); scm_i8(&s, 0);
    scm_op(&s, OP_RESTORE_CAMERA);
    scm_op(&s, OP_CAMERA_BEHIND);
    scm_op(&s, OP_DO_FADE); scm_i32(&s, 500); scm_i8(&s, 1);
    scm_run(&s);
    slog("game: story stopped, char %d ped %x", scm_get_local(L_CHAR), ped_ptr());
}

void game_teleport(float x, float y, float z)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_REQUEST_COLLISION); scm_f(&s, x); scm_f(&s, y);
    scm_op(&s, OP_LOAD_SCENE); scm_f(&s, x); scm_f(&s, y); scm_f(&s, z);
    scm_op(&s, OP_SET_CHAR_COORDS); scm_local(&s, L_CHAR); scm_f(&s, x); scm_f(&s, y); scm_f(&s, z);
    scm_op(&s, OP_CAMERA_BEHIND);
    scm_run(&s);
}

void game_set_skin(int model)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_REQUEST_MODEL); scm_i32(&s, model);
    scm_op(&s, OP_LOAD_ALL_MODELS);
    scm_op(&s, OP_SET_PLAYER_MODEL); scm_i8(&s, 0); scm_i32(&s, model);
    scm_op(&s, OP_CLEAR_HELP);
    scm_run(&s);
}

/* Rotation matrix (right, up, at as columns) to quaternion. */
static void mat_to_quat(const float *m, float *q)
{
    float rx = m[0], ry = m[1], rz = m[2];
    float ux = m[4], uy = m[5], uz = m[6];
    float ax = m[8], ay = m[9], az = m[10];
    float w = 1.0f + rx + uy + az, x = 1.0f + rx - uy - az;
    float y = 1.0f - rx + uy - az, z = 1.0f - rx - uy + az;

    q[0] = w > 0.0f ? fsqrt(w) * 0.5f : 0.0f;
    q[1] = x > 0.0f ? fsqrt(x) * 0.5f : 0.0f;
    q[2] = y > 0.0f ? fsqrt(y) * 0.5f : 0.0f;
    q[3] = z > 0.0f ? fsqrt(z) * 0.5f : 0.0f;
    if (ay - uz < 0.0f) q[1] = -q[1];
    if (rz - ax < 0.0f) q[2] = -q[2];
    if (ux - ry < 0.0f) q[3] = -q[3];
}

void game_read_local(samp_local_t *l)
{
    unsigned ped = ped_ptr();
    const float *m, *v;
    float h, a;

    memset(l, 0, sizeof *l);
    l->quat[0] = 1.0f;
    if (!ped)
        return;
    m = *(const float **)(ped + PED_MATRIX);
    if (!m)
        return;
    l->pos[0] = m[12];
    l->pos[1] = m[13];
    l->pos[2] = m[14];
    mat_to_quat(m, l->quat);
    v = (const float *)(ped + PED_MOVE_SPEED);
    l->vel[0] = v[0];
    l->vel[1] = v[1];
    l->vel[2] = v[2];
    h = *(float *)(ped + PED_HEALTH);
    a = *(float *)(ped + PED_ARMOUR);
    l->health = h < 0.0f ? 0 : h > 255.0f ? 255 : (unsigned char)h;
    l->armour = a < 0.0f ? 0 : a > 255.0f ? 255 : (unsigned char)a;
}

/* ---- remote players -------------------------------------------------- */

typedef struct {
    int used, id, handle, moving, held;
    float x, y, z, heading;
    float tx, ty, tz;
} remote_t;

static remote_t remotes[MAX_REMOTE];

static remote_t *find_remote(unsigned id)
{
    int i;
    for (i = 0; i < MAX_REMOTE; i++)
        if (remotes[i].used && remotes[i].id == (int)id)
            return &remotes[i];
    return 0;
}

/* atan2 approximation, good to well under a degree, returning degrees. */
static float atan2_deg(float y, float x)
{
    float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, a, d;
    if (ax == 0.0f && ay == 0.0f)
        return 0.0f;
    a = (ax < ay ? ax : ay) / (ax > ay ? ax : ay);
    d = a * (0.9724f - 0.1919f * a * a) * 57.29578f;
    if (ay > ax) d = 90.0f - d;
    if (x < 0) d = 180.0f - d;
    return y < 0 ? -d : d;
}

void game_remote_add(unsigned id, unsigned skin, const float *p, float angle)
{
    scm_t s;
    remote_t *r = find_remote(id);
    int i;

    if (r)
        return;
    for (i = 0; i < MAX_REMOTE; i++)
        if (!remotes[i].used) { r = &remotes[i]; break; }
    if (!r)
        return;
    /* Model 0 is the special player model and cannot be created as a normal ped. */
    if (skin == 0)
        skin = 7;
    scm_begin(&s);
    scm_op(&s, OP_REQUEST_MODEL); scm_i32(&s, skin);
    scm_op(&s, OP_LOAD_ALL_MODELS);
    scm_op(&s, OP_CREATE_CHAR); scm_i8(&s, PED_TYPE_CIV); scm_i32(&s, skin);
    scm_f(&s, p[0]); scm_f(&s, p[1]); scm_f(&s, p[2]); scm_local(&s, L_TMP);
    scm_op(&s, OP_SET_CHAR_HEADING); scm_local(&s, L_TMP); scm_f(&s, angle);
    scm_run(&s);
    r->used = 1;
    r->id = id;
    r->handle = scm_get_local(L_TMP);
    r->moving = 0;
    r->held = 0;
    r->x = r->tx = p[0];
    r->y = r->ty = p[1];
    r->z = r->tz = p[2];
    r->heading = angle;
    slog("game: remote %d streamed in, skin %d handle %d", id, skin, r->handle);
}

void game_remote_del(unsigned id)
{
    remote_t *r = find_remote(id);
    scm_t s;
    if (!r)
        return;
    scm_set_local(L_TMP, r->handle);
    scm_begin(&s);
    scm_op(&s, OP_DELETE_CHAR); scm_local(&s, L_TMP);
    scm_run(&s);
    r->used = 0;
    slog("game: remote %d streamed out", id);
}

void game_remote_sync(unsigned id, const float *p, const float *q, int moving)
{
    remote_t *r = find_remote(id);
    float fx, fy;
    if (!r)
        return;
    r->tx = p[0];
    r->ty = p[1];
    r->tz = p[2];
    /* forward vector of the wire quaternion, which is conjugated, then GTA heading in degrees */
    fx = 2.0f * (q[1] * q[2] + q[0] * q[3]);
    fy = 1.0f - 2.0f * (q[1] * q[1] + q[3] * q[3]);
    r->heading = atan2_deg(-fx, fy);
    r->moving = moving;
}

int game_remote_pos(unsigned id, float *pos)
{
    remote_t *r = find_remote(id);
    if (!r)
        return 0;
    pos[0] = r->tx;
    pos[1] = r->ty;
    pos[2] = r->tz;
    return 1;
}

int game_remote_handle(unsigned id)
{
    remote_t *r = find_remote(id);
    return r ? r->handle : 0;
}

/* While on, feat_vehicle.c has the ped: game_remote_update leaves it alone. */
void game_remote_hold(unsigned id, int on)
{
    remote_t *r = find_remote(id);
    if (r)
        r->held = on;
}

void game_remote_clear(void)
{
    int i;
    for (i = 0; i < MAX_REMOTE; i++)
        if (remotes[i].used)
            game_remote_del(remotes[i].id);
}

void game_remote_update(void)
{
    int i;
    for (i = 0; i < MAX_REMOTE; i++) {
        remote_t *r = &remotes[i];
        float dx, dy, dz, d2;
        scm_t s;
        if (!r->used || r->held)
            continue;
        dx = r->tx - r->x; dy = r->ty - r->y; dz = r->tz - r->z;
        d2 = dx * dx + dy * dy + dz * dz;
        scm_set_local(L_TMP, r->handle);
        scm_begin(&s);
        if (d2 > SNAP_DIST2) {
            scm_op(&s, OP_SET_CHAR_COORDS); scm_local(&s, L_TMP);
            scm_f(&s, r->tx); scm_f(&s, r->ty); scm_f(&s, r->tz);
            r->x = r->tx; r->y = r->ty; r->z = r->tz;
        } else if (d2 > STOP_DIST2) {
            /* The game's own walk task, so the ped animates instead of sliding. */
            scm_op(&s, OP_GO_STRAIGHT); scm_local(&s, L_TMP);
            scm_f(&s, r->tx); scm_f(&s, r->ty); scm_f(&s, r->tz);
            scm_i8(&s, d2 > 9.0f ? MOVE_RUN : MOVE_WALK); scm_i32(&s, 2000);
            r->x = r->tx; r->y = r->ty; r->z = r->tz;
        } else if (!r->moving) {
            scm_op(&s, OP_SET_CHAR_HEADING); scm_local(&s, L_TMP); scm_f(&s, r->heading);
        }
        scm_run(&s);
    }
}
