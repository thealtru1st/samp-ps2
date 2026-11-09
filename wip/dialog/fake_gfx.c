/* Host stand-in for payload/feat_dialog_gfx.c. Records what would reach the game and
 * checks the rules the real font code needs: see feat_dialog_text.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "feat_dialog_gfx.h"
#include "feat_dialog_text.h"
#include "fake_gfx.h"

fake_text_t fake_texts[FAKE_MAX];
int fake_ntext, fake_nrect, fake_nblip, fake_project_ok;
fake_rect_t fake_rects[FAKE_MAX];
static float cur_sx = 1.0f;

void fake_clear(void) { fake_ntext = fake_nrect = 0; }

static void die(const char *why, const char *s)
{
    fprintf(stderr, "RULE BROKEN: %s: '%s'\n", why, s);
    abort();
}

static void finite(float v, const char *what)
{
    if (!(v > -1.0e7f && v < 1.0e7f))
        die("coordinate not finite", what);
}

void fd_font(int font, float sx, float sy, int proportional, int edge, int shadow, unsigned drop)
{
    if (font < 0 || font > 3)
        die("bad font style", "");
    finite(sx, "sx");
    finite(sy, "sy");
    cur_sx = sx;
}

float fd_width(const char *s)
{
    size_t n = strlen(s);
    if (n > FD_RUN_MAX + 2)
        die("measured string too long", s);
    return (float)n * 18.0f * cur_sx;
}

void fd_text(float x, float y, const char *s, unsigned color)
{
    size_t i, n = strlen(s);

    finite(x, "x");
    finite(y, "y");
    if (n > FD_RUN_MAX)
        die("printed string too long", s);
    for (i = 0; i < n; i++) {
        unsigned char c = s[i];
        if (c == '~' || c < 32 || c > 126 || c == '_')
            die("character the font must not see", s);
    }
    if (fake_ntext < FAKE_MAX) {
        fake_text_t *t = &fake_texts[fake_ntext++];
        t->x = x;
        t->y = y;
        t->color = color;
        snprintf(t->s, sizeof t->s, "%s", s);
    }
}

void fd_rect(float l, float t, float r, float b, unsigned color)
{
    finite(l, "l"); finite(t, "t"); finite(r, "r"); finite(b, "b");
    if (fake_nrect < FAKE_MAX) {
        fake_rect_t *q = &fake_rects[fake_nrect++];
        q->l = l; q->t = t; q->r = r; q->b = b; q->color = color;
    }
}

void fd_flush(void) {}

int fd_blip_for_char(int h) { fake_nblip++; return h + 1000; }
int fd_blip_for_coord(const float *pos) { fake_nblip++; return 7; }
void fd_blip_colour(int blip, unsigned rgba) {}
void fd_blip_remove(int blip) { fake_nblip--; }
void fd_camera_update(void) {}
int fake_paused;
int fd_paused(void) { return fake_paused; }

int fd_project(const float *w, float *x, float *y, float *dist)
{
    if (!fake_project_ok)
        return 0;
    *x = w[0];
    *y = w[1];
    *dist = w[2];
    return 1;
}

int fake_find(const char *needle)
{
    int i;
    for (i = 0; i < fake_ntext; i++)
        if (strstr(fake_texts[i].s, needle))
            return i;
    return -1;
}

void fake_dump(void)
{
    int i;
    for (i = 0; i < fake_ntext; i++)
        printf("  %6.1f %6.1f %08X '%s'\n", fake_texts[i].x, fake_texts[i].y, fake_texts[i].color, fake_texts[i].s);
}
