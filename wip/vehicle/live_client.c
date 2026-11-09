/* Live test: the real samp.c (with integration.patch), rak.c, samp_rpc.c and feat_vehicle.c
 * against a real open.mp server. Only the game is fake (fake_game.c), so this shows what
 * goes over the wire and what would reach the game, not how it looks.
 *
 *   veh_live <ip> <port> driver|watcher <seconds>
 *
 * The driver walks through a fixed script against vehtest.pwn. The watcher stands by and
 * reports what it sees of the driver's car. Lines starting "EV" go to live_check.py. */
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
#include "rak.h"
#include "feat.h"
#include "feat_vehicle.h"
#include "fake_game.h"
#include "feat_vehicle_math.h"
#include "feat_vehicle.c"

static int sock;
static struct sockaddr_in srv;
static int driver_mode;
static float me[3], ped_quat[4] = {1, 0, 0, 0};
static unsigned now_v, spawned_at;
static int peds[1000];

static int car_id, truck_id, trailer_id, cop_id, bike_id, empty_id;

void slog(const char *f, ...)
{
    va_list ap;
    va_start(ap, f); vprintf(f, ap); va_end(ap);
    putchar('\n');
    fflush(stdout);
}

int game_remote_handle(unsigned id) { return id < 1000 ? peds[id] : 0; }
void game_remote_hold(unsigned id, int on) { printf("EV hold %u %d\n", id, on); }

void samp_plat_send(const unsigned char *d, unsigned n) { sendto(sock, d, n, 0, (struct sockaddr *)&srv, sizeof srv); }

void samp_plat_local(samp_local_t *l)
{
    memset(l, 0, sizeof *l);
    memcpy(l->pos, me, sizeof l->pos);
    memcpy(l->quat, ped_quat, sizeof l->quat);
    l->health = 100;
}

int samp_plat_in_vehicle(void) { return feat_vehicle_in_car(); }

void samp_plat_player_add(unsigned id, unsigned skin, const float *p, float a)
{
    if (id < 1000) {
        peds[id] = fake_new_ped();
        printf("EV player_in %u ped %x\n", id, peds[id]);
    }
}

void samp_plat_player_del(unsigned id)
{
    if (id < 1000 && peds[id]) {
        fake_ped_t *p = fake_ped(peds[id]);
        if (p && p->car) {
            float z[3] = {0, 0, 0};
            vg_warp_out(peds[id], z);
        }
        if (p) p->used = 0;
        peds[id] = 0;
    }
}

void samp_plat_player_sync(unsigned id, const float *p, const float *q, unsigned k, int m)
{
    feat_vehicle_foot_sync(id);
}

void samp_plat_set_pos(const float *p)
{
    feat_vehicle_eject(p);
    memcpy(me, p, sizeof me);
    memcpy(fake_local_at, p, sizeof me);
    printf("EV set_pos %.1f %.1f %.1f\n", p[0], p[1], p[2]);
}

void samp_plat_set_skin(unsigned s) {}

void samp_plat_message(const char *t)
{
    printf("chat: %s\n", t);
    if (!strncmp(t, "CB ids", 6))
        sscanf(t, "CB ids car %d truck %d trailer %d cop %d bike %d empty %d", &car_id, &truck_id, &trailer_id, &cop_id, &bike_id, &empty_id);
    fflush(stdout);
}

void samp_plat_closed(const char *w) { printf("closed: %s\n", w); }

static unsigned now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000u + t.tv_nsec / 1000000u;
}

static fake_car_t *car(int id) { return id > 0 && id < VEH_IDS ? fake_car(V[id].handle) : 0; }

static void report_car(const char *tag, int id)
{
    fake_car_t *c = car(id);
    if (!c) {
        printf("EV %s %d none\n", tag, id);
        return;
    }
    printf("EV %s %d model %d at %.2f %.2f %.2f quat %.3f %.3f %.3f %.3f health %.0f c %u %u plate '%s' paint %d mods %d %d %d "
           "panels %x doors %x lights %x tyres %x interior %u engine %d lights %d locked %d alarm %d door %d %d %d siren %d driver %x towed %x vel %.2f %.2f %.2f turn %.3f %.3f %.3f\n",
           tag, id, c->model, c->s.pos[0], c->s.pos[1], c->s.pos[2], c->s.quat[0], c->s.quat[1], c->s.quat[2], c->s.quat[3],
           c->health, c->c1, c->c2, c->plate, c->paintjob, c->mods[0], c->mods[1], c->mods[2], c->panels, c->doors, c->lights_dmg,
           c->tyres, c->interior, c->engine, c->lights, c->locked, c->alarm, c->door[0], c->door[2], c->door[3], c->siren, c->driver,
           c->towed, c->s.vel[0], c->s.vel[1], c->s.vel[2], c->s.turn[0], c->s.turn[1], c->s.turn[2]);
}

static int step;
static float heading_q[4] = {0.70710678f, 0, 0, -0.70710678f}; /* heading 90, on the wire */

static void cmd(const char *c) { samp_chat(c); }

/* One entry per moment in the script. Runs in order, each at its own time. */
static void driver_script(unsigned t)
{
    fake_car_t *c;

    if (step == 0 && t > 1500 && car_id) {
        report_car("seen", car_id);
        report_car("seen", truck_id);
        report_car("seen", cop_id);
        report_car("seen", bike_id);
        report_car("seen", empty_id);
        cmd("/vdamage");
        cmd("/vparams");
        cmd("/vcolour");
        cmd("/vmod");
        cmd("/vinterior");
        cmd("/vangle");
        step++;
    } else if (step == 1 && t > 2500) {
        report_car("changed", empty_id);
        if ((c = car(car_id))) {
            memcpy(fake_local_at, c->s.pos, 12);
            memcpy(me, c->s.pos, 12);
            fake_enter_car = c->handle; /* the game's enter task has started */
            fake_enter_passenger = 0;
        }
        step++;
    } else if (step == 2 && t > 3500) {
        fake_enter_car = 0;
        if ((c = car(car_id))) {
            vg_warp_driver(fake_local, c->handle);
            memcpy(c->s.quat, heading_q, sizeof heading_q);
            c->s.vel[0] = -0.2f;
            c->health = 850;
            fake_pad.cross = 1;
        }
        step++;
    } else if (step == 3 && t > 6000) {
        if ((c = car(car_id)))
            c->panels = 0x3300;
        step++;
    } else if (step == 4 && t > 7000) {
        cmd("/vvel");
        cmd("/vspin");
        step++;
    } else if (step == 5 && t > 7500) {
        report_car("velocity", car_id);
        cmd("/vangleget");
        cmd("/vhealth");
        step++;
    } else if (step == 6 && t > 8000) {
        report_car("health", car_id);
        cmd("/vrepair");
        step++;
    } else if (step == 7 && t > 8500) {
        report_car("repaired", car_id);
        cmd("/vpos");
        step++;
    } else if (step == 8 && t > 9000) {
        report_car("moved", car_id);
        fake_leaving = 1; /* Triangle */
        fake_pad.cross = 0;
        step++;
    } else if (step == 9 && t > 9500) {
        c = car(car_id);
        fake_leaving = 0;
        if (c) {
            c->s.vel[0] = c->s.vel[1] = c->s.vel[2] = 0;
            memcpy(me, c->s.pos, 12);
            me[0] += 3;
            memcpy(fake_local_at, me, 12);
            vg_warp_out(fake_local, me);
        }
        step++;
    } else if (step == 10 && t > 10500) {
        if ((c = car(empty_id))) {
            memcpy(me, c->s.pos, 12); /* stand next to the empty car and shove it */
            memcpy(fake_local_at, me, 12);
            c->s.vel[1] = 0.1f;
        }
        step++;
    } else if (step == 11 && t > 13500) {
        if ((c = car(empty_id)))
            c->s.vel[1] = 0;
        cmd("/vputtruck");
        step++;
    } else if (step == 12 && t > 15000) {
        report_car("truck", truck_id);
        cmd("/vattach");
        step++;
    } else if (step == 13 && t > 16500) {
        report_car("towing", truck_id);
        if ((c = car(trailer_id)))
            c->s.vel[1] = 0.05f;
        cmd("/vdetach");
        step++;
    } else if (step == 14 && t > 17500) {
        report_car("towing_done", truck_id);
        cmd("/vremove");
        step++;
    } else if (step == 15 && t > 18000) {
        fake_ped_t *p = fake_ped(fake_local);
        printf("EV leave_task %d\n", p->leaving);
        p->leaving = 0;
        if ((c = car(truck_id)))
            vg_warp_out(fake_local, c->s.pos);
        step++;
    } else if (step == 16 && t > 19000) {
        cmd("/vpassenger");
        step++;
    } else if (step == 17 && t > 20500) {
        c = car(truck_id);
        printf("EV seats driver %x p0 %x me %x\n", c ? c->driver : -1, c ? c->passenger[0] : -1, fake_local);
        cmd("/vfar");
        step++;
    } else if (step == 18 && t > 22000) {
        printf("EV in_car %d\n", feat_vehicle_in_car());
        report_car("gone", car_id);
        cmd("/vback");
        step++;
    } else if (step == 19 && t > 24500) {
        report_car("back", car_id);
        memcpy(ped_quat, heading_q, sizeof ped_quat); /* on foot, facing 90 on the wire */
        step++;
    } else if (step == 20 && t > 24900) {
        cmd("/vfaceget");
        step++;
    }
}

static int rep_at;

/* Reports the driver's car as this client sees it, on a timer. */
static void watcher_script(unsigned t)
{
    int want = (int)(t / 1000);
    int i;

    if (want == rep_at || !car_id)
        return;
    rep_at = want;
    report_car("watch", car_id);
    report_car("watch_empty", empty_id);
    report_car("watch_truck", truck_id);
    report_car("watch_trailer", trailer_id);
    for (i = 0; i < 1000; i++)
        if (peds[i]) {
            fake_ped_t *p = fake_ped(peds[i]);
            printf("EV watch_ped %d car %x\n", i, p ? p->car : -1);
        }
}

int main(int c, char **v)
{
    static samp_rpc_handlers_t handlers;
    unsigned char buf[2048];
    unsigned start, secs;
    const char *name;

    if (c < 5)
        return 2;
    driver_mode = !strcmp(v[3], "driver");
    name = driver_mode ? "PS2Driver" : "PS2Watcher";
    secs = atoi(v[4]);
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    srv.sin_family = AF_INET;
    srv.sin_port = htons(atoi(v[2]));
    inet_pton(AF_INET, v[1], &srv.sin_addr);
    fake_init();
    feat_vehicle_init(&handlers);
    samp_rpc_set_handlers(&handlers);
    samp_connect(ntohl(srv.sin_addr.s_addr), atoi(v[2]), name, "");
    start = now_ms();
    now_v = start;
    while (now_ms() - start < secs * 1000) {
        struct pollfd p = {sock, POLLIN, 0};
        unsigned t = now_ms();
        if (poll(&p, 1, 5) > 0) {
            int len = recv(sock, buf, sizeof buf, 0);
            if (len > 0)
                samp_datagram(buf, len, t);
        }
        samp_tick(t);
        if (t - now_v >= 33) {
            fake_car_t *cc;
            int i;
            float dt = (t - now_v) / 20.0f;
            now_v = t;
            fake_frame();
            /* The stand-in for physics: every car moves by its speed. */
            for (i = 1; i < FAKE_POOL; i++) {
                cc = &fake_cars[i];
                if (cc->used) {
                    cc->s.pos[0] += cc->s.vel[0] * dt;
                    cc->s.pos[1] += cc->s.vel[1] * dt;
                    cc->s.pos[2] += cc->s.vel[2] * dt;
                }
            }
            feat_vehicle_tick(t);
            if (samp_state() == SAMP_SPAWNED) {
                if (!spawned_at) {
                    spawned_at = t;
                    memcpy(me, samp_stats.spawn, sizeof me);
                    memcpy(fake_local_at, me, sizeof me);
                    if (!driver_mode)
                        me[1] -= 20, fake_local_at[1] -= 20;
                }
                if (driver_mode)
                    driver_script(t - spawned_at);
                else
                    watcher_script(t - spawned_at);
            }
        }
    }
    samp_disconnect();
    usleep(300000);
    printf("EV end stats live %d models %d peak %d\n", fake_stats.live, fake_stats.models_loaded, fake_stats.live_peak);
    return 0;
}
