#pragma once
/* Shared between the feat_world_*.c files. */
#include "feat.h"
#include "feat_world_game.h"

/* game.c and the vehicle stream know the handles. Both are optional here, so this builds
 * before they are wired up; things attached to them simply stay away until then. */
int game_remote_handle(unsigned id) __attribute__((weak));
int game_remote_pos(unsigned id, float *pos) __attribute__((weak));
int feat_vehicle_handle(unsigned id) __attribute__((weak));

#define WORLD_LIMIT 20000.0f  /* past this nothing is a real position */

void  feat_world_reset(void);           /* removes everything the server put in the game */

int   world_float_ok(float v);
int   world_pos_ok(const float *p);
float world_dist2(const float *a, const float *b);
int   world_player_char(unsigned id);   /* the local player or a streamed one, else 0 */
void  world_send(int rpc, const void *data, int bits);

void wobj_init(samp_rpc_handlers_t *h);
void wobj_tick(unsigned now);
void wobj_reset(void);

void wpick_init(samp_rpc_handlers_t *h);
void wpick_tick(unsigned now);
void wpick_reset(void);

void wradar_init(samp_rpc_handlers_t *h);
void wradar_tick(unsigned now);
void wradar_draw(void);
void wradar_reset(void);

void wlabel_init(samp_rpc_handlers_t *h);
void wlabel_tick(unsigned now);
void wlabel_draw(void);
void wlabel_reset(void);

void wactor_init(samp_rpc_handlers_t *h);
void wactor_tick(unsigned now);
void wactor_reset(void);
