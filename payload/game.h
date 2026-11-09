#pragma once
#include "samp.h"
void game_boot(void);                      /* first frame: replace the story with a bare world */
void game_read_local(samp_local_t *out);   /* 0 fields if there is no player yet */
void game_teleport(float x, float y, float z);
void game_set_skin(int model);              /* applied once resident, see game_skin_tick */
void game_skin_tick(void);
int  game_stream_over(void);
void game_heap_extend(void);                /* once at start: grow the game heap into extra RAM */
void game_stream_budget(void);              /* each frame: keep the raised model budget */                /* streaming memory past its limit: load nothing new */                  /* every frame */
int  game_has_player(void);
void game_remote_add(unsigned id, unsigned skin, const float *pos, float angle);
void game_remote_del(unsigned id);
void game_remote_sync(unsigned id, const float *pos, const float *quat, int moving);
void game_remote_clear(void);
int  game_remote_pos(unsigned id, float *pos); /* 0 if not streamed in */
int  game_remote_handle(unsigned id);          /* script handle, 0 if not streamed in */
int  game_in_vehicle(void);
void game_remote_update(void);
