/* Answer to the server's connection challenge. Real SA-MP servers send a random
 * string, so the reply is computed rather than looked up: SHA-1 of the challenge,
 * then a fixed XOR, as uppercase hex. The algorithm follows the MIT-licensed
 * ReSAMP sampnet/samp_auth.cpp, and it was checked against all 256 known pairs in
 * open.mp's SAMPRakNet.cpp. Those constants are part of the protocol: every client
 * sends the same thing, so they identify nothing about this machine. */
#include "authkey.h"
#include "util.h"

/* The published transform XORs a 100-byte table and a per-group value into each
 * byte. The group value cancels over 100 rounds and the table folds to this. */
#define TABLE_FOLD 0x8C

static const unsigned char suffix[20] = {
    0xFF, 0x25, 0x34, 0x39, 0x4D, 0x00, 0x90, 0x90, 0x90, 0x90,
    0x56, 0x57, 0x50, 0x8B, 0x44, 0x24, 0x14, 0x8D, 0x0C, 0x80
};

#define ROTL(v, n) (((v) << (n)) | ((v) >> (32 - (n))))

/* Single block only: challenges are at most a few dozen bytes. */
static void sha1_block(const unsigned char *msg, unsigned len, unsigned h[5])
{
    unsigned char block[64];
    unsigned w[80], a, b, c, d, e, i;

    memset(block, 0, sizeof block);
    memcpy(block, msg, len);
    block[len] = 0x80;
    block[62] = (len * 8) >> 8;
    block[63] = len * 8;
    for (i = 0; i < 16; i++)
        w[i] = ((unsigned)block[i * 4] << 24) | (block[i * 4 + 1] << 16) |
               (block[i * 4 + 2] << 8) | block[i * 4 + 3];
    for (i = 16; i < 80; i++) {
        unsigned t = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        w[i] = ROTL(t, 1);
    }
    h[0] = 0x67452301; h[1] = 0xEFCDAB89; h[2] = 0x98BADCFE;
    h[3] = 0x10325476; h[4] = 0xC3D2E1F0;
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4];
    for (i = 0; i < 80; i++) {
        unsigned f, k, t;
        if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
        t = ROTL(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ROTL(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

int samp_auth_answer(const unsigned char *challenge, unsigned len, char out[41])
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned h[5], i;

    if (len == 0 || len > 55)
        return -1;
    sha1_block(challenge, len, h);
    for (i = 0; i < 20; i++) {
        /* hash words are read in little-endian byte order */
        unsigned char v = (h[i / 4] >> ((i % 4) * 8)) ^ TABLE_FOLD ^ suffix[i];
        out[i * 2] = hex[v >> 4];
        out[i * 2 + 1] = hex[v & 15];
    }
    out[40] = 0;
    return 0;
}
