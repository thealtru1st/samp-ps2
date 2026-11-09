/* Live test: the real samp.c, rak.c and the world stream's logic against a real private
 * server running worldtest.pwn, with fake_game.c standing in for the game. It walks the
 * player through the checkpoints, collects a pickup, hits the actor, and checks what the
 * server echoed back. Everything the game would have done is only counted.
 *
 *   live_world <ip> <port> <name> */
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
#include "feat_world_int.h"
#include "fake_game.h"

static int sock;
static struct sockaddr_in srv;
static samp_rpc_handlers_t handlers;
static int cb_enter, cb_leave, cb_race_in, cb_race_out, cb_pickup, cb_actor;
static char last_actor[64];

void fg_hit_actor(int h, float amount);

/* combat's animation calls, unused here */
unsigned cg_anim_block(const char *lib) { return 0; }
void cg_anim_request(const char *lib) {}
int cg_anim_ready(unsigned b) { return 0; }
int cg_anim_exists(unsigned b, const char *n) { return 0; }
void cg_anim_play(int h, const char *n, const char *l, float d, int lp, int x, int y, int f, int ms) {}

void slog(const char *f, ...)
{
    va_list ap;
    va_start(ap, f); vprintf(f, ap); va_end(ap);
    putchar('\n');
    fflush(stdout);
}

void samp_plat_send(const unsigned char *d, unsigned n) { sendto(sock, d, n, 0, (struct sockaddr *)&srv, sizeof srv); }

void samp_plat_local(samp_local_t *l)
{
    memset(l, 0, sizeof *l);
    memcpy(l->pos, fg.me, sizeof fg.me);
    l->quat[0] = 1.0f;
    l->health = 100;
}

void samp_plat_player_add(unsigned id, unsigned skin, const float *p, float a) {}
void samp_plat_player_del(unsigned id) {}
void samp_plat_player_sync(unsigned id, const float *p, const float *q, unsigned k, int m) {}
void samp_plat_set_pos(const float *p) { memcpy(fg.me, p, 12); }
void samp_plat_set_skin(unsigned s) {}
int  samp_plat_in_vehicle(void) { return 0; }
void samp_plat_closed(const char *w) { printf("closed: %s\n", w); }

void samp_plat_message(const char *t)
{
    if (strstr(t, "CB ")) {
        printf("server: %s\n", t);
        cb_enter += strstr(t, "enter_checkpoint") != 0;
        cb_leave += strstr(t, "leave_checkpoint") != 0;
        cb_race_in += strstr(t, "enter_race") != 0;
        cb_race_out += strstr(t, "leave_race") != 0;
        cb_pickup += strstr(t, "near=1") != 0;
        if (strstr(t, "actor_damage")) {
            cb_actor++;
            strncpy(last_actor, t, sizeof last_actor - 1);
        }
    }
}

static unsigned now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000u + t.tv_nsec / 1000000u;
}

static void place(float x, float y, float z) { fg.me[0] = x; fg.me[1] = y; fg.me[2] = z; }

static int failed;
static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failed += !ok;
}

int main(int c, char **v)
{
    static unsigned char buf[2048];
    struct pollfd pf;
    unsigned start, step = 0, step_at = 0;

    if (c < 4)
        return 2;
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    srv.sin_family = AF_INET;
    srv.sin_port = htons(atoi(v[2]));
    inet_pton(AF_INET, v[1], &srv.sin_addr);
    fg_reset();
    place(100.0f, 200.0f, 10.0f);
    feat_world_init(&handlers);
    samp_rpc_set_handlers(&handlers);
    samp_connect(ntohl(srv.sin_addr.s_addr), atoi(v[2]), v[3], "");
    pf.fd = sock;
    pf.events = POLLIN;
    start = now_ms();
    while (now_ms() - start < 30000) {
        unsigned now = now_ms();
        if (poll(&pf, 1, 10) > 0) {
            int n = recv(sock, buf, sizeof buf, 0);
            if (n > 0)
                samp_datagram(buf, n, now);
        }
        samp_tick(now);
        feat_world_tick(now);
        feat_world_draw();
        fg_frame();
        if (samp_state() != SAMP_SPAWNED)
            continue;
        if (!step_at)
            step_at = now;
        if (now - step_at < 2500)
            continue;
        step_at = now;
        switch (step++) {
        case 0:
            printf("objects live %d peak %d, pickups live %d, actors %d, blips %d, created %d\n",
                   fg.objects_live, fg.objects_peak, fg.pickups_live, fg.peds_live, fg.blips_live, fg.create_calls);
            check(fg.objects_live > 0 && fg.objects_peak <= 130 + 24, "objects capped: 130 plus 24 attached");
            check(fg.pickups_live > 0 && fg.pickups_peak <= 96, "pickups capped under 96");
            check(fg.peds_live == 1, "actor in the game");
            check(fg.spheres > 0, "checkpoint sphere drawn");
            check(fg.race_live == 1, "race checkpoint in the game");
            check(fg.blips_live == 4, "two checkpoint blips and two icons");
            check(fg.gravity > 0.0119f && fg.gravity < 0.0121f, "gravity 0.012 applied");
            check(fg.last_weather == 8, "weather 8");
            check(fg.clock_hour == 21 && fg.clock_minute == 30, "clock held at 21:30");
            check(fg.clock_visible == 0, "clock hidden");
            check(fg.sounds == 1 && fg.last_sound == 1058, "sound played");
            check(fg.explosions == 1 && fg.last_explosion == 2, "explosion");
            check(fg.area_calls > 0, "gang zone drawn");
            check(fg.texts > 0, "label drawn");
            printf("label log: %.80s\n", fg.text_log);
            check(strstr(fg.text_log, "FF0000FF|Red") != 0, "label colour tag applied");
            {
                float p[3] = {101.0f, 204.0f, 10.0f};
                int h = fg_object_at(p);
                check(h && fg_object_model(h) == 12719, "PC id 18450 created as PS2 12719");
            }
            check(fg.entry_exits == 1, "InitGame reached the world code");
            check(fg.bad_handle == 0 && fg.bad_model == 0 && fg.pool_overrun == 0, "no bad calls so far");
            place(130.0f, 200.0f, 10.0f);
            break;
        case 1:
            check(cb_enter == 1, "OnPlayerEnterCheckpoint fired from sync alone");
            place(145.0f, 200.0f, 10.0f);
            break;
        case 2:
            check(cb_leave == 1, "OnPlayerLeaveCheckpoint fired");
            place(160.0f, 200.0f, 10.0f);
            break;
        case 3:
            check(cb_race_in == 1, "OnPlayerEnterRaceCheckpoint fired");
            place(100.0f, 200.0f, 10.0f);
            break;
        case 4: {
            float p[3] = {105.0f, 200.0f, 10.0f};
            check(cb_race_out == 1, "OnPlayerLeaveRaceCheckpoint fired");
            check(fg_pickup_handle_at(p) != 0, "the near pickup is in the game");
            fg_collect(fg_pickup_handle_at(p));
            break;
        }
        case 5:
            check(cb_pickup == 1, "OnPlayerPickUpPickup fired with the right id");
            fg_hit_actor((0 + 1) << 8 | 5, 30.0f);
            break;
        case 6:
            check(cb_actor == 1, "OnPlayerGiveDamageActor fired");
            printf("%s\n", last_actor);
            check(strstr(last_actor, "actor_damage 1 30.0 24 3") != 0, "actor, amount, weapon and part intact");
            check(fg.bad_handle == 0 && fg.bad_model == 0 && fg.pool_overrun == 0, "no bad calls at the end");
            samp_disconnect();
            printf("SUMMARY %d failed\n", failed);
            return failed != 0;
        }
    }
    printf("SUMMARY timed out at step %u, %d failed\n", step, failed);
    return 1;
}
