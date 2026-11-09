#include "feat.h"

/* A stream is switched on once it has been merged and run in the game. Until then
 * its code is built but stays idle, so a crash can be traced to what's actually new. */
#define FEAT_VEHICLE 1
#define FEAT_COMBAT  1
#define FEAT_WORLD   1

static samp_rpc_handlers_t handlers;

void feat_init(void)
{
    feat_dialog_init(&handlers);
#if FEAT_VEHICLE
    feat_vehicle_init(&handlers);
#endif
#if FEAT_COMBAT
    feat_combat_init(&handlers);
#endif
#if FEAT_WORLD
    feat_world_init(&handlers);
#endif
    samp_rpc_set_handlers(&handlers);
}

void feat_tick(unsigned now)
{
    feat_dialog_tick(now);
#if FEAT_VEHICLE
    feat_vehicle_tick(now);
#endif
#if FEAT_COMBAT
    feat_combat_tick(now);
#endif
#if FEAT_WORLD
    feat_world_tick(now);
#endif
}

void feat_draw(void)
{
    feat_dialog_draw();
#if FEAT_VEHICLE
    feat_vehicle_draw();
#endif
#if FEAT_COMBAT
    feat_combat_draw();
#endif
#if FEAT_WORLD
    feat_world_draw();
#endif
}
