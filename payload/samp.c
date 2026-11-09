#include "samp.h"
#include "fps.h"
#include "rak.h"
#include "bitstream.h"
#include "samp_rpc.h"
#include "util.h"

#define NETGAME_VERSION 4057
#define CLIENT_VERSION  "0.3.7"

#define RPC_SPAWN         52
#define RPC_CLIENT_JOIN   25
#define RPC_CHAT          101
#define RPC_COMMAND       50
#define RPC_REQUEST_CLASS 128
#define RPC_REQUEST_SPAWN 129
#define RPC_INIT_GAME     139
#define RPC_SET_POS       12
#define RPC_CLIENT_MSG    93
#define RPC_STREAM_IN     32
#define RPC_STREAM_OUT    163
#define RPC_PLAYER_QUIT   138
#define RPC_PLAYER_JOIN   137
#define RPC_SET_SKIN      153

#define ID_RPC       20
#define ID_TIMESTAMP 40
#define ID_FOOT_SYNC 207

#define SYNC_MS 100      /* until the server says otherwise */
#define SPAWN_QUIET_TICKS 12
#define SYNC_MIN_MS 40   /* one frame at 25 fps: there is nothing new to send any sooner */

samp_stats_t samp_stats;

static int state;
static char my_name[25];
static char names[1000][25];
static unsigned last_sync;
static int quiet;

static void on_rpc(unsigned id, bs_t *b);

/* The server requires the client serial to be a hex number divisible by 1001.
 * On PC it is derived from hardware; here it is a stable per-name value. */
static void make_serial(char *out, const char *name)
{
    unsigned w[5] = {0x53414D50, 0x50533221, 0x2F2F2F2F, 0x12345678, 0};
    unsigned long long carry = 0;
    int i, n = 0, started = 0;

    for (i = 0; name[i]; i++) {
        w[i & 3] = (w[i & 3] ^ (unsigned char)name[i]) * 0x01000193u;
    }
    for (i = 0; i < 5; i++) { /* w *= 1001, little-endian limbs */
        unsigned long long v = (unsigned long long)w[i] * 1001 + carry;
        w[i] = (unsigned)v;
        carry = v >> 32;
    }
    for (i = 39; i >= 0; i--) {
        unsigned nib = (w[i >> 3] >> ((i & 7) * 4)) & 15;
        if (nib || started) {
            out[n++] = "0123456789ABCDEF"[nib];
            started = 1;
        }
    }
    out[n] = 0;
}

static void on_connected(unsigned token, unsigned idx)
{
    static unsigned char buf[256];
    char serial[48];
    bs_t b;
    unsigned nlen = strlen(my_name), slen, vlen = sizeof CLIENT_VERSION - 1;

    make_serial(serial, my_name);
    slen = strlen(serial);
    bs_wr_init(&b, buf, sizeof buf);
    bs_w32(&b, NETGAME_VERSION);
    bs_w8(&b, 1);
    bs_w8(&b, nlen);
    bs_wbytes(&b, my_name, nlen);
    bs_w32(&b, token ^ NETGAME_VERSION);
    bs_w8(&b, slen);
    bs_wbytes(&b, serial, slen);
    bs_w8(&b, vlen);
    bs_wbytes(&b, CLIENT_VERSION, vlen);
    rak_rpc(RPC_CLIENT_JOIN, buf, b.bits, RAK_RELIABLE, 0);
    state = SAMP_JOINING;
    slog("samp: connected, index %d, ClientJoin sent as %s", idx, my_name);
}

static float root(float v)
{
    /* Only the quaternion w term needs a root; Newton keeps samp.c platform-free. */
    float x = v > 0.0f ? v : 0.0f, r = 1.0f;
    int i;
    if (x == 0.0f)
        return 0.0f;
    for (i = 0; i < 12; i++)
        r = 0.5f * (r + x / r);
    return r;
}

static void on_foot_sync(bs_t *b)
{
    float pos[3], q[4];
    unsigned pid, keys, lr = 0, ud = 0, nw, nx, ny, nz;
    int i;

    pid = bs_r16(b);
    if (bs_rbool(b)) lr = bs_r16(b);
    if (bs_rbool(b)) ud = bs_r16(b);
    keys = bs_r16(b);
    for (i = 0; i < 3; i++)
        pos[i] = bs_rf(b);
    nw = bs_rbool(b); nx = bs_rbool(b); ny = bs_rbool(b); nz = bs_rbool(b);
    q[1] = bs_r16(b) / 65535.0f;
    q[2] = bs_r16(b) / 65535.0f;
    q[3] = bs_r16(b) / 65535.0f;
    q[0] = root(1.0f - q[1] * q[1] - q[2] * q[2] - q[3] * q[3]);
    if (nw) q[0] = -q[0];
    if (nx) q[1] = -q[1];
    if (ny) q[2] = -q[2];
    if (nz) q[3] = -q[3];
    if (b->err)
        return;
    samp_stats.last_pid = pid;
    samp_stats.last_pos[0] = pos[0];
    samp_stats.last_pos[1] = pos[1];
    samp_stats.last_pos[2] = pos[2];
    samp_stats.foot_in++;
    samp_plat_player_sync(pid, pos, q, keys, lr || ud);
}

static void on_packet(const unsigned char *d, unsigned bits)
{
    static unsigned char rpcbuf[65536];
    bs_t b;
    unsigned id;

    bs_rd_init(&b, d, bits);
    id = bs_r8(&b);
    if (id == ID_TIMESTAMP) {
        bs_r32(&b);
        id = bs_r8(&b);
    }
    if (id == ID_RPC) {
        unsigned rpc = bs_r8(&b), n = bs_rcomp32(&b);
        bs_t r;
        if (b.err || BS_BYTES(n) > sizeof rpcbuf)
            return;
        if (n)
            bs_rbits(&b, rpcbuf, n, 0);
        bs_rd_init(&r, rpcbuf, n);
        samp_stats.rpcs++;
        on_rpc(rpc, &r);
        return;
    }
    samp_stats.sync_in++;
    if (id == ID_FOOT_SYNC) {
        bs_t copy = b;
        on_foot_sync(&b);
        samp_sync_dispatch(id, &copy);
    } else if (id >= 200)
        samp_sync_dispatch(id, &b);
}

static void on_closed(int reason)
{
    static const char *const why[] = {"disconnected", "timed out", "server full", "banned",
                                      "wrong password", "auth rejected", "connection lost", "kicked"};
    slog("samp: closed: %s", why[reason]);
    state = reason ? SAMP_ERROR : SAMP_OFFLINE;
    if (reason)
        samp_plat_closed(why[reason]);
}

static const rak_cb_t callbacks = {samp_plat_send, on_connected, on_packet, on_closed};

void samp_connect(unsigned ip, unsigned port, const char *name, const char *password)
{
    unsigned i;
    for (i = 0; name[i] && i < sizeof my_name - 1; i++)
        my_name[i] = name[i];
    my_name[i] = 0;
    memset(&samp_stats, 0, sizeof samp_stats);
    state = SAMP_CONNECTING;
    rak_connect(ip, port, password, &callbacks);
}

void samp_disconnect(void)
{
    rak_disconnect();
    state = SAMP_OFFLINE;
}

int samp_state(void) { return state; }

void samp_datagram(const unsigned char *d, unsigned len, unsigned now)
{
    rak_datagram(d, len, now);
}

static void show_chat(const char *who, const char *text, unsigned n)
{
    static char line[176];
    unsigned o = 0, i;
    for (i = 0; who[i] && o < 24; i++)
        line[o++] = who[i];
    line[o++] = ':';
    line[o++] = ' ';
    for (i = 0; i < n && o < sizeof line - 1; i++)
        line[o++] = text[i];
    line[o] = 0;
    samp_plat_message(line);
}

void samp_chat(const char *text)
{
    static unsigned char buf[160];
    bs_t b;
    unsigned n = strlen(text);
    if (n > 144)
        n = 144;
    bs_wr_init(&b, buf, sizeof buf);
    if (text[0] == '/') { /* commands are a separate RPC with a 32-bit length */
        bs_w32(&b, n);
        bs_wbytes(&b, text, n);
        rak_rpc(RPC_COMMAND, buf, b.bits, RAK_RELIABLE, 0);
        return;
    }
    bs_w8(&b, n);
    bs_wbytes(&b, text, n);
    rak_rpc(RPC_CHAT, buf, b.bits, RAK_RELIABLE, 0);
    show_chat(my_name, text, n); /* echoes of our own id are dropped on receive */
}

/* The rates come from the server's onfoot_rate and incar_rate, 30 by default. */
static unsigned samp_rate(unsigned ms)
{
    return ms < SYNC_MIN_MS ? SYNC_MIN_MS : ms > 1000 ? 1000 : ms;
}

static void parse_init(bs_t *b)
{
    unsigned n;

    bs_rbool(b); bs_rbool(b); bs_rbool(b); bs_rbool(b); /* zone names .. chat radius flag */
    bs_rf(b);
    bs_rbool(b);
    bs_rf(b);
    bs_rbool(b); bs_rbool(b); bs_rbool(b);
    bs_r32(b);
    samp_stats.player_id = bs_r16(b);
    bs_rbool(b);
    bs_r32(b);
    bs_r8(b); bs_r8(b);
    bs_rf(b);
    bs_rbool(b);
    bs_r32(b);
    bs_rbool(b);
    samp_stats.foot_ms = samp_rate(bs_r32(b));
    samp_stats.car_ms = samp_rate(bs_r32(b));
    bs_r32(b); bs_r32(b); bs_r32(b);
    n = bs_r8(b);
    if (n > sizeof samp_stats.server_name - 1)
        n = sizeof samp_stats.server_name - 1;
    bs_rbytes(b, samp_stats.server_name, n);
    samp_stats.server_name[n] = 0;
}

static void request_class(unsigned cls)
{
    unsigned char c[4] = {cls, cls >> 8, cls >> 16, cls >> 24};
    rak_rpc(RPC_REQUEST_CLASS, c, 32, RAK_RELIABLE, 0);
    state = SAMP_CLASS;
}

void samp_reselect_class(void) { request_class(0); }

/* The server hands out weapons when it sees the spawn, and a script that looks at the
 * weapon in our sync must not catch one sent before they got here. Counted in ticks, not
 * time: the teleports around a spawn can hold the game up for two seconds. */
void samp_spawn_sent(void) { quiet = SPAWN_QUIET_TICKS * fps_mult(); }

static void on_rpc(unsigned id, bs_t *b)
{
    bs_t copy = *b;
    samp_rpc_dispatch(id, &copy);
    switch (id) {
    case RPC_INIT_GAME:
        parse_init(b);
        slog("samp: InitGame: '%s', my id %d", samp_stats.server_name, samp_stats.player_id);
        request_class(0);
        break;
    case RPC_REQUEST_CLASS: {
        unsigned ok = bs_r8(b);
        bs_r8(b);
        samp_stats.skin = bs_r32(b);
        bs_r8(b);
        samp_stats.spawn[0] = bs_rf(b);
        samp_stats.spawn[1] = bs_rf(b);
        samp_stats.spawn[2] = bs_rf(b);
        slog("samp: class reply selectable=%d", ok);
        rak_rpc(RPC_REQUEST_SPAWN, 0, 0, RAK_RELIABLE, 0);
        state = SAMP_SPAWNING;
        break;
    }
    case RPC_REQUEST_SPAWN: {
        unsigned allow = bs_r32(b);
        slog("samp: spawn reply allow=%d", allow);
        if (allow) {
            rak_rpc(RPC_SPAWN, 0, 0, RAK_RELIABLE, 0);
            samp_spawn_sent();
            state = SAMP_SPAWNED;
        }
        break;
    }
    case RPC_PLAYER_JOIN: {
        unsigned pid = bs_r16(b), n;
        bs_r32(b);
        bs_r8(b);
        n = bs_r8(b);
        if (pid < 1000 && n < sizeof names[0] && !b->err) {
            bs_rbytes(b, names[pid], n);
            names[pid][n] = 0;
        }
        break;
    }
    case RPC_CHAT: {
        static char text[145];
        unsigned pid = bs_r16(b), n = bs_r8(b);
        if (n > sizeof text - 1)
            n = sizeof text - 1;
        bs_rbytes(b, text, n);
        if (!b->err && pid != samp_stats.player_id)
            show_chat(pid < 1000 && names[pid][0] ? names[pid] : "?", text, n);
        break;
    }
    case RPC_STREAM_IN: {
        unsigned pid = bs_r16(b), skin;
        float pos[3], angle;
        int i;
        bs_r8(b);
        skin = bs_r32(b);
        for (i = 0; i < 3; i++)
            pos[i] = bs_rf(b);
        angle = bs_rf(b);
        if (!b->err)
            samp_plat_player_add(pid, skin, pos, angle);
        break;
    }
    case RPC_PLAYER_QUIT:
    case RPC_STREAM_OUT:
        samp_plat_player_del(bs_r16(b));
        break;
    case RPC_SET_POS: {
        float pos[3];
        int i;
        for (i = 0; i < 3; i++)
            pos[i] = bs_rf(b);
        if (!b->err)
            samp_plat_set_pos(pos);
        break;
    }
    case RPC_SET_SKIN: {
        unsigned pid = bs_r32(b), skin = bs_r32(b);
        if (!b->err && pid == samp_stats.player_id)
            samp_plat_set_skin(skin);
        break;
    }
    case RPC_CLIENT_MSG: {
        static char text[145];
        unsigned n;
        bs_r32(b); /* colour */
        n = bs_r32(b);
        if (n > sizeof text - 1)
            n = sizeof text - 1;
        bs_rbytes(b, text, n);
        text[n] = 0;
        if (!b->err)
            samp_plat_message(text);
        break;
    }
    }
}

static void send_foot_sync(void)
{
    static unsigned char buf[80];
    samp_local_t l;
    bs_t b;
    int i;

    samp_plat_local(&l);
    if (l.hold)
        return;
    bs_wr_init(&b, buf, sizeof buf);
    bs_w8(&b, ID_FOOT_SYNC);
    bs_w16(&b, l.lr);
    bs_w16(&b, l.ud);
    bs_w16(&b, l.keys);
    for (i = 0; i < 3; i++) bs_wf(&b, l.pos[i]);
    for (i = 0; i < 4; i++) bs_wf(&b, l.quat[i]);
    bs_w8(&b, l.health);
    bs_w8(&b, l.armour);
    bs_w8(&b, l.weapon);
    bs_w8(&b, l.special);
    for (i = 0; i < 3; i++) bs_wf(&b, l.vel[i]);
    for (i = 0; i < 3; i++) bs_wf(&b, 0.0f); /* surf offset */
    bs_w16(&b, 0); /* surf id */
    bs_w16(&b, l.anim_id);
    bs_w16(&b, l.anim_flags);
    if (rak_send(buf, b.bits, RAK_UNRELIABLE_SEQ, 0) == 0)
        samp_stats.sync_out++;
}

void samp_tick(unsigned now)
{
    rak_tick(now);
    if (quiet)
        quiet--;
    /* A little slack, or a frame that comes 1 ms early would halve the rate. */
    if (state == SAMP_SPAWNED && rak_state() == RAK_CONNECTED && !quiet &&
        now - last_sync + 5 >= (samp_stats.foot_ms ? samp_stats.foot_ms : SYNC_MS) &&
        !samp_plat_in_vehicle()) {
        last_sync = now;
        send_foot_sync();
    }
}
