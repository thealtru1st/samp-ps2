/* Server dialogs: RPC 61 in, RPC 62 out. All six styles.
 *
 * Buttons follow the PC client: the left button answers 1 with the input or the list
 * row, and the right button or cancel answers 0, even when the server gave no right
 * button. open.mp drops an answer whose id is not the active dialog, whose list item is
 * not -1 for a msgbox or input style, or whose item is out of range, so those three
 * have to be right or the player waits forever. */
#include "feat_dialog_int.h"
#include "feat_dialog_gfx.h"
#include "feat_dialog_text.h"
#include "bitstream.h"
#include "rak.h"
#include "util.h"

enum { STYLE_MSGBOX, STYLE_INPUT, STYLE_LIST, STYLE_PASSWORD, STYLE_TABLIST, STYLE_TABLIST_HEADERS };

#define PANEL_L     50.0f
#define PANEL_W     540.0f
#define PAD_X       12.0f
#define BAR_H       24.0f
#define LINE_H      15.0f
#define FONT_SX     0.40f
#define FONT_SY     0.76f
#define VIS_TEXT    20    /* body lines shown before it scrolls */
#define VIS_INPUT   16
#define VIS_LIST    18
#define MAX_COLS    4
#define COL_GAP     14.0f
#define CELL_MAX    255
#define INPUT_MAX   127   /* the PC client's edit box */
#define INPUT_GUARD_MS 200 /* a press landing as the dialog appears was not meant for it */
#define BODY_LINES  512
#define BODY_RUNS   1024

#define COL_PANEL   FD_RGBA(0, 0, 0, 215)
#define COL_BAR     FD_RGBA(60, 90, 140, 235)
#define COL_HI      FD_RGBA(90, 130, 200, 210)
#define COL_FIELD   FD_RGBA(255, 255, 255, 40)
#define COL_WHITE   FD_RGB(255, 255, 255)
#define COL_HEAD    FD_RGB(255, 200, 80)
#define COL_HINT    FD_RGB(170, 170, 170)

static struct {
    int active, id;
    unsigned style, gen, opened_at;
    char title[SRPC_TITLE_MAX], button1[SRPC_BUTTON_MAX], button2[SRPC_BUTTON_MAX];
    char body[SRPC_DIALOG_MAX];
    unsigned short row[SRPC_DIALOG_MAX + 1]; /* list styles: start of each line in body */
    unsigned rows, items;
    int sel, top, vis, dirty;
    int held[2];
    int ncols;
    float col_x[MAX_COLS + 1];
    unsigned text_gen;
} d;

static char body_buf[SRPC_DIALOG_MAX + 1024];
static fd_run_t body_runs[BODY_RUNS];
static float body_w[BODY_LINES];
static fd_layout_t body = {body_buf, sizeof body_buf, 0, body_runs, BODY_RUNS, 0, body_w, BODY_LINES, 0, 0};

static int is_list(void)
{
    return d.style == STYLE_LIST || d.style == STYLE_TABLIST || d.style == STYLE_TABLIST_HEADERS;
}

static int is_input(void) { return d.style == STYLE_INPUT || d.style == STYLE_PASSWORD; }
static int has_header(void) { return d.style == STYLE_TABLIST_HEADERS; }

/* Cuts the body into lines in place. Like open.mp's own count, a final newline does not
 * start another row. */
static void split_rows(void)
{
    unsigned i = 0, start = 0;

    d.rows = 0;
    while (d.body[i]) {
        if (d.body[i] == '\n') {
            d.body[i] = 0;
            d.row[d.rows++] = start;
            start = i + 1;
        }
        i++;
    }
    if (i > start)
        d.row[d.rows++] = start;
    d.items = d.rows;
    if (has_header() && d.items)
        d.items--;
}

void fdlg_show(const srpc_dialog_t *in)
{
    if (in->id < 0) { /* the server's "hide": no answer goes back */
        d.active = 0;
        return;
    }
    slog("dialog: show %d style %d", in->id, in->style);
    d.gen++;
    d.active = 1;
    d.id = in->id;
    d.style = in->style <= STYLE_TABLIST_HEADERS ? in->style : STYLE_MSGBOX;
    d.opened_at = fd_now;
    fd_copy(d.title, in->title, sizeof d.title);
    fd_copy(d.button1, in->button1, sizeof d.button1);
    fd_copy(d.button2, in->button2, sizeof d.button2);
    fd_copy(d.body, in->body, sizeof d.body);
    d.sel = d.top = 0;
    d.vis = 1;
    d.held[0] = d.held[1] = 0;
    d.rows = d.items = 0;
    d.ncols = 1;
    if (is_list())
        split_rows();
    d.dirty = 1;
}

int fdlg_open(void) { return d.active; }
void fdlg_reset(void) { d.active = 0; }

static void respond(int response, int item, const char *text)
{
    static unsigned char buf[8 + 256];
    bs_t b;

    slog("dialog: answer %d button %d item %d", d.id, response, item); /* never the text */
    bs_wr_init(&b, buf, sizeof buf);
    srpc_enc_dialog_response(&b, d.id, response, item, text);
    if (!b.err)
        rak_rpc(SRPC_C2S_DIALOG_RESPONSE, buf, b.bits, RAK_RELIABLE, 0);
    d.active = 0;
    fd_spawn_retry_arm();
}

/* The selected row's first column, as the script gets it in inputtext. */
static void row_text(char *out, unsigned cap, unsigned row)
{
    const char *s = d.body + d.row[row];
    unsigned n = 0;
    while (s[n] && s[n] != '\t' && s[n] != '\r' && n < cap - 1) {
        out[n] = s[n];
        n++;
    }
    out[n] = 0;
}

static void text_done(int accepted, const char *text)
{
    char out[INPUT_MAX + 1];

    if (!d.active || d.gen != d.text_gen || !accepted)
        return;
    fd_copy(out, text, sizeof out);
    respond(1, -1, out);
}

static void accept(void)
{
    if (is_list()) {
        char text[INPUT_MAX + 1];
        if (!d.items) {
            respond(1, -1, "");
            return;
        }
        row_text(text, sizeof text, d.sel + has_header());
        respond(1, d.sel, text);
    } else if (is_input()) {
        if (menu_text_open) {
            char prompt[32];
            fd_plain(prompt, d.title, sizeof prompt);
            d.text_gen = d.gen;
            menu_text_open(prompt, INPUT_MAX, d.style == STYLE_PASSWORD, text_done);
        } else {
            respond(1, -1, ""); /* no way to type: better than leaving the player stuck */
        }
    } else {
        respond(1, -1, "");
    }
}

static void cancel(void)
{
    if (is_list()) {
        char text[INPUT_MAX + 1];
        if (!d.items) {
            respond(0, -1, "");
            return;
        }
        row_text(text, sizeof text, d.sel + has_header());
        respond(0, d.sel, text);
    } else {
        respond(0, -1, "");
    }
}

static void move(int delta)
{
    int count = is_list() ? (int)d.items : (int)body.lines;

    if (is_list()) {
        d.sel += delta;
        if (d.sel >= count)
            d.sel = count - 1;
        if (d.sel < 0)
            d.sel = 0;
        if (d.sel < d.top)
            d.top = d.sel;
        if (d.sel >= d.top + d.vis)
            d.top = d.sel - d.vis + 1;
    } else {
        d.top += delta;
        if (d.top > count - d.vis)
            d.top = count - d.vis;
        if (d.top < 0)
            d.top = 0;
    }
}

void fdlg_input(void)
{
    if (!d.active || d.dirty) /* the first draw measures it */
        return;
    if (fd_repeat(&d.held[0], fd_pad_up()))
        move(-1);
    if (fd_repeat(&d.held[1], fd_pad_down()))
        move(1);
    if (FD_PRESSED(l2))
        move(-d.vis);
    if (FD_PRESSED(r2))
        move(d.vis);
    if (fd_now - d.opened_at < INPUT_GUARD_MS)
        return;
    if (FD_PRESSED(cross) || FD_PRESSED(start))
        accept();
    else if (FD_PRESSED(circle) || FD_PRESSED(triangle))
        cancel();
}

/* ---- drawing ------------------------------------------------------------ */

static void font(void) { fd_font(FD_FONT_SUBTITLES, FONT_SX, FONT_SY, 1, 0, 1, FD_RGB(0, 0, 0)); }

/* Copies one tab-separated cell of a row, returning where the next one starts or 0. */
static const char *next_cell(const char *s, char *cell)
{
    unsigned n = 0;
    while (*s && *s != '\t') {
        if (n < CELL_MAX)
            cell[n++] = *s;
        s++;
    }
    cell[n] = 0;
    return *s ? s + 1 : 0;
}

static void measure_columns(void)
{
    static char cell[CELL_MAX + 1];
    float w[MAX_COLS] = {0, 0, 0, 0}, total = 0.0f, room, x, scale;
    unsigned r;
    int c;

    d.ncols = 1;
    if (d.style == STYLE_LIST) {
        d.col_x[0] = 0.0f;
        d.col_x[1] = PANEL_W - PAD_X * 2.0f;
        return;
    }
    for (r = 0; r < d.rows; r++) {
        const char *s = d.body + d.row[r];
        for (c = 0; s && c < MAX_COLS; c++) {
            fd_layout_t *l = fd_scratch();
            s = next_cell(s, cell);
            fd_layout(l, cell, FD_TAGS, COL_WHITE, FD_NOWRAP, 0);
            if (l->line_w[0] > w[c])
                w[c] = l->line_w[0];
            if (c + 1 > d.ncols)
                d.ncols = c + 1;
        }
    }
    room = PANEL_W - PAD_X * 2.0f - COL_GAP * (d.ncols - 1);
    for (c = 0; c < d.ncols; c++)
        total += w[c];
    scale = total > room && total > 0.0f ? room / total : 1.0f;
    x = 0.0f;
    for (c = 0; c < d.ncols; c++) {
        d.col_x[c] = x;
        x += w[c] * scale + COL_GAP;
    }
    d.col_x[d.ncols] = x;
}

static void relayout(void)
{
    font();
    if (is_list()) {
        measure_columns();
        d.vis = d.items < VIS_LIST ? (int)d.items : VIS_LIST;
    } else {
        int most = is_input() ? VIS_INPUT : VIS_TEXT;
        fd_layout(&body, d.body, FD_TAGS, COL_WHITE, PANEL_W - PAD_X * 2.0f, 1);
        d.vis = (int)body.lines < most ? (int)body.lines : most;
    }
    if (d.vis < 1)
        d.vis = 1;
    d.dirty = 0;
}

static void cell_text(float x, float y, const char *s, float limit, unsigned color)
{
    fd_layout_t *l = fd_scratch();
    fd_layout(l, s, FD_TAGS, color, limit, 1);
    fd_print(l, x, y, LINE_H, FD_LEFT, 0, 1);
}

static void row_cells(float x, float y, unsigned row, unsigned color)
{
    static char cell[CELL_MAX + 1];
    const char *s = d.body + d.row[row];
    int c;

    for (c = 0; s && c < d.ncols; c++) {
        s = next_cell(s, cell);
        cell_text(x + d.col_x[c], y, cell, d.col_x[c + 1] - d.col_x[c] - (d.ncols > 1 ? COL_GAP - 2.0f : 0.0f), color);
    }
}

static void button_text(float x, float y, const char *key, const char *label)
{
    static char line[SRPC_BUTTON_MAX + 16];
    fd_layout_t *l = fd_scratch();
    char *p = fd_put_str(line, key, 8);
    fd_put_str(p, label, SRPC_BUTTON_MAX);
    fd_layout(l, line, FD_TAGS, COL_WHITE, PANEL_W * 0.45f, 1);
    fd_print(l, x, y, LINE_H, FD_LEFT, 0, 1);
}

void fdlg_draw(void)
{
    float h, top, y, x = PANEL_L + PAD_X;
    int header, i;

    if (!d.active)
        return;
    if (d.dirty)
        relayout();
    header = has_header() && d.rows;
    h = BAR_H + 8.0f + d.vis * LINE_H + BAR_H + 6.0f;
    if (header)
        h += LINE_H + 3.0f;
    if (is_input())
        h += BAR_H + 6.0f;
    top = (448.0f - h) * 0.5f;

    fd_flush();
    fd_rect(PANEL_L, top, PANEL_L + PANEL_W, top + h, COL_PANEL);
    fd_rect(PANEL_L, top, PANEL_L + PANEL_W, top + BAR_H, COL_BAR);
    y = top + BAR_H + 6.0f + (header ? LINE_H + 3.0f : 0.0f);
    if (is_list() && d.items)
        fd_rect(PANEL_L + 3.0f, y + (d.sel - d.top) * LINE_H - 1.0f,
                PANEL_L + PANEL_W - 3.0f, y + (d.sel - d.top + 1) * LINE_H - 1.0f, COL_HI);
    if (is_input())
        fd_rect(x, y + d.vis * LINE_H + 4.0f, PANEL_L + PANEL_W - PAD_X,
                y + d.vis * LINE_H + 4.0f + BAR_H - 2.0f, COL_FIELD);
    if ((is_list() ? (int)d.items : (int)body.lines) > d.vis) {
        int count = is_list() ? (int)d.items : (int)body.lines;
        float track = d.vis * LINE_H, bar = track * d.vis / count;
        float at = y + (track - bar) * d.top / (count - d.vis);
        fd_rect(PANEL_L + PANEL_W - 6.0f, at, PANEL_L + PANEL_W - 3.0f, at + bar, COL_HEAD);
    }

    font();
    cell_text(x, top + 5.0f, d.title, PANEL_W - PAD_X * 2.0f, COL_WHITE);
    if (is_list()) {
        if (header)
            row_cells(x, y - LINE_H - 3.0f, 0, COL_HEAD);
        for (i = 0; i < d.vis && d.top + i < (int)d.items; i++)
            row_cells(x, y + i * LINE_H, d.top + i + header, COL_WHITE);
    } else {
        fd_print(&body, x, y, LINE_H, FD_LEFT, d.top, d.vis);
    }
    if (is_input())
        cell_text(x + 6.0f, y + d.vis * LINE_H + 8.0f,
                  menu_text_open ? "Press Enter to type" : "No text entry available",
                  PANEL_W - 40.0f, COL_HINT);
    y = top + h - BAR_H + 3.0f;
    button_text(x, y, "Enter: ", d.button1);
    if (d.button2[0])
        button_text(PANEL_L + PANEL_W * 0.55f, y, "Click: ", d.button2);
    fd_flush();
}
