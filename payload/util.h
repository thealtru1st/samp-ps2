#pragma once
void *memcpy(void *d, const void *s, unsigned long n);
void *memset(void *d, int c, unsigned long n);
int   memcmp(const void *a, const void *b, unsigned long n);
unsigned long strlen(const char *s);
void  slog(const char *fmt, ...); /* into the PINE-readable status log */
