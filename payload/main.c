#include "status.h"
#define FPS_OWNER
#include "fps.h"
#include "net.h"
#include "util.h"
#include "samp.h"
#include "game.h"
#include "scm.h"
#include "ui.h"
#include "menu.h"
#include "feat.h"
#include "feat_vehicle.h"
#include "feat_dialog_pad.h"
#include "browser.h"
#include "browser_ee.h"
#include "ui_game.h"
#include "feat_combat.h"

#define LOCAL_PORT  50000
#define COUNT_PER_MS 294912u /* EE core clock */

static int ready, started, teleported, release_wait;
static unsigned server_ip, server_port;
static unsigned last_count, acc, ms;
short samp_raw_l1;

static unsigned read_count(void)
{
    unsigned c;
    __asm__ volatile("mfc0 %0, $9\n\tsync.p" : "=r"(c));
    return c;
}

static unsigned now_ms(void)
{
    unsigned c = read_count();
    acc += c - last_count;
    last_count = c;
    ms += acc / COUNT_PER_MS;
    acc %= COUNT_PER_MS;
    return ms;
}

void samp_plat_send(const unsigned char *d, unsigned n)
{
    net_udp_send(server_ip, server_port, LOCAL_PORT, d, n);
}

void samp_plat_local(samp_local_t *l) { game_read_local(l); }
int  samp_plat_in_vehicle(void) { return feat_vehicle_in_car(); }
void samp_plat_player_add(unsigned id, unsigned skin, const float *p, float a) { game_remote_add(id, skin, p, a); }
void samp_plat_player_del(unsigned id) { game_remote_del(id); }
void samp_plat_player_sync(unsigned id, const float *p, const float *q, unsigned keys, int moving)
{
    feat_vehicle_foot_sync(id);
    game_remote_sync(id, p, q, moving);
}
void samp_plat_set_pos(const float *p)
{
    feat_vehicle_eject(p);
    game_teleport(p[0], p[1], p[2]);
}
void samp_plat_set_skin(unsigned skin) { game_set_skin(feat_combat_safe_skin(skin)); }
void samp_plat_message(const char *t) { ui_chat_add(t); }

/* A failed or lost connection must never leave the player without a way back. */
void samp_plat_closed(const char *why)
{
    char msg[64];
    unsigned n = 0;
    const char *pre = "Could not stay connected: ";
    while (*pre)
        msg[n++] = *pre++;
    while (*why && n < sizeof msg - 1)
        msg[n++] = *why++;
    msg[n] = 0;
    started = 0;
    feat_vehicle_reset();
    feat_world_reset();
    game_remote_clear();
    ui_status("");
    menu_set_status(msg);
    menu_open();
}

static void on_udp(unsigned ip, unsigned sport, unsigned dport,
                   const unsigned char *d, unsigned len)
{
    if (dport == LOCAL_PORT && ip == server_ip && sport == server_port)
        samp_datagram(d, len, ms);
    else if (dport == BROWSER_LOCAL_PORT)
        browser_on_udp(ip, sport, d, len, ms);
}

void menu_do_connect(unsigned ip, unsigned port, const char *name, const char *password)
{
    server_ip = ip;
    server_port = port;
    started = 1;
    teleported = 0;
    feat_vehicle_reset();
    feat_world_reset();
    game_remote_clear();
    feat_combat_connecting();
    slog("connecting as %s", name);
    ui_status("Connecting...");
    menu_set_status("");
    samp_connect(ip, port, name, password);
}

void menu_do_disconnect(void)
{
    samp_disconnect();
    ui_status("");
    feat_vehicle_reset();
    feat_world_reset();
    game_remote_clear();
    started = 0;
}

void menu_do_chat(const char *text) { samp_chat(text); }
int  menu_is_connected(void) { return started && samp_state() != SAMP_ERROR && samp_state() != SAMP_OFFLINE; }

void samp_tick_frame(unsigned now)
{
    net_poll();
    samp_tick(now);
}


void samp_tick_main(void)
{
    unsigned now;

    ST->magic = STATUS_MAGIC;
    ST->frames++;
    /* Every frame: the game sets its own value at boot and when a savestate loads. */
    *(volatile unsigned char *)FPS_LIMIT_BYTE = fps_high ? 1 : 2;
    now = now_ms();
    {   /* a stalled frame is what makes a server drop us: say so */
        static unsigned last_frame;
        if (ready && now - last_frame > 1000)
            slog("main: frame took %d ms", now - last_frame);
        last_frame = now;
    }

    if (!ready) {
        ST->logpos = 0;
        net_init(on_udp);
        game_heap_extend();
        ready = 1;
        ST->stage = 1;
        slog("payload up");
        game_boot();
        browser_ee_init();
        feat_init();
        menu_init();
        menu_open();
    }
    if (samp_state() == SAMP_SPAWNED && !teleported && game_has_player()) {
        teleported = 1;
        game_set_skin(feat_combat_safe_skin(samp_stats.skin));
        feat_combat_skin_set();
        game_teleport(samp_stats.spawn[0], samp_stats.spawn[1], samp_stats.spawn[2]);
        slog("game: spawned, skin %d", samp_stats.skin);
    }
    /* The server has answered, so we are past connecting whatever happens next: a login
     * dialog or a refused spawn can hold us here for as long as it likes. */
    if (samp_state() >= SAMP_CLASS || samp_state() == SAMP_OFFLINE)
        ui_status("");
    if (ST->cmd_len) { /* development: run SCM pushed from the host over PINE */
        scm_t c;
        scm_begin(&c);
        scm_raw(&c, ST->cmd, ST->cmd_len);
        scm_run(&c);
        ST->cmd_locals[0] = scm_get_local(2);
        ST->cmd_locals[1] = scm_get_local(3);
        ST->cmd_len = 0;
    }
    if (ST->dbg_fn) {
        ST->dbg_ret = ((unsigned (*)(unsigned, unsigned, unsigned, unsigned))ST->dbg_fn)(
            ST->dbg_arg[0], ST->dbg_arg[1], ST->dbg_arg[2], ST->dbg_arg[3]);
        ST->dbg_fn = 0;
    }
    game_stream_budget();
    ST->where = 1;
    menu_tick(now);
    ST->where = 2;
    game_skin_tick();
    if (started) {
        ST->where = 3;
        samp_tick_frame(now);
        ST->where = 4;
        game_remote_update();
        ST->where = 5;
        feat_tick(now);
    }
    else
        net_poll();
    ST->where = 0;

    ST->stage = samp_state();
    ST->v[0] = samp_stats.rpcs;
    ST->v[1] = samp_stats.sync_in;
    ST->v[2] = samp_stats.sync_out;
    ST->v[3] = samp_stats.player_id;
    ST->v[5] = net_rx_frames;
    ST->v[6] = net_tx_frames;
    ST->v[7] = net_rx_dropped;
    ST->v[8] = now;
    {
        samp_local_t l;
        game_read_local(&l);
        ST->v[9] = *(unsigned *)&l.pos[0];
        ST->v[10] = *(unsigned *)&l.pos[1];
        ST->v[11] = *(unsigned *)&l.pos[2];
    }
}

void samp_draw_main(void)
{
    if (ready) {
        ST->where = 0x20;
        ui_draw();
        ST->where = 0;
    }
}

/* Runs right after the game has read the pads. While the menu is up the game sees an
 * idle pad, and after it closes we keep that up until every button is released so a
 * held button can't register as a fresh press. */
void samp_pad_main(void)
{
    menu_pad_t p;
    unsigned char *pad = (unsigned char *)CPAD_ARRAY;
    int any, i;

    if (!ready)
        return;
    if (ST->dbg_pad_mask) /* development: buttons and sticks pushed from the host over PINE */
        for (i = 0; i < 20; i++)
            if (ST->dbg_pad_mask & (1u << i))
                ((short *)pad)[i] = ST->dbg_pad[i];
    memcpy(&p, pad + CPAD_NEWSTATE, sizeof p);
    /* Select is chat here. The game would also change the car camera, or show stats. */
    ((short *)pad)[13] = 0;
    /* On foot L1 is the scoreboard, and the game would put its stats box next to it. The
     * key still counts for sync, which reads it from here. */
    samp_raw_l1 = ((short *)pad)[4];
    if (menu_is_connected() && !game_in_vehicle())
        ((short *)pad)[4] = 0;
    menu_pad(&p);
    feat_dialog_pad(&p);
    any = 0;
    for (i = 4; i < 20; i++)
        if (((short *)&p)[i])
            any = 1;
    if (menu_is_open() || feat_dialog_modal()) {
        release_wait = 1;
    } else if (release_wait && !any) {
        release_wait = 0;
        pad[CPAD_JUSTOUTOFFRONTEND] = 5 * fps_mult();
    }
    if (release_wait) {
        memset(pad, 0, 0x50);
        return;
    }
    if (game_in_vehicle()) {
        /* PC-style driving: W/S accelerate and brake, Space is the handbrake.
         * The PS2 uses Cross, Square and R1 for those. */
        short *n = (short *)pad;
        int fwd = p.ly < 0, back = p.ly > 0, hand = p.cross;
        n[1] = 0;                 /* ly */
        n[16] = fwd ? 255 : 0;    /* cross */
        n[14] = back ? 255 : 0;   /* square */
        if (hand)
            n[6] = 255;           /* r1 */
    }
}
