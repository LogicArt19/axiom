#include "audio.h"
#include "config.h"

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
    Buf* bufs[SFX_COUNT] = {nullptr};
    auto put = [&](Sfx id, Buf b) { bufs[id] = new Buf(std::move(b)); };
    { Buf b(0.22f); addNoise(b, 0, 0.12f, 0.8f, 3000, 900, 30, 400); addClick(b, 0.05f, 0.9f, 2200, 0.03f); addSine(b, 0.05f, 0.1f, 0.4f, 300, 180, 30); finalize(b, 0.6f, 1.6f); put(SFX_ATTACK_MELEE, std::move(b)); }
    { Buf b(0.25f); addSine(b, 0, 0.2f, 0.7f, 1800, 500, 18); addNoise(b, 0, 0.05f, 0.4f, 6000, 3000, 60, 1500); finalize(b, 0.5f, 1.4f); put(SFX_ATTACK_RANGED, std::move(b)); }
    { Buf b(0.35f); addSine(b, 0, 0.3f, 0.7f, 900, 260, 10); addNoise(b, 0, 0.08f, 0.6f, 5000, 1500, 40, 800); addSine(b, 0, 0.12f, 0.6f, 120, 60, 25); finalize(b, 0.6f, 2.0f); put(SFX_ATTACK_HERO, std::move(b)); }
    { Buf b(0.9f); addNoise(b, 0, 0.8f, 1.0f, 2000, 150, 5, 0); addSine(b, 0, 0.4f, 1.0f, 70, 35, 8, 0.001f); reverb(b, 0.4f, 0.5f); finalize(b, 0.8f, 2.5f); put(SFX_ATTACK_SIEGE, std::move(b)); }
    { Buf b(0.5f); addSaw(b, 0, 0.25f, 0.6f, 220, 12, 2400, 0.002f); addSine(b, 0, 0.3f, 0.7f, 1400, 300, 12); addNoise(b, 0, 0.1f, 0.5f, 7000, 2000, 30, 1000); finalize(b, 0.7f, 2.0f); put(SFX_ATTACK_TOWER, std::move(b)); }
    { Buf b(0.12f); addNoise(b, 0, 0.1f, 1.0f, 1800, 400, 45); addSine(b, 0, 0.06f, 0.5f, 160, 90, 50); finalize(b, 0.5f, 1.8f); put(SFX_HIT, std::move(b)); }
    { Buf b(0.4f); addSine(b, 0, 0.35f, 0.4f, 880, 1320, 6, 0.02f); addSine(b, 0, 0.35f, 0.3f, 1320, 1760, 7, 0.02f); finalize(b, 0.35f); put(SFX_HEAL_BEAM, std::move(b)); }
    { Buf b(0.5f); addNoise(b, 0, 0.45f, 1.0f, 1500, 200, 7); addSine(b, 0, 0.25f, 0.8f, 110, 50, 12); finalize(b, 0.65f, 2.0f); put(SFX_DEATH_UNIT, std::move(b)); }
    { Buf b(2.4f); addNoise(b, 0, 0.03f, 2.0f, 9000, 4000, 30, 500); addNoise(b, 0, 2.3f, 1.2f, 2400, 80, 1.8f); addSine(b, 0, 1.2f, 1.2f, 50, 25, 2.5f, 0.002f);
      for (int k = 0; k < 14; k++) addClick(b, 0.2f + k * 0.11f, 0.5f, 900 + k * 60.f, 0.05f); reverb(b, 0.6f, 0.6f); finalize(b, 0.95f, 3.0f); put(SFX_DEATH_BUILDING, std::move(b)); }
    { Buf b(0.8f); addNoise(b, 0, 0.7f, 1.0f, 3000, 500, 4, 300); addSine(b, 0, 0.5f, 0.6f, 200, 90, 6); reverb(b, 0.4f, 0.4f); finalize(b, 0.7f, 2.0f); put(SFX_FIREBALL, std::move(b)); }
    { Buf b(0.45f); addSine(b, 0, 0.4f, 0.6f, 400, 2400, 6, 0.005f); addNoise(b, 0, 0.3f, 0.4f, 8000, 3000, 10, 2000); finalize(b, 0.55f); put(SFX_BLINK, std::move(b)); }
    { Buf b(1.0f); float ch[3] = {784, 988, 1175}; for (int k = 0; k < 3; k++) addSine(b, k * 0.08f, 0.9f, 0.4f, ch[k], ch[k], 3.5f, 0.02f); reverb(b, 0.5f, 0.5f); finalize(b, 0.55f); put(SFX_HEAL, std::move(b)); }
    { Buf b(0.9f); addSaw(b, 0, 0.8f, 0.4f, 330, 3, 3000, 0.05f); addSaw(b, 0.05f, 0.8f, 0.3f, 495, 3, 3000, 0.05f); finalize(b, 0.5f); put(SFX_BUFF, std::move(b)); }
    { Buf b(0.9f); addNoise(b, 0, 0.8f, 1.2f, 1200, 100, 5); addSine(b, 0, 0.5f, 1.2f, 60, 30, 6, 0.001f); reverb(b, 0.5f, 0.5f); finalize(b, 0.85f, 3.0f); put(SFX_SLAM, std::move(b)); }
    { Buf b(2.6f); addNoise(b, 0, 0.03f, 2.0f, 10000, 5000, 30, 500); addNoise(b, 0, 2.5f, 1.3f, 3500, 90, 2.0f); addSine(b, 0, 1.4f, 1.3f, 45, 22, 2.4f, 0.002f); reverb(b, 0.7f, 0.65f); finalize(b, 1.0f, 3.5f); put(SFX_METEOR, std::move(b)); }
    { Buf b(1.0f); addSine(b, 0, 0.9f, 0.5f, 150, 600, 3, 0.05f); addNoise(b, 0, 0.6f, 0.4f, 4000, 1000, 4, 600); reverb(b, 0.5f, 0.5f); finalize(b, 0.6f); put(SFX_SUMMON, std::move(b)); }
    { Buf b(1.2f); float ch[4] = {523, 659, 784, 1047}; for (int k = 0; k < 4; k++) addSaw(b, k * 0.09f, 1.0f, 0.3f, ch[k], 2.0f, 3500, 0.01f); reverb(b, 0.5f, 0.5f); finalize(b, 0.6f); put(SFX_LEVELUP, std::move(b)); }
    { Buf b(0.35f); addSine(b, 0, 0.12f, 0.6f, 660, 660, 12); addSine(b, 0.1f, 0.2f, 0.6f, 990, 990, 12); finalize(b, 0.45f); put(SFX_UNIT_READY, std::move(b)); }
    { Buf b(0.9f); addSaw(b, 0, 0.3f, 0.4f, 392, 4, 2500); addSaw(b, 0.2f, 0.6f, 0.4f, 587, 3, 2500); reverb(b, 0.4f, 0.5f); finalize(b, 0.55f); put(SFX_BUILD_DONE, std::move(b)); }
    { Buf b(0.6f); for (int k = 0; k < 3; k++) addClick(b, k * 0.17f, 0.9f, 1600 + k * 200.f, 0.04f); finalize(b, 0.55f); put(SFX_BUILD_START, std::move(b)); }
    { Buf b(0.35f); addSine(b, 0, 0.3f, 0.5f, 1568, 1568, 12); addSine(b, 0.06f, 0.28f, 0.4f, 2093, 2093, 12); addClick(b, 0, 0.3f, 6000, 0.01f); finalize(b, 0.5f); put(SFX_COIN, std::move(b)); }
    { Buf b(0.8f); for (int k = 0; k < 3; k++) addSquare(b, k * 0.22f, 0.15f, 0.5f, 880, 8); finalize(b, 0.5f); put(SFX_ALERT, std::move(b)); }
    { Buf b(1.4f); addSaw(b, 0, 1.3f, 0.5f, 146, 1.2f, 1200, 0.15f); addSaw(b, 0, 1.3f, 0.35f, 220, 1.2f, 1500, 0.15f); reverb(b, 0.6f, 0.6f); finalize(b, 0.55f); put(SFX_WAVE, std::move(b)); }
    { Buf b(2.0f); addSaw(b, 0, 1.8f, 0.6f, 70, 1.5f, 700, 0.1f); addNoise(b, 0, 1.8f, 0.7f, 900, 200, 1.8f); reverb(b, 0.6f, 0.6f); finalize(b, 0.8f, 2.0f); put(SFX_TITAN, std::move(b)); }
    { Buf b(1.0f); addSaw(b, 0, 0.15f, 0.5f, 440, 4, 3000); addSaw(b, 0.15f, 0.8f, 0.5f, 660, 3, 3000); reverb(b, 0.5f, 0.5f); finalize(b, 0.6f); put(SFX_HERO_KILL, std::move(b)); }
    { Buf b(3.0f); float ch[5] = {523.25f, 659.25f, 783.99f, 1046.5f, 1318.5f}; for (int k = 0; k < 5; k++) addSaw(b, k * 0.12f, 2.6f, 0.28f, ch[k], 1.0f, 3500, 0.02f); reverb(b, 0.7f, 0.6f); finalize(b, 0.7f); put(SFX_VICTORY, std::move(b)); }
    { Buf b(3.0f); float ch[4] = {440.f, 349.23f, 293.66f, 220.f}; for (int k = 0; k < 4; k++) addSaw(b, k * 0.25f, 2.2f, 0.32f, ch[k], 1.2f, 1500, 0.03f); reverb(b, 0.7f, 0.6f); finalize(b, 0.65f); put(SFX_DEFEAT, std::move(b)); }
    { Buf b(0.08f); addSine(b, 0, 0.08f, 0.5f, 1200, 1600, 40); finalize(b, 0.35f); put(SFX_SELECT, std::move(b)); }
    { Buf b(0.12f); addSine(b, 0, 0.05f, 0.5f, 900, 900, 50); addSine(b, 0.05f, 0.07f, 0.5f, 1350, 1350, 50); finalize(b, 0.35f); put(SFX_ORDER, std::move(b)); }
    { Buf b(0.15f); addClick(b, 0, 0.7f, 3500, 0.02f); addSine(b, 0, 0.12f, 0.3f, 2600, 2400, 30); finalize(b, 0.4f); put(SFX_GATHER, std::move(b)); }
    { Buf b(0.7f); addNoise(b, 0, 0.6f, 1.0f, 500, 4000, 4, 300); for (size_t i = 0; i < b.n(); i++) b.s[i] *= sinf((float)i / b.n() * 3.14159f); finalize(b, 0.45f); put(SFX_STEALTH, std::move(b)); }
    { Buf b(0.03f); addSine(b, 0, 0.03f, 0.5f, 2400, 2600, 90); finalize(b, 0.25f); put(SFX_UI_HOVER, std::move(b)); }
    { Buf b(0.08f); addSine(b, 0, 0.08f, 0.6f, 1300, 700, 40); addClick(b, 0, 0.3f, 4000, 0.008f); finalize(b, 0.45f); put(SFX_UI_CLICK, std::move(b)); }
    { Buf b(0.35f); addSine(b, 0, 0.1f, 0.5f, 1046, 1046, 15); addSine(b, 0.08f, 0.25f, 0.5f, 1568, 1568, 12); finalize(b, 0.5f); put(SFX_UI_BUY, std::move(b)); }
    { Buf b(0.25f); addSquare(b, 0, 0.1f, 0.4f, 220, 15); addSquare(b, 0.11f, 0.12f, 0.4f, 185, 15); finalize(b, 0.45f); put(SFX_UI_DENY, std::move(b)); }
    for (int i = 0; i < SFX_COUNT; i++) {
        if (!bufs[i]) { Buf b(0.05f); bufs[i] = new Buf(b); }
        base[i] = toSound(*bufs[i]);
        for (int v = 0; v < VOICES; v++) voices[i][v] = LoadSoundAlias(base[i]);
        delete bufs[i];
    }
}

void Audio::shutdown() {
    if (!ok) return;
    for (int i = 0; i < SFX_COUNT; i++) { for (int v = 0; v < VOICES; v++) UnloadSoundAlias(voices[i][v]); UnloadSound(base[i]); }
    CloseAudioDevice();
    ok = false;
}

float Audio::busVol(SfxBus b) const {
    switch (b) {
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

void Audio::playAt(Sfx s, Vector2 pos, float vol, float pitchVar) {
    if (!ok) return;
    float d = dist2(pos, lc);
    float g = clampf(1.0f - (d - lr * 0.6f) / (lr * 1.2f), 0, 1);
    if (g <= 0.02f) return;
    double now = GetTime();
    if (now - lastPlay[s] < 0.03) return;    // avoid stacking dozens of identical sounds
    lastPlay[s] = (float)now;
    float pan = 0.5f - 0.45f * clampf((pos.x - lc.x) / (lr + 1), -1, 1);
    Sound& snd = voices[s][next[s]];
    next[s] = (next[s] + 1) % VOICES;
    SetSoundVolume(snd, clampf(vol * g * busVol(BUS_EFFECTS), 0, 1));
    SetSoundPitch(snd, 1.0f + rng.range(-pitchVar, pitchVar));
    SetSoundPan(snd, pan);
    PlaySound(snd);
}

void Audio::stopAll() { if (!ok) return; for (int i = 0; i < SFX_COUNT; i++) for (int v = 0; v < VOICES; v++) StopSound(voices[i][v]); }

void Audio::update() {
    if (!ok) return;
    float master = g_settings.volMaster;
    if (g_settings.muteWhenUnfocused && !IsWindowFocused()) master = 0;
    SetMasterVolume(master);
}

} // namespace ax
