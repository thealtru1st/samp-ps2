#pragma once
#include "samp.h"

#define CG_MEM_SIZE 0x900000
#define FAKE_OPS 512
#define FAKE_MSGS 512

typedef struct {
    unsigned op;
    int nargs, done;
    char type[16];
    int i[16];
    float f[16];
    char s[16][64];
} fake_op_t;

typedef struct {
    int kind; /* 'r' rpc, 's' sync */
    int id, bits;
    unsigned char d[256];
} fake_msg_t;

extern unsigned char cg_mem[];
extern int cg_host_lr, cg_host_ud, cg_host_anim_found, cg_host_not_resident;
extern fake_op_t fake_ops[];
extern int fake_nops;
extern fake_msg_t fake_msgs[];
extern int fake_nmsgs, fake_state, fake_handles[], fake_area;
extern int fake_teleports, fake_skins, fake_last_skin, fake_remote_adds, fake_remote_dels;
extern float fake_teleport_pos[3];
void fake_dump_to(const char *path);
