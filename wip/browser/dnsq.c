#include "dnsq.h"
#include "util.h"

int dnsq_build(unsigned char *out, unsigned max, unsigned id, const char *host)
{
    unsigned n = strlen(host), off = 12, i, start = 0;

    if (n && host[n - 1] == '.')
        n--; /* a trailing dot only means "fully qualified" */
    if (n == 0 || n > 253 || 12 + n + 2 + 4 > max)
        return -1;
    memset(out, 0, 12);
    out[0] = id >> 8; out[1] = id;
    out[2] = 0x01; /* RD */
    out[5] = 1;    /* one question */
    for (i = 0; i <= n; i++) {
        if (i < n && host[i] != '.')
            continue;
        if (i == start || i - start > 63)
            return -1;
        out[off++] = i - start;
        memcpy(out + off, host + start, i - start);
        off += i - start;
        start = i + 1;
    }
    out[off++] = 0;
    out[off++] = 0; out[off++] = 1; /* A */
    out[off++] = 0; out[off++] = 1; /* IN */
    return off;
}

/* Steps over a possibly compressed name. Returns the new offset, or 0 if bad. */
static unsigned skip_name(const unsigned char *d, unsigned len, unsigned off)
{
    while (off < len) {
        unsigned c = d[off];
        if (c == 0)
            return off + 1;
        if ((c & 0xC0) == 0xC0)
            return off + 2 <= len ? off + 2 : 0;
        if (c & 0xC0)
            return 0;
        off += c + 1;
    }
    return 0;
}

int dnsq_parse(const unsigned char *d, unsigned len, unsigned id, unsigned *ip)
{
    unsigned qd, an, off = 12, i;

    if (len < 12 || d[0] != ((id >> 8) & 0xFF) || d[1] != (id & 0xFF))
        return DNSQ_NOT_OURS;
    if (!(d[2] & 0x80) || (d[2] & 0x78))
        return DNSQ_NOT_OURS; /* must be a standard-query response */
    if (d[3] & 0x0F)
        return DNSQ_NO_ADDR;  /* the adapter reports every failure as rcode 2 */
    qd = (d[4] << 8) | d[5];
    an = (d[6] << 8) | d[7];
    for (i = 0; i < qd; i++) {
        off = skip_name(d, len, off);
        if (!off || len - off < 4)
            return DNSQ_NOT_OURS;
        off += 4;
    }
    for (i = 0; i < an; i++) {
        unsigned type, cls, rdlen;
        off = skip_name(d, len, off);
        if (!off || len - off < 10)
            return DNSQ_NOT_OURS;
        type = (d[off] << 8) | d[off + 1];
        cls = (d[off + 2] << 8) | d[off + 3];
        rdlen = (d[off + 8] << 8) | d[off + 9];
        off += 10;
        if (rdlen > len - off)
            return DNSQ_NOT_OURS;
        if (type == 1 && cls == 1 && rdlen == 4) {
            *ip = ((unsigned)d[off] << 24) | (d[off + 1] << 16) | (d[off + 2] << 8) | d[off + 3];
            return DNSQ_OK;
        }
        off += rdlen; /* e.g. a CNAME ahead of the address */
    }
    return DNSQ_NO_ADDR;
}
