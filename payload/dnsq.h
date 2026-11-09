#pragma once
/* Minimal DNS A-record client packets. PCSX2's internal server (192.0.2.1:53)
 * answers type A class IN only; we ask one question per packet. */
#define DNSQ_MAX_QUERY 280

/* Returns the query length, or -1 if the name is not a valid hostname. */
int dnsq_build(unsigned char *out, unsigned max, unsigned id, const char *host);

#define DNSQ_OK       0    /* *ip holds the first A record */
#define DNSQ_NOT_OURS (-1) /* wrong id, not a response, or malformed: keep waiting */
#define DNSQ_NO_ADDR  (-2) /* a real answer saying the name has no address */

int dnsq_parse(const unsigned char *d, unsigned len, unsigned id, unsigned *ip);
