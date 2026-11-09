/* Everything on screen that a server drives. Owned by one work stream: see feat.h.
 *
 *   feat_dialog.c       entry points, pad routing, the session lifecycle
 *   feat_dialog_box.c   dialogs
 *   feat_dialog_td.c    textdraws and selecting them
 *   feat_dialog_menu.c  server menus
 *   feat_dialog_hud.c   game text, player table, name tags, bubbles, markers, scoreboard
 *   feat_dialog_text.c  layout of server text; nothing else may hand it to the game
 *   feat_dialog_gfx.c   the only file that touches the game
 *
 * One thing owns the pad at a time: dialog, then menu, then textdraw selection. The pad
 * state arrives through feat_dialog_pad and feat_dialog_modal tells the pad hook to
 * hide it from the game. */
#include "feat_dialog_int.h"
#include "fps.h"
#include "feat_dialog_pad.h"
#include "feat_dialog_gfx.h"
#include "samp.h"
#include "rak.h"

#define REPEAT_FIRST 12
#define REPEAT_NEXT  3

#define SPAWN_FIRST_MS 700  /* lets the script finish with the response first */
#define SPAWN_NEXT_MS  4000
#define SPAWN_TRIES    4

menu_pad_t fd_cur, fd_prev;
unsigned fd_now;

static unsigned spawn_at;
static int spawn_tries, live, menu_was_open;

int fd_repeat(int *held, int down)
{
    if (!down) {
        *held = 0;
        return 0;
    }
    ++*held;
    if (*held == 1)
        return 1;
    return *held > REPEAT_FIRST * fps_mult() &&
           (*held - REPEAT_FIRST * fps_mult()) % (REPEAT_NEXT * fps_mult()) == 0;
}

int fd_pad_up(void) { return fd_cur.up || fd_cur.ly < -FD_STICK; }
int fd_pad_down(void) { return fd_cur.down || fd_cur.ly > FD_STICK; }
int fd_pad_left(void) { return fd_cur.left || fd_cur.lx < -FD_STICK; }
int fd_pad_right(void) { return fd_cur.right || fd_cur.lx > FD_STICK; }

/* samp.c asks to spawn once, straight after class selection. A login server refuses
 * that, and on a PC the player then presses Spawn after logging in. We have no such
 * button, so anything that may have logged us in asks again, a few times at most. */
void fd_spawn_retry_arm(void)
{
    spawn_tries = SPAWN_TRIES;
    spawn_at = fd_now + SPAWN_FIRST_MS;
}

static void spawn_retry(void)
{
    if (!spawn_tries || samp_state() != SAMP_SPAWNING) {
        if (samp_state() != SAMP_SPAWNING)
            spawn_tries = 0;
        return;
    }
    if (fdlg_open() || (int)(fd_now - spawn_at) < 0)
        return;
    rak_rpc(SRPC_C2S_REQUEST_SPAWN, 0, 0, RAK_RELIABLE, 0);
    spawn_tries--;
    spawn_at = fd_now + SPAWN_NEXT_MS;
}

void fd_reset_all(void)
{
    fdlg_reset();
    ftd_reset();
    fmn_reset();
    fhud_reset();
    spawn_tries = 0;
}

static void on_restart(void) { fd_reset_all(); }

void feat_dialog_init(samp_rpc_handlers_t *h)
{
    h->show_dialog = fdlg_show;
    h->show_textdraw = ftd_show;
    h->hide_textdraw = ftd_hide;
    h->set_textdraw_text = ftd_set_text;
    h->select_textdraw = ftd_select;
    h->init_menu = fmn_init;
    h->show_menu = fmn_show;
    h->hide_menu = fmn_hide;
    h->game_mode_restart = on_restart;
    fhud_init(h);
}

/* From the pad hook, each frame, with what the player is really pressing. */
void feat_dialog_pad(const menu_pad_t *pad) { fd_cur = *pad; }

int feat_dialog_modal(void)
{
    return menu_is_connected() && !fd_paused() && (fdlg_open() || fmn_open() || ftd_selecting());
}

void feat_dialog_tick(unsigned now)
{
    fd_now = now;
    /* Nothing from the last session may reach this one. */
    if (samp_state() == SAMP_CONNECTING) {
        if (live)
            fd_reset_all();
        live = 0;
    } else {
        live = 1;
    }
    /* The tick after a box or the browser closes is skipped too: the menu ran first, so the
     * press that closed it would count again here. */
    if (!menu_is_open() && !menu_was_open && !fd_paused()) { /* they own the pad while up */
        if (fdlg_open())
            fdlg_input();
        else if (fmn_open())
            fmn_input();
        else
            ftd_input();
    }
    menu_was_open = menu_is_open();
    fhud_tick();
    spawn_retry();
    fd_prev = fd_cur; /* each press counts once */
}

void feat_dialog_draw(void)
{
    if (!menu_is_connected()) {
        if (live)
            fd_reset_all();
        live = 0;
        return;
    }
    if (fd_paused())
        return;
    ftd_draw();
    fhud_draw();
    fmn_draw();
    fdlg_draw();
}
