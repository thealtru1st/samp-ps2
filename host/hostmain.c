/* Host harness: runs the exact payload protocol code against a real server. */
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

static int sock;
static struct sockaddr_in srv;
static float px, py, pz, pace, pace_dir = 0.3f;

void slog(const char *f, ...)
{
    va_list ap; va_start(ap, f); vprintf(f, ap); va_end(ap); putchar('\n'); fflush(stdout);
}
void samp_plat_send(const unsigned char *d, unsigned n)
{
    sendto(sock, d, n, 0, (struct sockaddr *)&srv, sizeof srv);
}
void samp_plat_local(samp_local_t *l)
{
    memset(l, 0, sizeof *l);
    l->pos[0] = px; l->pos[1] = py; l->pos[2] = pz;
    l->quat[0] = 1.0f;
    l->health = 100;
    pace += pace_dir;
    if (pace > 10 || pace < -10) pace_dir = -pace_dir;
    l->pos[0] = px + pace;
    l->ud = 0xFF80;
}
int samp_plat_in_vehicle(void) { return 0; }
void samp_plat_player_add(unsigned id, unsigned skin, const float *p, float a) { printf("stream in %u skin %u at %.1f %.1f %.1f\n", id, skin, p[0], p[1], p[2]); }
void samp_plat_player_del(unsigned id) { printf("stream out %u\n", id); }
void samp_plat_player_sync(unsigned id, const float *p, const float *q, unsigned k, int m) {}
void samp_plat_set_pos(const float *p) { printf("server set pos %.1f %.1f %.1f\n", p[0], p[1], p[2]); }
void samp_plat_set_skin(unsigned s) { printf("server set skin %u\n", s); }
void samp_plat_message(const char *t) { printf("chat: %s\n", t); }
void samp_plat_closed(const char *w) { printf("closed: %s\n", w); }
static unsigned now_ms(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000u + t.tv_nsec / 1000000u;
}
int main(int c, char **v)
{
    const char *ip = v[1], *name = c > 3 ? v[3] : "PS2_Test";
    int port = atoi(v[2]), secs = c > 4 ? atoi(v[4]) : 15, said = 0;
    unsigned char buf[2048];
    unsigned start = now_ms(), last = 0;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    srv.sin_family = AF_INET; srv.sin_port = htons(port); inet_pton(AF_INET, ip, &srv.sin_addr);
    samp_connect(ntohl(srv.sin_addr.s_addr), port, name, "");
    while (now_ms() - start < (unsigned)secs * 1000) {
        struct pollfd p = {sock, POLLIN, 0};
        if (poll(&p, 1, 10) > 0) {
            int n = recv(sock, buf, sizeof buf, 0);
            if (n > 0) samp_datagram(buf, n, now_ms());
        }
        samp_tick(now_ms());
        if (samp_state() == SAMP_SPAWNED) {
            if (!said) { samp_chat("hello from the PS2 client code"); said = 1; }
            if (px == 0) { px = c > 5 ? atof(v[5]) : samp_stats.spawn[0]; py = c > 6 ? atof(v[6]) : samp_stats.spawn[1]; pz = c > 7 ? atof(v[7]) : samp_stats.spawn[2]; }
        }
        if (now_ms() - last > 2000) {
            last = now_ms();
            printf("state=%d rpcs=%u foot_in=%u from player %u at %.2f %.2f %.2f\n", samp_state(), samp_stats.rpcs, samp_stats.foot_in, samp_stats.last_pid, samp_stats.last_pos[0], samp_stats.last_pos[1], samp_stats.last_pos[2]);
            fflush(stdout);
        }
    }
    samp_disconnect();
    usleep(300000);
    return 0;
}
