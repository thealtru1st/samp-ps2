#pragma once
/* Feature modules. Each owns one feat_<name>.c, fills the handler slots it needs in
 * init, runs once per frame in tick while connected, and draws in draw. draw runs in
 * the render hook with text state set up: see ui.c and ui_game.h. */
#include "samp_rpc.h"

void feat_init(void);
void feat_tick(unsigned now);
void feat_draw(void);

void feat_dialog_init(samp_rpc_handlers_t *h);
void feat_dialog_tick(unsigned now);
void feat_dialog_draw(void);
void feat_vehicle_init(samp_rpc_handlers_t *h);
void feat_vehicle_tick(unsigned now);
void feat_vehicle_draw(void);
void feat_combat_init(samp_rpc_handlers_t *h);
void feat_combat_tick(unsigned now);
void feat_combat_draw(void);
void feat_world_init(samp_rpc_handlers_t *h);
void feat_world_tick(unsigned now);
void feat_world_draw(void);
void feat_world_reset(void);
