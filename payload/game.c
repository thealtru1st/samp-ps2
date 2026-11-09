/* Glue to GTA SA PS2, SLES-52927 v1.00. All addresses found by RE of that ELF. */
#include "game.h"
#include "scm.h"
#include "util.h"
#include "feat_combat.h"
#include "feat_combat_game.h"

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
#define OP_CLEAR_TASKS       0x0792

#define PED_TYPE_CIV  4
#define MOVE_WALK     4
#define MOVE_RUN      6
#define MAX_REMOTE    64
#define SNAP_DIST2    (6.0f * 6.0f)
#define STOP_DIST2    (0.6f * 0.6f)
#define HOLD_DIST2    (1.5f * 1.5f)  /* aiming players walk too: keep up with them */
#define ARRIVED_DIST2 (1.0f * 1.0f)

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

/* No LOAD_SCENE: it blocks until the area is loaded, and with a server's objects filling
 * streaming memory that took 34 to 52 s, long enough for the server to drop us. Like the
 * PC client, ask for the collision and let the world stream in around the player. */
void game_teleport(float x, float y, float z)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_REQUEST_COLLISION); scm_f(&s, x); scm_f(&s, y);
    scm_op(&s, OP_SET_CHAR_COORDS); scm_local(&s, L_CHAR); scm_f(&s, x); scm_f(&s, y); scm_f(&s, z);
    scm_op(&s, OP_CAMERA_BEHIND);
    scm_run(&s);
}

/* Stream info, stride 12: low three bits of +6 are 1 once the model is resident. */
static int model_loaded(int m) { return m >= 0 && m < 20000 && (*(volatile unsigned char *)(0x007C3CF6u + m * 12) & 7) == 1; }

/* ---- more memory for the game ----
 * The game heap (CMemoryMgr, descriptor at 0x66CD00) is 24 MB ending at 0x01FFA800.
 * Model memory is a 13.5 MB budget inside it, but models in use can't be evicted, so a big
 * server's objects, peds and cars push past the budget until the heap itself runs out.
 * The game's out-of-memory path then deadlocks on the heap's own lock (measured: main
 * thread in WaitSema from free at 0x100FE0, called from malloc's retry at 0x101A9C).
 * PCSX2's extra memory (needed for this payload anyway) leaves 0x04000000-0x07000000
 * unused, so the heap grows into it: blocks are address ordered with a 16-byte header
 * (size, used flag at +4, mem id at +6, previous block at +12) and a size-0 end marker.
 * The old end marker becomes a used block over the gap (the end of 32 MB, this payload,
 * the status block), a used block covers the new space, and the game's own free puts
 * that on its free lists. */
#define HEAP_BIG      0x0066CD00u  /* [0] first block, [1] end marker, [16] bytes used */
#define HEAP_SEMA     (0x0066B170u + 0xA4u)
#define HEAP_LOCK     0x00100230u  /* WaitSema(id) */
#define HEAP_UNLOCK   0x00100240u  /* SignalSema(id) */
#define GAME_FREE     0x00101BE0u  /* free(p): takes the block off the stats at gp+0x90/0x9C */
#define FREE_BYTES    (0x0066B170u + 0x90u)
#define FREE_COUNT    (0x0066B170u + 0x9Cu)
#define EXT_START     0x04000000u
#define EXT_END       0x07000000u
#define STREAM_USED   0x0066BE8Cu  /* CStreaming::ms_memoryUsed */
#define STREAM_AVAIL  0x00666E88u  /* CStreaming::ms_memoryAvailable */
#define STREAM_EXTRA  (24u << 20)  /* half the new space for models, half for the rest */
#define HEAP_RESERVE  (3u << 20)   /* stop loading with this little heap left */
#define HU32(a) (*(volatile unsigned *)(a))

static unsigned stream_base;

void game_heap_extend(void)
{
    volatile unsigned *h = (volatile unsigned *)HEAP_BIG;
    void (*lock)(int) = (void (*)(int))HEAP_LOCK;
    void (*unlock)(int) = (void (*)(int))HEAP_UNLOCK;
    void (*gfree)(void *) = (void (*)(void *))GAME_FREE;
    unsigned old = h[1], blk = EXT_START, end = EXT_END - 16, used0, i;
    int sema = *(volatile int *)HEAP_SEMA;

    if (old >= EXT_START || old < 0x01000000u || HU32(old) != 0 || (HU32(old + 4) & 0xFF) != 1) {
        slog("heap: left as is, end marker %x", old);
        return;
    }
    for (i = EXT_START; i < EXT_END; i += 0x10000)   /* must be untouched RAM */
        if (HU32(i) || HU32(i + 0x8000)) {
            slog("heap: %x in use, left as is", i);
            return;
        }
    lock(sema);
    used0 = h[16];
    HU32(old) = blk - old - 16;                /* old end marker: used block over the gap */
    HU32(blk) = end - blk - 16;
    HU32(blk + 4) = 0xFFFF0001u;               /* used, mem id -1 */
    HU32(blk + 8) = 0;
    HU32(blk + 12) = old;
    HU32(end) = 0;
    HU32(end + 4) = 0xFFFF0001u;
    HU32(end + 8) = 0;
    HU32(end + 12) = blk;
    h[1] = end;
    h[16] = used0 + (end - old);               /* all of it used until the free below */
    HU32(FREE_BYTES) += HU32(blk) + 16;      /* as if malloc had handed it out */
    HU32(FREE_COUNT) += 1;
    unlock(sema);
    gfree((void *)(blk + 16));
    slog("heap: grown to %x, %d KB used (was %d), first free %x size %d KB", end,
         h[16] / 1024, used0 / 1024, blk, HU32(blk) / 1024);
    stream_base = HU32(STREAM_AVAIL);
}

static unsigned heap_free(void)
{
    volatile unsigned *h = (volatile unsigned *)HEAP_BIG;
    unsigned size = h[1] - h[0];
    return h[16] < size ? size - h[16] : 0;
}

/* Each frame: the game sets its own budget at boot and may set it again. */
void game_stream_budget(void)
{
    volatile unsigned *h = (volatile unsigned *)HEAP_BIG;
    if (h[1] != EXT_END - 16)
        return;
    if (HU32(STREAM_AVAIL) != stream_base + STREAM_EXTRA) {
        stream_base = HU32(STREAM_AVAIL);   /* the game set a new budget: add to that */
        HU32(STREAM_AVAIL) = stream_base + STREAM_EXTRA;
    }
}

/* Models in use can't be evicted: past the budget, or near the end of the heap, our
 * loads only add pinned memory, so load nothing new until the game frees some. */
int game_stream_over(void)
{
    return HU32(STREAM_USED) > HU32(STREAM_AVAIL) || heap_free() < HEAP_RESERVE;
}

static void request_model(int m)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_REQUEST_MODEL); scm_i32(&s, m);
    scm_run(&s);
}

/* Skins load over the next frames, never with LOAD_ALL_MODELS: when a server's objects
 * fill streaming memory the game can't make room, and that opcode then waits forever. */
#define SKIN_WAIT_FRAMES 600
static int skin_pending = -1, skin_frames;

static void apply_skin(int model)
{
    scm_t s;
    scm_begin(&s);
    scm_op(&s, OP_SET_PLAYER_MODEL); scm_i8(&s, 0); scm_i32(&s, model);
    scm_op(&s, OP_CLEAR_HELP);
    scm_run(&s);
}

void game_set_skin(int model)
{
    if (model_loaded(model)) {
        skin_pending = -1;
        apply_skin(model);
        return;
    }
    request_model(model);
    skin_pending = model;
    skin_frames = 0;
}

void game_skin_tick(void)
{
    if (skin_pending < 0)
        return;
    if (model_loaded(skin_pending)) {
        apply_skin(skin_pending);
        skin_pending = -1;
        feat_combat_skin_set(); /* the model change drops the weapon in hand */
        return;
    }
    if (++skin_frames >= SKIN_WAIT_FRAMES) {
        slog("game: skin %d never loaded, keeping the current one", skin_pending);
        skin_pending = -1;
    } else if (skin_frames % 50 == 0) {
        request_model(skin_pending);
    }
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
    feat_combat_local(l);
}

/* ---- remote players -------------------------------------------------- */

typedef struct {
    int skin, used, id, handle, moving, held;
    int waiting;          /* skin still streaming in: no ped yet, handle 0 */
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

/* Creates the ped once its skin is resident; until then the record waits with handle 0,
 * which every user already treats as "not streamed in". */
static void remote_create(remote_t *r)
{
    scm_t s;
    if (!model_loaded(r->skin)) {
        /* Out of memory: wear a skin that is already loaded rather than load another. */
        int i;
        if (game_stream_over()) {
            for (i = 0; i < MAX_REMOTE; i++)
                if (remotes[i].used && !remotes[i].waiting && model_loaded(remotes[i].skin)) {
                    r->skin = remotes[i].skin;
                    break;
                }
            if (!model_loaded(r->skin) && model_loaded(7))
                r->skin = 7;
        }
        if (!model_loaded(r->skin)) {
            if (++r->waiting % 50 == 0 && !game_stream_over())
                request_model(r->skin);
            return;
        }
    }
    scm_begin(&s);
    scm_op(&s, OP_CREATE_CHAR); scm_i8(&s, PED_TYPE_CIV); scm_i32(&s, r->skin);
    scm_f(&s, r->tx); scm_f(&s, r->ty); scm_f(&s, r->tz); scm_local(&s, L_TMP);
    scm_op(&s, OP_SET_CHAR_HEADING); scm_local(&s, L_TMP); scm_f(&s, r->heading);
    scm_run(&s);
    r->handle = scm_get_local(L_TMP);
    r->x = r->tx; r->y = r->ty; r->z = r->tz;
    r->waiting = 0;
    slog("game: remote %d streamed in, skin %d handle %d", r->id, r->skin, r->handle);
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
    skin = feat_combat_safe_skin(skin);
    if (skin == 0)
        skin = 7;
    r->used = 1;
    r->id = id;
    r->handle = 0;
    r->skin = skin;
    r->moving = 0;
    r->held = 0;
    r->x = r->tx = p[0];
    r->y = r->ty = p[1];
    r->z = r->tz = p[2];
    r->heading = angle;
    r->waiting = 1;
    if (!game_stream_over())
        request_model(skin);
    remote_create(r);
}

extern void feat_world_char_going(unsigned id) __attribute__((weak));

void game_remote_del(unsigned id)
{
    remote_t *r = find_remote(id);
    scm_t s;
    if (!r)
        return;
    if (feat_world_char_going)   /* objects on the ped first, see feat_world_obj.c */
        feat_world_char_going(id);
    if (!r->waiting) {
        scm_set_local(L_TMP, r->handle);
        scm_begin(&s);
        scm_op(&s, OP_DELETE_CHAR); scm_local(&s, L_TMP);
        scm_run(&s);
    }
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
    cg_ped_t p;
    if (!r)
        return 0;
    /* Where the ped really is: foot sync stops while it is in a car or being walked in. */
    if (!r->waiting && cg_read_ped(r->handle, &p)) {
        pos[0] = p.pos[0];
        pos[1] = p.pos[1];
        pos[2] = p.pos[2];
        return 1;
    }
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
        float dx, dy, dz, d2, flat2, moved2, pos[3];
        cg_ped_t p;
        scm_t s;
        int gate;
        if (!r->used || r->held)
            continue;
        if (r->waiting) {
            remote_create(r);
            continue;
        }
        {   /* diagnosis (Valrise render freeze): a skin the game dropped while we draw it */
            static unsigned told;
            if (!model_loaded(r->skin) && told < 30) {
                told++;
                slog("diag: player %d skin %d not loaded while live", r->id, r->skin);
            }
        }
        gate = feat_combat_remote_gate(r->id, r->handle);
        if (gate == COMBAT_REMOTE_SKIP)
            continue;
        if (gate == COMBAT_REMOTE_LOST) {
            /* The game took the ped or killed it. Never send a stale handle to a script
             * command: the opcodes use the null ped without checking. DELETE_CHAR is the
             * exception, and it clears a body that is still there. */
            unsigned id = r->id, skin = r->skin;
            pos[0] = r->tx; pos[1] = r->ty; pos[2] = r->tz;
            game_remote_del(id);
            game_remote_add(id, skin, pos, r->heading);
            continue;
        }
        /* Goes by where the ped really is, because a walk task left over from before a
         * jump would otherwise carry it back to the old target for good. */
        if (!cg_read_ped(r->handle, &p))
            continue;
        dx = r->tx - p.pos[0]; dy = r->ty - p.pos[1]; dz = r->tz - p.pos[2];
        flat2 = dx * dx + dy * dy;
        d2 = flat2 + dz * dz;
        dx = r->tx - r->x; dy = r->ty - r->y; dz = r->tz - r->z;
        moved2 = dx * dx + dy * dy + dz * dz;
        scm_set_local(L_TMP, r->handle);
        scm_begin(&s);
        if (d2 > SNAP_DIST2 || (gate == COMBAT_REMOTE_HOLD && d2 > HOLD_DIST2)) {
            if (gate != COMBAT_REMOTE_HOLD) {
                scm_op(&s, OP_CLEAR_TASKS); scm_local(&s, L_TMP);
            }
            scm_op(&s, OP_SET_CHAR_COORDS); scm_local(&s, L_TMP);
            scm_f(&s, r->tx); scm_f(&s, r->ty); scm_f(&s, r->tz);
            r->x = r->tx; r->y = r->ty; r->z = r->tz;
        } else if (gate == COMBAT_REMOTE_HOLD) {
            /* Combat has it aiming or in an animation: a walk task would cancel that. */
        } else if (moved2 > STOP_DIST2 || (flat2 > ARRIVED_DIST2 && !r->moving)) {
            /* The game's own walk task, so the ped animates instead of sliding. */
            scm_op(&s, OP_GO_STRAIGHT); scm_local(&s, L_TMP);
            scm_f(&s, r->tx); scm_f(&s, r->ty); scm_f(&s, r->tz);
            scm_i8(&s, feat_combat_remote_move(r->id, d2 > 9.0f ? MOVE_RUN : MOVE_WALK)); scm_i32(&s, 2000);
            r->x = r->tx; r->y = r->ty; r->z = r->tz;
        } else if (!r->moving) {
            scm_op(&s, OP_SET_CHAR_HEADING); scm_local(&s, L_TMP); scm_f(&s, r->heading);
        }
        scm_run(&s);
    }
}
