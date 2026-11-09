#include "browser.h"
#include "dnsq.h"
#include "util.h"

#define HASH_SIZE  (BROWSER_MAX_SERVERS * 2) /* power of two */
#define HASH_EMPTY 0xFFFF
#define SCAN_PER_TICK 64 /* entries looked at while finding queued work */

#define DNS_TIMEOUT_MS 3000 /* the adapter does a real lookup, which can be slow */
#define DNS_TRIES      3
#define DNS_HOST_MAX   64
#define LINE_MAX_LEN   24 /* "255.255.255.255:65535" and a little slack */

enum { SLOT_FREE, SLOT_SEND, SLOT_WAIT };
enum { DNS_IDLE, DNS_WAIT, DNS_DONE, DNS_FAILED };

typedef struct {
    unsigned char state, op, tries;
    unsigned short idx;
    unsigned sent;
    unsigned char nonce[4];
} slot_t;

typedef struct {
    unsigned char state, tries, send_now;
    unsigned short id;
    unsigned sent, ip, port;
    char host[DNS_HOST_MAX];
} dns_job_t;

typedef struct {
    unsigned ip;
    unsigned short port;
} stage_t;

static struct {
    browser_transport_t t;
    browser_config_t cfg;
    int count, queued, cursor;
    unsigned now, rng;
    slot_t slots[BROWSER_SLOTS];

    browser_list_state_t list_state;
    browser_list_error_t list_error;
    http_t http;
    dns_job_t dns_list, dns_add;
    int add_state;
    int add_fav;  /* the address being added is a favourite */
    char line[LINE_MAX_LEN];
    unsigned line_len, line_bad;
    int stage_n;

    int det_active;
    browser_detail_t det;
} s;

static browser_server_t servers[BROWSER_MAX_SERVERS];
static unsigned short hash_tab[HASH_SIZE];
static stage_t stage[BROWSER_MAX_SERVERS];

static unsigned rnd(void)
{
    s.rng = s.rng * 1664525u + 1013904223u;
    return s.rng >> 8;
}

/* ---- server table ---- */

static unsigned hash_of(unsigned ip, unsigned port)
{
    unsigned h = (ip ^ (port * 2654435761u)) * 2654435761u;
    return (h >> 16) & (HASH_SIZE - 1);
}

static int hash_find(unsigned ip, unsigned port)
{
    unsigned h = hash_of(ip, port);

    while (hash_tab[h] != HASH_EMPTY) {
        browser_server_t *e = &servers[hash_tab[h]];
        if (e->ip == ip && e->port == port)
            return hash_tab[h];
        h = (h + 1) & (HASH_SIZE - 1);
    }
    return -1;
}

static void hash_put(unsigned ip, unsigned port, int idx)
{
    unsigned h = hash_of(ip, port);

    while (hash_tab[h] != HASH_EMPTY)
        h = (h + 1) & (HASH_SIZE - 1);
    hash_tab[h] = idx;
}

static void hash_rebuild(void)
{
    int i;
    memset(hash_tab, 0xFF, sizeof hash_tab);
    for (i = 0; i < s.count; i++)
        hash_put(servers[i].ip, servers[i].port, i);
}

static int server_add(unsigned ip, unsigned port, unsigned flags)
{
    int i = hash_find(ip, port);

    if (i >= 0) {
        servers[i].flags |= flags;
        return i;
    }
    if (s.count == BROWSER_MAX_SERVERS)
        return -1;
    i = s.count++;
    memset(&servers[i], 0, sizeof servers[i]);
    servers[i].ip = ip;
    servers[i].port = port;
    servers[i].flags = flags;
    servers[i].ping_ms = BROWSER_PING_NONE;
    hash_put(ip, port, i);
    return i;
}

static void set_queued(browser_server_t *e)
{
    if (e->state != BROWSER_ST_QUEUED)
        s.queued++;
    e->state = BROWSER_ST_QUEUED;
}

/* The table is about to be renumbered, so nothing may hold an index across it.
 * Interrupted queries go back on the queue. */
static void slots_clear(void)
{
    int i;
    for (i = 0; i < BROWSER_SLOTS; i++) {
        slot_t *q = &s.slots[i];
        if (q->state != SLOT_FREE) {
            browser_server_t *e = &servers[q->idx];
            if (q->op == 'i' || q->op == 'p')
                set_queued(e);
            else if (s.det_active)
                *(q->op == 'r' ? &s.det.rules_state : &s.det.players_state) = BROWSER_PART_NONE;
            q->state = SLOT_FREE;
        }
    }
}

/* Drops one entry, or with drop_index < 0 every entry without a keep_mask flag,
 * and closes up the table. */
static void compact(int drop_index, unsigned keep_mask)
{
    int r, w = 0;

    slots_clear();
    for (r = 0; r < s.count; r++) {
        if (r == drop_index || (drop_index < 0 && !(servers[r].flags & keep_mask))) {
            if (servers[r].state == BROWSER_ST_QUEUED)
                s.queued--;
            continue;
        }
        if (w != r)
            servers[w] = servers[r];
        w++;
    }
    s.count = w;
    s.cursor = 0;
    hash_rebuild();
}

/* ---- query scheduler ---- */

static slot_t *slot_alloc(int idx, char op)
{
    int i;
    for (i = 0; i < BROWSER_SLOTS; i++) {
        slot_t *q = &s.slots[i];
        if (q->state == SLOT_FREE) {
            q->state = SLOT_SEND;
            q->op = op;
            q->idx = idx;
            q->tries = 0;
            return q;
        }
    }
    return 0;
}

static slot_t *slot_find(int idx, int op)
{
    int i;
    for (i = 0; i < BROWSER_SLOTS; i++) {
        slot_t *q = &s.slots[i];
        if (q->state == SLOT_WAIT && q->idx == idx && q->op == op)
            return q;
    }
    return 0;
}

static void part_set(int op, browser_part_t st)
{
    if (op == 'r')
        s.det.rules_state = st;
    else
        s.det.players_state = st;
}

static int is_selected(const browser_server_t *e)
{
    return s.det_active && e->ip == s.det.ip && e->port == s.det.port;
}

/* A slot has used all its tries. */
static void slot_give_up(slot_t *q)
{
    browser_server_t *e = &servers[q->idx];

    if (q->op == 'i') {
        e->state = BROWSER_ST_OFFLINE;
        e->ping_ms = BROWSER_PING_NONE;
    } else if (q->op == 'p') {
        e->state = BROWSER_ST_ONLINE; /* it did answer 'i' */
        e->ping_ms = BROWSER_PING_NONE;
    } else if (is_selected(e)) {
        part_set(q->op, BROWSER_PART_FAILED);
    }
    q->state = SLOT_FREE;
}

/* Returns 0 if sent, <0 if the transport refused and it should wait for a tick. */
static int slot_send(slot_t *q)
{
    unsigned char pkt[SAMPQ_PING_LEN];
    browser_server_t *e = &servers[q->idx];
    unsigned n, r;

    /* A new nonce per attempt: a late reply to an old one must not give a tiny ping. */
    r = rnd();
    q->nonce[0] = r; q->nonce[1] = r >> 8; q->nonce[2] = r >> 16; q->nonce[3] = rnd();
    n = sampq_build(pkt, e->ip, e->port, q->op, q->nonce);
    if (s.t.udp_send(s.t.ctx, e->ip, e->port, pkt, n) < 0)
        return -1;
    q->state = SLOT_WAIT;
    q->sent = s.now;
    q->tries++;
    return 0;
}

static void detail_step(void)
{
    browser_server_t *e;
    int i;

    if (!s.det_active)
        return;
    if ((i = hash_find(s.det.ip, s.det.port)) < 0) { /* removed while selected */
        s.det_active = 0;
        return;
    }
    e = &servers[i];
    if (e->state == BROWSER_ST_OFFLINE) {
        if (s.det.rules_state == BROWSER_PART_NONE)
            s.det.rules_state = BROWSER_PART_FAILED;
        if (s.det.players_state == BROWSER_PART_NONE)
            s.det.players_state = BROWSER_PART_FAILED;
        return;
    }
    if (e->state != BROWSER_ST_PINGING && e->state != BROWSER_ST_ONLINE)
        return; /* 'c' depends on the player count in the 'i' reply */
    if (s.det.rules_state == BROWSER_PART_NONE && slot_alloc(i, 'r'))
        s.det.rules_state = BROWSER_PART_LOADING;
    if (s.det.players_state == BROWSER_PART_NONE) {
        if (e->info.players > BROWSER_PLAYER_QUERY_LIMIT)
            s.det.players_state = BROWSER_PART_UNAVAILABLE;
        else if (slot_alloc(i, 'c'))
            s.det.players_state = BROWSER_PART_LOADING;
    }
}

static void queries_tick(void)
{
    int budget = BROWSER_SENDS_PER_TICK, scan = SCAN_PER_TICK, i;

    for (i = 0; i < BROWSER_SLOTS; i++) {
        slot_t *q = &s.slots[i];
        if (q->state == SLOT_WAIT && (int)(s.now - q->sent) >= BROWSER_TIMEOUT_MS) {
            if (q->tries >= BROWSER_TRIES)
                slot_give_up(q);
            else
                q->state = SLOT_SEND;
        }
    }
    detail_step();
    for (i = 0; i < BROWSER_SLOTS && budget; i++) {
        slot_t *q = &s.slots[i];
        if (q->state == SLOT_SEND) {
            if (slot_send(q) < 0)
                return;
            budget--;
        }
    }
    while (budget && s.queued && scan-- && s.count) {
        browser_server_t *e;
        slot_t *q;
        if (s.cursor >= s.count)
            s.cursor = 0;
        e = &servers[s.cursor];
        if (e->state == BROWSER_ST_QUEUED) {
            if (!(q = slot_alloc(s.cursor, 'i')))
                return;
            if (slot_send(q) < 0) {
                q->state = SLOT_FREE;
                return;
            }
            e->state = BROWSER_ST_QUERYING;
            s.queued--;
            budget--;
        }
        s.cursor++;
    }
}

static void on_query_reply(int idx, const unsigned char *d, unsigned len, unsigned now)
{
    browser_server_t *e = &servers[idx];
    int op = d[10];
    slot_t *q = slot_find(idx, op);

    if (!q)
        return; /* unsolicited, or a duplicate of something finished */
    switch (op) {
    case 'i':
        if (sampq_parse_info(d, len, &e->info) < 0)
            return; /* the timeout will retry it */
        e->flags |= BROWSER_F_HAVE_INFO;
        e->state = BROWSER_ST_PINGING;
        q->op = 'p'; /* sent next tick, under the send budget */
        q->tries = 0;
        q->state = SLOT_SEND;
        return;
    case 'p': {
        unsigned rtt = now - q->sent;
        if (sampq_parse_ping(d, len, q->nonce) < 0)
            return;
        e->ping_ms = rtt < BROWSER_PING_NONE ? rtt : BROWSER_PING_NONE - 1;
        e->state = BROWSER_ST_ONLINE;
        break;
    }
    case 'r':
        if (!is_selected(e))
            break;
        if (sampq_parse_rules(d, len, s.det.rules, BROWSER_MAX_RULES,
                              &s.det.rule_count, &s.det.rule_total) < 0)
            return;
        s.det.rules_state = BROWSER_PART_READY;
        break;
    case 'c':
        if (!is_selected(e))
            break;
        if (sampq_parse_players(d, len, s.det.players, BROWSER_MAX_PLAYERS,
                                &s.det.player_count, &s.det.player_total) < 0)
            return;
        s.det.players_state = BROWSER_PART_READY;
        break;
    default:
        return;
    }
    q->state = SLOT_FREE;
}

/* ---- DNS ---- */

static void dns_start(dns_job_t *j, const char *host, unsigned port)
{
    unsigned n = strlen(host);

    memcpy(j->host, host, n + 1);
    j->port = port;
    j->state = DNS_WAIT;
    j->tries = 0;
    j->send_now = 1;
    j->id = rnd() & 0xFFFF;
}

static void dns_tick(dns_job_t *j)
{
    unsigned char pkt[DNSQ_MAX_QUERY];
    int n;

    if (j->state != DNS_WAIT)
        return;
    if (!j->send_now && (int)(s.now - j->sent) < DNS_TIMEOUT_MS)
        return;
    if (j->tries >= DNS_TRIES) {
        j->state = DNS_FAILED;
        return;
    }
    if ((n = dnsq_build(pkt, sizeof pkt, j->id, j->host)) < 0) {
        j->state = DNS_FAILED;
        return;
    }
    if (s.t.udp_send(s.t.ctx, s.t.dns_ip, 53, pkt, n) < 0)
        return;
    j->sent = s.now;
    j->tries++;
    j->send_now = 0;
}

static void dns_reply(dns_job_t *j, const unsigned char *d, unsigned len)
{
    int r;

    if (j->state != DNS_WAIT)
        return;
    r = dnsq_parse(d, len, j->id, &j->ip);
    if (r == DNSQ_OK)
        j->state = DNS_DONE;
    else if (r == DNSQ_NO_ADDR)
        j->state = DNS_FAILED;
}

/* ---- list fetch ---- */

/* "a.b.c.d:port" only. Returns 0, or -1. */
static int parse_ip_port(const char *p, unsigned len, unsigned *ip, unsigned *port)
{
    unsigned i = 0, v = 0, part;

    for (part = 0; part < 4; part++) {
        unsigned n = 0, digits = 0;
        while (i < len && p[i] >= '0' && p[i] <= '9' && digits < 3) {
            n = n * 10 + (p[i++] - '0');
            digits++;
        }
        if (!digits || n > 255)
            return -1;
        v = (v << 8) | n;
        if (part < 3 && (i >= len || p[i++] != '.'))
            return -1;
    }
    *ip = v;
    *port = BROWSER_DEFAULT_PORT;
    if (i == len)
        return 0;
    if (p[i++] != ':' || i == len)
        return -1;
    v = 0;
    for (; i < len; i++) {
        if (p[i] < '0' || p[i] > '9')
            return -1;
        v = v * 10 + (p[i] - '0');
        if (v > 65535)
            return -1;
    }
    if (v == 0)
        return -1;
    *port = v;
    return 0;
}

/* A list comes from a third party, so it does not get to aim our queries at the
 * host's loopback (the adapter forwards 127.x there) or the local network. */
static int is_public(unsigned ip)
{
    unsigned a = ip >> 24, b = (ip >> 16) & 255;

    return !(a == 0 || a == 10 || a == 127 || a >= 224 ||
             (a == 100 && b >= 64 && b < 128) ||
             (a == 169 && b == 254) ||
             (a == 172 && b >= 16 && b < 32) ||
             (a == 192 && b == 168) ||
             (ip >> 8) == 0xC00002);
}

static void list_line(void)
{
    unsigned ip, port, n = s.line_len;

    if (n && s.line[n - 1] == '\r')
        n--;
    if (!s.line_bad && n && !parse_ip_port(s.line, n, &ip, &port) &&
        s.stage_n < BROWSER_MAX_SERVERS &&
        (s.cfg.allow_private_list || is_public(ip))) {
        stage[s.stage_n].ip = ip;
        stage[s.stage_n].port = port;
        s.stage_n++;
    }
    s.line_len = 0;
    s.line_bad = 0;
}

static int list_sink(void *user, const unsigned char *d, unsigned n)
{
    unsigned i;
    (void)user;
    for (i = 0; i < n; i++) {
        if (d[i] == '\n')
            list_line();
        else if (s.line_len < LINE_MAX_LEN)
            s.line[s.line_len++] = d[i];
        else
            s.line_bad = 1;
    }
    return 0;
}

/* A list only replaces what we have once all of it has arrived. */
static void list_commit(void)
{
    int i;

    compact(-1, BROWSER_F_FAVOURITE | BROWSER_F_MANUAL);
    for (i = 0; i < s.count; i++)
        servers[i].flags &= ~BROWSER_F_LIST;
    for (i = 0; i < s.stage_n; i++) {
        if (server_add(stage[i].ip, stage[i].port, BROWSER_F_LIST) < 0)
            break;
    }
    if (!s.cfg.no_auto_query)
        browser_query_all();
}

static void list_fail(browser_list_error_t e)
{
    s.list_state = BROWSER_LIST_FAILED;
    s.list_error = e;
}

static void list_connect(unsigned ip)
{
    s.stage_n = 0;
    s.line_len = 0;
    s.line_bad = 0;
    if (http_start(&s.http, &s.t.tcp, ip, s.cfg.list_port, s.cfg.list_host,
                   s.cfg.list_path, list_sink, 0, s.now) < 0) {
        list_fail(BROWSER_LE_HTTP);
        return;
    }
    s.list_state = BROWSER_LIST_CONNECTING;
}

static void list_tick(void)
{
    switch (s.list_state) {
    case BROWSER_LIST_RESOLVING:
        if (s.dns_list.state == DNS_DONE)
            list_connect(s.dns_list.ip);
        else if (s.dns_list.state == DNS_FAILED)
            list_fail(BROWSER_LE_DNS);
        break;
    case BROWSER_LIST_CONNECTING:
    case BROWSER_LIST_RECEIVING:
        switch (http_tick(&s.http, s.now)) {
        case HTTP_DONE:
            if (s.line_len)
                list_line(); /* the last line may have no newline */
            if (s.stage_n == 0) {
                list_fail(BROWSER_LE_EMPTY);
                break;
            }
            list_commit();
            s.list_state = BROWSER_LIST_DONE;
            break;
        case HTTP_FAILED:
            list_fail(BROWSER_LE_HTTP);
            break;
        case HTTP_HEADERS:
        case HTTP_BODY:
            s.list_state = BROWSER_LIST_RECEIVING;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}

static void add_tick(void)
{
    if (s.add_state != 1)
        return;
    if (s.dns_add.state == DNS_FAILED) {
        s.add_state = -1;
    } else if (s.dns_add.state == DNS_DONE) {
        int i = server_add(s.dns_add.ip, s.dns_add.port, BROWSER_F_MANUAL);
        s.add_state = i < 0 ? -1 : 2;
        if (i >= 0 && s.add_fav)
            browser_fav_set(i, 1);
        s.add_fav = 0;
        if (i >= 0)
            browser_query(i);
    }
}

/* ---- public ---- */

void browser_init(const browser_transport_t *t, const browser_config_t *cfg)
{
    memset(&s, 0, sizeof s);
    s.t = *t;
    if (cfg)
        s.cfg = *cfg;
    if (!s.cfg.list_host) s.cfg.list_host = BROWSER_LIST_HOST;
    if (!s.cfg.list_path) s.cfg.list_path = BROWSER_LIST_PATH;
    if (!s.cfg.list_port) s.cfg.list_port = BROWSER_LIST_PORT;
    memset(hash_tab, 0xFF, sizeof hash_tab);
}

void browser_tick(unsigned now_ms)
{
    s.now = now_ms;
    s.rng ^= now_ms; /* no entropy source here; this only has to differ per query */
    if (s.t.poll)
        s.t.poll(s.t.ctx, now_ms);
    dns_tick(&s.dns_list);
    dns_tick(&s.dns_add);
    list_tick();
    add_tick();
    queries_tick();
}

void browser_on_udp(unsigned src_ip, unsigned src_port, const unsigned char *d,
                    unsigned len, unsigned now_ms)
{
    int idx;

    if (src_ip == s.t.dns_ip && src_port == 53) {
        dns_reply(&s.dns_list, d, len);
        dns_reply(&s.dns_add, d, len);
        return;
    }
    if ((idx = hash_find(src_ip, src_port)) < 0)
        return;
    if (sampq_check(d, len, src_ip, src_port) < 0)
        return;
    on_query_reply(idx, d, len, now_ms);
}

int browser_refresh_internet(void)
{
    if (s.list_state >= BROWSER_LIST_RESOLVING && s.list_state <= BROWSER_LIST_RECEIVING)
        return -1;
    s.list_error = BROWSER_LE_NONE;
    memset(&s.http, 0, sizeof s.http);
    /* The connect itself happens in list_tick, where the time is current. */
    s.list_state = BROWSER_LIST_RESOLVING;
    if (s.cfg.list_ip) {
        s.dns_list.state = DNS_DONE;
        s.dns_list.ip = s.cfg.list_ip;
    } else if (strlen(s.cfg.list_host) >= DNS_HOST_MAX) {
        list_fail(BROWSER_LE_DNS);
    } else {
        dns_start(&s.dns_list, s.cfg.list_host, 0);
    }
    return 0;
}

browser_list_state_t browser_list_state(void) { return s.list_state; }
browser_list_error_t browser_list_error(void) { return s.list_error; }
http_error_t browser_list_http_error(void) { return s.http.error; }
int browser_list_http_status(void) { return s.http.status; }

int browser_add_ip(const char *text)
{
    unsigned n, ip, port = BROWSER_DEFAULT_PORT, host_len, i;
    char host[DNS_HOST_MAX];
    int k;

    while (*text == ' ')
        text++;
    n = strlen(text);
    while (n && text[n - 1] == ' ')
        n--;
    if (n == 0)
        return BROWSER_ADD_BAD;
    if (!parse_ip_port(text, n, &ip, &port)) {
        k = server_add(ip, port, BROWSER_F_MANUAL);
        if (k < 0)
            return BROWSER_ADD_FULL;
        browser_query(k);
        return k;
    }
    host_len = n;
    for (i = 0; i < n; i++)
        if (text[i] == ':')
            break;
    if (i < n) {
        unsigned v = 0, j;
        host_len = i;
        if (i + 1 == n)
            return BROWSER_ADD_BAD;
        for (j = i + 1; j < n; j++) {
            if (text[j] < '0' || text[j] > '9')
                return BROWSER_ADD_BAD;
            v = v * 10 + (text[j] - '0');
            if (v > 65535)
                return BROWSER_ADD_BAD;
        }
        if (v == 0)
            return BROWSER_ADD_BAD;
        port = v;
    }
    if (host_len == 0 || host_len >= DNS_HOST_MAX)
        return BROWSER_ADD_BAD;
    for (i = 0; i < host_len; i++) {
        char c = text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '.'))
            return BROWSER_ADD_BAD;
    }
    /* Digits and dots only is a mistyped address, not a name. */
    for (i = 0; i < host_len; i++)
        if (text[i] != '.' && (text[i] < '0' || text[i] > '9'))
            break;
    if (i == host_len)
        return BROWSER_ADD_BAD;
    if (s.add_state == 1)
        return BROWSER_ADD_BUSY;
    memcpy(host, text, host_len);
    host[host_len] = 0;
    dns_start(&s.dns_add, host, port);
    s.add_state = 1;
    return BROWSER_ADD_RESOLVING;
}

int browser_add_state(void) { return s.add_state; }

int browser_add_favourite(const char *text)
{
    int k = browser_add_ip(text);
    if (k >= 0)
        browser_fav_set(k, 1);
    else if (k == BROWSER_ADD_RESOLVING)
        s.add_fav = 1;
    return k;
}

int browser_count(void) { return s.count; }

const browser_server_t *browser_get(int i)
{
    return i >= 0 && i < s.count ? &servers[i] : 0;
}

int browser_find(unsigned ip, unsigned port) { return hash_find(ip, port); }

int browser_remove(int i)
{
    if (i < 0 || i >= s.count)
        return -1;
    compact(i, 0);
    return 0;
}

void browser_query(int i)
{
    browser_server_t *e;

    if (i < 0 || i >= s.count)
        return;
    e = &servers[i];
    if (e->state != BROWSER_ST_QUERYING && e->state != BROWSER_ST_PINGING)
        set_queued(e);
}

void browser_query_all(void)
{
    int i;
    for (i = 0; i < s.count; i++)
        browser_query(i);
}

int browser_pending(void)
{
    int i, n = s.queued;
    for (i = 0; i < BROWSER_SLOTS; i++)
        if (s.slots[i].state != SLOT_FREE)
            n++;
    return n;
}

void browser_select(int i)
{
    int k;

    if (i < 0 || i >= s.count) {
        s.det_active = 0;
        return;
    }
    /* A reply still in flight for the previous selection must find no slot. */
    for (k = 0; k < BROWSER_SLOTS; k++)
        if (s.slots[k].op == 'r' || s.slots[k].op == 'c')
            s.slots[k].state = SLOT_FREE;
    memset(&s.det, 0, sizeof s.det);
    s.det.ip = servers[i].ip;
    s.det.port = servers[i].port;
    s.det_active = 1;
    browser_query(i);
}

const browser_detail_t *browser_detail(void)
{
    return s.det_active ? &s.det : 0;
}

int browser_fav_set(int i, int on)
{
    if (i < 0 || i >= s.count)
        return -1;
    if (on)
        servers[i].flags |= BROWSER_F_FAVOURITE;
    else
        servers[i].flags &= ~BROWSER_F_FAVOURITE;
    return 0;
}

int browser_fav_count(void)
{
    int i, n = 0;
    for (i = 0; i < s.count; i++)
        if (servers[i].flags & BROWSER_F_FAVOURITE)
            n++;
    return n;
}

static unsigned put_dec(char *o, unsigned v)
{
    char b[10];
    unsigned i = 0, n = 0;
    do { b[i++] = '0' + v % 10; v /= 10; } while (v);
    while (i) o[n++] = b[--i];
    return n;
}

int browser_fav_export(char *out, unsigned max)
{
    unsigned off = 0;
    int i;

    for (i = 0; i < s.count; i++) {
        const browser_server_t *e = &servers[i];
        int b;
        if (!(e->flags & BROWSER_F_FAVOURITE))
            continue;
        if (max - off < LINE_MAX_LEN)
            return -1;
        for (b = 24; b >= 0; b -= 8) {
            off += put_dec(out + off, (e->ip >> b) & 255);
            out[off++] = b ? '.' : ':';
        }
        off += put_dec(out + off, e->port);
        out[off++] = '\n';
    }
    return off;
}

int browser_fav_import(const char *text, unsigned len)
{
    unsigned i = 0, added = 0;

    while (i < len) {
        unsigned start = i, n, ip, port;
        int k;
        while (i < len && text[i] != '\n')
            i++;
        n = i - start;
        if (n && text[start + n - 1] == '\r')
            n--;
        i++;
        if (!parse_ip_port(text + start, n, &ip, &port) &&
            (k = server_add(ip, port, BROWSER_F_FAVOURITE)) >= 0) {
            added++;
            browser_query(k);
        }
    }
    return added;
}
