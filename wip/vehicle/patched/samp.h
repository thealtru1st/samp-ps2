#pragma once
/* SA-MP 0.3.7 client logic, independent of the game and platform. */

typedef struct {
    float pos[3];
    float quat[4]; /* w, x, y, z */
    float vel[3];
    unsigned char health, armour, weapon, special;
    unsigned short keys, lr, ud;
} samp_local_t;

enum { SAMP_OFFLINE, SAMP_CONNECTING, SAMP_JOINING, SAMP_CLASS, SAMP_SPAWNING, SAMP_SPAWNED, SAMP_ERROR };

/* Provided by the platform. */
void samp_plat_send(const unsigned char *data, unsigned len);
void samp_plat_local(samp_local_t *out);
int  samp_plat_in_vehicle(void); /* true while vehicle sync replaces foot sync */
void samp_plat_player_add(unsigned id, unsigned skin, const float *pos, float angle);
void samp_plat_player_del(unsigned id);
void samp_plat_player_sync(unsigned id, const float *pos, const float *quat, unsigned keys, int moving);
void samp_plat_set_pos(const float *pos);
void samp_plat_set_skin(unsigned skin);
void samp_plat_message(const char *text);
void samp_plat_closed(const char *why); /* only for failures, not our own disconnect */

void samp_connect(unsigned ip, unsigned port, const char *name, const char *password);
void samp_disconnect(void);
void samp_datagram(const unsigned char *data, unsigned len, unsigned now);
void samp_tick(unsigned now);
void samp_chat(const char *text);
int  samp_state(void);

typedef struct {
    unsigned rpcs, sync_in, sync_out, player_id;
    char server_name[64];
    float spawn[3];
    unsigned skin;
    unsigned last_pid, foot_in;
    float last_pos[3];
} samp_stats_t;
extern samp_stats_t samp_stats;
