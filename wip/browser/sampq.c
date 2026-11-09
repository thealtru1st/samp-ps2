#include "sampq.h"
#include "util.h"

unsigned sampq_build(unsigned char *out, unsigned ip, unsigned port, char op,
                     const unsigned char *nonce)
{
    out[0] = 'S'; out[1] = 'A'; out[2] = 'M'; out[3] = 'P';
    out[4] = ip >> 24; out[5] = ip >> 16; out[6] = ip >> 8; out[7] = ip;
    out[8] = port; out[9] = port >> 8;
    out[10] = op;
    if (op != 'p')
        return SAMPQ_HDR;
    memcpy(out + SAMPQ_HDR, nonce, 4);
    return SAMPQ_PING_LEN;
}

int sampq_check(const unsigned char *d, unsigned len, unsigned ip, unsigned port)
{
    unsigned char want[SAMPQ_HDR];

    if (len < SAMPQ_HDR)
        return -1;
    sampq_build(want, ip, port, 0, 0);
    if (memcmp(d, want, SAMPQ_HDR - 1))
        return -1;
    return d[10];
}

/* Server strings are in no fixed encoding, so bytes >= 0x80 pass through for the
 * UI to deal with. Control bytes never do: they would break a line of text. */
static void copy_clean(char *dst, unsigned cap, const unsigned char *src, unsigned len)
{
    unsigned i;
    if (len > cap - 1)
        len = cap - 1;
    for (i = 0; i < len; i++)
        dst[i] = (src[i] < 0x20 || src[i] == 0x7F) ? '?' : (char)src[i];
    dst[len] = 0;
}

static unsigned le16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned le32(const unsigned char *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

/* A u32-prefixed string. The length is compared against what is left rather
 * than added to the offset, so a huge value cannot wrap. */
static int take_str32(const unsigned char *d, unsigned len, unsigned *off,
                      char *dst, unsigned cap)
{
    unsigned n;
    if (len - *off < 4)
        return -1;
    n = le32(d + *off);
    *off += 4;
    if (n > len - *off)
        return -1;
    copy_clean(dst, cap, d + *off, n);
    *off += n;
    return 0;
}

int sampq_parse_info(const unsigned char *d, unsigned len, sampq_info_t *o)
{
    unsigned off = SAMPQ_HDR;

    if (len < SAMPQ_HDR + 5)
        return -1;
    o->password = d[off] != 0;
    o->players = le16(d + off + 1);
    o->max_players = le16(d + off + 3);
    off += 5;
    if (take_str32(d, len, &off, o->hostname, sizeof o->hostname) ||
        take_str32(d, len, &off, o->gamemode, sizeof o->gamemode) ||
        take_str32(d, len, &off, o->language, sizeof o->language))
        return -1;
    return 0;
}

int sampq_parse_ping(const unsigned char *d, unsigned len, const unsigned char *nonce)
{
    if (len != SAMPQ_PING_LEN || memcmp(d + SAMPQ_HDR, nonce, 4))
        return -1;
    return 0;
}

int sampq_parse_rules(const unsigned char *d, unsigned len, sampq_rule_t *out,
                      unsigned max, unsigned *stored, unsigned *total)
{
    unsigned off = SAMPQ_HDR + 2, count, i, n = 0;

    if (len < off)
        return -1;
    count = le16(d + SAMPQ_HDR);
    for (i = 0; i < count; i++) {
        unsigned nl, vl;
        const unsigned char *name;
        if (off >= len)
            return -1;
        nl = d[off++];
        if (nl > len - off)
            return -1;
        name = d + off;
        off += nl;
        if (off >= len)
            return -1;
        vl = d[off++];
        if (vl > len - off)
            return -1;
        if (n < max) {
            copy_clean(out[n].name, sizeof out[n].name, name, nl);
            copy_clean(out[n].value, sizeof out[n].value, d + off, vl);
            n++;
        }
        off += vl;
    }
    *stored = n;
    *total = count;
    return 0;
}

int sampq_parse_players(const unsigned char *d, unsigned len, sampq_player_t *out,
                        unsigned max, unsigned *stored, unsigned *total)
{
    unsigned off = SAMPQ_HDR + 2, count, i, n = 0;

    if (len < off)
        return -1;
    count = le16(d + SAMPQ_HDR);
    for (i = 0; i < count; i++) {
        unsigned nl;
        const unsigned char *name;
        if (off >= len)
            return -1;
        nl = d[off++];
        if (nl > len - off || len - off - nl < 4)
            return -1;
        name = d + off;
        off += nl;
        if (n < max) {
            copy_clean(out[n].name, sizeof out[n].name, name, nl);
            out[n].score = (int)le32(d + off);
            n++;
        }
        off += 4;
    }
    *stored = n;
    *total = count;
    return 0;
}
