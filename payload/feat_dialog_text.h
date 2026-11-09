#pragma once
/* Text layout for server strings: colour markup, wrapping, and characters the game's
 * font must never see. Portable, no game calls except fd_width. */

#define FD_RUN_MAX 200 /* longest string handed to fd_text */

enum { FD_TAGS,   /* {RRGGBB}, for dialogs */
       FD_TOKENS  /* ~r~ ~n~ ~h~ ..., for textdraws and game text */ };

#define FD_NOWRAP 1.0e9f

typedef struct {
    float x;              /* from the start of the line */
    unsigned short at;    /* NUL-terminated text at buf + at */
    unsigned short line;
    unsigned color;
} fd_run_t;

typedef struct {
    char *buf;
    unsigned cap, used;
    fd_run_t *runs;
    unsigned run_cap, nruns;
    float *line_w;
    unsigned line_cap, lines;
    int truncated;        /* some text did not fit the arrays */
} fd_layout_t;

/* Lays text out in lines no wider than limit, as the game wraps: by word, and a line
 * always takes its first word. hard_break also splits a word wider than limit.
 * Reads the current font through fd_width, so fd_font comes first. */
void fd_layout(fd_layout_t *l, const char *text, int mode, unsigned base, float limit, int hard_break);

/* One character as the font can show it. */
char fd_clean_char(unsigned char c);

/* dst is always terminated. Returns the length copied. */
unsigned fd_copy(char *dst, const char *src, unsigned cap);
/* Appends to p, which must have room, and terminates. Returns the new end. */
char *fd_put_uint(char *p, unsigned v);
char *fd_put_str(char *p, const char *s, unsigned max);

/* Text with the markup removed and every character cleaned. */
void fd_plain(char *dst, const char *src, unsigned cap);

/* A shared layout for short text. Valid until the next call to fd_layout on it. */
fd_layout_t *fd_scratch(void);

enum { FD_LEFT, FD_CENTRE, FD_RIGHT };
/* Prints lines first..first+n-1 with the first at y. x is the left edge, the centre or
 * the right edge, as align says. */
void fd_print(const fd_layout_t *l, float x, float y, float line_h, int align,
              unsigned first, unsigned n);
