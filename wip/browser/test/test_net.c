/* End to end. Built twice from this one file:
 *   RIG      net.c + fake SMAP + PCSX2's real TCP_Session/UDP/DNS code
 *   default  host_xport.c, real sockets, proving the injection point
 *
 * usage: test_net <server_ip> <server_port> <dns_ip> <http_port> <udp_base> <n_fake> [real-list]
 * UDP can only reach server_ip, loopback and the resolver: both builds enforce it.
 */
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "browser.h"

#ifdef RIG
#include "net.h"
#include "rig.h"
#include "../browser_ee.c" /* the real glue, for its transport table */
#else
#include "host_xport.h"
static browser_transport_t host_t;
#endif

#define LOOPBACK 0x7F000001u

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("  FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static const browser_transport_t *T;
static unsigned t0;

static unsigned ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned)t.tv_sec * 1000u + t.tv_nsec / 1000000u - t0;
}

#ifdef RIG
static void on_udp(unsigned ip, unsigned sport, unsigned dport, const unsigned char *d, unsigned n)
{
    if (dport == BROWSER_LOCAL_PORT)
        browser_on_udp(ip, sport, d, n, ms());
}
#endif

/* One game frame. tools/patch_ui2.py ticks the browser ahead of net_poll, while the
 * other order is the natural one, so the rig runs both (RIG_TICK_FIRST). */
static int tick_first;
static void frame(void)
{
    if (tick_first)
        browser_tick(ms());
#ifdef RIG
    net_poll();
#endif
    if (!tick_first)
        browser_tick(ms());
    usleep(33000);
}

static void raw_frame(void)
{
#ifdef RIG
    net_poll();
#endif
    if (T->poll)
        T->poll(T->ctx, ms());
    usleep(33000);
}

static void wait_frames(int n) { while (n--) frame(); }

#ifdef RIG
static void expect_clean(const char *what)
{
    int i;
    for (i = 0; i < 60 && rig_tcp_sessions(); i++)
        raw_frame();
    if (rig_tcp_sessions())
        printf("  FAIL %s left %d TCP sessions in the adapter\n", what, rig_tcp_sessions());
    checks++;
    fails += rig_tcp_sessions() != 0;
}
#else
#define expect_clean(x)
#endif

/* ---- HTTP through the injected TCP ---- */

static unsigned long long fnv;
static unsigned got;
static int hash_sink(void *u, const unsigned char *d, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        fnv = (fnv ^ d[i]) * 0x100000001B3ull;
    got += n;
    return 0;
}

static http_t h;
static int read_every = 1; /* frames between reads, to play a reader slower than the link */

static http_state_t fetch(unsigned port, const char *path, unsigned idle_ms)
{
    int f = 0;

    fnv = 0xCBF29CE484222325ull;
    got = 0;
    http_start(&h, &T->tcp, LOOPBACK, port, "fixture.test", path, hash_sink, 0, ms());
    if (idle_ms)
        h.idle_ms = idle_ms;
    while (h.state != HTTP_DONE && h.state != HTTP_FAILED) {
        raw_frame();
        if (++f % read_every == 0)
            http_tick(&h, ms());
    }
    return h.state;
}

static void test_http(unsigned port, unsigned long long want_big, unsigned big_size)
{
    char path[64];
    unsigned start;
#ifdef RIG
    int errors = rig.adapter_errors;
#endif

    printf("http:\n");
    CHECK(fetch(port, "/list", 0) == HTTP_DONE);
    printf("  /list: %u bytes, no Content-Length, ended by FIN\n", got);
    CHECK(got > 1000);
    expect_clean("/list");
    CHECK(fetch(port, "/list-cl", 0) == HTTP_DONE);
    expect_clean("/list-cl");
    CHECK(fetch(port, "/slowdrip", 0) == HTTP_DONE);
    expect_clean("/slowdrip");
    CHECK(fetch(port, "/longheader", 0) == HTTP_DONE && got == 13);
    expect_clean("/longheader");

    snprintf(path, sizeof path, "/big/%u", big_size);
    start = ms();
    CHECK(fetch(port, path, 0) == HTTP_DONE);
    printf("  %s: %u bytes in %u ms, hash %s\n", path, got, ms() - start,
           fnv == want_big ? "matches" : "DIFFERS");
    CHECK(got == big_size && fnv == want_big);
#ifdef RIG
    /* The reader is throttled below the link speed, so this must have closed the
     * window and reopened it, or this test proves nothing about that path. */
    printf("  window closed %d times during that transfer\n", rig.zero_window_acks);
#endif
    expect_clean(path);
    snprintf(path, sizeof path, "/bigcl/%u", big_size);
    CHECK(fetch(port, path, 0) == HTTP_DONE);
    CHECK(got == big_size && fnv == want_big);
    expect_clean(path);
#ifdef RIG
    /* Every path above is a normal transfer, so the adapter must have nothing to say. */
    printf("  adapter errors over all clean transfers: %d\n", rig.adapter_errors - errors);
    CHECK(rig.adapter_errors == errors);
#endif

    CHECK(fetch(port, "/404", 0) == HTTP_FAILED && h.error == HTTP_E_STATUS && h.status == 404);
    expect_clean("/404");
    CHECK(fetch(port, "/301", 0) == HTTP_FAILED && h.error == HTTP_E_STATUS && h.status == 301);
    expect_clean("/301");
    CHECK(fetch(port, "/chunked", 0) == HTTP_FAILED && h.error == HTTP_E_CHUNKED);
    expect_clean("/chunked");
    CHECK(fetch(port, "/shortcl", 0) == HTTP_FAILED && h.error == HTTP_E_TRUNCATED);
    expect_clean("/shortcl");
    CHECK(fetch(port, "/notfhttp", 0) == HTTP_FAILED && h.error == HTTP_E_BAD_RESPONSE);
    expect_clean("/notfhttp");
    CHECK(fetch(port, "/rstmid", 0) == HTTP_FAILED && h.error == HTTP_E_RESET);
    printf("  /rstmid: failed with error %d after %u body bytes\n", h.error, got);
    expect_clean("/rstmid");
    start = ms();
    CHECK(fetch(port, "/hang", 1500) == HTTP_FAILED && h.error == HTTP_E_TIMEOUT);
    printf("  /hang: timed out after %u ms\n", ms() - start);
    expect_clean("/hang");
    /* Nothing listens here. */
    start = ms();
    CHECK(fetch(port + 1, "/", 0) == HTTP_FAILED);
    printf("  closed port: failed with error %d after %u ms\n", h.error, ms() - start);
    expect_clean("closed port");
    /* A host that never answers: repeated SYNs, then the connect timeout. */
    {
#ifdef RIG
        int errors2 = rig.adapter_errors;
#endif
        start = ms();
        CHECK(fetch(port + 2, "/", 0) == HTTP_FAILED && h.error == HTTP_E_CONNECT);
        printf("  no answer: failed with error %d after %u ms\n", h.error, ms() - start);
        CHECK(ms() - start >= 9000 && ms() - start < 12000);
        expect_clean("no answer");
#ifdef RIG
        CHECK(rig.adapter_errors == errors2);
#endif
    }
}

/* ---- the adapter's quirks, tried for real ---- */

#ifdef RIG
static void test_adapter(unsigned port, unsigned long long want_big, unsigned big_size)
{
    char path[64];
    int errors;

    printf("adapter behaviour:\n");
    /* A reader far slower than the link. The ring fills and the window shuts again
     * and again; the adapter never probes, so only our update can restart it. */
    {
        int before = rig.zero_window_acks;
        errors = rig.adapter_errors;
        read_every = 4;
        snprintf(path, sizeof path, "/big/%u", big_size);
        CHECK(fetch(port, path, 0) == HTTP_DONE);
        printf("  slow reader: window closed %d times, %u bytes, hash %s\n",
               rig.zero_window_acks - before, got, fnv == want_big ? "matches" : "DIFFERS");
        CHECK(got == big_size && fnv == want_big);
        CHECK(rig.zero_window_acks - before >= 10);
        expect_clean("slow reader");

        /* Exactly two ring-fulls: the FIN sits behind a closed window, and the adapter
         * cannot even see the server's close until the window opens. */
        before = rig.zero_window_acks;
        CHECK(fetch(port, "/big/16384", 0) == HTTP_DONE);
        printf("  FIN behind a closed window: %u bytes, window closed %d times\n",
               got, rig.zero_window_acks - before);
        CHECK(got == 16384 && rig.zero_window_acks - before >= 1);
        read_every = 1;
        expect_clean("fin behind window");
        CHECK(rig.adapter_errors == errors);
    }

    /* smap_send refusing the SYN must only delay things. */
    rig.refuse_tx = 3;
    CHECK(fetch(port, "/list", 0) == HTTP_DONE);
    printf("  smap_send refused %d SYNs: transfer still completed\n", rig.tx_refused);
    CHECK(rig.tx_refused == 3 && got > 1000);
    expect_clean("refused tx");

    /* The handshake ACK refused while the request is ready to go. The ACK has to
     * stay ahead: the adapter ignores the payload of the first packet after its
     * SYN-ACK, so a request sent first would vanish and the fetch would hang. */
    rig.refuse_bare_acks = 3;
    CHECK(fetch(port, "/list", 0) == HTTP_DONE);
    printf("  handshake ACK refused 3 times: request held back, %u bytes received\n", got);
    CHECK(rig.refuse_bare_acks == 0 && got > 1000);
    expect_clean("refused ack");

    /* A frame lost on the way in can never be recovered, so it must fail fast. */
    snprintf(path, sizeof path, "/big/%u", big_size);
    rig_drop_next_tcp_data();
    CHECK(fetch(port, path, 0) == HTTP_FAILED && h.error == HTTP_E_RESET);
    CHECK(net_tcp_status() == NET_TCP_E_CLOSED);
    printf("  dropped segment: stream aborted with error %d, %u bytes in\n", h.error, got);
    expect_clean("dropped segment");

    /* A one-segment response is the awkward case: with no later data to show the
     * hole, only the FIN can reveal it. /empty is a single small segment. */
    rig_drop_next_tcp_data();
    {
        unsigned start = ms();
        CHECK(fetch(port, "/empty", 0) == HTTP_FAILED);
        printf("  dropped only segment: failed with error %d in %u ms\n", h.error, ms() - start);
        CHECK(ms() - start < 2000);
    }
    expect_clean("dropped last segment");

    /* Why we never retransmit data: repeat the request of a live connection. */
    {
        static unsigned char junk[512];
        int st, i;
        errors = rig.adapter_errors;
        net_tcp_open(LOOPBACK, port, ms());
        for (i = 0; i < 90 && net_tcp_status() == NET_TCP_CONNECTING; i++) {
            raw_frame();
            net_tcp_tick(ms());
        }
        CHECK(net_tcp_status() == NET_TCP_OPEN);
        CHECK(net_tcp_send("GET /hang HTTP/1.0\r\nHost: x\r\n\r\n", 31) == 31);
        for (i = 0; i < 5; i++) {
            raw_frame();
            net_tcp_tick(ms());
        }
        rig_replay_last_tcp_data();
        for (i = 0; i < 10; i++) {
            raw_frame();
            net_tcp_tick(ms());
        }
        st = net_tcp_status();
        printf("  retransmitted data: status %d, adapter logged %d error(s)\n",
               st, rig.adapter_errors - errors);
        CHECK(st == NET_TCP_E_RESET);
        net_tcp_recv(junk, sizeof junk);
        net_tcp_close();
        rig.adapter_errors = errors; /* those were provoked on purpose */
        expect_clean("replay");
    }
}
#endif

/* ---- the browser API ---- */

static browser_list_state_t run_list(void)
{
    int guard = 1500;
    CHECK(browser_refresh_internet() == 0);
    while (browser_list_state() != BROWSER_LIST_DONE &&
           browser_list_state() != BROWSER_LIST_FAILED && guard--)
        frame();
    if (browser_list_state() != BROWSER_LIST_DONE)
        printf("  list: state %d error %d http error %d status %d\n", browser_list_state(),
               browser_list_error(), browser_list_http_error(), browser_list_http_status());
    return browser_list_state();
}

static void cfg_list(browser_config_t *c, unsigned port, const char *path, int allow)
{
    memset(c, 0, sizeof *c);
    c->list_host = "fixture.test";
    c->list_path = path;
    c->list_port = port;
    c->list_ip = LOOPBACK;
    c->allow_private_list = allow;
}

static void test_list_edge(unsigned port, unsigned udp_base)
{
    browser_config_t c;
    int k;

    printf("list parsing:\n");
    cfg_list(&c, port, "/messy", 1);
    c.no_auto_query = 1;
    browser_init(T, &c);
    CHECK(run_list() == BROWSER_LIST_DONE);
    CHECK(browser_count() == 2);
    CHECK(browser_find(LOOPBACK, udp_base) >= 0 && browser_find(LOOPBACK, udp_base + 1) >= 0);
    printf("  /messy: %d valid entries kept from CRLF, junk and an unterminated line\n", browser_count());

    cfg_list(&c, port, "/private", 0);
    c.no_auto_query = 1;
    browser_init(T, &c);
    CHECK(run_list() == BROWSER_LIST_DONE);
    CHECK(browser_count() == 1 && browser_get(0)->ip == 0x08080808);
    printf("  /private: only the public address kept (%d of 7)\n", browser_count());

    /* A failed or empty fetch must leave the current list alone. */
    cfg_list(&c, port, "/html", 1);
    browser_init(T, &c);
    browser_fav_import("127.0.0.1:7\n", 12);
    k = browser_count();
    CHECK(run_list() == BROWSER_LIST_FAILED && browser_list_error() == BROWSER_LE_EMPTY);
    CHECK(browser_count() == k);

    cfg_list(&c, port, "/rstmid", 1);
    browser_init(T, &c);
    browser_fav_import("127.0.0.1:7\n", 12);
    CHECK(run_list() == BROWSER_LIST_FAILED && browser_list_error() == BROWSER_LE_HTTP);
    CHECK(browser_list_http_error() == HTTP_E_RESET);
    CHECK(browser_count() == 1);
    printf("  /html and /rstmid: refused, existing entries kept\n");
    expect_clean("list edge");
}

/* Mirrors behaviour() in fixture_servers.py */
static const char *fake_kind(int k)
{
    if (k % 11 == 3) return "silent";
    if (k % 11 == 5) return "garbage";
    if (k % 7 == 2) return "lossy";
    if (k % 13 == 6) return "noping";
    return "ok";
}

static void test_many(unsigned port, unsigned udp_base, int n)
{
    browser_config_t c;
    int k, online = 0, offline = 0, noping = 0, guard = 3000, kept;
    unsigned start, ping_max = 0;

    printf("many servers (%d fixtures, some lossy, silent or malformed):\n", n);
    cfg_list(&c, port, "/list", 1);
    browser_init(T, &c);
    browser_fav_import("127.0.0.1:19\n", 13);
    k = browser_find(LOOPBACK, 19);
    CHECK(k == 0);
    CHECK(run_list() == BROWSER_LIST_DONE);
    CHECK(browser_list_http_status() == 200);
    CHECK(browser_count() == n + 1);
    start = ms();
    while (browser_pending() && guard--)
        frame();
    CHECK(browser_pending() == 0);
    printf("  whole list finished in %u ms\n", ms() - start);

    for (k = 0; k < n; k++) {
        int i = browser_find(LOOPBACK, udp_base + k);
        const browser_server_t *e = browser_get(i);
        const char *kind = fake_kind(k);
        char want[32];
        CHECK(e != 0);
        if (!e)
            continue;
        if (!strcmp(kind, "silent") || !strcmp(kind, "garbage")) {
            if (e->state != BROWSER_ST_OFFLINE) {
                printf("  server %d (%s) state %d\n", k, kind, e->state);
                fails++;
            }
            offline++;
            continue;
        }
        snprintf(want, sizeof want, "Fixture %d", k);
        if (e->state != BROWSER_ST_ONLINE || strcmp(e->info.hostname, want) ||
            e->info.players != (k == 8 ? 64 : k == 10 ? 65 : k % 50) ||
            e->info.max_players != (k == 8 || k == 10 ? 100 : 50) ||
            e->info.password != k % 2 || strcmp(e->info.gamemode, "test mode") ||
            strcmp(e->info.language, "English")) {
            printf("  server %d (%s): state %d host \"%s\"\n", k, kind, e->state, e->info.hostname);
            fails++;
        }
        checks++;
        online++;
        if (!strcmp(kind, "noping")) {
            CHECK(e->ping_ms == BROWSER_PING_NONE);
            noping++;
        } else {
            CHECK(e->ping_ms != BROWSER_PING_NONE);
            if (e->ping_ms > ping_max)
                ping_max = e->ping_ms;
        }
    }
    printf("  online %d (of which %d without ping), offline %d, largest ping %u ms\n",
           online, noping, offline, ping_max);

    /* Favourites survive a refresh, and everything renumbers cleanly. */
    k = browser_find(LOOPBACK, 19);
    CHECK(k >= 0 && (browser_get(k)->flags & BROWSER_F_FAVOURITE));
    CHECK(browser_get(k)->state == BROWSER_ST_OFFLINE);
    kept = browser_count();
    CHECK(run_list() == BROWSER_LIST_DONE);
    CHECK(browser_count() == kept && browser_find(LOOPBACK, 19) >= 0);

    /* Rules and players, for a fixture server. */
    guard = 2000;
    while (browser_pending() && guard--)
        frame();
    k = browser_find(LOOPBACK, udp_base + 1);
    browser_select(k);
    for (guard = 200; guard--; ) {
        const browser_detail_t *d = browser_detail();
        if (d->rules_state >= BROWSER_PART_READY && d->players_state >= BROWSER_PART_READY)
            break;
        frame();
    }
    {
        const browser_detail_t *d = browser_detail();
        CHECK(d->rules_state == BROWSER_PART_READY && d->players_state == BROWSER_PART_READY);
        CHECK(d->rule_count == 2 && !strcmp(d->rules[1].value, "SA"));
        CHECK(d->player_count == 1 && d->players[0].score == -5);
    }

    /* The largest 'c' we ask for: over the MTU, so it needs the larger rxbuf. */
    browser_select(browser_find(LOOPBACK, udp_base + 8));
    for (guard = 200; guard-- && browser_detail()->players_state < BROWSER_PART_READY; )
        frame();
    {
        const browser_detail_t *d = browser_detail();
        CHECK(d->players_state == BROWSER_PART_READY && d->player_count == 64);
        CHECK(!strcmp(d->players[63].name, "Player_00000000000000063") && d->players[63].score == 63);
        printf("  64 player reply: state %d, %u players, last \"%s\"\n", d->players_state,
               d->player_count, d->players[63].name);
    }
    /* One more player and it must not be requested at all. */
    browser_select(browser_find(LOOPBACK, udp_base + 10));
    for (guard = 200; guard-- && browser_detail()->players_state < BROWSER_PART_READY; )
        frame();
    CHECK(browser_detail()->players_state == BROWSER_PART_UNAVAILABLE);
    wait_frames(45);
    CHECK(browser_detail()->rules_state == BROWSER_PART_READY);
    printf("  65 players: list not requested, state %d\n", browser_detail()->players_state);
    expect_clean("many");
}

static void test_real_server(unsigned ip, unsigned port)
{
    char a[32];
    const browser_server_t *e;
    const browser_detail_t *d;
    int k, guard, i;
    unsigned b;

    printf("real server:\n");
    browser_init(T, 0);
    snprintf(a, sizeof a, "%u.%u.%u.%u:%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255, port);
    k = browser_add_ip(a);
    CHECK(k == 0);
    browser_select(k);
    for (guard = 200; guard--; ) {
        d = browser_detail();
        e = browser_get(k);
        if (e->state >= BROWSER_ST_ONLINE && d->rules_state >= BROWSER_PART_READY &&
            d->players_state >= BROWSER_PART_READY)
            break;
        frame();
    }
    e = browser_get(k);
    d = browser_detail();
    printf("  %s: state %d \"%s\" mode \"%s\" lang \"%s\" %d/%d pw=%d ping=%u ms\n", a, e->state,
           e->info.hostname, e->info.gamemode, e->info.language, e->info.players,
           e->info.max_players, e->info.password, e->ping_ms);
    printf("  rules(%u):", d->rule_count);
    for (b = 0; b < d->rule_count; b++)
        printf(" %s=%s;", d->rules[b].name, d->rules[b].value);
    printf("\n  players(%u):", d->player_count);
    for (i = 0; i < (int)d->player_count; i++)
        printf(" %s(%d)", d->players[i].name, d->players[i].score);
    putchar('\n');
    CHECK(e->state == BROWSER_ST_ONLINE && e->ping_ms != BROWSER_PING_NONE);
    CHECK(e->info.max_players > 0 && e->info.hostname[0]);
    CHECK(d->rules_state == BROWSER_PART_READY && d->rule_count > 0);
    CHECK(d->players_state == BROWSER_PART_READY);
}

static void test_dns(void)
{
    int guard, r;

    printf("dns:\n");
    browser_init(T, 0);
    r = browser_add_ip("localhost:19");
    CHECK(r == BROWSER_ADD_RESOLVING);
    for (guard = 300; browser_add_state() == 1 && guard--; )
        frame();
    CHECK(browser_add_state() == 2);
    if (browser_count())
        printf("  localhost resolved to %08x\n", browser_get(0)->ip);
    CHECK(browser_count() == 1 && browser_get(0)->ip == LOOPBACK && browser_get(0)->port == 19);

    r = browser_add_ip("no-such-host.invalid");
    CHECK(r == BROWSER_ADD_RESOLVING);
    for (guard = 500; browser_add_state() == 1 && guard--; )
        frame();
    CHECK(browser_add_state() == -1);
    printf("  no-such-host.invalid: failed as it should\n");
}

/* The one fetch from the real list host: no queries follow it. */
static void test_real_list(void)
{
    browser_config_t c;
    int blocked;

    printf("real list (DNS + HTTP from %s, no queries sent):\n", BROWSER_LIST_HOST);
    memset(&c, 0, sizeof c);
    c.no_auto_query = 1;
    browser_init(T, &c);
#ifdef RIG
    blocked = rig.udp_blocked;
#else
    blocked = host_udp_blocked;
#endif
    {
        browser_list_state_t st = run_list();
        printf("  state %d error %d http error %d status %d: %d servers\n", st, browser_list_error(),
               browser_list_http_error(), browser_list_http_status(), browser_count());
        CHECK(st == BROWSER_LIST_DONE);
    }
    CHECK(browser_count() > 100);
    wait_frames(30);
    CHECK(browser_pending() == 0);
#ifdef RIG
    CHECK(rig.udp_blocked == blocked);
#else
    CHECK(host_udp_blocked == blocked);
#endif
    expect_clean("real list");
}

int main(int argc, char **argv)
{
    unsigned server, dns, sport, hport, ubase, big = 200000;
    int n;
    unsigned long long want_big;

    if (argc < 8) {
        fprintf(stderr, "usage: %s server_ip server_port dns_ip http_port udp_base n big_hash [real-list]\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, 0, _IOLBF, 0);
    server = ntohl(inet_addr(argv[1]));
    sport = atoi(argv[2]);
    dns = ntohl(inet_addr(argv[3]));
    hport = atoi(argv[4]);
    ubase = atoi(argv[5]);
    n = atoi(argv[6]);
    want_big = strtoull(argv[7], 0, 16);
    t0 = 0;
    t0 = ms() - 1000;
    tick_first = getenv("RIG_TICK_FIRST") != 0;

#ifdef RIG
    printf("=== RIG: net.c + real PCSX2 TCP_Session/UDP/DNS, %s first ===\n",
           tick_first ? "browser_tick" : "net_poll");
    rig_start(1);
    rig_allow_udp(server);
    rig_allow_udp(LOOPBACK);
    net_init(on_udp);
    T = &ee_transport;
    (void)dns;
#else
    printf("=== HOST: real sockets through the injected transport ===\n");
    host_xport_init(&host_t, dns);
    host_allow_udp(server);
    host_allow_udp(LOOPBACK);
    host_allow_udp(dns);
    T = &host_t;
#endif

    test_real_server(server, sport);
    test_http(hport, want_big, big);
#ifdef RIG
    test_adapter(hport, want_big, big);
#endif
    test_list_edge(hport, ubase);
    test_many(hport, ubase, n);
    test_dns();
    if (argc > 8 && !strcmp(argv[8], "real-list"))
        test_real_list();

#ifdef RIG
    printf("rig: adapter errors %d, bad IP csum %d, bad TCP/UDP csum %d, data segments %d, udp sent %d, "
           "udp blocked %d, dns %d, FIFO peak %lu frames, NetPacket overflows %d, udp sessions open %d\n",
           rig.adapter_errors, rig.bad_ip_csum, rig.bad_l4_csum, rig.tcp_data_segments, rig.udp_sent,
           rig.udp_blocked, rig.dns_queries, rig.fifo_peak_frames, rig.netpacket_overflows, rig_udp_sessions());
    CHECK(rig.bad_ip_csum == 0 && rig.bad_l4_csum == 0 && rig.netpacket_overflows == 0);
    CHECK(rig.udp_blocked == 0);
#else
    printf("host: udp blocked %d\n", host_udp_blocked);
    CHECK(host_udp_blocked == 0);
#endif
    printf("%d checks, %d failed\n", checks, fails);
    fflush(stdout);
    _exit(fails != 0);
}
