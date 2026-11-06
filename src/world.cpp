#include "world.h"
#include "config.h"
#include <ctime>

namespace ax {

static constexpr float PLANT_TIME = 3.2f;
static constexpr float DEFUSE_TIME = 10.0f, DEFUSE_TIME_KIT = 5.0f;
static constexpr float NADE_GRAVITY = 12.0f;

const char* reasonText(RoundEndReason r, Team winner) {
    switch (r) {
    case RE_ELIMINATION: return winner == TEAM_ATK ? "Les ATTAQUANTS éliminent l'équipe adverse" : "Les DÉFENSEURS éliminent l'équipe adverse";
    case RE_BOMB_EXPLODED: return "Le noyau a explosé";
    case RE_BOMB_DEFUSED: return "Le noyau a été désamorcé";
    case RE_TIME: return "Temps écoulé : les DÉFENSEURS tiennent";
    default: return "";
    }
}

bool Player::hasWeapon(WeaponId w) const {
    switch (w) {
    case W_KNIFE: return true;
    case W_FLASH: return nades[0] > 0;
    case W_SMOKE: return nades[1] > 0;
    case W_FRAG: return nades[2] > 0;
    case W_MOLLY: return nades[3] > 0;
    case W_BOMB: return hasBomb;
    case W_NONE: return false;
    default: return primary == w || secondary == w;
    }
}

static int nadeIndex(WeaponId w) { return w == W_FLASH ? 0 : w == W_SMOKE ? 1 : w == W_FRAG ? 2 : w == W_MOLLY ? 3 : -1; }
static WeaponId nadeFromIndex(int i) { static const WeaponId ids[4] = {W_FLASH, W_SMOKE, W_FRAG, W_MOLLY}; return ids[i]; }

// ---------------------------------------------------------------------------
void World::init(GameMode m) {
    mode = m;
    if (m == MODE_AIM) map.loadAimArena(); else map.loadCitadel();
    for (auto& p : players) p = Player{};
    drops.clear(); projectiles.clear(); smokes.clear(); fires.clear(); events.clear(); history.clear();
    bomb = Bomb{};
    tick = 0; time = 0; round = 0;
    squadScore[0] = squadScore[1] = 0;
    squadSide[0] = TEAM_DEF; squadSide[1] = TEAM_ATK;
    lossStreak[0] = lossStreak[1] = 0;
    overtime = false; otBaseScore = 0; matchWinnerSquad = -1;
    roundsToWin = g_settings.roundsToWin;
    roundTimeSec = (float)g_settings.roundTime;
    buyTimeSec = (float)g_settings.buyTime;
    freezeTimeSec = (float)g_settings.freezeTime;
    startMoney = g_settings.startMoney;
    friendlyFire = g_settings.friendlyFire;
    infiniteMoney = (m == MODE_PRACTICE || m == MODE_DEATHMATCH || m == MODE_AIM);
    infiniteAmmo = (m == MODE_PRACTICE || m == MODE_AIM);
    headshotOnly = (m == MODE_DEATHMATCH && g_settings.dmHeadshotOnly);
    passiveBots = (m == MODE_PRACTICE);
    rng = Rng((uint32_t)(nowSeconds() * 1e6) ^ (uint32_t)std::time(nullptr) ^ 0xA5A5A5u);
}

int World::addPlayer(const char* name, bool bot, int squad, int skill) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].active) continue;
        Player& p = players[i];
        p = Player{};
        p.active = true; p.isBot = bot; p.id = i; p.squad = squad; p.botSkill = skill;
        snprintf(p.name, sizeof p.name, "%s", name);
        p.team = squadSide[squad];
        p.money = startMoney;
        p.alive = false;
        return i;
    }
    return -1;
}

void World::removePlayer(int id) {
    if (id < 0 || id >= MAX_PLAYERS) return;
    Player& p = players[id];
    if (p.alive) { if (p.hasBomb) dropBomb(p, false); }
    p.active = false; p.alive = false;
}

bool World::isEnemy(int a, int b) const {
    if (a == b || a < 0 || b < 0) return false;
    if (mode == MODE_DEATHMATCH) return true;
    return players[a].team != players[b].team;
}

int World::aliveCount(Team t) const {
    int n = 0;
    for (auto& p : players) if (p.active && p.alive && p.team == t) n++;
    return n;
}
int World::teamCount(Team t) const {
    int n = 0;
    for (auto& p : players) if (p.active && p.team == t) n++;
    return n;
}

float World::roundTimeLeft() const {
    if (mode == MODE_DEATHMATCH) return std::max(0.f, dmEndTime - time);
    if (phase == PH_FREEZE) return std::max(0.f, phaseEnd - time);
    if (phase == PH_LIVE) {
        if (bomb.state == BOMB_PLANTED) return std::max(0.f, bomb.explodeTime - time);
        return std::max(0.f, roundStartTime + roundTimeSec - time);
    }
    if (phase == PH_ROUND_END) return std::max(0.f, phaseEnd - time);
    return 0;
}

bool World::isHalftimeNext() const {
    int played = (int)history.size();
    int reg = 2 * (roundsToWin - 1);
    if (played < reg) return played == roundsToWin - 1;
    int k = played - reg;
    return k > 0 && k % 6 == 3;
}

// ---------------------------------------------------------------------------
void World::giveWeapon(Player& p, WeaponId w) {
    const WeaponDef& d = weaponDef(w);
    if (isGrenade(w)) { p.nades[nadeIndex(w)]++; return; }
    if (d.slot == SLOT_PRIMARY) { p.primary = w; p.clip[0] = d.magSize; p.reserve[0] = d.reserve; }
    else if (d.slot == SLOT_SECONDARY) { p.secondary = w; p.clip[1] = d.magSize; p.reserve[1] = d.reserve; }
}

void World::giveDefaultLoadout(Player& p) {
    p.primary = W_NONE; p.clip[0] = p.reserve[0] = 0;
    giveWeapon(p, W_VIPER);
    p.armor = 0; p.helmet = false; p.kit = false;
    for (auto& n : p.nades) n = 0;
}

void World::selectBestWeapon(Player& p) {
    WeaponId w = p.primary != W_NONE ? p.primary : (p.secondary != W_NONE ? p.secondary : W_KNIFE);
    p.lastW = p.activeW;
    p.activeW = w;
    p.drawEnd = time + weaponDef(w).drawTime * 0.5f;
    p.reloading = false; p.scope = 0; p.pinPulled = false;
}

void World::pickSpawn(Player& p) {
    if (mode == MODE_DEATHMATCH) {
        Vector3 best = map.spawnAtk[0];
        float bestScore = -1;
        for (int tries = 0; tries < 40; tries++) {
            int x = rng.irange(1, map.w - 2), z = rng.irange(1, map.h - 2);
            if (!map.tileWalkable(x, z) || map.at(x, z).height != 0) continue;
            Vector3 c = map.tileCenter(x, z);
            float nearest = 1e9f;
            bool seen = false;
            for (auto& q : players) {
                if (!q.active || !q.alive || q.id == p.id) continue;
                float d = len3(q.pos - c);
                nearest = std::min(nearest, d);
                if (d < 30 && map.lineOfSight(q.eye(), c + V3(0, 1.5f, 0))) seen = true;
            }
            float score = std::min(nearest, 40.f) - (seen ? 30.f : 0.f);
            if (score > bestScore) { bestScore = score; best = c; }
        }
        p.pos = best;
        p.yaw = rng.range(-180, 180);
        return;
    }
    auto& list = p.team == TEAM_ATK ? map.spawnAtk : map.spawnDef;
    // pick the first free spawn slot, offset from players already placed
    for (size_t k = 0; k < list.size(); k++) {
        size_t idx = (k + p.id) % list.size();
        bool taken = false;
        for (auto& q : players)
            if (q.active && q.alive && q.id != p.id && len2xz(q.pos - list[idx]) < 1.0f) { taken = true; break; }
        if (!taken) { p.pos = list[idx]; break; }
        p.pos = list[idx];
    }
    p.yaw = p.team == TEAM_ATK ? 0.f : 180.f;
}

void World::respawnPlayer(Player& p, bool keepGear) {
    p.alive = true;
    p.health = 100;
    p.vel = {0, 0, 0};
    p.duck = 0; p.prevDuck = 0;
    p.onGround = true;
    p.pitch = 0;
    p.punchYaw = p.punchPitch = 0;
    p.spreadAccum = 0; p.shotsFired = 0;
    p.flashEnd = 0; p.flashDur = 0;
    p.planting = false; p.defusing = false;
    p.pinPulled = false; p.reloading = false;
    p.stamina = 0; p.tagUntil = 0; p.burnAccum = 0;
    p.scope = 0; p.scopeResume = 0;
    if (!keepGear) giveDefaultLoadout(p);
    if (mode == MODE_DEATHMATCH) {
        p.primary = W_NONE; p.secondary = W_NONE;
        giveWeapon(p, p.dmPrimary);
        giveWeapon(p, p.dmSecondary);
        p.armor = 100; p.helmet = true;
        p.spawnProtectUntil = time + 1.5f;
    }
    if (mode == MODE_PRACTICE && !p.isBot && p.primary == W_NONE) { giveWeapon(p, W_VANGUARD); }
    pickSpawn(p);
    p.prevPos = p.pos;
    for (auto& h : p.hist) h = {p.pos, 0, p.yaw, true};
    selectBestWeapon(p);
    p.drawEnd = time;
}

void World::startMatch() {
    round = 0;
    squadScore[0] = squadScore[1] = 0;
    history.clear();
    matchWinnerSquad = -1;
    overtime = false;
    firstBloodDone = false;
    for (auto& p : players) {
        if (!p.active) continue;
        p.team = squadSide[p.squad];
        p.money = startMoney;
        p.kills = p.deaths = p.assists = p.headshots = p.damage = p.mvps = p.score = 0;
        p.shotsFiredTotal = p.shotsHitTotal = p.plants = p.defuses = p.roundsSurvived = p.firstKills = 0;
        p.alive = false;
        giveDefaultLoadout(p);
    }
    if (mode == MODE_COMPETITIVE) { startRound(); return; }
    // free modes
    phase = PH_LIVE;
    roundStartTime = time;
    dmEndTime = time + g_settings.dmTimeLimit * 60.f;
    for (auto& p : players) if (p.active) respawnPlayer(p, false);
    emit({EV_ROUND_START});
}

void World::startRound() {
    round++;
    phase = PH_FREEZE;
    phaseEnd = time + freezeTimeSec;
    buyEndTime = phaseEnd + buyTimeSec;
    plantedThisRound = false;
    plantedBy = -1;
    firstBloodDone = false;
    projectiles.clear(); smokes.clear(); fires.clear(); drops.clear();
    bomb = Bomb{};
    int played = (int)history.size();
    int reg = 2 * (roundsToWin - 1);
    bool otStartHalf = played >= reg && ((played - reg) % 3 == 0);
    for (auto& p : players) {
        if (!p.active) continue;
        if (otStartHalf && played >= reg) { p.money = 10000; }
        bool keep = p.alive;
        respawnPlayer(p, keep);
        for (int& d : p.dmgTo) d = 0;
        p.roundKills = 0;
        p.boughtThisRound.fill(false);
    }
    // bomb to a random attacker
    std::vector<int> atk;
    for (auto& p : players) if (p.active && p.alive && p.team == TEAM_ATK) atk.push_back(p.id);
    if (!atk.empty()) {
        // prefer a bot if the human attacker has been carrier last round? keep simple: random
        int c = atk[rng.next() % atk.size()];
        players[c].hasBomb = true;
        bomb.state = BOMB_CARRIED; bomb.carrier = c;
    }
    GameEvent e{EV_ROUND_START}; e.value = (int16_t)round;
    emit(e);
}

void World::halftimeSwap() {
    std::swap(squadSide[0], squadSide[1]);
    int played = (int)history.size();
    int reg = 2 * (roundsToWin - 1);
    int money = played >= reg ? 10000 : startMoney;
    for (auto& p : players) {
        if (!p.active) continue;
        p.team = squadSide[p.squad];
        p.money = money;
        p.alive = false;   // forces default loadout at round start
        p.hasBomb = false;
    }
    lossStreak[0] = lossStreak[1] = 0;
    emit({EV_HALFTIME});
}

void World::endRound(Team winner, RoundEndReason reason) {
    if (phase != PH_LIVE) return;
    phase = PH_ROUND_END;
    phaseEnd = time + 5.0f;
    lastWinner = winner;
    lastReason = reason;
    int ws = squadOfTeam(winner), ls = 1 - ws;
    squadScore[ws]++;
    history.push_back({winner, reason, ws});
    lossStreak[ws] = std::max(0, lossStreak[ws] - 1);
    lossStreak[ls] = std::min(5, lossStreak[ls] + 1);
    int winReward = (reason == RE_BOMB_EXPLODED || reason == RE_BOMB_DEFUSED) ? 3500 : 3250;
    int lossReward = 1400 + 500 * (lossStreak[ls] - 1);
    Team loser = otherTeam(winner);
    for (auto& p : players) {
        if (!p.active) continue;
        if (p.team == winner) p.money += winReward;
        else {
            bool noBonus = (reason == RE_TIME && loser == TEAM_ATK && p.alive);
            if (!noBonus) p.money += lossReward;
            if (loser == TEAM_ATK && plantedThisRound) p.money += 800;
        }
        p.money = std::min(p.money, MAX_MONEY);
        if (p.alive) p.roundsSurvived++;
    }
    // MVP
    int mvp = -1;
    if (reason == RE_BOMB_DEFUSED) mvp = bomb.defuser;
    else if (reason == RE_BOMB_EXPLODED) mvp = plantedBy;
    if (mvp < 0) {
        int best = -1;
        for (auto& p : players)
            if (p.active && p.team == winner && p.roundKills > best) { best = p.roundKills; mvp = p.id; }
    }
    if (mvp >= 0) { players[mvp].mvps++; players[mvp].score += 2; }
    lastMvp = mvp;
    // match winner / overtime
    if (!overtime) {
        if (squadScore[ws] >= roundsToWin) matchWinnerSquad = ws;
        else if (squadScore[0] == roundsToWin - 1 && squadScore[1] == roundsToWin - 1) { overtime = true; otBaseScore = roundsToWin - 1; }
    } else {
        if (squadScore[ws] >= otBaseScore + 4) matchWinnerSquad = ws;
        else if (squadScore[0] == otBaseScore + 3 && squadScore[1] == otBaseScore + 3) otBaseScore += 3;
    }
    GameEvent e{EV_ROUND_END};
    e.a = (int8_t)winner; e.b = (int8_t)mvp; e.value = reason;
    emit(e);
}

// ---------------------------------------------------------------------------
bool World::canBuy(int pid) const {
    const Player& p = players[pid];
    if (!p.active || !p.alive) return false;
    if (mode != MODE_COMPETITIVE) return true;
    if (!(phase == PH_FREEZE || (phase == PH_LIVE && time < buyEndTime))) return false;
    uint8_t z = map.zoneAt(p.pos);
    return (p.team == TEAM_ATK && (z & ZONE_BUY_ATK)) || (p.team == TEAM_DEF && (z & ZONE_BUY_DEF));
}

static int boughtIndex(int item) { return item < W_COUNT ? item : 17 + (item - BUY_KEVLAR); }

bool World::tryBuy(int pid, int item) {
    if (!canBuy(pid)) return false;
    Player& p = players[pid];
    int price = 0;
    if (item < W_COUNT) {
        WeaponId w = (WeaponId)item;
        const WeaponDef& d = weaponDef(w);
        if (w == W_KNIFE || w == W_BOMB || w == W_NONE) return false;
        price = d.price;
        if (mode == MODE_DEATHMATCH) {
            if (d.slot == SLOT_PRIMARY) p.dmPrimary = w;
            else if (d.slot == SLOT_SECONDARY) p.dmSecondary = w;
            else return false;
            price = 0;
        }
        if (isGrenade(w)) {
            int idx = nadeIndex(w);
            int limit = (w == W_FLASH) ? 2 : 1;
            if (p.nades[idx] >= limit || p.nadeCount() >= 4) return false;
        } else if (p.hasWeapon(w) && mode != MODE_PRACTICE) return false;
        if (!infiniteMoney && p.money < price) return false;
        if (!isGrenade(w)) {
            WeaponId old = d.slot == SLOT_PRIMARY ? p.primary : p.secondary;
            if (old != W_NONE && old != w) dropWeapon(p, old, false);
        }
        giveWeapon(p, w);
        if (!isGrenade(w)) {
            p.lastW = p.activeW; p.activeW = w; p.drawEnd = time + d.drawTime; p.reloading = false; p.scope = 0;
        }
    } else if (item == BUY_KEVLAR) {
        if (p.armor >= 100) return false;
        price = EQ_KEVLAR_PRICE;
        if (!infiniteMoney && p.money < price) return false;
        p.armor = 100;
    } else if (item == BUY_HELMET) {
        if (p.armor >= 100 && p.helmet) return false;
        price = (p.armor >= 100) ? (EQ_HELMET_PRICE - EQ_KEVLAR_PRICE) : EQ_HELMET_PRICE;
        if (!infiniteMoney && p.money < price) return false;
        p.armor = 100; p.helmet = true;
    } else if (item == BUY_KIT) {
        if (p.team != TEAM_DEF || p.kit) return false;
        price = EQ_KIT_PRICE;
        if (!infiniteMoney && p.money < price) return false;
        p.kit = true;
    } else return false;
    if (!infiniteMoney) p.money -= price;
    p.boughtThisRound[boughtIndex(item)] = true;
    GameEvent e{EV_BUY}; e.a = (int8_t)pid; e.value = (int16_t)item; e.pos = p.pos;
    emit(e);
    return true;
}

bool World::trySell(int pid, int item) {
    if (!canBuy(pid) || mode != MODE_COMPETITIVE) return false;
    Player& p = players[pid];
    int bi = boughtIndex(item);
    if (!p.boughtThisRound[bi]) return false;
    int refund = 0;
    if (item < W_COUNT) {
        WeaponId w = (WeaponId)item;
        if (!p.hasWeapon(w)) return false;
        refund = weaponDef(w).price;
        if (isGrenade(w)) { p.nades[nadeIndex(w)]--; if (p.activeW == w && !p.hasWeapon(w)) selectBestWeapon(p); }
        else if (weaponDef(w).slot == SLOT_PRIMARY) { p.primary = W_NONE; if (p.activeW == w) selectBestWeapon(p); }
        else { p.secondary = W_VIPER; p.clip[1] = weaponDef(W_VIPER).magSize; p.reserve[1] = weaponDef(W_VIPER).reserve; if (p.activeW == w) selectBestWeapon(p); }
    } else if (item == BUY_KEVLAR || item == BUY_HELMET) {
        if (p.armor < 100) return false;
        refund = p.helmet ? EQ_HELMET_PRICE : EQ_KEVLAR_PRICE;
        p.armor = 0; p.helmet = false;
        p.boughtThisRound[boughtIndex(BUY_KEVLAR)] = p.boughtThisRound[boughtIndex(BUY_HELMET)] = false;
    } else if (item == BUY_KIT) {
        if (!p.kit) return false;
        refund = EQ_KIT_PRICE; p.kit = false;
    }
    p.money = std::min(MAX_MONEY, p.money + refund);
    p.boughtThisRound[bi] = false;
    return true;
}

// ---------------------------------------------------------------------------
// Movement (Source-like): friction, ground accel, air strafing, crouch-jump
// ---------------------------------------------------------------------------
float World::playerMaxSpeed(const Player& p, const UserCmd& c) const {
    const WeaponDef& wd = weaponDef(p.activeW);
    float s = wd.maxSpeed;
    if (p.scope > 0) s *= (p.activeW == W_ORACLE ? 0.42f : 0.55f);
    if (c.buttons & IN_WALK) s *= 0.52f;
    if (p.duck > 0.5f) s *= 0.34f;
    if (time < p.tagUntil) s *= (1.0f - 0.55f * p.tagAmount);
    s *= (1.0f - p.stamina * 0.35f);
    return s;
}

static bool collectBlockers(const Map& m, Vector3 feet, float r, float& minX, float& maxX, float& minZ, float& maxZ, float& top) {
    int x0, z0, x1, z1;
    const float e = 1e-4f;
    m.worldToTile({feet.x - r + e, 0, feet.z - r + e}, x0, z0);
    m.worldToTile({feet.x + r - e, 0, feet.z + r - e}, x1, z1);
    bool any = false;
    minX = minZ = 1e9f; maxX = maxZ = -1e9f; top = 0;
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++) {
            float hh = m.at(x, z).height;
            if (hh > feet.y + 1e-3f) {
                any = true;
                float tx0 = m.tileMinX(x), tz0 = m.tileMinZ(z);
                minX = std::min(minX, tx0); maxX = std::max(maxX, tx0 + Map::TILE);
                minZ = std::min(minZ, tz0); maxZ = std::max(maxZ, tz0 + Map::TILE);
                top = std::max(top, hh);
            }
        }
    return any;
}

static void moveAxis(const Map& m, Vector3& pos, Vector3& vel, float delta, int axis, bool canStep, float height) {
    if (fabsf(delta) < 1e-7f) return;
    Vector3 np = pos;
    if (axis == 0) np.x += delta; else np.z += delta;
    float mnx, mxx, mnz, mxz, top;
    if (!collectBlockers(m, np, PLAYER_RADIUS, mnx, mxx, mnz, mxz, top)) { pos = np; return; }
    if (canStep && top - pos.y <= STEP_HEIGHT) {
        Vector3 sp = np; sp.y = top;
        if (!m.boxBlocked(sp, PLAYER_RADIUS, height)) { pos = sp; return; }
    }
    // slide to contact
    if (axis == 0) np.x = delta > 0 ? mnx - PLAYER_RADIUS - 1e-3f : mxx + PLAYER_RADIUS + 1e-3f;
    else np.z = delta > 0 ? mnz - PLAYER_RADIUS - 1e-3f : mxz + PLAYER_RADIUS + 1e-3f;
    // only accept if it doesn't move us backwards past the start
    float moved = axis == 0 ? (np.x - pos.x) : (np.z - pos.z);
    if (moved * delta >= -1e-4f && !m.boxBlocked(np, PLAYER_RADIUS, height)) pos = np;
    if (axis == 0) vel.x = 0; else vel.z = 0;
}

void World::movePlayer(Player& p, const UserCmd& c, bool frozen) {
    const float dt = TICK_DT;
    // --- crouch
    float oldH = p.height();
    float target = (c.buttons & IN_DUCK) ? 1.f : 0.f;
    float oldDuck = p.duck;
    p.duck = approach(p.duck, target, dt / 0.11f);
    if (!p.onGround) {
        float dh = oldH - p.height();
        Vector3 np = p.pos; np.y += dh;
        float g = map.groundHeightAt(np, PLAYER_RADIUS, p.pos.y + 0.01f);
        if (np.y < g) np.y = g;
        if (map.boxBlocked(np, PLAYER_RADIUS, p.height())) p.duck = oldDuck;
        else p.pos = np;
    }

    // --- wish direction
    Vector3 f = flatForward(p.yaw), r = flatRight(p.yaw);
    float fm = frozen ? 0 : clampf(c.fwd, -1, 1), sm = frozen ? 0 : clampf(c.side, -1, 1);
    Vector3 wish = f * fm + r * sm;
    float wl = len3(wish);
    if (wl > 1) { wish = wish * (1.0f / wl); wl = 1; }
    Vector3 wishDir = wl > 0 ? wish * (1.0f / wl) : V3(0, 0, 0);
    float maxSpeed = playerMaxSpeed(p, c);
    float wishSpeed = maxSpeed * wl;

    p.stamina = std::max(0.f, p.stamina - dt * 0.9f);

    bool jumpPressed = (c.buttons & IN_JUMP) && !p.jumpHeld && !frozen;
    p.jumpHeld = (c.buttons & IN_JUMP) != 0;

    if (p.onGround) {
        if (jumpPressed) {
            p.vel.y = JUMP_SPEED * (1.0f - p.stamina * 0.25f);
            p.onGround = false;
            p.stamina = std::min(1.0f, p.stamina + 0.32f);
            GameEvent e{EV_JUMP}; e.a = (int8_t)p.id; e.pos = p.pos; emit(e);
        } else {
            // friction
            float speed = len2xz(p.vel);
            if (speed > 0.0001f) {
                float control = std::max(speed, 2.0f);
                float drop = control * 5.2f * dt;
                float ns = std::max(0.f, speed - drop) / speed;
                p.vel.x *= ns; p.vel.z *= ns;
            }
        }
    }
    if (p.onGround) {
        float cur = dot3(p.vel, wishDir);
        float add = wishSpeed - cur;
        if (add > 0) {
            float acc = std::min(5.5f * dt * maxSpeed, add);
            p.vel += wishDir * acc;
        }
        // hard cap on ground
        float sp = len2xz(p.vel);
        if (sp > maxSpeed && sp > 0) { float k = maxSpeed / sp; p.vel.x *= k; p.vel.z *= k; }
    } else {
        // air strafing
        float capped = std::min(wishSpeed, 0.62f);
        float cur = dot3(V3(p.vel.x, 0, p.vel.z), wishDir);
        float add = capped - cur;
        if (add > 0) {
            float acc = std::min(12.0f * wishSpeed * dt, add);
            p.vel += wishDir * acc;
        }
        p.vel.y -= GRAVITY * dt;
        p.airTime += dt;
    }

    bool wasGround = p.onGround;
    float fallSpeed = -p.vel.y;
    // --- horizontal collision
    moveAxis(map, p.pos, p.vel, p.vel.x * dt, 0, p.onGround, p.height());
    moveAxis(map, p.pos, p.vel, p.vel.z * dt, 1, p.onGround, p.height());

    // --- vertical
    float ground = map.groundHeightAt(p.pos, PLAYER_RADIUS, p.pos.y + 0.02f);
    if (p.onGround) {
        if (p.pos.y - ground > 1e-3f) {
            if (p.pos.y - ground <= STEP_HEIGHT + 0.01f && p.vel.y <= 0) p.pos.y = ground;   // step down
            else { p.onGround = false; }
        } else p.pos.y = ground;
    } else {
        float ny = p.pos.y + p.vel.y * dt;
        if (p.vel.y <= 0 && ny <= ground) {
            p.pos.y = ground;
            p.vel.y = 0;
            p.onGround = true;
        } else p.pos.y = ny;
    }
    if (!wasGround && p.onGround) {
        p.airTime = 0;
        p.lastLandTime = time;
        // landing slowdown (anti bunny-hop)
        p.vel.x *= 0.82f; p.vel.z *= 0.82f;
        GameEvent e{EV_LAND}; e.a = (int8_t)p.id; e.pos = p.pos; e.value = (int16_t)(fallSpeed * 10); emit(e);
        if (fallSpeed > 11.0f) {
            float dmg = (fallSpeed - 11.0f) * 12.0f;
            applyDamage(p, -1, dmg, HG_LEGS, W_NONE, 1.0f, p.pos, 0);
        }
    }
    // --- footsteps
    float sp = len2xz(p.vel);
    if (p.onGround && sp > 2.9f) {
        p.stepAccum += sp * dt;
        if (p.stepAccum > 2.1f) {
            p.stepAccum = 0;
            GameEvent e{EV_FOOTSTEP}; e.a = (int8_t)p.id; e.pos = p.pos; emit(e);
        }
    }
}

// ---------------------------------------------------------------------------
Vector3 World::hitboxCenter(const Player& p, HitGroup g) const {
    float s = p.height() / STAND_HEIGHT;
    switch (g) {
    case HG_HEAD: return p.pos + V3(0, p.eyeHeight() + 0.07f, 0);
    case HG_CHEST: return p.pos + V3(0, 1.2f * s, 0);
    default: return p.pos + V3(0, 0.5f * s, 0);
    }
}

static bool hitTest(Vector3 pos, float duck, float yaw, Vector3 o, Vector3 d, float& tOut, HitGroup& hg) {
    float s = lerpf(STAND_HEIGHT, CROUCH_HEIGHT, duck) / STAND_HEIGHT;
    float eyeH = lerpf(STAND_EYE, CROUCH_EYE, duck);
    // broadphase cylinder check via sphere
    Vector3 mid = pos + V3(0, 0.9f * s, 0);
    float tb;
    if (!raySphere(o, d, mid, 1.15f, tb)) return false;
    Vector3 fwd = flatForward(yaw);
    float best = 1e9f;
    HitGroup bg = HG_NONE;
    float t;
    Vector3 head = pos + V3(0, eyeH + 0.07f, 0) + fwd * 0.03f;
    if (raySphere(o, d, head, 0.145f, t) && t < best) { best = t; bg = HG_HEAD; }
    if (rayCapsule(o, d, pos + V3(0, 0.95f * s, 0), pos + V3(0, 1.42f * s, 0), 0.23f, t) && t < best - 0.02f) { best = t; bg = HG_CHEST; }
    if (rayCapsule(o, d, pos + V3(0, 0.12f, 0), pos + V3(0, 0.88f * s, 0), 0.19f, t) && t < best - 0.02f) { best = t; bg = HG_LEGS; }
    // arms (count as chest)
    Vector3 rr = flatRight(yaw);
    if (rayCapsule(o, d, pos + V3(0, 1.38f * s, 0) + rr * 0.3f, pos + V3(0, 1.25f * s, 0) + rr * 0.15f + fwd * 0.45f, 0.08f, t) && t < best - 0.02f) { best = t; bg = HG_CHEST; }
    if (bg == HG_NONE) return false;
    tOut = best; hg = bg;
    return true;
}

bool World::traceHitPlayer(Vector3 o, Vector3 d, float maxDist, int ignore, int& who, HitGroup& hg, float& tBest, int rewind) const {
    who = -1; tBest = maxDist;
    for (const Player& q : players) {
        if (!q.active || q.id == ignore) continue;
        Vector3 pos = q.pos; float duck = q.duck; float yaw = q.yaw; bool alive = q.alive;
        if (rewind > 0 && rewind < HISTORY - 1) {
            const PlayerHistory& h = q.hist[(tick - rewind) % HISTORY];
            pos = h.pos; duck = h.duck; yaw = h.yaw; alive = h.alive && q.alive;
        }
        if (!alive) continue;
        float t; HitGroup g;
        if (hitTest(pos, duck, yaw, o, d, t, g) && t < tBest) { tBest = t; who = q.id; hg = g; }
    }
    return who >= 0;
}

bool World::smokeBlocks(Vector3 a, Vector3 b) const {
    Vector3 ab = b - a;
    float L2 = dot3(ab, ab);
    for (auto& s : smokes) {
        if (!s.active) continue;
        float age = time - s.start, left = s.end - time;
        float r = 3.5f * clampf(age / 0.7f, 0, 1) * clampf(left / 1.5f, 0, 1);
        if (r < 0.6f) continue;
        Vector3 c = s.pos + V3(0, 1.4f, 0);
        float t = L2 > 0 ? clampf(dot3(c - a, ab) / L2, 0, 1) : 0;
        Vector3 p = a + ab * t;
        Vector3 dlt = p - c;
        dlt.y *= 1.6f;  // flattened ellipsoid
        if (dot3(dlt, dlt) < r * r) return true;
    }
    return false;
}

bool World::canSee(int viewer, int target, bool useFov) const {
    const Player& v = players[viewer];
    const Player& t = players[target];
    if (!v.alive || !t.alive) return false;
    Vector3 e = v.eye();
    if (useFov) {
        Vector3 to = norm3(t.pos + V3(0, 1.2f, 0) - e);
        if (dot3(to, forwardFromAngles(v.yaw, v.pitch)) < cosf(80 * DEG)) return false;
    }
    if (time < v.flashEnd && (v.flashEnd - time) > v.flashDur * 0.25f) return false;
    Vector3 pts[3] = {hitboxCenter(t, HG_HEAD), hitboxCenter(t, HG_CHEST), hitboxCenter(t, HG_LEGS)};
    for (auto& p : pts) {
        if (len3(p - e) > 140) continue;
        if (map.lineOfSight(e, p) && !smokeBlocks(e, p)) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Weapons
// ---------------------------------------------------------------------------
void World::weaponThink(Player& p, const UserCmd& c) {
    float now = time;
    bool atk = (c.buttons & IN_ATTACK) != 0, atk2 = (c.buttons & IN_ATTACK2) != 0;
    bool frozenPhase = (mode == MODE_COMPETITIVE && phase == PH_FREEZE) || phase == PH_MATCH_END;

    // --- switching
    if (c.select && c.select != p.activeW && p.hasWeapon((WeaponId)c.select) && !p.defusing) {
        p.lastW = p.activeW;
        p.activeW = (WeaponId)c.select;
        p.drawEnd = now + weaponDef(p.activeW).drawTime;
        p.reloading = false; p.scope = 0; p.scopeResume = 0; p.pinPulled = false; p.planting = false;
        p.shotsFired = 0; p.inspectUntil = 0;
        GameEvent e{EV_DRAW}; e.a = (int8_t)p.id; e.weapon = p.activeW; e.pos = p.pos; emit(e);
    }
    const WeaponDef& wd = weaponDef(p.activeW);
    int slot = (wd.slot == SLOT_PRIMARY) ? 0 : 1;

    // --- drop
    bool dropNow = (c.buttons & IN_DROP) && !p.dropHeld;
    p.dropHeld = (c.buttons & IN_DROP) != 0;
    if (dropNow && !frozenPhase) {
        if (p.activeW == W_BOMB) { dropBomb(p, true); selectBestWeapon(p); return; }
        if (isGun(p.activeW)) { dropWeapon(p, p.activeW, true); selectBestWeapon(p); return; }
    }

    // --- inspect
    if ((c.buttons & IN_INSPECT) && !p.reloading && now > p.nextAttack) p.inspectUntil = now + 2.2f;

    // --- reload completion
    if (p.reloading && now >= p.reloadEnd) {
        int need = wd.magSize - p.clip[slot];
        if (wd.cls == WC_SHOTGUN) need = std::min(need, 1);
        int take = infiniteAmmo ? need : std::min(need, p.reserve[slot]);
        p.clip[slot] += take;
        if (!infiniteAmmo) p.reserve[slot] -= take;
        p.reloading = false;
        if (wd.cls == WC_SHOTGUN && p.clip[slot] < wd.magSize && (p.reserve[slot] > 0 || infiniteAmmo) && !atk) {
            p.reloading = true; p.reloadEnd = now + wd.reloadTime;
            GameEvent e{EV_RELOAD}; e.a = (int8_t)p.id; e.weapon = p.activeW; e.pos = p.pos; emit(e);
        }
    }
    // --- start reload
    bool reloadPressed = (c.buttons & IN_RELOAD) != 0;
    if (reloadPressed && isGun(p.activeW) && !p.reloading && p.clip[slot] < wd.magSize &&
        (p.reserve[slot] > 0 || infiniteAmmo) && now >= p.drawEnd && now >= p.nextAttack - 0.05f) {
        p.reloading = true;
        p.reloadEnd = now + wd.reloadTime;
        p.scope = 0; p.scopeResume = 0;
        p.inspectUntil = 0;
        GameEvent e{EV_RELOAD}; e.a = (int8_t)p.id; e.weapon = p.activeW; e.pos = p.pos; emit(e);
    }

    // --- recoil / spread recovery
    float interval = wd.rpm > 0 ? 60.f / wd.rpm : 0.5f;
    if (now - p.lastShotTime > interval * 1.05f) {
        float m = sqrtf(p.punchPitch * p.punchPitch + p.punchYaw * p.punchYaw);
        if (m > 0) {
            float amt = (wd.recoilRecovery + m * 2.5f) * TICK_DT;
            float nm = std::max(0.f, m - amt);
            p.punchPitch *= nm / m; p.punchYaw *= nm / m;
        }
        p.spreadAccum = approach(p.spreadAccum, 0, wd.spreadRecovery * TICK_DT);
        if (now - p.lastShotTime > interval + 0.28f) p.shotsFired = 0;
        else if (m < 0.25f) p.shotsFired = std::max(0, p.shotsFired - 1);
    }
    // sniper scope resume after bolt
    if (p.scopeResume && now >= p.nextAttack && !p.reloading) { p.scope = p.scopeResume; p.scopeResume = 0; }

    if (frozenPhase) { p.attackHeld = atk; p.attack2Held = atk2; return; }

    // --- secondary (scope)
    if (atk2 && !p.attack2Held && wd.scopeLevels > 0 && now >= p.drawEnd && !p.reloading) {
        p.scope = (p.scope + 1) % (wd.scopeLevels + 1);
        p.scopeResume = 0;
        GameEvent e{EV_SCOPE}; e.a = (int8_t)p.id; e.value = (int16_t)p.scope; e.pos = p.pos; emit(e);
    }

    // --- knife
    if (p.activeW == W_KNIFE) {
        if (now >= p.nextAttack && now >= p.drawEnd) {
            if (atk) { knifeAttack(p, false); p.nextAttack = now + 0.42f; }
            else if (atk2) { knifeAttack(p, true); p.nextAttack = now + 1.0f; }
        }
    }
    // --- grenades
    else if (isGrenade(p.activeW)) {
        if (now >= p.drawEnd) {
            if (atk || atk2) { p.pinPulled = true; p.throwStrength = (atk ? 1 : 0) | (atk2 ? 2 : 0); }
            else if (p.pinPulled) {
                float s = p.throwStrength == 1 ? 1.0f : (p.throwStrength == 2 ? 0.38f : 0.68f);
                throwGrenade(p, s);
                p.pinPulled = false;
            }
        }
    }
    // --- bomb
    else if (p.activeW == W_BOMB) {
        uint8_t z = map.zoneAt(p.pos);
        bool inSite = (z & (ZONE_SITE_A | ZONE_SITE_B)) != 0;
        if (atk && inSite && p.onGround && phase == PH_LIVE && now >= p.drawEnd) {
            if (!p.planting) {
                p.planting = true; p.plantStart = now;
                GameEvent e{EV_PLANT_START}; e.a = (int8_t)p.id; e.pos = p.pos; emit(e);
            } else if (now - p.plantStart >= PLANT_TIME && !clientSide) {
                p.planting = false;
                p.hasBomb = false;
                bomb.state = BOMB_PLANTED;
                bomb.pos = p.pos;
                bomb.carrier = -1;
                bomb.plantTime = now;
                bomb.explodeTime = now + bombTimeSec;
                bomb.nextBeep = now;
                bomb.site = (z & ZONE_SITE_A) ? 1 : 2;
                plantedThisRound = true;
                plantedBy = p.id;
                p.money = std::min(MAX_MONEY, p.money + 300);
                p.plants++; p.score += 2;
                GameEvent e{EV_BOMB_PLANTED}; e.a = (int8_t)p.id; e.pos = p.pos; e.value = bomb.site; emit(e);
                selectBestWeapon(p);
            }
        } else p.planting = false;
    }
    // --- guns
    else if (isGun(p.activeW)) {
        if (atk && p.reloading && wd.cls == WC_SHOTGUN && p.clip[slot] > 0) p.reloading = false;
        if (atk && now >= p.nextAttack && now >= p.drawEnd && !p.reloading) {
            if (!wd.automatic && p.attackHeld) {
                // semi-auto: must release trigger
            } else if (p.clip[slot] <= 0) {
                if (!p.attackHeld) {
                    GameEvent e{EV_DRYFIRE}; e.a = (int8_t)p.id; e.pos = p.pos; emit(e);
                    if (p.reserve[slot] > 0 || infiniteAmmo) {
                        p.reloading = true; p.reloadEnd = now + wd.reloadTime;
                        GameEvent r{EV_RELOAD}; r.a = (int8_t)p.id; r.weapon = p.activeW; r.pos = p.pos; emit(r);
                    }
                }
                p.nextAttack = now + 0.2f;
            } else fireWeapon(p, c);
        }
    }
    p.attackHeld = atk;
    p.attack2Held = atk2;
}

float World::currentInaccuracy(const Player& p) const {
    const WeaponDef& wd = weaponDef(p.activeW);
    if (!isGun(p.activeW)) return 0;
    float speed = len2xz(p.vel);
    float maxs = wd.maxSpeed;
    float base = p.duck > 0.5f ? wd.spreadCrouch : wd.spreadStand;
    if (wd.cls == WC_SNIPER && p.scope > 0) base = wd.scopedSpread;
    float moveFrac = clampf((speed - maxs * 0.34f) / (maxs * 0.66f), 0, 1);
    float inacc = base + wd.spreadMove * moveFrac + p.spreadAccum;
    if (!p.onGround) inacc += (p.activeW == W_LANCER && fabsf(p.vel.y) < 0.9f) ? 0.05f : wd.spreadAir;
    return inacc;
}

void World::fireWeapon(Player& p, const UserCmd& c) {
    const WeaponDef& wd = weaponDef(p.activeW);
    int slot = (wd.slot == SLOT_PRIMARY) ? 0 : 1;
    float now = time;
    p.clip[slot]--;
    p.nextAttack = now + 60.f / wd.rpm;
    p.lastShotTime = now;
    p.shotsFiredTotal++;
    p.inspectUntil = 0;

    // --- inaccuracy
    float speed = len2xz(p.vel);
    float maxs = wd.maxSpeed;
    float base = p.duck > 0.5f ? wd.spreadCrouch : wd.spreadStand;
    bool scoped = p.scope > 0;
    if (wd.cls == WC_SNIPER && scoped) base = wd.scopedSpread;
    float moveFrac = clampf((speed - maxs * 0.34f) / (maxs * 0.66f), 0, 1);
    float inacc = base + wd.spreadMove * moveFrac + p.spreadAccum;
    if (!p.onGround) {
        bool apex = (p.activeW == W_LANCER && fabsf(p.vel.y) < 0.9f);
        inacc += apex ? 0.05f : wd.spreadAir;
    }
    p.spreadAccum = std::min(p.spreadAccum + wd.spreadPerShot, wd.spreadMax);

    // --- direction (punch applied BEFORE this shot's recoil -> first bullet is perfect)
    float byaw = p.yaw + p.punchYaw;
    float bpitch = p.pitch + p.punchPitch;

    // --- recoil
    if (!wd.pattern.empty()) {
        const RecoilStep& st = wd.pattern[std::min<size_t>(p.shotsFired, wd.pattern.size() - 1)];
        p.punchPitch += st.pitch * wd.recoilScale + rng.range(-1, 1) * wd.recoilRandom;
        p.punchYaw += st.yaw * wd.recoilScale + rng.range(-1, 1) * wd.recoilRandom;
    }
    p.shotsFired++;

    Vector3 o = p.eye();
    Vector3 fwd = forwardFromAngles(byaw, bpitch);
    Vector3 right = norm3(cross3(fwd, V3(0, 1, 0)));
    Vector3 up = cross3(right, fwd);
    int lag = c.lagTicks;
    for (int i = 0; i < wd.pellets; i++) {
        float a = rng.range(0, 6.2831853f);
        float rr;
        if (wd.pellets > 1) {
            // shotgun: fixed ring pattern + randomness (skill-friendly)
            float ring = (i == 0) ? 0.f : (i < 5 ? 0.55f : 1.0f);
            a = (i == 0 ? 0 : (i * 6.2831853f / 4.0f) + (i >= 5 ? 0.785f : 0)) + rng.range(-0.2f, 0.2f);
            rr = inacc * ring + rng.range(0, inacc * 0.25f);
        } else rr = inacc * sqrtf(rng.f01());
        float ox = tanf(rr * DEG) * cosf(a), oy = tanf(rr * DEG) * sinf(a);
        Vector3 d = norm3(fwd + right * ox + up * oy);
        traceBullet(p, o, d, wd, 1.0f, lag, i == 0);
    }
    if (wd.cls == WC_SNIPER && p.scope > 0) { p.scopeResume = p.scope; p.scope = 0; }

    GameEvent e{EV_SHOT};
    e.a = (int8_t)p.id; e.weapon = p.activeW; e.pos = o; e.pos2 = lastShotEnd;
    e.flags = scoped ? 1 : 0;
    emit(e);
}

void World::traceBullet(Player& shooter, Vector3 o, Vector3 d, const WeaponDef& wd, float dmgScale, int rewind, bool firstPellet) {
    static std::vector<SolidSpan> spans;
    float maxDist = wd.cls == WC_SHOTGUN ? 45.f : 220.f;
    map.raySpans(o, d, maxDist, spans);
    struct PH { int who; HitGroup hg; float t; };
    PH hits[MAX_PLAYERS]; int nh = 0;
    for (const Player& q : players) {
        if (!q.active || q.id == shooter.id) continue;
        Vector3 pos = q.pos; float duck = q.duck; float yaw = q.yaw; bool alive = q.alive;
        if (rewind > 0 && rewind < HISTORY - 1) {
            const PlayerHistory& h = q.hist[(tick - rewind) % HISTORY];
            pos = h.pos; duck = h.duck; yaw = h.yaw; alive = h.alive && q.alive;
        }
        if (!alive) continue;
        float t; HitGroup g;
        if (hitTest(pos, duck, yaw, o, d, t, g) && t < maxDist) hits[nh++] = {q.id, g, t};
    }
    std::sort(hits, hits + nh, [](const PH& a, const PH& b) { return a.t < b.t; });
    float power = wd.penetration;
    float mul = dmgScale;
    int walls = 0;
    size_t wi = 0; int hi = 0;
    float endT = maxDist;
    bool hitSomeone = false;
    while (true) {
        float nw = wi < spans.size() ? spans[wi].tIn : 1e30f;
        float np = hi < nh ? hits[hi].t : 1e30f;
        if (np < nw) {
            const PH& h = hits[hi++];
            Player& victim = players[h.who];
            if (!isEnemy(shooter.id, h.who) && !friendlyFire && mode != MODE_DEATHMATCH) continue;
            float falloff = powf(wd.rangeMod, h.t / 25.0f);
            uint8_t fl = 0;
            if (walls > 0) fl |= 2;
            if (smokeBlocks(o, o + d * h.t)) fl |= 8;
            if (wd.cls == WC_SNIPER && shooter.scope == 0 && shooter.scopeResume == 0) fl |= 4;
            if (time < shooter.flashEnd && shooter.flashEnd - time > 1.0f) fl |= 16;
            applyDamage(victim, shooter.id, wd.damage * mul * falloff, h.hg, wd.id, wd.armorPen, o, fl);
            if (!hitSomeone) { shooter.shotsHitTotal++; hitSomeone = true; }
            mul *= 0.55f;
            power -= 0.6f;
            if (power <= 0) { endT = h.t; break; }
            continue;
        }
        if (nw >= 1e29f) break;
        const SolidSpan& s = spans[wi++];
        GameEvent ie{EV_IMPACT};
        ie.a = (int8_t)shooter.id; ie.pos = o + d * s.tIn; ie.pos2 = s.nIn; ie.value = s.mat;
        emit(ie);
        if (s.tOut > 1e29f || s.mat == MAT_FLOOR) { endT = s.tIn; break; }
        float thick = s.tOut - s.tIn;
        float cost = s.mat == MAT_WOOD ? 1.0f : (s.mat == MAT_METAL ? 4.5f : 3.2f);
        power -= thick * cost;
        if (power <= 0) { endT = s.tIn; break; }
        mul *= clampf(1.0f - thick * cost * 0.22f - 0.12f, 0.2f, 1.0f);
        GameEvent xe{EV_IMPACT};
        xe.a = (int8_t)shooter.id; xe.pos = o + d * s.tOut; xe.pos2 = d; xe.value = s.mat; xe.flags = 1;
        emit(xe);
        walls++;
    }
    if (firstPellet) lastShotEnd = o + d * endT;
}

void World::knifeAttack(Player& p, bool heavy) {
    Vector3 o = p.eye();
    Vector3 d = forwardFromAngles(p.yaw, p.pitch);
    float range = heavy ? 1.6f : 1.9f;
    int who; HitGroup hg; float t;
    GameEvent sw{EV_KNIFE_SWING}; sw.a = (int8_t)p.id; sw.pos = o; sw.flags = heavy; emit(sw);
    if (traceHitPlayer(o, d, range, p.id, who, hg, t, 0)) {
        RayHit wh = map.raycast(o, d, t);
        if (!wh.hit && isEnemy(p.id, who)) {
            Player& v = players[who];
            bool back = dot3(flatForward(v.yaw), flatForward(p.yaw)) > 0.55f;
            float dmg = heavy ? (back ? 180.f : 65.f) : (back ? 90.f : 40.f);
            applyDamage(v, p.id, dmg / (hg == HG_HEAD ? 4.f : (hg == HG_LEGS ? 0.75f : 1.f)), hg, W_KNIFE, 0.85f, o, 0);
            GameEvent e{EV_KNIFE_HIT}; e.a = (int8_t)p.id; e.b = (int8_t)who; e.pos = o + d * t; emit(e);
            return;
        }
    }
    RayHit h = map.raycast(o, d, range);
    if (h.hit) { GameEvent e{EV_KNIFE_HIT}; e.a = (int8_t)p.id; e.pos = h.pos; e.pos2 = h.normal; emit(e); }
}

void World::applyDamage(Player& v, int attacker, float dmg, HitGroup hg, WeaponId w, float armorPen, Vector3 from, uint8_t extraFlags) {
    if (!v.alive || clientSide) return;
    if (time < v.spawnProtectUntil) return;
    if (headshotOnly && isGun(w) && hg != HG_HEAD) return;
    if (mode == MODE_COMPETITIVE && phase != PH_LIVE && phase != PH_ROUND_END) return;
    float mult = hg == HG_HEAD ? 4.0f : (hg == HG_LEGS ? 0.75f : 1.0f);
    if (w == W_BREACH && hg == HG_HEAD) mult = 2.5f;
    if (w == W_FRAG || w == W_MOLLY || w == W_NONE || w == W_BOMB) mult = 1.0f;
    float d = dmg * mult;
    bool armored = v.armor > 0 && (hg == HG_CHEST || (hg == HG_HEAD && v.helmet) || w == W_FRAG || w == W_BOMB);
    if (armored && armorPen < 1.0f) {
        float nd = d * armorPen;
        float ad = (d - nd) * 0.5f;
        if (ad > v.armor) { ad = (float)v.armor; nd = d - ad * 2.0f; }
        v.armor -= (int)ad;
        if (v.armor < 0) v.armor = 0;
        d = nd;
    }
    int id = std::max(1, (int)d);
    id = std::min(id, v.health);
    v.health -= id;
    v.lastHurtTime = time;
    v.lastHurtFrom = from;
    const WeaponDef& wd = weaponDef(w);
    if (isGun(w)) { v.tagUntil = time + 0.4f; v.tagAmount = wd.tagging; }
    if (attacker >= 0 && attacker < MAX_PLAYERS) {
        Player& a = players[attacker];
        if (isEnemy(attacker, v.id)) { a.damage += id; a.dmgTo[v.id] += id; }
    }
    GameEvent e{EV_HIT};
    e.a = (int8_t)attacker; e.b = (int8_t)v.id; e.value = (int16_t)id; e.weapon = w;
    e.flags = (hg == HG_HEAD ? 1 : 0) | (armored ? 2 : 0) | (v.helmet ? 4 : 0);
    e.pos = hitboxCenter(v, hg == HG_NONE ? HG_CHEST : hg); e.pos2 = from;
    emit(e);
    if (v.health <= 0) killPlayer(v, attacker, w, (hg == HG_HEAD ? 1 : 0) | extraFlags);
}

void World::killPlayer(Player& v, int attacker, WeaponId w, uint8_t flags) {
    v.alive = false;
    v.health = 0;
    v.deaths++;
    v.planting = false;
    if (v.defusing) { v.defusing = false; bomb.defuser = -1; }
    if (mode == MODE_DEATHMATCH || mode == MODE_PRACTICE) v.respawnTime = time + (mode == MODE_PRACTICE ? 2.5f : 2.0f);
    // drops
    if (mode == MODE_COMPETITIVE) {
        if (v.primary != W_NONE) dropWeapon(v, v.primary, false);
        else if (v.secondary != W_NONE && v.secondary != W_VIPER) dropWeapon(v, v.secondary, false);
    }
    if (v.hasBomb) dropBomb(v, false);
    v.kit = false;
    if (attacker >= 0 && attacker != v.id) {
        Player& a = players[attacker];
        if (isEnemy(attacker, v.id)) {
            a.kills++; a.roundKills++; a.score += 2;
            if (flags & 1) a.headshots++;
            if (mode == MODE_COMPETITIVE) a.money = std::min(MAX_MONEY, a.money + weaponDef(w).killReward);
            if (!firstBloodDone) { firstBloodDone = true; a.firstKills++; }
        } else {
            a.kills--; a.score -= 2;
            if (mode == MODE_COMPETITIVE) a.money = std::max(0, a.money - 300);
        }
    }
    for (auto& q : players) {
        if (!q.active || q.id == attacker || q.id == v.id) continue;
        if (q.dmgTo[v.id] >= 40 && isEnemy(q.id, v.id)) { q.assists++; q.score += 1; }
    }
    GameEvent e{EV_KILL};
    e.a = (int8_t)attacker; e.b = (int8_t)v.id; e.weapon = w; e.flags = flags; e.pos = v.pos;
    emit(e);
}

void World::dropWeapon(Player& p, WeaponId w, bool toss) {
    const WeaponDef& d = weaponDef(w);
    if (!isGun(w)) return;
    int slot = d.slot == SLOT_PRIMARY ? 0 : 1;
    DroppedWeapon dw;
    dw.active = true; dw.w = w; dw.clip = p.clip[slot]; dw.reserve = p.reserve[slot];
    dw.pos = p.pos + V3(0, toss ? 1.3f : 0.6f, 0);
    dw.vel = toss ? forwardFromAngles(p.yaw, std::max(p.pitch, -10.f) + 12.f) * 5.0f + p.vel : V3(rng.range(-1, 1), 1.0f, rng.range(-1, 1));
    dw.spawnTime = time;
    if (slot == 0) { p.primary = W_NONE; p.clip[0] = p.reserve[0] = 0; }
    else { p.secondary = W_NONE; p.clip[1] = p.reserve[1] = 0; }
    if (drops.size() > 40) drops.erase(drops.begin());
    drops.push_back(dw);
}

void World::dropBomb(Player& p, bool toss) {
    if (!p.hasBomb) return;
    p.hasBomb = false;
    p.planting = false;
    bomb.state = BOMB_DROPPED;
    bomb.carrier = -1;
    Vector3 pos = p.pos;
    if (toss) {
        Vector3 f = flatForward(p.yaw);
        Vector3 cand = p.pos + f * 1.6f;
        int x, z; map.worldToTile(cand, x, z);
        if (map.at(x, z).height <= p.pos.y + 0.01f) pos = cand;
    }
    pos.y = map.groundHeightAt(pos, 0.1f, pos.y + 0.05f);
    bomb.pos = pos;
    bomb.nextBeep = time + 1.0f;   // prevents immediate re-pickup by the dropper
    GameEvent e{EV_BOMB_DROP}; e.a = (int8_t)p.id; e.pos = pos; emit(e);
}

void World::predictGrenade(const Player& p, float strength, std::vector<Vector3>& out) const {
    out.clear();
    float pitch = p.pitch + (90.f - fabsf(p.pitch)) * 10.f / 90.f;
    Vector3 dir = forwardFromAngles(p.yaw, pitch);
    Vector3 pos = p.eye() + dir * 0.25f;
    Vector3 vel = dir * (19.5f * strength + 2.0f) + p.vel * 1.1f;
    auto solid = [&](Vector3 q) { if (q.y < 0.03f) return true; int x, z; map.worldToTile(q, x, z); return map.at(x, z).height > q.y - 0.03f; };
    out.push_back(pos);
    for (int i = 0; i < TICK_RATE * 3; i++) {
        vel.y -= NADE_GRAVITY * TICK_DT;
        Vector3 np = pos + vel * TICK_DT;
        if (solid(np)) {
            Vector3 t = pos; t.x = np.x;
            if (solid(t)) { vel.x = -vel.x * 0.45f; vel.z *= 0.75f; } else pos.x = np.x;
            t = pos; t.z = np.z;
            if (solid(t)) { vel.z = -vel.z * 0.45f; vel.x *= 0.75f; } else pos.z = np.z;
            t = pos; t.y = np.y;
            if (solid(t)) { vel.y = -vel.y * 0.38f; vel.x *= 0.7f; vel.z *= 0.7f; } else pos.y = np.y;
            if (len3(vel) < 0.5f) { out.push_back(pos); break; }
        } else pos = np;
        if (i % 4 == 0) out.push_back(pos);
    }
}

void World::throwGrenade(Player& p, float strength) {
    WeaponId w = p.activeW;
    int idx = nadeIndex(w);
    if (idx < 0 || p.nades[idx] == 0) return;
    if (!infiniteAmmo) p.nades[idx]--;
    Projectile pr;
    pr.active = true; pr.type = w; pr.owner = p.id; pr.team = p.team;
    float pitch = p.pitch;
    pitch = pitch + (90.f - fabsf(pitch)) * 10.f / 90.f;   // CS-like upward bias
    Vector3 dir = forwardFromAngles(p.yaw, pitch);
    Vector3 start = p.eye() + dir * 0.25f;
    if (!map.lineOfSight(p.eye(), start + dir * 0.2f)) start = p.eye();
    pr.pos = start;
    pr.prevPos = start;
    pr.vel = dir * (19.5f * strength + 2.0f) + p.vel * 1.1f;
    pr.spawnTime = time;
    projectiles.push_back(pr);
    GameEvent e{EV_NADE_THROW}; e.a = (int8_t)p.id; e.weapon = w; e.pos = start; emit(e);
    p.nextAttack = time + 0.5f;
    if (!p.hasWeapon(w)) {
        // auto-switch to next grenade or best weapon
        for (int i = 0; i < 4; i++) if (p.nades[i]) { p.lastW = p.activeW; p.activeW = nadeFromIndex(i); p.drawEnd = time + 0.6f; return; }
        selectBestWeapon(p);
    } else p.drawEnd = time + 0.6f;
}

static bool solidAt(const Map& m, Vector3 p) {
    if (p.y < 0.03f) return true;
    int x, z; m.worldToTile(p, x, z);
    return m.at(x, z).height > p.y - 0.03f;
}

void World::tickProjectiles() {
    const float dt = TICK_DT;
    for (auto& pr : projectiles) {
        if (!pr.active) continue;
        pr.prevPos = pr.pos;
        float age = time - pr.spawnTime;
        if (!pr.resting) {
            pr.vel.y -= NADE_GRAVITY * dt;
            Vector3 np = pr.pos + pr.vel * dt;
            if (solidAt(map, np)) {
                bool bounced = false, floorHit = false;
                Vector3 t = pr.pos; t.x = np.x;
                if (solidAt(map, t)) { pr.vel.x = -pr.vel.x * 0.45f; pr.vel.z *= 0.75f; bounced = true; } else pr.pos.x = np.x;
                t = pr.pos; t.z = np.z;
                if (solidAt(map, t)) { pr.vel.z = -pr.vel.z * 0.45f; pr.vel.x *= 0.75f; bounced = true; } else pr.pos.z = np.z;
                t = pr.pos; t.y = np.y;
                if (solidAt(map, t)) {
                    if (pr.vel.y < 0) floorHit = true;
                    pr.vel.y = -pr.vel.y * 0.38f;
                    pr.vel.x *= 0.7f; pr.vel.z *= 0.7f;
                    bounced = true;
                } else pr.pos.y = np.y;
                float sp = len3(pr.vel);
                if (bounced && sp > 1.2f && time - pr.lastBounceTime > 0.08f) {
                    pr.lastBounceTime = time;
                    GameEvent e{EV_NADE_BOUNCE}; e.a = (int8_t)pr.owner; e.weapon = pr.type; e.pos = pr.pos; emit(e);
                }
                if (floorHit && pr.type == W_MOLLY) { detonate(pr); continue; }
                if (floorHit && sp < 0.9f) { pr.resting = true; pr.vel = {0, 0, 0}; }
            } else pr.pos = np;
        }
        bool det = false;
        switch (pr.type) {
        case W_FLASH: case W_FRAG: det = age >= 1.6f; break;
        case W_SMOKE: det = (pr.resting && age > 0.8f) || age > 4.0f; break;
        case W_MOLLY: if (age > 2.2f) { pr.active = false; GameEvent e{EV_FIRE_OUT}; e.pos = pr.pos; emit(e); } break;
        default: det = age > 3; break;
        }
        if (det) detonate(pr);
    }
    projectiles.erase(std::remove_if(projectiles.begin(), projectiles.end(), [](const Projectile& p) { return !p.active; }), projectiles.end());
    smokes.erase(std::remove_if(smokes.begin(), smokes.end(), [&](const SmokeCloud& s) { return time > s.end; }), smokes.end());
}

void World::detonate(Projectile& pr) {
    pr.active = false;
    Vector3 c = pr.pos;
    switch (pr.type) {
    case W_FLASH: {
        GameEvent e{EV_FLASH_POP}; e.a = (int8_t)pr.owner; e.pos = c; emit(e);
        for (auto& p : players) {
            if (!p.active || !p.alive) continue;
            Vector3 eye = p.eye();
            float dist = len3(c - eye);
            if (dist > 55) continue;
            if (!map.lineOfSight(eye, c) || smokeBlocks(eye, c)) continue;
            Vector3 to = norm3(c - eye);
            float dp = dot3(to, forwardFromAngles(p.yaw, p.pitch));
            float facing = dp > 0.6f ? 1.0f : (dp > 0.0f ? 0.65f : (dp > -0.5f ? 0.3f : 0.12f));
            float distF = clampf(1.0f - dist / 55.f, 0, 1);
            float dur = 4.8f * facing * (0.35f + 0.65f * distF);
            if (dur < 0.25f) continue;
            if (time + dur > p.flashEnd) { p.flashEnd = time + dur; p.flashDur = dur; }
            GameEvent f{EV_FLASHED}; f.a = (int8_t)pr.owner; f.b = (int8_t)p.id; f.value = (int16_t)(dur * 100); emit(f);
        }
        break;
    }
    case W_FRAG: {
        GameEvent e{EV_FRAG_BOOM}; e.a = (int8_t)pr.owner; e.pos = c; emit(e);
        for (auto& p : players) {
            if (!p.active || !p.alive) continue;
            Vector3 chest = p.pos + V3(0, 1.0f, 0);
            float dist = len3(chest - c);
            if (dist > 8.5f) continue;
            if (!map.lineOfSight(c + V3(0, 0.3f, 0), chest)) continue;
            if (p.id != pr.owner && !isEnemy(pr.owner, p.id) && !friendlyFire) continue;
            float dmg = 98.f * powf(1.0f - dist / 8.5f, 1.3f);
            if (dmg >= 1) applyDamage(p, pr.owner, dmg, HG_CHEST, W_FRAG, 0.5f, c, 0);
        }
        break;
    }
    case W_SMOKE: {
        SmokeCloud s; s.active = true; s.pos = c; s.pos.y = map.groundHeightAt(c, 0.1f, c.y + 0.1f);
        s.start = time; s.end = time + 18.0f;
        smokes.push_back(s);
        for (auto& f : fires) if (f.active && len2xz(f.pos - s.pos) < f.radius + 2.8f) { f.active = false; GameEvent e{EV_FIRE_OUT}; e.pos = f.pos; emit(e); }
        GameEvent e{EV_SMOKE_POP}; e.a = (int8_t)pr.owner; e.pos = s.pos; emit(e);
        break;
    }
    case W_MOLLY: {
        Vector3 g = c; g.y = map.groundHeightAt(c, 0.1f, c.y + 0.1f);
        for (auto& s : smokes)
            if (len2xz(s.pos - g) < 3.8f) { GameEvent e{EV_FIRE_OUT}; e.pos = g; emit(e); return; }
        FireArea f; f.active = true; f.pos = g; f.start = time; f.end = time + 7.0f; f.owner = pr.owner; f.team = pr.team;
        fires.push_back(f);
        GameEvent e{EV_MOLLY_POP}; e.a = (int8_t)pr.owner; e.pos = g; emit(e);
        break;
    }
    default: break;
    }
}

void World::tickFires() {
    for (auto& f : fires) {
        if (!f.active) continue;
        if (time > f.end) { f.active = false; continue; }
        float grow = clampf((time - f.start) / 0.5f, 0.3f, 1.0f);
        for (auto& p : players) {
            if (!p.active || !p.alive) continue;
            if (len2xz(p.pos - f.pos) > f.radius * grow || fabsf(p.pos.y - f.pos.y) > 1.0f) continue;
            if (p.id != f.owner && !isEnemy(f.owner, p.id) && !friendlyFire) continue;
            p.burnAccum += 40.f * TICK_DT;
            if (p.burnAccum >= 8.f) {
                float d = p.burnAccum; p.burnAccum = 0;
                applyDamage(p, f.owner, d, HG_CHEST, W_MOLLY, 1.0f, f.pos, 0);
                GameEvent e{EV_HURT_FIRE}; e.b = (int8_t)p.id; e.pos = p.pos; emit(e);
            }
        }
    }
    fires.erase(std::remove_if(fires.begin(), fires.end(), [](const FireArea& f) { return !f.active; }), fires.end());
}

void World::tickBomb(const UserCmd* cmds) {
    if (bomb.state == BOMB_CARRIED && bomb.carrier >= 0) bomb.pos = players[bomb.carrier].pos;
    if (bomb.state == BOMB_DROPPED && time > bomb.nextBeep) {
        for (auto& p : players) {
            if (!p.active || !p.alive || p.team != TEAM_ATK) continue;
            if (len2xz(p.pos - bomb.pos) < 1.1f && fabsf(p.pos.y - bomb.pos.y) < 1.5f) {
                p.hasBomb = true; bomb.state = BOMB_CARRIED; bomb.carrier = p.id;
                GameEvent e{EV_BOMB_PICKUP}; e.a = (int8_t)p.id; e.pos = p.pos; emit(e);
                break;
            }
        }
    }
    if (bomb.state != BOMB_PLANTED) return;
    float left = bomb.explodeTime - time;
    if (time >= bomb.nextBeep) {
        float frac = clampf(left / bombTimeSec, 0, 1);
        float interval = 0.08f + 0.95f * frac * frac;
        bomb.nextBeep = time + interval;
        GameEvent e{EV_BOMB_BEEP}; e.pos = bomb.pos; e.value = (int16_t)(frac * 1000); emit(e);
    }
    // defusing
    if (bomb.defuser >= 0) {
        Player& d = players[bomb.defuser];
        bool ok = d.alive && (cmds[d.id].buttons & IN_USE) && len2xz(d.pos - bomb.pos) < 1.8f;
        if (!ok) { d.defusing = false; bomb.defuser = -1; }
        else if (time >= bomb.defuseEnd) {
            d.defusing = false;
            bomb.state = BOMB_DEFUSED;
            d.money = std::min(MAX_MONEY, d.money + 300);
            d.defuses++; d.score += 2;
            GameEvent e{EV_BOMB_DEFUSED}; e.a = (int8_t)d.id; e.pos = bomb.pos; emit(e);
            endRound(TEAM_DEF, RE_BOMB_DEFUSED);
            return;
        }
    } else if (phase == PH_LIVE) {
        for (auto& p : players) {
            if (!p.active || !p.alive || p.team != TEAM_DEF || !p.onGround) continue;
            if (!(cmds[p.id].buttons & IN_USE)) continue;
            if (len2xz(p.pos - bomb.pos) > 1.6f || fabsf(p.pos.y - bomb.pos.y) > 1.4f) continue;
            p.defusing = true;
            bomb.defuser = p.id;
            bomb.defuseStart = time;
            bomb.defuseEnd = time + (p.kit ? DEFUSE_TIME_KIT : DEFUSE_TIME);
            p.vel = {0, p.vel.y, 0};
            GameEvent e{EV_DEFUSE_START}; e.a = (int8_t)p.id; e.pos = bomb.pos; e.flags = p.kit; emit(e);
            break;
        }
    }
    if (time >= bomb.explodeTime) {
        bomb.state = BOMB_EXPLODED;
        GameEvent e{EV_BOMB_EXPLODE}; e.pos = bomb.pos; emit(e);
        for (auto& p : players) {
            if (!p.active || !p.alive) continue;
            float d = len3(p.pos - bomb.pos);
            float dmg = 500.f * expf(-(d * d) / (2.f * 10.f * 10.f));
            if (dmg >= 1) applyDamage(p, -1, dmg, HG_CHEST, W_BOMB, 0.5f, bomb.pos, 0);
        }
        endRound(TEAM_ATK, RE_BOMB_EXPLODED);
    }
}

void World::tickDrops() {
    for (auto& d : drops) {
        if (!d.active) continue;
        if (!d.resting) {
            d.vel.y -= GRAVITY * TICK_DT;
            Vector3 np = d.pos + d.vel * TICK_DT;
            int x, z; map.worldToTile(np, x, z);
            float hh = map.at(x, z).height;
            if (hh > d.pos.y + 0.05f) { d.vel.x *= -0.3f; d.vel.z *= -0.3f; np.x = d.pos.x; np.z = d.pos.z; }
            float g = map.groundHeightAt(np, 0.05f, d.pos.y + 0.05f);
            if (np.y <= g + 0.05f) { np.y = g + 0.05f; d.vel = d.vel * 0.4f; d.vel.y = 0; if (len3(d.vel) < 0.3f) d.resting = true; }
            d.pos = np;
        }
        if (time - d.spawnTime < 0.7f) continue;
        for (auto& p : players) {
            if (!p.active || !p.alive) continue;
            if (len2xz(p.pos - d.pos) > 1.0f || fabsf(p.pos.y + 0.3f - d.pos.y) > 1.5f) continue;
            const WeaponDef& wd = weaponDef(d.w);
            int slot = wd.slot == SLOT_PRIMARY ? 0 : 1;
            WeaponId cur = slot == 0 ? p.primary : p.secondary;
            if (cur != W_NONE) continue;
            if (slot == 0) { p.primary = d.w; p.clip[0] = d.clip; p.reserve[0] = d.reserve; }
            else { p.secondary = d.w; p.clip[1] = d.clip; p.reserve[1] = d.reserve; }
            d.active = false;
            GameEvent e{EV_PICKUP}; e.a = (int8_t)p.id; e.weapon = d.w; e.pos = p.pos; emit(e);
            if (slot == 0 && (p.activeW == W_KNIFE || p.activeW == p.secondary)) {
                if (!p.isBot ? g_settings.autoSwitchOnPickup : true) { p.lastW = p.activeW; p.activeW = d.w; p.drawEnd = time + wd.drawTime; p.reloading = false; }
            }
            break;
        }
    }
    drops.erase(std::remove_if(drops.begin(), drops.end(), [](const DroppedWeapon& d) { return !d.active; }), drops.end());
}

void World::tickRound() {
    if (mode == MODE_DEATHMATCH || mode == MODE_PRACTICE || mode == MODE_AIM) {
        for (auto& p : players)
            if (p.active && !p.alive && p.respawnTime > 0 && time >= p.respawnTime) {
                p.respawnTime = 0;
                respawnPlayer(p, mode == MODE_PRACTICE);
                GameEvent e{EV_RESPAWN}; e.a = (int8_t)p.id; e.pos = p.pos; emit(e);
            }
        if (mode == MODE_DEATHMATCH && phase == PH_LIVE) {
            bool limit = false;
            for (auto& p : players) if (p.active && p.kills >= dmKillLimit) limit = true;
            if (time >= dmEndTime || limit) { phase = PH_MATCH_END; emit({EV_MATCH_END}); }
        }
        return;
    }
    switch (phase) {
    case PH_FREEZE:
        if (time >= phaseEnd) {
            phase = PH_LIVE; roundStartTime = time;
            emit({EV_LIVE});
        }
        break;
    case PH_LIVE: {
        int atkA = aliveCount(TEAM_ATK), defA = aliveCount(TEAM_DEF);
        bool hasAtk = teamCount(TEAM_ATK) > 0, hasDef = teamCount(TEAM_DEF) > 0;
        if (bomb.state != BOMB_PLANTED) {
            if (hasAtk && atkA == 0) endRound(TEAM_DEF, RE_ELIMINATION);
            else if (hasDef && defA == 0) endRound(TEAM_ATK, RE_ELIMINATION);
            else if (time >= roundStartTime + roundTimeSec) endRound(TEAM_DEF, RE_TIME);
        } else {
            if (hasDef && defA == 0) endRound(TEAM_ATK, RE_ELIMINATION);
        }
        break;
    }
    case PH_ROUND_END:
        if (time >= phaseEnd) {
            if (matchWinnerSquad >= 0) {
                phase = PH_MATCH_END;
                GameEvent e{EV_MATCH_END}; e.value = (int16_t)matchWinnerSquad; emit(e);
            } else {
                if (isHalftimeNext()) halftimeSwap();
                startRound();
            }
        }
        break;
    default: break;
    }
}

void World::recordHistory() {
    for (auto& p : players) {
        if (!p.active) continue;
        p.hist[tick % HISTORY] = {p.pos, p.duck, p.yaw, p.alive};
    }
}

void World::tickPlayer(Player& p, const UserCmd& c) {
    if (!p.alive) return;
    p.yaw = angleNorm(c.yaw);
    p.pitch = clampf(c.pitch, -89.f, 89.f);
    bool frozen = (mode == MODE_COMPETITIVE && phase == PH_FREEZE) || p.planting || p.defusing || phase == PH_MATCH_END;
    UserCmd cc = c;
    if (p.planting || p.defusing) { cc.buttons &= ~(IN_JUMP); }
    movePlayer(p, cc, frozen);
    weaponThink(p, cc);
}

void World::step(const UserCmd* cmds) {
    tick++;
    time = tick * TICK_DT;
    for (auto& p : players) if (p.active) { p.prevPos = p.pos; p.prevDuck = p.duck; }
    for (auto& p : players) if (p.active) tickPlayer(p, cmds[p.id]);
    tickProjectiles();
    tickFires();
    tickBomb(cmds);
    tickDrops();
    tickRound();
    recordHistory();
}

} // namespace ax
