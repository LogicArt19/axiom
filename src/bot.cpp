#include "bot.h"

namespace ax {

static const BotSkill kSkills[5] = {
    //  react  turn   smooth  err   head  spray burst  cstrafe util  strafe
    {0.58f, 260.f, 7.0f, 4.2f, 0.12f, 0.10f, 14.f, false, false, false},  // Recrue
    {0.42f, 380.f, 9.0f, 2.6f, 0.30f, 0.35f, 18.f, false, true, false},   // Regulier
    {0.30f, 560.f, 12.f, 1.5f, 0.50f, 0.62f, 22.f, true, true, true},     // Veteran
    {0.22f, 760.f, 16.f, 0.9f, 0.66f, 0.80f, 26.f, true, true, true},     // Expert
    {0.165f, 980.f, 21.f, 0.5f, 0.80f, 0.92f, 30.f, true, true, true},    // Elite
};
static const char* kSkillNames[5] = {"RECRUE", "REGULIER", "VETERAN", "EXPERT", "ELITE"};
const BotSkill& botSkill(int l) { return kSkills[std::clamp(l, 0, 4)]; }
const char* botSkillName(int l) { return kSkillNames[std::clamp(l, 0, 4)]; }

static Vector3 g_intel[3];
static float g_intelTime[3] = {-100, -100, -100};

static float ballisticPitch(Vector3 from, Vector3 to, float v) {
    Vector3 d = to - from;
    float dx = len2xz(d), dy = d.y, g = 12.0f;
    float v2 = v * v;
    float disc = v2 * v2 - g * (g * dx * dx + 2 * dy * v2);
    float theta = disc < 0 ? 45.f : atanf((v2 - sqrtf(disc)) / (g * std::max(dx, 0.1f))) / DEG;
    // invert the +10 deg bias applied by World::throwGrenade
    return theta >= 10.f ? (theta - 10.f) * 9.f / 8.f : (theta - 10.f) * 0.9f;
}

// ---------------------------------------------------------------------------
void BotSystem::roundSetup(World& w) {
    lastRound = w.round;
    attackSite = rng.f01() < 0.5f ? 1 : 2;
    g_intelTime[0] = g_intelTime[1] = g_intelTime[2] = -100;
    int defIdx = 0;
    std::vector<int> atk;
    for (auto& p : w.players) {
        if (!p.active || !p.isBot) continue;
        BotBrain& b = brains[p.id];
        b = BotBrain{};
        b.aimYaw = p.yaw;
        b.initialized = true;
        if (p.team == TEAM_DEF) b.holdIndex = (defIdx++ + w.round) % 5;
        else atk.push_back(p.id);
    }
    // attack routes
    for (size_t i = 0; i < atk.size(); i++) {
        BotBrain& b = brains[atk[i]];
        const auto& routes = attackSite == 1 ? w.map.routeA : w.map.routeB;
        int k = (int)((i + w.round) % 3);
        if (rng.f01() < 0.3f) k = 2;      // occasional mid pressure
        if (!routes.empty()) b.waypoints.push_back(routes[k % routes.size()]);
        b.wpIdx = 0;
    }
}

void BotSystem::buy(World& w, Player& p) {
    if (!w.canBuy(p.id)) return;
    int teamMoney = 0, n = 0;
    bool teamHasSniper = false;
    for (auto& q : w.players)
        if (q.active && q.team == p.team) { teamMoney += q.money; n++; if (q.primary == W_ORACLE) teamHasSniper = true; }
    float avg = n ? (float)teamMoney / n : 0;
    int played = (int)w.history.size();
    bool pistolRound = played == 0 || played == w.roundsToWin - 1;
    auto money = [&]() { return p.money; };
    if (pistolRound) {
        float r = rng.f01();
        if (r < 0.45f) w.tryBuy(p.id, BUY_KEVLAR);
        else if (r < 0.75f) { w.tryBuy(p.id, W_MAMBA); w.tryBuy(p.id, W_FLASH); w.tryBuy(p.id, W_SMOKE); }
        else { w.tryBuy(p.id, W_FLASH); w.tryBuy(p.id, W_FLASH); w.tryBuy(p.id, W_SMOKE); }
        if (p.team == TEAM_DEF && money() >= EQ_KIT_PRICE + 100 && rng.f01() < 0.4f) w.tryBuy(p.id, BUY_KIT);
        return;
    }
    if (p.primary != W_NONE) {
        // already armed from last round: top up utility
    } else {
        bool eco = avg < 2300 && money() < 3900;
        if (eco) {
            if (money() > 2300 && rng.f01() < 0.35f) { w.tryBuy(p.id, rng.f01() < 0.5f ? W_WASP : W_BREACH); w.tryBuy(p.id, BUY_KEVLAR); }
            else if (money() > 1200 && rng.f01() < 0.4f) w.tryBuy(p.id, rng.f01() < 0.6f ? W_MAMBA : W_HORNET);
            return;
        }
        if (!teamHasSniper && money() >= 5900 && rng.f01() < 0.25f) w.tryBuy(p.id, W_ORACLE);
        else if (money() >= 3700) {
            WeaponId r = p.team == TEAM_ATK ? W_VANGUARD : (rng.f01() < 0.55f ? W_SPECTER : W_VANGUARD);
            w.tryBuy(p.id, r);
        } else if (money() >= 3000) w.tryBuy(p.id, rng.f01() < 0.5f ? W_RAPTOR : W_LANCER);
        else w.tryBuy(p.id, W_WASP);
    }
    if (p.armor < 100 || !p.helmet) { if (money() >= 1000) w.tryBuy(p.id, BUY_HELMET); else w.tryBuy(p.id, BUY_KEVLAR); }
    if (money() >= 300) w.tryBuy(p.id, W_SMOKE);
    if (money() >= 200) w.tryBuy(p.id, W_FLASH);
    if (p.team == TEAM_DEF && money() >= 400) w.tryBuy(p.id, BUY_KIT);
    if (money() >= 500 && rng.f01() < 0.5f) w.tryBuy(p.id, W_MOLLY);
    if (money() >= 300) w.tryBuy(p.id, W_FRAG);
    if (money() >= 200) w.tryBuy(p.id, W_FLASH);
}

void BotSystem::setGoal(World& w, Player& p, BotBrain& b, Vector3 goal, BotTask t) {
    b.task = t;
    if (len2xz(goal - b.goal) > 0.3f || b.path.empty()) {
        b.goal = goal;
        b.path.clear();
        b.pathIdx = 0;
        if (w.map.findPath(p.pos, goal, b.path)) b.pathIdx = b.path.size() > 1 ? 1 : 0;
    }
}

bool BotSystem::followPath(World& w, Player& p, BotBrain& b, Vector3& moveDir) {
    (void)w;
    if (b.path.empty() || b.pathIdx >= b.path.size()) return false;
    Vector3 wp = b.path[b.pathIdx];
    Vector3 d = wp - p.pos; d.y = 0;
    float dist = len3(d);
    bool last = b.pathIdx == b.path.size() - 1;
    if (dist < (last ? 0.35f : 0.7f)) {
        b.pathIdx++;
        if (b.pathIdx >= b.path.size()) return false;
        wp = b.path[b.pathIdx];
        d = wp - p.pos; d.y = 0; dist = len3(d);
    }
    moveDir = dist > 0.001f ? d * (1.0f / dist) : V3(0, 0, 0);
    return true;
}

void BotSystem::chooseTarget(World& w, Player& p, BotBrain& b) {
    const BotSkill& sk = botSkill(p.botSkill);
    float now = w.time;
    int best = -1;
    float bestD = 1e9f;
    for (auto& q : w.players) {
        if (!q.active || !q.alive || !w.isEnemy(p.id, q.id)) continue;
        float d = len3(q.pos - p.pos);
        if (d > 90) continue;
        bool fov = !(q.id == b.target && now - b.lastSeen < 0.3f);
        if (!w.canSee(p.id, q.id, fov)) continue;
        if (q.id == b.target) d *= 0.6f;     // stickiness
        if (d < bestD) { bestD = d; best = q.id; }
    }
    if (best >= 0) {
        Player& t = w.players[best];
        if (best != b.target || now - b.lastSeen > 1.0f) {
            float dist = len3(t.pos - p.pos);
            float react = sk.reaction * rng.range(0.8f, 1.25f);
            // targets far in peripheral vision take longer
            Vector3 to = norm3(t.pos - p.pos);
            float facing = dot3(to, flatForward(b.aimYaw));
            if (facing < 0.7f) react += 0.12f;
            if (now - b.lastKnownTime < 2.0f && len3(b.lastKnown - t.pos) < 6) react *= 0.65f;   // prepared for it
            b.acquireTime = now;
            b.reactReady = now + react;
            b.aimHead = rng.f01() < sk.headPref || dist < 6;
            float e = sk.aimError * (1.0f + dist / 35.f);
            b.errOffset = V3(rng.range(-e, e), rng.range(-e * 0.6f, e * 0.6f), 0);
            b.burstCount = 0;
            b.crouchSpray = sk.counterStrafe && dist > 16 && rng.f01() < 0.35f;
        }
        b.target = best;
        b.lastSeen = now;
        b.lastKnown = t.pos;
        b.lastKnownTime = now;
        b.hasLastKnown = true;
        g_intel[p.team] = t.pos;
        g_intelTime[p.team] = now;
    } else if (b.target >= 0 && now - b.lastSeen > 0.6f) {
        b.target = -1;
    }
}

void BotSystem::plan(World& w, Player& p, BotBrain& b) {
    float now = w.time;
    if (w.mode == MODE_DEATHMATCH) {
        if (b.path.empty() || b.pathIdx >= b.path.size() || now > b.nextRepath) {
            Vector3 g = (b.hasLastKnown && now - b.lastKnownTime < 5) ? b.lastKnown : w.map.randomWalkableNear(V3(0, 0, 0), 40, rng);
            b.goal = V3(9999, 0, 0);
            setGoal(w, p, b, g, BT_ROAM);
            b.nextRepath = now + 8.f;
        }
        return;
    }
    if (w.mode != MODE_COMPETITIVE) return;
    if (w.phase == PH_FREEZE || (w.phase == PH_LIVE && now < w.buyEndTime)) buy(w, p);
    if (w.phase != PH_LIVE && w.phase != PH_FREEZE) return;
    Vector3 site = attackSite == 1 ? w.map.siteA : w.map.siteB;
    if (p.team == TEAM_ATK) {
        if (w.bomb.state == BOMB_PLANTED) {
            if (b.task != BT_GUARD) setGoal(w, p, b, w.map.randomWalkableNear(w.bomb.pos, 7, rng), BT_GUARD);
            return;
        }
        if (w.bomb.state == BOMB_DROPPED) {
            // closest attacker fetches it
            int closest = -1; float cd = 1e9f;
            for (auto& q : w.players)
                if (q.active && q.alive && q.team == TEAM_ATK && q.isBot) { float d = len3(q.pos - w.bomb.pos); if (d < cd) { cd = d; closest = q.id; } }
            if (closest == p.id) { setGoal(w, p, b, w.bomb.pos, BT_GETBOMB); return; }
        }
        if (b.wpIdx < b.waypoints.size()) {
            Vector3 wp = b.waypoints[b.wpIdx];
            if (len2xz(p.pos - wp) < 2.0f) {
                if (b.waitUntil == 0) b.waitUntil = w.roundStartTime + 16.f + rng.range(0, 14.f);
                // execute when the clock says so, or early if the team already fights at the site
                bool late = now > w.roundStartTime + w.roundTimeSec - 45.f;
                if (now >= b.waitUntil || late) b.wpIdx++;
                else { b.task = BT_HOLD; float pp; anglesFromDir(site - p.pos, b.holdYaw, pp); return; }
            } else { setGoal(w, p, b, wp, BT_GOTO); return; }
        }
        if (p.hasBomb) {
            setGoal(w, p, b, site, BT_PLANT);
        } else if (b.task != BT_GUARD || b.path.empty()) {
            setGoal(w, p, b, w.map.randomWalkableNear(site, 7, rng), BT_GUARD);
        }
        return;
    }
    // defenders
    if (w.bomb.state == BOMB_PLANTED) {
        float d = len2xz(p.pos - w.bomb.pos);
        if (d < 1.3f) b.task = BT_DEFUSE;
        else setGoal(w, p, b, w.bomb.pos, BT_GOTO);
        return;
    }
    const std::vector<HoldSpot>* lists[3] = {&w.map.holdA, &w.map.holdB, &w.map.holdMid};
    int hi = b.holdIndex < 0 ? 0 : b.holdIndex;
    int listIdx = hi == 2 ? 2 : (hi % 2 == 0 ? 0 : 1);
    // rotate if the other site is being hit
    if (now - g_intelTime[TEAM_DEF] < 4.0f && hi != 2) {
        float dA = len2xz(g_intel[TEAM_DEF] - w.map.siteA), dB = len2xz(g_intel[TEAM_DEF] - w.map.siteB);
        if (dA < 16 && listIdx == 1 && rng.f01() < 0.5f) listIdx = 0;
        else if (dB < 16 && listIdx == 0 && rng.f01() < 0.5f) listIdx = 1;
    }
    const auto& L = *lists[listIdx];
    if (L.empty()) return;
    const HoldSpot& hs = L[(hi / 2 + p.id) % L.size()];
    if (len2xz(p.pos - hs.pos) < 0.8f) { b.task = BT_HOLD; b.holdYaw = hs.yaw; }
    else { setGoal(w, p, b, hs.pos, BT_GOTO); b.holdYaw = hs.yaw; }
}

void BotSystem::onEvent(World& w, const GameEvent& e) {
    float radius = 0;
    switch (e.type) {
    case EV_FOOTSTEP: radius = 26; break;
    case EV_SHOT: radius = 70; break;
    case EV_JUMP: case EV_LAND: radius = 14; break;
    case EV_RELOAD: radius = 10; break;
    default: return;
    }
    if (e.a < 0) return;
    for (auto& p : w.players) {
        if (!p.active || !p.isBot || !p.alive || !w.isEnemy(p.id, e.a)) continue;
        BotBrain& b = brains[p.id];
        if (len3(p.pos - e.pos) > radius) continue;
        if (b.target >= 0 && w.time - b.lastSeen < 0.5f) continue;
        b.lastKnown = e.pos;
        b.lastKnownTime = w.time;
        b.hasLastKnown = true;
    }
}

// ---------------------------------------------------------------------------
void BotSystem::think(World& w, int id, UserCmd& out) {
    Player& p = w.players[id];
    BotBrain& b = brains[id];
    out = UserCmd{};
    out.tick = w.tick;
    if (w.mode == MODE_COMPETITIVE && w.round != lastRound) roundSetup(w);
    if (!p.alive) {
        b.initialized = false;
        out.yaw = p.yaw;
        if (w.mode == MODE_DEATHMATCH) {
            static const WeaponId prim[] = {W_VANGUARD, W_SPECTER, W_RAPTOR, W_WASP, W_ORACLE, W_LANCER, W_BREACH, W_TITAN};
            p.dmPrimary = prim[rng.next() % 8];
            p.dmSecondary = rng.f01() < 0.5f ? W_HORNET : W_MAMBA;
        }
        return;
    }
    const BotSkill& sk = botSkill(p.botSkill);
    float now = w.time;
    const float dt = TICK_DT;
    if (!b.initialized) { b = BotBrain{}; b.aimYaw = p.yaw; b.aimPitch = 0; b.initialized = true; b.lastPos = p.pos; }

    if (w.passiveBots) {
        b.aimYaw += sinf(now * 0.5f + id) * 20.f * dt;
        out.yaw = b.aimYaw;
        return;
    }
    if (now >= b.decideAt) { plan(w, p, b); b.decideAt = now + rng.range(0.25f, 0.45f); }
    if ((w.tick + id) % 2 == 0) chooseTarget(w, p, b);

    Vector3 moveDir{0, 0, 0};
    bool wantMove = (b.task == BT_GOTO || b.task == BT_GUARD || b.task == BT_PLANT || b.task == BT_GETBOMB || b.task == BT_ROAM) && followPath(w, p, b, moveDir);

    // --- desired view
    bool haveTarget = b.target >= 0 && w.players[b.target].alive;
    Player* t = haveTarget ? &w.players[b.target] : nullptr;
    bool visibleNow = haveTarget && now - b.lastSeen < 0.05f;
    float dYaw = b.aimYaw, dPitch = b.aimPitch;
    float targetDist = 0;
    if (t) {
        Vector3 aimPt = w.hitboxCenter(*t, b.aimHead ? HG_HEAD : HG_CHEST);
        if (!visibleNow) aimPt = b.lastKnown + V3(0, t->eyeHeight(), 0);
        // lead a little for strafing targets (better bots)
        aimPt += t->vel * (0.03f * (p.botSkill + 1) / 5.f);
        Vector3 e = p.eye();
        targetDist = len3(aimPt - e);
        anglesFromDir(aimPt - e, dYaw, dPitch);
        float decay = expf(-(now - b.acquireTime) * (1.6f + p.botSkill * 0.6f));
        float jitter = sk.aimError * 0.25f;
        dYaw += b.errOffset.x * decay + sinf(now * 3.1f + id) * jitter * 0.4f;
        dPitch += b.errOffset.y * decay + cosf(now * 2.3f + id) * jitter * 0.3f;
    } else if (b.nadeState == 0 && b.hasLastKnown && now - b.lastKnownTime < 3.5f) {
        anglesFromDir(b.lastKnown + V3(0, 1.4f, 0) - p.eye(), dYaw, dPitch);
    } else if (b.task == BT_HOLD) {
        b.lookSweep += dt;
        dYaw = b.holdYaw + sinf(b.lookSweep * 0.7f) * 18.f;
        dPitch = 0;
    } else if (b.task == BT_DEFUSE) {
        anglesFromDir(w.bomb.pos - p.eye(), dYaw, dPitch);
    } else if (wantMove) {
        // look a bit ahead along the path, checking corners
        Vector3 ahead = b.path[std::min(b.pathIdx + 1, b.path.size() - 1)];
        Vector3 dir = ahead - p.pos; dir.y = 0;
        if (len3(dir) > 0.5f) { float py; anglesFromDir(dir, dYaw, py); }
        dPitch = 0;
    }

    // grenade aiming override
    if (b.nadeState >= 1) {
        float g, pp;
        anglesFromDir(b.nadeTarget - p.eye(), g, pp);
        dYaw = g;
        dPitch = ballisticPitch(p.eye(), b.nadeTarget, 21.5f);
    }

    // --- turn toward desired with human-like smoothing and max turn rate
    float ey = angleDiff(dYaw, b.aimYaw), ep = dPitch - b.aimPitch;
    float k = 1.0f - expf(-sk.smoothing * dt);
    float sy = ey * k, sp = ep * k;
    float maxStep = sk.turnSpeed * dt;
    float mag = sqrtf(sy * sy + sp * sp);
    if (mag > maxStep) { sy *= maxStep / mag; sp *= maxStep / mag; }
    // minimum creep so bots actually settle
    if (fabsf(ey) < 0.05f) sy = ey;
    b.aimYaw = angleNorm(b.aimYaw + sy);
    b.aimPitch = clampf(b.aimPitch + sp, -89, 89);

    // --- weapon handling
    const WeaponDef& wd = weaponDef(p.activeW);
    bool wantShoot = false;
    if (t && visibleNow && now >= b.reactReady && b.nadeState == 0) {
        float err = sqrtf(sqr(angleDiff(dYaw, b.aimYaw)) + sqr(dPitch - b.aimPitch));
        float tol = 0.9f + atanf(0.32f / std::max(targetDist, 1.f)) / DEG;
        if (err < tol) wantShoot = true;
    }
    int slot = wd.slot == SLOT_PRIMARY ? 0 : 1;
    // choose weapon
    if (b.nadeState == 0 && b.task != BT_PLANT && !p.planting) {
        WeaponId want = p.primary != W_NONE && (p.clip[0] > 0 || p.reserve[0] > 0) ? p.primary : (p.secondary != W_NONE ? p.secondary : W_KNIFE);
        if (p.activeW != want && (isGrenade(p.activeW) || p.activeW == W_BOMB || p.activeW == W_KNIFE ||
                                  (p.activeW == p.secondary && want == p.primary && !wantShoot)))
            out.select = want;
        // pistol switch when primary is empty in a fight
        if (wantShoot && isGun(p.activeW) && p.clip[slot] == 0 && p.activeW == p.primary && p.secondary != W_NONE && p.clip[1] > 0)
            out.select = p.secondary;
    }
    if (isGun(p.activeW)) {
        if (wd.cls == WC_SHOTGUN && targetDist > 16) wantShoot = false;
        if (wd.cls == WC_SNIPER && wantShoot) {
            if (p.scope == 0 && p.scopeResume == 0 && targetDist > 5) { if (!p.attack2Held) out.buttons |= IN_ATTACK2; wantShoot = false; }
            if (len2xz(p.vel) > wd.maxSpeed * 0.3f) wantShoot = false;
        }
        if (wantShoot) {
            if (wd.automatic) {
                if (targetDist > sk.burstRange) {
                    if (now < b.burstPauseUntil) wantShoot = false;
                    else if (p.shotsFired >= 3 + p.botSkill / 2) { b.burstPauseUntil = now + rng.range(0.22f, 0.4f); wantShoot = false; }
                }
            } else {
                // semi auto: release between shots
                if (p.attackHeld || now < p.nextAttack) wantShoot = false;
            }
        }
        if (wantShoot) out.buttons |= IN_ATTACK;
        // reload
        if (p.clip[slot] == 0 || (!t && p.clip[slot] < wd.magSize * 0.4f && now - b.lastKnownTime > 2.5f)) out.buttons |= IN_RELOAD;
    } else if (p.activeW == W_KNIFE && t && targetDist < 2.0f && now >= b.reactReady) {
        out.buttons |= IN_ATTACK;
    }

    // --- grenades
    if (sk.useUtility && b.nadeState == 0 && !t && w.mode == MODE_COMPETITIVE && w.phase == PH_LIVE) {
        bool atkExec = p.team == TEAM_ATK && b.waitUntil > 0 && now >= b.waitUntil && now < b.waitUntil + 1.5f && !b.usedEntryUtil;
        if (atkExec) {
            b.usedEntryUtil = true;
            Vector3 site = attackSite == 1 ? w.map.siteA : w.map.siteB;
            const auto& holds = attackSite == 1 ? w.map.holdA : w.map.holdB;
            if (p.nades[1] && !holds.empty()) { b.nadeType = W_SMOKE; b.nadeTarget = holds[rng.next() % holds.size()].pos; b.nadeState = 1; b.nadeTimer = now; }
            else if (p.nades[0]) { b.nadeType = W_FLASH; b.nadeTarget = site + V3(0, 4.0f, 0); b.nadeState = 1; b.nadeTimer = now; }
        } else if (b.hasLastKnown && now - b.lastKnownTime < 2.5f && now - b.lastKnownTime > 0.6f) {
            float d = len3(b.lastKnown - p.pos);
            if (d > 7 && d < 26 && rng.f01() < 0.01f) {
                if (p.nades[2]) { b.nadeType = W_FRAG; b.nadeTarget = b.lastKnown; b.nadeState = 1; b.nadeTimer = now; }
                else if (p.nades[3]) { b.nadeType = W_MOLLY; b.nadeTarget = b.lastKnown; b.nadeState = 1; b.nadeTimer = now; }
            }
        }
    }
    if (b.nadeState >= 1) {
        wantMove = false;
        if (!p.hasWeapon(b.nadeType) || t || now - b.nadeTimer > 3.0f) b.nadeState = 0;
        else if (p.activeW != b.nadeType) out.select = b.nadeType;
        else if (b.nadeState == 1 && now >= p.drawEnd) {
            float err = fabsf(angleDiff(dYaw, b.aimYaw)) + fabsf(dPitch - b.aimPitch);
            if (err < 1.5f) { b.nadeState = 2; b.nadeTimer = now; }
        } else if (b.nadeState == 2) {
            if (now - b.nadeTimer < 0.12f) out.buttons |= IN_ATTACK;
            else b.nadeState = 0;     // release -> throw
        }
    }

    // --- planting / defusing
    if (b.task == BT_PLANT && p.hasBomb) {
        uint8_t z = w.map.zoneAt(p.pos);
        if ((z & (ZONE_SITE_A | ZONE_SITE_B)) && (!wantMove || len2xz(p.pos - b.goal) < 3.0f) && !t) {
            wantMove = false;
            if (p.activeW != W_BOMB) out.select = W_BOMB;
            else out.buttons |= IN_ATTACK;
        }
    }
    if (b.task == BT_DEFUSE && w.bomb.state == BOMB_PLANTED) {
        float left = w.bomb.explodeTime - now;
        bool safe = !t || left < (p.kit ? 6.f : 11.f);
        if (safe) { out.buttons |= IN_USE; wantMove = false; out.buttons |= IN_DUCK; }
    }

    // --- movement
    out.yaw = b.aimYaw;
    out.pitch = b.aimPitch;
    if (out.buttons & IN_ATTACK) {
        out.yaw = angleNorm(b.aimYaw - p.punchYaw * sk.sprayControl);
        out.pitch = b.aimPitch - p.punchPitch * sk.sprayControl;
    }
    Vector3 mv{0, 0, 0};
    if (wantMove) mv = moveDir;
    if (t && visibleNow && now >= b.reactReady - 0.1f) {
        bool shooting = (out.buttons & IN_ATTACK) || wantShoot;
        if (sk.counterStrafe && shooting && isGun(p.activeW) && wd.cls != WC_SHOTGUN && wd.cls != WC_SMG) {
            // counter-strafe: push against current velocity to stop instantly
            Vector3 v = p.vel; v.y = 0;
            if (len3(v) > 0.6f) mv = norm3(v) * -1.0f; else mv = {0, 0, 0};
            if (b.crouchSpray && p.shotsFired > 2) out.buttons |= IN_DUCK;
        } else if (sk.strafeInFights && !shooting) {
            if (now > b.strafeSwitch) { b.strafeDir = -b.strafeDir; b.strafeSwitch = now + rng.range(0.25f, 0.6f); }
            mv = flatRight(b.aimYaw) * b.strafeDir;
        } else if (!sk.counterStrafe && shooting) {
            mv = mv * 0.5f;
        }
    }
    float fl = len3(mv);
    if (fl > 0.01f) {
        out.fwd = dot3(mv, flatForward(out.yaw));
        out.side = dot3(mv, flatRight(out.yaw));
        float m = std::max(fabsf(out.fwd), fabsf(out.side));
        if (m > 0) { out.fwd /= m; out.side /= m; }
        // walk silently when sneaking up on a heard enemy (better bots)
        if (p.botSkill >= 3 && !t && b.hasLastKnown && now - b.lastKnownTime < 3 && len3(b.lastKnown - p.pos) < 18) out.buttons |= IN_WALK;
    }

    // --- stuck detection
    if (wantMove && len2xz(p.vel) < 0.5f && w.phase != PH_FREEZE) {
        b.stuckTimer += dt;
        if (b.stuckTimer > 0.7f) {
            out.buttons |= IN_JUMP;
            out.side = rng.f01() < 0.5f ? -1.f : 1.f;
            if (b.stuckTimer > 1.4f) { b.path.clear(); Vector3 g = b.goal; b.goal = V3(9999, 0, 0); setGoal(w, p, b, g, b.task); b.stuckTimer = 0; }
        }
    } else b.stuckTimer = 0;
}

} // namespace ax
