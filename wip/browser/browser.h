#pragma once
/* In-game server browser backend. No UI, no libc beyond util.h, no allocation.
 * Everything is polled: call browser_tick once per frame and feed it datagrams
 * with browser_on_udp. Work per tick is bounded; see the limits below.
 *
 * The network comes in through browser_transport_t, so the same code runs on the
 * EE (browser_ee.c, over net.c) and on a host with real sockets.
 */
#include "sampq.h"
#include "httpget.h"

#define BROWSER_MAX_SERVERS 1024 /* the masterlist had 537 on 2026-11-07 */
#define BROWSER_SLOTS       32   /* queries outstanding at once */
#define BROWSER_SENDS_PER_TICK 4 /* about 120 queries/s at 30Hz */
#define BROWSER_TIMEOUT_MS  1000 /* per attempt */
#define BROWSER_TRIES       3
#define BROWSER_LOCAL_PORT  50001 /* must differ from the game's port */
#define BROWSER_DEFAULT_PORT 7777

#define BROWSER_MAX_RULES   24
#define BROWSER_MAX_PLAYERS 100
/* 'c' is skipped above this many players. A long reply can exceed the 2044 byte
 * NetPacket that the adapter copies into with no bounds check, which would
 * overrun PCSX2's RX thread stack. 64 * 29 byte entries + headers still fits. */
#define BROWSER_PLAYER_QUERY_LIMIT 64

#define BROWSER_LIST_HOST "sam.markski.ar"
#define BROWSER_LIST_PATH "/api/GetMasterlist?version=0.3.7"
#define BROWSER_LIST_PORT 80

typedef struct {
    void *ctx;
    /* Sends from BROWSER_LOCAL_PORT. Returns 0, or <0 to try again next tick. */
    int  (*udp_send)(void *ctx, unsigned ip, unsigned port, const void *d, unsigned n);
    /* Called at the start of each tick. Optional. */
    void (*poll)(void *ctx, unsigned now_ms);
    http_tcp_t tcp;
    unsigned dns_ip;  /* the resolver to ask, on port 53 */
} browser_transport_t;

typedef struct {
    const char *list_host;   /* defaults to BROWSER_LIST_* when 0 */
    const char *list_path;
    unsigned list_port;
    unsigned list_ip;        /* nonzero skips DNS and connects here */
    int no_auto_query;       /* list entries stay idle until queried */
    int allow_private_list;  /* keep private/reserved addresses found in a list */
} browser_config_t;

typedef enum {
    BROWSER_ST_IDLE,     /* known but not queried */
    BROWSER_ST_QUEUED,   /* waiting for a free slot */
    BROWSER_ST_QUERYING, /* 'i' outstanding */
    BROWSER_ST_PINGING,  /* info received, 'p' outstanding */
    BROWSER_ST_ONLINE,   /* info valid; ping too unless it is BROWSER_PING_NONE */
    BROWSER_ST_OFFLINE   /* no reply to 'i' after all tries */
} browser_state_t;

#define BROWSER_F_FAVOURITE 0x01
#define BROWSER_F_MANUAL    0x02 /* added by hand */
#define BROWSER_F_LIST      0x04 /* present in the last fetched list */
#define BROWSER_F_HAVE_INFO 0x08 /* info fields hold real data, perhaps old data */

#define BROWSER_PING_NONE 0xFFFF

typedef struct {
    unsigned ip;
    unsigned short port;
    unsigned char state;   /* browser_state_t */
    unsigned char flags;
    unsigned short ping_ms; /* resolution is one frame on the EE */
    sampq_info_t info;     /* hostname, gamemode, language, players, ... */
} browser_server_t;

typedef enum {
    BROWSER_LIST_IDLE, BROWSER_LIST_RESOLVING, BROWSER_LIST_CONNECTING,
    BROWSER_LIST_RECEIVING, BROWSER_LIST_DONE, BROWSER_LIST_FAILED
} browser_list_state_t;

typedef enum {
    BROWSER_LE_NONE, BROWSER_LE_DNS, BROWSER_LE_HTTP, BROWSER_LE_EMPTY
} browser_list_error_t;

typedef enum {
    BROWSER_PART_NONE, BROWSER_PART_LOADING, BROWSER_PART_READY,
    BROWSER_PART_FAILED,      /* no reply; for players, a full server may just not answer */
    BROWSER_PART_UNAVAILABLE  /* not requested: over BROWSER_PLAYER_QUERY_LIMIT */
} browser_part_t;

/* Rules and players of the one selected server. */
typedef struct {
    unsigned ip;
    unsigned short port;
    unsigned char rules_state, players_state; /* browser_part_t */
    unsigned rule_count, rule_total;
    unsigned player_count, player_total;
    sampq_rule_t rules[BROWSER_MAX_RULES];
    sampq_player_t players[BROWSER_MAX_PLAYERS];
} browser_detail_t;

void browser_init(const browser_transport_t *t, const browser_config_t *cfg);
void browser_tick(unsigned now_ms);
/* Hand over every datagram addressed to BROWSER_LOCAL_PORT. */
void browser_on_udp(unsigned src_ip, unsigned src_port, const unsigned char *d,
                    unsigned len, unsigned now_ms);

/* Starts fetching the list. Existing entries stay until a complete list has
 * arrived, and favourites and manual entries always stay. Returns 0, or -1 if a
 * fetch is already running. */
int browser_refresh_internet(void);
browser_list_state_t browser_list_state(void);
browser_list_error_t browser_list_error(void);
http_error_t browser_list_http_error(void);
int browser_list_http_status(void);

/* "1.2.3.4", "1.2.3.4:7777", "host.name" or "host.name:7777". An address is added
 * at once and its index returned. A hostname returns BROWSER_ADD_RESOLVING and
 * appears when resolved; see browser_add_state. Other negatives are errors. */
#define BROWSER_ADD_RESOLVING (-1)
#define BROWSER_ADD_BAD       (-2)
#define BROWSER_ADD_FULL      (-3)
#define BROWSER_ADD_BUSY      (-4)
int browser_add_ip(const char *text);
/* 0 idle, 1 resolving, 2 added, -1 failed: the last hostname given to add_ip. */
int browser_add_state(void);

/* Indices are valid until the list changes: a completed refresh and
 * browser_remove both renumber. Hold ip:port across those, not an index. */
int browser_count(void);
const browser_server_t *browser_get(int i);
int browser_find(unsigned ip, unsigned port);
int browser_remove(int i);

/* Queue an entry, or every entry, for a fresh 'i' and 'p'. */
void browser_query(int i);
void browser_query_all(void);
/* Queries still to finish, for a progress indicator. */
int browser_pending(void);

/* Also requeries the server. rules_state and players_state then move on their own. */
void browser_select(int i);
const browser_detail_t *browser_detail(void);

/* Favourites live in memory for now. To persist them later, write out what
 * browser_fav_export gives and pass it to browser_fav_import at start-up: the
 * format is one "a.b.c.d:port\n" per server. */
int browser_fav_set(int i, int on);
int browser_fav_count(void);
/* Returns bytes written, or -1 if max is too small. Not NUL terminated. */
int browser_fav_export(char *out, unsigned max);
/* Returns how many were added. Lines that do not parse are skipped. */
int browser_fav_import(const char *text, unsigned len);
