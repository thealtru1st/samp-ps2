#pragma once
/* What other files need from feat_vehicle.c, beyond the hooks in feat.h. */
int  feat_vehicle_in_car(void);              /* local player sits in a server car: send no foot sync */
int  feat_vehicle_handle(unsigned id);       /* SCM handle of a server car in the game, or 0 */
void feat_vehicle_foot_sync(unsigned player);/* a foot packet arrived, so this player is out of any car */
void feat_vehicle_eject(const float *pos);   /* before teleporting the local player */
void feat_vehicle_reset(void);               /* connection gone: remove every car */
