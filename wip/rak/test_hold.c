/* Host test for rak.c's receive side under loss: plays a server that resends whatever the
 * client has not acknowledged, and checks every message comes out once, in order, with no
 * disconnect. Build: wip/rak/run_tests.sh. RAK_C picks the rak.c under test. */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include RAK_C

void slog(const char *f, ...)
{
    va_list a;
    va_start(a, f);
    printf("  [client] ");
    vprintf(f, a);
    printf("\n");
    va_end(a);
}

#define MAXU 20000
typedef struct { unsigned num, oidx, split, sid, sidx, scnt, bytes; unsigned char data[120]; } unit_t;
static unit_t units[MAXU];
static int nunits, acked[MAXU], closed;
static unsigned got[MAXU];
static int ngot, order_bad;
static unsigned rng = 1;
static unsigned rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static void on_packet(const unsigned char *d, unsigned bits)
{
    unsigned v;
    if (d[0] != 0xC8)
        return;
    v = d[1] | (d[2] << 8) | (d[3] << 16);
    if (ngot < MAXU)
        got[ngot] = v;
    if (v != (unsigned)ngot)
        order_bad++;
    ngot++;
}
static void on_closed(int r) { closed = 1; }
static void snd(const unsigned char *d, unsigned len) {}
static const rak_cb_t cbs = {snd, 0, on_packet, on_closed};

static void reset(void)
{
    rak_connect(0x7F000001, 7777, "", &cbs);
    state = RAK_CONNECTED;
    nunits = ngot = order_bad = closed = 0;
    memset(acked, 0, sizeof acked);
}

/* nmsg messages; each split into parts pieces (1 = not split). */
static void make(int nmsg, int parts)
{
    int m, p, num = 0;
    for (m = 0; m < nmsg; m++)
        for (p = 0; p < parts; p++) {
            unit_t *u = &units[nunits++];
            u->num = num++ & 0xFFFF;
            u->oidx = m & 0xFFFF;
            u->split = parts > 1;
            u->sid = m;
            u->sidx = p;
            u->scnt = parts;
            u->bytes = 100;
            memset(u->data, 0x55, sizeof u->data);
            if (p == 0) {
                u->data[0] = 0xC8;
                u->data[1] = m; u->data[2] = m >> 8; u->data[3] = m >> 16;
            }
        }
}

static void put(bs_t *b, const unit_t *u)
{
    unsigned char rel = RAK_RELIABLE_ORDERED, ch = 0;
    bs_w16(b, u->num);
    bs_wbits(b, &rel, 4, 1);
    bs_wbits(b, &ch, 5, 1);
    bs_w16(b, u->oidx);
    bs_wbool(b, u->split);
    if (u->split) {
        bs_w16(b, u->sid);
        bs_wcomp32(b, u->sidx);
        bs_wcomp32(b, u->scnt);
    }
    bs_wcomp16(b, u->bytes * 8);
    bs_walign(b);
    bs_wbytes(b, u->data, u->bytes);
}

static void harvest(void)
{
    int i, j;
    for (i = 0; i < nacks; i++)
        for (j = 0; j < nunits; j++)
            if (units[j].num == acks[i]) acked[j] = 1;
    nacks = 0;
}

/* Sends the units in list (count n) in that order, up to 4 per datagram. */
static void send_list(const int *list, int n, int dup_pct)
{
    static unsigned char buf[1600];
    int i = 0;
    while (i < n && !closed) {
        bs_t b;
        int k;
        bs_wr_init(&b, buf, sizeof buf);
        bs_wbool(&b, 0);
        for (k = 0; k < 4 && i < n; k++, i++) {
            put(&b, &units[list[i]]);
            if (dup_pct && (int)(rnd() % 100) < dup_pct)
                put(&b, &units[list[i]]);
        }
        rak_datagram(buf, BS_BYTES(b.bits), 0);
        harvest();
    }
}

static int pending(int *list, int loss_pct, int shuffle)
{
    int i, n = 0;
    for (i = 0; i < nunits; i++)
        if (!acked[i] && (int)(rnd() % 100) >= loss_pct)
            list[n++] = i;
    if (shuffle)
        for (i = n - 1; i > 0; i--) {
            int j = rnd() % (i + 1), t = list[i];
            list[i] = list[j]; list[j] = t;
        }
    return n;
}

static int fails;
static void check(const char *name, int nmsg, int rounds)
{
    int ok = !closed && ngot == nmsg && !order_bad;
    printf("%-44s %s  delivered %d/%d, out of order %d, closed %d, rounds %d\n",
           name, ok ? "ok  " : "FAIL", ngot, nmsg, order_bad, closed, rounds);
    if (!ok)
        fails++;
}

static int run_rounds(int nmsg, int loss, int dup, int shuffle, int *list)
{
    int r, n;
    for (r = 1; r < 400 && !closed && ngot < nmsg; r++) {
        n = pending(list, loss, shuffle);
        send_list(list, n, dup);
    }
    return r;
}

int main(void)
{
    static int list[MAXU];
    int i, n, seed, r;

    /* Valrise: a big burst on connect, and the first message is lost on the way. */
    reset(); make(5000, 1);
    for (i = 1, n = 0; i < nunits; i++) list[n++] = i;
    send_list(list, n, 0);
    printf("  after the burst: %d of %d acknowledged, holding %d\n", nacks + 0, nunits, hold_count);
    r = run_rounds(5000, 0, 0, 0, list);
    check("burst of 5000, first one lost", 5000, r + 1);

    /* Random loss, duplicates and reordering, many times. */
    for (seed = 1; seed <= 20; seed++) {
        char name[64];
        rng = seed;
        reset(); make(3000, 1);
        r = run_rounds(3000, 30, 10, 1, list);
        sprintf(name, "3000, 30%% loss, dups, shuffled, seed %d", seed);
        check(name, 3000, r);
    }

    /* Split messages sent interleaved, more of them than there are split slots. */
    reset(); make(40, 6);
    for (i = 0, n = 0; i < 6; i++) {
        int m;
        for (m = 0; m < 40; m++) list[n++] = m * 6 + i;
    }
    send_list(list, n, 0);
    r = run_rounds(40, 0, 0, 0, list);
    check("40 split messages of 6 parts, interleaved", 40, r + 1);

    for (seed = 1; seed <= 10; seed++) {
        char name[64];
        rng = 100 + seed;
        reset(); make(300, 5);
        r = run_rounds(300, 25, 10, 1, list);
        sprintf(name, "300 split x5, 25%% loss, shuffled, seed %d", seed);
        check(name, 300, r);
    }
    printf("%d failed\n", fails);
    return fails != 0;
}
