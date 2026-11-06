// hud.h : RTS/MOBA interface
#pragma once
#include "sim.h"

namespace ax {

struct CardButton {
    std::string label, tip;
    int hotkeyAction = -1;
    int cost = 0, flux = 0;
    bool enabled = true;
    float progress = -1;
};

struct HudInput {
    int abilityClicked = -1;
    int cardClicked = -1;
    int buyItem = 0;
    int sellSlot = -1;
    bool minimapLeft = false, minimapRight = false;
    Vector2 minimapWorld{};
    uint16_t selectOnly = 0;
    int cancelQueueIndex = -1;
    bool overUI = false;
};

struct HudMsg { std::string text; Color col; float time; };
struct FeedEntry { std::string text; Color col; float time; };

struct HudState {
    std::vector<HudMsg> msgs;
    std::vector<FeedEntry> feed;
    bool shopOpen = false;
    Texture2D minimap{};
    bool minimapReady = false;
    Image fogImg{};
    Texture2D fogTex{};
    float time = 0;
    float frameTimes[120]{};
    int ftIdx = 0;
    int ping = 0;
    std::vector<Vector2> pings;
    std::vector<float> pingTimes;
    Rectangle minimapRect{};
    void reset() { msgs.clear(); feed.clear(); shopOpen = false; pings.clear(); pingTimes.clear(); }
};

void hudInit(HudState& h, const World& w);
void hudMessage(HudState& h, const std::string& s, Color c);
void hudOnEvent(HudState& h, const World& w, const GameEvent& e, int player);
void hudDraw(HudState& h, const World& w, int player, const std::vector<uint16_t>& sel, const Camera3D& cam,
             const std::vector<CardButton>& card, HudInput& in, float dt, int apm, bool aiming);
void hudScoreboard(const World& w, int player);
bool mouseOverHud(const HudState& h);

} // namespace ax
