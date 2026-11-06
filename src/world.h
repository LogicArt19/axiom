// world.h : authoritative game simulation (fixed 128 tick)
#pragma once
#include "common.h"
#include "map.h"
#include "weapons.h"

namespace ax {

enum Buttons : uint32_t {
    IN_ATTACK = 1u << 0, IN_ATTACK2 = 1u << 1, IN_JUMP = 1u << 2, IN_DUCK = 1u << 3, IN_WALK = 1u << 4,
    IN_RELOAD = 1u << 5, IN_USE = 1u << 6, IN_INSPECT = 1u << 7, IN_DROP = 1u << 8
};

struct UserCmd {
    uint32_t tick = 0;
    float yaw = 0, pitch = 0;
    float fwd = 0, side = 0;        // -1..1
    uint32_t buttons = 0;
    uint8_t select = 0;             // WeaponId to switch to (0 = none)
    int16_t lagTicks = 0;           // for lag compensation (server side)
    uint32_t viewTick = 0;          // server tick the client was looking at (network)
};

enum HitGroup : uint8_t { HG_NONE, HG_HEAD, HG_CHEST, HG_LEGS };

enum GameMode : uint8_t { MODE_COMPETITIVE, MODE_DEATHMATCH, MODE_PRACTICE, MODE_AIM };
enum Phase : uint8_t { PH_WARMUP, PH_FREEZE, PH_LIVE, PH_ROUND_END, PH_MATCH_END };
enum RoundEndReason : uint8_t { RE_NONE, RE_ELIMINATION, RE_BOMB_EXPLODED, RE_BOMB_DEFUSED, RE_TIME };

enum EvType : uint8_t {
    EV_SHOT, EV_IMPACT, EV_HIT, EV_KILL, EV_FOOTSTEP, EV_JUMP, EV_LAND, EV_RELOAD, EV_DRAW,
    EV_NADE_THROW, EV_NADE_BOUNCE, EV_FLASH_POP, EV_SMOKE_POP, EV_FRAG_BOOM, EV_MOLLY_POP, EV_FIRE_OUT,
    EV_BOMB_PLANTED, EV_BOMB_DEFUSED, EV_BOMB_EXPLODE, EV_BOMB_BEEP, EV_PLANT_START, EV_DEFUSE_START,
    EV_ROUND_START, EV_ROUND_END, EV_BUY, EV_PICKUP, EV_KNIFE_SWING, EV_KNIFE_HIT, EV_DRYFIRE,
    EV_HALFTIME, EV_MATCH_END, EV_SCOPE, EV_FLASHED, EV_BOMB_PICKUP, EV_BOMB_DROP, EV_LIVE, EV_RESPAWN, EV_HURT_FIRE,
    EV_COUNT
};

struct GameEvent {
    EvType type;
    int8_t a = -1, b = -1;          // usually actor / target player
    uint8_t weapon = 0;
    uint8_t flags = 0;              // EV_KILL: 1 headshot 2 wallbang 4 noscope 8 through smoke 16 blind ; EV_HIT: 1 headshot 2 armor
    int16_t value = 0;
    Vector3 pos{}, pos2{};
};

struct DroppedWeapon {
    bool active = false;
    WeaponId w = W_NONE;
    int clip = 0, reserve = 0;
    Vector3 pos{}, vel{};
    float spawnTime = 0;
    bool resting = false;
};

struct Projectile {
    bool active = false;
    WeaponId type = W_NONE;
    int owner = -1;
    Team team = TEAM_NONE;
    Vector3 pos{}, vel{}, prevPos{};
    float spawnTime = 0;
    bool resting = false;
    float lastBounceTime = -1;
};

struct SmokeCloud { bool active = false; Vector3 pos{}; float start = 0, end = 0; };
struct FireArea { bool active = false; Vector3 pos{}; float start = 0, end = 0, radius = 3.0f; int owner = -1; Team team = TEAM_NONE; };

enum BombState : uint8_t { BOMB_NONE, BOMB_CARRIED, BOMB_DROPPED, BOMB_PLANTED, BOMB_DEFUSED, BOMB_EXPLODED };
struct Bomb {
    BombState state = BOMB_NONE;
    int carrier = -1;
    Vector3 pos{};
    float plantTime = 0, explodeTime = 0;
    float nextBeep = 0;
    int defuser = -1;
    float defuseStart = 0, defuseEnd = 0;
    uint8_t site = 0;
};

struct PlayerHistory { Vector3 pos; float duck; float yaw; bool alive; };
constexpr int HISTORY = 64;

struct Player {
    // identity
    bool active = false;
    bool isBot = false;
    bool isRemote = false;          // controlled by a network client
    char name[24] = "";
    int id = 0;
    int squad = 0;                  // fixed group (0/1); side changes at halftime
    Team team = TEAM_NONE;
    int botSkill = 2;
    int ping = 0;

    // physical state
    bool alive = false;
    Vector3 pos{}, vel{}, prevPos{};
    float yaw = 0, pitch = 0;
    bool onGround = true;
    float duck = 0;                 // 0 stand .. 1 crouched
    float prevDuck = 0;
    bool jumpHeld = false;
    float stamina = 0;
    float lastLandTime = -10;
    float stepAccum = 0;
    float tagUntil = 0, tagAmount = 0;
    float airTime = 0;

    // combat
    int health = 100, armor = 0;
    bool helmet = false, kit = false;
    int money = 800;
    WeaponId primary = W_NONE, secondary = W_VIPER;
    int clip[2] = {0, 0}, reserve[2] = {0, 0};
    uint8_t nades[4] = {0, 0, 0, 0};   // flash, smoke, frag, molly
    bool hasBomb = false;
    WeaponId activeW = W_KNIFE, lastW = W_KNIFE;
    float nextAttack = 0, drawEnd = 0, reloadEnd = 0;
    bool reloading = false;
    int shotsFired = 0;
    float spreadAccum = 0;
    float punchYaw = 0, punchPitch = 0;
    float lastShotTime = -10;
    int scope = 0, scopeResume = 0;
    float burnAccum = 0;
    WeaponId dmPrimary = W_VANGUARD, dmSecondary = W_HORNET;
    bool attackHeld = false, attack2Held = false, useHeld = false, reloadHeld = false, dropHeld = false;
    bool pinPulled = false;
    uint8_t throwStrength = 0;      // bitmask attack/attack2 held while cooking
    bool planting = false;
    float plantStart = 0;
    bool defusing = false;
    float flashEnd = 0, flashDur = 0;
    float respawnTime = 0;
    float lastHurtTime = -10;
    Vector3 lastHurtFrom{};
    float inspectUntil = 0;
    float spawnProtectUntil = 0;
    std::array<bool, 20> boughtThisRound{};

    // per-round & match stats
    int kills = 0, deaths = 0, assists = 0, headshots = 0, damage = 0, mvps = 0, score = 0;
    int roundKills = 0, shotsFiredTotal = 0, shotsHitTotal = 0, plants = 0, defuses = 0, roundsSurvived = 0;
    int dmgTo[MAX_PLAYERS] = {0};
    int firstKills = 0;

    // lag compensation
    PlayerHistory hist[HISTORY];

    float height() const { return lerpf(STAND_HEIGHT, CROUCH_HEIGHT, duck); }
    float eyeHeight() const { return lerpf(STAND_EYE, CROUCH_EYE, duck); }
    Vector3 eye() const { return pos + V3(0, eyeHeight(), 0); }
    int slotOf(WeaponId w) const { return weaponDef(w).slot == SLOT_PRIMARY ? 0 : 1; }
    bool hasWeapon(WeaponId w) const;
    int nadeCount() const { return nades[0] + nades[1] + nades[2] + nades[3]; }
};

enum BuyItem : uint8_t {
    BUY_KEVLAR = W_COUNT + 1, BUY_HELMET, BUY_KIT
};

struct RoundRecord { Team winner; RoundEndReason reason; int winnerSquad; };

class World {
public:
    Map map;
    GameMode mode = MODE_COMPETITIVE;
    Player players[MAX_PLAYERS];
    std::vector<DroppedWeapon> drops;
    std::vector<Projectile> projectiles;
    std::vector<SmokeCloud> smokes;
    std::vector<FireArea> fires;
    Bomb bomb;
    std::vector<GameEvent> events;     // produced this tick(s); consumers clear
    bool collectEvents = true;

    // match state
    uint32_t tick = 0;
    float time = 0;
    Phase phase = PH_WARMUP;
    float phaseEnd = 0;
    float roundStartTime = 0;          // when live phase begins
    float buyEndTime = 0;
    int round = 0;                     // 1-based
    int squadScore[2] = {0, 0};
    Team squadSide[2] = {TEAM_DEF, TEAM_ATK};
    int lossStreak[2] = {0, 0};
    bool plantedThisRound = false;
    Team lastWinner = TEAM_NONE;
    RoundEndReason lastReason = RE_NONE;
    int lastMvp = -1;
    std::vector<RoundRecord> history;
    bool overtime = false;
    int otBaseScore = 0;
    int matchWinnerSquad = -1;
    bool firstBloodDone = false;
    int plantedBy = -1;
    Vector3 lastShotEnd{};
    bool passiveBots = false;
    bool clientSide = false;           // prediction-only copy on a network client

    // rules (copied from settings on start)
    int roundsToWin = 13;
    float roundTimeSec = 100, buyTimeSec = 20, freezeTimeSec = 10, bombTimeSec = 40;
    int startMoney = 800;
    bool friendlyFire = false;
    bool infiniteMoney = false, infiniteAmmo = false;
    bool headshotOnly = false;
    float dmEndTime = 0;
    int dmKillLimit = 40;

    Rng rng{1337};

    void init(GameMode m);
    int addPlayer(const char* name, bool bot, int squad, int skill);
    void removePlayer(int id);
    void startMatch();
    void step(const UserCmd* cmds);    // cmds indexed by player id (inactive ignored)

    // queries
    bool isEnemy(int a, int b) const;
    int aliveCount(Team t) const;
    int teamCount(Team t) const;
    bool canBuy(int pid) const;
    bool tryBuy(int pid, int item);
    bool trySell(int pid, int item);
    float roundTimeLeft() const;
    bool smokeBlocks(Vector3 a, Vector3 b) const;
    bool canSee(int viewer, int target, bool useFov) const;
    bool traceHitPlayer(Vector3 o, Vector3 d, float maxDist, int ignore, int& who, HitGroup& hg, float& t, int rewindTicks) const;
    Vector3 hitboxCenter(const Player& p, HitGroup g) const;
    int localSquadScore(int squad) const { return squadScore[squad]; }
    Team sideOfSquad(int s) const { return squadSide[s]; }
    int squadOfTeam(Team t) const { return squadSide[0] == t ? 0 : 1; }
    bool isHalftimeNext() const;
    int maxRounds() const { return roundsToWin * 2 - 2; }

    // helpers used by bots / client prediction
    void movePlayer(Player& p, const UserCmd& c, bool frozen);
    float playerMaxSpeed(const Player& p, const UserCmd& c) const;
    float currentInaccuracy(const Player& p) const;
    void respawnPlayer(Player& p, bool keepGear);
    void giveDefaultLoadout(Player& p);
    void giveWeapon(Player& p, WeaponId w);
    void predictPlayer(int id, const UserCmd& c) { tickPlayer(players[id], c); }
    void predictGrenade(const Player& p, float strength, std::vector<Vector3>& out) const;

private:
    void emit(const GameEvent& e) { if (collectEvents) events.push_back(e); }
    void tickPlayer(Player& p, const UserCmd& c);
    void weaponThink(Player& p, const UserCmd& c);
    void fireWeapon(Player& p, const UserCmd& c);
    void knifeAttack(Player& p, bool heavy);
    void traceBullet(Player& shooter, Vector3 o, Vector3 d, const WeaponDef& wd, float dmgScale, int rewind, bool firstPellet);
    void applyDamage(Player& victim, int attacker, float dmg, HitGroup hg, WeaponId w, float armorPen, Vector3 from, uint8_t extraFlags);
    void killPlayer(Player& victim, int attacker, WeaponId w, uint8_t flags);
    void dropWeapon(Player& p, WeaponId w, bool toss);
    void dropBomb(Player& p, bool toss);
    void throwGrenade(Player& p, float strength);
    void tickProjectiles();
    void detonate(Projectile& pr);
    void tickFires();
    void tickBomb(const UserCmd* cmds);
    void tickDrops();
    void tickRound();
    void startRound();
    void endRound(Team winner, RoundEndReason reason);
    void halftimeSwap();
    void recordHistory();
    void pickSpawn(Player& p);
    void selectBestWeapon(Player& p);
};

const char* reasonText(RoundEndReason r, Team winner);

} // namespace ax
