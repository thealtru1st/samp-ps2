#include "scm.h"
#include "util.h"
#include "status.h"

#define PROCESS_ONE_COMMAND 0x001FA870u

#define RS_BASE_IP 0x10
#define RS_IP      0x14
#define RS_LOCALS  0x3C

static unsigned char thread[0x100] __attribute__((aligned(16)));
static unsigned char code[512] __attribute__((aligned(16)));

void scm_begin(scm_t *s) { s->p = code; }

void scm_op(scm_t *s, unsigned op)
{
    *s->p++ = op;
    *s->p++ = op >> 8;
}

static void put32(scm_t *s, unsigned v)
{
    *s->p++ = v; *s->p++ = v >> 8; *s->p++ = v >> 16; *s->p++ = v >> 24;
}

void scm_i8(scm_t *s, int v) { *s->p++ = 4; *s->p++ = v; }
void scm_i32(scm_t *s, int v) { *s->p++ = 1; put32(s, v); }

void scm_f(scm_t *s, float v)
{
    union { float f; unsigned u; } x;
    x.f = v;
    *s->p++ = 6;
    put32(s, x.u);
}

void scm_str(scm_t *s, const char *t)
{
    unsigned n = strlen(t), i;
    *s->p++ = 0x0E;
    *s->p++ = n;
    for (i = 0; i < n; i++)
        *s->p++ = t[i];
}

void scm_local(scm_t *s, unsigned i)
{
    *s->p++ = 3;
    *s->p++ = i;
    *s->p++ = i >> 8;
}

void scm_raw(scm_t *s, const volatile unsigned char *p, unsigned n)
{
    while (n--)
        *s->p++ = *p++;
}

int scm_get_local(unsigned i) { return *(int *)(thread + RS_LOCALS + i * 4); }
void scm_set_local(unsigned i, int v) { *(int *)(thread + RS_LOCALS + i * 4) = v; }

void scm_run(scm_t *s)
{
    int (*one)(void *) = (int (*)(void *))PROCESS_ONE_COMMAND;
    unsigned end = (unsigned)s->p;

    ST->where_op = s->p > code + 1 ? code[0] | (code[1] << 8) : 0;
    *(unsigned *)(thread + RS_BASE_IP) = (unsigned)code;
    *(unsigned *)(thread + RS_IP) = (unsigned)code;
    while (*(unsigned *)(thread + RS_IP) < end)
        one(thread);
}
