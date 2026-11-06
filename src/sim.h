// sim.h : authoritative simulation of a match (30 ticks per second)
#pragma once
#include "data.h"
#include "map.h"

namespace ax {

enum OrderType : uint8_t { O_IDLE, O_MOVE, O_ATTACK_MOVE, O_ATTACK, O_HOLD, O_GATHER, O_RETURN, O_BUILD, O_CAST, O_LANE, O_PATROL };
struct Order { OrderType type = O_IDLE; Vector2 pt{}; uint16_t target = 0; uint8_t a = 0; };

constexpr int TEAM_NEUTRAL = 2;

struct Entity {
    uint16_t id = 0;
    bool alive = false;
    EntKind kind = K_UNIT;
    UnitType type = U_NONE;
    int8_t owner = -1;           // player index, -1 = none
    uint8_t team = TEAM_NEUTRAL;
    Vector2 pos{}, prevPos{};
    float facing = 0, radius = 0.5f;
    float hp = 1, maxHp = 1, mana = 0, maxMana = 0, shield = 0, shieldUntil = 0;
    float attackCd = 0, acquireCd = 0;
    uint16_t target = 0;
    Order order;
    std::vector<Order> queue;
    bool arrived = false;
    float stuck = 0;
    // buildings
    bool constructing = false;
    float buildProgress = 0;
    std::vector<UnitType> prod;
    float prodProgress = 0;
    Vector2 rally{};
    bool hasRally = false;
    int8_t research = -1;
    float researchProgress = 0;
    // workers
    int carry = 0;
    bool carryFlux = false;
    uint16_t node = 0;
    int amount = 0;              // resources left (crystal, geyser, extractor)
    bool rich = false;
    float gatherTimer = 0;
    float occupiedUntil = 0;     // resource node
    // status effects
    float stunUntil = 0, slowUntil = 0, slowAmt = 0, hasteUntil = 0, hasteAmt = 0, asUntil = 0, asAmt = 0;
    float dmgUntil = 0, dmgAmt = 0, vulnUntil = 0, vulnAmt = 0, stealthUntil = 0, poisonUntil = 0, poisonDps = 0;
    float regenUntil = 0, regenRate = 0, expireAt = 0, empowered = 0;
    int poisonCharges = 0;
    // hero
    int level = 1, xp = 0;
    uint8_t items[4] = {0, 0, 0, 0};
    float abilCd[4] = {0, 0, 0, 0};
    float respawnAt = 0;
    int kills = 0, deaths = 0, assists = 0, lastHits = 0;
    float lastHeroDamage[MAX_PLAYERS] = {0};
    // lanes / neutrals
    int8_t lane = -1;
    int wp = 0;
    Vector2 home{};
    // misc
    float lastHurt = -10;
    int8_t lastAttackerOwner = -1;
    float attackAnim = -10;
    bool summoned = false;
};

struct Projectile {
    bool alive = true;
    Vector2 pos{}, dir{};
    float height = 1.2f;
    uint16_t src = 0, target = 0;
    int8_t owner = -1;
    uint8_t team = 0;
    UnitType srcType = U_NONE;
    float speed = 20, dmg = 0, splash = 0;
    bool skill = false, pierce = false;
    float traveled = 0, maxDist = 0, width = 0.8f;
    uint8_t visual = 0;          // 0 bullet, 1 fireball, 2 pierce shot, 3 heal
    std::vector<uint16_t> hit;
};

enum ZoneKind : uint8_t { Z_FIRE, Z_METEOR, Z_ORBITAL, Z_SLOW };
struct Zone {
    ZoneKind kind;
    Vector2 pos{};
    float radius = 3, start = 0, end = 0, dps = 0, dmg = 0, delay = 0;
    uint8_t team = 0;
    int8_t owner = -1;
    uint16_t src = 0, target = 0;
    bool fired = false;
};

struct PlayerState {
    bool active = false, isAI = true, isRemote = false;
    char name[24] = "";
    uint8_t team = 0;
    UnitType hero = H_VANGUARD;
    uint16_t heroId = 0;
    int gold = 0, flux = 0;
    int fluxEarned = 0;
    int upgrades[UP_COUNT] = {0, 0};
    int aiSkill = 2;
    int goldEarned = 0, unitsProduced = 0, unitsKilled = 0, unitsLost = 0, actions = 0;
    int ping = 0;
    bool ready = false;
};

enum EvType : uint8_t {
    EV_ATTACK, EV_HIT, EV_DEATH, EV_ABILITY, EV_LEVELUP, EV_BUILD_DONE, EV_UNIT_DONE, EV_RESEARCH_DONE, EV_TOWER_DOWN,
    EV_HERO_KILL, EV_TITAN_SLAIN, EV_WAVE, EV_GAME_OVER, EV_NO_GOLD, EV_NO_SUPPLY, EV_ITEM, EV_UNDER_ATTACK, EV_HEAL,
    EV_BUILD_START, EV_CANT_BUILD, EV_RESPAWN, EV_GAME_START, EV_BLINK, EV_METEOR, EV_SPAWN
};
struct GameEvent {
    EvType type;
    int16_t a = -1, b = -1;      // entity ids or player ids depending on type
    uint8_t c = 0;
    int8_t player = -1;          // player concerned (for private notifications)
    Vector2 p{}, p2{};
    int16_t value = 0;
};

enum CmdType : uint8_t {
    CMD_MOVE, CMD_ATTACK_MOVE, CMD_ATTACK, CMD_STOP, CMD_HOLD, CMD_GATHER, CMD_BUILD, CMD_TRAIN, CMD_CANCEL,
    CMD_RESEARCH, CMD_ABILITY, CMD_BUY, CMD_SELL, CMD_RALLY, CMD_PICK_HERO, CMD_SMART, CMD_READY
};
struct Command {
    CmdType type = CMD_MOVE;
    uint8_t player = 0;
    bool queue = false;
    uint8_t a = 0;
    Vector2 pt{};
    uint16_t target = 0;
    std::vector<uint16_t> units;
};

enum Phase : uint8_t { PH_LOBBY, PH_PLAYING, PH_OVER };

class World {
public:
    Map map;
    std::vector<Entity> ents;     // index == id (id 0 unused)
    std::vector<Projectile> projs;
    std::vector<Zone> zones;
    PlayerState players[MAX_PLAYERS];
    std::vector<GameEvent> events;
    std::vector<Command> pending;
    uint32_t tick = 0;
    float time = 0;
    Phase phase = PH_LOBBY;
    float lobbyEnd = 0;
    int winner = -1;
    float nextWave = 20;
    int waveCount = 0;
    float titanRespawn = 0, titanBuffUntil[2] = {0, 0};
    std::vector<uint8_t> vis[2], explored[2];
    uint16_t core[2] = {0, 0};
    bool clientSide = false;
    Rng rng{1};
    float campRespawn[16] = {0};
    std::vector<std::pair<Vector2, int>> geyserRespawn;
    int mapId = 0;
    uint8_t beaconHeld[8] = {0};   // bit per team

    void setup(int playersPerTeam, int mapId = 0);
    int addPlayer(const char* name, int team, UnitType hero, bool ai, int skill);
    void start();
    void issue(const Command& c) { pending.push_back(c); }
    void step();

    Entity* get(uint16_t id) { return (id > 0 && id < ents.size() && ents[id].alive) ? &ents[id] : nullptr; }
    const Entity* get(uint16_t id) const { return (id > 0 && id < ents.size() && ents[id].alive) ? &ents[id] : nullptr; }
    bool visibleTo(int team, const Entity& e) const;
    bool cellVisible(int team, Vector2 p) const { int i = map.cellIndex(p); return i >= 0 && vis[team][i]; }
    int supplyUsed(int player) const;
    int supplyMax(int player) const;
    bool hasBuilding(int player, UnitType t) const;
    bool heroNearShop(int player) const;
    float attackDamage(const Entity& e) const;
    float attackSpeedMult(const Entity& e) const;
    float moveSpeed(const Entity& e) const;
    float armorOf(const Entity& e) const;
    float abilityPower(const Entity& e) const;
    float cdr(const Entity& e) const;
    bool abilityReady(const Entity& hero, int slot) const;
    std::vector<uint16_t> unitsInRadius(Vector2 p, float r, int teamFilter, bool enemiesOf) const;
    int countUnits(int player, UnitType t, bool includeQueued) const;
    uint16_t spawn(UnitType t, int owner, int team, Vector2 pos);
    void updateVision();
    int canBuild(UnitType t, Vector2 site, int team) const;    // -1 ok, otherwise the EV_CANT_BUILD reason
    Vector2 buildSite(UnitType t, Vector2 p) const;             // snaps (extractors snap onto vents)
    const Entity* dropOff(const Entity& worker) const;          // nearest friendly Core / Centre

private:
    std::vector<std::vector<uint16_t>> grid;   // spatial hash 8m cells
    static constexpr int GRID = 25;
    void rebuildGrid();
    void applyCommand(const Command& c);
    void tickEntity(Entity& e);
    void tickBuilding(Entity& e);
    void tickWorker(Entity& e);
    void tickHero(Entity& e);
    void moveToward(Entity& e, Vector2 goal, float stopDist);
    bool chaseAndAttack(Entity& e, Entity& t);
    void doAttack(Entity& e, Entity& t);
    uint16_t acquire(const Entity& e, float range) const;
    void damage(Entity& t, float amount, const Entity* src, bool isAbility, int srcOwnerOverride = -1);
    void heal(Entity& t, float amount);
    void kill(Entity& t, const Entity* src, int killerOwner);
    void tickProjectiles();
    void tickZones();
    void separate();
    bool castAbility(Entity& h, int slot, Vector2 pt, uint16_t target);
    void giveXp(int team, Vector2 at, int xp);
    void emit(const GameEvent& e) { events.push_back(e); }
    float inRangeDist(const Entity& a, const Entity& b) const { return dist2(a.pos, b.pos) - a.radius - b.radius; }
    void finishBuilding(Entity& e);
    Vector2 exitPoint(const Entity& b, Vector2 toward);
};

const char* playerColorName(int i);
Color playerColor(int i);
Color teamColor(int team);

} // namespace ax
