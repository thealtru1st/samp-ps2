#include <stdarg.h>
#include "util.h"
#include "status.h"

void *memcpy(void *d, const void *s, unsigned long n)
{
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n--) *a++ = *b++;
    return d;
}

void *memset(void *d, int c, unsigned long n)
{
    unsigned char *a = d;
    while (n--) *a++ = c;
    return d;
}

int memcmp(const void *x, const void *y, unsigned long n)
{
    const unsigned char *a = x, *b = y;
    for (; n; n--, a++, b++)
        if (*a != *b)
            return *a - *b;
    return 0;
}

unsigned long strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return p - s;
}

/* When the log is full the older half goes, from a line start, so the latest lines are
 * always there to read after something goes wrong. */
static void lc(char c)
{
    unsigned p = ST->logpos, from, i;
    if (p >= sizeof ST->log) {
        from = sizeof ST->log / 2;
        while (from < sizeof ST->log && ST->log[from - 1] != '\n')
            from++;
        for (i = 0; from + i < sizeof ST->log; i++)
            ST->log[i] = ST->log[from + i];
        p = i;
    }
    ST->log[p] = c;
    ST->logpos = p + 1;
}

void slog(const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    for (; *f; f++) {
        if (*f != '%') { lc(*f); continue; }
        f++;
        if (*f == 's') {
            const char *s = va_arg(ap, const char *);
            while (*s) lc(*s++);
        } else if (*f == 'x' || *f == 'd') {
            unsigned v = va_arg(ap, unsigned), base = *f == 'x' ? 16 : 10;
            char b[12];
            int i = 0;
            do { b[i++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
            while (i) lc(b[--i]);
        } else {
            lc(*f);
        }
    }
    va_end(ap);
    lc('\n');
}
