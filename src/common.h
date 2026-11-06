// AXIOM - Hero Strategy
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
#define AXIOM_VERSION_STR "2.0.0"
#endif

namespace ax {

inline double nowSeconds() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return duration<double>(steady_clock::now() - t0).count();
}

constexpr int   TICK_RATE = 30;
constexpr float TICK_DT = 1.0f / TICK_RATE;
constexpr int   MAX_PLAYERS = 6;          // 3v3
constexpr float DEG = 0.017453292519943f;

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float sqr(float v) { return v * v; }
inline float approach(float c, float t, float d) { return c < t ? std::min(c + d, t) : std::max(c - d, t); }

// 2D (ground plane x,z) helpers
inline Vector2 V2(float x, float y) { return Vector2{x, y}; }
inline Vector2 operator+(Vector2 a, Vector2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vector2 operator-(Vector2 a, Vector2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vector2 operator*(Vector2 a, float s) { return {a.x * s, a.y * s}; }
inline Vector2& operator+=(Vector2& a, Vector2 b) { a.x += b.x; a.y += b.y; return a; }
inline Vector2& operator-=(Vector2& a, Vector2 b) { a.x -= b.x; a.y -= b.y; return a; }
inline float dot2(Vector2 a, Vector2 b) { return a.x * b.x + a.y * b.y; }
inline float len2(Vector2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
inline float dist2(Vector2 a, Vector2 b) { return len2(a - b); }
inline float distSq(Vector2 a, Vector2 b) { return sqr(a.x - b.x) + sqr(a.y - b.y); }
inline Vector2 norm2(Vector2 a) { float l = len2(a); return l > 1e-6f ? a * (1.0f / l) : V2(0, 0); }
inline Vector2 lerp2(Vector2 a, Vector2 b, float t) { return a + (b - a) * t; }
inline float angleOf(Vector2 d) { return atan2f(d.y, d.x); }

inline Vector3 V3(float x, float y, float z) { return Vector3{x, y, z}; }
inline Vector3 operator+(Vector3 a, Vector3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vector3 operator-(Vector3 a, Vector3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vector3 operator*(Vector3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float len3(Vector3 a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }
inline Vector3 norm3(Vector3 a) { float l = len3(a); return l > 1e-6f ? a * (1.0f / l) : V3(0, 0, 0); }
inline Vector3 cross3(Vector3 a, Vector3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
// terrain elevation used by the renderer (set to the map being drawn; null = flat)
extern float (*g_groundFn)(Vector2 p);
inline float groundAt(Vector2 p) { return g_groundFn ? g_groundFn(p) : 0.0f; }
inline Vector3 to3(Vector2 p, float y = 0) { return {p.x, y + groundAt(p), p.y}; }

inline float distPointSeg(Vector2 p, Vector2 a, Vector2 b) {
    Vector2 ab = b - a;
    float L = dot2(ab, ab);
    float t = L > 0 ? clampf(dot2(p - a, ab) / L, 0, 1) : 0;
    return dist2(p, a + ab * t);
}

struct Rng {
    uint32_t s = 0x9E3779B9u;
    explicit Rng(uint32_t seed = 1234567u) : s(seed ? seed : 1u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f01() { return (next() & 0xFFFFFF) / 16777216.0f; }
    float range(float a, float b) { return a + (b - a) * f01(); }
    int irange(int a, int b) { return a + (int)(next() % (uint32_t)(b - a + 1)); }
};

inline Color withAlpha(Color c, float a) { c.a = (unsigned char)clampf(a * 255.f, 0, 255); return c; }
inline Color mixColor(Color a, Color b, float t) {
    return Color{(unsigned char)lerpf(a.r, b.r, t), (unsigned char)lerpf(a.g, b.g, t), (unsigned char)lerpf(a.b, b.b, t), (unsigned char)lerpf(a.a, b.a, t)};
}

} // namespace ax
