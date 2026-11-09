/* World to screen. The game's own projection is not found in the ELF yet, so this
 * rebuilds it from what the camera script opcodes give: position, the point looked at
 * and the field of view, which is horizontal on a 4:3 view. No roll, which only cutscene
 * cameras use. Portable, so wip/dialog can test it. */
#include "feat_dialog_gfx.h"

#define DEFAULT_FOV 70.0f
#define VIEW_ASPECT 0.75f
#define NEAR_Z      0.5f
#define SCREEN_SLOP 60.0f

static struct {
    int ok;
    float pos[3], right[3], up[3], front[3], tan_x;
} cam;

static float root(float v)
{
#ifdef __mips__
    float r;
    __asm__("sqrt.s %0, %1" : "=f"(r) : "f"(v));
    return r;
#else
    float r = v > 1.0f ? v : 1.0f;
    int i;
    for (i = 0; i < 30; i++)
        r = 0.5f * (r + v / r);
    return r;
#endif
}

/* Good to 0.1% up to a 90 degree field of view, and only ever fed up to 120. */
static float tan_small(float x)
{
    float x2 = x * x;
    return x * (1.0f + x2 * (0.33333f + x2 * (0.13333f + x2 * 0.05397f)));
}

void fd_camera_set(const float *pos, const float *at, float fov)
{
    float len;
    int i;

    for (i = 0; i < 3; i++) {
        cam.pos[i] = pos[i];
        cam.front[i] = at[i] - pos[i];
    }
    if (!(fov > 5.0f && fov < 120.0f))
        fov = DEFAULT_FOV;
    cam.tan_x = tan_small(fov * 0.5f * 0.0174533f);

    len = cam.front[0] * cam.front[0] + cam.front[1] * cam.front[1] + cam.front[2] * cam.front[2];
    cam.ok = len > 1.0e-6f;
    if (!cam.ok)
        return;
    len = 1.0f / root(len);
    for (i = 0; i < 3; i++)
        cam.front[i] *= len;
    cam.right[0] = cam.front[1];
    cam.right[1] = -cam.front[0];
    cam.right[2] = 0.0f;
    len = cam.right[0] * cam.right[0] + cam.right[1] * cam.right[1];
    cam.ok = len > 1.0e-6f; /* straight up or down has no right */
    if (!cam.ok)
        return;
    len = 1.0f / root(len);
    cam.right[0] *= len;
    cam.right[1] *= len;
    cam.up[0] = cam.right[1] * cam.front[2];
    cam.up[1] = -cam.right[0] * cam.front[2];
    cam.up[2] = cam.right[0] * cam.front[1] - cam.right[1] * cam.front[0];
}

int fd_project(const float *world, float *x, float *y, float *dist)
{
    float v[3], depth, sx, sy;
    int i;

    if (!cam.ok)
        return 0;
    for (i = 0; i < 3; i++)
        v[i] = world[i] - cam.pos[i];
    depth = v[0] * cam.front[0] + v[1] * cam.front[1] + v[2] * cam.front[2];
    if (depth < NEAR_Z)
        return 0;
    sx = (v[0] * cam.right[0] + v[1] * cam.right[1]) / (depth * cam.tan_x);
    sy = (v[0] * cam.up[0] + v[1] * cam.up[1] + v[2] * cam.up[2]) / (depth * cam.tan_x * VIEW_ASPECT);
    *x = 320.0f + 320.0f * sx;
    *y = 224.0f - 224.0f * sy;
    *dist = root(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    return *x > -SCREEN_SLOP && *x < 640.0f + SCREEN_SLOP && *y > -SCREEN_SLOP && *y < 448.0f + SCREEN_SLOP;
}
