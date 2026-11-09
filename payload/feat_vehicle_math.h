#pragma once
/* Pure maths for vehicle sync: no game, no libc, so the host tests use this exact code.
 *
 * Matrices are the game's CMatrix floats: right at [0], forward ("up") at [4], up
 * ("at") at [8], position at [12].
 *
 * Quaternions are (w, x, y, z) in the SA-MP wire convention, which is the CONJUGATE of
 * the usual quaternion for that matrix: a car turned to heading h has z = -sin(h/2).
 * open.mp's GTAQuat negates Euler angles for the same reason (NPC/npc.cpp), and
 * wip/vehicle/live_run.sh confirms it against GetVehicleZAngle on a real server. */

#define VM_WORLD_LIMIT 25000.0f /* SA-MP's map is +-3000; scripts do use far-away coords */
#define VM_SPEED_LIMIT 20.0f    /* units per 1/50 s step, about 3600 km/h */
#define VM_TURN_LIMIT  2.0f     /* the server itself rejects above 1 */
#define VM_STEP_MS     20.0f    /* one game time step: speeds are units per step */

static inline float vm_sqrt(float v)
{
#ifdef __mips__
    float r;
    __asm__("sqrt.s %0, %1" : "=f"(r) : "f"(v));
    return r;
#else
    return __builtin_sqrtf(v);
#endif
}

/* The EE FPU has no NaN or infinity and reads those bit patterns as huge numbers, so
 * this must look at the bits and never compare. */
static inline int vm_finite(float f)
{
    union { float f; unsigned u; } x;
    x.f = f;
    return (x.u & 0x7F800000u) != 0x7F800000u;
}

static inline int vm_vec_ok(const float *v, unsigned n, float limit)
{
    unsigned i;
    for (i = 0; i < n; i++)
        if (!vm_finite(v[i]) || v[i] > limit || v[i] < -limit)
            return 0;
    return 1;
}

static inline float vm_dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline float vm_dist2(const float *a, const float *b)
{
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return x * x + y * y + z * z;
}

static inline void vm_mat_to_quat(const float *m, float *q)
{
    float rx = m[0], ry = m[1], rz = m[2];
    float ux = m[4], uy = m[5], uz = m[6];
    float ax = m[8], ay = m[9], az = m[10];
    float w = 1.0f + rx + uy + az, x = 1.0f + rx - uy - az;
    float y = 1.0f - rx + uy - az, z = 1.0f - rx - uy + az;

    q[0] = w > 0.0f ? vm_sqrt(w) * 0.5f : 0.0f;
    q[1] = x > 0.0f ? vm_sqrt(x) * 0.5f : 0.0f;
    q[2] = y > 0.0f ? vm_sqrt(y) * 0.5f : 0.0f;
    q[3] = z > 0.0f ? vm_sqrt(z) * 0.5f : 0.0f;
    if (ay - uz < 0.0f) q[1] = -q[1];
    if (rz - ax < 0.0f) q[2] = -q[2];
    if (ux - ry < 0.0f) q[3] = -q[3];
}

/* Fills the 3x3 part only; m[12..14] and the padding floats are left alone. */
static inline void vm_quat_to_mat(const float *q, float *m)
{
    float w = q[0], x = -q[1], y = -q[2], z = -q[3];

    m[0] = 1.0f - 2.0f * (y * y + z * z);
    m[1] = 2.0f * (x * y + w * z);
    m[2] = 2.0f * (x * z - w * y);
    m[4] = 2.0f * (x * y - w * z);
    m[5] = 1.0f - 2.0f * (x * x + z * z);
    m[6] = 2.0f * (y * z + w * x);
    m[8] = 2.0f * (x * z + w * y);
    m[9] = 2.0f * (y * z - w * x);
    m[10] = 1.0f - 2.0f * (x * x + y * y);
}

/* Scales q to unit length. Returns 0, leaving q alone, for anything not a rotation. */
static inline int vm_quat_norm(float *q)
{
    float n, s;
    int i;

    if (!vm_vec_ok(q, 4, 4.0f))
        return 0;
    n = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    if (n < 0.01f)
        return 0;
    s = 1.0f / vm_sqrt(n);
    for (i = 0; i < 4; i++)
        q[i] *= s;
    return 1;
}

/* out = a moved towards b by t, the short way round, unit length. */
static inline void vm_quat_nlerp(const float *a, const float *b, float t, float *out)
{
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    float s = d < 0.0f ? -t : t;
    int i;

    for (i = 0; i < 4; i++)
        out[i] = a[i] * (1.0f - t) + b[i] * s;
    if (!vm_quat_norm(out))
        for (i = 0; i < 4; i++)
            out[i] = b[i];
}

/* The server drops an unoccupied packet unless roll and direction are unit length and
 * perpendicular to within 1e-6, tighter than the game keeps its matrices. Several
 * passes: each removes what rounding left behind in the one before. Returns 0 for a
 * matrix that has collapsed. */
static inline int vm_orthonormal(float *r, float *f)
{
    int pass, i;

    for (pass = 0; pass < 3; pass++) {
        float n, s, d;
        if (!vm_vec_ok(r, 3, 4.0f) || !vm_vec_ok(f, 3, 4.0f))
            return 0;
        n = vm_dot3(r, r);
        if (n < 1e-6f)
            return 0;
        s = 1.0f / vm_sqrt(n);
        for (i = 0; i < 3; i++)
            r[i] *= s;
        d = vm_dot3(r, f);
        for (i = 0; i < 3; i++)
            f[i] -= r[i] * d;
        n = vm_dot3(f, f);
        if (n < 1e-6f)
            return 0;
        s = 1.0f / vm_sqrt(n);
        for (i = 0; i < 3; i++)
            f[i] *= s;
    }
    return 1;
}

/* Rebuilds a quaternion from the two vectors of an unoccupied packet. */
static inline void vm_vectors_to_quat(const float *r, const float *f, float *q)
{
    float m[16];
    int i;

    for (i = 0; i < 3; i++) {
        m[i] = r[i];
        m[4 + i] = f[i];
    }
    m[8] = r[1] * f[2] - r[2] * f[1];
    m[9] = r[2] * f[0] - r[0] * f[2];
    m[10] = r[0] * f[1] - r[1] * f[0];
    vm_mat_to_quat(m, q);
}
