// audio.h : procedural sound synthesis + positional 3D playback
#pragma once
#include "common.h"
#include "weapons.h"

namespace ax {

class Map;

enum Sfx : int {
    SFX_SHOT_PISTOL, SFX_SHOT_HEAVY, SFX_SHOT_SMG, SFX_SHOT_SHOTGUN, SFX_SHOT_RIFLE, SFX_SHOT_RIFLE2,
    SFX_SHOT_SNIPER, SFX_SHOT_MARKSMAN, SFX_SHOT_LMG, SFX_SHOT_DISTANT,
    SFX_STEP0, SFX_STEP1, SFX_STEP2, SFX_STEP3, SFX_LAND, SFX_JUMP,
    SFX_RELOAD, SFX_DRAW, SFX_DRYFIRE, SFX_SCOPE,
    SFX_HIT_BODY, SFX_HIT_HEAD, SFX_HIT_HELMET, SFX_KILL, SFX_HURT,
    SFX_IMPACT_CONCRETE, SFX_IMPACT_WOOD, SFX_IMPACT_METAL, SFX_WHIZ,
    SFX_KNIFE_SWING, SFX_KNIFE_HIT,
    SFX_NADE_PIN, SFX_NADE_BOUNCE, SFX_FLASHBANG, SFX_EAR_RING, SFX_FRAG, SFX_SMOKE, SFX_FIRE, SFX_FIRE_OUT,
    SFX_BOMB_BEEP, SFX_PLANTING, SFX_DEFUSING, SFX_BOMB_EXPLODE, SFX_BOMB_PLANTED,
    SFX_UI_HOVER, SFX_UI_CLICK, SFX_UI_BUY, SFX_UI_DENY,
    SFX_ROUND_START, SFX_ROUND_WIN, SFX_ROUND_LOSE, SFX_TICK, SFX_PICKUP, SFX_MATCH_WIN,
    SFX_COUNT
};

enum SfxBus { BUS_EFFECTS, BUS_FOOTSTEPS, BUS_UI, BUS_ANNOUNCER };

class Audio {
public:
    bool ok = false;
    void init();
    void shutdown();
    void setListener(Vector3 pos, Vector3 fwd, Vector3 right) { lpos = pos; lfwd = fwd; lright = right; }
    void setMap(const Map* m) { map = m; }
    void play2D(Sfx s, float vol = 1.0f, float pitch = 1.0f, SfxBus bus = BUS_EFFECTS);
    void play3D(Sfx s, Vector3 pos, float vol = 1.0f, float refDist = 4.0f, float maxDist = 60.0f, SfxBus bus = BUS_EFFECTS, float pitchVar = 0.04f);
    void playShot(WeaponId w, Vector3 pos, bool local);
    void stopAll();
    void update();
    static Sfx shotSfx(WeaponId w);
private:
    static constexpr int VOICES = 8;
    Sound base[SFX_COUNT]{};
    Sound voices[SFX_COUNT][VOICES]{};
    int next[SFX_COUNT]{};
    Vector3 lpos{}, lfwd{0, 0, -1}, lright{1, 0, 0};
    const Map* map = nullptr;
    Rng rng{99};
    float busVol(SfxBus b) const;
};

extern Audio g_audio;

} // namespace ax
