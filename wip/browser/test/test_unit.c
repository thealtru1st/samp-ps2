/* Parsers and state machines with no network. Run under ASan and UBSan. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "browser.h"
#include "dnsq.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static unsigned load(const char *dir, const char *name, unsigned char *b, unsigned max)
{
    char p[300];
    FILE *f;
    unsigned n;
    snprintf(p, sizeof p, "%s/%s", dir, name);
    if (!(f = fopen(p, "rb")))
        return 0;
    n = fread(b, 1, max, f);
    fclose(f);
    return n;
}

/* Every read in the parsers must stay inside len. Copying to an exact-size heap
 * block makes ASan catch a single byte over. */
static void fuzz_one(const unsigned char *d, unsigned n)
{
    static sampq_rule_t rules[8];
    static sampq_player_t players[8];
    sampq_info_t info;
    unsigned char *h = malloc(n ? n : 1), nonce[4] = {0};
    unsigned a, b, ip;

    memcpy(h, d, n);
    sampq_check(h, n, 0x7F000001, 7777);
    if (n >= SAMPQ_HDR) {
        sampq_parse_info(h, n, &info);
        sampq_parse_rules(h, n, rules, 8, &a, &b);
        sampq_parse_players(h, n, players, 8, &a, &b);
        sampq_parse_ping(h, n, nonce);
    }
    dnsq_parse(h, n, (n >= 2) ? (h[0] << 8 | h[1]) : 0, &ip);
    free(h);
}

static void fuzz(const unsigned char *seed, unsigned n, unsigned rounds)
{
    static unsigned char m[2048];
    unsigned r, i;

    for (i = 0; i <= n; i++)
        fuzz_one(seed, i); /* every truncation */
    for (r = 0; r < rounds; r++) {
        unsigned flips = 1 + rand() % 6, len = n;
        memcpy(m, seed, n);
        for (i = 0; i < flips && n; i++)
            m[rand() % n] = (rand() & 1) ? 0xFF : rand();
        if (rand() % 3 == 0 && n)
            len = rand() % (n + 1);
        fuzz_one(m, len);
    }
}

static void test_build(void)
{
    unsigned char p[SAMPQ_PING_LEN], nonce[4] = {1, 2, 3, 4};
    static const unsigned char want[] = {'S','A','M','P',192,168,1,2,0x61,0x1E,'i'};
    CHECK(sampq_build(p, 0xC0A80102, 7777, 'i', 0) == 11);
    CHECK(!memcmp(p, want, 11));
    CHECK(sampq_build(p, 0xC0A80102, 7777, 'p', nonce) == 15);
    CHECK(p[10] == 'p' && p[14] == 4);
    CHECK(sampq_check(p, 15, 0xC0A80102, 7777) == 'p');
    CHECK(sampq_check(p, 15, 0xC0A80103, 7777) == -1);
    CHECK(sampq_check(p, 15, 0xC0A80102, 7778) == -1);
    CHECK(sampq_check(p, 10, 0xC0A80102, 7777) == -1);
    CHECK(!sampq_parse_ping(p, 15, nonce));
    nonce[0] = 9;
    CHECK(sampq_parse_ping(p, 15, nonce) == -1);
}

/* Canned: what stock 0.3.7 sends, which we have no live server for. */
static void test_canned(void)
{
    static const unsigned char info[] =
        "SAMP\x7f\x00\x00\x01\x61\x1ei" "\x01" "\x0a\x00" "\xf4\x01"
        "\x05\x00\x00\x00" "He\x01lo" "\x02\x00\x00\x00" "gm" "\x00\x00\x00\x00";
    static const unsigned char rules[] =
        "SAMP\x7f\x00\x00\x01\x61\x1er" "\x02\x00"
        "\x07version\x06" "0.3.7\x7f" "\x04time\x00";
    static const unsigned char players[] =
        "SAMP\x7f\x00\x00\x01\x61\x1e" "c" "\x02\x00"
        "\x03" "Bob" "\xff\xff\xff\xff" "\x00" "\x05\x00\x00\x00";
    sampq_info_t o;
    sampq_rule_t r[2];
    sampq_player_t pl[1];
    unsigned st, tot;

    CHECK(!sampq_parse_info(info, sizeof info - 1, &o));
    CHECK(o.password == 1 && o.players == 10 && o.max_players == 500);
    CHECK(!strcmp(o.hostname, "He?lo")); /* control byte cleaned */
    CHECK(!strcmp(o.gamemode, "gm") && o.language[0] == 0);

    CHECK(!sampq_parse_rules(rules, sizeof rules - 1, r, 2, &st, &tot));
    CHECK(st == 2 && tot == 2);
    CHECK(!strcmp(r[0].name, "version") && !strcmp(r[0].value, "0.3.7?"));
    CHECK(!strcmp(r[1].name, "time") && r[1].value[0] == 0);
    CHECK(sampq_parse_rules(rules, sizeof rules - 2, r, 2, &st, &tot) == -1);

    CHECK(!sampq_parse_players(players, sizeof players - 1, pl, 1, &st, &tot));
    CHECK(st == 1 && tot == 2); /* more declared than stored is fine */
    CHECK(!strcmp(pl[0].name, "Bob") && pl[0].score == -1);
    CHECK(sampq_parse_players(players, sizeof players - 2, pl, 1, &st, &tot) == -1);

    fuzz(info, sizeof info - 1, 20000);
    fuzz(rules, sizeof rules - 1, 20000);
    fuzz(players, sizeof players - 1, 20000);
}

static void test_hostile(void)
{
    unsigned char b[64];
    sampq_info_t o;
    sampq_rule_t r[1];
    sampq_player_t p[1];
    unsigned st, tot, i;

    memset(b, 0, sizeof b);
    memcpy(b, "SAMP\x7f\x00\x00\x01\x61\x1ei", 11);
    b[16] = 0xFF; b[17] = 0xFF; b[18] = 0xFF; b[19] = 0xFF; /* 4GB hostname */
    CHECK(sampq_parse_info(b, 40, &o) == -1);
    b[19] = 0x7F;
    CHECK(sampq_parse_info(b, 40, &o) == -1);

    b[11] = 0xFF; b[12] = 0xFF; /* 65535 entries declared, 2 bytes of data */
    CHECK(sampq_parse_rules(b, 15, r, 1, &st, &tot) == -1);
    CHECK(sampq_parse_players(b, 15, p, 1, &st, &tot) == -1);

    /* A long name must be cut to the field, not overrun it. */
    {
        static unsigned char big[400];
        memcpy(big, "SAMP\x7f\x00\x00\x01\x61\x1ei\x00\x00\x00\x00\x00", 16);
        big[16] = 200;
        memset(big + 20, 'A', 200);
        CHECK(sampq_parse_info(big, 232, &o) == 0);
        CHECK(strlen(o.hostname) == SAMPQ_HOST_LEN - 1);
    }
    for (i = 0; i < 64; i++)
        b[i] = i * 37;
    fuzz(b, sizeof b, 5000);
}

static void test_real(const char *dir)
{
    static unsigned char b[4096], rd[256];
    static sampq_rule_t r[BROWSER_MAX_RULES];
    static sampq_player_t pl[BROWSER_MAX_PLAYERS];
    static const unsigned char nonce[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    char ips[32], want[6][80];
    unsigned ip0, ip1, ip2, ip3, port, n, st, tot, ip, i;
    sampq_info_t o;
    FILE *f;

    n = load(dir, "meta.txt", rd, sizeof rd - 1);
    if (!n) {
        printf("no captured replies in %s, skipping the real-bytes test\n", dir);
        fails++;
        return;
    }
    rd[n] = 0;
    sscanf((char *)rd, "%31s %u", ips, &port);
    sscanf(ips, "%u.%u.%u.%u", &ip0, &ip1, &ip2, &ip3);
    ip = ip0 << 24 | ip1 << 16 | ip2 << 8 | ip3;

    n = load(dir, "reply_i.bin", b, sizeof b);
    CHECK(sampq_check(b, n, ip, port) == 'i');
    CHECK(!sampq_parse_info(b, n, &o));
    printf("real 'i' (%u bytes): pw=%d players=%d/%d host=\"%s\" mode=\"%s\" lang=\"%s\"\n",
           n, o.password, o.players, o.max_players, o.hostname, o.gamemode, o.language);
    /* Compared with the Python reading of the same bytes. */
    snprintf((char *)rd, sizeof rd, "%s/expect_i.txt", dir);
    f = fopen((char *)rd, "r");
    CHECK(f != 0);
    if (f) {
        for (i = 0; i < 6; i++) {
            want[i][0] = 0;
            if (fgets(want[i], sizeof want[i], f))
                want[i][strcspn(want[i], "\n")] = 0;
        }
        fclose(f);
        CHECK(atoi(want[0]) == o.password && atoi(want[1]) == o.players && atoi(want[2]) == o.max_players);
        CHECK(!strcmp(want[3], o.hostname) && !strcmp(want[4], o.gamemode) && !strcmp(want[5], o.language));
    }
    fuzz(b, n, 50000);

    n = load(dir, "reply_r.bin", b, sizeof b);
    CHECK(sampq_check(b, n, ip, port) == 'r');
    CHECK(!sampq_parse_rules(b, n, r, BROWSER_MAX_RULES, &st, &tot));
    CHECK(st == tot && st > 0);
    printf("real 'r' (%u bytes): %u rules:", n, st);
    for (i = 0; i < st; i++)
        printf(" %s=%s;", r[i].name, r[i].value);
    putchar('\n');
    fuzz(b, n, 50000);

    n = load(dir, "reply_c.bin", b, sizeof b);
    CHECK(sampq_check(b, n, ip, port) == 'c');
    CHECK(!sampq_parse_players(b, n, pl, BROWSER_MAX_PLAYERS, &st, &tot));
    printf("real 'c' (%u bytes): %u players:", n, st);
    for (i = 0; i < st; i++)
        printf(" %s(%d)", pl[i].name, pl[i].score);
    putchar('\n');
    fuzz(b, n, 50000);

    n = load(dir, "reply_p.bin", b, sizeof b);
    CHECK(sampq_check(b, n, ip, port) == 'p');
    CHECK(!sampq_parse_ping(b, n, nonce));
}

static void test_dns(void)
{
    unsigned char q[DNSQ_MAX_QUERY], r[300];
    unsigned ip = 0;
    int n;

    n = dnsq_build(q, sizeof q, 0x1234, "sam.markski.ar");
    CHECK(n == 12 + 16 + 4);
    CHECK(q[12] == 3 && !memcmp(q + 13, "sam", 3) && q[16] == 7 && q[24] == 2 && q[27] == 0);
    CHECK(dnsq_build(q, sizeof q, 1, "a..b") == -1);
    CHECK(dnsq_build(q, sizeof q, 1, "") == -1);
    CHECK(dnsq_build(q, sizeof q, 1, ".") == -1);
    CHECK(dnsq_build(q, sizeof q, 1, "example.com.") > 0);

    /* A CNAME ahead of the A record, using name compression. */
    n = dnsq_build(q, sizeof q, 0x1234, "a.b");
    memcpy(r, q, n);
    r[2] = 0x81; r[3] = 0x80; r[7] = 2;
    {
        static const unsigned char ans[] = {
            0xC0,12, 0,5, 0,1, 0,0,0,60, 0,2, 0xC0,12,
            0xC0,12, 0,1, 0,1, 0,0,0,60, 0,4, 188,114,97,5 };
        memcpy(r + n, ans, sizeof ans);
        CHECK(dnsq_parse(r, n + sizeof ans, 0x1234, &ip) == DNSQ_OK);
        CHECK(ip == 0xBC726105);
        CHECK(dnsq_parse(r, n + sizeof ans, 0x1235, &ip) == DNSQ_NOT_OURS);
        CHECK(dnsq_parse(r, n + sizeof ans - 1, 0x1234, &ip) == DNSQ_NOT_OURS);
        fuzz(r, n + sizeof ans, 20000);
    }
    r[3] = 0x82; /* what the adapter sends when a lookup fails */
    CHECK(dnsq_parse(r, n, 0x1234, &ip) == DNSQ_NO_ADDR);
    r[3] = 0x80; r[7] = 0;
    CHECK(dnsq_parse(r, n, 0x1234, &ip) == DNSQ_NO_ADDR);
}

/* ---- HTTP with a scripted connection ---- */

static const unsigned char *m_data;
static unsigned m_len, m_off, m_chunk, m_end_state, m_closed;
static char m_sent[512];
static unsigned m_sent_len;

static int m_open(void *c, unsigned ip, unsigned port, unsigned now) { return 0; }
static int m_status(void *c)
{
    if (m_off < m_len)
        return HTTP_TCP_OPEN;
    return m_end_state;
}
static int m_send(void *c, const void *d, unsigned n)
{
    memcpy(m_sent + m_sent_len, d, n);
    m_sent_len += n;
    return n;
}
static int m_recv(void *c, void *d, unsigned max)
{
    unsigned n = m_len - m_off;
    if (n > m_chunk) n = m_chunk;
    if (n > max) n = max;
    memcpy(d, m_data + m_off, n);
    m_off += n;
    return n;
}
static void m_close(void *c) { m_closed++; }
static const http_tcp_t mock = {0, m_open, m_status, m_send, m_recv, m_close};

static char body[4096];
static unsigned body_len;
static int sink(void *u, const unsigned char *d, unsigned n)
{
    memcpy(body + body_len, d, n);
    body_len += n;
    return 0;
}

static http_t h;
static http_state_t run_http(const char *resp, unsigned chunk, int end_state)
{
    unsigned t = 0;
    m_data = (const unsigned char *)resp;
    m_len = strlen(resp); m_off = 0; m_chunk = chunk; m_end_state = end_state;
    m_sent_len = 0; m_closed = 0; body_len = 0;
    http_start(&h, &mock, 1, 8080, "example.org", "/x?y=1", sink, 0, 0);
    while (h.state != HTTP_DONE && h.state != HTTP_FAILED && t < 100000)
        http_tick(&h, t += 33);
    return h.state;
}

static void test_http(void)
{
    static const char ok[] = "HTTP/1.1 200 OK\r\nServer: x\r\nCONTENT-length:  5\r\n\r\nhelloEXTRA";
    static const char nolen[] = "HTTP/1.0 200 OK\nX: y\n\nabc\ndef";
    unsigned c;

    /* Any split of the response must give the same result. */
    for (c = 1; c < 80; c++) {
        CHECK(run_http(ok, c, HTTP_TCP_PEER_DONE) == HTTP_DONE);
        CHECK(body_len == 5 && !memcmp(body, "hello", 5));
        CHECK(m_closed == 1);
        CHECK(run_http(nolen, c, HTTP_TCP_PEER_DONE) == HTTP_DONE);
        CHECK(body_len == 7 && !memcmp(body, "abc\ndef", 7));
    }
    CHECK(strstr(m_sent, "GET /x?y=1 HTTP/1.0\r\n") == m_sent);
    CHECK(strstr(m_sent, "\r\nHost: example.org:8080\r\n") != 0);

    CHECK(run_http(nolen, 5, -3) == HTTP_FAILED && h.error == HTTP_E_RESET);
    CHECK(run_http("HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\nabc", 5, HTTP_TCP_PEER_DONE) == HTTP_FAILED);
    CHECK(h.error == HTTP_E_TRUNCATED);
    CHECK(run_http("HTTP/1.1 200 OK\r\nX: y", 5, HTTP_TCP_PEER_DONE) == HTTP_FAILED);
    CHECK(h.error == HTTP_E_TRUNCATED);
    CHECK(run_http("HTTP/1.1 301 Moved\r\nLocation: https://x/\r\n\r\n", 9, HTTP_TCP_PEER_DONE) == HTTP_FAILED);
    CHECK(h.error == HTTP_E_STATUS && h.status == 301);
    CHECK(run_http("HTTP/1.1 200 OK\r\nTransfer-Encoding: Chunked\r\n\r\n5\r\n", 9, HTTP_TCP_PEER_DONE) == HTTP_FAILED);
    CHECK(h.error == HTTP_E_CHUNKED);
    CHECK(run_http("garbage\r\n\r\n", 9, HTTP_TCP_PEER_DONE) == HTTP_FAILED);
    CHECK(h.error == HTTP_E_BAD_RESPONSE);
    CHECK(run_http("HTTP/1.1 200 OK\r\nContent-Length: 99999999999999\r\n\r\n", 99, HTTP_TCP_PEER_DONE) == HTTP_FAILED);
    CHECK(h.error == HTTP_E_TOO_BIG);
    CHECK(run_http("HTTP/1.1 200 OK\r\nContent-Length: 300000\r\n\r\n", 99, HTTP_TCP_PEER_DONE) == HTTP_FAILED);
    CHECK(h.error == HTTP_E_TOO_BIG);
    CHECK(run_http("", 9, HTTP_TCP_OPEN) == HTTP_FAILED && h.error == HTTP_E_TIMEOUT);
    CHECK(m_closed == 1);
}

/* ---- browser table, with a transport that only records ---- */

static unsigned sent_ip[4096], sent_port[4096], sent_n;
static unsigned char sent_op[4096];
static int t_udp(void *c, unsigned ip, unsigned port, const void *d, unsigned n)
{
    sent_ip[sent_n] = ip; sent_port[sent_n] = port;
    sent_op[sent_n] = ((const unsigned char *)d)[10];
    sent_n++;
    return 0;
}

static void test_table(void)
{
    static const browser_transport_t t = {0, t_udp, 0, {0, m_open, m_status, m_send, m_recv, m_close}, 0x01010101};
    char out[256];
    int a, b, n, i;
    unsigned now = 1000, ticks;

    browser_init(&t, 0);
    CHECK(browser_add_ip("") == BROWSER_ADD_BAD);
    CHECK(browser_add_ip("1.2.3") == BROWSER_ADD_BAD);
    CHECK(browser_add_ip("999.1.1.1") == BROWSER_ADD_BAD);
    CHECK(browser_add_ip("1.2.3.4:0") == BROWSER_ADD_BAD);
    CHECK(browser_add_ip("1.2.3.4:70000") == BROWSER_ADD_BAD);
    CHECK(browser_add_ip("1.2.3.4:x") == BROWSER_ADD_BAD);
    CHECK(browser_add_ip("bad host") == BROWSER_ADD_BAD);
    CHECK(browser_add_ip("host:") == BROWSER_ADD_BAD);
    CHECK(browser_count() == 0);

    a = browser_add_ip(" 1.2.3.4 ");
    b = browser_add_ip("5.6.7.8:7778");
    CHECK(a == 0 && b == 1);
    CHECK(browser_add_ip("1.2.3.4:7777") == a); /* same server, not a duplicate */
    CHECK(browser_count() == 2);
    CHECK(browser_get(a)->ip == 0x01020304 && browser_get(a)->port == 7777);
    CHECK(browser_get(a)->flags & BROWSER_F_MANUAL);
    CHECK(browser_get(2) == 0 && browser_get(-1) == 0);
    CHECK(browser_find(0x05060708, 7778) == b);
    CHECK(browser_find(0x05060708, 7779) == -1);

    CHECK(!browser_fav_set(b, 1) && browser_fav_count() == 1);
    n = browser_fav_export(out, sizeof out);
    CHECK(n == (int)strlen("5.6.7.8:7778\n") && !memcmp(out, "5.6.7.8:7778\n", n));
    CHECK(browser_fav_export(out, 5) == -1);
    CHECK(browser_fav_import("9.9.9.9:1\r\nnope\n\n1.2.3.4:7777\n8.8.8.8", 37) == 3);
    CHECK(browser_fav_count() == 4 && browser_count() == 4);

    CHECK(!browser_remove(0));
    CHECK(browser_count() == 3 && browser_find(0x01020304, 7777) == -1);
    CHECK(browser_find(0x05060708, 7778) == 0);

    /* Fill the table, then one more. */
    browser_init(&t, 0);
    for (i = 0; i < BROWSER_MAX_SERVERS; i++) {
        snprintf(out, sizeof out, "20.%d.%d.1:7777", i / 200, i % 200);
        CHECK(browser_add_ip(out) == i);
    }
    CHECK(browser_add_ip("30.0.0.1") == BROWSER_ADD_FULL);
    for (i = 0; i < BROWSER_MAX_SERVERS; i += 97) {
        snprintf(out, sizeof out, "20.%d.%d.1:7777", i / 200, i % 200);
        CHECK(browser_add_ip(out) == i);
    }

    /* The bound: never more than SENDS_PER_TICK per frame, or SLOTS in flight, and
     * every entry is tried TRIES times before it is marked offline. */
    sent_n = 0;
    {
        unsigned prev = 0, worst = 0;
        for (ticks = 0; ticks < 3000 && browser_pending(); ticks++) {
            browser_tick(now += 33);
            if (sent_n - prev > worst)
                worst = sent_n - prev;
            prev = sent_n;
            CHECK(browser_pending() <= BROWSER_MAX_SERVERS);
            if (sent_n > 4000) break;
        }
        CHECK(worst <= BROWSER_SENDS_PER_TICK);
        printf("scheduler: worst sends in one tick = %u, %u ticks so far\n", worst, ticks);
    }
    {
        int offline = 0;
        for (i = 0; i < 40; i++)
            if (browser_get(i)->state == BROWSER_ST_OFFLINE)
                offline++;
        CHECK(offline == 40);
    }
    for (i = 0, n = 0; i < (int)sent_n; i++)
        if (sent_ip[i] == browser_get(0)->ip)
            n++;
    CHECK(n == BROWSER_TRIES);
}

int main(int argc, char **argv)
{
    srand(12345);
    test_build();
    test_canned();
    test_hostile();
    test_dns();
    test_http();
    test_table();
    test_real(argc > 1 ? argv[1] : "fixtures");
    printf("unit: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
