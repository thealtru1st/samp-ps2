#include "menu.h"
#include "fps.h"
#include "browser.h"
#include "ui_game.h"
#include "ui_font.h"
#include "ui.h"
#include "util.h"
#include "status.h"

#define ROWS        15
#define ROW_H       21.0f
#define LIST_TOP    86.0f
#define REPEAT_FIRST 12
#define REPEAT_NEXT  3

enum { VIEW_HIDDEN, VIEW_LIST, VIEW_EDIT };
enum { TAB_FAV, TAB_NET, TAB_COUNT };
enum { EDIT_ADDR, EDIT_NAME, EDIT_PASSWORD, EDIT_CHAT, EDIT_CALLBACK };

static const char set_addr[] = "0123456789.:abcdefghijklmnopqrstuvwxyz-";
static const char set_name[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_[]().$@=";
static const char set_text[] = " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?'-_/:()";

static int view, tab, sel, top;
static int edit_mode, edit_cursor, edit_len_max, join_index;
static char edit_buf[128], edit_prompt[32];
static char name[25] = "PS2_Player";
static unsigned join_ip, join_port;

static menu_pad_t cur, prev;
static int held[4], list_requested;
static char status[72];
static unsigned kb_seen;
static void (*edit_cb)(int accepted, const char *text);
static int edit_masked;

/* True while tools/kbd.py is running beside the emulator. */
static int kb_present(void) { return ST->frames - ST->kb_alive < 60u * fps_mult(); }

static int rows_in_tab(int *out, int max)
{
    int i, n = 0, total = browser_count();
    for (i = 0; i < total && n < max; i++) {
        const browser_server_t *s = browser_get(i);
        if (!s)
            continue;
        if (tab == TAB_FAV ? (s->flags & (BROWSER_F_FAVOURITE | BROWSER_F_MANUAL)) != 0
                           : (s->flags & BROWSER_F_LIST) != 0)
            out[n++] = i;
    }
    return n;
}

void menu_init(void)
{
    view = VIEW_LIST;
}

void menu_set_status(const char *text)
{
    unsigned i;
    for (i = 0; text[i] && i < sizeof status - 1; i++)
        status[i] = text[i];
    status[i] = 0;
}

void menu_open(void)
{
    view = VIEW_LIST;
    browser_query_all();
}

int menu_is_open(void) { return view != VIEW_HIDDEN; }

/* Junk can appear in the pad state before the controller is up, so nothing counts
 * until it has been seen idle once. */
void menu_pad(const menu_pad_t *pad)
{
    static int armed;
    const short *b = (const short *)pad;
    int i, any = 0;

    for (i = 4; i < 20; i++)
        if (b[i])
            any = 1;
    if (!armed) {
        armed = !any;
        return;
    }
    cur = *pad;
}

#define PRESSED(f) (cur.f && !prev.f)

/* Press, then auto-repeat while held, for moving through lists. */
static int repeat(int slot, int down)
{
    if (!down) {
        held[slot] = 0;
        return 0;
    }
    held[slot]++;
    if (held[slot] == 1)
        return 1;
    return held[slot] > REPEAT_FIRST * fps_mult() &&
           (held[slot] - REPEAT_FIRST * fps_mult()) % (REPEAT_NEXT * fps_mult()) == 0;
}

static const char *charset(void)
{
    return edit_mode == EDIT_ADDR ? set_addr : edit_mode == EDIT_NAME ? set_name : set_text;
}

static void edit_start(int mode, const char *prompt, const char *initial, int max)
{
    unsigned i;
    edit_mode = mode;
    edit_len_max = max;
    for (i = 0; i < sizeof edit_prompt - 1 && prompt[i]; i++)
        edit_prompt[i] = prompt[i];
    edit_prompt[i] = 0;
    for (i = 0; initial[i] && (int)i < max; i++)
        edit_buf[i] = initial[i];
    edit_buf[i] = 0;
    edit_cursor = i ? i - 1 : 0;
    if (!i && !kb_present()) {
        edit_buf[0] = charset()[0];
        edit_buf[1] = 0;
    }
    for (i = 0; i < sizeof edit_buf && i < 159; i++) {
        ST->edit_text[i] = edit_buf[i];
        if (!edit_buf[i])
            break;
    }
    ST->edit_text[159] = 0;
    kb_seen = ST->kb_seq;
    ST->edit_open = 1;
    view = VIEW_EDIT;
}

void menu_text_open(const char *prompt, int max_len, int masked,
                    void (*done)(int accepted, const char *text))
{
    if (max_len > (int)sizeof edit_buf - 1)
        max_len = sizeof edit_buf - 1;
    edit_cb = done;
    edit_masked = masked;
    edit_start(EDIT_CALLBACK, prompt, "", max_len);
}

static void cycle(int dir)
{
    const char *set = charset();
    int n = strlen(set), i, at = 0;
    for (i = 0; i < n; i++)
        if (set[i] == edit_buf[edit_cursor])
            at = i;
    edit_buf[edit_cursor] = set[(at + dir + n) % n];
}

static void join(unsigned ip, unsigned port, const char *password)
{
    if (menu_is_connected())
        menu_do_disconnect();
    menu_do_connect(ip, port, name, password);
    view = VIEW_HIDDEN;
}

static void edit_done(void)
{
    if (edit_mode == EDIT_CALLBACK) {
        view = VIEW_HIDDEN;
        edit_masked = 0;
        edit_cb(1, edit_buf);
    } else if (edit_mode == EDIT_ADDR) {
        int r = browser_add_ip(edit_buf);
        if (r >= 0)
            browser_fav_set(r, 1);
        tab = TAB_FAV;
        view = VIEW_LIST;
    } else if (edit_mode == EDIT_NAME) {
        unsigned i;
        for (i = 0; i < sizeof name - 1 && edit_buf[i]; i++)
            name[i] = edit_buf[i];
        name[i] = 0;
        view = VIEW_LIST;
    } else if (edit_mode == EDIT_PASSWORD) {
        join(join_ip, join_port, edit_buf);
    } else {
        menu_do_chat(edit_buf);
        view = VIEW_HIDDEN;
    }
}

static void edit_close(int next_view)
{
    ST->edit_open = 0;
    view = next_view;
}

static void edit_cancel(void)
{
    if (edit_mode == EDIT_CALLBACK) {
        edit_close(VIEW_HIDDEN);
        edit_masked = 0;
        edit_cb(0, "");
    } else {
        edit_close(edit_mode == EDIT_CHAT ? VIEW_HIDDEN : VIEW_LIST);
    }
}

/* Text from the PC keyboard replaces the box, and the cursor sits at its end. */
static void kb_input(void)
{
    if (ST->kb_seq != kb_seen) {
        int i;
        kb_seen = ST->kb_seq;
        for (i = 0; i < edit_len_max && ST->kb_text[i]; i++)
            edit_buf[i] = ST->kb_text[i];
        edit_buf[i] = 0;
        edit_cursor = i;
    }
    /* Only Enter: Space is the Cross button and has to stay a plain space here. */
    if (PRESSED(start)) {
        ST->edit_open = 0;
        if (edit_buf[0] || edit_mode == EDIT_CALLBACK)
            edit_done();
        else
            view = edit_mode == EDIT_CHAT ? VIEW_HIDDEN : VIEW_LIST;
    } else if (PRESSED(circle)) {
        edit_cancel();
    }
}

static void edit_input(void)
{
    int len = strlen(edit_buf);

    if (kb_present()) {
        kb_input();
        return;
    }

    if (repeat(0, cur.up))
        cycle(1);
    if (repeat(1, cur.down))
        cycle(-1);
    if (repeat(2, cur.right) && edit_cursor < edit_len_max - 1) {
        edit_cursor++;
        if (edit_cursor >= len) {
            edit_buf[edit_cursor] = edit_mode == EDIT_ADDR ? '0' : charset()[0];
            edit_buf[edit_cursor + 1] = 0;
        }
    }
    if (repeat(3, cur.left) && edit_cursor > 0)
        edit_cursor--;
    if (PRESSED(square) && len > 1) { /* delete at the cursor */
        int i;
        for (i = edit_cursor; i < len; i++)
            edit_buf[i] = edit_buf[i + 1];
        if (edit_cursor >= len - 1)
            edit_cursor = len - 2;
    }
    if (PRESSED(cross) || PRESSED(start)) {
        ST->edit_open = 0;
        edit_done();
    } else if (PRESSED(circle)) {
        edit_cancel();
    }
}

static void list_input(void)
{
    static int idx[BROWSER_MAX_SERVERS];
    int n = rows_in_tab(idx, BROWSER_MAX_SERVERS);

    if (repeat(0, cur.up) && sel > 0)
        sel--;
    if (repeat(1, cur.down) && sel < n - 1)
        sel++;
    if (PRESSED(l1) || PRESSED(r1)) {
        tab = (tab + 1) % TAB_COUNT;
        sel = top = 0;
        if (tab == TAB_NET && (!list_requested || browser_list_state() == BROWSER_LIST_FAILED)) {
            list_requested = 1;
            browser_refresh_internet();
        }
    }
    if (sel < top)
        top = sel;
    if (sel >= top + ROWS)
        top = sel - ROWS + 1;
    if ((PRESSED(cross) || PRESSED(start)) && sel < n) {
        const browser_server_t *s = browser_get(idx[sel]);
        if (s) {
            join_ip = s->ip;
            join_port = s->port;
            if (s->info.password)
                edit_start(EDIT_PASSWORD, "Server password", "", 31);
            else
                join(s->ip, s->port, "");
        }
    }
    if (PRESSED(square))
        edit_start(EDIT_ADDR, "Server address", "127.0.0.1:7777", 40);
    if (PRESSED(triangle))
        edit_start(EDIT_NAME, "Nickname", name, 24);
    if (PRESSED(circle) && sel < n)
        browser_fav_set(idx[sel], !(browser_get(idx[sel])->flags & BROWSER_F_FAVOURITE));
    if (PRESSED(l2))
        browser_query_all();
    if (PRESSED(r2))
        fps_high = !fps_high;
    if ((PRESSED(select) || PRESSED(r3)) && menu_is_connected())
        view = VIEW_HIDDEN;
}

void menu_tick(unsigned now)
{
    browser_tick(now);
    {   /* development: lets a test check the row before it presses Enter */
        static int idx[BROWSER_MAX_SERVERS];
        int n = rows_in_tab(idx, BROWSER_MAX_SERVERS);
        const browser_server_t *s = view == VIEW_LIST && sel < n ? browser_get(idx[sel]) : 0;
        ST->v[4] = s ? s->port : 0;
    }
    if (view == VIEW_LIST)
        list_input();
    else if (view == VIEW_EDIT)
        edit_input();
    else if (PRESSED(r3))
        menu_open();
    else if (PRESSED(select) && menu_is_connected())
        edit_start(EDIT_CHAT, "Say", "", 100);
    prev = cur; /* the tick can outrun the pad read: each press counts exactly once */
}

/* ---- drawing -------------------------------------------------------- */

static const ui_rgba panel = {0, 0, 0, 200}, bar = {60, 90, 140, 230}, hi = {90, 130, 200, 200};
static const ui_rgba white = {255, 255, 255, 255}, grey = {170, 170, 170, 255}, gold = {255, 200, 80, 255};
static const ui_rgba black = {0, 0, 0, 255};

static void rect(float l, float t, float r, float b, const ui_rgba *c)
{
    ui_rect q;
    q.left = l; q.right = r; q.y_top = t; q.y_bottom = b;
    CSprite2d_DrawRect(&q, c);
}

static void style(float sx, float sy, const ui_rgba *c)
{
    CFont_SetFontStyle(UI_FONT_SUBTITLES);
    CFont_SetScale(sx, sy);
    CFont_SetOrientation(UI_ALIGN_LEFT);
    CFont_SetProportional(1);
    CFont_SetJustify(0);
    CFont_SetSlant(0.0f);
    CFont_SetWrapx(640.0f);
    CFont_SetRightWrap(640.0f);
    CFont_SetAlphaFade(255.0f);
    CFont_SetColor(c);
    CFont_SetDropColor(&black);
    CFont_SetEdge(0);
    CFont_SetBackground(0, 0);
}

/* Copies text the font can show; '_', '~' and a leading '*' are control codes to it. */
static void safe(char *o, const char *s, unsigned max)
{
    unsigned n = 0;
    for (; *s && n < max - 1; s++) {
        unsigned char c = *s;
        if (c < 32 || c > 126 || c == '~' || c == '_')
            c = ' ';
        c = ui_font_char(c);
        if (c == '*' && !n)
            c = ' ';
        o[n++] = c;
    }
    o[n] = 0;
}

static char *put_int(char *p, unsigned v)
{
    char b[12];
    int i = 0;
    do { b[i++] = '0' + v % 10; v /= 10; } while (v);
    while (i)
        *p++ = b[--i];
    *p = 0;
    return p;
}

static char *put_str(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    *p = 0;
    return p;
}

static void draw_list(void)
{
    static int idx[BROWSER_MAX_SERVERS];
    int n = rows_in_tab(idx, BROWSER_MAX_SERVERS), i;
    char line[96], *p;

    rect(30, 26, 610, 420, &panel);
    rect(30, 26, 610, 54, &bar);
    rect(tab == TAB_FAV ? 30 : 130, 56, tab == TAB_FAV ? 130 : 230, 78, &bar);
    if (sel >= top && sel < top + ROWS && n)
        rect(32, LIST_TOP + (sel - top) * ROW_H - 2, 608, LIST_TOP + (sel - top + 1) * ROW_H - 2, &hi);
    CFont_DrawFonts();

    style(0.55f, 1.0f, &white);
    p = put_str(line, "SA-MP PS2   ");
    safe(p, name, 26);
    ui_PrintString(40, 32, line);
    style(0.40f, 0.76f, &white);
    ui_PrintString(42, 60, "Favourites");
    ui_PrintString(142, 60, "Internet");

    style(0.36f, 0.72f, &grey);
    if (tab == TAB_NET) {
        browser_list_state_t st = browser_list_state();
        ui_PrintString(250, 60, st == BROWSER_LIST_DONE ? "master list loaded"
                               : st == BROWSER_LIST_FAILED ? "list unavailable, try Favourites"
                               : st == BROWSER_LIST_IDLE ? "" : "fetching list...");
    }
    if (!n)
        ui_PrintString(42, LIST_TOP, tab == TAB_FAV ? "No favourites yet. Press Square to add a server."
                                                    : "Nothing here yet.");
    for (i = top; i < n && i < top + ROWS; i++) {
        const browser_server_t *s = browser_get(idx[i]);
        float y = LIST_TOP + (i - top) * ROW_H;
        if (!s)
            continue;
        style(0.36f, 0.72f, i == sel ? &white : &grey);
        if (s->flags & BROWSER_F_HAVE_INFO) {
            safe(line, s->info.hostname, 52);
            ui_PrintString(40, y, line);
            p = put_int(line, s->info.players);
            p = put_str(p, "/");
            p = put_int(p, s->info.max_players);
            ui_PrintString(450, y, line);
            if (s->ping_ms != BROWSER_PING_NONE) {
                p = put_int(line, s->ping_ms);
                put_str(p, "ms");
                ui_PrintString(530, y, line);
            }
        } else {
            p = put_int(line, s->ip >> 24); p = put_str(p, ".");
            p = put_int(p, (s->ip >> 16) & 255); p = put_str(p, ".");
            p = put_int(p, (s->ip >> 8) & 255); p = put_str(p, ".");
            p = put_int(p, s->ip & 255); p = put_str(p, ":");
            p = put_int(p, s->port);
            put_str(p, s->state == BROWSER_ST_OFFLINE ? "  offline" : "  ...");
            ui_PrintString(40, y, line);
        }
    }
    CFont_DrawFonts();
    if (status[0]) {
        style(0.36f, 0.72f, &white);
        safe(line, status, sizeof line);
        ui_PrintString(40, 382, line);
    }
    style(0.34f, 0.68f, &gold);
    ui_PrintString(40, 400, fps_high ? "Enter join   Shift add IP   F nickname   Click favourite   Tab lists   Q refresh   E 50 fps"
                                     : "Enter join   Shift add IP   F nickname   Click favourite   Tab lists   Q refresh   E 25 fps");
}

static void draw_edit(void)
{
    char before[132];
    float x;

    rect(90, 170, 550, 270, &panel);
    rect(90, 170, 550, 196, &bar);
    CFont_DrawFonts();
    style(0.45f, 0.86f, &white);
    ui_PrintString(100, 175, edit_prompt);
    style(0.45f, 0.9f, &white);
    safe(before, edit_buf, sizeof before);
    if (edit_mode == EDIT_PASSWORD || edit_masked) {
        int i;
        for (i = 0; before[i]; i++)
            before[i] = 'x';
    }
    ui_PrintString(100, 208, before);
    CFont_DrawFonts();

    /* caret under the active character */
    safe(before, edit_buf, sizeof before);
    if (edit_mode == EDIT_PASSWORD || edit_masked) {
        int i;
        for (i = 0; before[i]; i++)
            before[i] = 'x';
    }
    before[edit_cursor] = 0;
    x = 100.0f + (edit_cursor ? CFont_GetStringWidth(before, 1, 0) : 0.0f);
    rect(x, 228, x + 9.0f, 231, &gold);
    CFont_DrawFonts();
    style(0.32f, 0.64f, &gold);
    ui_PrintString(100, 246, kb_present() ? "Type on your keyboard   Enter ok   Click cancel"
                                          : "Up/Down letter   Left/Right move   Shift delete   Enter ok   Click cancel");
}

void menu_draw(void)
{
    if (view == VIEW_LIST)
        draw_list();
    else if (view == VIEW_EDIT)
        draw_edit();
}
