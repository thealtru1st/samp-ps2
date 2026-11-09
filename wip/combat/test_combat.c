/* Host tests for payload/feat_combat.c and feat_combat_game.c over a fake memory image.
 * Server input goes in as real bit streams through samp_rpc_dispatch, and everything sent
 * back is decoded the way the server's read() does. Nothing here runs the game itself:
 * it proves the logic and the bytes, not that the opcodes do what the notes say. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "fake.h"
#include "feat.h"
#include "feat_combat.h"
#include "feat_combat_game.h"
#include "bitstream.h"
#include "scm.h"

#define GP 0x0066B170u
#define POOL_STRUCT 0x800000u
#define POOL_FLAGS  0x800100u
#define POOL_OBJS   0x810000u
#define PED(i)      (POOL_OBJS + (i) * 0x810u)
#define MATRIX      0x7F0000u
#define ME          PED(0)
#define MY_ID       7

static samp_rpc_handlers_t handlers;
static int fails, passes;
static unsigned now = 1000;

#define W32(a, v) (*(unsigned *)(cg_mem + (a)) = (unsigned)(v))
#define WF(a, v)  (*(float *)(cg_mem + (a)) = (v))
#define W8(a, v)  (cg_mem[a] = (v))
#define RF(a)     (*(float *)(cg_mem + (a)))

#define CHECK(c) do { if (c) passes++; else { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static void world(void)
{
    int i;
    memset(cg_mem, 0, CG_MEM_SIZE);
    W32(GP + 0x828, POOL_STRUCT);
    W32(POOL_STRUCT, POOL_OBJS);
    W32(POOL_STRUCT + 4, POOL_FLAGS);
    W32(POOL_STRUCT + 8, 140);
    for (i = 0; i < 140; i++)
        W8(POOL_FLAGS + i, 0x80);
    W8(POOL_FLAGS + 0, 1);
    scm_set_local(1, 1); /* game_boot: the player's handle, slot 0 flag 1 */
    W32(0x00709650, ME);
    W32(ME + 0x14, MATRIX);
    WF(MATRIX + 0x30, 100.0f); WF(MATRIX + 0x34, 200.0f); WF(MATRIX + 0x38, 10.0f);
    WF(ME + 0x580, 100.0f);
    W8(ME + 0x7A0, 0xFF);
    /* anim blocks */
    W32(GP + 0xB8, 2);
    memcpy(cg_mem + 0x677E30, "PED", 4);
    W8(0x677E30 + 0x10, 1);
    memcpy(cg_mem + 0x677E50, "DANCING", 8);
    /* models */
    for (i = 0; i < 300; i++)
        W32(0x7A8800 + i * 4, 1);
    W32(0x7A8800 + 42 * 4, 0);
    W32(0x7A8800 + 370 * 4, 1);
}

static int add_ped(int slot, int flag)
{
    W8(POOL_FLAGS + slot, flag);
    W32(PED(slot) + 0x14, MATRIX + 0x100 * slot);
    WF(PED(slot) + 0x580, 100.0f);
    W32(PED(slot) + 0x7A0, 0xFF); /* no last weapon */
    return slot << 8 | flag;
}

static void reset_log(void) { fake_nops = 0; fake_nmsgs = 0; }

static int count_op(unsigned op)
{
    int i, n = 0;
    for (i = 0; i < fake_nops; i++)
        n += fake_ops[i].op == op;
    return n;
}

static fake_op_t *find_op(unsigned op)
{
    int i;
    for (i = fake_nops - 1; i >= 0; i--)
        if (fake_ops[i].op == op)
            return &fake_ops[i];
    return 0;
}

static fake_msg_t *find_msg(int kind, int id)
{
    int i;
    for (i = 0; i < fake_nmsgs; i++)
        if (fake_msgs[i].kind == kind && fake_msgs[i].id == id)
            return &fake_msgs[i];
    return 0;
}

static int count_msg(int kind, int id)
{
    int i, n = 0;
    for (i = 0; i < fake_nmsgs; i++)
        n += fake_msgs[i].kind == kind && fake_msgs[i].id == id;
    return n;
}

/* ---- server input, as the wire has it ---- */
static unsigned char wire[4096];
static bs_t w;

static void begin(void) { bs_wr_init(&w, wire, sizeof wire); }
static void rpc(unsigned id)
{
    bs_t r;
    bs_rd_init(&r, wire, w.bits);
    samp_rpc_dispatch(id, &r);
}
static void sync_in(unsigned id)
{
    bs_t r;
    bs_rd_init(&r, wire, w.bits);
    samp_sync_dispatch(id, &r);
}

static void rpc_weapon(unsigned id, unsigned weapon, unsigned ammo)
{
    begin(); bs_w32(&w, weapon); bs_w32(&w, ammo); rpc(id);
}
static void rpc_float(unsigned id, float v) { begin(); bs_wf(&w, v); rpc(id); }

static void stream_in(unsigned id, int handle)
{
    int i;
    fake_handles[id] = handle;
    begin();
    bs_w16(&w, id); bs_w8(&w, 0); bs_w32(&w, 7);
    bs_wf(&w, 110.0f); bs_wf(&w, 200.0f); bs_wf(&w, 10.0f); bs_wf(&w, 0.0f);
    bs_w32(&w, 0xFFFFFFFF); bs_w8(&w, 4);
    for (i = 0; i < SRPC_SKILLS; i++)
        bs_w16(&w, 0);
    rpc(SRPC_player_stream_in);
}

static void foot(unsigned id, unsigned lr, unsigned ud, unsigned keys, float x, float y, float z,
                 unsigned weapon, unsigned action)
{
    begin();
    bs_w16(&w, id);
    bs_wbool(&w, lr != 0); if (lr) bs_w16(&w, lr);
    bs_wbool(&w, ud != 0); if (ud) bs_w16(&w, ud);
    bs_w16(&w, keys);
    bs_wf(&w, x); bs_wf(&w, y); bs_wf(&w, z);
    bs_wbool(&w, 0); bs_wbool(&w, 0); bs_wbool(&w, 0); bs_wbool(&w, 0);
    bs_w16(&w, 0); bs_w16(&w, 0); bs_w16(&w, 0);  /* identity quat: facing +y */
    bs_w8(&w, 0xFF);                              /* health and armour: 15, 15 */
    bs_w8(&w, weapon);
    bs_w8(&w, action);
    bs_wf(&w, 0.0f);                              /* compressed velocity: magnitude 0 */
    bs_wbool(&w, 0);                              /* surfing */
    bs_wbool(&w, 0);                              /* animation */
    sync_in(SRPC_sync_foot);
}

static void bullet(unsigned player, unsigned type, unsigned hit, unsigned weapon)
{
    int i;
    begin();
    bs_w16(&w, player);
    bs_w8(&w, type);
    bs_w16(&w, hit);
    for (i = 0; i < 9; i++)
        bs_wf(&w, 1.0f);
    bs_w8(&w, weapon);
    sync_in(SRPC_sync_bullet);
}

static void class_reply(void)
{
    int i;
    begin();
    bs_w8(&w, 1); bs_w8(&w, 0); bs_w32(&w, 7); bs_w8(&w, 0);
    bs_wf(&w, 500.0f); bs_wf(&w, 600.0f); bs_wf(&w, 20.0f); bs_wf(&w, 90.0f);
    bs_w32(&w, 24); bs_w32(&w, 0); bs_w32(&w, 0);
    for (i = 0; i < 3; i++)
        bs_w32(&w, i == 0 ? 50 : 0);
    rpc(SRPC_request_class_reply);
}

static void tick(void) { feat_tick(now); now += 16; }
static void tick_for(unsigned ms) { unsigned end = now + ms; while ((int)(now - end) < 0) tick(); }

/* ---- what we sent, read back in the server's order ---- */
typedef struct { int taking; unsigned player; float damage; unsigned weapon, part; } dmg_t;

static dmg_t read_dmg(const fake_msg_t *m)
{
    dmg_t d;
    bs_t b;
    bs_rd_init(&b, m->d, m->bits);
    d.taking = bs_rbool(&b);
    d.player = bs_r16(&b);
    d.damage = bs_rf(&b);
    d.weapon = bs_r32(&b);
    d.part = bs_r32(&b);
    CHECK(!b.err);
    return d;
}

static void start_session(void)
{
    world();
    fake_state = SAMP_SPAWNED;
    samp_stats.player_id = MY_ID;
    memset(fake_handles, 0, sizeof(int) * 1000);
    fake_state = SAMP_OFFLINE;
    tick();
    fake_state = SAMP_SPAWNED;
    class_reply();
    tick(); tick();
    reset_log();
}

static void test_keys(void)
{
    static const struct { int idx; unsigned bit; } map[] = {
        {4, 1}, {18, 2}, {17, 4}, {16, 8}, {15, 16}, {14, 32}, {7, 64}, {6, 128}, {5, 256}, {19, 512}};
    unsigned i;
    short p[20];
    for (i = 0; i < sizeof map / sizeof map[0]; i++) {
        memset(p, 0, sizeof p);
        p[map[i].idx] = 12; /* pressure mode: anything non-zero counts */
        CHECK(feat_combat_keys(p, 0, 0) == map[i].bit);
    }
    memset(p, 0, sizeof p);
    CHECK(feat_combat_keys(p, 0, 0) == 0);
    CHECK(feat_combat_keys(p, 0, -127) == 0);          /* full stick is a run */
    CHECK(feat_combat_keys(p, 30, 0) == 1024);         /* gentle stick walks */
    p[3] = -100; CHECK(feat_combat_keys(p, 0, 0) == 2048);
    p[3] = 100; CHECK(feat_combat_keys(p, 0, 0) == 4096);
    p[3] = 0; p[2] = -100; CHECK(feat_combat_keys(p, 0, 0) == 8192);
    p[2] = 100; CHECK(feat_combat_keys(p, 0, 0) == 16384);
    p[2] = 20; CHECK(feat_combat_keys(p, 0, 0) == 0);
}

static void test_local_sync(void)
{
    samp_local_t l;
    start_session();
    memset(&l, 0, sizeof l);
    cg_host_lr = -127; cg_host_ud = 64;
    *(short *)(cg_mem + 0x700BC0 + 0x20) = 255; /* cross */
    W8(ME + 0x758, 2);                          /* slot 2 */
    W32(ME + 0x5E0 + 2 * 0x1C, 24);
    feat_combat_local(&l);
    CHECK(l.lr == 0xFF81);
    CHECK(l.ud == 64);
    CHECK(l.keys == 8);
    CHECK(l.weapon == 24);
    CHECK(l.special == 0);
    W8(ME + 0x4A7, 4);
    W32(ME + 0x5E0 + 2 * 0x1C, 53);             /* a drowning type, not a weapon */
    feat_combat_local(&l);
    CHECK(l.special == 1);
    CHECK(l.weapon == 0);
    *(short *)(cg_mem + 0x700BC0 + 0x6E) = 0x20; /* controls off */
    feat_combat_local(&l);
    CHECK(l.keys == 0 && l.lr == 0 && l.ud == 0);
#ifdef SAMP_LOCAL_HAS_COMBAT
    CHECK(l.anim_id == 1273 && l.hold == 0);
#endif
    cg_host_lr = cg_host_ud = 0;
}

static void test_weapons(void)
{
    start_session();
    rpc_weapon(SRPC_give_weapon, 24, 100);
    /* Resident model: given at once, and never a blocking LOAD_ALL_MODELS. */
    CHECK(count_op(0x0781) == 1 && count_op(0x038B) == 0);
    CHECK(find_op(0x01B2) && find_op(0x01B2)->i[1] == 24 && find_op(0x01B2)->i[2] == 100);
    CHECK(find_op(0x01B9) && find_op(0x01B9)->i[1] == 24);
    reset_log();
    /* Model still streaming: requested, held back, then given in order once resident. */
    cg_host_not_resident = 1;
    rpc_weapon(SRPC_give_weapon, 25, 30);
    rpc_weapon(SRPC_give_weapon, 31, 60);
    CHECK(count_op(0x0247) >= 1 && count_op(0x038B) == 0 && !find_op(0x01B2));
    tick(); tick();
    CHECK(!find_op(0x01B2));
    cg_host_not_resident = 0;
    tick();
    {   /* both given, in the order they came */
        int i, first = -1;
        for (i = 0; i < fake_nops && first < 0; i++)
            if (fake_ops[i].op == 0x01B2)
                first = fake_ops[i].i[1];
        CHECK(count_op(0x01B2) == 2 && first == 25 && find_op(0x01B2)->i[1] == 31);
    }
    reset_log();
    rpc_weapon(SRPC_give_weapon, 19, 100);
    rpc_weapon(SRPC_give_weapon, 47, 100);
    rpc_weapon(SRPC_give_weapon, 200, 100);
    rpc_weapon(SRPC_give_weapon, 0, 100);
    CHECK(fake_nops == 0);
    rpc_weapon(SRPC_give_weapon, 31, 0xFFFFFFFF);
    CHECK(find_op(0x01B2)->i[2] == 99999);
    reset_log();
    begin(); bs_w32(&w, 30); rpc(SRPC_set_armed_weapon);
    CHECK(fake_nops == 0);                       /* not owned */
    W32(ME + 0x5E0 + 5 * 0x1C, 30);
    begin(); bs_w32(&w, 30); rpc(SRPC_set_armed_weapon);
    CHECK(find_op(0x01B9) && find_op(0x01B9)->i[1] == 30);
    reset_log();
    begin(); bs_w32(&w, 0); rpc(SRPC_set_armed_weapon);
    CHECK(find_op(0x01B9) && find_op(0x01B9)->i[1] == 0);
    reset_log();
    begin(); bs_w8(&w, 30); bs_w16(&w, 12); rpc(SRPC_set_ammo);
    CHECK(find_op(0x017B) && find_op(0x017B)->i[2] == 12);
    reset_log();
    begin(); rpc(SRPC_reset_weapons);
    CHECK(count_op(0x048F) == 1);
}

static void test_health(void)
{
    start_session();
    rpc_float(SRPC_set_health, 55.0f);
    CHECK(RF(ME + 0x580) == 55.0f);
    tick_for(600);
    CHECK(count_msg('r', 115) == 0);             /* the server's own change is not damage */
    rpc_float(SRPC_set_armour, 40.0f);
    CHECK(RF(ME + 0x588) == 40.0f);
    rpc_float(SRPC_set_health, NAN);
    CHECK(count_op(0x0223) == 1);                /* NaN treated as 0 */
    world();
    start_session();
    rpc_float(SRPC_set_health, 0.0f);
    CHECK(find_op(0x0223) && find_op(0x0223)->i[1] == 0);
}

static void test_damage_out(void)
{
    dmg_t d;
    int h;
    start_session();
    h = add_ped(3, 5);
    stream_in(12, h);
    tick();
    reset_log();
    /* a fall: no source, weapon 54 */
    WF(ME + 0x580, 90.0f);
    W8(ME + 0x7A0, 54);
    W32(ME + 0x7A4, 0);
    tick_for(300);
    CHECK(count_msg('r', 115) == 1);
    d = read_dmg(find_msg('r', 115));
    CHECK(d.taking == 1 && d.player == 0xFFFF && d.weapon == 54 && d.part == 3);
    CHECK(fabsf(d.damage - 10.0f) < 0.01f);
    reset_log();
    /* hit by a remote ped */
    WF(ME + 0x580, 80.0f);
    W8(ME + 0x7A0, 24);
    W32(ME + 0x7A4, PED(3));
    tick_for(300);
    d = read_dmg(find_msg('r', 115));
    CHECK(d.taking == 1 && d.player == 12 && d.weapon == 24);
    reset_log();
    /* fire ticking every frame is added up, not sent every frame */
    W8(ME + 0x7A0, 37);
    W32(ME + 0x7A4, 0);
    {
        int i;
        float hp = 80.0f;
        for (i = 0; i < 20; i++) {
            hp -= 0.5f;
            WF(ME + 0x580, hp);
            tick();
        }
    }
    tick_for(300);
    CHECK(count_msg('r', 115) >= 1 && count_msg('r', 115) <= 3);
}

static void test_bullet_in(void)
{
    dmg_t d;
    int h;
    start_session();
    h = add_ped(3, 5);
    stream_in(12, h);
    tick();
    reset_log();
    WF(ME + 0x588, 20.0f);
    tick();
    bullet(12, 1, MY_ID, 24);
    CHECK(RF(ME + 0x588) == 0.0f);               /* armour goes first */
    CHECK(fabsf(RF(ME + 0x580) - (100.0f - 26.2f)) < 0.01f);
    tick_for(300);
    d = read_dmg(find_msg('r', 115));
    CHECK(d.taking == 1 && d.player == 12 && d.weapon == 24 && fabsf(d.damage - 46.2f) < 0.01f);
    CHECK(count_msg('r', 115) == 1);             /* not counted again as a health drop */
    reset_log();
    bullet(12, 1, MY_ID + 1, 24);                /* someone else was hit */
    bullet(12, 0, 0xFFFF, 24);
    bullet(12, 1, MY_ID, 5);                     /* a bat fires no bullets */
    bullet(999, 1, MY_ID, 24);                   /* a player we can't see */
    bullet(50000, 1, MY_ID, 24);
    tick_for(300);
    CHECK(count_msg('r', 115) == 0);
    /* the killing shot goes through the game's die event, and an old fall in the ped's
     * last damage must not take the credit: seen in the live run */
    reset_log();
    WF(ME + 0x580, 5.0f);
    W8(ME + 0x7A0, 54);
    tick();
    bullet(12, 1, MY_ID, 24);
    CHECK(find_op(0x0223) && find_op(0x0223)->i[1] == 0);
    CHECK(RF(ME + 0x580) == 0.0f);
    W32(ME + 0x568, 55);
    reset_log();
    tick();
    {
        fake_msg_t *m = find_msg('r', 53);
        bs_t b;
        CHECK(m != 0);
        if (m) {
            bs_rd_init(&b, m->d, m->bits);
            CHECK(bs_r8(&b) == 24);
            CHECK(bs_r16(&b) == 12);
        }
    }
}

static void test_death_flow(void)
{
    samp_local_t l;
    int h;
    start_session();
    h = add_ped(3, 5);
    stream_in(12, h);
    tick();
    bullet(12, 1, MY_ID, 34);
    reset_log();
    W32(ME + 0x568, 55);
    WF(ME + 0x580, 0.0f);
    W8(GP + 0xAB4, 1);
    tick();
    {
        fake_msg_t *m = find_msg('r', 53);
        bs_t b;
        CHECK(m != 0);
        if (m) {
            bs_rd_init(&b, m->d, m->bits);
            CHECK(bs_r8(&b) == 34);
            CHECK(bs_r16(&b) == 12);
        }
    }
    CHECK(find_op(0x016E) && find_op(0x016E)->f[0] == 500.0f && find_op(0x016E)->f[3] == 90.0f);
#ifdef SAMP_LOCAL_HAS_COMBAT
    memset(&l, 0, sizeof l);
    feat_combat_local(&l);
    CHECK(l.hold == 1);
#endif
    (void)l;
    tick_for(3000);
    CHECK(count_msg('r', 53) == 1);
    CHECK(count_msg('r', 52) == 0);
    /* the game resurrects: health back, state back */
    reset_log();
    W32(ME + 0x568, 1);
    WF(ME + 0x580, 100.0f);
    W8(GP + 0xAB4, 0);
    tick();
    CHECK(count_msg('r', 52) == 1);
    CHECK(fake_last_skin == 7);
    CHECK(count_op(0x048F) == 1);
    CHECK(find_op(0x01B2) && find_op(0x01B2)->i[1] == 24 && find_op(0x01B2)->i[2] == 50);
    CHECK(find_op(0x01B9) && find_op(0x01B9)->i[1] == 24);
    CHECK(find_op(0x02AB) && find_op(0x02AB)->i[1] == 1);
#ifdef SAMP_LOCAL_HAS_COMBAT
    feat_combat_local(&l);
    CHECK(l.hold == 0);
#endif
    /* unknown death reports 255 and the invalid id */
    reset_log();
    W32(ME + 0x568, 54);
    tick();
    {
        fake_msg_t *m = find_msg('r', 53);
        bs_t b;
        CHECK(m != 0);
        if (m) {
            bs_rd_init(&b, m->d, m->bits);
            CHECK(bs_r8(&b) == 255);
            CHECK(bs_r16(&b) == 0xFFFF);
        }
    }
    /* force_class_selection: the next respawn asks for a class, not a spawn */
    begin(); rpc(SRPC_force_class_selection);
    reset_log();
    W32(ME + 0x568, 1);
    tick();
    CHECK(count_msg('r', 128) == 1 && count_msg('r', 52) == 0);
}

static void test_stale_handles(void)
{
    int h;
    start_session();
    h = add_ped(3, 5);
    stream_in(12, h);
    tick();
    CHECK(feat_combat_remote_gate(12, h) == COMBAT_REMOTE_NORMAL);
    /* the game removes the ped: the slot's flag byte changes */
    W8(POOL_FLAGS + 3, 0x85);
    CHECK(feat_combat_remote_gate(12, h) == COMBAT_REMOTE_LOST);
    reset_log();
    foot(12, 0, 0, 128 | 4, 111.0f, 200.0f, 10.0f, 24, 0);
    tick_for(500);
    CHECK(fake_nops == 0 || (count_op(0x0668) == 0 && count_op(0x01B2) == 0));
    /* slot reused by another ped with a new generation */
    W8(POOL_FLAGS + 3, 6);
    CHECK(feat_combat_remote_gate(12, h) == COMBAT_REMOTE_LOST);
    /* out of range and junk handles */
    CHECK(feat_combat_remote_gate(12, 0) == COMBAT_REMOTE_LOST);
    CHECK(feat_combat_remote_gate(12, -1) == COMBAT_REMOTE_LOST);
    CHECK(feat_combat_remote_gate(12, 9999 << 8 | 1) == COMBAT_REMOTE_LOST);
    /* killed locally against all odds */
    W8(POOL_FLAGS + 3, 5);
    W32(PED(3) + 0x568, 55);
    CHECK(feat_combat_remote_gate(12, h) == COMBAT_REMOTE_LOST);
    W32(PED(3) + 0x568, 1);
    /* killed by the server: leave the body alone */
    begin(); bs_w16(&w, 12); rpc(SRPC_player_death);
    CHECK(find_op(0x05BE) != 0);
    W32(PED(3) + 0x568, 55);
    CHECK(feat_combat_remote_gate(12, h) == COMBAT_REMOTE_SKIP);
    /* they come back without a restream: the body is replaced */
    foot(12, 0, 0, 0, 111.0f, 200.0f, 10.0f, 0, 0);
    CHECK(feat_combat_remote_gate(12, h) == COMBAT_REMOTE_LOST);
}

static void test_remote_combat(void)
{
    int h;
    unsigned hp, ar;
    start_session();
    h = add_ped(3, 5);
    stream_in(12, h);
    tick();
    CHECK(count_op(0x060B) == 1 && count_op(0x0446) == 1 && count_op(0x04D8) == 1 && count_op(0x02A9) == 1);
    CHECK(RF(PED(3) + 0x580) == 1000.0f);
    CHECK(find_op(0x0446)->i[1] == 0);
    reset_log();
    foot(12, 0, 0, 0, 111.0f, 200.0f, 10.0f, 24, 0);
    tick();
    CHECK(find_op(0x01B2) && find_op(0x01B2)->i[1] == 24);
    CHECK(find_op(0x01B9) && find_op(0x01B9)->i[1] == 24);
    CHECK(feat_combat_remote_health(12, &hp, &ar) && hp == 100 && ar == 100);
    reset_log();
    tick();
    CHECK(count_op(0x01B2) == 0);                /* only once per change */
    /* aiming, then firing, take the ped from game.c */
    foot(12, 0, 0, 128, 111.0f, 200.0f, 10.0f, 24, 0);
    tick_for(50);
    CHECK(count_op(0x0667) >= 1);
    CHECK(feat_combat_remote_gate(12, h) == COMBAT_REMOTE_HOLD);
    reset_log();
    foot(12, 0, 0, 132, 111.0f, 200.0f, 10.0f, 24, 0);
    tick_for(300);
    CHECK(count_op(0x0668) >= 1 && count_op(0x0668) <= 3);
    foot(12, 0, 0, 0, 111.0f, 200.0f, 10.0f, 24, 0);
    CHECK(feat_combat_remote_gate(12, h) == COMBAT_REMOTE_NORMAL);
    CHECK(feat_combat_remote_move(12, 4) == 6);
    foot(12, 0, 0, 8, 111.0f, 200.0f, 10.0f, 24, 0);
    CHECK(feat_combat_remote_move(12, 4) == 7);
    foot(12, 0, 0, 1024, 111.0f, 200.0f, 10.0f, 24, 0);
    CHECK(feat_combat_remote_move(12, 6) == 4);
    CHECK(feat_combat_remote_move(13, 6) == 6);
    /* our hit on them */
    tick();
    reset_log();
    W8(PED(3) + 0x7A0, 24);
    W32(PED(3) + 0x7A4, ME);
    WF(PED(3) + 0x580, 954.0f);
    W8(ME + 0x758, 2);
    W32(ME + 0x5E0 + 2 * 0x1C, 24);
    W32(ME + 0x5E0 + 2 * 0x1C + 12, 50);
    tick();                                      /* primes the ammo count */
    reset_log();
    W8(PED(3) + 0x7A0, 24);
    WF(PED(3) + 0x580, 954.0f);
    W32(ME + 0x5E0 + 2 * 0x1C + 12, 49);
    tick();
    {
        dmg_t d = read_dmg(find_msg('r', 115));
        fake_msg_t *m = find_msg('s', 206);
        bs_t b;
        CHECK(d.taking == 0 && d.player == 12 && d.weapon == 24 && fabsf(d.damage - 46.2f) < 0.01f);
        CHECK(m != 0);
        if (m) {
            bs_rd_init(&b, m->d, m->bits);
            CHECK(bs_r8(&b) == 206);
            CHECK(bs_r8(&b) == 1);
            CHECK(bs_r16(&b) == 12);
        }
    }
    CHECK(RF(PED(3) + 0x580) == 1000.0f);
    /* a miss */
    reset_log();
    W32(ME + 0x5E0 + 2 * 0x1C + 12, 48);
    tick();
    {
        fake_msg_t *m = find_msg('s', 206);
        bs_t b;
        CHECK(m != 0);
        if (m) {
            bs_rd_init(&b, m->d, m->bits);
            bs_r8(&b);
            CHECK(bs_r8(&b) == 0);
            CHECK(bs_r16(&b) == 0xFFFF);
        }
    }
    /* a fall on the remote ped, with our stale source, is not our hit */
    reset_log();
    W32(PED(3) + 0x7A0, 54);
    W32(PED(3) + 0x7A4, ME);
    WF(PED(3) + 0x580, 990.0f);
    tick();
    CHECK(count_msg('r', 115) == 0);
    /* a fist in the face */
    reset_log();
    foot(12, 0, 0, 0, 100.0f, 199.0f, 10.0f, 0, 0);
    tick();
    foot(12, 0, 0, 4, 100.0f, 199.0f, 10.0f, 0, 0);
    tick();
    tick_for(300);
    CHECK(count_msg('r', 115) == 1);
    CHECK(read_dmg(find_msg('r', 115)).player == 12);
    /* facing away */
    reset_log();
    foot(12, 0, 0, 0, 100.0f, 201.0f, 10.0f, 0, 0);
    tick_for(500);
    foot(12, 0, 0, 4, 100.0f, 201.0f, 10.0f, 0, 0);
    tick_for(300);
    CHECK(count_msg('r', 115) == 0);
    /* a jump on the rising edge only */
    reset_log();
    foot(12, 0, 0, 32, 100.0f, 201.0f, 10.0f, 0, 0);
    tick();
    tick();
    CHECK(count_op(0x05BC) == 1);
    /* skills */
    reset_log();
    begin(); bs_w16(&w, MY_ID); bs_w32(&w, 2); bs_w16(&w, 500); rpc(SRPC_set_skill_level);
    CHECK(find_op(0x062A) && find_op(0x062A)->i[0] == 71 && find_op(0x062A)->f[1] == 500.0f);
    begin(); bs_w16(&w, 12); bs_w32(&w, 0); bs_w16(&w, 999); rpc(SRPC_set_skill_level);
    CHECK(find_op(0x081A) && find_op(0x081A)->i[1] == 2);
    reset_log();
    begin(); bs_w16(&w, MY_ID); bs_w32(&w, 11); bs_w16(&w, 500); rpc(SRPC_set_skill_level);
    begin(); bs_w16(&w, MY_ID); bs_w32(&w, 1); bs_w16(&w, 5000); rpc(SRPC_set_skill_level);
    CHECK(count_op(0x062A) == 0);
    /* stream out forgets them */
    begin(); bs_w16(&w, 12); rpc(SRPC_player_stream_out);
    CHECK(!feat_combat_remote_health(12, &hp, &ar));
}

static void anim(unsigned id, const char *lib, const char *name, int loop, int time)
{
    begin();
    bs_w16(&w, id);
    bs_w8(&w, strlen(lib)); bs_wbytes(&w, lib, strlen(lib));
    bs_w8(&w, strlen(name)); bs_wbytes(&w, name, strlen(name));
    bs_wf(&w, 4.1f);
    bs_wbool(&w, loop); bs_wbool(&w, 0); bs_wbool(&w, 0); bs_wbool(&w, 0);
    bs_w32(&w, time);
    rpc(SRPC_apply_animation);
}

static void test_anims(void)
{
    int h;
    start_session();
    h = add_ped(3, 5);
    stream_in(12, h);
    tick();
    reset_log();
    anim(MY_ID, "NOSUCHLIB", "X", 0, 0);
    tick();
    CHECK(fake_nops == 0);                        /* no request for a file the game lacks */
    anim(MY_ID, "PED", "WALK_PLAYER", 1, 0);
    tick();
    CHECK(find_op(0x0605) && !strcmp(find_op(0x0605)->s[1], "WALK_PLAYER") && !strcmp(find_op(0x0605)->s[2], "PED"));
    CHECK(find_op(0x0605)->i[4] == 1);
    reset_log();
    cg_host_anim_found = 0;
    anim(MY_ID, "ped", "NOT_THERE", 0, 0);
    tick();
    CHECK(count_op(0x0605) == 0);
    cg_host_anim_found = 1;
    /* a file that has to load first */
    reset_log();
    anim(12, "dancing", "DNCE_M_A", 1, 0);
    CHECK(count_op(0x04ED) == 1);
    tick();
    CHECK(count_op(0x0605) == 0);
    W8(0x677E50 + 0x10, 1);
    tick();
    CHECK(count_op(0x0605) == 1);
    CHECK(feat_combat_remote_gate(12, h) == COMBAT_REMOTE_HOLD);
    /* walking breaks a looping animation */
    foot(12, 0xFF80, 0, 0, 111.0f, 200.0f, 10.0f, 0, 0);
    CHECK(feat_combat_remote_gate(12, h) == COMBAT_REMOTE_NORMAL);
    reset_log();
    anim(12, "PED", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", 0, 0);
    anim(MY_ID, "PEDPEDPEDPEDPEDPEDPEDPEDPED", "X", 0, 0);
    tick();
    CHECK(count_op(0x0605) == 0);
    reset_log();
    begin(); bs_w16(&w, MY_ID); rpc(SRPC_clear_animations);
    CHECK(count_op(0x0792) == 1);
    /* an expired wait is dropped */
    reset_log();
    W8(0x677E50 + 0x10, 0);
    anim(MY_ID, "DANCING", "DNCE_M_A", 1, 0);
    tick_for(6000);
    W8(0x677E50 + 0x10, 1);
    tick();
    CHECK(count_op(0x0605) == 0);
    /* special actions */
    reset_log();
    begin(); bs_w8(&w, 10); rpc(SRPC_set_special_action);
    CHECK(count_op(0x05C4) == 1);
    reset_log();
    begin(); bs_w8(&w, 0); rpc(SRPC_set_special_action);
    CHECK(count_op(0x0792) == 1);
}

static void test_world(void)
{
    float v[3];
    start_session();
    begin(); bs_w8(&w, 30); rpc(SRPC_set_weather);
    CHECK(count_op(0x01B6) == 0);
    begin(); bs_w8(&w, 8); rpc(SRPC_set_weather);
    CHECK(find_op(0x01B6) && find_op(0x01B6)->i[0] == 8);
    begin(); bs_w8(&w, 25); bs_w8(&w, 0); rpc(SRPC_set_time);
    CHECK(count_op(0x00C0) == 0);
    begin(); bs_w8(&w, 13); bs_w8(&w, 30); rpc(SRPC_set_time);
    CHECK(find_op(0x00C0) && find_op(0x00C0)->i[0] == 13 && find_op(0x00C0)->i[1] == 30);
    /* camera */
    reset_log();
    begin(); bs_wf(&w, 1); bs_wf(&w, 2); bs_wf(&w, 3); rpc(SRPC_set_camera_pos);
    CHECK(find_op(0x015F) && find_op(0x015F)->f[2] == 3.0f);
    begin(); bs_wf(&w, 1); bs_wf(&w, 2); bs_wf(&w, 3); bs_w8(&w, 2); rpc(SRPC_set_camera_look_at);
    CHECK(find_op(0x0160) && find_op(0x0160)->i[3] == 2);
    begin(); rpc(SRPC_set_camera_behind);
    CHECK(count_op(0x02EB) == 1 && count_op(0x0373) == 1);
    /* interpolation ends on the target and stops */
    reset_log();
    begin();
    bs_wbool(&w, 1);
    bs_wf(&w, 0); bs_wf(&w, 0); bs_wf(&w, 0);
    bs_wf(&w, 100); bs_wf(&w, 0); bs_wf(&w, 0);
    bs_w32(&w, 200); bs_w8(&w, 1);
    rpc(SRPC_interpolate_camera);
    tick_for(300);
    CHECK(count_op(0x015F) >= 5);
    CHECK(find_op(0x015F)->f[0] == 100.0f);
    reset_log();
    tick_for(100);
    CHECK(count_op(0x015F) == 0);
    /* positions */
    reset_log();
    begin(); bs_wf(&w, 1e30f); bs_wf(&w, 2); bs_wf(&w, 3); rpc(SRPC_set_pos_find_z);
    begin(); bs_wf(&w, NAN); bs_wf(&w, 2); bs_wf(&w, 3); rpc(SRPC_set_pos_find_z);
    CHECK(fake_teleports == 0 || count_op(0x02CE) == 0);
    begin(); bs_wf(&w, 1); bs_wf(&w, 2); bs_wf(&w, 3); rpc(SRPC_set_pos_find_z);
    CHECK(count_op(0x02CE) == 1 && fake_teleport_pos[2] == 13.5f);
    rpc_float(SRPC_set_facing_angle, 45.0f);
    CHECK(find_op(0x0173) && find_op(0x0173)->f[1] == 45.0f);
    v[0] = v[1] = v[2] = 0.5f;
    begin(); bs_wf(&w, v[0]); bs_wf(&w, v[1]); bs_wf(&w, v[2]); rpc(SRPC_set_velocity);
    CHECK(find_op(0x083C) && find_op(0x083C)->f[1] == 25.0f);
    /* money and wanted */
    reset_log();
    W32(0x00709650 + 0xE4, 500);
    begin(); bs_w32(&w, 250); rpc(SRPC_give_money);
    CHECK(find_op(0x0109) && find_op(0x0109)->i[1] == 250);
    begin(); rpc(SRPC_reset_money);
    CHECK(find_op(0x0109) && find_op(0x0109)->i[1] == -500);
    begin(); bs_w8(&w, 200); rpc(SRPC_set_wanted_level);
    /* stats and interior go out on their timers */
    reset_log();
    fake_area = 3;
    tick_for(1100);
    CHECK(count_msg('s', 205) >= 1);
    CHECK(count_msg('r', 118) == 1);
    {
        bs_t b;
        fake_msg_t *m = find_msg('s', 205);
        bs_rd_init(&b, m->d, m->bits);
        bs_r8(&b);
        CHECK((int)bs_r32(&b) == 500);
    }
    reset_log();
    begin(); bs_w8(&w, 9); rpc(SRPC_set_interior);
    fake_area = 9;
    CHECK(find_op(0x04BB) && find_op(0x04BB)->i[0] == 9 && find_op(0x0860) && find_op(0x0860)->i[1] == 9);
    tick_for(1100);
    CHECK(count_msg('r', 118) == 0);             /* the server knows */
    /* skin ids */
    CHECK(feat_combat_safe_skin(7) == 7);
    CHECK(feat_combat_safe_skin(42) == 0);
    CHECK(feat_combat_safe_skin(305) == 0);
    CHECK(feat_combat_safe_skin(70000) == 0);
    /* weapon sync */
    reset_log();
    W32(ME + 0x5E0 + 4 * 0x1C, 28);
    W32(ME + 0x5E0 + 4 * 0x1C + 12, 70000);
    tick_for(600);
    CHECK(count_msg('s', 204) == 1);
    {
        bs_t b;
        fake_msg_t *m = find_msg('s', 204);
        bs_rd_init(&b, m->d, m->bits);
        bs_r8(&b); bs_r16(&b); bs_r16(&b);
        CHECK(bs_r8(&b) == 4);
        CHECK(bs_r8(&b) == 28);
        CHECK(bs_r16(&b) == 0xFFFF);
    }
}

static void test_spectate(void)
{
    int h;
    start_session();
    h = add_ped(3, 5);
    stream_in(12, h);
    tick();
    reset_log();
    begin(); bs_w32(&w, 1); rpc(SRPC_toggle_spectating);
    CHECK(find_op(0x01B4) && find_op(0x01B4)->i[1] == 0);
    begin(); bs_w16(&w, 12); bs_w8(&w, 1); rpc(SRPC_spectate_player);
    reset_log();
    tick_for(300);
    CHECK(find_op(0x015F) != 0); /* a fixed camera moved every frame: 0159 does nothing here */
    CHECK(count_msg('s', 212) >= 2);
    CHECK(count_msg('s', 205) == 0);
    reset_log();
    begin(); bs_w32(&w, 0); rpc(SRPC_toggle_spectating);
    CHECK(count_msg('r', 52) == 1);
    CHECK(fake_teleport_pos[0] == 500.0f);
}

static void test_aim_out(void)
{
    start_session();
    /* camera: mode 53, front along +x */
    W8(0x006FE5B0 + 0x59, 1);
    *(short *)(cg_mem + 0x006FE5B0 + 0x238 + 0x180) = 53;
    WF(0x006FE5B0 + 0x238 + 0x304, 1.0f);
    WF(0x006FE5B0 + 0x238 + 0x310, 5.0f);
    W8(ME + 0x758, 2);
    W32(ME + 0x5E0 + 2 * 0x1C, 24);
    W32(ME + 0x5E0 + 2 * 0x1C + 8, 7);
    tick_for(300);
    CHECK(count_msg('s', 203) >= 2 && count_msg('s', 203) <= 8);
    {
        bs_t b;
        fake_msg_t *m = find_msg('s', 203);
        float f[3], p[3], z;
        unsigned v;
        int i;
        bs_rd_init(&b, m->d, m->bits);
        bs_r8(&b);
        CHECK(bs_r8(&b) == 53);
        for (i = 0; i < 3; i++) f[i] = bs_rf(&b);
        for (i = 0; i < 3; i++) p[i] = bs_rf(&b);
        z = bs_rf(&b);
        v = bs_r8(&b);
        CHECK(f[0] == 1.0f && p[0] == 5.0f && z == 0.0f);
        CHECK((v & 63) == 63 && (v >> 6) == 2);
        CHECK(bs_r8(&b) == 85);
    }
    /* follow camera and no aim key: nothing */
    reset_log();
    *(short *)(cg_mem + 0x006FE5B0 + 0x238 + 0x180) = 4;
    tick_for(300);
    CHECK(count_msg('s', 203) == 0);
}

/* Random server input through every decoder into every handler, on the live fake world. */
static void test_fuzz(void)
{
    unsigned seed = 12345;
    int i, h;
    for (i = 0; i < 400000; i++) {
        unsigned n, k, id;
        bs_t r;
        static unsigned char junk[512];

        if (i % 20000 == 0) {
            start_session();
            h = add_ped(3, 5);
            stream_in(12, h);
        }
        seed = seed * 1664525u + 1013904223u;
        n = (seed >> 8) % 64 + 1;
        id = (seed >> 16) & 0xFF;
        for (k = 0; k < n; k++) {
            seed = seed * 1664525u + 1013904223u;
            junk[k] = seed >> 24;
            if ((seed >> 8) & 1)
                junk[k] = "\0\14\7\1\377"[(seed >> 12) % 5];
        }
        bs_rd_init(&r, junk, n * 8);
        if (id >= 200 && id <= 212)
            samp_sync_dispatch(id, &r);
        else
            samp_rpc_dispatch(id, &r);
        if (i % 16 == 0) {
            reset_log();
            tick();
        }
        fake_state = SAMP_SPAWNED;
    }
    passes++;
}

int main(int c, char **v)
{
    if (c > 1)
        fake_dump_to(v[1]);
    world();
    feat_init();
    test_keys();
    test_local_sync();
    test_weapons();
    test_health();
    test_damage_out();
    test_bullet_in();
    test_death_flow();
    test_stale_handles();
    test_remote_combat();
    test_anims();
    test_world();
    test_spectate();
    test_aim_out();
    test_fuzz();
    printf("%d passed, %d failed\n", passes, fails);
    return fails != 0;
}
