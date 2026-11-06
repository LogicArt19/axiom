#include "sim.h"

namespace ax {

static const Color kPlayerColors[MAX_PLAYERS] = {
    {60, 150, 255, 255}, {255, 90, 70, 255}, {80, 220, 140, 255}, {255, 200, 60, 255}, {170, 110, 255, 255}, {255, 130, 200, 255}};
static const char* kPlayerColorNames[MAX_PLAYERS] = {"Bleu", "Rouge", "Vert", "Jaune", "Violet", "Rose"};
Color playerColor(int i) { return (i >= 0 && i < MAX_PLAYERS) ? kPlayerColors[i] : Color{170, 160, 140, 255}; }
const char* playerColorName(int i) { return (i >= 0 && i < MAX_PLAYERS) ? kPlayerColorNames[i] : "Neutre"; }
Color teamColor(int team) { return team == 0 ? Color{60, 150, 255, 255} : team == 1 ? Color{255, 90, 70, 255} : Color{170, 160, 140, 255}; }

static bool isMobile(const Entity& e) { return e.kind == K_UNIT || e.kind == K_HERO || e.kind == K_MINION || e.kind == K_NEUTRAL; }
static bool isStructure(const Entity& e) { return e.kind == K_BUILDING || e.kind == K_TOWER; }

// ---------------------------------------------------------------------------
void World::setup(int ppt, int mid) {
    (void)ppt;
    mapId = std::clamp(mid, 0, MAP_COUNT - 1);
    map.generate(mapId);
    ents.clear();
    ents.reserve(24000);          // entity references must stay valid while units spawn mid-tick
    ents.resize(1);
    geyserRespawn.clear();
    for (auto& b : beaconHeld) b = 0;
    projs.clear(); zones.clear(); events.clear(); pending.clear();
    for (auto& p : players) p = PlayerState{};
    for (int t = 0; t < 2; t++) { vis[t].assign(Map::N * Map::N, 0); explored[t].assign(Map::N * Map::N, 0); }
    tick = 0; time = 0; phase = PH_LOBBY; winner = -1; nextWave = 25; waveCount = 0;
    titanRespawn = 0; titanBuffUntil[0] = titanBuffUntil[1] = 0;
    for (auto& c : campRespawn) c = 0;
    grid.assign(GRID * GRID, {});
    rng = Rng((uint32_t)(nowSeconds() * 1e6) ^ 0x51ED);
}

int World::addPlayer(const char* name, int team, UnitType hero, bool ai, int skill) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].active) continue;
        PlayerState& p = players[i];
        p = PlayerState{};
        p.active = true; p.team = (uint8_t)team; p.hero = hero; p.isAI = ai; p.aiSkill = skill;
        snprintf(p.name, sizeof p.name, "%s", name);
        return i;
    }
    return -1;
}

uint16_t World::spawn(UnitType t, int owner, int team, Vector2 pos) {
    const UnitDef& d = unitDef(t);
    Entity e;
    e.id = (uint16_t)ents.size();
    e.alive = true; e.kind = d.kind; e.type = t; e.owner = (int8_t)owner; e.team = (uint8_t)team;
    e.pos = e.prevPos = pos; e.radius = d.radius;
    e.hp = e.maxHp = d.hp;
    e.maxMana = e.mana = d.manaMax;
    e.home = pos;
    e.facing = team == 0 ? -0.785f : 2.356f;
    if (ents.size() >= ents.capacity() || ents.size() >= 65000) return 0;
    if (t == R_CRYSTAL) e.amount = CRYSTAL_AMOUNT;
    if (t == R_GEYSER) e.amount = GEYSER_AMOUNT;
    ents.push_back(e);
    if (isStructure(e) || e.kind == K_RESOURCE) map.setBlocked(pos, std::max(1, d.footprint), e.kind == K_RESOURCE ? 3 : 2);
    return e.id;
}

void World::start() {
    // structures and resources
    for (int t = 0; t < 2; t++) core[t] = spawn(B_CORE, -1, t, map.base[t]);
    for (auto& x : map.expansions) {
        for (auto& c : x.crystals) {
            uint16_t id = spawn(R_CRYSTAL, -1, TEAM_NEUTRAL, c);
            if (id && x.rich) { ents[id].rich = true; ents[id].amount = CRYSTAL_RICH_AMOUNT; }
        }
        for (auto& g : x.geysers) spawn(R_GEYSER, -1, TEAM_NEUTRAL, g);
    }
    for (size_t c = 0; c < map.camps.size() && c < 16; c++)
        for (int k = 0; k < 2; k++) {
            uint16_t id = spawn(U_GOLEM, -1, TEAM_NEUTRAL, map.camps[c] + V2(k * 2.4f - 1.2f, 0));
            if (id) ents[id].lane = (int8_t)c;
        }
    spawn(U_TITAN, -1, TEAM_NEUTRAL, map.titanPos);
    // players: hero in front of the core, five drones already mining
    int perTeam[2] = {0, 0};
    for (int i = 0; i < MAX_PLAYERS; i++) {
        PlayerState& p = players[i];
        if (!p.active) continue;
        int t = p.team;
        const Expansion& home = map.expansions[t];
        Vector2 toRamp = norm2(map.ramp[t] - map.base[t]);
        Vector2 side = {-toRamp.y, toRamp.x};
        Vector2 hp = map.base[t] + toRamp * 8 + side * ((perTeam[t] - 1) * 3.0f);
        p.heroId = spawn(p.hero, i, t, map.nearestWalkable(hp));
        p.gold = 150; p.flux = 0;
        int k0 = perTeam[t]++;
        Vector2 mside = {-home.dir.y, home.dir.x};
        for (int k = 0; k < 5; k++) {
            Vector2 sp = map.nearestWalkable(map.base[t] + home.dir * 5.0f + mside * ((k0 - 1) * 4.0f + (k - 2) * 0.9f));
            uint16_t w = spawn(U_DRONE, i, t, sp);
            if (!w || home.crystals.empty()) continue;
            Vector2 cp = home.crystals[(k0 * 3 + k) % home.crystals.size()];
            for (auto& r : ents) if (r.alive && r.type == R_CRYSTAL && dist2(r.pos, cp) < 0.5f) { ents[w].order = {O_GATHER, r.pos, r.id, 0}; ents[w].node = r.id; ents[w].home = r.pos; }
        }
    }
    phase = PH_PLAYING;
    time = 0;
    nextWave = 1e9f;
    updateVision();
    emit({EV_GAME_START});
}

// ---------------------------------------------------------------------------
// queries
// ---------------------------------------------------------------------------
void World::rebuildGrid() {
    for (auto& g : grid) g.clear();
    for (auto& e : ents) {
        if (!e.alive || e.kind == K_RESOURCE) continue;
        int gx = std::clamp((int)(e.pos.x / 8), 0, GRID - 1), gz = std::clamp((int)(e.pos.y / 8), 0, GRID - 1);
        grid[gz * GRID + gx].push_back(e.id);
    }
}

std::vector<uint16_t> World::unitsInRadius(Vector2 p, float r, int team, bool enemiesOf) const {
    std::vector<uint16_t> out;
    int x0 = std::clamp((int)((p.x - r - 5) / 8), 0, GRID - 1), x1 = std::clamp((int)((p.x + r + 5) / 8), 0, GRID - 1);
    int z0 = std::clamp((int)((p.y - r - 5) / 8), 0, GRID - 1), z1 = std::clamp((int)((p.y + r + 5) / 8), 0, GRID - 1);
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++)
            for (uint16_t id : grid[z * GRID + x]) {
                const Entity& e = ents[id];
                if (!e.alive) continue;
                if (enemiesOf ? e.team == team : e.team != team) continue;
                if (dist2(e.pos, p) - e.radius > r) continue;
                out.push_back(id);
            }
    return out;
}

bool World::visibleTo(int team, const Entity& e) const {
    if (team < 0 || team > 1) return true;
    if (e.team == team || e.kind == K_RESOURCE) return true;
    if (time < e.stealthUntil) {
        bool detected = false;
        for (auto& d : ents)
            if (d.alive && d.team == team && (d.type == B_WATCHTOWER || d.type == T_TOWER || d.type == B_CORE) && !d.constructing && dist2(d.pos, e.pos) < 9.5f) { detected = true; break; }
        if (!detected) return false;
    }
    return cellVisible(team, e.pos);
}

int World::supplyUsed(int player) const {
    int s = 0;
    for (auto& e : ents) {
        if (!e.alive) continue;
        if (e.owner == player && (e.kind == K_UNIT) && !e.summoned) s += unitDef(e.type).supply;
        if (isStructure(e))
            for (size_t i = 0; i < e.prod.size(); i++)
                if ((e.owner == player) || (e.type == B_CORE && i < e.queue.size() && e.queue[i].a == player)) s += unitDef(e.prod[i]).supply;
    }
    return s;
}

int World::supplyMax(int player) const {
    int s = 12;
    for (auto& e : ents) if (e.alive && e.owner == player && (e.type == B_RELAY || e.type == B_HUB) && !e.constructing) s += 10;
    return std::min(s, MAX_SUPPLY);
}

bool World::hasBuilding(int player, UnitType t) const {
    for (auto& e : ents) if (e.alive && e.owner == player && e.type == t && !e.constructing) return true;
    return false;
}

int World::countUnits(int player, UnitType t, bool includeQueued) const {
    int n = 0;
    for (auto& e : ents) {
        if (!e.alive) continue;
        if (e.owner == player && e.type == t && !e.summoned) n++;
        if (includeQueued && isStructure(e))
            for (size_t i = 0; i < e.prod.size(); i++)
                if (e.prod[i] == t && (e.owner == player || (e.type == B_CORE && i < e.queue.size() && e.queue[i].a == player))) n++;
    }
    return n;
}

bool World::heroNearShop(int player) const {
    const Entity* h = get(players[player].heroId);
    return h && dist2(h->pos, map.base[players[player].team]) < map.baseRadius;
}

const Entity* World::dropOff(const Entity& w) const {
    const Entity* best = nullptr;
    float bd = 1e9f;
    for (auto& e : ents) {
        if (!e.alive || e.team != w.team || e.constructing || (e.type != B_CORE && e.type != B_HUB)) continue;
        float d = dist2(e.pos, w.pos);
        if (d < bd) { bd = d; best = &e; }
    }
    return best;
}

Vector2 World::buildSite(UnitType t, Vector2 p) const {
    const UnitDef& d = unitDef(t);
    if (t == B_EXTRACTOR) {
        const Entity* best = nullptr;
        float bd = 6;
        for (auto& e : ents) if (e.alive && e.type == R_GEYSER) { float dd = dist2(e.pos, p); if (dd < bd) { bd = dd; best = &e; } }
        if (best) return best->pos;
    }
    if (t == B_HUB) {
        // snap onto the expansion spot when close (like the usual RTS placement helpers)
        for (auto& x : map.expansions) if (dist2(x.hub, p) < 4.5f) return x.hub;
    }
    return map.snapBuilding(p, d.footprint);
}

int World::canBuild(UnitType t, Vector2 site, int team) const {
    (void)team;
    const UnitDef& d = unitDef(t);
    if (t == B_EXTRACTOR) {
        for (auto& e : ents) if (e.alive && e.type == R_GEYSER && dist2(e.pos, site) < 0.5f) return -1;
        return 3;
    }
    if (!map.canPlace(site, d.footprint)) return 0;
    if (t == B_HUB) {
        for (auto& e : ents) if (e.alive && (e.type == B_CORE || e.type == B_HUB) && dist2(e.pos, site) < 22) return 4;
        for (auto& e : ents) if (e.alive && e.kind == K_RESOURCE && dist2(e.pos, site) < 6.6f + e.radius * 0.5f) return 2;
        return -1;
    }
    if (map.nearResources(site, d.footprint, 1.5f)) return 2;
    return -1;
}

static int itemCount(const Entity& e, ItemId id) { int n = 0; for (int i = 0; i < 4; i++) if (e.items[i] == id) n++; return n; }

float World::attackSpeedMult(const Entity& e) const {
    float m = 1;
    if (time < e.asUntil) m += e.asAmt;
    for (int i = 0; i < 4; i++) m += itemDef(e.items[i]).attackSpeed;
    return m;
}

float World::moveSpeed(const Entity& e) const {
    float s = unitDef(e.type).speed;
    float m = 1;
    if (time < e.hasteUntil) m += e.hasteAmt;
    for (int i = 0; i < 4; i++) m += itemDef(e.items[i]).moveSpeed;
    if (time < e.slowUntil) m *= (1 - e.slowAmt);
    if (e.carry > 0) m *= 0.85f;
    return s * m;
}

float World::armorOf(const Entity& e) const {
    float a = unitDef(e.type).armor;
    for (int i = 0; i < 4; i++) a += itemDef(e.items[i]).armor;
    if (e.owner >= 0 && e.kind == K_UNIT) a += players[e.owner].upgrades[UP_ARMOR] * 6.f;
    if (e.kind == K_HERO) a += (e.level - 1) * 2.f;
    return a;
}

float World::abilityPower(const Entity& e) const { return 1.0f + 0.4f * itemCount(e, I_SCEPTER); }
float World::cdr(const Entity& e) const { return std::min(0.4f, 0.2f * itemCount(e, I_FOCUS)); }

float World::attackDamage(const Entity& e) const {
    const UnitDef& d = unitDef(e.type);
    float dmg = d.dmg;
    if (e.kind == K_HERO) dmg += d.dmgPerLevel * (e.level - 1);
    for (int i = 0; i < 4; i++) dmg += itemDef(e.items[i]).dmg;
    float mult = 1;
    if (e.owner >= 0 && e.kind == K_UNIT) mult += players[e.owner].upgrades[UP_WEAPONS] * 0.12f;
    if (time < e.dmgUntil) mult += e.dmgAmt;
    if (e.team < 2 && time < titanBuffUntil[e.team]) mult += 0.2f;
    if (e.kind == K_MINION) mult += waveCount * 0.015f;
    if (e.owner >= 0 && e.kind == K_UNIT) {
        const Entity* h = get(players[e.owner].heroId);
        if (h && itemCount(*h, I_AEGIS) && dist2(h->pos, e.pos) < 12) mult += 0.2f;
    }
    return dmg * mult;
}

bool World::abilityReady(const Entity& h, int slot) const {
    const AbilityDef& a = abilityDef(h.type, slot);
    return h.level >= a.unlockLevel && h.abilCd[slot] <= 0 && h.mana >= a.mana && time >= h.stunUntil;
}

// ---------------------------------------------------------------------------
// commands
// ---------------------------------------------------------------------------
static void setOrder(Entity& e, const Order& o, bool queue) {
    if (queue && e.order.type != O_IDLE) { if (e.queue.size() < 16) e.queue.push_back(o); return; }
    e.queue.clear();
    e.order = o;
    e.arrived = false;
    e.stuck = 0;
}

void World::applyCommand(const Command& c) {
    if (c.player >= MAX_PLAYERS || !players[c.player].active) return;
    PlayerState& P = players[c.player];
    if (phase == PH_LOBBY) {
        if (c.type == CMD_PICK_HERO && isHeroType(c.a)) P.hero = (UnitType)c.a;
        if (c.type == CMD_READY) P.ready = true;
        return;
    }
    if (phase != PH_PLAYING) return;
    P.actions++;
    std::vector<Entity*> us;
    for (uint16_t id : c.units) {
        Entity* e = get(id);
        if (e && e->owner == c.player && (isMobile(*e) || isStructure(*e))) us.push_back(e);
    }
    std::vector<Entity*> mob;
    for (auto* e : us) if (isMobile(*e)) mob.push_back(e);
    uint8_t stopR = (uint8_t)std::min(250.f, (0.8f + sqrtf((float)mob.size()) * 0.75f) * 10);
    switch (c.type) {
    case CMD_MOVE: for (auto* e : mob) setOrder(*e, {O_MOVE, c.pt, 0, stopR}, c.queue); break;
    case CMD_ATTACK_MOVE: for (auto* e : mob) setOrder(*e, {O_ATTACK_MOVE, c.pt, 0, stopR}, c.queue); break;
    case CMD_ATTACK: {
        Entity* t = get(c.target);
        if (!t) break;
        for (auto* e : mob) setOrder(*e, {O_ATTACK, t->pos, t->id, 0}, c.queue);
        break;
    }
    case CMD_STOP: for (auto* e : mob) { e->queue.clear(); e->order = {O_IDLE}; } break;
    case CMD_HOLD: for (auto* e : mob) { e->queue.clear(); e->order = {O_HOLD, e->pos}; } break;
    case CMD_GATHER: case CMD_SMART: {
        Entity* t = get(c.target);
        for (auto* e : us) {
            if (isStructure(*e)) { e->rally = c.pt; e->hasRally = true; continue; }
            bool mineable = t && (t->type == R_CRYSTAL || (t->type == B_EXTRACTOR && t->team == e->team && !t->constructing));
            if (mineable && e->type == U_DRONE) { setOrder(*e, {O_GATHER, t->pos, t->id, 0}, c.queue); e->node = t->id; e->home = t->pos; }
            else if (t && t->team != e->team && t->kind != K_RESOURCE && visibleTo(e->team, *t)) setOrder(*e, {O_ATTACK, t->pos, t->id, 0}, c.queue);
            else if (t && t->team == e->team && (t->type == B_CORE || t->type == B_HUB) && e->type == U_DRONE && e->carry > 0) setOrder(*e, {O_RETURN, t->pos, t->id, 0}, c.queue);
            else setOrder(*e, {O_MOVE, c.pt, 0, stopR}, c.queue);
        }
        break;
    }
    case CMD_RALLY: for (auto* e : us) if (isStructure(*e)) { e->rally = c.pt; e->hasRally = true; } break;
    case CMD_BUILD: {
        UnitType bt = (UnitType)c.a;
        const UnitDef& d = unitDef(bt);
        if (!isBuildingType(bt) || bt == B_CORE || bt == T_TOWER) break;
        if (std::find(buildList().begin(), buildList().end(), bt) == buildList().end()) break;
        Entity* worker = nullptr;
        float best = 1e9f;
        for (auto* e : mob) if (e->type == U_DRONE) { float dd = dist2(e->pos, c.pt); if (dd < best) { best = dd; worker = e; } }
        if (!worker) break;
        Vector2 site = buildSite(bt, c.pt);
        if (d.reqTech != U_NONE && !hasBuilding(c.player, d.reqTech)) { emit({EV_CANT_BUILD, -1, -1, 1, (int8_t)c.player}); break; }
        int why = canBuild(bt, site, P.team);
        if (why >= 0) { emit({EV_CANT_BUILD, -1, -1, (uint8_t)why, (int8_t)c.player, site}); break; }
        if (P.gold < d.cost) { emit({EV_NO_GOLD, -1, -1, 0, (int8_t)c.player}); break; }
        if (P.flux < d.flux) { emit({EV_NO_GOLD, -1, -1, 1, (int8_t)c.player}); break; }
        P.gold -= d.cost; P.flux -= d.flux;
        Order o{O_BUILD, site, 0, (uint8_t)bt};
        setOrder(*worker, o, c.queue);
        break;
    }
    case CMD_TRAIN: {
        Entity* b = get(c.target);
        UnitType ut = (UnitType)c.a;
        if (!b || b->constructing || b->team != P.team) break;
        if (b->owner != c.player && !(b->type == B_CORE)) break;
        auto& tl = trainList(b->type);
        if (std::find(tl.begin(), tl.end(), ut) == tl.end()) break;
        const UnitDef& d = unitDef(ut);
        if (d.reqTech != U_NONE && !hasBuilding(c.player, d.reqTech)) { emit({EV_CANT_BUILD, -1, -1, 1, (int8_t)c.player}); break; }
        if (b->prod.size() >= 5) break;
        if (P.gold < d.cost) { emit({EV_NO_GOLD, -1, -1, 0, (int8_t)c.player}); break; }
        if (P.flux < d.flux) { emit({EV_NO_GOLD, -1, -1, 1, (int8_t)c.player}); break; }
        if (supplyUsed(c.player) + d.supply > supplyMax(c.player)) { emit({EV_NO_SUPPLY, -1, -1, 0, (int8_t)c.player}); break; }
        P.gold -= d.cost; P.flux -= d.flux;
        b->prod.push_back(ut);
        b->queue.push_back({O_IDLE, {}, 0, c.player});     // remembers who paid (shared Core)
        break;
    }
    case CMD_CANCEL: {
        Entity* b = get(c.target);
        if (!b || b->team != P.team) break;
        if (b->constructing && b->owner == c.player) {
            P.gold += unitDef(b->type).cost * 3 / 4; P.flux += unitDef(b->type).flux * 3 / 4;
            b->hp = 0;
            kill(*b, nullptr, -1);
            break;
        }
        for (int i = (int)b->prod.size() - 1; i >= 0; i--) {
            int payer = i < (int)b->queue.size() ? b->queue[i].a : b->owner;
            if (payer != c.player) continue;
            P.gold += unitDef(b->prod[i]).cost; P.flux += unitDef(b->prod[i]).flux;
            b->prod.erase(b->prod.begin() + i);
            if (i < (int)b->queue.size()) b->queue.erase(b->queue.begin() + i);
            if (i == 0) b->prodProgress = 0;
            break;
        }
        if (b->research >= 0 && b->owner == c.player && b->prod.empty()) {
            P.gold += upgradeCost(b->research, P.upgrades[b->research]); P.flux += upgradeFlux(b->research, P.upgrades[b->research]);
            b->research = -1; b->researchProgress = 0;
        }
        break;
    }
    case CMD_RESEARCH: {
        Entity* b = get(c.target);
        if (!b || b->type != B_FORGE || b->owner != c.player || b->constructing || b->research >= 0) break;
        int up = c.a;
        if (up >= UP_COUNT || P.upgrades[up] >= UPGRADE_MAX) break;
        for (auto& e : ents) if (e.alive && e.owner == c.player && e.type == B_FORGE && e.research == up) return;
        int cost = upgradeCost(up, P.upgrades[up]);
        int fcost = upgradeFlux(up, P.upgrades[up]);
        if (P.gold < cost) { emit({EV_NO_GOLD, -1, -1, 0, (int8_t)c.player}); break; }
        if (P.flux < fcost) { emit({EV_NO_GOLD, -1, -1, 1, (int8_t)c.player}); break; }
        P.gold -= cost; P.flux -= fcost;
        b->research = (int8_t)up; b->researchProgress = 0;
        break;
    }
    case CMD_ABILITY: {
        Entity* h = get(P.heroId);
        if (!h || c.a > 3) break;
        const AbilityDef& a = abilityDef(h->type, c.a);
        if (!abilityReady(*h, c.a)) break;
        Order o{O_CAST, c.pt, c.target, c.a};
        bool inRange = true;
        bool mobility = (h->type == H_VANGUARD && c.a == 0) || (h->type == H_PYRA && c.a == 2) || (h->type == H_HAWKE && c.a == 2);
        if (a.tm == TM_POINT) inRange = mobility || dist2(h->pos, c.pt) <= a.range + 0.5f;   // dashes/blinks clamp
        if (a.tm == TM_ENEMY || a.tm == TM_ALLY) { Entity* t = get(c.target); inRange = t && inRangeDist(*h, *t) <= a.range; }
        if (inRange) { castAbility(*h, c.a, c.pt, c.target); }
        else { Order prev = h->order; setOrder(*h, o, false); if (prev.type != O_CAST && prev.type != O_IDLE) h->queue.push_back(prev); }
        break;
    }
    case CMD_BUY: {
        Entity* h = get(P.heroId);
        int it = c.a;
        if (it <= I_NONE || it >= I_COUNT || !heroNearShop(c.player)) break;
        const ItemDef& d = itemDef(it);
        if (P.gold < d.cost) { emit({EV_NO_GOLD, -1, -1, 0, (int8_t)c.player}); break; }
        if (!h) break;
        if (it == I_POTION) { P.gold -= d.cost; h->regenUntil = time + 8; h->regenRate = 220.f / 8; emit({EV_ITEM, (int16_t)h->id, -1, (uint8_t)it, (int8_t)c.player}); break; }
        for (int s = 0; s < 4; s++)
            if (h->items[s] == I_NONE) {
                P.gold -= d.cost;
                h->items[s] = (uint8_t)it;
                h->hp += d.hp; h->mana += d.mana;
                emit({EV_ITEM, (int16_t)h->id, -1, (uint8_t)it, (int8_t)c.player});
                break;
            }
        break;
    }
    case CMD_SELL: {
        Entity* h = get(P.heroId);
        if (!h || c.a > 3 || !h->items[c.a] || !heroNearShop(c.player)) break;
        P.gold += itemDef(h->items[c.a]).cost * 6 / 10;
        h->items[c.a] = I_NONE;
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------------------
// movement & combat
// ---------------------------------------------------------------------------
void World::moveToward(Entity& e, Vector2 goal, float stopDist) {
    float d = dist2(e.pos, goal);
    if (d <= stopDist) { e.arrived = true; return; }
    if (time < e.stunUntil) return;
    Vector2 dir = d < 3.0f ? norm2(goal - e.pos) : map.flowDirection(e.pos, goal, std::max(stopDist, 1.5f));
    float sp = moveSpeed(e) * TICK_DT;
    if (sp > d - stopDist + 0.05f) sp = std::max(0.f, d - stopDist + 0.05f);
    Vector2 np = e.pos + dir * sp;
    if (!map.walkable(np)) {
        Vector2 a = {np.x, e.pos.y}, b = {e.pos.x, np.y};
        if (map.walkable(a)) np = a; else if (map.walkable(b)) np = b; else np = e.pos;
    }
    if (dist2(np, e.pos) < sp * 0.2f) e.stuck += TICK_DT; else e.stuck = std::max(0.f, e.stuck - TICK_DT);
    e.pos = np;
    if (len2(dir) > 0) e.facing = angleOf(dir);
}

uint16_t World::acquire(const Entity& e, float range) const {
    uint16_t best = 0;
    float bestScore = 1e9f;
    auto cand = unitsInRadius(e.pos, range, e.team, true);
    for (uint16_t id : cand) {
        const Entity& t = ents[id];
        if (t.kind == K_RESOURCE) continue;
        if (t.team == TEAM_NEUTRAL && e.kind != K_NEUTRAL) continue;   // camps are only fought on purpose
        if (!visibleTo(e.team, t)) continue;
        float s = dist2(e.pos, t.pos);
        if (isStructure(t)) s += 7;
        if (t.kind == K_UNIT && t.type == U_DRONE) s += 3;
        if (s < bestScore) { bestScore = s; best = id; }
    }
    return best;
}

void World::doAttack(Entity& e, Entity& t) {
    if (e.attackCd > 0 || time < e.stunUntil) return;
    const UnitDef& d = unitDef(e.type);
    e.attackCd = d.cooldown / attackSpeedMult(e);
    e.attackAnim = time;
    e.facing = angleOf(t.pos - e.pos);
    if (e.stealthUntil > time) e.stealthUntil = 0;
    float dmg = attackDamage(e);
    const UnitDef& td = unitDef(t.type);
    float mult = td.ac == AC_LIGHT ? d.vsLight : td.ac == AC_STRUCTURE ? d.vsStructure : d.vsHeavy;
    dmg *= mult;
    if (e.empowered > 0) { dmg *= 1 + e.empowered; e.empowered = 0; }
    GameEvent ev{EV_ATTACK, (int16_t)e.id, (int16_t)t.id, (uint8_t)e.type, e.owner, e.pos, t.pos};
    emit(ev);
    if (d.ranged && d.projSpeed > 0) {
        Projectile p;
        p.pos = e.pos; p.src = e.id; p.target = t.id; p.owner = e.owner; p.team = e.team; p.srcType = e.type;
        p.speed = d.projSpeed; p.dmg = dmg; p.splash = d.splash;
        p.height = isStructure(e) ? 4.0f : 1.2f;
        p.visual = e.kind == K_HERO ? 4 : (isStructure(e) ? 5 : 0);
        projs.push_back(p);
    } else {
        damage(t, dmg, &e, false);
        if (e.poisonCharges > 0 && t.alive) { e.poisonCharges--; t.poisonUntil = time + 4; t.poisonDps = 12; t.lastAttackerOwner = e.owner; }
    }
}

bool World::chaseAndAttack(Entity& e, Entity& t) {
    float range = unitDef(e.type).range;
    float gap = inRangeDist(e, t);
    if (gap <= range) {
        e.facing = angleOf(t.pos - e.pos);
        doAttack(e, t);
        return true;
    }
    moveToward(e, t.pos, range + e.radius + t.radius - 0.15f);
    return false;
}

void World::heal(Entity& t, float amount) {
    if (!t.alive || clientSide) return;
    float before = t.hp;
    t.hp = std::min(t.maxHp, t.hp + amount);
    if (t.hp - before > 20) emit({EV_HEAL, (int16_t)t.id, -1, 0, t.owner, t.pos, {}, (int16_t)(t.hp - before)});
}

void World::damage(Entity& t, float amount, const Entity* src, bool isAbility, int srcOwnerOverride) {
    if (!t.alive || clientSide || t.kind == K_RESOURCE) return;
    if (t.constructing) amount *= 1.5f;
    if (time > 1500 && isStructure(t)) amount *= 1.0f + std::min(1.5f, (time - 1500) / 300.f);   // sudden death: no endless stalemates
    if (time < t.vulnUntil) amount *= 1 + t.vulnAmt;
    float armor = armorOf(t);
    if (isAbility) armor *= 0.6f;
    amount *= 100.f / (100.f + std::max(0.f, armor));
    int srcOwner = srcOwnerOverride >= 0 ? srcOwnerOverride : (src ? src->owner : -1);
    if (t.shield > 0 && time < t.shieldUntil) {
        float a = std::min(t.shield, amount);
        t.shield -= a; amount -= a;
    }
    t.hp -= amount;
    t.lastHurt = time;
    if (srcOwner >= 0) { t.lastAttackerOwner = (int8_t)srcOwner; if (t.kind == K_HERO) t.lastHeroDamage[srcOwner] = time; }
    if (src && src->kind == K_HERO && !isAbility) {
        Entity* s = get(src->id);
        if (s) { int n = itemCount(*s, I_VAMP); if (n) heal(*s, amount * 0.15f * n); }
    }
    if (src && t.kind == K_NEUTRAL && src->team != TEAM_NEUTRAL) {
        t.target = src->id;
        for (auto id : unitsInRadius(t.pos, 8, TEAM_NEUTRAL, false)) {
            Entity& m = ents[id];
            if (m.kind == K_NEUTRAL && m.target == 0) m.target = src->id;
        }
    }
    if (src && t.owner >= 0 && (t.kind == K_UNIT || isStructure(t))) {
        static float lastWarn[MAX_PLAYERS] = {0};
        if (time - lastWarn[t.owner] > 10 || time < lastWarn[t.owner]) { lastWarn[t.owner] = time; emit({EV_UNDER_ATTACK, (int16_t)t.id, -1, 0, t.owner, t.pos}); }
    }
    if (t.kind == K_HERO && src && src->kind == K_HERO) {
        // towers defend heroes under attack
        for (auto& tw : ents)
            if (tw.alive && (tw.type == T_TOWER || tw.type == B_CORE) && tw.team == t.team && dist2(tw.pos, src->pos) <= unitDef(tw.type).range + 2) tw.target = src->id;
    }
    GameEvent ev{EV_HIT, (int16_t)t.id, src ? (int16_t)src->id : (int16_t)-1, (uint8_t)isAbility, (int8_t)srcOwner, t.pos, {}, (int16_t)amount};
    emit(ev);
    if (t.hp <= 0) kill(t, src, srcOwner);
}

void World::giveXp(int team, Vector2 at, int xp) {
    if (team < 0 || team > 1) return;
    std::vector<Entity*> hs;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!players[i].active || players[i].team != team) continue;
        Entity* h = get(players[i].heroId);
        if (h && dist2(h->pos, at) < 24) hs.push_back(h);
    }
    if (hs.empty()) return;
    int share = (int)(xp * 0.6f * (hs.size() > 1 ? 1.3f : 1.0f) / hs.size());
    for (auto* h : hs) {
        h->xp += share;
        while (h->level < MAX_LEVEL && h->xp >= XP_PER_LEVEL[h->level]) {
            h->level++;
            emit({EV_LEVELUP, (int16_t)h->id, -1, (uint8_t)h->level, h->owner, h->pos});
        }
    }
}

void World::kill(Entity& t, const Entity* src, int killerOwner) {
    if (!t.alive) return;
    t.alive = false;
    t.hp = 0;
    const UnitDef& d = unitDef(t.type);
    int killerTeam = killerOwner >= 0 ? players[killerOwner].team : (src ? src->team : -1);
    GameEvent ev{EV_DEATH, (int16_t)t.id, src ? (int16_t)src->id : (int16_t)-1, (uint8_t)t.type, (int8_t)killerOwner, t.pos};
    ev.value = t.owner;
    emit(ev);
    if (t.owner >= 0) players[t.owner].unitsLost++;
    if (killerOwner >= 0 && killerTeam != t.team) {
        PlayerState& K = players[killerOwner];
        K.unitsKilled++;
        int bounty = d.bounty;
        bool byHero = src && src->kind == K_HERO;
        if (t.kind == K_MINION && !byHero) bounty = bounty / 2;
        if (t.kind == K_HERO) bounty = 250 + 20 * t.level;
        K.gold += bounty; K.goldEarned += bounty;
        if (byHero && t.kind != K_HERO) { Entity* h = get(src->id); if (h) h->lastHits++; }
    }
    if (t.kind == K_HERO) {
        t.deaths++;
        t.respawnAt = time + 5 + t.level * 2.4f;
        Entity* kh = killerOwner >= 0 ? get(players[killerOwner].heroId) : nullptr;
        if (kh && kh->team != t.team) kh->kills++;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!players[i].active || i == killerOwner || players[i].team == t.team) continue;
            if (time - t.lastHeroDamage[i] < 10) {
                Entity* ah = get(players[i].heroId);
                if (ah) ah->assists++;
                players[i].gold += 100; players[i].goldEarned += 100;
            }
        }
        emit({EV_HERO_KILL, (int16_t)t.id, src ? (int16_t)src->id : (int16_t)-1, (uint8_t)t.type, (int8_t)killerOwner, t.pos});
    }
    if (killerTeam >= 0 && killerTeam != t.team) giveXp(killerTeam, t.pos, d.xp + (t.kind == K_HERO ? 60 * t.level : 0));
    if (t.type == U_TITAN && killerTeam >= 0 && killerTeam < 2) {
        titanBuffUntil[killerTeam] = time + 120;
        titanRespawn = time + 240;
        for (int i = 0; i < MAX_PLAYERS; i++) if (players[i].active && players[i].team == killerTeam) { players[i].gold += 150; players[i].goldEarned += 150; }
        emit({EV_TITAN_SLAIN, (int16_t)t.id, -1, (uint8_t)killerTeam, (int8_t)killerOwner, t.pos});
    }
    if (t.kind == K_NEUTRAL && t.type == U_GOLEM && t.lane >= 0) {
        bool any = false;
        for (auto& e : ents) if (e.alive && e.type == U_GOLEM && e.lane == t.lane) any = true;
        if (!any) campRespawn[t.lane] = time + 75;
    }
    if (t.kind == K_RESOURCE) map.setBlocked(t.pos, std::max(1, d.footprint), 0);
    if (t.type == B_EXTRACTOR && t.amount > 0) geyserRespawn.push_back({t.pos, t.amount});
    if (isStructure(t)) {
        map.setBlocked(t.pos, std::max(1, d.footprint), 0);
        if (t.type == T_TOWER) emit({EV_TOWER_DOWN, (int16_t)t.id, -1, t.team, (int8_t)killerOwner, t.pos});
        if (t.type == B_CORE && phase == PH_PLAYING) {
            winner = 1 - t.team;
            phase = PH_OVER;
            emit({EV_GAME_OVER, -1, -1, (uint8_t)winner, -1, t.pos});
        }
        // refund nothing; production lost
    }
}

Vector2 World::exitPoint(const Entity& b, Vector2 toward) {
    Vector2 dir = norm2(toward - b.pos);
    if (len2(dir) < 0.1f) dir = norm2(V2(100, 100) - b.pos);
    for (float r = b.radius + 1.2f; r < b.radius + 8; r += 0.8f)
        for (int k = 0; k < 12; k++) {
            float a = angleOf(dir) + (k % 2 ? 1 : -1) * (k / 2) * 0.45f;
            Vector2 p = b.pos + V2(cosf(a), sinf(a)) * r;
            if (map.walkable(p)) return p;
        }
    return map.nearestWalkable(b.pos);
}

void World::finishBuilding(Entity& e) {
    e.constructing = false;
    e.hp = std::max(e.hp, e.maxHp * 0.98f);
    emit({EV_BUILD_DONE, (int16_t)e.id, -1, (uint8_t)e.type, e.owner, e.pos});
}

// ---------------------------------------------------------------------------
// abilities
// ---------------------------------------------------------------------------
bool World::castAbility(Entity& h, int slot, Vector2 pt, uint16_t target) {
    if (!abilityReady(h, slot)) return false;
    const AbilityDef& a = abilityDef(h.type, slot);
    float L = (float)h.level, ap = abilityPower(h);
    Entity* tgt = get(target);
    auto enemiesAround = [&](Vector2 p, float r) {
        std::vector<Entity*> v;
        for (auto id : unitsInRadius(p, r, h.team, true)) { Entity& e = ents[id]; if (isMobile(e) && visibleTo(h.team, e)) v.push_back(&e); }
        return v;
    };
    auto alliesAround = [&](Vector2 p, float r) {
        std::vector<Entity*> v;
        for (auto id : unitsInRadius(p, r, h.team, false)) { Entity& e = ents[id]; if (isMobile(e)) v.push_back(&e); }
        return v;
    };
    auto clampPoint = [&](Vector2 p, float range) {
        Vector2 d = p - h.pos;
        float l = len2(d);
        if (l > range) p = h.pos + d * (range / l);
        return p;
    };
    auto dash = [&](Vector2 dest) {
        Vector2 d = dest - h.pos;
        float l = len2(d);
        Vector2 dir = norm2(d), last = h.pos;
        for (float s = 0.5f; s <= l; s += 0.5f) { Vector2 p = h.pos + dir * s; if (!map.walkable(p)) break; last = p; }
        if (map.walkable(dest)) last = dest;
        emit({EV_BLINK, (int16_t)h.id, -1, 0, h.owner, h.pos, last});
        h.pos = last; h.prevPos = last;
        h.order = {O_IDLE}; h.queue.clear();
        return last;
    };
    bool ok = true;
    switch (h.type) {
    case H_VANGUARD:
        if (slot == 0) {
            Vector2 start = h.pos;
            Vector2 end = dash(clampPoint(pt, a.range));
            for (auto id : unitsInRadius((start + end) * 0.5f, dist2(start, end) * 0.5f + 2, h.team, true)) {
                Entity& e = ents[id];
                if (!isMobile(e) || distPointSeg(e.pos, start, end) > 2.0f + e.radius) continue;
                damage(e, (60 + 22 * L) * ap, &h, true);
                e.slowUntil = time + 2; e.slowAmt = 0.4f;
            }
        } else if (slot == 1) { h.shield = 140 + 35 * L; h.shieldUntil = time + 4; }
        else if (slot == 2) { for (auto* e : enemiesAround(h.pos, a.radius)) { damage(*e, (55 + 16 * L) * ap, &h, true); e->stunUntil = time + 1.2f; } }
        else { for (auto* e : alliesAround(h.pos, a.radius)) { e->shield = 160 + 20 * L; e->shieldUntil = time + 6; } heal(h, h.maxHp * 0.25f); }
        break;
    case H_PYRA:
        if (slot == 0 || false) {
            Projectile p; p.pos = h.pos; p.dir = norm2(pt - h.pos); p.owner = h.owner; p.team = h.team; p.src = h.id; p.srcType = h.type;
            p.speed = 26; p.dmg = (90 + 28 * L) * ap; p.skill = true; p.maxDist = a.range; p.width = 0.9f; p.visual = 1;
            if (len2(p.dir) < 0.1f) p.dir = V2(cosf(h.facing), sinf(h.facing));
            projs.push_back(p);
        } else if (slot == 1) {
            Zone z{Z_FIRE, clampPoint(pt, a.range), a.radius, time, time + 4, (28 + 9 * L) * ap, 0, 0, h.team, h.owner, h.id};
            zones.push_back(z);
        } else if (slot == 2) dash(map.nearestWalkable(clampPoint(pt, a.range)));
        else {
            Zone z{Z_METEOR, clampPoint(pt, a.range), a.radius, time, time + 1.5f, 0, (320 + 45 * L) * ap, 1.5f, h.team, h.owner, h.id};
            zones.push_back(z);
        }
        break;
    case H_HAWKE:
        if (slot == 0) {
            Projectile p; p.pos = h.pos; p.dir = norm2(pt - h.pos); p.owner = h.owner; p.team = h.team; p.src = h.id; p.srcType = h.type;
            p.speed = 42; p.dmg = (95 + 30 * L) * ap; p.skill = true; p.pierce = true; p.maxDist = a.range; p.width = 1.0f; p.visual = 2;
            if (len2(p.dir) < 0.1f) p.dir = V2(cosf(h.facing), sinf(h.facing));
            projs.push_back(p);
        } else if (slot == 1) { h.asUntil = time + 5; h.asAmt = 0.7f; }
        else if (slot == 2) { dash(clampPoint(pt, a.range)); h.empowered = 0.6f; }
        else {
            if (!tgt || tgt->team == h.team || !visibleTo(h.team, *tgt)) { ok = false; break; }
            Zone z{Z_ORBITAL, tgt->pos, 1.5f, time, time + 1.0f, 0, (380 + 50 * L) * ap, 1.0f, h.team, h.owner, h.id, tgt->id};
            zones.push_back(z);
        }
        break;
    case H_SERAPH:
        if (slot == 0) {
            if (!tgt || tgt->team != h.team || !isMobile(*tgt)) { ok = false; break; }
            heal(*tgt, (110 + 32 * L) * ap);
            Projectile p; p.pos = h.pos; p.target = tgt->id; p.visual = 3; p.speed = 30; p.dmg = 0; p.team = h.team; p.owner = h.owner; projs.push_back(p);
        } else if (slot == 1) { for (auto* e : alliesAround(h.pos, a.radius)) { e->hasteUntil = time + 5; e->hasteAmt = 0.35f; e->asUntil = time + 5; e->asAmt = 0.2f; } }
        else if (slot == 2) { for (auto* e : enemiesAround(clampPoint(pt, a.range), a.radius)) { damage(*e, (40 + 12 * L) * ap, &h, true); e->slowUntil = time + 3; e->slowAmt = 0.5f; } }
        else { for (auto* e : alliesAround(h.pos, a.radius)) { heal(*e, (300 + 40 * L) * ap); e->stunUntil = 0; e->slowUntil = 0; e->poisonUntil = 0; } }
        break;
    case H_WARLORD:
        if (slot == 0) { for (auto* e : alliesAround(h.pos, a.radius)) { e->dmgUntil = time + 6; e->dmgAmt = 0.3f; } }
        else if (slot == 1 || slot == 3) {
            Vector2 at = slot == 1 ? h.pos : map.nearestWalkable(clampPoint(pt, a.range));
            int n = slot == 1 ? 2 : 6;
            for (int k = 0; k < n; k++) {
                UnitType ut = (slot == 3 && k >= 3) ? U_STRIKER : U_WARDEN;
                Vector2 sp = map.nearestWalkable(at + V2(cosf(k * 1.1f) * 2.2f, sinf(k * 1.1f) * 2.2f));
                uint16_t id = spawn(ut, h.owner, h.team, sp);
                if (!id) continue;
                Entity& u = ents[id];
                u.summoned = true; u.expireAt = time + (slot == 1 ? 30 : 45);
                u.order = {O_ATTACK_MOVE, at + V2(cosf(h.facing), sinf(h.facing)) * 3, 0, 20};
                emit({EV_SPAWN, (int16_t)id, -1, 0, h.owner, sp});
            }
        } else { for (auto* e : enemiesAround(h.pos, a.radius)) { e->slowUntil = time + 4; e->slowAmt = 0.4f; e->vulnUntil = time + 4; e->vulnAmt = 0.2f; } }
        break;
    case H_SHADE:
        if (slot == 0 || slot == 3) {
            if (!tgt || tgt->team == h.team || !visibleTo(h.team, *tgt) || !isMobile(*tgt)) { ok = false; break; }
            if (slot == 3 && tgt->kind != K_HERO) { ok = false; break; }
            Vector2 behind = tgt->pos + norm2(tgt->pos - h.pos) * (tgt->radius + h.radius + 0.2f);
            dash(map.nearestWalkable(behind));
            float dmg = slot == 0 ? (80 + 26 * L) * ap : (220 + 30 * L) * ap + 0.25f * (tgt->maxHp - tgt->hp);
            damage(*tgt, dmg, &h, true);
            h.order = {O_ATTACK, tgt->pos, tgt->id};
        } else if (slot == 1) { h.stealthUntil = time + 4; h.hasteUntil = time + 4; h.hasteAmt = 0.3f; h.empowered = 1.0f; }
        else h.poisonCharges = 4;
        break;
    default: ok = false; break;
    }
    if (!ok) return false;
    h.mana -= a.mana;
    h.abilCd[slot] = a.cooldown * (1 - cdr(h));
    if (h.stealthUntil > time && !(h.type == H_SHADE && slot == 1)) h.stealthUntil = 0;
    emit({EV_ABILITY, (int16_t)h.id, (int16_t)target, (uint8_t)slot, h.owner, h.pos, pt});
    return true;
}

// ---------------------------------------------------------------------------
// per-entity logic
// ---------------------------------------------------------------------------
static void popOrder(Entity& e) {
    if (!e.queue.empty()) { e.order = e.queue.front(); e.queue.erase(e.queue.begin()); }
    else e.order = {O_IDLE};
    e.arrived = false;
}

void World::tickWorker(Entity& e) {
    auto validNode = [&](const Entity* n) {
        return n && (n->type == R_CRYSTAL || (n->type == B_EXTRACTOR && n->team == e.team && !n->constructing && n->amount > 0));
    };
    if (e.order.type == O_GATHER) {
        Entity* node = get(e.order.target);
        if (!validNode(node)) {
            // field mined out or vent lost: move on to the closest crystal around
            Vector2 from = e.home.x != 0 || e.home.y != 0 ? e.home : e.pos;
            float best = 1e9f; uint16_t bn = 0;
            for (auto& r : ents) if (r.alive && r.type == R_CRYSTAL) { float d = dist2(r.pos, from); if (d < best) { best = d; bn = r.id; } }
            if (!bn || best > 18) { e.order = {O_IDLE}; return; }
            e.order.target = bn; node = get(bn);
            e.home = node->pos;
        }
        e.node = node->id;
        e.home = node->pos;
        if (e.carry > 0 && e.gatherTimer <= 0) { const Entity* d = dropOff(e); e.order = {O_RETURN, {}, d ? d->id : (uint16_t)0}; return; }
        float reach = node->radius + e.radius + 0.5f;
        if (dist2(e.pos, node->pos) > reach) { moveToward(e, node->pos, reach - 0.1f); return; }
        bool flux = node->type == B_EXTRACTOR;
        if (e.gatherTimer > 0) {
            e.gatherTimer -= TICK_DT;
            if (e.gatherTimer <= 0) {
                int trip = flux ? FLUX_TRIP : (node->rich ? CRYSTAL_RICH_TRIP : CRYSTAL_TRIP);
                int got = std::min(trip, node->amount);
                node->amount -= got;
                e.carry = got; e.carryFlux = flux; e.gatherTimer = 0;
                if (!flux && node->amount <= 0) kill(*node, nullptr, -1);
                const Entity* d = dropOff(e);
                e.order = {O_RETURN, {}, d ? d->id : (uint16_t)0};
            }
            return;
        }
        float mine = flux ? 1.4f : 2.0f;
        if (node->occupiedUntil <= time) { e.gatherTimer = mine; node->occupiedUntil = time + mine + 0.05f; e.facing = angleOf(node->pos - e.pos); return; }
        // node busy: try a free neighbour
        if (!flux && ((tick + e.id) % 15) == 0)
            for (auto& r : ents)
                if (r.alive && r.type == R_CRYSTAL && r.id != node->id && r.occupiedUntil <= time && dist2(r.pos, node->pos) < 7) { e.order.target = r.id; break; }
        return;
    }
    if (e.order.type == O_RETURN) {
        const Entity* d = get(e.order.target);
        if (!d || d->team != e.team || d->constructing || (d->type != B_CORE && d->type != B_HUB)) d = dropOff(e);
        if (!d) { e.order = {O_IDLE}; return; }
        e.order.target = d->id;
        float reach = d->radius + e.radius + 0.6f;
        if (dist2(e.pos, d->pos) > reach) { moveToward(e, d->pos, reach - 0.1f); return; }
        if (e.owner >= 0) {
            PlayerState& P = players[e.owner];
            if (e.carryFlux) { P.flux += e.carry; P.fluxEarned += e.carry; }
            else { P.gold += e.carry; P.goldEarned += e.carry; }
        }
        e.carry = 0; e.carryFlux = false;
        e.order = {O_GATHER, e.home, e.node};
    }
}

void World::tickBuilding(Entity& e) {
    const UnitDef& d = unitDef(e.type);
    if (e.constructing) {
        e.buildProgress += TICK_DT / d.buildTime;
        e.hp = std::min(e.maxHp, e.hp + e.maxHp * 0.9f * TICK_DT / d.buildTime);
        if (e.buildProgress >= 1) finishBuilding(e);
        return;
    }
    if (!e.prod.empty()) {
        UnitType ut = e.prod[0];
        e.prodProgress += TICK_DT;
        if (e.prodProgress >= unitDef(ut).buildTime) {
            int owner = e.queue.empty() ? e.owner : e.queue[0].a;
            Vector2 toward = e.hasRally ? e.rally : V2(100, 100);
            if (!e.hasRally && ut == U_DRONE) {
                float bd = 16;
                for (auto& r : ents) if (r.alive && r.type == R_CRYSTAL && dist2(r.pos, e.pos) < bd) { bd = dist2(r.pos, e.pos); toward = r.pos; }
            }
            Vector2 sp = exitPoint(e, toward);
            uint16_t id = spawn(ut, owner, e.team, sp);
            if (id) {
                Entity& u = ents[id];
                if (e.hasRally) {
                    Entity* nodeAtRally = nullptr;
                    for (auto& r : ents) if (r.alive && (r.type == R_CRYSTAL || (r.type == B_EXTRACTOR && r.team == e.team)) && dist2(r.pos, e.rally) < r.radius + 0.8f) nodeAtRally = &r;
                    if (ut == U_DRONE && nodeAtRally) { u.order = {O_GATHER, nodeAtRally->pos, nodeAtRally->id}; u.node = nodeAtRally->id; u.home = nodeAtRally->pos; }
                    else u.order = {O_MOVE, e.rally, 0, 15};
                } else if (ut == U_DRONE) {
                    float best = 1e9f; uint16_t bn = 0;
                    for (auto& r : ents) if (r.alive && r.type == R_CRYSTAL) { float dd = dist2(r.pos, sp); if (dd < best) { best = dd; bn = r.id; } }
                    if (bn && best < 16) { u.order = {O_GATHER, ents[bn].pos, bn}; u.node = bn; u.home = ents[bn].pos; }
                }
                if (owner >= 0) players[owner].unitsProduced++;
                emit({EV_UNIT_DONE, (int16_t)id, (int16_t)e.id, (uint8_t)ut, (int8_t)owner, sp});
            }
            e.prod.erase(e.prod.begin());
            if (!e.queue.empty()) e.queue.erase(e.queue.begin());
            e.prodProgress = 0;
        }
    }
    if (e.research >= 0 && e.owner >= 0) {
        int lvl = players[e.owner].upgrades[e.research];
        e.researchProgress += TICK_DT;
        if (e.researchProgress >= upgradeTime(lvl)) {
            players[e.owner].upgrades[e.research]++;
            emit({EV_RESEARCH_DONE, (int16_t)e.id, -1, (uint8_t)e.research, e.owner, e.pos});
            e.research = -1; e.researchProgress = 0;
        }
    }
    // defensive structures
    if (d.dmg > 0) {
        e.attackCd -= TICK_DT;
        Entity* t = get(e.target);
        if (t && (inRangeDist(e, *t) > d.range || !visibleTo(e.team, *t) || t->team == e.team)) { e.target = 0; t = nullptr; }
        if (!t) {
            uint16_t best = 0; float bs = 1e9f;
            for (auto id : unitsInRadius(e.pos, d.range + e.radius, e.team, true)) {
                Entity& c = ents[id];
                if (c.team == TEAM_NEUTRAL || c.kind == K_RESOURCE || !visibleTo(e.team, c)) continue;
                float s = dist2(c.pos, e.pos) + (c.kind == K_HERO ? 30 : 0) + (isStructure(c) ? 60 : 0);
                if (s < bs) { bs = s; best = id; }
            }
            e.target = best;
            t = get(best);
        }
        if (t && e.attackCd <= 0) doAttack(e, *t);
    }
}

void World::tickHero(Entity& h) {
    const UnitDef& d = unitDef(h.type);
    float hpMax = d.hp + d.hpPerLevel * (h.level - 1);
    float manaMax = d.manaMax + 25.f * (h.level - 1);
    for (int i = 0; i < 4; i++) { hpMax += itemDef(h.items[i]).hp; manaMax += itemDef(h.items[i]).mana; }
    if (hpMax > h.maxHp) h.hp += hpMax - h.maxHp;
    h.maxHp = hpMax; h.maxMana = manaMax;
    h.hp = std::min(h.hp, h.maxHp);
    float regen = 1.2f + h.level * 0.25f, mregen = d.manaRegen + h.level * 0.15f;
    if (map.teamOfBase(h.pos) == h.team) { regen += h.maxHp * 0.05f; mregen += h.maxMana * 0.05f; }
    h.hp = std::min(h.maxHp, h.hp + regen * TICK_DT);
    h.mana = std::min(h.maxMana, h.mana + mregen * TICK_DT);
    for (int i = 0; i < 4; i++) h.abilCd[i] = std::max(0.f, h.abilCd[i] - TICK_DT);
    if (h.shieldUntil < time) h.shield = 0;
}

void World::tickEntity(Entity& e) {
    const UnitDef& d = unitDef(e.type);
    if (e.kind == K_RESOURCE) return;
    if (isStructure(e)) { tickBuilding(e); return; }
    if (e.kind == K_HERO) tickHero(e);
    e.attackCd -= TICK_DT;
    if (e.summoned && time >= e.expireAt) { e.hp = 0; kill(e, nullptr, -1); return; }
    if (time < e.poisonUntil) damage(e, e.poisonDps * TICK_DT * 1.2f, nullptr, true, e.lastAttackerOwner);
    if (!e.alive) return;
    if (time < e.regenUntil) heal(e, e.regenRate * TICK_DT);
    if (e.kind == K_UNIT && e.team < 2 && map.teamOfBase(e.pos) == e.team) e.hp = std::min(e.maxHp, e.hp + e.maxHp * 0.02f * TICK_DT);
    if (time < e.stunUntil) return;

    // healers
    if (e.type == U_MENDER) {
        if (e.attackCd <= 0) {
            Entity* best = nullptr; float worst = 0.98f;
            for (auto id : unitsInRadius(e.pos, d.range + 1, e.team, false)) {
                Entity& a = ents[id];
                if (!isMobile(a) || a.id == e.id) continue;
                float r = a.hp / a.maxHp;
                if (r < worst) { worst = r; best = &a; }
            }
            if (best) {
                heal(*best, d.dmg * (1 + (e.owner >= 0 ? players[e.owner].upgrades[UP_WEAPONS] * 0.12f : 0)));
                e.attackCd = d.cooldown; e.attackAnim = time; e.facing = angleOf(best->pos - e.pos);
                Projectile p; p.pos = e.pos; p.target = best->id; p.visual = 3; p.speed = 30; p.dmg = 0; p.team = e.team; projs.push_back(p);
            }
        }
    }
    bool canFight = d.dmg > 0 && e.type != U_MENDER && e.type != U_DRONE;
    if (e.type == U_DRONE && (e.order.type == O_GATHER || e.order.type == O_RETURN)) { tickWorker(e); return; }

    // neutrals: guard camp, leash back
    if (e.kind == K_NEUTRAL) {
        Entity* t = get(e.target);
        if (dist2(e.pos, e.home) > 15 || (t && dist2(t->pos, e.home) > 17)) { e.target = 0; t = nullptr; }
        if (t && t->alive) chaseAndAttack(e, *t);
        else {
            e.target = 0;
            if (dist2(e.pos, e.home) > 0.6f) moveToward(e, e.home, 0.5f);
            else e.hp = std::min(e.maxHp, e.hp + e.maxHp * 0.1f * TICK_DT);
        }
        return;
    }

    switch (e.order.type) {
    case O_IDLE: case O_HOLD: {
        if (!canFight) break;
        float range = e.order.type == O_HOLD ? d.range + 0.5f : (e.kind == K_HERO && e.owner >= 0 && !players[e.owner].isAI ? d.range + 1.0f : d.sight);
        Entity* t = get(e.target);
        if (!t || t->team == e.team || !visibleTo(e.team, *t) || inRangeDist(e, *t) > range + 2) { e.target = acquire(e, range); t = get(e.target); }
        if (t) {
            if (e.order.type == O_HOLD) { if (inRangeDist(e, *t) <= d.range) doAttack(e, *t); }
            else chaseAndAttack(e, *t);
        }
        break;
    }
    case O_MOVE:
        moveToward(e, e.order.pt, e.order.a * 0.1f);
        if (e.arrived || e.stuck > 1.5f) popOrder(e);
        else if (e.order.a > 12) {
            // group arrival: stop when touching a stopped unit of the same group near the goal
            if (dist2(e.pos, e.order.pt) < e.order.a * 0.1f + 2.5f)
                for (auto id : unitsInRadius(e.pos, e.radius + 0.6f, e.team, false)) {
                    Entity& o = ents[id];
                    if (o.id != e.id && o.owner == e.owner && o.order.type == O_IDLE && dist2(o.pos, e.order.pt) < e.order.a * 0.1f + 2.5f) { popOrder(e); break; }
                }
        }
        break;
    case O_ATTACK_MOVE: case O_LANE: {
        Entity* t = get(e.target);
        float sight = d.sight;
        if (canFight) {
            if (!t || t->team == e.team || !visibleTo(e.team, *t) || dist2(t->pos, e.pos) > sight + 4 || (t->team == TEAM_NEUTRAL)) {
                if ((e.acquireCd -= TICK_DT) <= 0 || !t) { e.target = acquire(e, sight); e.acquireCd = 0.25f; }
                t = get(e.target);
            }
            if (t && t->team != TEAM_NEUTRAL) { chaseAndAttack(e, *t); break; }
        }
        Vector2 goal = e.order.pt;
        {
            (void)0;
            moveToward(e, goal, e.order.a * 0.1f);
            if (e.arrived || e.stuck > 2.0f) popOrder(e);
        }
        break;
    }
    case O_ATTACK: {
        Entity* t = get(e.order.target);
        if (!t || (t->team == e.team) || !visibleTo(e.team, *t)) { popOrder(e); break; }
        if (!canFight && e.kind != K_HERO) { moveToward(e, t->pos, 1.5f); break; }
        chaseAndAttack(e, *t);
        break;
    }
    case O_BUILD: {
        UnitType bt = (UnitType)e.order.a;
        const UnitDef& bd = unitDef(bt);
        float reach = bd.footprint * Map::CELL * 0.5f + e.radius + 1.0f;
        if (dist2(e.pos, e.order.pt) > reach) { moveToward(e, e.order.pt, reach - 0.2f); if (e.stuck > 3) { if (e.owner >= 0) { players[e.owner].gold += bd.cost; players[e.owner].flux += bd.flux; } popOrder(e); } break; }
        int why = canBuild(bt, e.order.pt, e.team);
        if (why >= 0) {
            if (e.owner >= 0) { players[e.owner].gold += bd.cost; players[e.owner].flux += bd.flux; }
            emit({EV_CANT_BUILD, -1, -1, (uint8_t)why, e.owner, e.order.pt});
            popOrder(e);
            break;
        }
        int vent = 0;
        if (bt == B_EXTRACTOR)
            for (auto& g : ents) if (g.alive && g.type == R_GEYSER && dist2(g.pos, e.order.pt) < 0.5f) { vent = g.amount; g.alive = false; map.setBlocked(g.pos, 3, 0); }
        uint16_t id = spawn(bt, e.owner, e.team, e.order.pt);
        if (id) {
            Entity& b = ents[id];
            b.amount = vent;
            b.constructing = true; b.buildProgress = 0; b.hp = b.maxHp * 0.1f;
            emit({EV_BUILD_START, (int16_t)id, (int16_t)e.id, (uint8_t)bt, e.owner, b.pos});
            // push units standing on the footprint out of the way
            for (auto uid : unitsInRadius(b.pos, b.radius + 0.5f, -1, true)) {
                Entity& u = ents[uid];
                if (isMobile(u)) u.pos = exitPoint(b, u.pos);
            }
        }
        popOrder(e);
        if (e.order.type == O_IDLE && e.node) { e.order = {O_GATHER, {}, e.node}; }
        break;
    }
    case O_CAST: {
        if (e.kind != K_HERO) { popOrder(e); break; }
        const AbilityDef& a = abilityDef(e.type, e.order.a);
        if (!abilityReady(e, e.order.a)) { popOrder(e); break; }
        Vector2 at = e.order.pt;
        if (a.tm == TM_ENEMY || a.tm == TM_ALLY) {
            Entity* t = get(e.order.target);
            if (!t || (a.tm == TM_ENEMY && !visibleTo(e.team, *t))) { popOrder(e); break; }
            at = t->pos;
            if (inRangeDist(e, *t) > a.range) { moveToward(e, at, a.range + e.radius + t->radius - 0.3f); break; }
        } else if (dist2(e.pos, at) > a.range + 0.3f) { moveToward(e, at, a.range); break; }
        castAbility(e, e.order.a, at, e.order.target);
        if (e.order.type == O_CAST) popOrder(e);
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------------------
void World::separate() {
    for (auto& e : ents) {
        if (!e.alive || !isMobile(e)) continue;
        int gx = std::clamp((int)(e.pos.x / 8), 0, GRID - 1), gz = std::clamp((int)(e.pos.y / 8), 0, GRID - 1);
        Vector2 push{0, 0};
        for (int z = std::max(0, gz - 1); z <= std::min(GRID - 1, gz + 1); z++)
            for (int x = std::max(0, gx - 1); x <= std::min(GRID - 1, gx + 1); x++)
                for (uint16_t id : grid[z * GRID + x]) {
                    if (id == e.id) continue;
                    const Entity& o = ents[id];
                    if (!o.alive || !isMobile(o)) continue;
                    Vector2 d = e.pos - o.pos;
                    float dd = len2(d), minD = e.radius + o.radius;
                    if (dd >= minD || dd < 1e-5f) { if (dd < 1e-5f) push += V2((float)((e.id % 7) - 3) * 0.01f, (float)((e.id % 5) - 2) * 0.01f); continue; }
                    float w = (minD - dd) * 0.5f;
                    // moving units yield to idle ones less; heroes are heavier
                    if (o.kind == K_HERO && e.kind != K_HERO) w *= 1.4f;
                    push += d * (w / dd);
                }
        if (len2(push) > 0) {
            if (len2(push) > 0.25f) push = norm2(push) * 0.25f;
            Vector2 np = e.pos + push;
            if (map.walkable(np)) e.pos = np;
        }
    }
}

void World::tickProjectiles() {
    for (auto& p : projs) {
        if (!p.alive) continue;
        if (p.skill) {
            Vector2 step = p.dir * (p.speed * TICK_DT);
            p.pos += step;
            p.traveled += len2(step);
            if (p.traveled > p.maxDist || !map.inside((int)(p.pos.x / Map::CELL), (int)(p.pos.y / Map::CELL))) { p.alive = false; continue; }
            int ci = map.cellIndex(p.pos);
            if (ci >= 0 && map.type[ci] == CELL_ROCK) { p.alive = false; continue; }
            for (auto id : unitsInRadius(p.pos, p.width, p.team, true)) {
                Entity& t = ents[id];
                if (!isMobile(t) || std::find(p.hit.begin(), p.hit.end(), id) != p.hit.end()) continue;
                const Entity* src = get(p.src);
                damage(t, p.dmg, src, true, p.owner);
                p.hit.push_back(id);
                if (!p.pierce) { p.alive = false; break; }
            }
            continue;
        }
        Entity* t = get(p.target);
        if (!t) { p.alive = false; continue; }
        Vector2 d = t->pos - p.pos;
        float L = len2(d), s = p.speed * TICK_DT;
        if (L <= s + 0.3f) {
            p.alive = false;
            if (p.dmg <= 0) continue;
            const Entity* src = get(p.src);
            damage(*t, p.dmg, src, false, p.owner);
            if (p.splash > 0)
                for (auto id : unitsInRadius(t->pos, p.splash, p.team, true)) {
                    Entity& o = ents[id];
                    if (o.id == p.target || o.kind == K_RESOURCE || o.team == TEAM_NEUTRAL) continue;
                    damage(o, p.dmg * 0.6f, src, false, p.owner);
                }
            continue;
        }
        p.pos += d * (s / L);
        p.dir = d * (1.0f / L);
    }
    projs.erase(std::remove_if(projs.begin(), projs.end(), [](const Projectile& p) { return !p.alive; }), projs.end());
}

void World::tickZones() {
    for (auto& z : zones) {
        Entity* src = get(z.src);
        if (z.kind == Z_FIRE) {
            for (auto id : unitsInRadius(z.pos, z.radius, z.team, true)) {
                Entity& t = ents[id];
                if (isMobile(t)) damage(t, z.dps * TICK_DT, src, true, z.owner);
            }
        } else if ((z.kind == Z_METEOR || z.kind == Z_ORBITAL) && !z.fired && time >= z.start + z.delay) {
            z.fired = true;
            if (z.kind == Z_ORBITAL) {
                Entity* t = get(z.target);
                if (t && visibleTo(z.team, *t)) z.pos = t->pos;
                if (t) damage(*t, z.dmg, src, true, z.owner);
            } else {
                for (auto id : unitsInRadius(z.pos, z.radius, z.team, true)) {
                    Entity& t = ents[id];
                    if (t.kind == K_RESOURCE) continue;
                    damage(t, isStructure(t) ? z.dmg * 0.4f : z.dmg, src, true, z.owner);
                }
            }
            emit({EV_METEOR, -1, -1, (uint8_t)z.kind, z.owner, z.pos});
            z.end = time;
        } else if (z.kind == Z_ORBITAL && !z.fired) {
            Entity* t = get(z.target);
            if (t && visibleTo(z.team, *t)) z.pos = t->pos;
        }
    }
    zones.erase(std::remove_if(zones.begin(), zones.end(), [&](const Zone& z) { return time > z.end && (z.kind == Z_FIRE || z.fired); }), zones.end());
}

void World::updateVision() {
    const int NN = Map::N;
    for (int t = 0; t < 2; t++) std::fill(vis[t].begin(), vis[t].end(), 0);
    auto reveal = [&](int team, Vector2 pos, float r, int lvl) {
        int cx = (int)(pos.x / Map::CELL), cz = (int)(pos.y / Map::CELL);
        int rc = (int)ceilf(r / Map::CELL);
        float r2 = sqr(r / Map::CELL);
        auto& V = vis[team];
        for (int dz = -rc; dz <= rc; dz++) {
            int z = cz + dz;
            if (z < 0 || z >= NN) continue;
            for (int dx = -rc; dx <= rc; dx++) {
                int x = cx + dx;
                if (x < 0 || x >= NN || dx * dx + dz * dz > r2) continue;
                int i = z * NN + x;
                if (map.level[i] > lvl) continue;          // low ground cannot see up the cliffs
                V[i] = 1;
            }
        }
    };
    for (auto& b : beaconHeld) b = 0;
    for (auto& e : ents) {
        if (!e.alive || e.team > 1 || e.kind == K_RESOURCE) continue;
        float r = unitDef(e.type).sight;
        if (e.constructing) r = 5;
        int lvl = map.levelAt(e.pos);
        reveal(e.team, e.pos, r, lvl);
        // watch beacons: a unit standing next to one sees far, even onto high ground
        if (!isStructure(e))
            for (size_t k = 0; k < map.beacons.size() && k < 8; k++)
                if (dist2(e.pos, map.beacons[k]) < 3.6f) beaconHeld[k] |= (uint8_t)(1 << e.team);
    }
    for (size_t k = 0; k < map.beacons.size() && k < 8; k++)
        for (int t = 0; t < 2; t++) if (beaconHeld[k] & (1 << t)) reveal(t, map.beacons[k], 22, 1);
    // shooting from the high ground gives your position away
    for (auto& e : ents) {
        if (!e.alive || e.team > 1 || time - e.attackAnim > 1.0f) continue;
        int i = map.cellIndex(e.pos);
        if (i >= 0 && map.level[i] == 1) vis[1 - e.team][i] = 1;
    }
    for (int t = 0; t < 2; t++)
        for (size_t i = 0; i < vis[t].size(); i++) explored[t][i] |= vis[t][i];
}

void World::step() {
    tick++;
    time += TICK_DT;
    map.frame = tick;
    if (phase == PH_LOBBY) {
        for (auto& c : pending) applyCommand(c);
        pending.clear();
        bool allReady = true;
        for (auto& p : players) if (p.active && !p.isAI && !p.ready) allReady = false;
        if (allReady || time >= lobbyEnd) start();
        return;
    }
    if (phase == PH_OVER) { pending.clear(); return; }
    rebuildGrid();
    for (auto& c : pending) applyCommand(c);
    pending.clear();
    size_t n = ents.size();
    for (size_t i = 1; i < n; i++) {
        Entity& e = ents[i];
        if (!e.alive) continue;
        e.prevPos = e.pos;
        tickEntity(ents[i]);
    }
    separate();
    tickProjectiles();
    tickZones();
    for (auto& g : geyserRespawn) { uint16_t id = spawn(R_GEYSER, -1, TEAM_NEUTRAL, g.first); if (id) ents[id].amount = g.second; }
    geyserRespawn.clear();
    // respawns
    for (size_t c = 0; c < map.camps.size() && c < 16; c++)
        if (campRespawn[c] > 0 && time >= campRespawn[c]) {
            campRespawn[c] = 0;
            for (int k = 0; k < 2; k++) { uint16_t id = spawn(U_GOLEM, -1, TEAM_NEUTRAL, map.camps[c] + V2(k * 2.4f - 1.2f, 0)); if (id) { ents[id].lane = (int8_t)c; ents[id].maxHp = ents[id].hp = ents[id].maxHp * (1 + time / 900.f); } }
        }
    if (titanRespawn > 0 && time >= titanRespawn) { titanRespawn = 0; spawn(U_TITAN, -1, TEAM_NEUTRAL, map.titanPos); }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        PlayerState& p = players[i];
        if (!p.active) continue;
        if (tick % TICK_RATE == 0) { p.gold += 1; p.goldEarned += 1; }
        Entity& h = ents[p.heroId];
        if (!h.alive && h.respawnAt > 0 && time >= h.respawnAt) {
            h.alive = true; h.hp = h.maxHp; h.mana = h.maxMana;
            Vector2 toC = norm2(map.ramp[p.team] - map.base[p.team]);
            h.pos = h.prevPos = map.nearestWalkable(map.base[p.team] + toC * 8);
            h.order = {O_IDLE}; h.queue.clear(); h.respawnAt = 0;
            h.stunUntil = h.slowUntil = h.poisonUntil = 0;
            emit({EV_RESPAWN, (int16_t)h.id, -1, 0, (int8_t)i, h.pos});
        }
    }
    if (tick % 3 == 0) updateVision();
}

} // namespace ax
