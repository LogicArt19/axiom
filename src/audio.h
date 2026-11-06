// audio.h : procedural sound synthesis + positional playback
#pragma once
#include "common.h"

namespace ax {

enum Sfx : int {
    SFX_ATTACK_MELEE, SFX_ATTACK_RANGED, SFX_ATTACK_HERO, SFX_ATTACK_SIEGE, SFX_ATTACK_TOWER, SFX_HIT, SFX_HEAL_BEAM,
    SFX_DEATH_UNIT, SFX_DEATH_BUILDING, SFX_FIREBALL, SFX_BLINK, SFX_HEAL, SFX_BUFF, SFX_SLAM, SFX_METEOR, SFX_SUMMON,
    SFX_LEVELUP, SFX_UNIT_READY, SFX_BUILD_DONE, SFX_BUILD_START, SFX_COIN, SFX_ALERT, SFX_WAVE, SFX_TITAN, SFX_HERO_KILL,
    SFX_VICTORY, SFX_DEFEAT, SFX_SELECT, SFX_ORDER, SFX_GATHER, SFX_STEALTH,
    SFX_UI_HOVER, SFX_UI_CLICK, SFX_UI_BUY, SFX_UI_DENY,
    SFX_COUNT
};
enum SfxBus { BUS_EFFECTS, BUS_UI, BUS_ANNOUNCER };

class Audio {
public:
    bool ok = false;
    void init();
    void shutdown();
    void setListener(Vector2 center, float viewRadius) { lc = center; lr = viewRadius; }
    void play2D(Sfx s, float vol = 1.0f, float pitch = 1.0f, SfxBus bus = BUS_EFFECTS);
    void playAt(Sfx s, Vector2 pos, float vol = 1.0f, float pitchVar = 0.05f);
    void update();
    void stopAll();
private:
    static constexpr int VOICES = 10;
    Sound base[SFX_COUNT]{};
    Sound voices[SFX_COUNT][VOICES]{};
    int next[SFX_COUNT]{};
    float lastPlay[SFX_COUNT]{};
    Vector2 lc{100, 100};
    float lr = 40;
    Rng rng{99};
    float busVol(SfxBus b) const;
};
extern Audio g_audio;

} // namespace ax
