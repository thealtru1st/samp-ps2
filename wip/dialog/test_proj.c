/* feat_dialog_proj.c against a tiny reference: known camera, known answers. */
#include <stdio.h>
#include <math.h>
#include "feat_dialog_gfx.h"

static int bad;

static void expect(const float *pos, const float *at, float fov, const float *w, int ok, float ex, float ey)
{
    float x = 0, y = 0, d = 0;
    int r;
    fd_camera_set(pos, at, fov);
    r = fd_project(w, &x, &y, &d);
    if (r != ok || (ok && (fabsf(x - ex) > 2.0f || fabsf(y - ey) > 2.0f))) {
        printf("FAIL got %d %.1f %.1f, want %d %.1f %.1f\n", r, x, y, ok, ex, ey);
        bad++;
    }
}

int main(void)
{
    float pos[3] = {100, 200, 10}, north[3] = {100, 201, 10}, east[3] = {101, 200, 10};
    float t = tanf(35.0f * 3.14159265f / 180.0f);
    float ahead[3] = {100, 210, 10}, right[3] = {103, 210, 10}, up[3] = {100, 210, 13};
    float behind[3] = {100, 190, 10}, edge[3];

    expect(pos, north, 70, ahead, 1, 320, 224);
    expect(pos, north, 70, right, 1, 320 + 320 * 0.3f / t, 224);
    expect(pos, north, 70, up, 1, 320, 224 - 224 * 0.3f / (t * 0.75f));
    expect(pos, north, 70, behind, 0, 0, 0);
    /* facing east, a point to the south is on the right */
    edge[0] = 110; edge[1] = 197; edge[2] = 10;
    expect(pos, east, 70, edge, 1, 320 + 320 * 0.3f / t, 224);
    /* a junk fov falls back to the default */
    expect(pos, north, 0, right, 1, 320 + 320 * 0.3f / t, 224);
    /* the camera looking straight down has no answer rather than a wrong one */
    { float down[3] = {100, 200, 5}; expect(pos, down, 70, ahead, 0, 0, 0); }
    /* zoomed in, things spread out */
    { float t2 = tanf(15.0f * 3.14159265f / 180.0f), near_c[3] = {101, 210, 10};
      expect(pos, north, 30, near_c, 1, 320 + 320 * 0.1f / t2, 224); }
    printf("projection: %d failed\n", bad);
    return bad != 0;
}
