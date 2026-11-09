#pragma once
/* Everything feat_vehicle.c needs from the game, by SCM handle. feat_vehicle_game.c is
 * the PS2 side; wip/vehicle/fake_game.c stands in for it on the host, so the sync and
 * streaming logic can be tested against a real server without the game.
 *
 * Handles are the script handles the game hands out. Every call checks its handle
 * against the pool first and does nothing for a dead one, because the opcode handlers
 * use what the pool returns without looking at it. */

typedef struct {
    float pos[3], quat[4], right[3], fwd[3], vel[3], turn[3];
} vg_state_t;

typedef struct {
    short lx, ly;
    unsigned char cross, square, circle, triangle, l1, l2, r1, r2, l3, r3, dpad_right;
} vg_pad_t;

/* streaming */
int  vg_model_ok(unsigned model);
int  vg_model_loaded(unsigned model);
void vg_model_request(unsigned model);
int  vg_stream_over(void);
void vg_model_release(unsigned model);
int  vg_mod_loaded(unsigned comp);
void vg_mod_request(unsigned comp);
void vg_mod_release(unsigned comp);
int  vg_mod_ok(int car, unsigned comp); /* the game's own list for this car */
int  vg_car_slots_free(void);           /* game vehicle pool */

/* lifetime. plate may be 0 */
int  vg_create(unsigned model, const float *pos, float angle, const char *plate);
void vg_delete(int car); /* occupants must be out first */
int  vg_alive(int car);
int  vg_wrecked(int car);

/* motion */
void vg_read(int car, vg_state_t *s);
void vg_teleport(int car, const float *pos); /* keeps rotation, zeroes speed */
void vg_set_heading(int car, float degrees);
void vg_set_rotation(int car, const float *quat);
void vg_set_velocity(int car, const float *v);
void vg_set_turn(int car, const float *v);

/* state */
float vg_health(int car);
void vg_set_health(int car, float h);
void vg_set_colours(int car, unsigned c1, unsigned c2);
void vg_set_interior(int car, unsigned interior);
void vg_set_paintjob(int car, int index);  /* -1 removes */
void vg_add_mod(int car, unsigned comp);
void vg_remove_mod(int car, unsigned comp);
void vg_set_engine(int car, int on);
int  vg_engine(int car);
void vg_set_lights(int car, int on);       /* forces on or off */
void vg_set_locked(int car, int locked);
void vg_set_siren(int car, int on);
int  vg_siren(int car);
void vg_set_alarm(int car, int on);
void vg_set_door(int car, unsigned door, int open); /* 0 bonnet, 1 boot, 2-5 doors */
void vg_set_collision(int car, int on);
int  vg_add_blip(int car);
void vg_remove_blip(int blip);

/* damage, in wire format */
void vg_get_damage(int car, unsigned *panels, unsigned *doors, unsigned *lights, unsigned *tyres);
void vg_set_damage(int car, unsigned panels, unsigned doors, unsigned lights, unsigned tyres);
void vg_repair(int car); /* everything fixed; health is left alone */

/* trailers */
int  vg_being_towed(int car); /* a cab is pulling it */
int  vg_towed(int cab);  /* car handle of the trailer behind cab, or 0 */
void vg_attach(int trailer, int cab);
void vg_detach(int trailer, int cab);

/* seats. Passenger indices start at 0, which is SA-MP seat 1 */
int  vg_driver(int car);
int  vg_passenger(int car, unsigned index);
unsigned vg_seats(int car);

/* peds */
int  vg_ped_alive(int ped);
int  vg_ped_car(int ped);  /* car the ped is sitting in, or 0 */
void vg_warp_driver(int ped, int car);
void vg_warp_passenger(int ped, int car, unsigned index);
void vg_warp_out(int ped, const float *pos);
void vg_task_enter(int ped, int car, int passenger);
void vg_task_leave(int ped, int car);
void vg_task_leave_any(int ped);

/* the local player */
int  vg_local_ped(void);
void vg_local_pos(float *pos);
void vg_local_vitals(unsigned *health, unsigned *armour);
int  vg_local_entering(int *passenger); /* car the ped is walking to, or 0 */
int  vg_local_leaving(void);
void vg_read_pad(vg_pad_t *p);
void vg_camera_behind(void);
