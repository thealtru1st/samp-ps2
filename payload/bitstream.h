#pragma once
/* RakNet-compatible bit stream. Bits fill each byte from the MSB; multi-byte
 * integers are little-endian on the wire. No allocation: caller owns the buffer. */
typedef struct {
    unsigned char *d;
    int bits; /* bits written, or total bits available when reading */
    int rd;   /* read offset in bits */
    int cap;  /* capacity in bits */
    int err;
} bs_t;

void bs_wr_init(bs_t *b, void *buf, int cap_bytes);
void bs_rd_init(bs_t *b, const void *buf, int bits);

void bs_wbits(bs_t *b, const void *in, int n, int right_aligned);
void bs_wbool(bs_t *b, int v);
void bs_w8(bs_t *b, unsigned v);
void bs_w16(bs_t *b, unsigned v);
void bs_w32(bs_t *b, unsigned v);
void bs_wf(bs_t *b, float v);
void bs_wbytes(bs_t *b, const void *p, int n);
void bs_wcomp16(bs_t *b, unsigned v);
void bs_wcomp32(bs_t *b, unsigned v);
void bs_walign(bs_t *b);

int      bs_rbits(bs_t *b, void *out, int n, int right_aligned);
int      bs_rbool(bs_t *b);
unsigned bs_r8(bs_t *b);
unsigned bs_r16(bs_t *b);
unsigned bs_r32(bs_t *b);
float    bs_rf(bs_t *b);
int      bs_rbytes(bs_t *b, void *p, int n);
unsigned bs_rcomp16(bs_t *b);
unsigned bs_rcomp32(bs_t *b);
void     bs_ralign(bs_t *b);
int      bs_unread(const bs_t *b);
#define BS_BYTES(bits) (((bits) + 7) >> 3)
