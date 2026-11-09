#include "rak.h"
#include "bitstream.h"
#include "util.h"
#include "sampdata.h"
#include "authkey.h"

#define ID_INTERNAL_PING      6
#define ID_CONNECTED_PONG     9
#define ID_CONNECTION_REQUEST 11
#define ID_AUTH_KEY           12
#define ID_RPC                20
#define ID_DETECT_LOST        23
#define ID_OPEN_REQUEST       24
#define ID_OPEN_REPLY         25
#define ID_OPEN_COOKIE        26
#define ID_NEW_INCOMING       30
#define ID_NO_FREE            31
#define ID_DISCONNECT         32
#define ID_LOST               33
#define ID_ACCEPTED           34
#define ID_BANNED             36
#define ID_BAD_PASSWORD       37

#define COOKIE_XOR   0x6969
#define MAX_MSG      1400
#define MAX_OUT      256
#define MAX_ACKS     128
#define NCHAN        32
#define RESEND_MS    400
#define OPEN_RETRY   1000
#define OPEN_TRIES   10
#define PING_MS      5000
#define TIMEOUT_MS   15000

#define SPLIT_SLOTS  4
#define SPLIT_STRIDE 1500
#define SPLIT_MAX    512
/* Out-of-order reliable messages wait here for a lost one to be resent. A big server can
 * send a thousand messages in the time that takes, so this is sized for that, and a hash
 * keeps the lookup cheap. Anything that still doesn't fit is left unacknowledged, so the
 * server sends it again later, the way it would after any other loss. */
#define HOLD_MAX     4096 /* power of two; kept at most three quarters full */
#define HOLD_ARENA   (4 * 1024 * 1024)

typedef struct {
    int used, bits;
    unsigned short num, oidx;
    unsigned char rel, chan;
    unsigned last;
    unsigned char data[MAX_MSG];
} out_t;

typedef struct {
    int used;
    unsigned id, count, got, rel, chan, oidx;
    unsigned short bits[SPLIT_MAX];
    unsigned char data[SPLIT_MAX * SPLIT_STRIDE];
} split_t;

typedef struct { int used, bits, off; unsigned short chan, oidx; } hold_t;

static const rak_cb_t *cb;
static int state, fail;
static unsigned srv_ip, srv_port, now_ms, last_rx, last_open, open_tries, last_ping, ping_ms;
static unsigned char cipher_port;
static char password[32];

static unsigned short next_num, ord_out[NCHAN], seq_out[NCHAN], ord_in[NCHAN], seq_in[NCHAN];
static out_t outq[MAX_OUT];
static unsigned short acks[MAX_ACKS];
static int nacks;
static unsigned char seen[8192];

static split_t splits[SPLIT_SLOTS];
static hold_t holds[HOLD_MAX];
static unsigned char hold_arena[HOLD_ARENA];
static int hold_top, hold_count, hold_warned;
static unsigned char assembled[SPLIT_MAX * SPLIT_STRIDE];

/* All client->server datagrams go through SA-MP's substitution cipher; a leading
 * checksum byte covers the plaintext. The server replies in the clear. */
static void send_raw(const unsigned char *p, unsigned len)
{
    static unsigned char e[1600];
    unsigned i, sum = 0;
    if (len + 1 > sizeof e)
        return;
    for (i = 0; i < len; i++) {
        unsigned char c = samp_encr[p[i]];
        sum ^= p[i] & 0xAA;
        if (!((i + 1) & 1))
            c ^= cipher_port;
        e[i + 1] = c;
    }
    e[0] = sum;
    cb->send(e, len + 1);
}

static void write_acks(bs_t *b)
{
    unsigned short lo[MAX_ACKS], hi[MAX_ACKS];
    int n = 0, i;

    for (i = 0; i < nacks; i++) {
        if (n && acks[i] == (unsigned short)(hi[n - 1] + 1) && acks[i] > hi[n - 1]) {
            hi[n - 1] = acks[i];
        } else {
            lo[n] = hi[n] = acks[i];
            n++;
        }
    }
    bs_wcomp16(b, n);
    for (i = 0; i < n; i++) {
        bs_wbool(b, lo[i] == hi[i]);
        bs_w16(b, lo[i]);
        if (lo[i] != hi[i])
            bs_w16(b, hi[i]);
    }
    nacks = 0;
}

static void write_msg(bs_t *b, const out_t *m)
{
    unsigned char rel = m->rel;
    bs_w16(b, m->num);
    bs_wbits(b, &rel, 4, 1);
    if (m->rel == RAK_UNRELIABLE_SEQ || m->rel == RAK_RELIABLE_SEQ || m->rel == RAK_RELIABLE_ORDERED) {
        unsigned char ch = m->chan;
        bs_wbits(b, &ch, 5, 1);
        bs_w16(b, m->oidx);
    }
    bs_wbool(b, 0); /* not split: nothing we send exceeds the MTU */
    bs_wcomp16(b, m->bits);
    bs_walign(b);
    bs_wbytes(b, m->data, BS_BYTES(m->bits));
}

static void send_datagram(const out_t *m)
{
    static unsigned char buf[1600];
    bs_t b;
    bs_wr_init(&b, buf, sizeof buf);
    if (nacks) {
        bs_wbool(&b, 1);
        write_acks(&b);
    } else {
        bs_wbool(&b, 0);
    }
    if (m)
        write_msg(&b, m);
    if (!b.err)
        send_raw(buf, BS_BYTES(b.bits));
}

int rak_send(const void *data, int bits, int rel, int chan)
{
    static out_t tmp;
    out_t *m = &tmp;
    int i, reliable = rel == RAK_RELIABLE || rel == RAK_RELIABLE_ORDERED || rel == RAK_RELIABLE_SEQ;

    if (BS_BYTES(bits) > MAX_MSG || bits <= 0)
        return -1;
    if (reliable) {
        m = 0;
        for (i = 0; i < MAX_OUT; i++)
            if (!outq[i].used) { m = &outq[i]; break; }
        if (!m)
            return -2;
    }
    m->num = next_num++;
    m->rel = rel;
    m->chan = chan;
    m->bits = bits;
    m->oidx = 0;
    if (rel == RAK_RELIABLE_ORDERED) m->oidx = ord_out[chan]++;
    else if (rel == RAK_UNRELIABLE_SEQ || rel == RAK_RELIABLE_SEQ) m->oidx = seq_out[chan]++;
    memcpy(m->data, data, BS_BYTES(bits));
    m->last = now_ms;
    m->used = reliable;
    send_datagram(m);
    return 0;
}

int rak_rpc(int id, const void *data, int bits, int rel, int chan)
{
    static unsigned char buf[MAX_MSG];
    bs_t b;
    bs_wr_init(&b, buf, sizeof buf);
    bs_w8(&b, ID_RPC);
    bs_w8(&b, id);
    bs_wcomp32(&b, bits);
    if (bits > 0)
        bs_wbits(&b, data, bits, 0);
    if (b.err)
        return -1;
    return rak_send(buf, b.bits, rel, chan);
}

static void close_with(int newstate, int reason)
{
    if (state == RAK_FAILED || state == RAK_CLOSED)
        return;
    state = newstate;
    fail = reason;
    if (cb && cb->on_closed)
        cb->on_closed(reason);
}

static void send_open(unsigned cookie)
{
    unsigned char p[3] = {ID_OPEN_REQUEST, cookie, cookie >> 8};
    send_raw(p, 3);
    last_open = now_ms;
}

void rak_connect(unsigned ip, unsigned port, const char *pw, const rak_cb_t *c)
{
    unsigned i;
    cb = c;
    srv_ip = ip;
    srv_port = port;
    cipher_port = (unsigned char)port ^ 0xCC;
    memset(password, 0, sizeof password);
    for (i = 0; pw && pw[i] && i < sizeof password - 1; i++)
        password[i] = pw[i];
    next_num = 0;
    memset(ord_out, 0, sizeof ord_out);
    memset(seq_out, 0, sizeof seq_out);
    memset(ord_in, 0, sizeof ord_in);
    memset(seq_in, 0, sizeof seq_in);
    memset(seen, 0, sizeof seen);
    for (i = 0; i < MAX_OUT; i++) outq[i].used = 0;
    for (i = 0; i < SPLIT_SLOTS; i++) splits[i].used = 0;
    for (i = 0; i < HOLD_MAX; i++) holds[i].used = 0;
    hold_top = hold_count = hold_warned = nacks = 0;
    open_tries = 0;
    fail = RAK_FAIL_NONE;
    state = RAK_OPENING;
    last_rx = now_ms;
    send_open(0);
}

void rak_disconnect(void)
{
    if (state == RAK_CONNECTED) {
        unsigned char c = ID_DISCONNECT;
        rak_send(&c, 8, RAK_RELIABLE, 0);
    }
    close_with(RAK_CLOSED, RAK_FAIL_NONE);
}

int rak_state(void) { return state; }
int rak_fail_reason(void) { return fail; }
unsigned rak_ping(void) { return ping_ms; }

static void handle_auth(const unsigned char *d, unsigned bytes)
{
    unsigned char r[64];
    char answer[41];
    unsigned len, n;

    if (bytes < 2)
        return;
    len = d[1];
    if (len + 2 > bytes)
        return;
    while (len && d[2 + len - 1] == 0) /* the server includes the terminator */
        len--;
    if (samp_auth_answer(d + 2, len, answer) == 0) {
        n = 40;
        r[0] = ID_AUTH_KEY;
        r[1] = n;
        memcpy(r + 2, answer, n);
        rak_send(r, (n + 2) * 8, RAK_RELIABLE, 0);
        return;
    }
    slog("rak: unusable auth challenge, length %d", len);
    close_with(RAK_FAILED, RAK_FAIL_AUTH);
}

static void handle_accepted(const unsigned char *d, unsigned bytes)
{
    unsigned char p[7];
    unsigned token, idx, ip_be;

    if (bytes != 13)
        return;
    idx = d[7] | (d[8] << 8);
    token = d[9] | (d[10] << 8) | (d[11] << 16) | ((unsigned)d[12] << 24);
    ip_be = srv_ip;
    p[0] = ID_NEW_INCOMING;
    p[1] = ip_be >> 24; p[2] = ip_be >> 16; p[3] = ip_be >> 8; p[4] = ip_be;
    p[5] = srv_port; p[6] = srv_port >> 8;
    state = RAK_CONNECTED;
    rak_send(p, 7 * 8, RAK_RELIABLE, 0);
    last_ping = 0;
    if (cb->on_connected)
        cb->on_connected(token, idx);
}

static void deliver(const unsigned char *d, int bits)
{
    unsigned bytes = BS_BYTES(bits);
    if (bits < 8)
        return;
    switch (d[0]) {
    case ID_AUTH_KEY:
        state = RAK_AUTHING;
        handle_auth(d, bytes);
        return;
    case ID_ACCEPTED:
        handle_accepted(d, bytes);
        return;
    case ID_INTERNAL_PING: {
        unsigned char p[9];
        if (bytes < 5)
            return;
        p[0] = ID_CONNECTED_PONG;
        memcpy(p + 1, d + 1, 4);
        p[5] = now_ms; p[6] = now_ms >> 8; p[7] = now_ms >> 16; p[8] = now_ms >> 24;
        rak_send(p, 9 * 8, RAK_UNRELIABLE, 0);
        return;
    }
    case ID_CONNECTED_PONG:
        if (bytes >= 5) {
            unsigned t = d[1] | (d[2] << 8) | (d[3] << 16) | ((unsigned)d[4] << 24);
            ping_ms = now_ms - t;
        }
        return;
    case ID_DETECT_LOST:
        return;
    case ID_NO_FREE:      close_with(RAK_FAILED, RAK_FAIL_FULL); return;
    case ID_BANNED:       close_with(RAK_FAILED, RAK_FAIL_BANNED); return;
    case ID_BAD_PASSWORD: close_with(RAK_FAILED, RAK_FAIL_PASSWORD); return;
    case ID_DISCONNECT:   close_with(RAK_CLOSED, RAK_FAIL_KICKED); return;
    case ID_LOST:         close_with(RAK_CLOSED, RAK_FAIL_LOST); return;
    }
    if (cb->on_packet)
        cb->on_packet(d, bits);
}

/* Reliable-ordered messages are released strictly in index order. */
static int hold_home(unsigned chan, unsigned oidx)
{
    return (int)((oidx + (chan << 9)) & (HOLD_MAX - 1));
}

static int hold_find(unsigned chan, unsigned oidx)
{
    int i = hold_home(chan, oidx);
    while (holds[i].used) {
        if (holds[i].chan == chan && holds[i].oidx == (unsigned short)oidx)
            return i;
        i = (i + 1) & (HOLD_MAX - 1);
    }
    return -1;
}

/* Linear probing: pull later entries of the same run back over the hole. */
static void hold_del(int i)
{
    int j = i, k;
    holds[i].used = 0;
    hold_count--;
    for (;;) {
        j = (j + 1) & (HOLD_MAX - 1);
        if (!holds[j].used)
            return;
        k = hold_home(holds[j].chan, holds[j].oidx);
        if (i <= j ? (i < k && k <= j) : (i < k || k <= j))
            continue;
        holds[i] = holds[j];
        holds[j].used = 0;
        i = j;
    }
}

/* 0 if there is no room: the caller then leaves the message unacknowledged. */
static int hold_add(unsigned chan, unsigned oidx, const unsigned char *d, int bits)
{
    int i, n = BS_BYTES(bits);
    if (hold_find(chan, oidx) >= 0)
        return 1;
    if (hold_count >= HOLD_MAX / 4 * 3 || hold_top + n > HOLD_ARENA) {
        if (!hold_warned++)
            slog("rak: hold buffer full, leaving messages for the server to resend");
        return 0;
    }
    i = hold_home(chan, oidx);
    while (holds[i].used)
        i = (i + 1) & (HOLD_MAX - 1);
    holds[i].used = 1;
    holds[i].bits = bits;
    holds[i].off = hold_top;
    holds[i].chan = chan;
    holds[i].oidx = oidx;
    memcpy(hold_arena + hold_top, d, n);
    hold_top += n;
    hold_count++;
    return 1;
}

static void hold_drain(unsigned chan)
{
    int i, off, bits;
    while (hold_count && (state == RAK_AUTHING || state == RAK_CONNECTED) &&
           (i = hold_find(chan, ord_in[chan])) >= 0) {
        off = holds[i].off;
        bits = holds[i].bits;
        hold_del(i);
        ord_in[chan]++;
        deliver(hold_arena + off, bits);
    }
    if (!hold_count)
        hold_top = 0;
}

/* 0 if the message was not taken and must not be acknowledged. */
static int sequence(unsigned rel, unsigned chan, unsigned oidx, const unsigned char *d, int bits)
{
    if (rel == RAK_RELIABLE_ORDERED) {
        unsigned short ahead = (unsigned short)(oidx - ord_in[chan]);
        if (ahead == 0) {
            ord_in[chan]++;
            deliver(d, bits);
            hold_drain(chan);
        } else if (ahead < 0x8000) {
            return hold_add(chan, oidx, d, bits);
        }
    } else if (rel == RAK_RELIABLE_SEQ || rel == RAK_UNRELIABLE_SEQ) {
        unsigned short ahead = (unsigned short)(oidx - seq_in[chan]);
        if (ahead < 0x8000) {
            seq_in[chan] = oidx + 1;
            deliver(d, bits);
        }
    } else {
        deliver(d, bits);
    }
    return 1;
}

/* 0 if the part was not taken and must not be acknowledged. */
static int on_split(unsigned rel, unsigned chan, unsigned oidx, unsigned id, unsigned idx,
                     unsigned count, const unsigned char *d, int bits)
{
    split_t *s = 0;
    int i, total = 0, off = 0;

    if (count == 0 || count > SPLIT_MAX || idx >= count || BS_BYTES(bits) > SPLIT_STRIDE)
        return 1;
    for (i = 0; i < SPLIT_SLOTS; i++)
        if (splits[i].used && splits[i].id == id) s = &splits[i];
    if (!s) {
        for (i = 0; i < SPLIT_SLOTS; i++)
            if (!splits[i].used) { s = &splits[i]; break; }
        if (!s)
            return 0; /* every slot is mid-message: the server resends this part later */
        s->used = 1; s->id = id; s->count = count; s->got = 0;
        s->rel = rel; s->chan = chan; s->oidx = oidx;
        memset(s->bits, 0, sizeof s->bits);
    }
    if (s->bits[idx])
        return 1;
    s->bits[idx] = bits;
    memcpy(s->data + idx * SPLIT_STRIDE, d, BS_BYTES(bits));
    if (++s->got < s->count)
        return 1;
    for (i = 0; i < (int)s->count; i++) {
        memcpy(assembled + off, s->data + i * SPLIT_STRIDE, BS_BYTES(s->bits[i]));
        off += BS_BYTES(s->bits[i]);
        total += s->bits[i];
    }
    s->used = 0;
    /* Its parts are acknowledged already, so a whole message can't be handed back. */
    if (!sequence(s->rel, s->chan, s->oidx, assembled, total)) {
        slog("rak: no room for a split message, connection is out of sync");
        close_with(RAK_CLOSED, RAK_FAIL_LOST);
    }
    return 1;
}

static void handle_acks(bs_t *b)
{
    unsigned count = bs_rcomp16(b), i;
    for (i = 0; i < count && !b->err; i++) {
        int same = bs_rbool(b);
        unsigned lo = bs_r16(b), hi = same ? lo : bs_r16(b), n;
        int j;
        if (b->err || hi < lo)
            return;
        for (j = 0; j < MAX_OUT; j++) {
            n = outq[j].num;
            if (outq[j].used && n >= lo && n <= hi)
                outq[j].used = 0;
        }
    }
}

static int parse_msg(bs_t *b)
{
    static unsigned char data[1600];
    unsigned num, chan = 0, oidx = 0, split, sid = 0, sidx = 0, scnt = 0, bits, reliable, taken;
    int acked = 0;
    unsigned char rel = 0;

    if (bs_unread(b) < 16 + 4 + 1)
        return 0;
    num = bs_r16(b);
    bs_rbits(b, &rel, 4, 1);
    if (rel == RAK_UNRELIABLE_SEQ || rel == RAK_RELIABLE_SEQ || rel == RAK_RELIABLE_ORDERED) {
        unsigned char c = 0;
        bs_rbits(b, &c, 5, 1);
        chan = c;
        oidx = bs_r16(b);
    }
    split = bs_rbool(b);
    if (split) {
        sid = bs_r16(b);
        sidx = bs_rcomp32(b);
        scnt = bs_rcomp32(b);
    }
    bits = bs_rcomp16(b);
    bs_ralign(b);
    if (b->err || bits == 0 || BS_BYTES(bits) > sizeof data || chan >= NCHAN)
        return 0;
    if (!bs_rbytes(b, data, BS_BYTES(bits)))
        return 0;

    reliable = rel == RAK_RELIABLE || rel == RAK_RELIABLE_ORDERED || rel == RAK_RELIABLE_SEQ;
    if (reliable) {
        unsigned far = (num + 32768) & 0xFFFF;
        if (nacks < MAX_ACKS) {
            acks[nacks++] = num;
            acked = 1;
        }
        if (seen[num >> 3] & (1 << (num & 7)))
            return 1; /* duplicate: re-ack it, deliver nothing */
        seen[num >> 3] |= 1 << (num & 7);
        seen[far >> 3] &= ~(1 << (far & 7));
    }
    if (split)
        taken = on_split(rel, chan, oidx, sid, sidx, scnt, data, bits);
    else
        taken = sequence(rel, chan, oidx, data, bits);
    if (!taken && reliable) { /* as if it never arrived: the server will send it again */
        if (acked)
            nacks--;
        seen[num >> 3] &= ~(1 << (num & 7));
    }
    return 1;
}

void rak_datagram(const unsigned char *d, unsigned len, unsigned now)
{
    bs_t b;

    now_ms = now;
    last_rx = now;
    if (len == 0)
        return;
    /* Offline messages are too short to be reliability-layer datagrams. */
    if (len <= 3 && state >= RAK_OPENING && state <= RAK_AUTHING) {
        switch (d[0]) {
        case ID_OPEN_COOKIE:
            if (len == 3) {
                unsigned cookie = d[1] | (d[2] << 8);
                state = RAK_OPENED;
                send_open(cookie ^ COOKIE_XOR);
            }
            return;
        case ID_OPEN_REPLY: {
            unsigned char p[1 + sizeof password];
            unsigned n = strlen(password);
            if (state == RAK_AUTHING)
                return;
            state = RAK_AUTHING;
            p[0] = ID_CONNECTION_REQUEST;
            memcpy(p + 1, password, n);
            rak_send(p, (n + 1) * 8, RAK_RELIABLE, 0);
            return;
        }
        case ID_NO_FREE:      close_with(RAK_FAILED, RAK_FAIL_FULL); return;
        case ID_BANNED:       close_with(RAK_FAILED, RAK_FAIL_BANNED); return;
        case ID_BAD_PASSWORD: close_with(RAK_FAILED, RAK_FAIL_PASSWORD); return;
        }
    }
    if (state < RAK_AUTHING || state > RAK_CONNECTED)
        return;
    bs_rd_init(&b, d, len * 8);
    if (bs_rbool(&b))
        handle_acks(&b);
    while (!b.err && state <= RAK_CONNECTED && parse_msg(&b))
        ;
}

void rak_tick(unsigned now)
{
    int i;

    now_ms = now;
    if (state == RAK_OPENING || state == RAK_OPENED) {
        if (now - last_open >= OPEN_RETRY) {
            if (++open_tries >= OPEN_TRIES) {
                close_with(RAK_FAILED, RAK_FAIL_TIMEOUT);
                return;
            }
            state = RAK_OPENING;
            send_open(0);
        }
        return;
    }
    if (state != RAK_AUTHING && state != RAK_CONNECTED)
        return;
    if (now - last_rx >= TIMEOUT_MS) {
        close_with(state == RAK_CONNECTED ? RAK_CLOSED : RAK_FAILED,
                   state == RAK_CONNECTED ? RAK_FAIL_LOST : RAK_FAIL_TIMEOUT);
        return;
    }
    for (i = 0; i < MAX_OUT; i++) {
        if (outq[i].used && now - outq[i].last >= RESEND_MS) {
            outq[i].last = now;
            send_datagram(&outq[i]);
        }
    }
    if (nacks)
        send_datagram(0);
    if (state == RAK_CONNECTED && now - last_ping >= PING_MS) {
        unsigned char p[5] = {ID_INTERNAL_PING, now, now >> 8, now >> 16, now >> 24};
        last_ping = now;
        rak_send(p, 5 * 8, RAK_UNRELIABLE, 0);
    }
}
