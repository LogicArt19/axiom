#include "ai.h"

namespace ax {

struct Skill { float macroInterval, heroInterval; int workers; int attackSupply; float abilityChance, retreatHp; bool lastHit, focusHeroes, useUpgrades; float expandTime; int maxBases; };
static const Skill kSkill[5] = {
    {2.2f, 0.9f, 12, 34, 0.25f, 0.15f, false, false, false, 600, 2},
    {1.6f, 0.6f, 15, 30, 0.45f, 0.22f, true, false, false, 420, 3},
    {1.1f, 0.4f, 18, 28, 0.7f, 0.28f, true, true, true, 300, 4},
    {0.7f, 0.25f, 20, 28, 0.85f, 0.32f, true, true, true, 240, 5},
    {0.45f, 0.15f, 22, 28, 1.0f, 0.35f, true, true, true, 200, 6},
};

static const ItemId kBuild[HERO_COUNT][6] = {
    {I_PLATE, I_BOOTS, I_HEART, I_AEGIS, I_BLADE, I_PLATE},     // vanguard
    {I_FOCUS, I_BOOTS, I_SCEPTER, I_HEART, I_SCEPTER, I_PLATE}, // pyra
    {I_BOW, I_BOOTS, I_BLADE, I_VAMP, I_BOW, I_HEART},          // hawke
    {I_FOCUS, I_BOOTS, I_AEGIS, I_HEART, I_SCEPTER, I_PLATE},   // seraph
    {I_AEGIS, I_BOOTS, I_PLATE, I_BLADE, I_HEART, I_VAMP},      // warlord
    {I_BLADE, I_BOOTS, I_VAMP, I_BLADE, I_HEART, I_BOW},        // shade
};

static Command cmd(CmdType t, int pid) { Command c; c.type = t; c.player = (uint8_t)pid; return c; }
static bool isDropOff(const Entity& e) { return e.type == B_CORE || e.type == B_HUB; }

void AISystem::reset(World& w) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        ai[i] = AIPlayer{};
        ai[i].pid = i;
        ai[i].rng = Rng(1000 + i * 7919 + (uint32_t)(nowSeconds() * 1000));
    }
    known[0].clear(); known[1].clear();
    nextMemory = 0;
    (void)w;
}

void AISystem::remember(World& w) {
    for (int t = 0; t < 2; t++) {
        auto& K = known[t];
        for (auto& e : w.ents)
            if (e.alive && e.team == 1 - t && (e.kind == K_BUILDING) && w.visibleTo(t, e) && std::find(K.begin(), K.end(), e.id) == K.end()) K.push_back(e.id);
        // forget what we can see is gone
        K.erase(std::remove_if(K.begin(), K.end(), [&](uint16_t id) {
            const Entity& e = w.ents[id];
            if (!e.alive) return w.cellVisible(t, e.pos) || true;
            return false;
        }), K.end());
    }
}

uint16_t AISystem::freeDrone(World& w, int pid, Vector2 near) {
    uint16_t best = 0;
    float bd = 1e9f;
    for (auto& e : w.ents) {
        if (!e.alive || e.owner != pid || e.type != U_DRONE) continue;
        if (e.order.type == O_BUILD || e.carry > 0) continue;
        if (e.order.type == O_GATHER) { const Entity* n = w.get(e.order.target); if (n && n->type == B_EXTRACTOR) continue; }
        float d = dist2(e.pos, near) - (e.order.type == O_IDLE ? 30 : 0);
        if (d < bd) { bd = d; best = e.id; }
    }
    return best;
}

bool AISystem::findSpot(World& w, int pid, UnitType t, Vector2 near, Vector2& out, Rng& rng) {
    int team = w.players[pid].team;
    int fp = unitDef(t).footprint;
    // the resource side of the closest base stays free for mining
    const Expansion* ex = nullptr;
    for (auto& x : w.map.expansions) if (!ex || dist2(x.hub, near) < dist2(ex->hub, near)) ex = &x;
    for (int tries = 0; tries < 300; tries++) {
        bool relaxed = tries > 180;
        float r = rng.range(8, relaxed ? 24.f : 19.f);
        float a = rng.range(0, 6.2831f);
        Vector2 p = w.buildSite(t, near + V2(cosf(a), sinf(a)) * r);
        if (ex && dist2(ex->hub, p) < 15 && dot2(norm2(p - ex->hub), ex->dir) > (relaxed ? 0.2f : -0.1f)) continue;
        if (w.canBuild(t, p, team) >= 0) continue;
        if (w.map.levelAt(p) != w.map.levelAt(near) && !relaxed) continue;
        bool crowded = false;
        for (auto& e : w.ents)
            if (e.alive && (e.kind == K_BUILDING) && dist2(e.pos, p) < e.radius + fp * Map::CELL * 0.5f + (relaxed ? 2.2f : 3.0f)) { crowded = true; break; }
        if (crowded) continue;
        // never wall the ramp, never seal a gap against the cliffs
        if (dist2(p, w.map.ramp[team]) < 10 || dist2(p, w.map.rampTop[team]) < 12) continue;
        bool hugsRock = false;
        {
            float half = fp * Map::CELL * 0.5f + Map::CELL;
            for (float dz = -half; dz <= half + 0.01f && !hugsRock; dz += Map::CELL)
                for (float dx = -half; dx <= half + 0.01f; dx += Map::CELL) {
                    int ci = w.map.cellIndex(p + V2(dx, dz));
                    if (ci < 0 || w.map.blocked[ci] == 1) { hugsRock = true; break; }
                }
        }
        if (hugsRock && !relaxed) continue;
        out = p;
        return true;
    }
    return false;
}

Vector2 AISystem::rallyPoint(World& w, int team) {
    // in front of our most exposed base
    Vector2 enemy = w.map.base[1 - team];
    const Entity* front = nullptr;
    for (auto& e : w.ents)
        if (e.alive && e.team == team && isDropOff(e) && (!front || dist2(e.pos, enemy) < dist2(front->pos, enemy))) front = &e;
    if (!front || front->type == B_CORE) return w.map.ramp[team];
    Vector2 p = front->pos + norm2(enemy - front->pos) * 10;
    return w.map.nearestWalkable(p);
}

int AISystem::freeExpansion(World& w, int team) {
    int best = -1;
    float bd = 1e9f;
    Vector2 from = w.map.ramp[team];
    for (size_t i = 0; i < w.map.expansions.size(); i++) {
        const Expansion& x = w.map.expansions[i];
        if (x.mainOf >= 0) continue;
        bool taken = false;
        for (auto& e : w.ents) if (e.alive && isDropOff(e) && dist2(e.pos, x.hub) < 14) { taken = true; break; }
        if (taken) continue;
        // known enemy presence nearby
        for (auto id : known[team]) { const Entity& e = w.ents[id]; if (e.alive && dist2(e.pos, x.hub) < 26) taken = true; }
        if (taken) continue;
        int crystals = 0;
        for (auto& e : w.ents) if (e.alive && e.type == R_CRYSTAL && dist2(e.pos, x.hub) < 12) crystals++;
        if (crystals < 3) continue;
        float d = w.map.flowDistance(from, x.hub + x.dir * -7.0f) + dist2(x.hub, w.map.base[1 - team]) * -0.25f;
        if (d < bd) { bd = d; best = (int)i; }
    }
    return best;
}

void AISystem::macro(World& w, AIPlayer& a) {
    int pid = a.pid;
    PlayerState& P = w.players[pid];
    int team = P.team;
    const Skill& sk = kSkill[std::clamp(P.aiSkill, 0, 4)];
    int used = w.supplyUsed(pid), cap = w.supplyMax(pid);
    int drones = w.countUnits(pid, U_DRONE, true);
    // hero items first when shopping
    Entity* h = w.get(P.heroId);
    if (h && w.heroNearShop(pid)) {
        int free = 0; for (int i = 0; i < 4; i++) if (!h->items[i]) free++;
        int hi = std::clamp(P.hero - HERO_FIRST, 0, HERO_COUNT - 1);
        if (free && a.itemIdx < 6) {
            ItemId it = kBuild[hi][a.itemIdx];
            if (P.gold >= itemDef(it).cost + (w.time < 300 ? 150 : 0)) { Command c = cmd(CMD_BUY, pid); c.a = it; w.issue(c); a.itemIdx++; return; }
        }
        if (h->hp < h->maxHp * 0.5f && P.gold > 200 && h->regenUntil < w.time) { Command c = cmd(CMD_BUY, pid); c.a = I_POTION; w.issue(c); }
    }
    // --- census
    std::vector<Entity*> myHubs, dropOffs, extractors;
    int hubsBuilding = 0, teamHubBuilding = 0;
    int barracks = 0, factories = 0, forges = 0, towers = 0, relaysBuilding = 0, extBuilding = 0;
    for (auto& e : w.ents) {
        if (!e.alive) continue;
        if (e.team == team && isDropOff(e)) { if (e.constructing) teamHubBuilding++; else dropOffs.push_back(&e); }
        if (e.owner != pid) continue;
        switch (e.type) {
        case B_HUB: if (e.constructing) hubsBuilding++; else myHubs.push_back(&e); break;
        case B_EXTRACTOR: if (e.constructing) extBuilding++; else extractors.push_back(&e); break;
        case B_BARRACKS: barracks++; break;
        case B_FACTORY: factories++; break;
        case B_FORGE: forges++; break;
        case B_WATCHTOWER: towers++; break;
        case B_RELAY: if (e.constructing) relaysBuilding++; break;
        default: break;
        }
    }
    for (auto& e : w.ents) {
        if (!e.alive || e.team != team || e.type != U_DRONE || e.order.type != O_BUILD) continue;
        UnitType bt = (UnitType)e.order.a;
        if (bt == B_HUB) teamHubBuilding++;
        if (e.owner != pid) continue;
        if (bt == B_BARRACKS) barracks++;
        if (bt == B_FACTORY) factories++;
        if (bt == B_FORGE) forges++;
        if (bt == B_WATCHTOWER) towers++;
        if (bt == B_RELAY) relaysBuilding++;
        if (bt == B_EXTRACTOR) extBuilding++;
        if (bt == B_HUB) hubsBuilding++;
    }
    int bases = 1 + (int)myHubs.size();
    int armySupply = 0;
    for (auto& e : w.ents) if (e.alive && e.owner == pid && e.kind == K_UNIT && e.type != U_DRONE) armySupply += unitDef(e.type).supply;
    for (auto& e : w.ents) if (e.alive && e.owner == pid && e.kind == K_BUILDING) for (auto q : e.prod) if (q != U_DRONE) armySupply += unitDef(q).supply;
    // never fall too far behind on army: a greedy economy dies to an early push
    bool needArmy = drones >= 14 && armySupply < (int)(w.time / (P.aiSkill >= 3 ? 11.f : 14.f));
    int workerTarget = std::min(42, sk.workers + 12 * (int)myHubs.size() + 3 * (int)extractors.size());
    auto build = [&](UnitType bt, Vector2 near) {
        const UnitDef& d = unitDef(bt);
        if (P.gold < d.cost || P.flux < d.flux || w.time - a.lastBuildOrder < 3) return false;
        Vector2 spot;
        if (!findSpot(w, pid, bt, near, spot, a.rng)) return false;
        uint16_t dr = freeDrone(w, pid, spot);
        if (!dr) return false;
        Command c = cmd(CMD_BUILD, pid); c.a = bt; c.pt = spot; c.units = {dr}; w.issue(c);
        a.lastBuildOrder = w.time;
        return true;
    };
    Vector2 home = w.map.base[team];

    // --- idle drones go back to the least crowded crystal field we own
    for (auto& e : w.ents) {
        if (!e.alive || e.owner != pid || e.type != U_DRONE || e.order.type != O_IDLE) continue;
        const Entity* bestC = nullptr; float bs = 1e9f;
        for (auto* d : dropOffs)
            for (auto& r : w.ents) {
                if (!r.alive || r.type != R_CRYSTAL || dist2(r.pos, d->pos) > 13) continue;
                int load = 0;
                for (auto& o : w.ents) if (o.alive && o.type == U_DRONE && o.order.target == r.id) load++;
                float sc = load * 40 + dist2(r.pos, e.pos) * 0.3f;
                if (sc < bs) { bs = sc; bestC = &r; }
            }
        if (bestC) { Command c = cmd(CMD_GATHER, pid); c.target = bestC->id; c.pt = bestC->pos; c.units = {e.id}; w.issue(c); }
    }
    // --- three drones per extractor
    for (auto* x : extractors) {
        if (x->amount <= 0) continue;
        int n = 0;
        for (auto& e : w.ents) if (e.alive && e.owner == pid && e.type == U_DRONE && (e.order.target == x->id || (e.node == x->id && e.order.type == O_RETURN))) n++;
        if (n >= 3 || drones < 10) continue;
        uint16_t best = 0; float bd = 1e9f;
        for (auto& e : w.ents) {
            if (!e.alive || e.owner != pid || e.type != U_DRONE || e.order.type != O_GATHER || e.carry > 0) continue;
            const Entity* n2 = w.get(e.order.target);
            if (!n2 || n2->type != R_CRYSTAL) continue;
            float d = dist2(e.pos, x->pos);
            if (d < bd) { bd = d; best = e.id; }
        }
        if (best && bd < 40) { Command c = cmd(CMD_GATHER, pid); c.target = x->id; c.pt = x->pos; c.units = {best}; w.issue(c); break; }
    }
    // --- workers
    if (drones < workerTarget && used + 1 <= cap && P.gold >= 50 && !(needArmy && drones >= 16)) {
        std::vector<Entity*> producers = myHubs;
        Entity* coreE = w.get(w.core[team]);
        if (coreE) producers.push_back(coreE);
        for (auto* pr : producers) {
            int queued = 0;
            if (pr->type == B_CORE) { for (auto& q : pr->queue) if (q.a == pid) queued++; }
            else queued = (int)pr->prod.size();
            if (queued < (pr->type == B_CORE ? 1 : 2) && P.gold >= 50) { Command c = cmd(CMD_TRAIN, pid); c.target = pr->id; c.a = U_DRONE; w.issue(c); break; }
        }
    }
    // --- supply
    if (cap < MAX_SUPPLY && cap - used < 4 + bases * 2 + (int)(w.time / 240) && relaysBuilding == 0) { if (build(B_RELAY, home)) return; }
    // --- expansion (saved for: no army spending while the crystals are short)
    bool wantExpand = !needArmy && hubsBuilding == 0 && teamHubBuilding == 0 && bases < sk.maxBases &&
                      w.time > sk.expandTime + 200.f * (bases - 1) && drones >= 12 + 8 * (bases - 1) && w.time - a.lastExpandOrder > 30;
    // main nearly mined out -> expand now
    int mainCrystals = 0;
    for (auto& e : w.ents) if (e.alive && e.type == R_CRYSTAL && dist2(e.pos, home) < 13) mainCrystals++;
    if (mainCrystals < 4 && hubsBuilding == 0 && teamHubBuilding == 0 && w.time - a.lastExpandOrder > 30 && bases < 6) wantExpand = true;
    bool savingForHub = false;
    if (wantExpand) {
        int xi = freeExpansion(w, team);
        if (xi >= 0) {
            if (P.gold >= unitDef(B_HUB).cost) {
                const Expansion& x = w.map.expansions[xi];
                uint16_t dr = freeDrone(w, pid, x.hub);
                if (dr) {
                    Command c = cmd(CMD_BUILD, pid); c.a = B_HUB; c.pt = x.hub; c.units = {dr}; w.issue(c);
                    a.lastExpandOrder = w.time;
                    return;
                }
            } else savingForHub = true;
        }
    }
    // --- flux
    int extWanted = (w.time > 70 ? 1 : 0) + (factories > 0 ? 1 : 0) + (P.flux < 400 ? 2 : 1) * (int)myHubs.size();
    if (P.flux > 800) extWanted = 0;
    if ((int)extractors.size() + extBuilding < extWanted && extBuilding == 0 && P.gold >= 75 && w.time - a.lastBuildOrder > 3) {
        const Entity* vent = nullptr; float bd = 1e9f;
        for (auto& g : w.ents) {
            if (!g.alive || g.type != R_GEYSER) continue;
            bool near = false;
            for (auto* d : dropOffs) if (dist2(d->pos, g.pos) < 14 && (d->type == B_CORE || d->owner == pid)) near = true;
            if (!near) continue;
            float d = dist2(g.pos, home);
            if (d < bd) { bd = d; vent = &g; }
        }
        if (vent) {
            uint16_t dr = freeDrone(w, pid, vent->pos);
            if (dr) { Command c = cmd(CMD_BUILD, pid); c.a = B_EXTRACTOR; c.pt = vent->pos; c.units = {dr}; w.issue(c); a.lastBuildOrder = w.time; return; }
        }
    }
    // --- tech & production
    if (!savingForHub) {
        // spread production over our bases when the main gets crowded
        Vector2 where = home;
        if (!myHubs.empty() && a.rng.f01() < 0.4f) where = myHubs[a.rng.irange(0, (int)myHubs.size() - 1)]->pos;
        bool tried = false;
        auto want = [&](bool cond, UnitType bt, Vector2 at) { if (!cond || tried) return false; tried = true; return build(bt, at); };
        if (want(barracks == 0 && drones >= 8, B_BARRACKS, home)) return;
        if (want(barracks >= 1 && factories == 0 && w.time > 150 && w.hasBuilding(pid, B_BARRACKS) && P.flux >= 100, B_FACTORY, where)) return;
        if (want(forges == 0 && w.time > 240 && w.hasBuilding(pid, B_BARRACKS) && sk.useUpgrades, B_FORGE, where)) return;
        if (want(barracks < 1 + bases / 2 + (w.time > 360 ? 1 : 0) + (needArmy && w.time > 150 ? 1 : 0) && (w.time > 300 || needArmy) && P.gold > 150, B_BARRACKS, where)) return;
        if (want(factories < (bases + 1) / 2 + (w.time > 600 ? 1 : 0) + (P.flux > 600 ? 1 : 0) && w.time > 420 && P.gold > 300 && P.flux > 150, B_FACTORY, where)) return;
        if (!tried && towers < (int)myHubs.size() && P.aiSkill >= 2 && P.gold > 250) {
            Entity* hub = myHubs[towers % myHubs.size()];
            if (build(B_WATCHTOWER, hub->pos)) return;
        }
        if (want(P.gold > 700 && barracks + factories < 3 + 2 * bases && cap - used > 6, factories * 2 < barracks + 1 && P.flux > 250 ? B_FACTORY : B_BARRACKS, where)) return;
    }
    // --- research
    for (auto& e : w.ents) {
        if (!e.alive || e.owner != pid || e.type != B_FORGE || e.constructing || e.research >= 0) continue;
        int up = P.upgrades[UP_WEAPONS] <= P.upgrades[UP_ARMOR] ? UP_WEAPONS : UP_ARMOR;
        if (P.upgrades[up] < UPGRADE_MAX && P.gold >= upgradeCost(up, P.upgrades[up]) + 100 && P.flux >= upgradeFlux(up, P.upgrades[up])) {
            Command c = cmd(CMD_RESEARCH, pid); c.target = e.id; c.a = (uint8_t)up; w.issue(c);
        }
    }
    // --- army (keep some crystals for the hero and the next base)
    int reserve = (h && a.itemIdx < 6 && w.time > 240) ? 150 : 0;
    if (savingForHub) reserve += unitDef(B_HUB).cost;
    for (auto& e : w.ents) {
        if (!e.alive || e.owner != pid || e.constructing || e.prod.size() >= 2) continue;
        UnitType ut = U_NONE;
        if (e.type == B_BARRACKS) ut = (a.rng.f01() < (P.flux > 200 ? 0.35f : 0.6f) || P.flux < 25) ? U_WARDEN : U_STRIKER;
        else if (e.type == B_FACTORY) {
            float r = a.rng.f01();
            ut = r < 0.45f ? U_LANCER : (r < 0.7f ? U_MENDER : U_SIEGE);
            if (ut == U_SIEGE && !w.hasBuilding(pid, B_FORGE)) ut = U_LANCER;
        }
        if (ut == U_NONE) continue;
        const UnitDef& d = unitDef(ut);
        if (P.gold - reserve >= d.cost && P.flux >= d.flux && used + d.supply <= cap) {
            Command c = cmd(CMD_TRAIN, pid); c.target = e.id; c.a = ut; w.issue(c);
            used += d.supply;
        }
    }
}

void AISystem::army(World& w, AIPlayer& a) {
    int pid = a.pid;
    PlayerState& P = w.players[pid];
    const Skill& sk = kSkill[std::clamp(P.aiSkill, 0, 4)];
    int team = P.team;
    Vector2 rally = rallyPoint(w, team);
    std::vector<uint16_t> units;
    int supply = 0;
    Vector2 centroid{0, 0};
    for (auto& e : w.ents) {
        if (!e.alive || e.owner != pid || e.kind != K_UNIT || e.type == U_DRONE) continue;
        units.push_back(e.id);
        supply += unitDef(e.type).supply;
        centroid += e.pos;
    }
    if (units.empty()) { a.attacking = false; return; }
    centroid = centroid * (1.0f / units.size());
    // defend any of our bases / buildings
    Vector2 threat{0, 0}; int threats = 0;
    for (auto& e : w.ents) {
        if (!e.alive || e.team == team || e.team > 1 || e.kind == K_RESOURCE || !w.visibleTo(team, e)) continue;
        bool near = false;
        for (auto& b : w.ents) if (b.alive && b.team == team && b.kind == K_BUILDING && dist2(b.pos, e.pos) < 16) { near = true; break; }
        if (near) { threat += e.pos; threats++; }
    }
    if (threats > 0) {
        Command c = cmd(CMD_ATTACK_MOVE, pid); c.pt = threat * (1.0f / threats); c.units = units; w.issue(c);
        a.attacking = false;
        return;
    }
    // local strength comparison -> retreat when clearly losing
    float mine = 0, theirs = 0;
    for (auto& e : w.ents) {
        if (!e.alive || e.kind == K_RESOURCE) continue;
        if (e.kind == K_BUILDING && unitDef(e.type).dmg <= 0) continue;
        if (dist2(e.pos, centroid) > 16) continue;
        float v = (e.kind == K_BUILDING ? std::min(e.hp, 1500.f) : e.hp) * std::max(1.f, unitDef(e.type).dmg) / 100.f;
        if (e.team == team) mine += v;
        else if (e.team < 2 && w.visibleTo(team, e)) theirs += v;
    }
    if (a.attacking && theirs > mine * 1.6f && sk.focusHeroes) {
        a.attacking = false; a.retreating = true;
        Command c = cmd(CMD_MOVE, pid); c.pt = rally; c.units = units; w.issue(c);
        return;
    }
    int threshold = sk.attackSupply + (int)(w.time / 100);
    if (!a.attacking && (supply >= std::min(threshold, 80) || w.time > 1500)) { a.attacking = true; a.retreating = false; }
    if (a.attacking && supply < threshold / 3) a.attacking = false;
    if (a.attacking) {
        // the closest enemy structure we know of, else their starting base
        const Entity* o = nullptr;
        Vector2 goal = w.map.base[1 - team];
        float bd = 1e9f;
        for (auto id : known[team]) {
            const Entity& e = w.ents[id];
            float d = dist2(e.pos, centroid) - (e.type == B_HUB ? 25 : 0);
            if (d < bd) { bd = d; o = &e; }
        }
        if (o) goal = o->pos;
        // the Titan is a great target when we are strong and it is up
        for (auto& e : w.ents)
            if (e.alive && e.type == U_TITAN && supply >= threshold && w.time > 480 && dist2(centroid, e.pos) < 45 && a.rng.f01() < 0.2f) { o = &e; goal = e.pos; }
        Command c = cmd(o && o->type == U_TITAN ? CMD_ATTACK : CMD_ATTACK_MOVE, pid);
        c.pt = goal; c.target = o ? o->id : 0; c.units = units;
        w.issue(c);
        a.armyGoal = goal;
    } else if (!a.retreating || dist2(centroid, rally) < 6) {
        a.retreating = false;
        std::vector<uint16_t> idle;
        for (auto id : units) { Entity& e = w.ents[id]; if (e.order.type == O_IDLE && dist2(e.pos, rally) > 7) idle.push_back(id); }
        if (!idle.empty()) { Command c = cmd(CMD_ATTACK_MOVE, pid); c.pt = rally; c.units = idle; w.issue(c); }
    }
}

void AISystem::hero(World& w, AIPlayer& a) {
    int pid = a.pid;
    PlayerState& P = w.players[pid];
    const Skill& sk = kSkill[std::clamp(P.aiSkill, 0, 4)];
    Entity* h = w.get(P.heroId);
    if (!h) return;
    int team = P.team;
    Vector2 base = w.map.base[team];
    auto order = [&](CmdType t, Vector2 pt, uint16_t target) { Command c = cmd(t, pid); c.pt = pt; c.target = target; c.units = {h->id}; w.issue(c); };
    // rest & shop
    if (h->hp < h->maxHp * sk.retreatHp) a.heroResting = true;
    if (a.heroResting) {
        if (h->hp > h->maxHp * 0.9f && h->mana > h->maxMana * 0.5f) a.heroResting = false;
        else {
            if (dist2(h->pos, base) > 10) order(CMD_MOVE, base + norm2(V2(100, 100) - base) * 6, 0);
            // defensive spells while escaping
            for (int s = 0; s < 4; s++) {
                if (!w.abilityReady(*h, s)) continue;
                bool escape = (h->type == H_PYRA && s == 2) || (h->type == H_HAWKE && s == 2) || (h->type == H_VANGUARD && s == 1) || (h->type == H_SHADE && s == 1);
                if (escape && h->lastHurt > w.time - 1.5f) { Command c = cmd(CMD_ABILITY, pid); c.a = (uint8_t)s; c.pt = h->pos + norm2(base - h->pos) * 8; w.issue(c); break; }
            }
            return;
        }
    }
    // pick a target: enemy hero in reach first (if strong enough), else minions to last hit
    Entity* enemyHero = nullptr; float ehd = 1e9f;
    Entity* lowMinion = nullptr; float lmh = 1e9f;
    Entity* nearestEnemy = nullptr; float ned = 1e9f;
    float myDmg = w.attackDamage(*h);
    int alliesNear = 0, enemiesNear = 0;
    bool underTower = false;   // enemy Core / sentinels
    for (auto& e : w.ents) {
        if (!e.alive || e.kind == K_RESOURCE) continue;
        float d = dist2(e.pos, h->pos);
        if (d > 16) continue;
        if (e.team == team) { if (e.kind != K_BUILDING) alliesNear++; continue; }
        if (e.team > 1 || !w.visibleTo(team, e)) continue;
        if (e.kind != K_BUILDING && e.kind != K_TOWER) enemiesNear++;
        if ((e.type == B_CORE || e.type == B_WATCHTOWER) && d < unitDef(e.type).range + 1.5f) underTower = true;
        if (e.kind == K_HERO && d < ehd) { ehd = d; enemyHero = &e; }
        if ((e.kind == K_UNIT) && e.hp < myDmg * 2.5f && e.hp < lmh) { lmh = e.hp; lowMinion = &e; }
        if (d < ned && e.kind != K_BUILDING) { ned = d; nearestEnemy = &e; }
    }
    // abilities
    for (int s = 3; s >= 0; s--) {
        if (!w.abilityReady(*h, s) || a.rng.f01() > sk.abilityChance) continue;
        const AbilityDef& ad = abilityDef(h->type, s);
        Command c = cmd(CMD_ABILITY, pid); c.a = (uint8_t)s;
        bool cast = false;
        Entity* foe = enemyHero && ehd < 14 ? enemyHero : nearestEnemy;
        switch (ad.tm) {
        case TM_ENEMY:
            if (enemyHero && ehd <= ad.range + 2 && (s != 3 || enemyHero->hp < enemyHero->maxHp * 0.55f || h->type == H_HAWKE)) { c.target = enemyHero->id; c.pt = enemyHero->pos; cast = true; }
            else if (s == 0 && foe && dist2(foe->pos, h->pos) < ad.range && enemiesNear >= 2) { c.target = foe->id; c.pt = foe->pos; cast = true; }
            break;
        case TM_ALLY: {
            Entity* low = nullptr; float lr = 0.6f;
            for (auto& e : w.ents)
                if (e.alive && e.team == team && (e.kind == K_UNIT || e.kind == K_HERO) && dist2(e.pos, h->pos) < ad.range + 3 && e.hp / e.maxHp < lr) { lr = e.hp / e.maxHp; low = &e; }
            if (low) { c.target = low->id; c.pt = low->pos; cast = true; }
            break;
        }
        case TM_POINT: case TM_DIRECTION:
            if (foe && dist2(foe->pos, h->pos) < ad.range + (ad.tm == TM_DIRECTION ? 0 : ad.radius * 0.5f)) {
                bool mobility = (h->type == H_PYRA && s == 2) || (h->type == H_HAWKE && s == 2);
                if (mobility) break;
                c.pt = foe->pos + foe->pos * 0 + (foe->pos - foe->prevPos) * (TICK_RATE * 0.4f);
                cast = foe->kind == K_HERO || enemiesNear >= 3 || (s == 3 && enemiesNear >= 4);
            }
            break;
        case TM_NONE: {
            float r = ad.radius > 0 ? ad.radius : 6;
            bool fight = foe && dist2(foe->pos, h->pos) < r;
            if (h->type == H_SERAPH && s == 3) cast = alliesNear >= 4 && enemiesNear >= 3;
            else if (h->type == H_WARLORD && s == 1) cast = fight;
            else if (h->type == H_SHADE && s == 1) cast = enemyHero && ehd < 12 && h->hp > h->maxHp * 0.5f;
            else if (h->type == H_VANGUARD && s == 1) cast = h->lastHurt > w.time - 1 && enemiesNear > 0;
            else cast = fight && (enemiesNear >= 2 || (foe && foe->kind == K_HERO));
            break;
        }
        }
        if (cast) { w.issue(c); return; }
    }
    // fight enemy heroes when favourable
    if (enemyHero && ehd < 10 && !underTower && h->hp > enemyHero->hp * 0.8f && sk.focusHeroes) { order(CMD_ATTACK, enemyHero->pos, enemyHero->id); return; }
    if (underTower && alliesNear < 4) { order(CMD_MOVE, h->pos + norm2(base - h->pos) * 6, 0); return; }
    if (sk.lastHit && lowMinion && dist2(lowMinion->pos, h->pos) < unitDef(h->type).range + 4) { order(CMD_ATTACK, lowMinion->pos, lowMinion->id); return; }
    // group with the army when it attacks
    if (ai[pid].attacking) { order(CMD_ATTACK_MOVE, ai[pid].armyGoal, 0); return; }
    // otherwise clear neutral camps near our side of the map for gold and experience
    Entity* camp = w.get(a.campTarget);
    if (camp && (camp->kind != K_NEUTRAL || camp->type != U_GOLEM)) camp = nullptr;
    float need = h->level < 3 ? 0.8f : 0.6f;
    if (!camp && h->hp > h->maxHp * need && w.time > 20) {
        float bd = 1e9f;
        for (auto& e : w.ents) {
            if (!e.alive || e.type != U_GOLEM) continue;
            float d = dist2(e.pos, base) - dist2(e.pos, w.map.base[1 - team]) * 0.5f;
            bool contested = false;
            for (auto& o : w.ents) if (o.alive && o.team == 1 - team && o.kind != K_BUILDING && w.visibleTo(team, o) && dist2(o.pos, e.pos) < 14) contested = true;
            if (!contested && d < bd && dist2(e.pos, base) < 110) { bd = d; camp = &e; }
        }
    }
    if (camp && h->hp > h->maxHp * 0.35f) { a.campTarget = camp->id; if (h->order.type != O_ATTACK || h->order.target != camp->id) order(CMD_ATTACK, camp->pos, camp->id); return; }
    a.campTarget = 0;
    Vector2 spot = rallyPoint(w, team);
    if (h->order.type == O_IDLE || dist2(h->pos, spot) > 9) order(CMD_ATTACK_MOVE, spot, 0);
}

void AISystem::think(World& w) {
    if (w.phase != PH_PLAYING) return;
    if (w.time >= nextMemory) { remember(w); nextMemory = w.time + 1.0f; }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        PlayerState& P = w.players[i];
        if (!P.active || !P.isAI) continue;
        AIPlayer& a = ai[i];
        a.pid = i;
        const Skill& sk = kSkill[std::clamp(P.aiSkill, 0, 4)];
        if (w.time >= a.nextMacro) { macro(w, a); a.nextMacro = w.time + sk.macroInterval * a.rng.range(0.8f, 1.2f); }
        if (w.time >= a.nextArmy) { army(w, a); a.nextArmy = w.time + 1.5f; }
        if (w.time >= a.nextHero) { hero(w, a); a.nextHero = w.time + sk.heroInterval * a.rng.range(0.8f, 1.2f); }
    }
}

} // namespace ax
