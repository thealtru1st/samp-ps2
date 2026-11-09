#include <math.h>
#include <stdio.h>
#include <string.h>
#include "fake_game.h"
#include "feat_world_models.h"

fg_t fg;

typedef struct { int live, model; float pos[3], rot[3]; } fobj_t;
typedef struct { int live, type, model; float pos[3]; } fpick_t;
typedef struct { int model, hidden; float pos[3]; } fent_t;

#define MODELS 15080
static fobj_t objs[FG_OBJECTS];
static fpick_t picks[FG_PICKUPS];
static int peds[FG_PEDS];
static int ped_seen_hit[FG_PEDS];
static float ped_health[FG_PEDS];
static unsigned char state[MODELS];   /* 0 none, 2 requested, 1 loaded */
static int ring[20];
static fent_t ents[20000];
static int ent_count, blips_next = 1;
static int blips[256];

#define HANDLE(i) (((i) + 1) << 8 | 5)
#define INDEX(h)  (((h) >> 8) - 1)

void fg_reset(void)
{
    memset(&fg, 0, sizeof fg);
    memset(objs, 0, sizeof objs);
    memset(picks, 0, sizeof picks);
    memset(peds, 0, sizeof peds);
    memset(state, 0, sizeof state);
    memset(ring, 0, sizeof ring);
    memset(blips, 0, sizeof blips);
    ent_count = 0;
    fg.stream_room = 1;
    fg.gravity = 0.008f;
    fg.line_clear = 1;
    fg.load_delay = 0;
}

void fg_frame(void)
{
    int i;
    for (i = 0; i < MODELS; i++)
        if (state[i] == 2)
            state[i] = 1;
}

float wg_sqrt(float v) { return sqrtf(v); }

int wg_model_map(int pc)
{
    int m = fg.map_identity ? pc : world_model_lookup(pc);
    return m >= 0 && m < MODELS ? m : -1;
}

int wg_ped_model_ok(int m) { return m > 0 && m < 300; }

static int bad_model(int m)
{
    if (m >= 0 && m < MODELS)
        return 0;
    fg.bad_model++;
    return 1;
}

int wg_model_loaded(int m) { return !bad_model(m) && state[m] == 1; }
void fg_unload(int m) { if (!bad_model(m)) state[m] = 0; }  /* the streamer took it */

void wg_model_request(int m)
{
    if (bad_model(m))
        return;
    fg.request_calls++;
    fg.requested_now++;
    if (!state[m])
        state[m] = 2;
}

void wg_model_release(int m)
{
    if (bad_model(m))
        return;
    fg.release_calls++;
    fg.requested_now--;
}

int wg_stream_room(void) { return fg.stream_room; }

int wg_object_slots(void)
{
    int i, n = 0;
    for (i = 0; i < FG_OBJECTS; i++)
        n += !objs[i].live;
    return n - fg.world_objects;
}

int wg_ped_slots(void) { return FG_PEDS - fg.peds_live - fg.ped_slots_used; }
int wg_local_char(void) { return 1 << 8 | 7; }

int wg_local_pos(float *p)
{
    memcpy(p, fg.me, sizeof fg.me);
    return 1;
}

int wg_camera_pos(float *p) { return wg_local_pos(p); }
int wg_menu_open(void) { return fg.menu_open; }

int wg_object_alive(int h)
{
    int i = INDEX(h);
    return h > 0 && i >= 0 && i < FG_OBJECTS && objs[i].live;
}

int wg_car_alive(int h) { return h > 0; }
int wg_car_pos(int h, float *p) { memset(p, 0, 12); return h > 0; }

int wg_char_alive(int h)
{
    int i;
    if (h == wg_local_char())
        return 1;
    i = INDEX(h);
    return h > 0 && i >= 0 && i < FG_PEDS && peds[i];
}

static fobj_t *object(int h)
{
    if (wg_object_alive(h))
        return &objs[INDEX(h)];
    fg.bad_handle++;
    return 0;
}

int wg_object_create(int model, const float *p, const float *r)
{
    int i;

    if (bad_model(model))
        return 0;
    if (state[model] != 1)
        fg.bad_model++;
    if (wg_object_slots() <= 0) {
        fg.pool_overrun++;
        return 0;
    }
    for (i = 0; i < FG_OBJECTS; i++)
        if (!objs[i].live) {
            objs[i].live = 1;
            objs[i].model = model;
            memcpy(objs[i].pos, p, 12);
            memcpy(objs[i].rot, r, 12);
            fg.objects_live++;
            fg.create_calls++;
            if (fg.objects_live > fg.objects_peak)
                fg.objects_peak = fg.objects_live;
            return HANDLE(i);
        }
    return 0;
}

void wg_object_delete(int h)
{
    fobj_t *o = object(h);
    if (o) {
        o->live = 0;
        fg.objects_live--;
    }
}

void wg_object_move(int h, const float *p)
{
    fobj_t *o = object(h);
    if (o) {
        memcpy(o->pos, p, 12);
        fg.moves++;
    }
}

void wg_object_rotate(int h, const float *r)
{
    fobj_t *o = object(h);
    if (o) {
        memcpy(o->rot, r, 12);
        fg.rotates++;
    }
}

void wg_object_attach_car(int h, int to, const float *o, const float *r)
{
    if (object(h) && wg_car_alive(to))
        fg.attach_calls++;
}

void wg_object_attach_object(int h, int to, const float *o, const float *r)
{
    if (object(h) && object(to))
        fg.attach_calls++;
}

void wg_object_attach_char(int h, int to, const float *o, const float *r)
{
    if (object(h) && wg_char_alive(to))
        fg.attach_calls++;
}

int fg_object_at(const float *p)
{
    int i;
    for (i = 0; i < FG_OBJECTS; i++)
        if (objs[i].live && !memcmp(objs[i].pos, p, 12))
            return HANDLE(i);
    return 0;
}

int fg_object_model(int h) { return objs[INDEX(h)].model; }
void fg_object_pos(int h, float *p) { memcpy(p, objs[INDEX(h)].pos, 12); }

int wg_pickup_create(int model, int type, const float *p)
{
    int i;
    if (state[model] != 1)
        fg.bad_model++;
    for (i = 0; i < FG_PICKUPS; i++)
        if (!picks[i].live) {
            picks[i].live = 1;
            picks[i].model = model;
            picks[i].type = type;
            memcpy(picks[i].pos, p, 12);
            fg.pickups_live++;
            if (fg.pickups_live > fg.pickups_peak)
                fg.pickups_peak = fg.pickups_live;
            return (1 << 16) | i;
        }
    return -1;
}

void wg_pickup_delete(int h)
{
    int i = h & 0xFFFF;
    if (i >= FG_PICKUPS || !picks[i].live) {
        fg.bad_handle++;
        return;
    }
    picks[i].live = 0;
    fg.pickups_live--;
}

int fg_pickup_handle_at(const float *p)
{
    int i;
    for (i = 0; i < FG_PICKUPS; i++)
        if (picks[i].live && !memcmp(picks[i].pos, p, 12))
            return (1 << 16) | i;
    return 0;
}

void fg_collect(int h)
{
    int i;
    for (i = 0; i < 20; i++)
        if (!ring[i]) {
            ring[i] = h;
            return;
        }
}

int wg_pickup_collected(void)
{
    int i;
    for (i = 0; i < 20; i++)
        if (ring[i]) {
            int h = ring[i];
            ring[i] = 0;
            return h;
        }
    return 0;
}

static int new_blip(void)
{
    int i;
    for (i = 0; i < 175; i++)
        if (!blips[i]) {
            blips[i] = blips_next++;
            fg.blips_live++;
            return (blips[i] << 16) | i;
        }
    return -1;
}

int wg_blip_coord(const float *p) { return new_blip(); }
int wg_blip_sprite(int style, const float *p, int sprite)
{
    if (style < 0 || style > 3 || sprite < 0 || sprite > 63)
        fg.bad_model++;
    return new_blip();
}

void wg_blip_colour(int b, unsigned c) {}
void wg_blip_scale(int b, int s) {}

void wg_blip_delete(int b)
{
    int i = b & 0xFFFF;
    if (i >= 175 || blips[i] != b >> 16) {
        fg.bad_handle++;
        return;
    }
    blips[i] = 0;
    fg.blips_live--;
}

void wg_radar_area(const float *r, const unsigned char *c)
{
    fg.area_calls++;
    memcpy(fg.last_rect, r, 16);
    memcpy(fg.last_rgba, c, 4);
}

void wg_draw_sphere(const float *p, float s) { fg.spheres++; }

int wg_race_cp_create(int t, const float *p, const float *n, float s)
{
    if (t < 0 || t > 8 || fg.race_live)
        fg.bad_model++;
    fg.race_live++;
    fg.race_created++;
    return 77;
}

void wg_race_cp_delete(int h)
{
    if (h != 77 || !fg.race_live)
        fg.bad_handle++;
    fg.race_live--;
}

void wg_clock_hold(unsigned h, unsigned m) { fg.clock_hour = h; fg.clock_minute = m; }
void wg_clock_visible(int on) { fg.clock_visible = on; }
void wg_gravity(float g) { fg.gravity = g; }
void wg_entry_exits(int on) { fg.entry_exits = on; }
void wg_zone_names(int on) { fg.zone_names = on; }
void wg_set_time(unsigned h, unsigned m) { fg.time_calls++; }
void wg_set_weather(unsigned id) { fg.weather_calls++; fg.last_weather = id; }
void wg_sound(unsigned id, const float *p) { fg.sounds++; fg.last_sound = id; }
void wg_explosion(const float *p, unsigned t) { fg.explosions++; fg.last_explosion = t; }
void wg_kill_help(void) { fg.help_killed = 1; }
void wg_move_local(const float *p) { fg.move_local++; memcpy(fg.me, p, 12); }
void wg_set_area(unsigned a) { fg.area = a; fg.area_sets++; }
int  wg_get_area(void) { return fg.area; }
int  wg_line_clear(const float *a, const float *b) { return fg.line_clear; }

int fg_entities(int count, int model, const float *p)
{
    int i;
    for (i = 0; i < count && ent_count < 20000; i++) {
        ents[ent_count].model = model;
        ents[ent_count].hidden = 0;
        memcpy(ents[ent_count].pos, p, 12);
        ent_count++;
    }
    return ent_count;
}

int fg_entities_hidden(void)
{
    int i, n = 0;
    for (i = 0; i < ent_count; i++)
        n += ents[i].hidden;
    return n;
}

void wg_scan_entities(unsigned count, wg_entity_cb cb)
{
    static int at;
    unsigned i;
    for (i = 0; i < count && ent_count; i++, at++) {
        if (at >= ent_count)
            at = 0;
        if (cb(ents[at].model, ents[at].pos) && !ents[at].hidden) {
            ents[at].hidden = 1;
            fg.hidden++;
        }
    }
}

void wg_show_all(wg_entity_cb cb)
{
    int i;
    for (i = 0; i < ent_count; i++)
        if (cb(ents[i].model, ents[i].pos) && ents[i].hidden) {
            ents[i].hidden = 0;
            fg.shown_back++;
        }
}

int wg_actor_create(int skin, const float *p, float a)
{
    int i;
    if (state[skin] != 1 || skin <= 0)
        fg.bad_model++;
    for (i = 0; i < FG_PEDS; i++)
        if (!peds[i]) {
            peds[i] = 1;
            ped_health[i] = 100.0f;
            fg.peds_live++;
            return HANDLE(i);
        }
    fg.pool_overrun++;
    return 0;
}

void wg_actor_delete(int h)
{
    if (!wg_char_alive(h)) {
        fg.bad_handle++;
        return;
    }
    peds[INDEX(h)] = 0;
    fg.peds_live--;
}

void wg_actor_place(int h, const float *p) { if (!wg_char_alive(h)) fg.bad_handle++; }
void wg_actor_angle(int h, float a) { if (!wg_char_alive(h)) fg.bad_handle++; }
void wg_actor_health(int h, float v) { if (wg_char_alive(h)) ped_health[INDEX(h)] = v; }
void wg_actor_invulnerable(int h, int on) {}
float wg_actor_get_health(int h) { return wg_char_alive(h) ? ped_health[INDEX(h)] : 0.0f; }
int wg_actor_hit_by_local(int h)
{
    int i = INDEX(h), hit = ped_seen_hit[i];
    ped_seen_hit[i] = 0;
    return hit;
}
void fg_hit_actor(int h, float amount) { ped_health[INDEX(h)] -= amount; ped_seen_hit[INDEX(h)] = 1; }
int wg_local_weapon(void) { return 24; }
void wg_actor_clear_tasks(int h) {}

int wg_world_to_screen(const float *p, float *x, float *y)
{
    *x = 320.0f;
    *y = 224.0f;
    return 1;
}

float wg_text_width(const char *t) { return (float)strlen(t) * 6.0f; }
void wg_text_begin(void) {}
void wg_text_colour(unsigned c) { fg.last_text_colour = c; }

void wg_text_print(float x, float y, const char *t)
{
    size_t n = strlen(fg.text_log);
    fg.texts++;
    snprintf(fg.text_log + n, sizeof fg.text_log - n, "%08X|%s\n", fg.last_text_colour, t);
}

void wg_text_end(void) {}


void wg_keep_inside(const float *b)
{
    float p[3], q[3];
    wg_local_pos(p);
    q[0] = p[0] > b[0] ? b[0] : p[0] < b[1] ? b[1] : p[0];
    q[1] = p[1] > b[2] ? b[2] : p[1] < b[3] ? b[3] : p[1];
    q[2] = p[2];
    if (q[0] != p[0] || q[1] != p[1])
        wg_move_local(q);
}
