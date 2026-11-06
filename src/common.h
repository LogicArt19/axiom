// AXIOM - Tactical Competitive Shooter
// common.h : shared includes, constants and math helpers
#pragma once

#include "raylib.h"
#define RAYMATH_DISABLE_CPP_OPERATORS
#include "raymath.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <functional>
#include <chrono>

#ifndef AXIOM_VERSION_STR
#define AXIOM_VERSION_STR "1.0.0"
#endif

namespace ax {

// monotonic wall clock independent from the window (used by the dedicated server too)
inline double nowSeconds() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return duration<double>(steady_clock::now() - t0).count();
}

// ---------------------------------------------------------------------------
// Simulation constants
// ---------------------------------------------------------------------------
constexpr int   TICK_RATE      = 128;
constexpr float TICK_DT        = 1.0f / TICK_RATE;
constexpr int   MAX_PLAYERS    = 16;
constexpr float PLAYER_RADIUS  = 0.40f;
constexpr float STAND_HEIGHT   = 1.80f;
constexpr float CROUCH_HEIGHT  = 1.30f;
constexpr float STAND_EYE      = 1.64f;
constexpr float CROUCH_EYE     = 1.16f;
constexpr float GRAVITY        = 16.0f;
constexpr float JUMP_SPEED     = 5.45f;
constexpr float STEP_HEIGHT    = 0.45f;

constexpr float DEG = 0.017453292519943f;

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float sqr(float v) { return v * v; }
inline float approach(float cur, float target, float delta) {
    if (cur < target) return std::min(cur + delta, target);
    return std::max(cur - delta, target);
}
inline float angleNorm(float a) {          // degrees -> (-180,180]
    while (a > 180.f) a -= 360.f;
    while (a <= -180.f) a += 360.f;
    return a;
}
inline float angleDiff(float a, float b) { return angleNorm(a - b); }

inline Vector3 V3(float x, float y, float z) { return Vector3{x, y, z}; }
inline Vector3 operator+(Vector3 a, Vector3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vector3 operator-(Vector3 a, Vector3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vector3 operator*(Vector3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vector3 operator*(float s, Vector3 a) { return {a.x * s, a.y * s, a.z * s}; }
inline Vector3& operator+=(Vector3& a, Vector3 b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
inline Vector3& operator-=(Vector3& a, Vector3 b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
inline Vector3& operator*=(Vector3& a, float s) { a.x *= s; a.y *= s; a.z *= s; return a; }
inline float dot3(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float len3(Vector3 a) { return sqrtf(dot3(a, a)); }
inline float len2xz(Vector3 a) { return sqrtf(a.x * a.x + a.z * a.z); }
inline Vector3 norm3(Vector3 a) { float l = len3(a); return l > 1e-6f ? a * (1.0f / l) : V3(0, 0, 0); }
inline Vector3 cross3(Vector3 a, Vector3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline Vector3 lerp3(Vector3 a, Vector3 b, float t) { return a + (b - a) * t; }

// yaw: 0 = looking toward -Z, increases turning left?  We use: yaw 0 -> -Z, yaw 90 -> -X? No:
// Convention: forward = (sin(yaw), 0, -cos(yaw)) with yaw in degrees, positive yaw turns RIGHT.
inline Vector3 forwardFromAngles(float yawDeg, float pitchDeg) {
    float cy = cosf(yawDeg * DEG), sy = sinf(yawDeg * DEG);
    float cp = cosf(pitchDeg * DEG), sp = sinf(pitchDeg * DEG);
    return {sy * cp, sp, -cy * cp};
}
inline Vector3 flatForward(float yawDeg) { return {sinf(yawDeg * DEG), 0, -cosf(yawDeg * DEG)}; }
inline Vector3 flatRight(float yawDeg) { return {cosf(yawDeg * DEG), 0, sinf(yawDeg * DEG)}; }
inline void anglesFromDir(Vector3 d, float& yaw, float& pitch) {
    yaw = atan2f(d.x, -d.z) / DEG;
    pitch = atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z)) / DEG;
}

// Deterministic RNG (xorshift)
struct Rng {
    uint32_t s = 0x9E3779B9u;
    explicit Rng(uint32_t seed = 1234567u) : s(seed ? seed : 1u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f01() { return (next() & 0xFFFFFF) / 16777216.0f; }
    float range(float a, float b) { return a + (b - a) * f01(); }
    int irange(int a, int b) { return a + (int)(next() % (uint32_t)(b - a + 1)); }
};

// Ray / shape intersection helpers --------------------------------------------
inline bool raySphere(Vector3 o, Vector3 d, Vector3 c, float r, float& t) {
    Vector3 m = o - c;
    float b = dot3(m, d);
    float cc = dot3(m, m) - r * r;
    if (cc > 0 && b > 0) return false;
    float disc = b * b - cc;
    if (disc < 0) return false;
    t = -b - sqrtf(disc);
    if (t < 0) t = 0;
    return true;
}
// Capsule from a to b, radius r
inline bool rayCapsule(Vector3 o, Vector3 d, Vector3 a, Vector3 b, float r, float& tOut) {
    // sample-based closest approach (robust & cheap enough for few hitboxes)
    Vector3 ab = b - a;
    float best = 1e9f;
    bool hit = false;
    // analytic infinite cylinder + caps via closest points between lines
    Vector3 w0 = o - a;
    float A = dot3(d, d), B = dot3(d, ab), C = dot3(ab, ab), D = dot3(d, w0), E = dot3(ab, w0);
    float den = A * C - B * B;
    float tc;
    if (den < 1e-8f) { tc = (B > C ? D / B : E / C); }
    else { tc = (A * E - B * D) / den; }
    tc = clampf(tc, 0, 1);
    Vector3 pc = a + ab * tc;
    float t;
    if (raySphere(o, d, pc, r, t)) { best = t; hit = true; }
    // also test the end caps for robustness
    if (raySphere(o, d, a, r, t) && t < best) { best = t; hit = true; }
    if (raySphere(o, d, b, r, t) && t < best) { best = t; hit = true; }
    if (hit) tOut = best;
    return hit;
}

inline Color withAlpha(Color c, float a) { c.a = (unsigned char)clampf(a * 255.f, 0, 255); return c; }
inline Color mixColor(Color a, Color b, float t) {
    return Color{(unsigned char)lerpf(a.r, b.r, t), (unsigned char)lerpf(a.g, b.g, t),
                 (unsigned char)lerpf(a.b, b.b, t), (unsigned char)lerpf(a.a, b.a, t)};
}

enum Team : uint8_t { TEAM_NONE = 0, TEAM_ATK = 1, TEAM_DEF = 2 };
inline Team otherTeam(Team t) { return t == TEAM_ATK ? TEAM_DEF : (t == TEAM_DEF ? TEAM_ATK : TEAM_NONE); }

} // namespace ax
