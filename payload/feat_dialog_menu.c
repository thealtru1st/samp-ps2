/* Server menus: RPC 76 init, 77 show, 78 hide, 132 row chosen, 140 left.
 *
 * The game's own panels take GXT keys, not text, so this draws its own at the server's
 * position. Choosing or leaving closes the menu, as on the PC client. A menu whose
 * interaction flag is off can only be left. */
#include "feat_dialog_int.h"
#include "feat_dialog_gfx.h"
#include "feat_dialog_text.h"
#include "bitstream.h"
#include "rak.h"

#define MENUS   128
#define ROW_H   17.0f
#define HEAD_H  24.0f
#define FONT_SX 0.42f
#define FONT_SY 0.80f

#define COL_PANEL FD_RGBA(0, 0, 0, 190)
#define COL_BAR   FD_RGBA(60, 90, 140, 235)
#define COL_HI    FD_RGBA(90, 130, 200, 210)

typedef struct {
    int used;
    srpc_menu_t m;
} menu_t;

static menu_t menus[MENUS];
static int current = -1, sel, held[2];

static int row_ok(const srpc_menu_t *m, int row) { return row >= 0 && (unsigned)row < m->count[0] && m->row_enabled[row]; }

static int step(const srpc_menu_t *m, int from, int dir)
{
    int r = from + dir;
    while (r >= 0 && (unsigned)r < m->count[0]) {
        if (row_ok(m, r))
            return r;
        r += dir;
    }
    return from;
}

void fmn_init(const srpc_menu_t *m)
{
    if (m->id >= MENUS)
        return;
    if (current == (int)m->id)
        current = -1;
    menus[m->id].used = 1;
    menus[m->id].m = *m;
}

void fmn_show(const srpc_id_t *id)
{
    const srpc_menu_t *m;

    if (id->id >= MENUS || !menus[id->id].used)
        return;
    current = id->id;
    m = &menus[current].m;
    sel = row_ok(m, 0) ? 0 : step(m, 0, 1);
    held[0] = held[1] = 0;
}

void fmn_hide(const srpc_id_t *id)
{
    if ((int)id->id == current)
        current = -1;
}

int fmn_open(void) { return current >= 0; }

void fmn_reset(void)
{
    int i;
    for (i = 0; i < MENUS; i++)
        menus[i].used = 0;
    current = -1;
}

void fmn_input(void)
{
    static unsigned char buf[8];
    const srpc_menu_t *m;
    bs_t b;

    if (current < 0)
        return;
    m = &menus[current].m;
    if (fd_repeat(&held[0], fd_pad_up()))
        sel = step(m, sel, -1);
    if (fd_repeat(&held[1], fd_pad_down()))
        sel = step(m, sel, 1);
    if ((FD_PRESSED(cross) || FD_PRESSED(start)) && m->enabled && row_ok(m, sel)) {
        bs_wr_init(&b, buf, sizeof buf);
        srpc_enc_menu_select(&b, sel);
        if (!b.err)
            rak_rpc(SRPC_C2S_MENU_SELECT, buf, b.bits, RAK_RELIABLE, 0);
        current = -1;
    } else if (FD_PRESSED(circle) || FD_PRESSED(triangle)) {
        rak_rpc(SRPC_C2S_MENU_QUIT, 0, 0, RAK_RELIABLE, 0);
        current = -1;
    }
}

static void cell(float x, float y, const char *s, float limit, unsigned color)
{
    fd_layout_t *l = fd_scratch();
    fd_layout(l, s, FD_TOKENS, color, limit, 1);
    fd_print(l, x, y, ROW_H, FD_LEFT, 0, 1);
}

static float clamp(float v, float lo, float hi)
{
    if (!(v > lo)) /* also catches NaN */
        return lo;
    return v < hi ? v : hi;
}

void fmn_draw(void)
{
    const srpc_menu_t *m;
    float x, y, w1, w2, w, h, row_y;
    int has_head, i;

    if (current < 0)
        return;
    m = &menus[current].m;
    w1 = clamp(m->col1_width, 40.0f, 600.0f);
    w2 = m->two_columns ? clamp(m->col2_width, 0.0f, 600.0f) : 0.0f;
    w = w1 + w2;
    if (w > 620.0f)
        w = 620.0f;
    has_head = m->header[0][0] || m->header[1][0];
    h = HEAD_H + (has_head ? ROW_H : 0.0f) + m->count[0] * ROW_H + 6.0f;
    x = clamp(m->x, 0.0f, 640.0f - w);
    y = clamp(m->y, 0.0f, 448.0f - h);
    row_y = y + HEAD_H + (has_head ? ROW_H : 0.0f);

    fd_flush();
    fd_rect(x, y, x + w, y + h, COL_PANEL);
    fd_rect(x, y, x + w, y + HEAD_H, COL_BAR);
    if (row_ok(m, sel))
        fd_rect(x + 2.0f, row_y + sel * ROW_H, x + w - 2.0f, row_y + (sel + 1) * ROW_H, COL_HI);

    fd_font(FD_FONT_SUBTITLES, FONT_SX, FONT_SY, 1, 0, 1, FD_RGB(0, 0, 0));
    cell(x + 8.0f, y + 4.0f, m->title, w - 12.0f, FD_RGB(255, 255, 255));
    if (has_head) {
        cell(x + 8.0f, y + HEAD_H, m->header[0], w1 - 10.0f, FD_RGB(255, 200, 80));
        if (m->two_columns)
            cell(x + w1 + 4.0f, y + HEAD_H, m->header[1], w2 - 6.0f, FD_RGB(255, 200, 80));
    }
    for (i = 0; (unsigned)i < m->count[0]; i++) {
        unsigned c = m->row_enabled[i] ? FD_RGB(255, 255, 255) : FD_RGB(130, 130, 130);
        cell(x + 8.0f, row_y + i * ROW_H + 1.0f, m->items[0][i], w1 - 10.0f, c);
        if (m->two_columns && (unsigned)i < m->count[1])
            cell(x + w1 + 4.0f, row_y + i * ROW_H + 1.0f, m->items[1][i], w2 - 6.0f, c);
    }
    fd_flush();
}
