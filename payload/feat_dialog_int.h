#pragma once
/* Shared between the feat_dialog modules. See feat_dialog.c for the map. */
#include "feat.h"
#include "menu.h"

#define FD_PRESSED(f) (fd_cur.f && !fd_prev.f)
#define FD_STICK 64

extern menu_pad_t fd_cur, fd_prev;
extern unsigned fd_now;

/* The shared text box. Weak so this builds and links before it exists: see the report.
 * done runs exactly once, from menu_tick; text is only valid during the call. */
void menu_text_open(const char *prompt, int max_len, int masked,
                    void (*done)(int accepted, const char *text)) __attribute__((weak));
/* Latest position of a streamed-in remote player, and its script handle. */
int  game_remote_pos(unsigned id, float *pos) __attribute__((weak));
int  game_remote_handle(unsigned id) __attribute__((weak));

/* Press, then auto-repeat while held. */
int  fd_repeat(int *held, int down);
int  fd_pad_up(void);
int  fd_pad_down(void);
int  fd_pad_left(void);
int  fd_pad_right(void);
/* The server was told something that may unblock a refused spawn. */
void fd_spawn_retry_arm(void);
/* Forgets everything a server put on screen. */
void fd_reset_all(void);

/* feat_dialog_box.c: dialogs */
void fdlg_show(const srpc_dialog_t *d);
void fdlg_input(void);
void fdlg_draw(void);
int  fdlg_open(void);
void fdlg_reset(void);

/* feat_dialog_td.c: textdraws */
void ftd_show(const srpc_textdraw_t *t);
void ftd_hide(const srpc_textdraw_id_t *t);
void ftd_set_text(const srpc_textdraw_text_t *t);
void ftd_select(const srpc_textdraw_select_t *s);
void ftd_input(void);
void ftd_draw(void);
int  ftd_selecting(void);
void ftd_reset(void);

/* feat_dialog_menu.c: server menus */
void fmn_init(const srpc_menu_t *m);
void fmn_show(const srpc_id_t *id);
void fmn_hide(const srpc_id_t *id);
void fmn_input(void);
void fmn_draw(void);
int  fmn_open(void);
void fmn_reset(void);

/* feat_dialog_hud.c: game text, players, name tags, bubbles, markers, scoreboard */
void fhud_init(samp_rpc_handlers_t *h);
void fhud_tick(void);
void fhud_draw(void);
void fhud_reset(void);
