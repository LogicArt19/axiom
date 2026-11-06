#include "audio.h"
#include "config.h"
#include "map.h"

namespace ax {

Audio g_audio;

static constexpr int SR = 44100;

// ---------------------------------------------------------------------------
// Tiny DSP toolkit
// ---------------------------------------------------------------------------
struct Buf {
    std::vector<float> s;
    explicit Buf(float seconds) : s((size_t)(seconds * SR), 0.0f) {}
    size_t n() const { return s.size(); }
};
static Rng g_dsp(4242);
static float white() { return g_dsp.f01() * 2.f - 1.f; }

struct OnePole {      // simple lowpass
    float y = 0;
    float process(float x, float cutoff) {
        float a = 1.0f - expf(-2.0f * 3.14159f * cutoff / SR);
        y += a * (x - y);
        return y;
    }
};
struct HighPass {
    OnePole lp;
    float process(float x, float cutoff) { return x - lp.process(x, cutoff); }
};

static void addNoise(Buf& b, float start, float dur, float amp, float cutStart, float cutEnd, float decay, float hp = 0) {
    OnePole lp, lp2; HighPass h;
    size_t s0 = (size_t)(start * SR), n = (size_t)(dur * SR);
    for (size_t i = 0; i < n && s0 + i < b.n(); i++) {
        float t = (float)i / SR;
        float cut = cutStart + (cutEnd - cutStart) * std::min(1.0f, t / dur);
        float v = lp2.process(lp.process(white(), cut), cut);
        if (hp > 0) v = h.process(v, hp);
        b.s[s0 + i] += v * amp * expf(-t * decay);
    }
}
static void addSine(Buf& b, float start, float dur, float amp, float f0, float f1, float decay, float attack = 0.002f) {
    size_t s0 = (size_t)(start * SR), n = (size_t)(dur * SR);
    float ph = 0;
    for (size_t i = 0; i < n && s0 + i < b.n(); i++) {
        float t = (float)i / SR;
        float f = f0 + (f1 - f0) * (t / dur);
        ph += 2 * 3.14159f * f / SR;
        float env = expf(-t * decay) * std::min(1.0f, t / attack);
        b.s[s0 + i] += sinf(ph) * amp * env;
    }
}
static void addSquare(Buf& b, float start, float dur, float amp, float f, float decay) {
    size_t s0 = (size_t)(start * SR), n = (size_t)(dur * SR);
    OnePole lp;
    for (size_t i = 0; i < n && s0 + i < b.n(); i++) {
        float t = (float)i / SR;
        float v = fmodf(t * f, 1.0f) < 0.5f ? 1.f : -1.f;
        b.s[s0 + i] += lp.process(v, 5000) * amp * expf(-t * decay) * std::min(1.0f, t / 0.002f);
    }
}
static void addSaw(Buf& b, float start, float dur, float amp, float f, float decay, float cutoff, float attack = 0.01f) {
    size_t s0 = (size_t)(start * SR), n = (size_t)(dur * SR);
    OnePole lp, lp2;
    for (size_t i = 0; i < n && s0 + i < b.n(); i++) {
        float t = (float)i / SR;
        float v = 2.f * fmodf(t * f, 1.0f) - 1.f;
        float env = std::min(1.0f, t / attack) * expf(-t * decay);
        float rel = std::min(1.0f, (dur - t) / 0.05f);
        b.s[s0 + i] += lp2.process(lp.process(v, cutoff), cutoff) * amp * env * rel;
    }
}
static void addClick(Buf& b, float start, float amp, float freq, float len) {
    addNoise(b, start, len, amp, freq * 1.5f, freq, 60.f / len * 0.08f, freq * 0.5f);
    addSine(b, start, len, amp * 0.5f, freq, freq * 0.8f, 6.f / len);
}
static void reverb(Buf& b, float mix, float decay) {
    // cheap multi-tap comb for a sense of space
    const int taps[4] = {1557, 1617, 1491, 1422};
    std::vector<float> out = b.s;
    for (int k = 0; k < 4; k++) {
        std::vector<float> buf(taps[k], 0.f);
        int idx = 0;
        for (size_t i = 0; i < b.n(); i++) {
            float y = buf[idx];
            buf[idx] = b.s[i] + y * decay;
            idx = (idx + 1) % taps[k];
            out[i] += y * mix * 0.25f;
        }
    }
    b.s.swap(out);
}
static void finalize(Buf& b, float gain, float drive = 1.0f) {
    float peak = 0;
    for (float& v : b.s) { v = tanhf(v * drive) / tanhf(drive); peak = std::max(peak, fabsf(v)); }
    float k = peak > 0 ? gain / peak : 1;
    for (float& v : b.s) v *= k;
    // fade out last 5ms
    size_t f = std::min<size_t>(b.n(), SR / 200);
    for (size_t i = 0; i < f; i++) b.s[b.n() - 1 - i] *= (float)i / f;
}
static Sound toSound(const Buf& b) {
    Wave w{};
    w.frameCount = (unsigned)b.n();
    w.sampleRate = SR;
    w.sampleSize = 16;
    w.channels = 1;
    short* d = (short*)MemAlloc((unsigned)(b.n() * sizeof(short)));
    for (size_t i = 0; i < b.n(); i++) d[i] = (short)(clampf(b.s[i], -1, 1) * 32000);
    w.data = d;
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

struct ShotParams { float crack, bodyCut, bodyDecay, thumpF, thumpAmp, tailAmp, tailDecay, dur, drive; };
static Buf makeShot(const ShotParams& p) {
    Buf b(p.dur);
    addNoise(b, 0, 0.006f, p.crack, 9000, 6000, 200, 1500);
    addNoise(b, 0, p.dur * 0.5f, 1.0f, p.bodyCut, p.bodyCut * 0.25f, p.bodyDecay);
    addSine(b, 0, 0.18f, p.thumpAmp, p.thumpF, p.thumpF * 0.45f, 22.f, 0.001f);
    addNoise(b, 0.01f, p.dur - 0.01f, p.tailAmp, 1800, 300, p.tailDecay);
    reverb(b, 0.35f, 0.45f);
    finalize(b, 0.95f, p.drive);
    return b;
}

// ---------------------------------------------------------------------------
void Audio::init() {
    InitAudioDevice();
    ok = IsAudioDeviceReady();
    if (!ok) { TraceLog(LOG_WARNING, "AXIOM: no audio device"); return; }
    SetMasterVolume(g_settings.volMaster);
    Buf* bufs[SFX_COUNT] = {nullptr};
    auto put = [&](Sfx id, Buf b) { bufs[id] = new Buf(std::move(b)); };

    //                       crack bodyCut decay thumpF thumpA tail tailDecay dur drive
    put(SFX_SHOT_PISTOL,   makeShot({1.2f, 4200, 38, 160, 0.6f, 0.25f, 9, 0.45f, 2.0f}));
    put(SFX_SHOT_HEAVY,    makeShot({1.4f, 3000, 22, 95, 1.1f, 0.45f, 6, 0.75f, 2.6f}));
    put(SFX_SHOT_SMG,      makeShot({0.9f, 5200, 55, 190, 0.45f, 0.18f, 14, 0.30f, 1.8f}));
    put(SFX_SHOT_SHOTGUN,  makeShot({1.6f, 2400, 16, 70, 1.3f, 0.55f, 5, 0.9f, 3.0f}));
    put(SFX_SHOT_RIFLE,    makeShot({1.3f, 3400, 30, 110, 0.9f, 0.38f, 7, 0.6f, 2.6f}));
    put(SFX_SHOT_RIFLE2,   makeShot({1.1f, 4000, 36, 130, 0.7f, 0.30f, 8, 0.5f, 2.2f}));
    put(SFX_SHOT_SNIPER,   makeShot({1.8f, 2600, 12, 60, 1.4f, 0.6f, 3.5f, 1.4f, 3.2f}));
    put(SFX_SHOT_MARKSMAN, makeShot({1.5f, 3200, 20, 90, 1.0f, 0.45f, 5, 1.0f, 2.8f}));
    put(SFX_SHOT_LMG,      makeShot({1.2f, 3000, 32, 100, 0.95f, 0.35f, 8, 0.55f, 2.6f}));
    {
        Buf b(1.4f);
        addNoise(b, 0, 1.4f, 1.0f, 700, 150, 3.5f);
        addSine(b, 0, 0.4f, 0.7f, 70, 40, 7);
        reverb(b, 0.5f, 0.5f);
        finalize(b, 0.8f, 1.5f);
        put(SFX_SHOT_DISTANT, std::move(b));
    }
    for (int i = 0; i < 4; i++) {
        Buf b(0.16f);
        float f = 900 + i * 140.f;
        addNoise(b, 0, 0.05f, 1.0f, f * 2, f, 70, 250);
        addNoise(b, 0.045f + i * 0.006f, 0.06f, 0.7f, f * 1.6f, f * 0.8f, 80, 300);
        addSine(b, 0, 0.05f, 0.4f, 110 + i * 10.f, 70, 60);
        finalize(b, 0.8f, 1.2f);
        put((Sfx)(SFX_STEP0 + i), std::move(b));
    }
    { Buf b(0.25f); addNoise(b, 0, 0.2f, 1, 1200, 300, 25, 120); addSine(b, 0, 0.12f, 0.8f, 90, 50, 30); finalize(b, 0.85f, 1.5f); put(SFX_LAND, std::move(b)); }
    { Buf b(0.15f); addNoise(b, 0, 0.12f, 0.6f, 2500, 900, 40, 400); finalize(b, 0.5f); put(SFX_JUMP, std::move(b)); }
    { Buf b(0.8f); addClick(b, 0.02f, 0.9f, 2600, 0.03f); addClick(b, 0.32f, 1.0f, 1400, 0.05f); addSine(b, 0.32f, 0.08f, 0.6f, 180, 120, 40);
      addClick(b, 0.62f, 0.9f, 3200, 0.025f); addClick(b, 0.68f, 0.8f, 2200, 0.03f); finalize(b, 0.7f); put(SFX_RELOAD, std::move(b)); }
    { Buf b(0.3f); addNoise(b, 0, 0.12f, 0.4f, 3000, 1500, 30, 800); addClick(b, 0.12f, 0.8f, 3000, 0.02f); finalize(b, 0.55f); put(SFX_DRAW, std::move(b)); }
    { Buf b(0.12f); addClick(b, 0, 1.0f, 4200, 0.02f); finalize(b, 0.6f); put(SFX_DRYFIRE, std::move(b)); }
    { Buf b(0.18f); addClick(b, 0, 0.6f, 1800, 0.02f); addSine(b, 0.02f, 0.12f, 0.3f, 900, 1400, 20); finalize(b, 0.45f); put(SFX_SCOPE, std::move(b)); }
    { Buf b(0.18f); addNoise(b, 0, 0.15f, 1.0f, 1200, 200, 35); addSine(b, 0, 0.1f, 0.9f, 140, 80, 40); finalize(b, 0.85f, 2.0f); put(SFX_HIT_BODY, std::move(b)); }
    { Buf b(0.45f); addSine(b, 0, 0.45f, 0.6f, 2230, 2210, 9); addSine(b, 0, 0.35f, 0.4f, 3170, 3150, 12); addSine(b, 0, 0.25f, 0.3f, 4730, 4700, 16);
      addClick(b, 0, 0.7f, 5000, 0.01f); finalize(b, 0.9f); put(SFX_HIT_HEAD, std::move(b)); }
    { Buf b(0.4f); addSine(b, 0, 0.35f, 0.5f, 1630, 1600, 11); addSine(b, 0, 0.3f, 0.4f, 2770, 2740, 14); addNoise(b, 0, 0.05f, 0.6f, 6000, 3000, 60, 1000);
      finalize(b, 0.85f); put(SFX_HIT_HELMET, std::move(b)); }
    { Buf b(0.3f); addSine(b, 0, 0.09f, 0.6f, 880, 880, 10); addSine(b, 0.07f, 0.2f, 0.6f, 1320, 1320, 12); finalize(b, 0.6f); put(SFX_KILL, std::move(b)); }
    { Buf b(0.25f); addNoise(b, 0, 0.2f, 1.0f, 600, 150, 20); addSine(b, 0, 0.15f, 1.0f, 70, 45, 20); finalize(b, 0.9f, 2.5f); put(SFX_HURT, std::move(b)); }
    { Buf b(0.25f); addNoise(b, 0, 0.2f, 1.0f, 5000, 1200, 35, 600); addClick(b, 0, 0.6f, 3000, 0.01f); finalize(b, 0.7f, 1.8f); put(SFX_IMPACT_CONCRETE, std::move(b)); }
    { Buf b(0.22f); addNoise(b, 0, 0.18f, 1.0f, 1800, 500, 30, 150); addSine(b, 0, 0.08f, 0.5f, 240, 160, 35); finalize(b, 0.7f, 1.6f); put(SFX_IMPACT_WOOD, std::move(b)); }
    { Buf b(0.5f); addSine(b, 0, 0.45f, 0.5f, 3400, 2100, 8); addNoise(b, 0, 0.06f, 0.8f, 7000, 4000, 60, 2000); finalize(b, 0.6f); put(SFX_IMPACT_METAL, std::move(b)); }
    { Buf b(0.25f); addNoise(b, 0, 0.25f, 1.0f, 3000, 7000, 8, 1500); for (size_t i = 0; i < b.n(); i++) { float t = (float)i / b.n(); b.s[i] *= sinf(t * 3.14159f); } finalize(b, 0.5f); put(SFX_WHIZ, std::move(b)); }
    { Buf b(0.22f); addNoise(b, 0, 0.22f, 1.0f, 900, 4000, 6, 600); for (size_t i = 0; i < b.n(); i++) { float t = (float)i / b.n(); b.s[i] *= sinf(t * 3.14159f); } finalize(b, 0.55f); put(SFX_KNIFE_SWING, std::move(b)); }
    { Buf b(0.25f); addNoise(b, 0, 0.18f, 1.0f, 2500, 600, 30); addSine(b, 0, 0.1f, 0.6f, 160, 90, 30); finalize(b, 0.8f, 1.8f); put(SFX_KNIFE_HIT, std::move(b)); }
    { Buf b(0.25f); addClick(b, 0, 0.8f, 3800, 0.015f); addSine(b, 0.05f, 0.15f, 0.25f, 4200, 4000, 25); finalize(b, 0.55f); put(SFX_NADE_PIN, std::move(b)); }
    { Buf b(0.2f); addClick(b, 0, 1.0f, 1700, 0.03f); addSine(b, 0, 0.12f, 0.4f, 600, 500, 35); finalize(b, 0.6f); put(SFX_NADE_BOUNCE, std::move(b)); }
    { Buf b(1.6f); addNoise(b, 0, 0.015f, 2.0f, 12000, 9000, 50, 2000); addNoise(b, 0, 1.5f, 1.0f, 5000, 400, 4.5f); addSine(b, 0, 0.3f, 0.8f, 80, 40, 12);
      reverb(b, 0.5f, 0.55f); finalize(b, 1.0f, 3.0f); put(SFX_FLASHBANG, std::move(b)); }
    { Buf b(3.5f); addSine(b, 0, 3.5f, 0.5f, 3800, 3790, 0.9f, 0.05f); addSine(b, 0, 3.5f, 0.35f, 3870, 3860, 1.0f, 0.05f); finalize(b, 0.35f); put(SFX_EAR_RING, std::move(b)); }
    { Buf b(2.2f); addNoise(b, 0, 0.02f, 2.0f, 10000, 5000, 40, 800); addNoise(b, 0, 2.1f, 1.2f, 3500, 120, 2.6f); addSine(b, 0, 0.8f, 1.2f, 55, 28, 4.5f, 0.001f);
      reverb(b, 0.6f, 0.6f); finalize(b, 1.0f, 3.5f); put(SFX_FRAG, std::move(b)); }
    { Buf b(2.0f); addNoise(b, 0, 2.0f, 1.0f, 6000, 3000, 0.9f, 1500); for (size_t i = 0; i < b.n(); i++) { float t = (float)i / SR; b.s[i] *= std::min(1.0f, t / 0.15f); } finalize(b, 0.45f); put(SFX_SMOKE, std::move(b)); }
    {
        Buf b(3.0f);
        addNoise(b, 0, 3.0f, 0.5f, 900, 900, 0.0f, 150);
        for (int k = 0; k < 90; k++) { float t = g_dsp.range(0, 2.9f); addNoise(b, t, 0.03f, g_dsp.range(0.3f, 1.0f), 4000, 1500, 120, 500); }
        for (size_t i = 0; i < b.n(); i++) { float t = (float)i / SR; b.s[i] *= std::min(1.0f, t / 0.2f) * std::min(1.0f, (3.0f - t) / 0.3f); }
        finalize(b, 0.55f, 1.5f);
        put(SFX_FIRE, std::move(b));
    }
    { Buf b(0.6f); addNoise(b, 0, 0.6f, 1.0f, 5000, 1500, 5, 800); finalize(b, 0.45f); put(SFX_FIRE_OUT, std::move(b)); }
    { Buf b(0.11f); addSquare(b, 0, 0.1f, 0.6f, 1950, 25); finalize(b, 0.55f); put(SFX_BOMB_BEEP, std::move(b)); }
    { Buf b(3.2f); for (int k = 0; k < 7; k++) { addSquare(b, k * 0.42f, 0.07f, 0.5f, 1500 + (k % 3) * 220.f, 30); addClick(b, k * 0.42f + 0.2f, 0.4f, 3000, 0.015f); } finalize(b, 0.5f); put(SFX_PLANTING, std::move(b)); }
    { Buf b(1.0f); for (int k = 0; k < 4; k++) { addClick(b, k * 0.25f, 0.6f, 2800 - k * 200.f, 0.02f); } finalize(b, 0.45f); put(SFX_DEFUSING, std::move(b)); }
    { Buf b(3.5f); addNoise(b, 0, 0.03f, 2.0f, 9000, 4000, 30, 500); addNoise(b, 0, 3.4f, 1.3f, 2500, 60, 1.4f); addSine(b, 0, 2.0f, 1.4f, 42, 22, 1.6f, 0.002f);
      reverb(b, 0.7f, 0.65f); finalize(b, 1.0f, 4.0f); put(SFX_BOMB_EXPLODE, std::move(b)); }
    { Buf b(1.2f); addSaw(b, 0, 0.25f, 0.5f, 660, 2, 3000); addSaw(b, 0.25f, 0.25f, 0.5f, 660, 2, 3000); addSaw(b, 0.5f, 0.6f, 0.5f, 880, 2, 3000); finalize(b, 0.6f); put(SFX_BOMB_PLANTED, std::move(b)); }
    { Buf b(0.03f); addSine(b, 0, 0.03f, 0.5f, 2400, 2600, 90); finalize(b, 0.25f); put(SFX_UI_HOVER, std::move(b)); }
    { Buf b(0.08f); addSine(b, 0, 0.08f, 0.6f, 1300, 700, 40); addClick(b, 0, 0.3f, 4000, 0.008f); finalize(b, 0.45f); put(SFX_UI_CLICK, std::move(b)); }
    { Buf b(0.35f); addSine(b, 0, 0.1f, 0.5f, 1046, 1046, 15); addSine(b, 0.08f, 0.25f, 0.5f, 1568, 1568, 12); addNoise(b, 0, 0.05f, 0.3f, 6000, 3000, 60, 2000); finalize(b, 0.5f); put(SFX_UI_BUY, std::move(b)); }
    { Buf b(0.25f); addSquare(b, 0, 0.1f, 0.4f, 220, 15); addSquare(b, 0.11f, 0.12f, 0.4f, 185, 15); finalize(b, 0.45f); put(SFX_UI_DENY, std::move(b)); }
    { Buf b(1.4f); addSaw(b, 0, 0.18f, 0.4f, 392, 3, 2500); addSaw(b, 0.18f, 0.18f, 0.4f, 523, 3, 2500); addSaw(b, 0.36f, 1.0f, 0.5f, 784, 2.2f, 3000); addSaw(b, 0.36f, 1.0f, 0.3f, 392, 2.2f, 2000);
      reverb(b, 0.5f, 0.5f); finalize(b, 0.6f); put(SFX_ROUND_START, std::move(b)); }
    { Buf b(2.0f); float ch[4] = {523.25f, 659.25f, 783.99f, 1046.5f}; for (int k = 0; k < 4; k++) addSaw(b, k * 0.07f, 1.8f, 0.3f, ch[k], 1.4f, 2600, 0.02f);
      reverb(b, 0.6f, 0.55f); finalize(b, 0.65f); put(SFX_ROUND_WIN, std::move(b)); }
    { Buf b(2.0f); float ch[3] = {440.f, 349.23f, 293.66f}; for (int k = 0; k < 3; k++) addSaw(b, k * 0.22f, 1.5f, 0.35f, ch[k], 1.6f, 1500, 0.03f);
      reverb(b, 0.6f, 0.55f); finalize(b, 0.6f); put(SFX_ROUND_LOSE, std::move(b)); }
    { Buf b(0.05f); addClick(b, 0, 0.7f, 2500, 0.01f); finalize(b, 0.4f); put(SFX_TICK, std::move(b)); }
    { Buf b(0.25f); addClick(b, 0, 0.8f, 2400, 0.02f); addClick(b, 0.1f, 0.7f, 3200, 0.02f); finalize(b, 0.5f); put(SFX_PICKUP, std::move(b)); }
    { Buf b(3.0f); float ch[5] = {523.25f, 659.25f, 783.99f, 1046.5f, 1318.5f};
      for (int k = 0; k < 5; k++) addSaw(b, k * 0.12f, 2.6f, 0.28f, ch[k], 1.0f, 3500, 0.02f);
      reverb(b, 0.7f, 0.6f); finalize(b, 0.7f); put(SFX_MATCH_WIN, std::move(b)); }

    for (int i = 0; i < SFX_COUNT; i++) {
        if (!bufs[i]) { Buf b(0.05f); bufs[i] = new Buf(b); }
        base[i] = toSound(*bufs[i]);
        for (int v = 0; v < VOICES; v++) voices[i][v] = LoadSoundAlias(base[i]);
        delete bufs[i];
    }
}

void Audio::shutdown() {
    if (!ok) return;
    for (int i = 0; i < SFX_COUNT; i++) {
        for (int v = 0; v < VOICES; v++) UnloadSoundAlias(voices[i][v]);
        UnloadSound(base[i]);
    }
    CloseAudioDevice();
    ok = false;
}

float Audio::busVol(SfxBus b) const {
    switch (b) {
    case BUS_FOOTSTEPS: return g_settings.volEffects * g_settings.volFootsteps;
    case BUS_UI: return g_settings.volUI;
    case BUS_ANNOUNCER: return g_settings.volAnnouncer;
    default: return g_settings.volEffects;
    }
}

void Audio::play2D(Sfx s, float vol, float pitch, SfxBus bus) {
    if (!ok) return;
    Sound& snd = voices[s][next[s]];
    next[s] = (next[s] + 1) % VOICES;
    SetSoundVolume(snd, clampf(vol * busVol(bus), 0, 1));
    SetSoundPitch(snd, pitch);
    SetSoundPan(snd, 0.5f);
    PlaySound(snd);
}

void Audio::play3D(Sfx s, Vector3 pos, float vol, float refDist, float maxDist, SfxBus bus, float pitchVar) {
    if (!ok) return;
    Vector3 d = pos - lpos;
    float dist = len3(d);
    if (dist > maxDist) return;
    float gain = dist <= refDist ? 1.0f : refDist / (refDist + 1.4f * (dist - refDist));
    gain *= clampf(1.0f - (dist - maxDist * 0.7f) / (maxDist * 0.3f), 0, 1);
    Vector3 dir = dist > 0.01f ? d * (1.0f / dist) : V3(0, 0, 0);
    float side = dot3(dir, lright);
    float front = dot3(dir, lfwd);
    if (front < 0) gain *= 1.0f + front * 0.18f;          // slightly duller behind
    if (map && dist > 2.0f && !map->lineOfSight(lpos, pos + V3(0, 0.3f, 0))) gain *= 0.42f;   // occlusion
    float width = g_settings.hrtfLike ? 0.95f : 0.65f;
    float pan = 0.5f - 0.5f * side * width;
    Sound& snd = voices[s][next[s]];
    next[s] = (next[s] + 1) % VOICES;
    SetSoundVolume(snd, clampf(vol * gain * busVol(bus), 0, 1));
    SetSoundPitch(snd, 1.0f + rng.range(-pitchVar, pitchVar));
    SetSoundPan(snd, clampf(pan, 0, 1));
    PlaySound(snd);
}

Sfx Audio::shotSfx(WeaponId w) {
    switch (weaponDef(w).sound) {
    case SK_PISTOL: return SFX_SHOT_PISTOL;
    case SK_HEAVYPISTOL: return SFX_SHOT_HEAVY;
    case SK_SMG: return SFX_SHOT_SMG;
    case SK_SHOTGUN: return SFX_SHOT_SHOTGUN;
    case SK_RIFLE: return SFX_SHOT_RIFLE;
    case SK_RIFLE2: return SFX_SHOT_RIFLE2;
    case SK_SNIPER: return SFX_SHOT_SNIPER;
    case SK_MARKSMAN: return SFX_SHOT_MARKSMAN;
    case SK_LMG: return SFX_SHOT_LMG;
    default: return SFX_SHOT_PISTOL;
    }
}

void Audio::playShot(WeaponId w, Vector3 pos, bool local) {
    if (local) { play2D(shotSfx(w), 0.55f, 1.0f + rng.range(-0.02f, 0.02f)); return; }
    float d = len3(pos - lpos);
    if (d < 45) play3D(shotSfx(w), pos, 0.9f, 6.0f, 90.0f);
    if (d > 20) play3D(SFX_SHOT_DISTANT, pos, 0.75f, 30.0f, 160.0f);
}

void Audio::stopAll() {
    if (!ok) return;
    for (int i = 0; i < SFX_COUNT; i++) for (int v = 0; v < VOICES; v++) StopSound(voices[i][v]);
}

void Audio::update() {
    if (!ok) return;
    float master = g_settings.volMaster;
    if (g_settings.muteWhenUnfocused && !IsWindowFocused()) master = 0;
    SetMasterVolume(master);
}

} // namespace ax
