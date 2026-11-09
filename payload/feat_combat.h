#pragma once
/* What the combat stream offers to the rest of the client. */
#include "samp.h"

/* game_remote_update asks this once per remote ped before it issues any command. */
enum {
    COMBAT_REMOTE_NORMAL, /* walk it as usual */
    COMBAT_REMOTE_HOLD,   /* combat owns its tasks: only snap it if it is far off */
    COMBAT_REMOTE_SKIP,   /* leave it alone this frame: it is dying on the server's word */
    COMBAT_REMOTE_LOST    /* the game removed or killed it: recreate it */
};

/* Fills keys, lr, ud, weapon and special, plus the animation and hold fields when samp.h
 * has them. Memory reads only, so game_read_local can call it as often as it likes. */
void feat_combat_local(samp_local_t *l);

int feat_combat_remote_gate(unsigned id, int handle);

/* The move state the ped should use, from the player's keys. fallback is game.c's guess
 * from the distance, used until a foot sync has arrived. */
int feat_combat_remote_move(unsigned id, int fallback);

/* Last health and armour the player reported, for name tags. 0 when not streamed in. */
int feat_combat_remote_health(unsigned id, unsigned *health, unsigned *armour);

/* A model the disc has, for a skin id the server sent: PC-only ids come back as 0. */
unsigned feat_combat_safe_skin(unsigned skin);

/* Pure helpers, exposed for the tests. */
unsigned feat_combat_keys(const void *pad, int lr, int ud);
unsigned feat_combat_weapon_ok(unsigned weapon);
float    feat_combat_weapon_damage(unsigned weapon);
void feat_combat_connecting(void);
void feat_combat_skin_set(void);
