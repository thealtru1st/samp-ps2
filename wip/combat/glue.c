/* feat.c for the host builds: only the combat stream, always on. */
#include "feat.h"

static samp_rpc_handlers_t handlers;

void feat_init(void)
{
    feat_combat_init(&handlers);
    samp_rpc_set_handlers(&handlers);
}

void feat_tick(unsigned now) { feat_combat_tick(now); }
void feat_draw(void) { feat_combat_draw(); }
