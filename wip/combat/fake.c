/* Host stand-ins for everything feat_combat touches outside its own files: game memory,
 * the SCM interpreter, RakNet and game.c. Each SCM command is recorded with its arguments
 * so the test can assert on it and check_scm.py can compare it with ref/scm/sa.json. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fake.h"
#include "scm.h"
#include "rak.h"
#include "game.h"

unsigned char cg_mem[CG_MEM_SIZE];
int cg_host_lr, cg_host_ud, cg_host_anim_found = 1, cg_host_not_resident;

fake_op_t fake_ops[FAKE_OPS];
int fake_nops;
fake_msg_t fake_msgs[FAKE_MSGS];
int fake_nmsgs;
int fake_state = SAMP_SPAWNED;
int fake_handles[1000];
int fake_teleports, fake_skins, fake_last_skin;
float fake_teleport_pos[3];
int fake_remote_adds, fake_remote_dels;
static FILE *dump;

void fake_dump_to(const char *path) { dump = fopen(path, "w"); }

/* ---- scm ---- */
static unsigned char code[512];
static int locals[64];
static fake_op_t *cur;

void scm_begin(scm_t *s) { s->p = code; }

static void flush_op(void)
{
    if (!cur)
        return;
    if (dump) {
        fprintf(dump, "%04X %d\n", cur->op, cur->nargs);
        fflush(dump);
    }
    cur = 0;
}

void scm_op(scm_t *s, unsigned op)
{
    flush_op();
    if (fake_nops >= FAKE_OPS) {
        memmove(fake_ops, fake_ops + FAKE_OPS / 2, sizeof fake_ops[0] * (FAKE_OPS - FAKE_OPS / 2));
        fake_nops = FAKE_OPS - FAKE_OPS / 2;
    }
    cur = &fake_ops[fake_nops++];
    memset(cur, 0, sizeof *cur);
    cur->op = op;
    s->p += 2;
}

static void arg(int type, int i, float f, const char *t)
{
    if (!cur || cur->nargs >= 16)
        abort();
    cur->type[cur->nargs] = type;
    cur->i[cur->nargs] = i;
    cur->f[cur->nargs] = f;
    if (t)
        snprintf(cur->s[cur->nargs], sizeof cur->s[0], "%s", t);
    cur->nargs++;
}

void scm_i8(scm_t *s, int v) { if (v < -128 || v > 255) abort(); arg('i', v, 0, 0); s->p += 2; }
void scm_i32(scm_t *s, int v) { arg('i', v, 0, 0); s->p += 5; }
void scm_f(scm_t *s, float v) { arg('f', 0, v, 0); s->p += 5; }
void scm_str(scm_t *s, const char *t) { arg('s', 0, 0, t); s->p += 2 + strlen(t); }
void scm_local(scm_t *s, unsigned i) { arg('l', i, 0, 0); s->p += 3; }
void scm_raw(scm_t *s, const volatile unsigned char *p, unsigned n) { (void)p; s->p += n; }
int scm_get_local(unsigned i) { return locals[i]; }
void scm_set_local(unsigned i, int v) { locals[i] = v; }

void scm_run(scm_t *s)
{
    int k;
    if (s->p > code + sizeof code)
        abort(); /* the real buffer would have overflowed */
    flush_op();
    /* what the opcodes that return something would leave behind */
    for (k = fake_nops - 1; k >= 0 && k >= fake_nops - 4; k--) {
        fake_op_t *o = &fake_ops[k];
        union { float f; int i; } u;
        if (o->done)
            break;
        o->done = 1;
        switch (o->op) {
        case 0x0781: locals[o->i[1]] = o->i[0] ? 340 + o->i[0] : -1; break;
        case 0x060A: locals[o->i[1]] = 77; break;
        case 0x077E: locals[o->i[0]] = fake_area; break;
        case 0x02CE: u.f = 12.5f; locals[o->i[3]] = u.i; break;
        case 0x0801: u.f = 70.0f; locals[o->i[0]] = u.i; break;
        }
    }
}

int fake_area;

#ifndef FAKE_LIVE
/* ---- rak ---- */
static void store(int kind, int id, const void *d, int bits)
{
    fake_msg_t *m;
    if (fake_nmsgs >= FAKE_MSGS)
        return;
    m = &fake_msgs[fake_nmsgs++];
    m->kind = kind;
    m->id = id;
    m->bits = bits;
    memset(m->d, 0, sizeof m->d);
    if (bits > (int)sizeof m->d * 8)
        abort();
    if (bits)
        memcpy(m->d, d, (bits + 7) / 8);
}

int rak_rpc(int id, const void *d, int bits, int rel, int ch) { (void)rel; (void)ch; store('r', id, d, bits); return 0; }
int rak_send(const void *d, int bits, int rel, int ch)
{
    (void)rel; (void)ch;
    store('s', ((const unsigned char *)d)[0], d, bits);
    return 0;
}

int samp_state(void) { return fake_state; }
samp_stats_t samp_stats;
#endif

/* ---- game.c ---- */
void game_teleport(float x, float y, float z)
{
    fake_teleports++;
    fake_teleport_pos[0] = x; fake_teleport_pos[1] = y; fake_teleport_pos[2] = z;
}
void game_set_skin(int m) { fake_skins++; fake_last_skin = m; }
void game_remote_del(unsigned id) { fake_remote_dels++; (void)id; }
void game_remote_add(unsigned id, unsigned skin, const float *p, float a)
{
    fake_remote_adds++; (void)id; (void)skin; (void)p; (void)a;
}
int game_remote_handle(unsigned id) { return id < 1000 ? fake_handles[id] : 0; }
int feat_vehicle_handle(unsigned id) { (void)id; return 0; }
void feat_vehicle_eject(const float *pos) { (void)pos; }

#ifndef FAKE_LIVE
void slog(const char *f, ...) { (void)f; }
#endif


#ifndef FAKE_LIVE
void samp_spawn_sent(void) {}
void samp_reselect_class(void)
{
    static const unsigned char c[4];
    rak_rpc(128, c, 32, 0, 0);
}
#endif
