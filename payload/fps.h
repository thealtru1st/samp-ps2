#pragma once
/* Frame rate. The PAL game waits for two vblanks per frame (25 fps); fps_high makes it
 * wait for one (50 fps). main.c defines it as 1 and writes the limiter every frame. The
 * weak 0 here is for host builds that don't link main.c, so they keep 25 fps timing.
 *
 * Anything counted in frames rather than milliseconds goes through fps_mult(), so it
 * keeps the same length in time at either rate. */
#ifdef FPS_OWNER
int fps_high = 1; /* main.c: 50 fps by default; E in the browser toggles it */
#else
__attribute__((weak)) int fps_high = 0;
#endif

static inline int fps_mult(void) { return fps_high ? 2 : 1; }

#define FPS_LIMIT_BYTE 0x0066794Cu /* vblanks per frame, read by the vblank handler */
