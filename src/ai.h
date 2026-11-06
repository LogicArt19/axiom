// ai.h : computer opponents/allies. They only use Commands, exactly like a human,
// and only know about enemy structures they have actually seen.
#pragma once
#include "sim.h"

namespace ax {

struct AIPlayer {
    int pid = -1;
    float nextMacro = 0, nextArmy = 0, nextHero = 0;
    bool attacking = false, retreating = false;
    Vector2 armyGoal{};
    bool heroResting = false;
    int itemIdx = 0;
    float lastBuildOrder = -100, lastExpandOrder = -100;
    uint16_t campTarget = 0;
    Rng rng{99};
};

class AISystem {
public:
    AIPlayer ai[MAX_PLAYERS];
    std::vector<uint16_t> known[2];      // enemy structures each team has seen
    void reset(World& w);
    void think(World& w);
private:
    float nextMemory = 0;
    void remember(World& w);
    void macro(World& w, AIPlayer& a);
    void army(World& w, AIPlayer& a);
    void hero(World& w, AIPlayer& a);
    bool findSpot(World& w, int pid, UnitType t, Vector2 near, Vector2& out, Rng& rng);
    uint16_t freeDrone(World& w, int pid, Vector2 near);
    Vector2 rallyPoint(World& w, int team);
    int freeExpansion(World& w, int team);
};

} // namespace ax
