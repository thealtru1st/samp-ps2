#include "bitstream.h"
#include "util.h"

void bs_wr_init(bs_t *b, void *buf, int cap_bytes)
{
    b->d = buf; b->bits = 0; b->rd = 0; b->cap = cap_bytes * 8; b->err = 0;
}

void bs_rd_init(bs_t *b, const void *buf, int bits)
{
    b->d = (unsigned char *)buf; b->bits = bits; b->rd = 0; b->cap = bits; b->err = 0;
}

void bs_wbits(bs_t *b, const void *in, int n, int right)
{
    const unsigned char *p = in;
    int mod = b->bits & 7, off = 0;

    if (n <= 0)
        return;
    if (b->bits + n > b->cap) {
        b->err = 1;
        return;
    }
    while (n > 0) {
        unsigned char c = p[off];
        if (n < 8 && right)
            c <<= 8 - n;
        if (mod == 0) {
            b->d[b->bits >> 3] = c;
        } else {
            b->d[b->bits >> 3] |= c >> mod;
            if (8 - mod < n)
                b->d[(b->bits >> 3) + 1] = (unsigned char)(c << (8 - mod));
        }
        b->bits += n >= 8 ? 8 : n;
        n -= 8;
        off++;
    }
}

void bs_wbool(bs_t *b, int v)
{
    unsigned char c = v ? 1 : 0;
    bs_wbits(b, &c, 1, 1);
}

void bs_w8(bs_t *b, unsigned v)
{
    unsigned char c = v;
    bs_wbits(b, &c, 8, 1);
}

void bs_w16(bs_t *b, unsigned v)
{
    unsigned char c[2] = {v, v >> 8};
    bs_wbits(b, c, 16, 1);
}

void bs_w32(bs_t *b, unsigned v)
{
    unsigned char c[4] = {v, v >> 8, v >> 16, v >> 24};
    bs_wbits(b, c, 32, 1);
}

void bs_wf(bs_t *b, float v)
{
    union { float f; unsigned u; } x;
    x.f = v;
    bs_w32(b, x.u);
}

void bs_wbytes(bs_t *b, const void *p, int n)
{
    bs_wbits(b, p, n * 8, 1);
}

void bs_walign(bs_t *b)
{
    if (b->bits)
        b->bits += 8 - (((b->bits - 1) & 7) + 1);
}

static void wcomp(bs_t *b, const unsigned char *in, int size)
{
    int cur = (size >> 3) - 1;
    while (cur > 0) {
        if (in[cur] == 0) {
            bs_wbool(b, 1);
        } else {
            bs_wbool(b, 0);
            bs_wbits(b, in, (cur + 1) << 3, 1);
            return;
        }
        cur--;
    }
    if ((in[cur] & 0xF0) == 0) {
        bs_wbool(b, 1);
        bs_wbits(b, in + cur, 4, 1);
    } else {
        bs_wbool(b, 0);
        bs_wbits(b, in + cur, 8, 1);
    }
}

void bs_wcomp16(bs_t *b, unsigned v)
{
    unsigned char c[2] = {v, v >> 8};
    wcomp(b, c, 16);
}

void bs_wcomp32(bs_t *b, unsigned v)
{
    unsigned char c[4] = {v, v >> 8, v >> 16, v >> 24};
    wcomp(b, c, 32);
}

int bs_unread(const bs_t *b) { return b->bits - b->rd; }

int bs_rbits(bs_t *b, void *out, int n, int right)
{
    unsigned char *o = out;
    int mod = b->rd & 7, off = 0;

    if (n <= 0 || bs_unread(b) < n) {
        b->err = 1;
        return 0;
    }
    memset(o, 0, BS_BYTES(n));
    while (n > 0) {
        o[off] |= b->d[b->rd >> 3] << mod;
        if (mod > 0 && n > 8 - mod)
            o[off] |= b->d[(b->rd >> 3) + 1] >> (8 - mod);
        n -= 8;
        if (n < 0) {
            if (right)
                o[off] >>= -n;
            b->rd += 8 + n;
        } else {
            b->rd += 8;
        }
        off++;
    }
    return 1;
}

int bs_rbool(bs_t *b)
{
    unsigned char c = 0;
    bs_rbits(b, &c, 1, 1);
    return c;
}

unsigned bs_r8(bs_t *b)
{
    unsigned char c = 0;
    bs_rbits(b, &c, 8, 1);
    return c;
}

unsigned bs_r16(bs_t *b)
{
    unsigned char c[2] = {0, 0};
    bs_rbits(b, c, 16, 1);
    return c[0] | (c[1] << 8);
}

unsigned bs_r32(bs_t *b)
{
    unsigned char c[4] = {0, 0, 0, 0};
    bs_rbits(b, c, 32, 1);
    return c[0] | (c[1] << 8) | (c[2] << 16) | ((unsigned)c[3] << 24);
}

float bs_rf(bs_t *b)
{
    union { float f; unsigned u; } x;
    x.u = bs_r32(b);
    return x.f;
}

int bs_rbytes(bs_t *b, void *p, int n)
{
    return n > 0 ? bs_rbits(b, p, n * 8, 1) : 1;
}

void bs_ralign(bs_t *b)
{
    if (b->rd)
        b->rd += 8 - (((b->rd - 1) & 7) + 1);
}

static void rcomp(bs_t *b, unsigned char *o, int size)
{
    int cur = (size >> 3) - 1;
    while (cur > 0) {
        if (bs_rbool(b)) {
            o[cur] = 0;
            cur--;
        } else {
            bs_rbits(b, o, (cur + 1) << 3, 1);
            return;
        }
    }
    if (bs_rbool(b))
        bs_rbits(b, o + cur, 4, 1);
    else
        bs_rbits(b, o + cur, 8, 1);
}

unsigned bs_rcomp16(bs_t *b)
{
    unsigned char c[2] = {0, 0};
    rcomp(b, c, 16);
    return c[0] | (c[1] << 8);
}

unsigned bs_rcomp32(bs_t *b)
{
    unsigned char c[4] = {0, 0, 0, 0};
    rcomp(b, c, 32);
    return c[0] | (c[1] << 8) | (c[2] << 16) | ((unsigned)c[3] << 24);
}
