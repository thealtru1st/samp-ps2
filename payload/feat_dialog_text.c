/* Layout of server text, done here and not by CFont, for three reasons found in the ELF:
 *
 * - ProcessCurrentString (0x2A7080) has a 260-byte stack buffer at sp+144. When a line
 *   wraps after a colour token it copies the rest of the string into it with no bound
 *   (0x2A7404-0x2A74B0), so a long server string with a ~r~ in it smashes the game's stack.
 * - One queued line must stay under about 450 bytes or it overruns the 512-byte queue.
 * - There is no {RRGGBB} support at all.
 *
 * So the game only ever sees short runs with no '~' in them. Token letters and the wrap
 * rule below were read from the same code: table at 0x64D480, '~h~' is x1.5 capped at
 * 255 (0x2A8160), a line always takes its first word (s4 at 0x2A71C0). The token RGB
 * values are the PC's HUD colours, for the same table indices (0, 1, 2, 4, 5, 7, 11). */
#include "feat_dialog_text.h"
#include "feat_dialog_gfx.h"
#include "ui_font.h"

#define TAB_STOP 48.0f
#define KEY_NAME_MAX 40

unsigned fd_copy(char *dst, const char *src, unsigned cap)
{
    unsigned n = 0;
    if (!cap)
        return 0;
    while (n < cap - 1 && src[n]) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = 0;
    return n;
}

char *fd_put_uint(char *p, unsigned v)
{
    char b[12];
    int i = 0;
    do { b[i++] = '0' + v % 10; v /= 10; } while (v);
    while (i)
        *p++ = b[--i];
    *p = 0;
    return p;
}

char *fd_put_str(char *p, const char *s, unsigned max)
{
    while (*s && max--)
        *p++ = *s++;
    *p = 0;
    return p;
}

/* Latin-1 letters to their base letter: the font's high glyphs are not Latin-1. */
static const char latin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";

char fd_clean_char(unsigned char c)
{
    if (c >= 0xC0)
        return latin1[c - 0xC0];
    if (c >= 0x7F)
        return '?';
    if (c < 32 || c == '_')  /* '_' is a blank glyph anyway */
        return ' ';
    if (c == '~')
        return '-';
    return ui_font_char(c);
}

typedef struct {
    fd_layout_t *l;
    int hard, wrapped, full;
    int open;             /* a run is being written */
    unsigned run_len;
    unsigned color;
    float x, limit, space;
    unsigned line;
} st_t;

static void close_run(st_t *s)
{
    if (s->open) {
        s->l->buf[s->l->used++] = 0;
        s->open = 0;
    }
}

static int new_line(st_t *s, int wrapped)
{
    fd_layout_t *l = s->l;
    close_run(s);
    if (l->lines >= l->line_cap) {
        l->truncated = 1;
        s->full = 1;
        return 0;
    }
    s->line = l->lines++;
    l->line_w[s->line] = 0.0f;
    s->x = 0.0f;
    s->wrapped = wrapped;
    return 1;
}

/* Room for n more characters in the current run, opening one if needed. */
static int room(st_t *s, unsigned n)
{
    fd_layout_t *l = s->l;
    fd_run_t *r;

    if (s->open && s->run_len + n <= FD_RUN_MAX && l->used + n + 1 <= l->cap)
        return 1;
    close_run(s);
    if (l->nruns >= l->run_cap || l->used + n + 1 > l->cap) {
        l->truncated = 1;
        s->full = 1;
        return 0;
    }
    r = &l->runs[l->nruns++];
    r->x = s->x;
    r->at = l->used;
    r->line = s->line;
    r->color = s->color;
    s->open = 1;
    s->run_len = 0;
    return 1;
}

static void append(st_t *s, const char *w, unsigned n, float width)
{
    fd_layout_t *l = s->l;
    unsigned i;

    if (!room(s, n))
        return;
    for (i = 0; i < n; i++)
        l->buf[l->used++] = w[i];
    s->run_len += n;
    s->x += width;
    s->wrapped = 0;
}

/* The width of the first k characters of w. */
static float prefix_width(char *w, unsigned k)
{
    char keep = w[k];
    float got;
    w[k] = 0;
    got = fd_width(w);
    w[k] = keep;
    return got;
}

/* How many leading characters of w fit in limit. At least one. */
static unsigned fit(char *w, unsigned n, float width, float limit)
{
    unsigned k = limit > 0.0f ? (unsigned)(n * (limit / width)) : 1;

    if (k < 1)
        k = 1;
    if (k > n)
        k = n;
    while (k > 1 && prefix_width(w, k) > limit)
        k--;
    return k;
}

/* w has room for a terminator at w[n]. */
static void emit_word(st_t *s, char *w, unsigned n)
{
    while (n && !s->full) {
        float width = prefix_width(w, n);
        if (s->x > 0.0f && s->x + width > s->limit && !new_line(s, 1))
            return;
        if (s->hard && width > s->limit) {
            unsigned k = fit(w, n, width, s->limit);
            if (k < n) {
                append(s, w, k, prefix_width(w, k));
                s->l->line_w[s->line] = s->x;
                w += k;
                n -= k;
                if (!new_line(s, 1))
                    return;
                continue;
            }
        }
        append(s, w, n, width);
        s->l->line_w[s->line] = s->x;
        return;
    }
}

static int hex(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Reads {RRGGBB}. A NUL is not a hex digit, so this cannot run off the string. */
static int read_tag(const unsigned char *p, unsigned *rgb)
{
    int i;
    unsigned v = 0;

    if (p[0] != '{')
        return 0;
    for (i = 1; i < 7; i++) {
        int h = hex(p[i]);
        if (h < 0)
            return 0;
        v = v << 4 | h;
    }
    if (p[7] != '}')
        return 0;
    *rgb = v;
    return 1;
}

static void set_color(st_t *s, unsigned c)
{
    if (c != s->color) {
        close_run(s);
        s->color = c;
    }
}

static unsigned brighten(unsigned c)
{
    unsigned out = c & 0xFF000000u;
    int i;
    for (i = 0; i < 24; i += 8) {
        unsigned v = (c >> i & 255) * 3 / 2;
        out |= (v > 255 ? 255 : v) << i;
    }
    return out;
}

/* ~k~~NAME~ asks for a key binding, which only the PC's text tables know: show the name. */
static const unsigned char *key_name(st_t *s, const unsigned char *p)
{
    char w[KEY_NAME_MAX + 4];
    unsigned n = 0;

    w[n++] = '(';
    while (*p && *p != '~') {
        if (n < KEY_NAME_MAX)
            w[n++] = fd_clean_char(*p);
        p++;
    }
    if (*p)
        p++;
    w[n++] = ')';
    emit_word(s, w, n);
    return p;
}

static const unsigned char *token(st_t *s, const unsigned char *p)
{
    unsigned a = s->color & 0xFF000000u;
    char sym[2];
    unsigned c = p[1];

    if (!c)
        return p + 1;
    if (c == '~') /* an empty token: both tildes go */
        return p + 2;
    if (p[2] != '~') { /* not a token the game knows: it swallows up to the next '~' */
        p += 2;
        while (*p && *p != '~')
            p++;
        return *p ? p + 1 : p;
    }
    p += 3;
    switch (c) {
    case 'r': case 'R': set_color(s, a | FD_RGBA(180, 25, 29, 0)); break;
    case 'g': case 'G': set_color(s, a | FD_RGBA(54, 104, 44, 0)); break;
    case 'b': case 'B': set_color(s, a | FD_RGBA(50, 60, 127, 0)); break;
    case 'w': case 'W':
    case 's': case 'S': set_color(s, a | FD_RGBA(225, 225, 225, 0)); break;
    case 'y': case 'Y': set_color(s, a | FD_RGBA(226, 192, 99, 0)); break;
    case 'p': case 'P': set_color(s, a | FD_RGBA(168, 110, 252, 0)); break;
    case 'l':           set_color(s, a); break;
    case 'h': case 'H': set_color(s, brighten(s->color)); break;
    case 'n': case 'N': new_line(s, 0); break;
    case 'k': case 'K':
        if (*p == '~')
            return key_name(s, p + 1);
        break;
    case '<': case '>': case 'u': case 'U': case 'd': case 'D':
        sym[0] = c == '<' || c == '>' ? (char)c : c == 'u' || c == 'U' ? '^' : 'v';
        emit_word(s, sym, 1);
        break;
    default: /* button icons and the rest draw nothing here */
        break;
    }
    return p;
}

void fd_layout(fd_layout_t *l, const char *text, int mode, unsigned base, float limit, int hard_break)
{
    const unsigned char *p = (const unsigned char *)text;
    char w[FD_RUN_MAX + 2];
    st_t s;

    l->used = 0;
    l->nruns = 0;
    l->lines = 0;
    l->truncated = 0;
    s.l = l;
    s.hard = hard_break;
    s.wrapped = 0;
    s.full = 0;
    s.open = 0;
    s.run_len = 0;
    s.color = base;
    s.x = 0.0f;
    s.limit = limit;
    s.line = 0;
    s.space = fd_width(" ");
    if (!l->cap || !new_line(&s, 0))
        return;

    while (*p && !s.full) {
        unsigned rgb, n;

        if (mode == FD_TAGS && read_tag(p, &rgb)) {
            set_color(&s, (base & 0xFF000000u) | rgb >> 16 | (rgb & 0xFF00u) | (rgb & 255) << 16);
            p += 8;
        } else if (mode == FD_TOKENS && *p == '~') {
            p = token(&s, p);
        } else if (*p == '\n') {
            new_line(&s, 0);
            p++;
        } else if (*p == '\r') {
            p++;
        } else if (*p == '\t') {
            float stop = ((float)(int)(s.x / TAB_STOP) + 1.0f) * TAB_STOP;
            close_run(&s);
            s.x = stop;
            p++;
        } else if (*p == ' ') {
            if (!s.wrapped)
                append(&s, " ", 1, s.space);
            p++;
        } else {
            unsigned tmp;
            for (n = 0; p[n] && n < FD_RUN_MAX; n++) {
                if (p[n] == ' ' || p[n] == '\n' || p[n] == '\r' || p[n] == '\t')
                    break;
                if (mode == FD_TOKENS ? p[n] == '~' : read_tag(p + n, &tmp))
                    break;
                w[n] = fd_clean_char(p[n]);
            }
            p += n;
            emit_word(&s, w, n);
        }
    }
    close_run(&s);
}

void fd_plain(char *dst, const char *src, unsigned cap)
{
    const unsigned char *p = (const unsigned char *)src;
    unsigned n = 0, rgb;

    if (!cap)
        return;
    while (*p && n < cap - 1) {
        if (read_tag(p, &rgb)) {
            p += 8;
            continue;
        }
        dst[n++] = fd_clean_char(*p++);
    }
    dst[n] = 0;
}

fd_layout_t *fd_scratch(void)
{
    static char buf[2048];
    static fd_run_t runs[192];
    static float line_w[64];
    static fd_layout_t l = {buf, sizeof buf, 0, runs, sizeof runs / sizeof runs[0], 0,
                            line_w, sizeof line_w / sizeof line_w[0], 0, 0};
    return &l;
}

void fd_print(const fd_layout_t *l, float x, float y, float line_h, int align,
              unsigned first, unsigned n)
{
    unsigned i;

    for (i = 0; i < l->nruns; i++) {
        const fd_run_t *r = &l->runs[i];
        float left = x;
        if (r->line < first || r->line >= first + n)
            continue;
        if (align == FD_CENTRE)
            left = x - l->line_w[r->line] * 0.5f;
        else if (align == FD_RIGHT)
            left = x - l->line_w[r->line];
        fd_text(left + r->x, y + (r->line - first) * line_h, l->buf + r->at, r->color);
    }
}
