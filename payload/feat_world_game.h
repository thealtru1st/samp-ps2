#pragma once
/* Everything the world stream needs from the game. feat_world_game.c is the only file
 * that knows addresses or opcodes; the host tests link wip/world/fake_game.c instead.
 * Handles are script handles. Every call checks its handle, because the opcode handlers
 * dereference whatever the pool hands back without a null check. */

/* ---- models. Servers speak PC ids; the PS2 disc numbers about 6000 objects
 * differently. wg_model_map returns the PS2 id, or -1 when the disc has no such model
 * or the game did not load it. Everything below takes PS2 ids. ---- */
int  wg_model_map(int pc_model);
int  wg_ped_model_ok(int model);
int  wg_model_loaded(int model);
void wg_model_request(int model);
void wg_model_release(int model);
int  wg_stream_room(void);          /* 0 when the streamer is full or has a backlog */

int  wg_object_slots(void);         /* free entries in the game's object pool */
int  wg_ped_slots(void);

/* ---- local player and camera ---- */
int  wg_local_char(void);
int  wg_local_pos(float *pos);      /* 0 before there is a player */
int  wg_camera_pos(float *pos);
int  wg_menu_open(void);

/* ---- objects ---- */
int  wg_object_create(int model, const float *pos, const float *rot);
void wg_object_delete(int handle);
void wg_object_move(int handle, const float *pos);
void wg_object_rotate(int handle, const float *rot);
void wg_object_attach_car(int handle, int car, const float *off, const float *rot);
void wg_object_attach_object(int handle, int object, const float *off, const float *rot);
void wg_object_attach_char(int handle, int ped, const float *off, const float *rot);
int  wg_object_alive(int handle);
int  wg_car_alive(int handle);
int  wg_char_alive(int handle);
int  wg_car_pos(int handle, float *pos);

/* ---- pickups. Collected pickups come out of the game's ring one at a time. ---- */
int  wg_pickup_create(int model, int type, const float *pos);
void wg_pickup_delete(int handle);
int  wg_pickup_collected(void);     /* a handle, or 0 when the ring is empty */

/* ---- radar ---- */
int  wg_blip_coord(const float *pos);
int  wg_blip_sprite(int style, const float *pos, int sprite);
void wg_blip_colour(int blip, unsigned colour);
void wg_blip_scale(int blip, int size);
void wg_blip_delete(int blip);
void wg_radar_area(const float *rect, const unsigned char *rgba); /* minx, maxy, maxx, miny */

/* ---- checkpoints ---- */
void wg_draw_sphere(const float *pos, float size);  /* once per frame while it is up */
int  wg_race_cp_create(int type, const float *pos, const float *next, float size);
void wg_race_cp_delete(int handle);

/* ---- world ---- */
void wg_clock_hold(unsigned hour, unsigned minute);
void wg_clock_visible(int on);
void wg_gravity(float g);
void wg_entry_exits(int enabled);
void wg_zone_names(int on);
void wg_set_time(unsigned hour, unsigned minute);
void wg_set_weather(unsigned id);
void wg_sound(unsigned id, const float *pos);
void wg_explosion(const float *pos, unsigned type);
void wg_kill_help(void);
void wg_move_local(const float *pos);
void wg_keep_inside(const float *bounds); /* max x, min x, max y, min y */
void wg_set_area(unsigned area);
int  wg_get_area(void);
int  wg_line_clear(const float *from, const float *to);

/* Buildings and dummies: one call walks the next `count` pool entries and hands every
 * live entity to cb, which returns 1 to hide it. wg_show_all undoes that for whatever
 * cb still matches, over the whole pools. */
typedef int (*wg_entity_cb)(int model, const float *pos);
void wg_scan_entities(unsigned count, wg_entity_cb cb);
void wg_show_all(wg_entity_cb cb);

/* ---- actors ---- */
int   wg_actor_create(int skin, const float *pos, float angle);
void  wg_actor_delete(int handle);
void  wg_actor_place(int handle, const float *pos);
void  wg_actor_angle(int handle, float angle);
void  wg_actor_health(int handle, float health);
void  wg_actor_invulnerable(int handle, int on);
float wg_actor_get_health(int handle);
int   wg_actor_hit_by_local(int handle);  /* true once per hit */
int   wg_local_weapon(void);
void  wg_actor_clear_tasks(int handle);

/* ---- drawing, from the render hook only ---- */
int   wg_world_to_screen(const float *pos, float *x, float *y);
float wg_text_width(const char *text);
void  wg_text_begin(void);
void  wg_text_colour(unsigned rgba);
void  wg_text_print(float x, float y, const char *text);
void  wg_text_end(void);

float wg_sqrt(float v);
