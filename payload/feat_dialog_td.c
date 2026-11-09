/* Textdraws: RPC 134 show, 135 hide, 105 set text, 83 select in, 83 click out.
 *
 * Coordinates need no conversion: SA-MP's 640x448 space is the PS2 game's own. The sizes
 * follow the PC client's draw code: scale is letter size with the height halved, TextSize
 * x is the wrap edge and y the click height, and the two swap roles when centred.
 * Selection on a pad has no cursor: the D-pad or stick jumps between selectable
 * textdraws, which reach the same RPC a mouse click would.
 *
 * Fonts 0-3 draw. Font 4 sprites need textures we cannot load, so the plain white and
 * black sprites become rectangles, which is most of what servers use them for, and
 * others get an outline to keep the layout readable. Font 5 model previews are the same. */
#include "feat_dialog_int.h"
#include "feat_dialog_gfx.h"
#include "feat_dialog_text.h"
#include "bitstream.h"
#include "rak.h"
#include "util.h"

#define TD_IDS    (SRPC_TD_GLOBALS + 256)
#define TD_SLOTS  512
#define BOX_PAD   4.0f   /* CFont::GetTextRect's margin, 0x2A77B0 */
#define MAX_COORD 100000.0f
#define MAX_SCALE 64.0f
#define MAX_EDGE  8      /* the game keeps these in signed bytes */
#define OUTLINE   FD_RGBA(255, 255, 255, 110)

typedef struct {
    int used, drawn, key;
    float l, t, r, b;     /* click rectangle, set when drawn */
    srpc_textdraw_t td;
} slot_t;

static slot_t pool[TD_SLOTS];
static unsigned short slot_of[TD_IDS]; /* slot + 1, 0 for none */

static int selecting, hover = -1, held[4];
static unsigned hover_color;

static int td_key(int player, unsigned id)
{
    unsigned k = player ? id + SRPC_TD_GLOBALS : id;
    return k < TD_IDS ? (int)k : -1;
}

static slot_t *td_find(int key) { return key >= 0 && slot_of[key] ? &pool[slot_of[key] - 1] : 0; }

void ftd_show(const srpc_textdraw_t *t)
{
    int key = td_key(t->player, t->id), i;
    slot_t *s = td_find(key);

    if (key < 0)
        return;
    if (!s) {
        for (i = 0; i < TD_SLOTS; i++)
            if (!pool[i].used)
                break;
        if (i == TD_SLOTS)
            return;
        s = &pool[i];
        slot_of[key] = i + 1;
    }
    s->used = 1;
    s->drawn = 0;
    s->key = key;
    s->td = *t;
    s->td.text[SRPC_TD_MAX - 1] = 0;
}

void ftd_hide(const srpc_textdraw_id_t *t)
{
    int key = td_key(t->player, t->id);
    slot_t *s = td_find(key);

    if (!s)
        return;
    s->used = 0;
    slot_of[key] = 0;
    if (hover == key)
        hover = -1;
}

void ftd_set_text(const srpc_textdraw_text_t *t)
{
    slot_t *s = td_find(td_key(t->player, t->id));
    if (s)
        fd_copy(s->td.text, t->text, sizeof s->td.text);
}

static void send_click(unsigned wire_id)
{
    static unsigned char buf[8];
    bs_t b;

    bs_wr_init(&b, buf, sizeof buf);
    srpc_enc_click_textdraw(&b, wire_id);
    if (!b.err)
        rak_rpc(SRPC_C2S_CLICK_TEXTDRAW, buf, b.bits, RAK_RELIABLE, 0);
    fd_spawn_retry_arm();
}

/* As the PC client, leaving selection always tells the server, even when the server
 * asked for it: that is how OnPlayerClickTextDraw gets INVALID_TEXT_DRAW. */
void ftd_select(const srpc_textdraw_select_t *s)
{
    if (s->enable) {
        selecting = 1;
        hover_color = FD_FROM_RGBA(s->color);
        hover = -1;
    } else {
        selecting = 0;
        hover = -1;
        send_click(SRPC_INVALID_ID);
    }
}

int ftd_selecting(void) { return selecting; }

void ftd_reset(void)
{
    int i;
    for (i = 0; i < TD_SLOTS; i++)
        pool[i].used = 0;
    for (i = 0; i < TD_IDS; i++)
        slot_of[i] = 0;
    selecting = 0;
    hover = -1;
}

/* ---- selection ---------------------------------------------------------- */

static int can_select(const slot_t *s) { return s->td.selectable && s->drawn; }

static int first_selectable(void)
{
    int k;
    for (k = 0; k < TD_IDS; k++) {
        slot_t *s = td_find(k);
        if (s && can_select(s))
            return k;
    }
    return -1;
}

/* The nearest selectable textdraw in a direction, favouring ones straight ahead. */
static void move_hover(float dx, float dy)
{
    slot_t *from = td_find(hover);
    float cx, cy, best_score = 1.0e30f;
    int k, best = -1;

    if (!from)
        return;
    cx = (from->l + from->r) * 0.5f;
    cy = (from->t + from->b) * 0.5f;
    for (k = 0; k < TD_IDS; k++) {
        slot_t *s = td_find(k);
        float ox, oy, along, across, score;
        if (!s || k == hover || !can_select(s))
            continue;
        ox = (s->l + s->r) * 0.5f - cx;
        oy = (s->t + s->b) * 0.5f - cy;
        along = ox * dx + oy * dy;
        across = ox * dy + oy * dx;
        if (across < 0.0f)
            across = -across;
        if (along <= 0.0f)
            continue;
        score = along + across * 2.0f;
        if (score < best_score) {
            best_score = score;
            best = k;
        }
    }
    if (best >= 0)
        hover = best;
}

void ftd_input(void)
{
    slot_t *h;

    if (!selecting)
        return;
    h = td_find(hover);
    if (!h || !can_select(h))
        hover = first_selectable();
    if (fd_repeat(&held[0], fd_pad_up()))
        move_hover(0.0f, -1.0f);
    if (fd_repeat(&held[1], fd_pad_down()))
        move_hover(0.0f, 1.0f);
    if (fd_repeat(&held[2], fd_pad_left()))
        move_hover(-1.0f, 0.0f);
    if (fd_repeat(&held[3], fd_pad_right()))
        move_hover(1.0f, 0.0f);
    if (FD_PRESSED(cross) || FD_PRESSED(start)) {
        if (hover >= 0)
            send_click(hover);
    } else if (FD_PRESSED(circle) || FD_PRESSED(triangle)) {
        selecting = 0;
        hover = -1;
        send_click(SRPC_INVALID_ID);
    }
}

/* ---- drawing ------------------------------------------------------------ */

/* Server floats reach the game's font code: reject NaN and absurd values here. */
static int sane(float v, float limit) { return v > -limit && v < limit; }

static int has_word(const char *s, const char *word)
{
    unsigned i, n = strlen(word);
    for (; *s; s++) {
        for (i = 0; i < n; i++) {
            char c = s[i];
            if (c >= 'A' && c <= 'Z')
                c += 32;
            if (c != word[i])
                break;
        }
        if (i == n)
            return 1;
    }
    return 0;
}

static void outline(float l, float t, float r, float b, unsigned color)
{
    fd_rect(l, t, r, t + 1.0f, color);
    fd_rect(l, b - 1.0f, r, b, color);
    fd_rect(l, t, l + 1.0f, b, color);
    fd_rect(r - 1.0f, t, r, b, color);
}

static void draw_sprite(slot_t *s, unsigned color)
{
    const srpc_textdraw_t *t = &s->td;
    float l = t->x, u = t->y, r = t->x + t->text_w, b = t->y + t->text_h;

    if (r < l) { float v = l; l = r; r = v; }
    if (b < u) { float v = u; u = b; b = v; }
    s->l = l; s->t = u; s->r = r; s->b = b;
    s->drawn = 1;
    fd_flush();
    if (t->style == 5) {
        if (t->box)
            fd_rect(l, u, r, b, t->back_color);
        else
            outline(l, u, r, b, OUTLINE);
    } else if (has_word(t->text, "white") || has_word(t->text, "chit")) {
        fd_rect(l, u, r, b, color);
    } else if (has_word(t->text, "black")) {
        fd_rect(l, u, r, b, color & 0xFF000000u);
    } else {
        outline(l, u, r, b, OUTLINE);
    }
}

static void draw_text(slot_t *s, unsigned color)
{
    const srpc_textdraw_t *t = &s->td;
    fd_layout_t *lay = fd_scratch();
    float sy = t->letter_h * 0.5f, line_h = 18.0f * sy, limit;
    int align = t->align & SRPC_TD_RIGHT ? FD_RIGHT : t->align & SRPC_TD_CENTER ? FD_CENTRE : FD_LEFT;

    if (!t->text[0] || t->letter_w <= 0.0f || t->letter_h <= 0.0f)
        return;
    fd_font(t->style, t->letter_w, sy, t->proportional, t->outline < MAX_EDGE ? t->outline : MAX_EDGE,
            t->shadow < MAX_EDGE ? t->shadow : MAX_EDGE, t->back_color);
    limit = align == FD_LEFT ? t->text_w - t->x : align == FD_CENTRE ? t->text_h : t->x;
    fd_layout(lay, t->text, FD_TOKENS, color, limit, 0);

    if (align == FD_LEFT) {
        s->l = t->x; s->r = t->text_w; s->b = t->y + t->text_h;
    } else if (align == FD_CENTRE) {
        s->l = t->x - t->text_h * 0.5f; s->r = t->x + t->text_h * 0.5f; s->b = t->y + t->text_w;
    } else {
        s->l = t->x - (t->text_w - t->x); s->r = t->x; s->b = t->y + t->text_h;
    }
    s->t = t->y;
    s->drawn = 1;

    if (t->box) {
        float l, r, b = t->y + lay->lines * line_h + BOX_PAD;
        if (align == FD_LEFT) {
            l = t->x - BOX_PAD;
            r = t->text_w + BOX_PAD;
        } else if (align == FD_CENTRE) {
            l = t->x - t->text_h * 0.5f - BOX_PAD;
            r = t->x + t->text_h * 0.5f + BOX_PAD;
        } else {
            l = t->x - lay->line_w[0] - BOX_PAD;
            r = t->x + BOX_PAD;
        }
        fd_flush(); /* a rectangle goes out at once: earlier text must be under it */
        fd_rect(l, t->y - BOX_PAD, r, b, t->box_color);
    }
    fd_print(lay, t->x, t->y, line_h, align, 0, lay->lines);
}

static void draw_one(slot_t *s, int key)
{
    const srpc_textdraw_t *t = &s->td;
    unsigned color = selecting && key == hover ? hover_color : t->letter_color;

    if (!sane(t->x, MAX_COORD) || !sane(t->y, MAX_COORD) || !sane(t->text_w, MAX_COORD) ||
        !sane(t->text_h, MAX_COORD) || !sane(t->letter_w, MAX_SCALE) || !sane(t->letter_h, MAX_SCALE))
        return;
    if (t->style <= 3)
        draw_text(s, color);
    else if (t->style <= 5)
        draw_sprite(s, color);
    if (selecting && key == hover) {
        fd_flush();
        outline(s->l, s->t, s->r, s->b, hover_color | 0xFF000000u);
    }
}

void ftd_draw(void)
{
    int k;

    for (k = 0; k < TD_IDS; k++) {
        slot_t *s = td_find(k);
        if (s)
            draw_one(s, k);
    }
    fd_flush();
    if (selecting) {
        fd_layout_t *l = fd_scratch();
        fd_font(FD_FONT_SUBTITLES, 0.34f, 0.68f, 1, 0, 1, FD_RGB(0, 0, 0));
        fd_layout(l, "Arrows move   Enter select   Click cancel", FD_TAGS, FD_RGB(255, 200, 80), FD_NOWRAP, 0);
        fd_print(l, 320.0f, 424.0f, 13.0f, FD_CENTRE, 0, 1);
        fd_flush();
    }
}
