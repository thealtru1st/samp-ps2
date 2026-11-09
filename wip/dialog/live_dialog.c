/* End-to-end host test: the real samp.c, rak.c and feat_dialog modules joined to a real
 * open.mp server running dlgtest.pwn. The only fakes are the game (fake_gfx.c records
 * what would be drawn) and the player, a script below that reads the screen and presses
 * pad buttons. The server's callbacks echo what its own parsers made of our answers.
 *
 *   live_dialog <ip> <port> <name> [peer]
 * peer logs in and idles, to be someone on the scoreboard. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "samp.h"
#include "samp_rpc.h"
#include "feat.h"
#include "feat_dialog_pad.h"
#include "fake_gfx.h"

static int sock;
static struct sockaddr_in srv;
static int peer;

void slog(const char *f, ...)
{
    va_list ap;
    if (peer) return;
    printf("log: ");
    va_start(ap, f); vprintf(f, ap); va_end(ap);
    putchar('\n');
}
void samp_plat_send(const unsigned char *d, unsigned n) { sendto(sock, d, n, 0, (struct sockaddr *)&srv, sizeof srv); }
void samp_plat_local(samp_local_t *l)
{
    memset(l, 0, sizeof *l);
    l->pos[0] = 2495; l->pos[1] = -1687; l->pos[2] = 13.5f;
    l->quat[0] = 1;
    l->health = 100;
}
void samp_plat_player_add(unsigned id, unsigned skin, const float *p, float a) {}
void samp_plat_player_del(unsigned id) {}
void samp_plat_player_sync(unsigned id, const float *p, const float *q, unsigned k, int m) {}
void samp_plat_set_pos(const float *p) {}
void samp_plat_set_skin(unsigned s) {}

#define CHAT_MAX 256
static char chat[CHAT_MAX][160];
static int nchat, chat_seen;

void samp_plat_message(const char *t)
{
    if (!peer) printf("chat: %s\n", t);
    if (nchat < CHAT_MAX)
        snprintf(chat[nchat++], sizeof chat[0], "%s", t);
}
void samp_plat_closed(const char *w) { printf("closed: %s\n", w); exit(1); }

/* The platform calls the feat modules use. */
int menu_is_connected(void) { return samp_state() != SAMP_ERROR && samp_state() != SAMP_OFFLINE; }

static void (*box_done)(int, const char *);
static int box_open, box_masked;
int menu_is_open(void) { return box_open; }
void menu_text_open(const char *prompt, int max, int masked, void (*done)(int, const char *))
{
    printf("text box: '%s' max %d masked %d\n", prompt, max, masked);
    box_done = done;
    box_masked = masked;
    box_open = 1;
}

static unsigned now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000u + t.tv_nsec / 1000000u;
}

/* ---- the player -------------------------------------------------------------- */
enum { SEE, GONE, PRESS, TYPE, CMD, CB, WAITMS, SPAWNED, SHOT, HOLD_L1, END };
typedef struct { int op; const char *arg; int field; } step_t;

#define F(name) ((int)(__builtin_offsetof(menu_pad_t, name) / sizeof(short)))

static const step_t steps[] = {
    /* login exactly as a public server asks for it */
    {SEE, "Please log in."}, {SHOT, "login dialog"},
    {PRESS, 0, F(cross)}, {TYPE, "wrong"}, {CB, "CB dialog 1 1 -1 [wrong]"},
    {SEE, "Wrong password."}, {PRESS, 0, F(start)}, {TYPE, "hunter2"},
    {CB, "CB dialog 1 1 -1 [hunter2]"}, {CB, "CB logged in"},
    {CB, "CB reqspawn 1"}, {SPAWNED}, {CB, "CB spawned"},
    /* styles */
    {CMD, "/s1"}, {SEE, "Teleports"}, {SEE, "Red row"}, {SHOT, "list"},
    {PRESS, 0, F(down)}, {PRESS, 0, F(down)}, {PRESS, 0, F(up)}, {PRESS, 0, F(cross)},
    {CB, "CB dialog 10 1 1 [{FF0000}Red row]"}, {GONE, "Teleports"},
    {CMD, "/s1"}, {SEE, "Teleports"}, {PRESS, 0, F(down)}, {PRESS, 0, F(down)}, {PRESS, 0, F(circle)},
    {CB, "CB dialog 10 0 2 [Airport]"},
    {CMD, "/s2"}, {SEE, "Sawnoff"}, {SHOT, "tablist"}, {PRESS, 0, F(down)}, {PRESS, 0, F(cross)},
    {CB, "CB dialog 11 1 1 [Sawnoff]"},
    {CMD, "/s3"}, {SEE, "Score"}, {SEE, "Alice"}, {SHOT, "tablist headers"},
    {PRESS, 0, F(down)}, {PRESS, 0, F(down)}, {PRESS, 0, F(cross)},
    {CB, "CB dialog 12 1 1 [{00FF00}Bob]"},
    {CMD, "/s4"}, {SEE, "nice"}, {SEE, "paragraph"}, {SEE, "Tabbed"}, {SHOT, "msgbox"}, {PRESS, 0, F(circle)},
    {CB, "CB dialog 13 0 -1 []"},
    {CMD, "/s5"}, {SEE, "Pick a name"}, {PRESS, 0, F(cross)}, {TYPE, "Sweet Johnson 100%"},
    {CB, "CB dialog 14 1 -1 [Sweet Johnson 100%]"},
    {CMD, "/s5"}, {SEE, "Pick a name"}, {PRESS, 0, F(circle)}, {CB, "CB dialog 14 0 -1 []"},
    {CMD, "/s6"}, {SEE, "goes away"}, {CB, "CB hidden"}, {GONE, "goes away"},
    /* textdraws */
    {CMD, "/s7"}, {SEE, "Global"}, {SEE, "line two"}, {SEE, "Boxed"}, {SHOT, "textdraws"},
    {CMD, "/s8"}, {SEE, "Changed"}, {GONE, "Global"},
    {CMD, "/s9"}, {GONE, "Changed"},
    {CMD, "/s10"}, {SEE, "Click A"}, {SEE, "Click P"}, {SHOT, "selection"},
    {PRESS, 0, F(cross)}, {CB, "CB click 1"},
    {PRESS, 0, F(down)}, {PRESS, 0, F(cross)}, {CB, "CB click 2"},
    {PRESS, 0, F(down)}, {PRESS, 0, F(cross)}, {CB, "CB pclick 1"},
    {PRESS, 0, F(circle)}, {CB, "CB click 65535"},
    {CMD, "/s10"}, {WAITMS, 0, 300}, {CMD, "/s11"}, {CB, "CB click 65535"}, {CMD, "/s12"}, {GONE, "Click A"},
    /* menus */
    {CMD, "/s13"}, {SEE, "Weapon shop"}, {SEE, "Shotgun"}, {SEE, "$300"}, {SHOT, "menu"},
    {PRESS, 0, F(down)}, {PRESS, 0, F(cross)}, {CB, "CB menu row 2"}, {GONE, "Weapon shop"},
    {CMD, "/s13"}, {SEE, "Weapon shop"}, {PRESS, 0, F(circle)}, {CB, "CB menu exit"},
    /* the rest */
    {CMD, "/s14"}, {SEE, "three"}, {SEE, "second line"}, {SHOT, "game text"},
    {CMD, "/s15"}, {SEE, "style 0"}, {SEE, "style 6"},
    {CMD, "/s16"}, {WAITMS, 0, 500}, {HOLD_L1}, {SEE, "Peer"}, {SEE, "1234"}, {SHOT, "scoreboard"},
    {CMD, "/s18"}, {SEE, "long word"}, {SHOT, "long textdraw"},
    {END},
};

static int step, failed, done;
static unsigned step_at, press_at;
static menu_pad_t pad;
static int hold_l1;

static void next(const char *what)
{
    if (what)
        printf("ok   %s\n", what);
    step++;
    step_at = now_ms();
}

static int cb_seen(const char *want)
{
    int i;
    for (i = chat_seen; i < nchat; i++)
        if (strstr(chat[i], want)) {
            chat_seen = i + 1;
            return 1;
        }
    return 0;
}

static void run_step(void)
{
    const step_t *s = &steps[step];
    short *f = (short *)&pad;
    unsigned now = now_ms();
    char what[200];

    memset(&pad, 0, sizeof pad);
    if (hold_l1)
        pad.l1 = 255;
    if (s->op != END && now - step_at > 8000) {
        printf("FAIL step %d op %d '%s'\n", step, s->op, s->arg ? s->arg : "");
        failed++;
        if (getenv("STOP_ON_FAIL"))
            done = 1;
        fake_dump();
        next(0);
        return;
    }
    switch (s->op) {
    case SEE:
        if (fake_find(s->arg) >= 0) { snprintf(what, sizeof what, "on screen: %s", s->arg); next(what); }
        break;
    case GONE:
        if (fake_find(s->arg) < 0) { snprintf(what, sizeof what, "gone: %s", s->arg); next(what); }
        break;
    case PRESS:
        if (now - step_at < 300)
            break;
        if (!press_at) {
            press_at = now;
            f[s->field] = 255;
        } else if (now - press_at > 40) {
            press_at = 0;
            next(0);
        }
        break;
    case TYPE:
        if (box_open) {
            box_open = 0;
            box_done(1, s->arg);
            snprintf(what, sizeof what, "typed: %s", s->arg);
            next(what);
        }
        break;
    case CMD:
        samp_chat(s->arg);
        next(s->arg);
        break;
    case CB:
        if (cb_seen(s->arg)) { snprintf(what, sizeof what, "server said: %s", s->arg); next(what); }
        break;
    case WAITMS:
        if (now - step_at >= (unsigned)s->field)
            next(0);
        break;
    case SPAWNED:
        if (samp_state() == SAMP_SPAWNED)
            next("state is SPAWNED");
        break;
    case SHOT:
        printf("---- screen: %s\n", s->arg);
        fake_dump();
        next(0);
        break;
    case HOLD_L1:
        hold_l1 = 1;
        next(0);
        break;
    case END:
        done = 1;
        break;
    }
}

int main(int c, char **v)
{
    const char *ip = v[1], *name = v[3];
    int port = atoi(v[2]);
    unsigned char buf[2048];
    unsigned start = now_ms(), last = 0;
    int peer_logged = 0;
    static samp_rpc_handlers_t h;

    peer = c > 4 && !strcmp(v[4], "peer");
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    srv.sin_family = AF_INET;
    srv.sin_port = htons(port);
    inet_pton(AF_INET, ip, &srv.sin_addr);
    feat_dialog_init(&h);
    samp_rpc_set_handlers(&h);
    samp_connect(ntohl(srv.sin_addr.s_addr), port, name, "");
    step_at = now_ms();
    while (!done && now_ms() - start < (peer ? 150000u : 140000u)) {
        struct pollfd p = {sock, POLLIN, 0};
        unsigned now;

        if (poll(&p, 1, 8) > 0) {
            int n = recv(sock, buf, sizeof buf, 0);
            if (n > 0)
                samp_datagram(buf, n, now_ms());
        }
        now = now_ms();
        samp_tick(now);
        if (now - last < 16)
            continue;
        last = now;
        if (peer) {
            memset(&pad, 0, sizeof pad);
            if (!peer_logged && now - start > 2000) { pad.cross = 255; peer_logged = 1; }
            if (box_open) { box_open = 0; box_done(1, "hunter2"); }
        } else {
            run_step();
        }
        feat_dialog_pad(&pad);
        fake_clear();
        feat_dialog_tick(now);
        feat_dialog_draw();
    }
    samp_disconnect();
    usleep(200000);
    if (!peer)
        printf("LIVE RESULT: %d steps, %d failed%s\n", step, failed, done ? "" : ", timed out");
    return failed || !done;
}
