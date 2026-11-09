/* Host tests for the feat_dialog modules, under ASan and UBSan. Server messages go
 * through the real samp_rpc.c decoder, and answers are read back in open.mp's layout
 * (Shared/NetCode/dialog.hpp, textdraw.hpp, menu.hpp). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "feat_dialog_int.h"
#include "feat_dialog_pad.h"
#include "feat_dialog_gfx.h"
#include "feat_dialog_text.h"
#include "samp.h"
#include "rak.h"
#include "fake_gfx.h"

/* ---- stubs for what the payload links against -------------------------------- */
samp_stats_t samp_stats;
static int state = SAMP_SPAWNED, connected = 1, menu_up;
static unsigned now = 1000;

int samp_state(void) { return state; }
int menu_is_open(void) { return menu_up; }
int menu_is_connected(void) { return connected; }
void slog(const char *f, ...) {}

typedef struct { int id, bits; unsigned char d[512]; } sent_t;
static sent_t sent[64];
static int nsent;

int rak_rpc(int id, const void *data, int bits, int rel, int chan)
{
    if (nsent < 64) {
        sent[nsent].id = id;
        sent[nsent].bits = bits;
        if (bits)
            memcpy(sent[nsent].d, data, BS_BYTES(bits));
        nsent++;
    }
    return 0;
}

static void (*box_done)(int, const char *);
static char box_prompt[64];
static int box_max, box_masked;

void menu_text_open(const char *prompt, int max_len, int masked, void (*done)(int, const char *))
{
    snprintf(box_prompt, sizeof box_prompt, "%s", prompt);
    box_max = max_len;
    box_masked = masked;
    box_done = done;
    menu_up = 1;
}

static float remote_pos[3];
static int remote_on;
int game_remote_pos(unsigned id, float *p)
{
    if (!remote_on) return 0;
    memcpy(p, remote_pos, sizeof remote_pos);
    return 1;
}
int game_remote_handle(unsigned id) { return remote_on ? 55 : 0; }

/* ---- helpers ---------------------------------------------------------------- */
static int checks, failed;
#define CHECK(c) do { checks++; if (!(c)) { failed++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static samp_rpc_handlers_t handlers;

static void frame(const menu_pad_t *p)
{
    static const menu_pad_t idle;
    feat_dialog_pad(p ? p : &idle);
    now += 16;
    fake_clear();
    feat_dialog_tick(now);
    feat_dialog_draw();
}

static void idle_ms(int ms)
{
    int i;
    for (i = 0; i < ms / 16 + 1; i++)
        frame(0);
}

#define PRESS(field) do { menu_pad_t q; memset(&q, 0, sizeof q); q.field = 255; frame(&q); frame(0); } while (0)

static void wstr8(bs_t *b, const char *s) { bs_w8(b, strlen(s)); bs_wbytes(b, s, strlen(s)); }

static void show_dialog(int id, int style, const char *title, const char *b1, const char *b2, const char *body)
{
    static unsigned char buf[16384];
    bs_t b, r;
    bs_wr_init(&b, buf, sizeof buf);
    bs_w16(&b, (unsigned)id);
    bs_w8(&b, style);
    wstr8(&b, title);
    wstr8(&b, b1);
    wstr8(&b, b2);
    samp_write_compressed_str(&b, body);
    bs_rd_init(&r, buf, b.bits);
    samp_rpc_dispatch(SRPC_show_dialog, &r);
    idle_ms(260); /* past the input guard, and drawn once */
    nsent = 0;
}

typedef struct { int id, response, item; char text[260]; } answer_t;

static int read_i16(bs_t *b) { unsigned v = bs_r16(b); return v & 0x8000 ? (int)v - 65536 : (int)v; }

static answer_t last_answer(void)
{
    answer_t a;
    bs_t b;
    unsigned n;
    memset(&a, 0, sizeof a);
    a.id = -99;
    if (nsent < 1 || sent[nsent - 1].id != 62)
        return a;
    bs_rd_init(&b, sent[nsent - 1].d, sent[nsent - 1].bits);
    a.id = read_i16(&b);
    a.response = bs_r8(&b);
    a.item = read_i16(&b);
    n = bs_r8(&b);
    bs_rbytes(&b, a.text, n);
    a.text[n] = 0;
    if (b.err || bs_unread(&b) != 0)
        a.id = -98;
    return a;
}

/* ---- text ------------------------------------------------------------------- */
static void test_text(void)
{
    fd_layout_t *l = fd_scratch();

    fd_font(1, 1.0f / 18.0f, 1, 1, 0, 1, 0); /* one unit per character */
    fd_layout(l, "ab {FF0000}cd{00ff00} e", FD_TAGS, 0xFF112233u, FD_NOWRAP, 0);
    CHECK(l->nruns == 3);
    CHECK(!strcmp(l->buf + l->runs[0].at, "ab "));
    CHECK(l->runs[0].color == 0xFF112233u);
    CHECK(!strcmp(l->buf + l->runs[1].at, "cd"));
    CHECK(l->runs[1].color == FD_RGB(255, 0, 0));
    CHECK(l->runs[1].x == 3.0f);
    CHECK(!strcmp(l->buf + l->runs[2].at, " e"));
    CHECK(l->runs[2].color == FD_RGB(0, 255, 0));

    fd_layout(l, "(12345)x(GGGGGG)y(", FD_TAGS, 1, FD_NOWRAP, 0);
    CHECK(l->nruns == 1 && !strcmp(l->buf + l->runs[0].at, "(12345)x(GGGGGG)y("));

    fd_layout(l, "aaa bbb ccc", FD_TAGS, 1, 7.0f, 0);
    CHECK(l->lines == 2);
    CHECK(l->line_w[0] == 7.0f);

    fd_layout(l, "aaaaaaaaaa", FD_TAGS, 1, 4.0f, 1);
    CHECK(l->lines == 3);
    fd_layout(l, "aaaaaaaaaa", FD_TOKENS, 1, 4.0f, 0);
    CHECK(l->lines == 1);

    fd_layout(l, "one\ntwo\r\n\nfour", FD_TAGS, 1, FD_NOWRAP, 0);
    CHECK(l->lines == 4);

    fd_layout(l, "~r~red~n~~g~~h~green~w~x~s~y", FD_TOKENS, 0xFF000000u, FD_NOWRAP, 0);
    CHECK(l->lines == 2);
    CHECK(!strcmp(l->buf + l->runs[0].at, "red"));
    CHECK(l->runs[0].color == FD_RGB(180, 25, 29));
    CHECK(l->runs[1].color == FD_RGB(81, 156, 66));
    CHECK(l->runs[1].line == 1);

    fd_layout(l, "press ~k~~VEHICLE_HORN~ now", FD_TOKENS, 1, FD_NOWRAP, 0);
    CHECK(strstr(l->buf + l->runs[0].at, "(VEHICLE HORN)") != 0);
    fd_layout(l, "a~q~b~unterminated", FD_TOKENS, 1, FD_NOWRAP, 0);
    CHECK(l->nruns == 1 && !strcmp(l->buf + l->runs[0].at, "ab"));
    fd_layout(l, "tail~", FD_TOKENS, 1, FD_NOWRAP, 0);
    CHECK(!strcmp(l->buf + l->runs[0].at, "tail"));
    fd_layout(l, "~", FD_TOKENS, 1, FD_NOWRAP, 0);
    CHECK(l->nruns == 0);

    {   /* far more than the arrays hold */
        static char big[8000];
        memset(big, 'x', sizeof big - 1);
        fd_layout(l, big, FD_TAGS, 1, 5.0f, 1);
        CHECK(l->truncated);
        CHECK(l->lines <= l->line_cap);
    }
    CHECK(fd_clean_char('_') == ' ');
    CHECK(fd_clean_char('~') == '-');
    CHECK(fd_clean_char(0xE9) == 'e');
    CHECK(fd_clean_char(0xC7) == 'C');
    CHECK(fd_clean_char(0x85) == '?');
    {
        char out[8];
        fd_plain(out, "{FF0000}a_b~cdefghij", sizeof out);
        CHECK(!strcmp(out, "a b-cde"));
    }
}

/* ---- dialogs ---------------------------------------------------------------- */
static void test_msgbox(void)
{
    answer_t a;

    show_dialog(7, 0, "{FF0000}Rules", "OK", "No", "Line one\n{00FF00}green line");
    CHECK(feat_dialog_modal());
    CHECK(fake_find("Rules") >= 0);
    CHECK(fake_find("Line one") >= 0);
    CHECK(fake_find("OK") >= 0);
    CHECK(fake_find("No") >= 0);
    { int i = fake_find("green line"); CHECK(i >= 0 && fake_texts[i].color == FD_RGB(0, 255, 0)); }
    { int i = fake_find("Rules"); CHECK(i >= 0 && fake_texts[i].color == FD_RGB(255, 0, 0)); }
    PRESS(cross);
    a = last_answer();
    CHECK(a.id == 7 && a.response == 1 && a.item == -1 && !a.text[0]);
    CHECK(!feat_dialog_modal());

    show_dialog(8, 0, "t", "OK", "", "b");
    PRESS(circle);
    a = last_answer();
    CHECK(a.id == 8 && a.response == 0 && a.item == -1); /* cancel with no second button */

    show_dialog(9, 0, "t", "OK", "", "b");
    PRESS(start);
    CHECK(last_answer().response == 1);
}

static void test_guard(void)
{
    static unsigned char buf[512];
    bs_t b, r;
    menu_pad_t q;

    /* a button already held when the dialog arrives does nothing until pressed again */
    memset(&q, 0, sizeof q);
    q.cross = 255;
    frame(&q);
    show_dialog(3, 0, "t", "OK", "", "b");
    frame(0);
    feat_dialog_pad(&q);
    nsent = 0;
    PRESS(cross);
    CHECK(last_answer().id == 3);

    /* a press in the first moments is ignored */
    bs_wr_init(&b, buf, sizeof buf);
    bs_w16(&b, 4); bs_w8(&b, 0); wstr8(&b, "t"); wstr8(&b, "OK"); wstr8(&b, ""); samp_write_compressed_str(&b, "b");
    bs_rd_init(&r, buf, b.bits);
    samp_rpc_dispatch(SRPC_show_dialog, &r);
    nsent = 0;
    frame(0);
    PRESS(cross);
    CHECK(nsent == 0 && feat_dialog_modal());
    idle_ms(260);
    PRESS(cross);
    CHECK(last_answer().id == 4);
}

static void test_input(void)
{
    answer_t a;

    show_dialog(11, 1, "{FFFFFF}Your name", "Save", "Back", "Type it");
    CHECK(fake_find("Press Enter to type") >= 0);
    PRESS(cross);
    CHECK(nsent == 0);
    CHECK(menu_up && box_done && !box_masked && box_max == 127);
    CHECK(!strcmp(box_prompt, "Your name"));
    idle_ms(100);
    CHECK(feat_dialog_modal());
    menu_up = 0;
    box_done(1, "Carl_J ohnson");
    a = last_answer();
    CHECK(a.id == 11 && a.response == 1 && a.item == -1 && !strcmp(a.text, "Carl_J ohnson"));

    show_dialog(12, 3, "Login", "Go", "Quit", "Password?");
    PRESS(start);
    CHECK(box_masked == 1);
    menu_up = 0;
    box_done(0, "");           /* cancelled the box: dialog still up, nothing sent */
    idle_ms(50);
    CHECK(nsent == 0 && feat_dialog_modal());
    PRESS(cross);
    menu_up = 0;
    box_done(1, "");           /* empty is a real answer */
    a = last_answer();
    CHECK(a.id == 12 && a.response == 1 && !a.text[0]);

    show_dialog(13, 1, "t", "a", "b", "c");
    PRESS(circle);
    a = last_answer();
    CHECK(a.id == 13 && a.response == 0 && a.item == -1);

    /* the text box outlives its dialog: the late answer must not go to the next one */
    show_dialog(14, 1, "t", "a", "b", "c");
    PRESS(cross);
    menu_up = 0;
    show_dialog(15, 1, "t", "a", "b", "c");
    box_done(1, "for fourteen");
    CHECK(nsent == 0 && feat_dialog_modal());
    PRESS(circle);

    /* over-long text is cut to what the PC's edit box allows */
    show_dialog(16, 1, "t", "a", "b", "c");
    PRESS(cross);
    menu_up = 0;
    {
        char big[400];
        memset(big, 'z', sizeof big - 1);
        big[sizeof big - 1] = 0;
        box_done(1, big);
    }
    a = last_answer();
    CHECK(a.id == 16 && strlen(a.text) == 127);
}

static void test_list(void)
{
    answer_t a;
    static char body[4096];
    int i;

    show_dialog(21, 2, "Pick", "Buy", "Cancel", "/help\n/kill\n{FF0000}Red $3000\n");
    CHECK(fake_find("/kill") >= 0);
    PRESS(down);
    PRESS(down);
    PRESS(down); /* stops at the last row: a trailing newline is not a row */
    PRESS(cross);
    a = last_answer();
    CHECK(a.id == 21 && a.response == 1 && a.item == 2 && !strcmp(a.text, "{FF0000}Red $3000"));

    show_dialog(22, 2, "Pick", "Buy", "Cancel", "a\nb");
    PRESS(down);
    PRESS(up);
    PRESS(up);
    PRESS(circle);
    a = last_answer();
    CHECK(a.id == 22 && a.response == 0 && a.item == 0 && !strcmp(a.text, "a"));

    show_dialog(23, 4, "Tab", "Ok", "", "Deagle\t$5000\t100\nSawnoff\t$3\t2\nPistol\t$1\t1");
    CHECK(fake_find("Sawnoff") >= 0);
    CHECK(fake_find("$3") >= 0);
    if (fake_find("100") < 0) fake_dump();
    CHECK(fake_find("100") >= 0);
    { int i1 = fake_find("Deagle"), i2 = fake_find("$5000"); CHECK(i1 >= 0 && i2 >= 0 && fake_texts[i2].x > fake_texts[i1].x); }
    PRESS(down);
    PRESS(cross);
    a = last_answer();
    CHECK(a.item == 1 && !strcmp(a.text, "Sawnoff"));

    show_dialog(24, 5, "Hdr", "Ok", "", "Weapon\tPrice\nDeagle\t$5\nSawnoff\t$3");
    CHECK(fake_find("Weapon") >= 0);
    PRESS(cross);
    a = last_answer();
    CHECK(a.item == 0 && !strcmp(a.text, "Deagle")); /* the header is not an item */
    show_dialog(25, 5, "Hdr", "Ok", "", "Weapon\tPrice\nDeagle\t$5\nSawnoff\t$3");
    PRESS(down);
    PRESS(down);
    PRESS(cross);
    a = last_answer();
    CHECK(a.item == 1 && !strcmp(a.text, "Sawnoff"));

    /* long list: paging, and the selected row stays on screen */
    body[0] = 0;
    for (i = 0; i < 100; i++)
        sprintf(body + strlen(body), "row%d\n", i);
    show_dialog(26, 2, "Long", "Ok", "", body);
    { menu_pad_t q; memset(&q, 0, sizeof q); q.r2 = 255; frame(&q); frame(0); }
    CHECK(fake_find("row18") >= 0 && fake_find("row0") < 0);
    for (i = 0; i < 200; i++)
        PRESS(down);
    CHECK(fake_find("row99") >= 0);
    PRESS(cross);
    CHECK(last_answer().item == 99);

    /* left stick counts as the D-pad, and holding repeats */
    show_dialog(27, 2, "Hold", "Ok", "", body);
    { menu_pad_t q; memset(&q, 0, sizeof q); q.ly = 127; for (i = 0; i < 60; i++) frame(&q); frame(0); }
    PRESS(cross);
    a = last_answer();
    CHECK(a.item > 5 && a.item < 30);

    show_dialog(28, 2, "Empty", "Ok", "", "");
    PRESS(cross);
    a = last_answer();
    CHECK(a.id == 28 && a.item == -1);
}

static void test_hide_and_replace(void)
{
    show_dialog(31, 0, "t", "a", "", "first");
    show_dialog(32, 0, "t", "a", "", "second");
    CHECK(fake_find("second") >= 0 && fake_find("first") < 0);
    PRESS(cross);
    CHECK(last_answer().id == 32);

    show_dialog(33, 0, "t", "a", "", "b");
    show_dialog(-1, 0, " ", " ", " ", " ");
    CHECK(!feat_dialog_modal());
    PRESS(cross);
    CHECK(nsent == 0);  /* a hidden dialog gets no answer */

    show_dialog(34, 77, "t", "a", "", "bad style"); /* treated as a message */
    CHECK(fake_find("bad style") >= 0);
    PRESS(cross);
    CHECK(last_answer().item == -1);

    {   /* long text scrolls instead of leaving the screen */
        static char body[4000];
        int i;
        for (i = 0; i < 60; i++)
            sprintf(body + strlen(body), "line%d\n", i);
        show_dialog(35, 0, "t", "a", "", body);
        CHECK(fake_find("line0") >= 0 && fake_find("line40") < 0);
        for (i = 0; i < 80; i++)
            PRESS(down);
        CHECK(fake_find("line59") >= 0 && fake_find("line0") < 0);
        for (i = 0; i < fake_ntext; i++)
            CHECK(fake_texts[i].y >= 0.0f && fake_texts[i].y < 448.0f);
        PRESS(cross);
    }
}

/* ---- spawn after login ------------------------------------------------------- */
static int count_sent(int id)
{
    int i, n = 0;
    for (i = 0; i < nsent; i++)
        n += sent[i].id == id;
    return n;
}

static void test_spawn_retry(void)
{
    state = SAMP_SPAWNING;
    show_dialog(41, 3, "Login", "Go", "", "pw");
    idle_ms(3000);
    CHECK(count_sent(129) == 0);          /* nothing while the dialog is up */
    PRESS(cross);
    menu_up = 0;
    box_done(1, "secret");
    CHECK(count_sent(62) == 1 && count_sent(129) == 0);
    idle_ms(800);
    CHECK(count_sent(129) == 1);          /* once the script has had its turn */
    idle_ms(4100);
    CHECK(count_sent(129) == 2);          /* still refused: try again, slowly */
    show_dialog(42, 0, "Next", "Ok", "", "second dialog");
    nsent = 0;
    idle_ms(5000);
    CHECK(count_sent(129) == 0);          /* never over a dialog */
    PRESS(cross);
    idle_ms(800);
    CHECK(count_sent(129) == 1);
    state = SAMP_SPAWNED;
    nsent = 0;
    idle_ms(9000);
    CHECK(count_sent(129) == 0);          /* accepted */
    state = SAMP_SPAWNING;
    idle_ms(30000);
    CHECK(count_sent(129) == 0);          /* and it does not come back by itself */
    state = SAMP_SPAWNED;
}

/* ---- textdraws --------------------------------------------------------------- */
typedef struct {
    unsigned id, flags, style, selectable, letter_color, box_color, back_color;
    float lw, lh, tw, th, x, y;
    const char *text;
} td_in_t;

static void send_td(const td_in_t *t)
{
    static unsigned char buf[4096];
    bs_t b, r;
    int i;

    bs_wr_init(&b, buf, sizeof buf);
    bs_w16(&b, t->id);
    bs_w8(&b, t->flags);
    bs_wf(&b, t->lw); bs_wf(&b, t->lh);
    bs_w32(&b, t->letter_color);
    bs_wf(&b, t->tw); bs_wf(&b, t->th);
    bs_w32(&b, t->box_color);
    bs_w8(&b, 1); bs_w8(&b, 0);
    bs_w32(&b, t->back_color);
    bs_w8(&b, t->style);
    bs_w8(&b, t->selectable);
    bs_wf(&b, t->x); bs_wf(&b, t->y);
    bs_w16(&b, 0);
    for (i = 0; i < 4; i++) bs_wf(&b, 0.0f);
    bs_w16(&b, 0xFFFF); bs_w16(&b, 0xFFFF);
    bs_w16(&b, strlen(t->text));
    bs_wbytes(&b, t->text, strlen(t->text));
    bs_rd_init(&r, buf, b.bits);
    samp_rpc_dispatch(SRPC_show_textdraw, &r);
}

static void hide_td(unsigned id)
{
    unsigned char buf[4];
    bs_t b, r;
    bs_wr_init(&b, buf, sizeof buf);
    bs_w16(&b, id);
    bs_rd_init(&r, buf, b.bits);
    samp_rpc_dispatch(SRPC_hide_textdraw, &r);
}

static void set_td(unsigned id, const char *text)
{
    static unsigned char buf[2048];
    bs_t b, r;
    bs_wr_init(&b, buf, sizeof buf);
    bs_w16(&b, id);
    bs_w16(&b, strlen(text));
    bs_wbytes(&b, text, strlen(text));
    bs_rd_init(&r, buf, b.bits);
    samp_rpc_dispatch(SRPC_set_textdraw_text, &r);
}

static void select_td(int on, unsigned color)
{
    unsigned char buf[8];
    bs_t b, r;
    bs_wr_init(&b, buf, sizeof buf);
    bs_wbool(&b, on);
    bs_w32(&b, color);
    bs_rd_init(&r, buf, b.bits);
    samp_rpc_dispatch(SRPC_select_textdraw, &r);
}

static int click_id(void)
{
    bs_t b;
    if (nsent < 1 || sent[nsent - 1].id != 83)
        return -1;
    bs_rd_init(&b, sent[nsent - 1].d, sent[nsent - 1].bits);
    return bs_r16(&b);
}

static void test_textdraws(void)
{
    td_in_t t;
    int i;

    memset(&t, 0, sizeof t);
    t.id = 5; t.flags = 2 | 16; t.lw = 0.5f; t.lh = 1.2f; t.tw = 640; t.th = 20;
    t.letter_color = 0xFF3366CCu; t.x = 100; t.y = 50; t.style = 1;
    t.text = "Hello ~r~World~n~second";
    send_td(&t);
    frame(0);
    { int a = fake_find("Hello"), b = fake_find("World"), c = fake_find("second");
      CHECK(a >= 0 && b >= 0 && c >= 0);
      CHECK(fake_texts[a].color == 0xFF3366CCu);
      CHECK(fake_texts[b].color == FD_RGB(180, 25, 29));
      CHECK(fake_texts[a].x == 100.0f && fake_texts[a].y == 50.0f);
      CHECK(fake_texts[b].x > 100.0f);
      /* line step is 18 * sy, and sy is half the letter height */
      CHECK(fake_texts[c].y > 60.7f && fake_texts[c].y < 60.9f); }

    set_td(5, "Changed");
    frame(0);
    CHECK(fake_find("Changed") >= 0 && fake_find("Hello") < 0);

    /* centred: x is the middle, and TextSize y the width */
    t.id = 6; t.flags = 4 << 1 | 1 | 16; t.x = 320; t.y = 100; t.tw = 30; t.th = 200; t.box_color = 0x80000000u;
    t.text = "Mid";
    send_td(&t);
    frame(0);
    { int a = fake_find("Mid"); CHECK(a >= 0 && fake_texts[a].x < 320.0f); }
    CHECK(fake_nrect == 1);
    CHECK(fake_rects[0].l == 320.0f - 100.0f - 4.0f && fake_rects[0].r == 320.0f + 100.0f + 4.0f);
    CHECK(fake_rects[0].color == 0x80000000u);
    hide_td(6);

    /* right */
    t.id = 7; t.flags = 2 << 1 | 16; t.x = 600; t.y = 120; t.text = "Rgt";
    send_td(&t);
    frame(0);
    { int a = fake_find("Rgt"); CHECK(a >= 0 && fake_texts[a].x < 600.0f); }
    hide_td(7);

    /* per-player ids live above 2048 and are distinct from the same global id */
    t.id = 2048 + 5; t.flags = 2 | 16; t.x = 10; t.y = 10; t.text = "PerPlayer";
    send_td(&t);
    frame(0);
    CHECK(fake_find("PerPlayer") >= 0 && fake_find("Changed") >= 0);
    hide_td(2048 + 5);
    hide_td(5);
    frame(0);
    CHECK(fake_ntext == 0);

    /* ids out of range and pool exhaustion must be harmless */
    t.id = 60000; send_td(&t);
    for (i = 0; i < 2304; i++) { t.id = i; t.text = "x"; send_td(&t); }
    frame(0);
    CHECK(fake_ntext > 400);
    for (i = 0; i < 2304; i++) hide_td(i);
    hide_td(9999);
    set_td(9999, "nope");
    frame(0);
    CHECK(fake_ntext == 0);

    /* sprites */
    memset(&t, 0, sizeof t);
    t.id = 1; t.style = 4; t.letter_color = 0xFF0000FFu; t.x = 10; t.y = 20; t.tw = 100; t.th = 50; t.text = "LD_SPAC:white";
    t.lw = 1; t.lh = 1;
    send_td(&t);
    frame(0);
    CHECK(fake_nrect == 1 && fake_rects[0].r == 110.0f && fake_rects[0].b == 70.0f && fake_rects[0].color == 0xFF0000FFu);
    t.text = "loadsc12:loadsc12";
    send_td(&t);
    frame(0);
    CHECK(fake_nrect == 4); /* outline only, never a solid block over the screen */
    hide_td(1);

    /* NaN and absurd sizes never reach the game */
    memset(&t, 0, sizeof t);
    { unsigned nan = 0x7FC00000u; memcpy(&t.x, &nan, 4); }
    t.id = 2; t.lw = 1; t.lh = 1; t.style = 1; t.text = "nan";
    send_td(&t);
    frame(0);
    CHECK(fake_find("nan") < 0);
    hide_td(2);
}

static void test_selection(void)
{
    td_in_t t;

    memset(&t, 0, sizeof t);
    t.lw = 0.4f; t.lh = 1.0f; t.style = 1; t.flags = 2 | 16; t.letter_color = 0xFFFFFFFFu;
    t.selectable = 1; t.tw = 200; t.th = 20;
    t.id = 10; t.x = 100; t.y = 100; t.text = "Login"; send_td(&t);
    t.id = 2048 + 3; t.x = 100; t.y = 200; t.text = "Register"; send_td(&t);
    t.id = 12; t.x = 300; t.y = 200; t.tw = 400; t.text = "Quit"; send_td(&t);
    t.id = 13; t.x = 100; t.y = 300; t.selectable = 0; t.tw = 200; t.text = "Label"; send_td(&t);
    frame(0);

    CHECK(!feat_dialog_modal());
    select_td(1, 0xFF0000FFu); /* RGBA red */
    CHECK(feat_dialog_modal());
    frame(0);
    frame(0);
    { int i = fake_find("Login"); CHECK(i >= 0 && fake_texts[i].color == FD_RGB(255, 0, 0)); }
    nsent = 0;
    PRESS(cross);
    CHECK(click_id() == 10);
    CHECK(feat_dialog_modal());   /* stays on until the server or player ends it */

    PRESS(down);
    frame(0);
    { int i = fake_find("Register"); CHECK(i >= 0 && fake_texts[i].color == FD_RGB(255, 0, 0)); }
    PRESS(cross);
    CHECK(click_id() == 2048 + 3);  /* per-player id goes back in wire form */

    PRESS(right);
    PRESS(cross);
    CHECK(click_id() == 12);

    PRESS(down);                    /* Label is not selectable: nothing below */
    PRESS(cross);
    CHECK(click_id() == 12);

    /* hovering a textdraw that goes away */
    hide_td(12);
    frame(0);
    frame(0);
    PRESS(cross);
    CHECK(click_id() == 10);

    PRESS(circle);
    CHECK(click_id() == 0xFFFF);
    CHECK(!feat_dialog_modal());

    /* the server ending selection is reported the same way */
    select_td(1, 0xFF0000FFu);
    nsent = 0;
    select_td(0, 0);
    CHECK(click_id() == 0xFFFF);
    hide_td(10); hide_td(2048 + 3); hide_td(13);

    /* a dialog takes the pad from textdraw selection, and gives it back */
    t.id = 10; t.selectable = 1; t.text = "Login"; send_td(&t);
    select_td(1, 0xFF0000FFu);
    frame(0); frame(0);
    show_dialog(51, 0, "t", "a", "", "b");
    PRESS(cross);
    CHECK(last_answer().id == 51);
    nsent = 0;
    PRESS(cross);
    CHECK(click_id() == 10);
    select_td(0, 0);
    hide_td(10);
}

/* ---- menus ------------------------------------------------------------------- */
static void send_menu(unsigned id, int enabled, int rows, unsigned off_mask)
{
    static unsigned char buf[2048];
    char line[32];
    bs_t b, r;
    int i;

    bs_wr_init(&b, buf, sizeof buf);
    bs_w8(&b, id);
    bs_w32(&b, 0);
    memset(line, 0, sizeof line); strcpy(line, "Shop");
    bs_wbytes(&b, line, 32);
    bs_wf(&b, 200); bs_wf(&b, 100); bs_wf(&b, 180);
    bs_w32(&b, enabled);
    for (i = 0; i < 12; i++) bs_w32(&b, !(off_mask >> i & 1));
    memset(line, 0, sizeof line); strcpy(line, "Items");
    bs_wbytes(&b, line, 32);
    bs_w8(&b, rows);
    for (i = 0; i < rows; i++) {
        memset(line, 0, sizeof line);
        sprintf(line, "Item%d", i);
        bs_wbytes(&b, line, 32);
    }
    bs_rd_init(&r, buf, b.bits);
    samp_rpc_dispatch(SRPC_init_menu, &r);
}

static void menu_cmd(int rpc, unsigned id)
{
    unsigned char buf[4];
    bs_t b, r;
    bs_wr_init(&b, buf, sizeof buf);
    bs_w8(&b, id);
    bs_rd_init(&r, buf, b.bits);
    samp_rpc_dispatch(rpc, &r);
}

static void test_menus(void)
{
    bs_t b;

    send_menu(3, 1, 3, 1u << 1);
    frame(0);
    CHECK(!feat_dialog_modal());
    menu_cmd(SRPC_show_menu, 3);
    frame(0);
    CHECK(feat_dialog_modal());
    CHECK(fake_find("Shop") >= 0 && fake_find("Items") >= 0 && fake_find("Item2") >= 0);
    nsent = 0;
    PRESS(down); /* row 1 is off: skips to 2 */
    PRESS(cross);
    CHECK(nsent == 1 && sent[0].id == 132 && sent[0].bits == 8);
    bs_rd_init(&b, sent[0].d, 8);
    CHECK(bs_r8(&b) == 2);
    CHECK(!feat_dialog_modal());

    menu_cmd(SRPC_show_menu, 3);
    frame(0);
    nsent = 0;
    PRESS(circle);
    CHECK(nsent == 1 && sent[0].id == 140 && sent[0].bits == 0);

    menu_cmd(SRPC_show_menu, 3);
    menu_cmd(SRPC_hide_menu, 3);
    CHECK(!feat_dialog_modal());

    send_menu(4, 0, 2, 0); /* interaction off: can only leave */
    menu_cmd(SRPC_show_menu, 4);
    frame(0);
    nsent = 0;
    PRESS(cross);
    CHECK(nsent == 0 && feat_dialog_modal());
    PRESS(circle);
    CHECK(nsent == 1 && sent[0].id == 140);

    send_menu(200, 1, 2, 0);       /* id beyond the pool */
    menu_cmd(SRPC_show_menu, 200);
    menu_cmd(SRPC_show_menu, 90);  /* never defined */
    CHECK(!feat_dialog_modal());
    send_menu(5, 1, 1, 1);         /* no row can be chosen */
    menu_cmd(SRPC_show_menu, 5);
    frame(0);
    nsent = 0;
    PRESS(cross);
    CHECK(nsent == 0);
    PRESS(circle);
}

/* ---- hud --------------------------------------------------------------------- */
static void send_game_text(unsigned style, unsigned ms, const char *text)
{
    static unsigned char buf[1024];
    bs_t b, r;
    bs_wr_init(&b, buf, sizeof buf);
    bs_w32(&b, style); bs_w32(&b, ms); bs_w32(&b, strlen(text));
    bs_wbytes(&b, text, strlen(text));
    bs_rd_init(&r, buf, b.bits);
    samp_rpc_dispatch(SRPC_game_text, &r);
}

static void send_join(unsigned id, unsigned color, const char *name)
{
    srpc_player_join_t j;
    memset(&j, 0, sizeof j);
    j.id = id; j.color = color;
    snprintf(j.name, sizeof j.name, "%s", name);
    handlers.player_join(&j);
}

static void test_hud(void)
{
    send_game_text(3, 500, "~y~Hello~n~there");
    frame(0);
    CHECK(fake_find("HELLO") >= 0 || fake_find("Hello") >= 0);
    CHECK(fake_find("there") >= 0);
    idle_ms(600);
    CHECK(fake_find("Hello") < 0);
    send_game_text(99, 500, "bad style");
    frame(0);
    CHECK(fake_find("bad style") < 0);
    send_game_text(1, 5000, "one");
    send_game_text(2, 5000, "two");
    frame(0);
    CHECK(fake_find("one") >= 0 && fake_find("two") >= 0);
    send_game_text(1, 5000, "");
    send_game_text(2, 1, "");
    frame(0);
    CHECK(fake_find("one") < 0 && fake_find("two") < 0);

    /* scoreboard */
    send_join(1, 0x11223344u, "Alice_B");
    send_join(2, 0, "Bob");
    {
        srpc_score_t s = {1, 77, 30}, me = {0, 5, 12};
        handlers.score_and_ping(&s);
        handlers.score_and_ping(&me);
    }
    frame(0);
    CHECK(fake_find("Alice") < 0);
    nsent = 0;
    {
        menu_pad_t q;
        int i;
        memset(&q, 0, sizeof q);
        q.l1 = 255;
        for (i = 0; i < 10; i++) frame(&q);
        CHECK(count_sent(155) == 1);
        CHECK(fake_find("Alice B") >= 0 && fake_find("77") >= 0 && fake_find("You") >= 0);
        { int k = fake_find("Alice B"); CHECK(fake_texts[k].color == 0xFF443322u + 0 || fake_texts[k].color == (FD_FROM_RGBA(0x11223344u) | 0xFF000000u)); }
        CHECK(fake_find("3 players") >= 0);
        CHECK(!feat_dialog_modal()); /* the game keeps the pad */
        for (i = 0; i < 200; i++) frame(&q);
        CHECK(count_sent(155) == 2); /* no faster than the PC client */
    }
    frame(0);
    CHECK(fake_find("Alice") < 0);

    {   /* renames, and leaving */
        srpc_set_name_t n; srpc_player_quit_t q = {1, 0};
        memset(&n, 0, sizeof n);
        n.id = 2; n.success = 1; strcpy(n.name, "Robert");
        handlers.set_player_name(&n);
        handlers.player_quit(&q);
        menu_pad_t p;
        memset(&p, 0, sizeof p);
        p.l1 = 255;
        frame(&p);
        CHECK(fake_find("Robert") >= 0 && fake_find("Alice") < 0);
        frame(0);
    }

    /* tags and bubbles */
    remote_on = 1;
    fake_project_ok = 1;
    remote_pos[0] = 300; remote_pos[1] = 200; remote_pos[2] = 20; /* fake_project: z is the distance */
    frame(0);
    CHECK(fake_find("Robert (2)") >= 0);
    {
        srpc_nametag_t off = {2, 0}, on = {2, 1};
        handlers.show_nametag(&off);
        frame(0);
        CHECK(fake_find("Robert") < 0);
        handlers.show_nametag(&on);
    }
    {
        srpc_chat_bubble_t b;
        memset(&b, 0, sizeof b);
        b.id = 2; b.color = 0xFF00FFFFu; b.distance = 50; b.expire = 400;
        strcpy(b.text, "hi there");
        handlers.chat_bubble(&b);
        frame(0);
        { int k = fake_find("hi there"); CHECK(k >= 0 && fake_texts[k].color == FD_RGB(255, 0, 255)); }
        idle_ms(500);
        CHECK(fake_find("hi there") < 0);
    }
    remote_pos[2] = 100;
    frame(0);
    CHECK(fake_find("Robert") < 0); /* past the tag distance */

    /* markers: a streamed player gets one blip, the colour alpha can hide it */
    idle_ms(600);
    CHECK(fake_nblip == 1);
    {
        srpc_id_val_t c = {2, 0x00FF0000u};
        handlers.set_color(&c);
        idle_ms(600);
        CHECK(fake_nblip == 0);
    }
    remote_on = 0;
    fake_project_ok = 0;
}

static void test_session(void)
{
    td_in_t t;

    memset(&t, 0, sizeof t);
    t.id = 1; t.style = 1; t.lw = 1; t.lh = 1; t.text = "stale"; t.tw = 640;
    send_td(&t);
    show_dialog(61, 0, "t", "a", "", "stale dialog");
    send_menu(1, 1, 1, 0);
    menu_cmd(SRPC_show_menu, 1);
    select_td(1, 0xFFFFFFFFu);
    CHECK(feat_dialog_modal());

    /* lost connection: nothing stays on screen or holds the pad */
    connected = 0;
    CHECK(!feat_dialog_modal());
    frame(0);
    CHECK(fake_ntext == 0 && fake_nrect == 0);
    connected = 1;
    state = SAMP_CONNECTING;
    frame(0);
    state = SAMP_SPAWNED;
    frame(0);
    CHECK(!feat_dialog_modal());
    CHECK(fake_find("stale") < 0);

    /* the same via a reconnect that never lost menu_is_connected */
    show_dialog(62, 0, "t", "a", "", "stale dialog");
    state = SAMP_CONNECTING;
    frame(0);
    CHECK(!feat_dialog_modal());
    state = SAMP_SPAWNED;

    /* gamemode restart */
    send_td(&t);
    show_dialog(63, 0, "t", "a", "", "stale dialog");
    samp_rpc_dispatch(SRPC_game_mode_restart, &(bs_t){0});
    frame(0);
    CHECK(!feat_dialog_modal() && fake_find("stale") < 0);

    /* a second InitGame clears the screen; the first must not, a dialog can precede it */
    {
        srpc_init_game_t g;
        memset(&g, 0, sizeof g);
        g.nametags = 1;
        show_dialog(70, 0, "t", "a", "", "early dialog");
        handlers.init_game(&g);
        CHECK(feat_dialog_modal());
        handlers.init_game(&g);
        CHECK(!feat_dialog_modal());
    }

    /* the game's pause menu: never hold its pad, draw nothing over it */
    show_dialog(71, 0, "t", "a", "", "under pause");
    fake_paused = 1;
    CHECK(!feat_dialog_modal());
    frame(0);
    CHECK(fake_ntext == 0 && fake_nrect == 0);
    PRESS(cross);
    CHECK(nsent == 0);
    fake_paused = 0;
    frame(0);
    CHECK(feat_dialog_modal() && fake_find("under pause") >= 0);
    PRESS(cross);
    CHECK(last_answer().id == 71);

    /* while the browser or text box is up, input is theirs */
    show_dialog(64, 0, "t", "a", "", "b");
    menu_up = 1;
    PRESS(cross);
    CHECK(nsent == 0);
    { menu_pad_t q; memset(&q, 0, sizeof q); q.cross = 255; frame(&q); menu_up = 0; frame(&q); frame(0); }
    CHECK(nsent == 0 && feat_dialog_modal()); /* the press that closed it is spent */
    PRESS(cross);
    CHECK(last_answer().id == 64);
}

/* ---- fuzz: anything a server can send, then anything a player can press ---------- */
static unsigned rng = 12345;
static unsigned rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

static void fuzz_pad(void)
{
    menu_pad_t q;
    short *f = (short *)&q;
    int i;
    memset(&q, 0, sizeof q);
    for (i = 0; i < 20; i++)
        if (rnd() % 5 == 0)
            f[i] = (short)(rnd() % 255 - (i < 4 ? 127 : 0));
    frame(&q);
    if (menu_up && rnd() % 3 == 0) {
        menu_up = 0;
        if (box_done)
            box_done(rnd() & 1, "fuzz");
    }
}

static const unsigned char fuzz_ids[] = {
    SRPC_show_dialog, SRPC_show_textdraw, SRPC_hide_textdraw, SRPC_set_textdraw_text, SRPC_select_textdraw,
    SRPC_game_text, SRPC_chat_bubble, SRPC_init_menu, SRPC_show_menu, SRPC_hide_menu, SRPC_set_player_name,
    SRPC_player_join, SRPC_player_quit, SRPC_set_color, SRPC_show_nametag, SRPC_score_and_ping, SRPC_game_mode_restart,
};

static unsigned char rbyte(void)
{
    static const char mix[] = "~~~~{}nrgbhk_*\n\n\t\t FF0000abc";
    unsigned r = rnd();
    return r & 1 ? (unsigned char)mix[(r >> 1) % (sizeof mix - 1)] : (unsigned char)(r >> 3);
}

static void fuzz_random_rpc(void)
{
    static unsigned char buf[6000];
    unsigned n = rnd() % sizeof buf, i, id = fuzz_ids[rnd() % sizeof fuzz_ids];
    bs_t b;
    for (i = 0; i < n; i++)
        buf[i] = rbyte();
    bs_rd_init(&b, buf, n * 8);
    samp_rpc_dispatch(id, &b);
}

/* Well-formed framing with hostile contents, so the layers past the decoder are reached. */
static void fuzz_shaped(void)
{
    static char s[5000], title[300], b1[300], b2[300];
    unsigned n = rnd() % (rnd() % 8 ? 300 : sizeof s - 1), i;
    td_in_t t;

    for (i = 0; i < n; i++) {
        s[i] = rbyte();
        if (!s[i]) s[i] = '~';
    }
    s[n] = 0;
    n = rnd() % 256;
    for (i = 0; i < n; i++) { title[i] = rbyte(); if (!title[i]) title[i] = '{'; }
    title[n] = 0;
    strcpy(b1, "ok");
    strcpy(b2, rnd() & 1 ? "" : "no");
    switch (rnd() % 5) {
    case 0: {
        static unsigned char buf[16384];
        bs_t b, r;
        bs_wr_init(&b, buf, sizeof buf);
        bs_w16(&b, rnd() % 40);
        bs_w8(&b, rnd() % 8);
        wstr8(&b, title); wstr8(&b, b1); wstr8(&b, b2);
        samp_write_compressed_str(&b, s);
        bs_rd_init(&r, buf, b.bits);
        samp_rpc_dispatch(SRPC_show_dialog, &r);
        break;
    }
    case 1: {
        static const float f[] = {0, 1, -1, 0.5f, 1280, 640, 1e30f, -1e30f, 1e-30f, 1000000};
        unsigned u[8];
        memset(&t, 0, sizeof t);
        t.id = rnd() % 2400; t.flags = rnd() & 255; t.style = rnd() % 7; t.selectable = rnd() & 1;
        t.lw = f[rnd() % 10]; t.lh = f[rnd() % 10]; t.tw = f[rnd() % 10]; t.th = f[rnd() % 10];
        t.x = f[rnd() % 10]; t.y = f[rnd() % 10];
        t.letter_color = rnd(); t.box_color = rnd(); t.back_color = rnd();
        if (strlen(s) > 1000) s[1000] = 0;
        t.text = s;
        send_td(&t);
        (void)u;
        break;
    }
    case 2:
        if (strlen(s) > 500) s[500] = 0;
        send_game_text(rnd() % 8, rnd() % 500, s);
        break;
    case 3:
        select_td(rnd() & 1, rnd());
        break;
    default:
        send_menu(rnd() % 130, rnd() & 1, rnd() % 14, rnd());
        menu_cmd(SRPC_show_menu, rnd() % 130);
        break;
    }
}

static void fuzz(int rounds)
{
    int i;
    remote_on = 1;
    fake_project_ok = 1;
    for (i = 0; i < rounds; i++) {
        unsigned k = rnd() % 10;
        if (k < 3)
            fuzz_random_rpc();
        else if (k < 6)
            fuzz_shaped();
        else if (k == 6)
            now += rnd() % 100000; /* also walks past the wrap of the clock in time */
        fuzz_pad();
        nsent = 0;
        if (rnd() % 500 == 0)
            connected = !connected;
        if (rnd() % 500 == 0)
            state = rnd() % 7;
    }
    remote_on = 0;
    fake_project_ok = 0;
    connected = 1;
    state = SAMP_SPAWNED;
    menu_up = 0;
}

int main(int argc, char **argv)
{
    int rounds = argc > 1 ? atoi(argv[1]) : 200000;

    feat_dialog_init(&handlers);
    samp_rpc_set_handlers(&handlers);
    test_text();
    test_msgbox();
    test_guard();
    test_input();
    test_list();
    test_hide_and_replace();
    test_spawn_retry();
    test_textdraws();
    test_selection();
    test_menus();
    test_hud();
    test_session();
    fuzz(rounds);
    printf("%d checks, %d failed, %d fuzz rounds, stats decoded=%u truncated=%u\n",
           checks, failed, rounds, samp_rpc_stats.decoded, samp_rpc_stats.truncated);
    return failed != 0;
}
