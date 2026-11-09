/* 3D text labels. The tick picks the nearest few and works out where they are; the draw
 * hook projects those with the game's CSprite::CalcScreenCoors and prints them. Script
 * opcodes stay out of the draw hook, so line of sight is tested in the tick, a few per
 * frame. */
#include "feat_world_int.h"
#include "ui_font.h"
#include "util.h"

#define POOL_GLOBAL  SRPC_LABEL_GLOBALS
#define POOL         (POOL_GLOBAL * 2)
#define TEXT_MAX     256
#define VISIBLE_MAX  24
#define LINE_MAX     112   /* keeps one PrintString far under the 512 byte queue */
#define LINES_MAX    6
#define LINE_H       12.0f
#define SCAN_MS      200
#define LOS_PER_TICK 3
#define DEFAULT_RANGE 40.0f
#define RANGE_LIMIT  1000.0f
#define ATTACH_NONE  0xFFFFu
#define HEAD_Z       0.9f

typedef struct {
    unsigned char used, los, seen;
    unsigned color, attach_player, attach_vehicle;
    float pos[3], range, world[3];
    char text[TEXT_MAX];
} wlabel_t;

static wlabel_t labels[POOL];
static unsigned short used_ids[POOL];
static unsigned used_count, scan_at, los_next;
static unsigned short shown[VISIBLE_MAX];
static float shown_dist[VISIBLE_MAX];
static unsigned shown_count;

static unsigned slot_of(int player, unsigned id) { return (player ? POOL_GLOBAL : 0) + id; }

static void on_show(const srpc_label_t *l)
{
    unsigned s = slot_of(l->player, l->id), n = 0, i;
    wlabel_t *w;

    if (l->id >= POOL_GLOBAL || !world_pos_ok(l->pos) || !world_float_ok(l->distance))
        return;
    w = &labels[s];
    if (!w->used)
        used_ids[used_count++] = s;
    w->used = 1;
    w->los = l->los != 0;
    w->seen = !w->los;
    w->color = l->color;
    w->attach_player = l->attach_player;
    w->attach_vehicle = l->attach_vehicle;
    w->pos[0] = l->pos[0];
    w->pos[1] = l->pos[1];
    w->pos[2] = l->pos[2];
    w->range = l->distance > 0.0f && l->distance < RANGE_LIMIT ? l->distance : DEFAULT_RANGE;
    for (i = 0; l->text[i] && n < TEXT_MAX - 1; i++)
        w->text[n++] = l->text[i];
    w->text[n] = 0;
}

static void on_hide(const srpc_label_id_t *l)
{
    unsigned s = slot_of(l->player, l->id), i;

    if (l->id >= POOL_GLOBAL || !labels[s].used)
        return;
    labels[s].used = 0;
    for (i = 0; i < used_count; i++)
        if (used_ids[i] == s) {
            used_ids[i] = used_ids[--used_count];
            break;
        }
    for (i = 0; i < shown_count; i++)
        if (shown[i] == s) {
            shown[i] = shown[--shown_count];
            shown_dist[i] = shown_dist[shown_count];
            break;
        }
}

/* The world position right now, or 0 when it hangs from something not streamed in. */
static int locate(wlabel_t *w)
{
    float p[3];
    if (w->attach_player != ATTACH_NONE) {
        if (!game_remote_pos || !game_remote_pos(w->attach_player, p))
            return 0;
        w->world[0] = p[0] + w->pos[0];
        w->world[1] = p[1] + w->pos[1];
        w->world[2] = p[2] + w->pos[2] + HEAD_Z;
        return 1;
    }
    if (w->attach_vehicle != ATTACH_NONE) {
        if (!feat_vehicle_handle || !wg_car_pos(feat_vehicle_handle(w->attach_vehicle), p))
            return 0;
        w->world[0] = p[0] + w->pos[0];
        w->world[1] = p[1] + w->pos[1];
        w->world[2] = p[2] + w->pos[2];
        return 1;
    }
    w->world[0] = w->pos[0];
    w->world[1] = w->pos[1];
    w->world[2] = w->pos[2];
    return 1;
}

static void select_near(const float *cam)
{
    unsigned i;

    shown_count = 0;
    for (i = 0; i < used_count; i++) {
        unsigned s = used_ids[i], k;
        wlabel_t *w = &labels[s];
        float d;

        if (!locate(w))
            continue;
        d = world_dist2(w->world, cam);
        if (d > w->range * w->range)
            continue;
        if (shown_count == VISIBLE_MAX) {
            if (d >= shown_dist[VISIBLE_MAX - 1])
                continue;
            shown_count--;
        }
        k = shown_count++;
        while (k > 0 && shown_dist[k - 1] > d) {
            shown[k] = shown[k - 1];
            shown_dist[k] = shown_dist[k - 1];
            k--;
        }
        shown[k] = s;
        shown_dist[k] = d;
    }
}

void wlabel_tick(unsigned now)
{
    float cam[3];
    unsigned i;

    if (!used_count || !wg_camera_pos(cam))
        return;
    if (now - scan_at >= SCAN_MS) {
        scan_at = now;
        select_near(cam);
    }
    for (i = 0; i < shown_count && i < LOS_PER_TICK; i++) {
        wlabel_t *w;
        los_next = los_next + 1 >= shown_count ? 0 : los_next + 1;
        w = &labels[shown[los_next]];
        if (w->los)
            w->seen = wg_line_clear(cam, w->world);
    }
    /* Attached labels move between scans. */
    for (i = 0; i < shown_count; i++) {
        wlabel_t *w = &labels[shown[i]];
        if (w->attach_player != ATTACH_NONE || w->attach_vehicle != ATTACH_NONE)
            locate(w);
    }
}

/* ---- text ----------------------------------------------------------------- */

static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* {RRGGBB} at p, or -1. */
static int colour_tag(const char *p)
{
    int i, v = 0;
    if (p[0] != '{')
        return -1;
    for (i = 1; i < 7; i++) {
        int h = hex(p[i]);
        if (h < 0)
            return -1;
        v = v * 16 + h;
    }
    return p[7] == '}' ? v : -1;
}

/* Font control characters: ~ opens a token, a leading * hides the string, _ and high
 * bytes have no glyph here. */
static char printable(char c, unsigned at)
{
    if (c == '~' || c == '_' || (unsigned char)c < 32 || (c == '*' && at == 0))
        return ' ';
    if ((unsigned char)c >= 0x80)
        return '?';
    c = ui_font_char(c);
    return c == '*' && at == 0 ? ' ' : c;
}

typedef struct { char text[LINE_MAX]; unsigned color; float width; } seg_t;

/* Splits one line at colour tags, then prints it centred on x. */
static const char *draw_line(const char *p, float x, float y, unsigned color, unsigned *carry)
{
    static seg_t segs[8];
    unsigned n = 0, count = 0, i, total = 0;
    float width = 0.0f;

    segs[0].color = color;
    while (*p && *p != '\n' && total < LINE_MAX - 1) {
        int tag = colour_tag(p);
        if (tag >= 0) {
            if (n) {
                segs[count].text[n] = 0;
                if (count == 7)
                    break;
                count++;
                n = 0;
            }
            color = ((unsigned)tag << 8) | (color & 0xFF);
            segs[count].color = color;
            p += 8;
            continue;
        }
        segs[count].text[n++] = printable(*p++, total);
        total++;
    }
    segs[count].text[n] = 0;
    if (n)
        count++;
    while (*p && *p != '\n')
        p++;
    *carry = color;
    for (i = 0; i < count; i++) {
        segs[i].width = wg_text_width(segs[i].text);
        width += segs[i].width;
    }
    x -= width * 0.5f;
    for (i = 0; i < count; i++) {
        wg_text_colour(segs[i].color);
        wg_text_print(x, y, segs[i].text);
        x += segs[i].width;
    }
    return *p ? p + 1 : p;
}

void wlabel_draw(void)
{
    unsigned i;

    if (!shown_count || wg_menu_open())
        return;
    wg_text_begin();
    for (i = 0; i < shown_count; i++) {
        wlabel_t *w = &labels[shown[i]];
        const char *p = w->text;
        unsigned color = w->color, line;
        float x, y;

        if (!w->used || !w->seen || !wg_world_to_screen(w->world, &x, &y))
            continue;
        for (line = 0; *p && line < LINES_MAX; line++) {
            p = draw_line(p, x, y, color, &color);
            y += LINE_H;
        }
    }
    wg_text_end();
}

void wlabel_reset(void)
{
    unsigned i;

    for (i = 0; i < used_count; i++)
        labels[used_ids[i]].used = 0;
    used_count = 0;
    shown_count = 0;
}

void wlabel_init(samp_rpc_handlers_t *h)
{
    h->show_label = on_show;
    h->hide_label = on_hide;
}
