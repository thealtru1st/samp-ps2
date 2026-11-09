/* Game text, the player table, name tags, chat bubbles, markers and the scoreboard.
 *
 * Name tags, bubbles and markers need to know where remote players are, which only
 * game.c knows; until it offers game_remote_pos and game_remote_handle they stay off.
 * The player table fills from RPCs samp.c handles inline: see the report. */
#include "feat_dialog_int.h"
#include "feat_dialog_gfx.h"
#include "feat_dialog_text.h"
#include "samp.h"
#include "rak.h"
#include "util.h"

#define PLAYERS         1000
#define GT_STYLES       7
#define GT_DEFAULT_MS   3000
#define SCORE_ROWS      22
#define SCORE_REQUEST_MS 3000 /* the PC client's rate limit */
#define TAG_DISTANCE    70.0f /* until InitGame says otherwise */
#define MARKER_MS       500
#define BUBBLE_MAX      SRPC_CHAT_MAX
#define MAX_BUBBLES     16
#define HEAD_Z          1.0f
#define BUBBLE_Z        1.3f

enum { MARKERS_OFF, MARKERS_GLOBAL, MARKERS_STREAMED };

typedef struct {
    unsigned char used, hidden_tag, npc, streamed_blip;
    unsigned color;     /* RGBA as the server sends it; 0 means the default for this id */
    int score, ping;
    int blip;
    char name[SRPC_NAME_MAX];
} player_t;

typedef struct {
    int used;
    unsigned id, color, until;
    float distance;
    char text[BUBBLE_MAX];
} bubble_t;

static player_t players[PLAYERS];
static bubble_t bubbles[MAX_BUBBLES];
static unsigned top_player; /* one past the highest id seen */

static struct { unsigned until; char text[SRPC_GAMETEXT_MAX]; } game_texts[GT_STYLES];

static float tag_distance = TAG_DISTANCE;
static int tags_on = 1;
static unsigned marker_mode = MARKERS_STREAMED, marker_at;

static int init_seen;
static int score_open, score_top, score_held[2];
static unsigned score_requested;

/* open.mp's default colours for the first few ids (MPL-2.0, Server/Source/player_pool.hpp).
 * The real list repeats every 100; this short one only has to tell players apart. */
static const unsigned default_colors[] = {
    0xFF8C13FFu, 0xC715FFFFu, 0x20B2AAFFu, 0xDC143CFFu, 0x6495EDFFu, 0xF0E68CFFu,
};

static unsigned player_color(const player_t *p, unsigned id)
{
    return p->color ? p->color : default_colors[id % (sizeof default_colors / sizeof default_colors[0])];
}

static player_t *touch(unsigned id)
{
    if (id >= PLAYERS)
        return 0;
    if (id >= top_player)
        top_player = id + 1;
    return &players[id];
}

/* ---- RPC handlers ------------------------------------------------------- */

/* A server may show a dialog before its first InitGame, but a second one means the
 * gamemode restarted, which is also how we hear of it if another module holds that slot. */
static void on_init_game(const srpc_init_game_t *g)
{
    if (init_seen)
        fd_reset_all();
    init_seen = 1;
    tags_on = g->nametags;
    if (g->nametag_distance > 0.0f && g->nametag_distance < 10000.0f)
        tag_distance = g->nametag_distance;
    marker_mode = g->markers;
}

static void on_join(const srpc_player_join_t *j)
{
    player_t *p = touch(j->id);
    if (!p)
        return;
    memset(p, 0, sizeof *p);
    p->used = 1;
    p->color = j->color;
    p->npc = j->npc != 0;
    fd_copy(p->name, j->name, sizeof p->name);
}

static void drop_blip(player_t *p)
{
    if (p->blip)
        fd_blip_remove(p->blip);
    p->blip = 0;
}

static void on_quit(const srpc_player_quit_t *q)
{
    if (q->id < PLAYERS) {
        drop_blip(&players[q->id]);
        players[q->id].used = 0;
    }
}

static void on_set_name(const srpc_set_name_t *s)
{
    player_t *p = touch(s->id);
    if (p && s->success)
        fd_copy(p->name, s->name, sizeof p->name);
}

static void on_set_color(const srpc_id_val_t *c)
{
    player_t *p = touch(c->id);
    if (!p)
        return;
    p->color = c->v;
    if (p->blip)
        fd_blip_colour(p->blip, player_color(p, c->id));
}

static void on_nametag(const srpc_nametag_t *n)
{
    player_t *p = touch(n->id);
    if (p)
        p->hidden_tag = !n->show;
}

static void on_score(const srpc_score_t *s)
{
    player_t *p = touch(s->id);
    if (!p)
        return;
    p->used = 1;
    p->score = (int)s->score;
    p->ping = (int)s->ping;
}

static void on_game_text(const srpc_game_text_t *g)
{
    if (g->style >= GT_STYLES)
        return;
    fd_copy(game_texts[g->style].text, g->text, sizeof game_texts[g->style].text);
    /* 1 so that "until" is never the idle value of 0 */
    game_texts[g->style].until = (fd_now + (g->time ? g->time : GT_DEFAULT_MS)) | 1;
    if (!g->text[0])
        game_texts[g->style].until = 0;
}

static void on_bubble(const srpc_chat_bubble_t *b)
{
    int i, slot = -1;

    for (i = 0; i < MAX_BUBBLES; i++) {
        if (bubbles[i].used && bubbles[i].id == b->id) {
            slot = i;
            break;
        }
        if (!bubbles[i].used && slot < 0)
            slot = i;
    }
    if (slot < 0)
        return;
    bubbles[slot].used = 1;
    bubbles[slot].id = b->id;
    bubbles[slot].color = FD_FROM_RGBA(b->color) | 0xFF000000u;
    bubbles[slot].distance = b->distance;
    bubbles[slot].until = fd_now + b->expire;
    fd_copy(bubbles[slot].text, b->text, sizeof bubbles[slot].text);
}

/* ---- markers ------------------------------------------------------------ */

static int marker_visible(const player_t *p, unsigned id)
{
    return marker_mode != MARKERS_OFF && (player_color(p, id) & 255) != 0;
}

/* Streamed players get a blip that follows the ped. Others come from sync packet 208. */
static void update_markers(void)
{
    unsigned id;

    if (!game_remote_handle)
        return;
    for (id = 0; id < top_player; id++) {
        player_t *p = &players[id];
        int handle;
        if (!p->used || id == samp_stats.player_id)
            continue;
        handle = game_remote_handle(id);
        if (handle && marker_visible(p, id)) {
            if (p->blip && !p->streamed_blip)
                drop_blip(p);
            if (!p->blip) {
                p->blip = fd_blip_for_char(handle);
                p->streamed_blip = 1;
                if (p->blip)
                    fd_blip_colour(p->blip, player_color(p, id));
            }
        } else if (p->blip && p->streamed_blip) {
            drop_blip(p);
        }
    }
}

/* Packet 208: position of a player we cannot see. No opcode moves a blip, so it is
 * replaced. */
static void on_marker(const srpc_marker_t *m)
{
    float pos[3];
    player_t *p = touch(m->id);

    if (!p || !p->used || m->id == samp_stats.player_id || p->streamed_blip)
        return;
    drop_blip(p);
    if (!m->active || !marker_visible(p, m->id))
        return;
    pos[0] = (float)m->x;
    pos[1] = (float)m->y;
    pos[2] = (float)m->z;
    p->blip = fd_blip_for_coord(pos);
    if (p->blip)
        fd_blip_colour(p->blip, player_color(p, m->id));
}

/* ---- per frame ---------------------------------------------------------- */

void fhud_init(samp_rpc_handlers_t *h)
{
    h->init_game = on_init_game;
    h->player_join = on_join;
    h->player_quit = on_quit;
    h->set_player_name = on_set_name;
    h->set_color = on_set_color;
    h->show_nametag = on_nametag;
    h->score_and_ping = on_score;
    h->game_text = on_game_text;
    h->chat_bubble = on_bubble;
    h->sync_marker = on_marker;
}

void fhud_reset(void)
{
    unsigned i;
    for (i = 0; i < top_player; i++) {
        drop_blip(&players[i]);
        players[i].used = 0;
    }
    top_player = 0;
    for (i = 0; i < GT_STYLES; i++)
        game_texts[i].until = 0;
    for (i = 0; i < MAX_BUBBLES; i++)
        bubbles[i].used = 0;
    tag_distance = TAG_DISTANCE;
    tags_on = 1;
    marker_mode = MARKERS_STREAMED;
    score_open = 0;
    init_seen = 0;
}

/* L1 is Tab in the PC-style layout, the same key as the PC client. It stays with the
 * game too, so this never takes the pad. */
static void score_input(void)
{
    score_open = fd_cur.l1 && !menu_is_open() && !fd_paused(); /* L1 changes tabs in the browser */
    if (!score_open)
        return;
    if (!score_requested || fd_now - score_requested > SCORE_REQUEST_MS) {
        score_requested = fd_now | 1;
        rak_rpc(SRPC_C2S_REQUEST_SCORES, 0, 0, RAK_RELIABLE, 0);
    }
    if (fd_repeat(&score_held[0], fd_pad_up()) && score_top > 0)
        score_top--;
    if (fd_repeat(&score_held[1], fd_pad_down()))
        score_top++;
}

void fhud_tick(void)
{
    unsigned i;

    if (game_remote_pos && top_player)
        fd_camera_update();
    score_input();
    if (fd_now - marker_at >= MARKER_MS) {
        marker_at = fd_now;
        update_markers();
    }
    for (i = 0; i < MAX_BUBBLES; i++)
        if (bubbles[i].used && (int)(fd_now - bubbles[i].until) >= 0)
            bubbles[i].used = 0;
    for (i = 0; i < GT_STYLES; i++)
        if (game_texts[i].until && (int)(fd_now - game_texts[i].until) >= 0)
            game_texts[i].until = 0;
}

/* ---- drawing ------------------------------------------------------------ */

/* Approximates the game's big-message slots. The real positions are in CHud, not RE'd. */
static const struct {
    int font;
    float sx, sy, x, y;
    int align;
    unsigned color;
} gt_style[GT_STYLES] = {
    {FD_FONT_PRICEDOWN, 1.30f, 2.60f, 320.0f, 165.0f, FD_CENTRE, FD_RGB(226, 192, 99)},
    {FD_FONT_PRICEDOWN, 0.90f, 1.90f, 600.0f, 320.0f, FD_RIGHT,  FD_RGB(226, 192, 99)},
    {FD_FONT_PRICEDOWN, 1.40f, 2.80f, 320.0f, 165.0f, FD_CENTRE, FD_RGB(225, 225, 225)},
    {FD_FONT_PRICEDOWN, 0.80f, 1.70f, 320.0f, 110.0f, FD_CENTRE, FD_RGB(226, 192, 99)},
    {FD_FONT_PRICEDOWN, 0.70f, 1.50f, 320.0f, 140.0f, FD_CENTRE, FD_RGB(226, 192, 99)},
    {FD_FONT_SUBTITLES, 0.55f, 1.10f, 320.0f, 260.0f, FD_CENTRE, FD_RGB(255, 255, 255)},
    {FD_FONT_PRICEDOWN, 0.70f, 1.50f, 320.0f,  40.0f, FD_CENTRE, FD_RGB(225, 225, 225)},
};

static void draw_game_text(void)
{
    int i;

    for (i = 0; i < GT_STYLES; i++) {
        fd_layout_t *l = fd_scratch();
        if (!game_texts[i].until)
            continue;
        fd_font(gt_style[i].font, gt_style[i].sx, gt_style[i].sy * 0.5f, 1, 2, 0, FD_RGB(0, 0, 0));
        fd_layout(l, game_texts[i].text, FD_TOKENS, gt_style[i].color, 600.0f, 0);
        fd_print(l, gt_style[i].x, gt_style[i].y, 18.0f * gt_style[i].sy * 0.5f, gt_style[i].align, 0, l->lines);
        fd_flush();
    }
}

/* Health and armour under the name, as the PC client draws them: armour (only when the
 * player has some) above health, each a dark track with a bright fill and a black frame. */
#define BAR_W   40.0f
#define BAR_H   5.0f
#define BAR_GAP 3.0f
int feat_combat_remote_health(unsigned id, unsigned *health, unsigned *armour) __attribute__((weak));

static void draw_bar(float x, float y, unsigned value, unsigned track, unsigned fill)
{
    float l = x - BAR_W * 0.5f, r = x + BAR_W * 0.5f;
    if (value > 100)
        value = 100;
    fd_rect(l - 1.0f, y - 1.0f, r + 1.0f, y + BAR_H + 1.0f, FD_RGB(0, 0, 0));
    fd_rect(l, y, r, y + BAR_H, track);
    if (value)
        fd_rect(l, y, l + BAR_W * (float)value / 100.0f, y + BAR_H, fill);
}

static void draw_bars(unsigned id, float x, float y)
{
    unsigned health, armour;
    if (!feat_combat_remote_health || !feat_combat_remote_health(id, &health, &armour))
        return;
    if (armour) {
        draw_bar(x, y, armour, FD_RGB(40, 40, 40), FD_RGB(200, 200, 200));
        y += BAR_H + BAR_GAP;
    }
    draw_bar(x, y, health, FD_RGB(75, 11, 20), FD_RGB(185, 34, 40));
}

static void draw_tag(unsigned id, const player_t *p, float x, float y)
{
    char line[SRPC_NAME_MAX + 12], *e;
    fd_layout_t *l = fd_scratch();
    unsigned c = FD_FROM_RGBA(player_color(p, id)) | 0xFF000000u;

    e = fd_put_str(line, p->name, SRPC_NAME_MAX);
    e = fd_put_str(e, " (", 2);
    e = fd_put_uint(e, id);
    fd_put_str(e, ")", 1);
    fd_layout(l, line, FD_TAGS, c, FD_NOWRAP, 0);
    fd_print(l, x, y, 12.0f, FD_CENTRE, 0, 1);
    draw_bars(id, x, y + 15.0f);
}

static void draw_tags(void)
{
    unsigned id;
    int i;

    if (!game_remote_pos)
        return;
    fd_font(FD_FONT_SUBTITLES, 0.32f, 0.62f, 1, 1, 0, FD_RGB(0, 0, 0));
    if (tags_on) {
        for (id = 0; id < top_player; id++) {
            const player_t *p = &players[id];
            float pos[3], x, y, dist;
            if (!p->used || p->hidden_tag || !game_remote_pos(id, pos))
                continue;
            pos[2] += HEAD_Z;
            if (fd_project(pos, &x, &y, &dist) && dist <= tag_distance)
                draw_tag(id, p, x, y);
        }
    }
    for (i = 0; i < MAX_BUBBLES; i++) {
        const bubble_t *b = &bubbles[i];
        float pos[3], x, y, dist;
        fd_layout_t *l = fd_scratch();
        if (!b->used || !game_remote_pos(b->id, pos))
            continue;
        pos[2] += BUBBLE_Z;
        if (!fd_project(pos, &x, &y, &dist) || dist > b->distance)
            continue;
        fd_layout(l, b->text, FD_TAGS, b->color, 200.0f, 0);
        fd_print(l, x, y - l->lines * 12.0f, 12.0f, FD_CENTRE, 0, l->lines);
    }
    fd_flush();
}

static void draw_scoreboard(void)
{
    static const float col_id = 60.0f, col_name = 100.0f, col_score = 420.0f, col_ping = 500.0f;
    unsigned id, shown = 0, count = 0;
    float y = 62.0f;
    char line[16];
    fd_layout_t *l;

    for (id = 0; id < top_player; id++)
        if (players[id].used)
            count++;
    if ((unsigned)score_top + SCORE_ROWS > count)
        score_top = count > SCORE_ROWS ? (int)(count - SCORE_ROWS) : 0;

    fd_flush();
    fd_rect(50.0f, 26.0f, 590.0f, 420.0f, FD_RGBA(0, 0, 0, 200));
    fd_rect(50.0f, 26.0f, 590.0f, 50.0f, FD_RGBA(60, 90, 140, 235));
    fd_font(FD_FONT_SUBTITLES, 0.40f, 0.76f, 1, 0, 1, FD_RGB(0, 0, 0));
    l = fd_scratch();
    fd_layout(l, samp_stats.server_name, FD_TAGS, FD_RGB(255, 255, 255), 400.0f, 1);
    fd_print(l, col_id, 31.0f, 15.0f, FD_LEFT, 0, 1);
    fd_put_str(fd_put_uint(line, count), " players", 8);
    l = fd_scratch();
    fd_layout(l, line, FD_TAGS, FD_RGB(255, 255, 255), FD_NOWRAP, 0);
    fd_print(l, 580.0f, 31.0f, 15.0f, FD_RIGHT, 0, 1);

    for (id = 0; id < top_player && shown < (unsigned)score_top + SCORE_ROWS; id++) {
        const player_t *p = &players[id];
        unsigned c;
        if (!p->used)
            continue;
        if (shown++ < (unsigned)score_top)
            continue;
        c = FD_FROM_RGBA(player_color(p, id)) | 0xFF000000u;
        fd_put_uint(line, id);
        l = fd_scratch();
        fd_layout(l, line, FD_TAGS, c, FD_NOWRAP, 0);
        fd_print(l, col_id, y, 15.0f, FD_LEFT, 0, 1);
        l = fd_scratch();
        fd_layout(l, p->name[0] ? p->name : id == samp_stats.player_id ? "You" : "?", FD_TAGS, c, 310.0f, 1);
        fd_print(l, col_name, y, 15.0f, FD_LEFT, 0, 1);
        if (p->score < 0) {
            line[0] = '-';
            fd_put_uint(line + 1, 0u - (unsigned)p->score);
        } else {
            fd_put_uint(line, p->score);
        }
        l = fd_scratch();
        fd_layout(l, line, FD_TAGS, c, FD_NOWRAP, 0);
        fd_print(l, col_score, y, 15.0f, FD_LEFT, 0, 1);
        fd_put_uint(line, (unsigned)p->ping);
        l = fd_scratch();
        fd_layout(l, line, FD_TAGS, c, FD_NOWRAP, 0);
        fd_print(l, col_ping, y, 15.0f, FD_LEFT, 0, 1);
        y += 16.0f;
        fd_flush();
    }
    fd_flush();
}

void fhud_draw(void)
{
    draw_tags();
    draw_game_text();
    if (score_open)
        draw_scoreboard();
}
