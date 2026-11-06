// hud.h : in-game heads-up display, buy menu, scoreboard, radar
#pragma once
#include "world.h"
#include "render.h"

namespace ax {

struct KillFeedEntry { std::string killer, victim; Team kt, vt; WeaponId w; uint8_t flags; float time; bool local; bool assistLocal; };
struct DamageIndicator { float yaw; float time; int amount; };
struct CenterMsg { std::string text, sub; Color col; float time, dur; };
struct DamageNumber { Vector3 pos; int value; bool head; float time; };

struct HudState {
    std::vector<KillFeedEntry> feed;
    std::vector<DamageIndicator> dmg;
    std::vector<DamageNumber> numbers;
    CenterMsg center{};
    float hitTime = -10; bool hitKill = false; bool hitHead = false;
    float roundBannerTime = -10; bool roundWon = false; std::string roundReason, roundMvp;
    float weaponListTime = -10;
    float frameTimes[160]{}; int ftIdx = 0;
    int pingMs = 0;
    float lossPct = 0;
    float spottedTime[MAX_PLAYERS]{};
    int buyCategory = 0;
    float flashWhite = 0;
    Texture2D radarTex{};
    bool radarReady = false;
    float time = 0;
    void reset() { feed.clear(); dmg.clear(); numbers.clear(); center = CenterMsg{}; hitTime = -10; roundBannerTime = -10; }
};

void hudBuildRadar(HudState& h, const Map& m);
void hudOnEvent(HudState& h, const World& w, const GameEvent& e, int localId);
void hudMessage(HudState& h, const char* text, const char* sub, Color c, float dur = 2.5f);
void hudDraw(HudState& h, const World& w, int localId, int viewId, const ViewState& v, float dt);
void drawCrosshair(float cx, float cy, float dynGap, float scale);
int  hudBuyMenu(HudState& h, const World& w, int localId, bool& close);   // returns item to buy (or -item-1 to sell), 0 = nothing
void hudScoreboard(const World& w, int localId);
const char* weaponShortName(WeaponId w);
const char* buyItemName(int item);

} // namespace ax
