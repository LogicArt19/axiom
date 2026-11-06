// bot.h : AI players that produce UserCmds exactly like a human would
#pragma once
#include "world.h"

namespace ax {

struct BotSkill {
    float reaction, turnSpeed, smoothing, aimError, headPref, sprayControl, burstRange;
    bool counterStrafe, useUtility, strafeInFights;
};
const BotSkill& botSkill(int level);
const char* botSkillName(int level);

enum BotTask : uint8_t { BT_IDLE, BT_GOTO, BT_HOLD, BT_PLANT, BT_DEFUSE, BT_GUARD, BT_GETBOMB, BT_ROAM, BT_PASSIVE };

struct BotBrain {
    BotTask task = BT_IDLE;
    std::vector<Vector3> path;
    size_t pathIdx = 0;
    Vector3 goal{};
    float holdYaw = 0;
    float nextRepath = 0;
    float waitUntil = 0;
    std::vector<Vector3> waypoints;   // strategic route
    size_t wpIdx = 0;

    int target = -1;
    float acquireTime = 0, reactReady = 0, lastSeen = -10;
    Vector3 lastKnown{};
    bool hasLastKnown = false;
    float lastKnownTime = -10;
    float aimYaw = 0, aimPitch = 0;
    Vector3 errOffset{};
    float nextErr = 0;
    bool aimHead = true;
    int burstCount = 0;
    float burstPauseUntil = 0;
    bool semiToggle = false;
    float strafeDir = 1, strafeSwitch = 0;
    bool crouchSpray = false;

    float stuckTimer = 0;
    Vector3 lastPos{};
    float lookSweep = 0;
    float decideAt = 0;

    int nadeState = 0;               // 0 none 1 selecting 2 cooking
    WeaponId nadeType = W_NONE;
    Vector3 nadeTarget{};
    float nadeTimer = 0;
    bool usedEntryUtil = false;
    int holdIndex = -1;
    bool initialized = false;
};

class BotSystem {
public:
    BotBrain brains[MAX_PLAYERS];
    int attackSite = 1;               // 1 = A, 2 = B
    int lastRound = -1;
    Rng rng{777};
    std::vector<Vector3> sharedSightings;   // team knowledge (enemy positions recently seen)

    void reset() { for (auto& b : brains) b = BotBrain{}; lastRound = -1; }
    void think(World& w, int id, UserCmd& out);
    void onEvent(World& w, const GameEvent& e);

private:
    void roundSetup(World& w);
    void buy(World& w, Player& p);
    void plan(World& w, Player& p, BotBrain& b);
    void setGoal(World& w, Player& p, BotBrain& b, Vector3 goal, BotTask t);
    bool followPath(World& w, Player& p, BotBrain& b, Vector3& moveDir);
    void chooseTarget(World& w, Player& p, BotBrain& b);
};

} // namespace ax
