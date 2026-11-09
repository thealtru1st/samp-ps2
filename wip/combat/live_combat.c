/* The patched samp.c, rak.c and all of feat_combat on the host, over the fake game memory
 * of fake.c, against a real private server running combat.pwn. Two runs of this program
 * are the two players:
 *
 *   live_combat <ip> <port> <name> <seconds> victim|shooter|actor
 *
 * "victim" plays the PS2 client: its pad and ped are scripted, the rest is the real path.
 * "shooter" stands in for a PC player and sends a bullet sync hit on player 0 by hand.
 * "actor" is a player to watch from the game: see wip/combat/ingame-tests.md.
 * Everything the server made of it is in its log: see check_live.py. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "fake.h"
#include "feat.h"
#include "feat_combat.h"
#include "rak.h"
#include "bitstream.h"
#include "game.h"
#include "scm.h"

#define GP 0x0066B170u
#define POOL_STRUCT 0x800000u
#define POOL_FLAGS  0x800100u
#define POOL_OBJS   0x810000u
#define PED(i)      (POOL_OBJS + (i) * 0x810u)
#define MATRIX      0x7F0000u
#define ME          PED(0)
#define PAD         0x700BC0u
#define W32(a, v) (*(unsigned *)(cg_mem + (a)) = (unsigned)(v))
#define WF(a, v)  (*(float *)(cg_mem + (a)) = (v))
#define W8(a, v)  (cg_mem[a] = (v))
#define RF(a)     (*(float *)(cg_mem + (a)))
#define PAD_BTN(i) (*(short *)(cg_mem + PAD + (i) * 2))

static int sock;
static struct sockaddr_in srv;
static const char *mode;
static int next_ped = 3;

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
    int i;
    memset(l, 0, sizeof *l);
    l->quat[0] = 1.0f;
    for (i = 0; i < 3; i++)
        l->pos[i] = RF(MATRIX + 0x30 + i * 4);
    l->health = (unsigned char)RF(ME + 0x580);
    l->armour = (unsigned char)RF(ME + 0x588);
    feat_combat_local(l); /* what the integration adds to game_read_local */
}

void samp_plat_player_add(unsigned id, unsigned skin, const float *p, float a)
{
    int s = next_ped++ % 100 + 3;
    W8(POOL_FLAGS + s, 5);
    W32(PED(s) + 0x14, MATRIX + 0x100 * s);
    WF(PED(s) + 0x580, 100.0f);
    W8(PED(s) + 0x7A0, 0xFF);
    fake_handles[id] = s << 8 | 5;
    printf("stream in %u\n", id);
}
void samp_plat_player_del(unsigned id) { fake_handles[id] = 0; }
void samp_plat_player_sync(unsigned id, const float *p, const float *q, unsigned k, int m) {}
void samp_plat_set_pos(const float *p) {}
void samp_plat_set_skin(unsigned s) {}
void samp_plat_message(const char *t) {}
void samp_plat_closed(const char *w) { printf("closed: %s\n", w); }

static unsigned now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000u + t.tv_nsec / 1000000u;
}

static void world(void)
{
    int i;
    W32(GP + 0x828, POOL_STRUCT);
    W32(POOL_STRUCT, POOL_OBJS);
    W32(POOL_STRUCT + 4, POOL_FLAGS);
    W32(POOL_STRUCT + 8, 140);
    for (i = 0; i < 140; i++)
        W8(POOL_FLAGS + i, 0x80);
    W8(POOL_FLAGS, 1);
    scm_set_local(1, 1);
    W32(0x00709650, ME);
    W32(ME + 0x14, MATRIX);
    WF(MATRIX + 0x30, 100.0f); WF(MATRIX + 0x34, 200.0f); WF(MATRIX + 0x38, 10.0f);
    WF(ME + 0x580, 100.0f);
    W8(ME + 0x7A0, 0xFF);
    W32(GP + 0xB8, 1);
    memcpy(cg_mem + 0x677E30, "PED", 4);
    W8(0x677E30 + 0x10, 1);
    for (i = 0; i < 300; i++)
        W32(0x7A8800 + i * 4, 1);
}

/* The bits of the game the fake has to act out: weapons land in their slots, a 0 health
 * command kills, and the wasted state ends a few seconds later. */
static unsigned died_at;

static int slot_of(unsigned w) { return w >= 30 && w <= 31 ? 5 : w >= 22 && w <= 24 ? 2 : 0; }

static void act_out(unsigned now)
{
    int i;
    for (i = 0; i < fake_nops; i++) {
        fake_op_t *o = &fake_ops[i];
        if (!strcmp(mode, "stepper"))
            printf("op %04X\n", o->op);
        if (o->op == 0x01B2) {
            W32(ME + 0x5E0 + slot_of(o->i[1]) * 0x1C, o->i[1]);
            W32(ME + 0x5E0 + slot_of(o->i[1]) * 0x1C + 12, o->i[2]);
        } else if (o->op == 0x01B9) {
            W8(ME + 0x758, slot_of(o->i[1]));
        } else if (o->op == 0x048F) {
            memset(cg_mem + ME + 0x5E0, 0, 13 * 0x1C);
        } else if (o->op == 0x0109) {
            *(int *)(cg_mem + 0x00709650 + 0xE4) += o->i[1];
        } else if (o->op == 0x0223 && o->i[1] == 0 && !died_at) {
            W32(ME + 0x568, 55);
            WF(ME + 0x580, 0.0f);
            W8(GP + 0xAB4, 1);
            died_at = now;
            printf("died\n");
            fflush(stdout);
        }
    }
    fake_nops = 0;
    if (died_at && now - died_at > 3000) {
        died_at = 0;
        W32(ME + 0x568, 1);
        WF(ME + 0x580, 100.0f);
        W8(GP + 0xAB4, 0);
        printf("restart\n");
    }
}

static void victim_script(unsigned t)
{
    static int step;
    switch (step) {
    case 0:
        if (t < 1500) return;
        /* run forward, then sprint */
        cg_host_ud = -127;
        PAD_BTN(16) = 255;
        break;
    case 1:
        if (t < 2500) return;
        /* aim with the deagle, the camera in weapon mode */
        cg_host_ud = 0;
        PAD_BTN(16) = 0;
        PAD_BTN(6) = 255;
        *(short *)(cg_mem + 0x6FE5B0 + 0x180) = 53;
        WF(0x6FE5B0 + 0x304, 1.0f);
        WF(0x6FE5B0 + 0x310, 100.0f);
        WF(0x6FE5B0 + 0x314, 200.0f);
        WF(0x6FE5B0 + 0x318, 11.0f);
        break;
    case 2:
        if (t < 3500) return;
        /* one shot that misses */
        PAD_BTN(17) = 255;
        {
            unsigned a = ME + 0x5E0 + (cg_mem[ME + 0x758]) * 0x1C + 12;
            W32(a, *(unsigned *)(cg_mem + a) - 1);
        }
        break;
    case 3:
        if (t < 4000) return;
        PAD_BTN(17) = 0;
        PAD_BTN(6) = 0;
        *(short *)(cg_mem + 0x6FE5B0 + 0x180) = 4;
        /* a fall */
        WF(ME + 0x580, RF(ME + 0x580) - 10.0f);
        W8(ME + 0x7A0, 54);
        W32(ME + 0x7A4, 0);
        break;
    case 4:
        if (t < 4500) return;
        /* crouch */
        W8(ME + 0x4A7, 4);
        break;
    case 5:
        if (t < 5000) return;
        W8(ME + 0x4A7, 0);
        /* our shot lands on the other player */
        if (fake_handles[1]) {
            int s = fake_handles[1] >> 8;
            unsigned a = ME + 0x5E0 + (cg_mem[ME + 0x758]) * 0x1C + 12;
            W8(PED(s) + 0x7A0, cg_mem[ME + 0x5E0 + cg_mem[ME + 0x758] * 0x1C]);
            W32(PED(s) + 0x7A4, ME);
            WF(PED(s) + 0x580, 960.0f);
            W32(a, *(unsigned *)(cg_mem + a) - 1);
        }
        break;
    default:
        return;
    }
    step++;
}

/* A player for the in-game tests to look at: stands near the spawn and goes round every
 * animation and weapon state, in a 30 second loop. */
static void actor_script(unsigned now)
{
    static unsigned last_shot, cycle_start;
    unsigned t;
    float *x = (float *)(cg_mem + MATRIX + 0x30);

    if (!cycle_start || now - cycle_start >= 30000) {
        cycle_start = now;
        printf("cycle\n");
    }
    t = now - cycle_start;
    memset(cg_mem + PAD, 0, 0x28);
    cg_host_lr = cg_host_ud = 0;
    W8(ME + 0x4A7, 0);
    *(short *)(cg_mem + 0x6FE5B0 + 0x180) = 4;
    if (t >= 5000 && t < 10000) {
        cg_host_ud = -40;
        x[0] += 0.02f;
    } else if (t >= 10000 && t < 15000) {
        cg_host_ud = -127;
        PAD_BTN(16) = 255;
        x[0] -= 0.05f;
    } else if (t >= 15000 && t < 25000) {
        PAD_BTN(6) = 255;
        *(short *)(cg_mem + 0x6FE5B0 + 0x180) = 53;
        WF(0x6FE5B0 + 0x304, 0.0f);
        WF(0x6FE5B0 + 0x308, 1.0f);
        WF(0x6FE5B0 + 0x310, x[0]);
        WF(0x6FE5B0 + 0x314, x[1]);
        WF(0x6FE5B0 + 0x318, x[2] + 1.0f);
        if (t >= 20000) {
            PAD_BTN(17) = 255;
            if (now - last_shot >= 700) {
                unsigned a = ME + 0x5E0 + cg_mem[ME + 0x758] * 0x1C + 12;
                last_shot = now;
                if (*(unsigned *)(cg_mem + a))
                    W32(a, *(unsigned *)(cg_mem + a) - 1);
                /* every third shot lands on player 0 */
                if (fake_handles[0] && (now / 700) % 3 == 0) {
                    int s = fake_handles[0] >> 8;
                    W8(PED(s) + 0x7A0, cg_mem[ME + 0x5E0 + cg_mem[ME + 0x758] * 0x1C]);
                    W32(PED(s) + 0x7A4, ME);
                    WF(PED(s) + 0x580, 960.0f);
                }
            }
        }
    } else if (t >= 25000 && t < 27000) {
        W8(ME + 0x4A7, 4);
    } else if (t >= 27000 && t < 27300) {
        PAD_BTN(14) = 255;
    }
}

/* Presses R3 through every step of combat.pwn, the way ingame-tests.md does by hand. */
static void stepper_script(unsigned now)
{
    static unsigned last;
    static int down;
    if (now - last < 450)
        return;
    last = now;
    down = !down;
    PAD_BTN(19) = down ? 255 : 0;
}

static void shooter_script(unsigned t)
{
    static const float f3[3] = {100, 200, 10}, zero[3];
    static unsigned last;
    static unsigned char buf[64];
    bs_t b;
    /* three deagle hits on player 0, from second 7: enough to kill them */
    if (t < 7000 || t > 9500 || t - last < 500)
        return;
    last = t;
    bs_wr_init(&b, buf, sizeof buf);
    srpc_enc_sync_bullet(&b, 1, 0, f3, f3, zero, 24);
    rak_send(buf, b.bits, RAK_UNRELIABLE_SEQ, 0);
}

int main(int c, char **v)
{
    int port = atoi(v[2]), secs = atoi(v[4]);
    unsigned char buf[2048];
    unsigned start = now_ms(), spawned_at = 0;

    mode = v[5];
    world();
    feat_init();
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    srv.sin_family = AF_INET;
    srv.sin_port = htons(port);
    inet_pton(AF_INET, v[1], &srv.sin_addr);
    samp_connect(ntohl(srv.sin_addr.s_addr), port, v[3], "");
    while (now_ms() - start < (unsigned)secs * 1000) {
        struct pollfd p = {sock, POLLIN, 0};
        unsigned now;
        if (poll(&p, 1, 5) > 0) {
            int n = recv(sock, buf, sizeof buf, 0);
            if (n > 0)
                samp_datagram(buf, n, now_ms());
        }
        now = now_ms();
        samp_tick(now);
        feat_tick(now);
        act_out(now);
        if (samp_state() == SAMP_SPAWNED) {
            if (!spawned_at)
                spawned_at = now;
            if (!strcmp(mode, "victim"))
                victim_script(now - spawned_at);
            else if (!strcmp(mode, "actor"))
                actor_script(now);
            else if (!strcmp(mode, "stepper"))
                stepper_script(now);
            else
                shooter_script(now - spawned_at);
        }
    }
    samp_disconnect();
    usleep(300000);
    return 0;
}
