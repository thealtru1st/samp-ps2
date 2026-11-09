#pragma once
/* A strict stand-in for feat_world_game.c. It keeps real pools and fails the test on the
 * mistakes that would crash the game: a stale handle, a pool overrun, a model used before
 * it loaded, an unbalanced request. */
#include "feat_world_game.h"

#define FG_OBJECTS 250
#define FG_PEDS    74
#define FG_PICKUPS 620

typedef struct {
    int objects_live, objects_peak, pickups_live, pickups_peak, blips_live, peds_live;
    int request_calls, release_calls, requested_now;
    int bad_handle, bad_model, pool_overrun;
    int create_calls, moves, rotates, attach_calls;
    int spheres, race_live, race_created;
    int sounds, explosions, last_sound, last_explosion;
    int time_calls, weather_calls, last_weather, clock_hour, clock_minute, clock_visible;
    float gravity;
    int entry_exits, zone_names, help_killed, move_local;
    int area_calls;
    float last_rect[4];
    unsigned char last_rgba[4];
    int texts;
    char text_log[4096];
    unsigned last_text_colour;
    int menu_open, line_clear, stream_room, load_delay, map_identity;
    int hidden, shown_back;
    float me[3];
    int world_objects;       /* the game's own use of the object pool */
    int ped_slots_used;
    int area, area_sets;
} fg_t;
extern fg_t fg;

void fg_reset(void);
void fg_unload(int m);
void fg_frame(void);                     /* a game frame: finishes pending model loads */
int  fg_object_at(const float *pos);     /* handle of a live object there, or 0 */
int  fg_object_model(int h);
void fg_object_pos(int h, float *pos);
int  fg_pickup_handle_at(const float *pos);
void fg_collect(int handle);
int  fg_entities(int count, int model, const float *pos);
int  fg_entities_hidden(void);
