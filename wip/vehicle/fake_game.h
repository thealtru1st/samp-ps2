#pragma once
#include "feat_vehicle_game.h"
#define FAKE_POOL 70
#define FAKE_PEDS 74

typedef struct {
    int used, handle, model, wrecked, seats, driver, passenger[8], towed;
    vg_state_t s;
    float health, heading_set;
    unsigned c1, c2, interior, panels, doors, lights_dmg, tyres;
    int paintjob, engine, lights, locked, siren, alarm, door[6], no_collision;
    int mods[16], n_mods, teleports, repairs;
    char plate[9];
} fake_car_t;

typedef struct {
    int used, handle, car, warps, outs, enter_car, enter_passenger, leaving;
    float pos[3];
} fake_ped_t;

typedef struct {
    int created, deleted, live, live_peak, models_loaded, models_peak, blips, camera;
} fake_stats_t;

extern fake_car_t fake_cars[FAKE_POOL];
extern fake_ped_t fake_peds[FAKE_PEDS];
extern fake_stats_t fake_stats;
extern vg_pad_t fake_pad;
extern int fake_local, fake_enter_car, fake_enter_passenger, fake_leaving, fake_load_frames;
extern float fake_local_at[3];

void fake_init(void);
void fake_frame(void);
fake_car_t *fake_car(int handle);
fake_ped_t *fake_ped(int handle);
int fake_new_ped(void);
