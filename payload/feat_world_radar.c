/* Map icons and gang zones. Player markers are in feat_dialog_hud.c.
 *
 * Gang zones go through the game's own CRadar::DrawAreaOnRadar, which clips to the
 * radar and rotates with it. It runs from the draw hook, so a zone lands on top of the
 * radar's blips, and the pause map does not show zones. */
#include "feat_world_int.h"
#include "util.h"

#define ICONS       100
#define SPRITE_LAST 63
#define ICON_PLAIN_SIZE 2
#define ICON_NAMED_COLOUR 1004 /* below this the colour is one of the game's named ones */

#define ZONES       1024
#define FLASH_MS    500

typedef struct { int used; float rect[4]; unsigned char color[4], flash[4]; } zone_t;

static int icons[ICONS];
static zone_t zones[ZONES];
static unsigned zone_top, flash_at;
static int flash_on;

static void on_icon(const srpc_map_icon_t *m)
{
    int b;

    if (m->icon >= ICONS || m->type > SPRITE_LAST || !world_pos_ok(m->pos))
        return;
    if (icons[m->icon]) {
        wg_blip_delete(icons[m->icon]);
        icons[m->icon] = 0;
    }
    b = wg_blip_sprite(m->style > 3 ? 0 : m->style, m->pos, m->type);
    if (b == -1)
        return;
    icons[m->icon] = b;
    if (m->type == 0) {
        wg_blip_colour(b, m->color);
        wg_blip_scale(b, m->color < ICON_NAMED_COLOUR ? ICON_PLAIN_SIZE : ICON_PLAIN_SIZE + 1);
    }
}

static void on_icon_off(const srpc_id_t *v)
{
    if (v->id < ICONS && icons[v->id]) {
        wg_blip_delete(icons[v->id]);
        icons[v->id] = 0;
    }
}

/* Wire colours are ABGR, which is CRGBA's byte order in memory. */
static void set_color(unsigned char *c, unsigned v)
{
    c[0] = v;
    c[1] = v >> 8;
    c[2] = v >> 16;
    c[3] = v >> 24;
}

static void on_zone(const srpc_gangzone_t *z)
{
    zone_t *n;

    if (z->id < 0 || z->id >= ZONES || !world_float_ok(z->min_x) || !world_float_ok(z->min_y) ||
        !world_float_ok(z->max_x) || !world_float_ok(z->max_y) ||
        z->min_x > z->max_x || z->min_y > z->max_y)
        return;
    n = &zones[z->id];
    n->used = 1;
    n->rect[0] = z->min_x;
    n->rect[1] = z->max_y;
    n->rect[2] = z->max_x;
    n->rect[3] = z->min_y;
    set_color(n->color, z->color);
    set_color(n->flash, z->color);
    if ((unsigned)z->id >= zone_top)
        zone_top = z->id + 1;
}

static void on_zone_off(const srpc_int_t *v)
{
    if (v->v >= 0 && v->v < ZONES)
        zones[v->v].used = 0;
}

static void on_zone_flash(const srpc_gangzone_flash_t *f)
{
    if (f->id >= 0 && f->id < ZONES)
        set_color(zones[f->id].flash, f->color);
}

static void on_zone_stop(const srpc_int_t *v)
{
    if (v->v >= 0 && v->v < ZONES)
        memcpy(zones[v->v].flash, zones[v->v].color, 4);
}

void wradar_tick(unsigned now)
{
    if (now - flash_at >= FLASH_MS) {
        flash_at = now;
        flash_on = !flash_on;
    }
}

void wradar_draw(void)
{
    unsigned i;

    if (wg_menu_open())
        return;
    for (i = 0; i < zone_top; i++)
        if (zones[i].used)
            wg_radar_area(zones[i].rect, flash_on ? zones[i].flash : zones[i].color);
}

void wradar_reset(void)
{
    unsigned i;

    for (i = 0; i < ICONS; i++)
        if (icons[i]) {
            wg_blip_delete(icons[i]);
            icons[i] = 0;
        }
    memset(zones, 0, sizeof zones);
    zone_top = 0;
}

void wradar_init(samp_rpc_handlers_t *h)
{
    h->set_map_icon = on_icon;
    h->remove_map_icon = on_icon_off;
    h->show_gangzone = on_zone;
    h->hide_gangzone = on_zone_off;
    h->flash_gangzone = on_zone_flash;
    h->stop_flash_gangzone = on_zone_stop;
}
