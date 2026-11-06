#include "hud.h"
#include "ui.h"
#include "config.h"
#include "audio.h"

namespace ax {

using namespace ui;

const char* weaponShortName(WeaponId w) { return weaponDef(w).name ? weaponDef(w).name : "?"; }

const char* buyItemName(int item) {
    if (item < W_COUNT) return weaponDef((WeaponId)item).name;
    if (item == BUY_KEVLAR) return "GILET";
    if (item == BUY_HELMET) return "GILET + CASQUE";
    if (item == BUY_KIT) return "KIT DE DÉSAMORÇAGE";
    return "?";
}

static Color teamCol(Team t) { return t == TEAM_ATK ? theme::atk : theme::def; }
static Color hudCol(float a = 1) { Color c = g_settings.hudColor(); c.a = (unsigned char)(255 * a * g_settings.hudAlpha); return c; }
static float HS(float v) { return S(v) * g_settings.hudScale; }

void hudBuildRadar(HudState& h, const Map& m) {
    if (h.radarReady) UnloadTexture(h.radarTex);
    const int K = 6;
    Image img = GenImageColor(m.w * K, m.h * K, Color{0, 0, 0, 0});
    for (int z = 0; z < m.h; z++)
        for (int x = 0; x < m.w; x++) {
            const Tile& t = m.at(x, z);
            Color c;
            if (t.height >= 3.0f) c = Color{0, 0, 0, 0};
            else if (t.height > 0.5f && !t.walkableNav) c = Color{70, 78, 92, 230};
            else if (t.zone & ZONE_SITE_A) c = Color{120, 92, 84, 230};
            else if (t.zone & ZONE_SITE_B) c = Color{84, 100, 124, 230};
            else if (t.height > 0) c = Color{120, 126, 140, 230};
            else c = Color{98, 104, 118, 225};
            ImageDrawRectangle(&img, x * K, z * K, K, K, c);
            // outline walls
            if (t.height >= 3.0f) {
                bool edge = false;
                for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) if (m.inBounds(x + dx, z + dz) && m.at(x + dx, z + dz).height < 3.0f) edge = true;
                if (edge) ImageDrawRectangle(&img, x * K, z * K, K, K, Color{200, 210, 225, 90});
            }
        }
    h.radarTex = LoadTextureFromImage(img);
    SetTextureFilter(h.radarTex, TEXTURE_FILTER_BILINEAR);
    UnloadImage(img);
    h.radarReady = true;
}

void hudMessage(HudState& h, const char* text, const char* sub, Color c, float dur) {
    h.center = {text ? text : "", sub ? sub : "", c, h.time, dur};
}

void hudOnEvent(HudState& h, const World& w, const GameEvent& e, int localId) {
    switch (e.type) {
    case EV_KILL: {
        KillFeedEntry k;
        k.killer = e.a >= 0 ? w.players[e.a].name : "";
        k.victim = e.b >= 0 ? w.players[e.b].name : "";
        k.kt = e.a >= 0 ? w.players[e.a].team : TEAM_NONE;
        k.vt = e.b >= 0 ? w.players[e.b].team : TEAM_NONE;
        k.w = (WeaponId)e.weapon; k.flags = e.flags; k.time = h.time;
        k.local = (e.a == localId || e.b == localId);
        k.assistLocal = localId >= 0 && e.b >= 0 && w.players[localId].dmgTo[e.b] >= 40 && e.a != localId;
        h.feed.push_back(k);
        if (h.feed.size() > 8) h.feed.erase(h.feed.begin());
        if (e.a == localId && e.b != localId) { h.hitTime = h.time; h.hitKill = true; h.hitHead = e.flags & 1; }
        break;
    }
    case EV_HIT:
        if (e.a == localId && e.b != localId) {
            if (h.time - h.hitTime > 0.05f || !h.hitKill) { h.hitTime = h.time; h.hitKill = false; h.hitHead = e.flags & 1; }
            if (g_settings.showDamageNumbers) h.numbers.push_back({e.pos, e.value, (e.flags & 1) != 0, h.time});
        }
        if (e.b == localId) {
            Vector3 d = e.pos2 - w.players[localId].pos;
            float yaw, pitch; anglesFromDir(d, yaw, pitch);
            h.dmg.push_back({yaw, h.time, e.value});
        }
        break;
    case EV_ROUND_END: {
        Team lt = localId >= 0 ? w.players[localId].team : TEAM_NONE;
        h.roundBannerTime = h.time;
        h.roundWon = (Team)e.a == lt;
        h.roundReason = reasonText((RoundEndReason)e.value, (Team)e.a);
        h.roundMvp = e.b >= 0 ? w.players[e.b].name : "";
        break;
    }
    case EV_HALFTIME: hudMessage(h, "MI-TEMPS", "Changement de camp", theme::gold, 4.0f); break;
    case EV_BOMB_PLANTED: hudMessage(h, "NOYAU POSÉ", e.value == 1 ? "Site A" : "Site B", theme::bad, 3.0f); break;
    case EV_BOMB_DEFUSED: hudMessage(h, "NOYAU DÉSAMORCÉ", "", theme::def, 3.0f); break;
    case EV_LIVE: {
        bool matchPoint = false;
        for (int s = 0; s < 2; s++) if (w.squadScore[s] == w.roundsToWin - 1 && !w.overtime) matchPoint = true;
        hudMessage(h, matchPoint ? "BALLE DE MATCH" : "GO GO GO", "", matchPoint ? theme::gold : theme::accent, 1.6f);
        break;
    }
    case EV_ROUND_START:
        if (w.mode == MODE_COMPETITIVE) {
            Team lt = localId >= 0 ? w.players[localId].team : TEAM_NONE;
            hudMessage(h, ui::fmt("ROUND %d", w.round), lt == TEAM_ATK ? "ATTAQUANTS : posez le noyau sur A ou B" : "DÉFENSEURS : protégez les sites A et B",
                       lt == TEAM_ATK ? theme::atk : theme::def, 3.0f);
            if (localId >= 0 && w.players[localId].hasBomb) h.weaponListTime = h.time;
        }
        break;
    case EV_BOMB_PICKUP: if (e.a == localId) hudMessage(h, "VOUS AVEZ LE NOYAU", "Posez-le sur un site (maintenir TIR)", theme::gold, 2.5f); break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
void drawCrosshair(float cx, float cy, float dynGap, float sc) {
    const Settings& s = g_settings;
    Color c = s.crosshairColor();
    Color ol = {0, 0, 0, (unsigned char)(c.a * 0.9f)};
    float k = sc * 2.0f;
    float len = s.chSize * k, th = std::max(1.0f, roundf(s.chThickness * k)), gap = s.chGap * k + 2.0f * sc + dynGap;
    float o = s.chOutline ? std::max(1.0f, s.chOutlineThickness * sc) : 0;
    auto R = [&](float x, float y, float w, float h) {
        if (o > 0) DrawRectangleRec({floorf(x - o), floorf(y - o), floorf(w + 2 * o), floorf(h + 2 * o)}, ol);
        DrawRectangleRec({floorf(x), floorf(y), floorf(w), floorf(h)}, c);
    };
    if (s.chStyle == 3) {
        float rr = gap + len;
        if (o > 0) DrawRing({cx, cy}, rr - th / 2 - o, rr + th / 2 + o, 0, 360, 48, ol);
        DrawRing({cx, cy}, rr - th / 2, rr + th / 2, 0, 360, 48, c);
    } else if (s.chStyle != 2) {
        R(cx + gap, cy - th / 2, len, th);
        R(cx - gap - len, cy - th / 2, len, th);
        R(cx - th / 2, cy + gap, th, len);
        if (!s.chTStyle) R(cx - th / 2, cy - gap - len, th, len);
    }
    if (s.chDot || s.chStyle == 2) R(cx - th / 2, cy - th / 2, th, th);
}

static void drawBar(float x, float y, float w, float h, float frac, Color c) {
    DrawRectangleRec({x, y, w, h}, Color{0, 0, 0, 120});
    DrawRectangleRec({x, y, w * clampf(frac, 0, 1), h}, c);
}

static void drawIconHealth(float x, float y, float s, Color c) {
    DrawRectangleRec({x + s * 0.35f, y, s * 0.3f, s}, c);
    DrawRectangleRec({x, y + s * 0.35f, s, s * 0.3f}, c);
}
static void drawIconShield(float x, float y, float s, Color c, bool helmet) {
    DrawTriangle({x, y}, {x + s * 0.5f, y + s}, {x + s, y}, c);
    DrawRectangleRec({x, y - s * 0.15f, s, s * 0.3f}, c);
    if (helmet) DrawCircle((int)(x + s * 0.5f), (int)(y + s * 0.3f), s * 0.16f, Color{0, 0, 0, 200});
}

static void drawRadar(HudState& h, const World& w, int localId, int viewId) {
    if (!h.radarReady) return;
    float size = HS(250);
    float x0 = HS(20), y0 = HS(20);
    Rectangle area = {x0, y0, size, size};
    DrawRectangleRec(area, Color{8, 12, 18, 170});
    const Player& me = w.players[viewId >= 0 ? viewId : 0];
    const float K = 6.0f;   // radar texels per tile
    float zoom = g_settings.radarZoom * HS(1.0f) * 1.25f;   // screen px per texel
    float ppm = zoom * K / Map::TILE;                       // screen px per meter
    float yaw = g_settings.radarRotate ? me.yaw : 0;
    Vector2 c = {x0 + size / 2, y0 + size / 2};
    Vector3 center = g_settings.radarCentered ? me.pos : V3(0, 0, 0);
    if (!g_settings.radarCentered) { ppm = size / (w.map.w * Map::TILE); zoom = ppm * Map::TILE / K; yaw = 0; }
    BeginScissorMode((int)x0, (int)y0, (int)size, (int)size);
    float gx = center.x / Map::TILE + w.map.w * 0.5f, gz = center.z / Map::TILE + w.map.h * 0.5f;
    Rectangle src = {0, 0, (float)h.radarTex.width, (float)h.radarTex.height};
    Rectangle dst = {c.x, c.y, h.radarTex.width * zoom, h.radarTex.height * zoom};
    DrawTexturePro(h.radarTex, src, dst, {gx * K * zoom, gz * K * zoom}, -yaw, WHITE);
    auto toR = [&](Vector3 p) {
        float dx = (p.x - center.x) * ppm, dy = (p.z - center.z) * ppm;
        float a = -yaw * DEG;
        return Vector2{c.x + dx * cosf(a) - dy * sinf(a), c.y + dx * sinf(a) + dy * cosf(a)};
    };
    // site letters
    if (w.mode == MODE_COMPETITIVE) {
        Vector2 a = toR(w.map.siteA), b = toR(w.map.siteB);
        text("A", a.x, a.y - HS(14), HS(26), Color{255, 255, 255, 170}, true, AL_CENTER);
        text("B", b.x, b.y - HS(14), HS(26), Color{255, 255, 255, 170}, true, AL_CENTER);
    }
    for (auto& s : w.smokes) { Vector2 p = toR(s.pos); DrawCircleV(p, 3.5f * ppm, Color{200, 200, 205, 150}); }
    for (auto& f : w.fires) { Vector2 p = toR(f.pos); DrawCircleV(p, f.radius * ppm, Color{255, 120, 40, 110}); }
    if (w.bomb.state == BOMB_DROPPED || w.bomb.state == BOMB_PLANTED) {
        if (localId < 0 || w.players[localId].team == TEAM_ATK || w.bomb.state == BOMB_PLANTED) {
            Vector2 p = toR(w.bomb.pos);
            bool blink = fmodf(h.time, 0.6f) < 0.35f;
            DrawRectangleV({p.x - HS(5), p.y - HS(4)}, {HS(10), HS(8)}, blink ? theme::bad : Color{120, 30, 30, 255});
        }
    }
    Team myTeam = localId >= 0 ? w.players[localId].team : me.team;
    for (auto& p : w.players) {
        if (!p.active || !p.alive) continue;
        bool friendly = w.mode != MODE_DEATHMATCH && p.team == myTeam;
        bool show = friendly || p.id == viewId || (h.time - h.spottedTime[p.id] < 0.25f);
        if (!show) continue;
        Vector2 rp = toR(p.pos);
        Color col = friendly ? teamCol(p.team) : theme::bad;
        if (p.id == viewId) col = WHITE;
        float r = HS(5);
        DrawCircleV(rp, r + 1.5f, BLACK);
        DrawCircleV(rp, r, col);
        // view direction tick
        float a = (p.yaw - yaw) * DEG;
        Vector2 d = {sinf(a), -cosf(a)};
        DrawLineEx(rp, {rp.x + d.x * r * 2.4f, rp.y + d.y * r * 2.4f}, 2, col);
        if (p.hasBomb && friendly) DrawRectangleV({rp.x - 2, rp.y - 2}, {4, 4}, theme::bad);
    }
    EndScissorMode();
    DrawRectangleLinesEx(area, 1, Color{255, 255, 255, 40});
    uint8_t z = w.map.zoneAt(me.pos);
    const char* zn = (z & ZONE_SITE_A) ? "SITE A" : (z & ZONE_SITE_B) ? "SITE B" : (z & ZONE_BUY_ATK) ? "SPAWN ATTAQUE" : (z & ZONE_BUY_DEF) ? "SPAWN DÉFENSE" : "";
    if (zn[0]) text(zn, x0 + HS(6), y0 + size + HS(4), HS(16), theme::dim, true);
}

static void drawTopBar(HudState& h, const World& w, int localId) {
    float cx = GetScreenWidth() / 2.0f, y = HS(14);
    Team lt = localId >= 0 ? w.players[localId].team : TEAM_DEF;
    if (w.mode == MODE_DEATHMATCH) {
        float t = w.roundTimeLeft();
        panel({cx - HS(90), y, HS(180), HS(54)}, Color{10, 14, 20, 200});
        text(fmt("%d:%02d", (int)t / 60, (int)t % 60), cx, y + HS(6), HS(40), hudCol(), true, AL_CENTER);
        // leader
        int best = -1, bk = -1;
        for (auto& p : w.players) if (p.active && p.kills > bk) { bk = p.kills; best = p.id; }
        if (best >= 0) text(fmt("EN TÊTE : %s (%d)  •  OBJECTIF %d", w.players[best].name, bk, w.dmKillLimit), cx, y + HS(60), HS(17), theme::dim, true, AL_CENTER);
        return;
    }
    if (w.mode != MODE_COMPETITIVE) return;
    int mySquad = localId >= 0 ? w.players[localId].squad : 0;
    Team leftT = lt, rightT = otherTeam(lt);
    int ls = w.squadScore[mySquad], rs = w.squadScore[1 - mySquad];
    // timer box
    float t = w.roundTimeLeft();
    bool bombPlanted = w.bomb.state == BOMB_PLANTED;
    panel({cx - HS(70), y, HS(140), HS(58)}, Color{10, 14, 20, 210});
    Color tc = bombPlanted ? theme::bad : (w.phase == PH_FREEZE ? theme::gold : hudCol());
    if (bombPlanted) {
        bool blink = fmodf(h.time, std::max(0.12f, t / 40.f)) < 0.08f;
        DrawRectangleRec({cx - HS(70), y + HS(54), HS(140), HS(4)}, blink ? theme::bad : Color{120, 30, 30, 255});
    }
    text(fmt("%d:%02d", (int)t / 60, (int)t % 60), cx, y + HS(8), HS(40), tc, true, AL_CENTER);
    // scores
    panel({cx - HS(150), y, HS(76), HS(58)}, withAlpha(teamCol(leftT), 0.85f));
    panel({cx + HS(74), y, HS(76), HS(58)}, withAlpha(teamCol(rightT), 0.85f));
    text(fmt("%d", ls), cx - HS(112), y + HS(6), HS(42), WHITE, true, AL_CENTER);
    text(fmt("%d", rs), cx + HS(112), y + HS(6), HS(42), WHITE, true, AL_CENTER);
    // alive tiles
    auto tiles = [&](Team t, bool left) {
        int i = 0;
        for (auto& p : w.players) {
            if (!p.active || p.team != t) continue;
            float bx = left ? cx - HS(160) - HS(40) * (i + 1) : cx + HS(160) + HS(40) * i;
            Color c = p.alive ? teamCol(t) : Color{40, 44, 52, 220};
            panel({bx, y, HS(36), HS(58)}, withAlpha(c, p.alive ? 0.9f : 0.8f));
            if (p.alive) DrawRectangleRec({bx, y + HS(54), HS(36) * p.health / 100.f, HS(4)}, WHITE);
            else { DrawLineEx({bx + HS(8), y + HS(14)}, {bx + HS(28), y + HS(44)}, 3, Color{255, 255, 255, 90}); DrawLineEx({bx + HS(28), y + HS(14)}, {bx + HS(8), y + HS(44)}, 3, Color{255, 255, 255, 90}); }
            text(fmt("%.1s", p.name), bx + HS(18), y + HS(14), HS(24), p.alive ? WHITE : theme::dim, true, AL_CENTER);
            if (p.hasBomb && p.team == lt) DrawRectangleRec({bx + HS(26), y + HS(4), HS(7), HS(7)}, theme::bad);
            i++;
        }
    };
    tiles(leftT, true);
    tiles(rightT, false);
    const char* label = w.phase == PH_FREEZE ? "PHASE D'ACHAT" : (w.overtime ? "PROLONGATIONS" : fmt("ROUND %d / %d", w.round, w.maxRounds()));
    text(label, cx, y + HS(64), HS(15), theme::dim, true, AL_CENTER);
}

static void drawKillfeed(HudState& h, const World& w) {
    if (!g_settings.showKillfeed) return;
    float x = GetScreenWidth() - HS(20), y = HS(20);
    for (int i = (int)h.feed.size() - 1, n = 0; i >= 0 && n < 6; i--) {
        auto& k = h.feed[i];
        float age = h.time - k.time;
        if (age > 7) continue;
        float a = clampf((7 - age) / 0.5f, 0, 1);
        float fs = HS(18);
        std::string tags;
        if (k.flags & 16) tags += " [AVEUGLE]";
        if (k.flags & 4) tags += " [NOSCOPE]";
        if (k.flags & 8) tags += " [FUMÉE]";
        if (k.flags & 2) tags += " [MUR]";
        if (k.flags & 1) tags += " [HS]";
        std::string wn = std::string(" ") + weaponShortName(k.w) + tags + " ";
        if (k.w == W_NONE || k.w == W_BOMB) wn = k.w == W_BOMB ? " NOYAU " : " CHUTE ";
        float wk = textWidth(k.killer.c_str(), fs, true), ww = textWidth(wn.c_str(), fs, false), wv = textWidth(k.victim.c_str(), fs, true);
        float total = wk + ww + wv + HS(24);
        Rectangle r = {x - total, y, total, HS(30)};
        panel(r, withAlpha(k.local ? Color{60, 18, 24, 255} : Color{10, 14, 20, 255}, 0.75f * a));
        if (k.local) DrawRectangleLinesEx(r, 2, withAlpha(theme::bad, a));
        float tx = r.x + HS(12);
        Color kc = w.mode == MODE_DEATHMATCH ? theme::text : teamCol(k.kt), vc = w.mode == MODE_DEATHMATCH ? theme::text : teamCol(k.vt);
        if (!k.killer.empty()) { text(k.killer.c_str(), tx, r.y + HS(4), fs, withAlpha(kc, a), true); }
        tx += wk;
        text(wn.c_str(), tx, r.y + HS(4), fs, withAlpha((k.flags & 1) ? theme::gold : theme::text, a));
        tx += ww;
        text(k.victim.c_str(), tx, r.y + HS(4), fs, withAlpha(vc, a), true);
        y += HS(34);
        n++;
    }
}

static void drawPlayerStatus(HudState& h, const World& w, const Player& p) {
    float H = (float)GetScreenHeight(), W = (float)GetScreenWidth();
    float y = H - HS(80);
    // health & armor
    panel({HS(20), y, HS(330), HS(60)}, Color{10, 14, 20, 170});
    Color hc = p.health > 25 ? hudCol() : theme::bad;
    if (h.time - p.lastHurtTime < 0.3f) hc = theme::bad;
    drawIconHealth(HS(36), y + HS(18), HS(24), hc);
    text(fmt("%d", p.health), HS(70), y + HS(6), HS(44), hc, true);
    drawBar(HS(70), y + HS(52), HS(110), HS(4), p.health / 100.f, hc);
    drawIconShield(HS(196), y + HS(20), HS(22), p.armor > 0 ? hudCol() : theme::dim, p.helmet);
    text(fmt("%d", p.armor), HS(228), y + HS(6), HS(44), p.armor > 0 ? hudCol() : theme::dim, true);
    drawBar(HS(228), y + HS(52), HS(100), HS(4), p.armor / 100.f, hudCol(0.8f));
    // money
    if (w.mode == MODE_COMPETITIVE) {
        text(fmt("$ %d", p.money), HS(24), y - HS(44), HS(32), theme::good, true);
        if (p.kit) text("KIT", HS(170), y - HS(38), HS(20), theme::def, true);
    }
    // ammo
    const WeaponDef& wd = weaponDef(p.activeW);
    panel({W - HS(330), y, HS(310), HS(60)}, Color{10, 14, 20, 170});
    text(wd.name, W - HS(316), y + HS(8), HS(18), theme::dim, true);
    if (isGun(p.activeW)) {
        int slot = wd.slot == SLOT_PRIMARY ? 0 : 1;
        int clip = p.clip[slot], res = p.reserve[slot];
        Color ac = clip <= wd.magSize / 4 ? theme::bad : hudCol();
        text(fmt("%d", clip), W - HS(110), y + HS(4), HS(48), ac, true, AL_RIGHT);
        text(w.infiniteAmmo ? "/ INF" : fmt("/ %d", res), W - HS(100), y + HS(20), HS(24), theme::dim, true);
        if (p.reloading) {
            float rp = clampf(1.0f - (p.reloadEnd - w.time) / wd.reloadTime, 0, 1);
            drawBar(W - HS(316), y + HS(36), HS(120), HS(5), rp, theme::accent);
            text("RECHARGEMENT", W - HS(316), y + HS(42), HS(14), theme::accent, true);
        }
    } else if (isGrenade(p.activeW)) {
        text(p.pinPulled ? "GOUPILLE RETIRÉE" : "", W - HS(316), y + HS(32), HS(16), theme::gold, true);
    }
    // inventory strip (grenades / bomb)
    float ix = W - HS(330), iy = y - HS(38);
    const char* nadeNames[4] = {"FL", "SM", "HE", "IN"};
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < p.nades[i]; k++) {
            bool act = (p.activeW == W_FLASH + i);
            panel({ix, iy, HS(40), HS(28)}, act ? theme::accent : Color{10, 14, 20, 170});
            text(nadeNames[i], ix + HS(20), iy + HS(4), HS(18), act ? BLACK : theme::text, true, AL_CENTER);
            ix += HS(44);
        }
    if (p.hasBomb) {
        bool blink = fmodf(h.time, 1.0f) < 0.5f;
        panel({ix, iy, HS(80), HS(28)}, blink ? theme::bad : Color{120, 30, 30, 220});
        text("NOYAU", ix + HS(40), iy + HS(4), HS(18), WHITE, true, AL_CENTER);
    }
    // weapon list (on switch)
    if (h.time - h.weaponListTime < 1.6f) {
        float a = clampf((1.6f - (h.time - h.weaponListTime)) / 0.4f, 0, 1);
        float ly = H * 0.5f;
        WeaponId list[5] = {p.primary, p.secondary, W_KNIFE, W_NONE, p.hasBomb ? W_BOMB : W_NONE};
        for (int i = 0; i < 4; i++) if (p.nades[i]) { list[3] = (WeaponId)(W_FLASH + i); break; }
        for (int i = 0; i < 5; i++) {
            if (list[i] == W_NONE) continue;
            bool act = list[i] == p.activeW || (i == 3 && isGrenade(p.activeW));
            Rectangle r = {W - HS(240), ly, HS(220), HS(34)};
            panel(r, withAlpha(act ? Color{30, 60, 80, 255} : Color{10, 14, 20, 255}, 0.7f * a));
            text(fmt("%d", i + 1), r.x + HS(10), r.y + HS(6), HS(18), withAlpha(theme::dim, a), true);
            text(weaponDef(list[i]).name, r.x + r.width - HS(12), r.y + HS(6), HS(18), withAlpha(act ? theme::accent : theme::text, a), true, AL_RIGHT);
            ly += HS(38);
        }
    }
}

static void drawScope(const World& w, const Player& p) {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float r = H * 0.48f;
    Vector2 c = {W / 2, H / 2};
    // vignette ring
    DrawRing(c, r, std::max(W, H), 0, 360, 96, BLACK);
    DrawRing(c, r - 6, r, 0, 360, 96, Color{0, 0, 0, 200});
    DrawRectangle(0, 0, (int)(c.x - r + 2), (int)H, BLACK);
    DrawRectangle((int)(c.x + r - 2), 0, (int)(W - c.x - r + 2), (int)H, BLACK);
    if (g_settings.chSniperLines) {
        DrawLineEx({c.x - r, c.y}, {c.x + r, c.y}, 1.5f, BLACK);
        DrawLineEx({c.x, c.y - r}, {c.x, c.y + r}, 1.5f, BLACK);
    }
    // scoped inaccuracy blur ring
    float inacc = w.currentInaccuracy(p);
    if (inacc > 0.3f) {
        float rr = clampf(inacc * 30.f, 0, r);
        DrawRing(c, rr, rr + 3, 0, 360, 48, Color{0, 0, 0, 110});
    }
}

void hudDraw(HudState& h, const World& w, int localId, int viewId, const ViewState& v, float dt) {
    h.time += dt;
    h.frameTimes[h.ftIdx = (h.ftIdx + 1) % 160] = dt;
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    if (viewId < 0) viewId = localId;
    const Player* vp = viewId >= 0 ? &w.players[viewId] : nullptr;

    // spotting for radar
    static int spotIdx = 0;
    spotIdx++;
    if (localId >= 0 && spotIdx % 3 == 0) {
        for (auto& e : w.players) {
            if (!e.active || !e.alive || !w.isEnemy(localId, e.id)) continue;
            for (auto& t : w.players) {
                if (!t.active || !t.alive || (w.isEnemy(localId, t.id) && t.id != localId)) continue;
                if (w.mode == MODE_DEATHMATCH && t.id != localId) continue;
                if (w.canSee(t.id, e.id, true)) { h.spottedTime[e.id] = h.time; break; }
            }
        }
    }

    // flash overlay
    if (vp && vp->alive && w.time < vp->flashEnd) {
        float left = vp->flashEnd - w.time;
        float a = clampf(left / std::max(0.1f, vp->flashDur * 0.55f), 0, 1);
        DrawRectangle(0, 0, (int)W, (int)H, Color{255, 255, 255, (unsigned char)(255 * a)});
    }
    // inside smoke overlay
    for (auto& s : w.smokes) {
        float age = w.time - s.start, left = s.end - w.time;
        float r = 3.5f * clampf(age / 0.7f, 0, 1) * clampf(left / 1.5f, 0, 1);
        Vector3 c = s.pos + V3(0, 1.4f, 0);
        Vector3 d = v.eye - c; d.y *= 1.6f;
        float dist = len3(d);
        if (dist < r) {
            float a = clampf((r - dist) / 1.0f, 0, 1);
            DrawRectangle(0, 0, (int)W, (int)H, Color{190, 192, 198, (unsigned char)(250 * a)});
        }
    }
    bool scoped = vp && vp->alive && vp->scope > 0;
    if (scoped) drawScope(w, *vp);

    // damage direction
    for (size_t i = 0; i < h.dmg.size();) {
        float age = h.time - h.dmg[i].time;
        if (age > 1.2f) { h.dmg.erase(h.dmg.begin() + i); continue; }
        if (vp) {
            float rel = angleDiff(h.dmg[i].yaw, vp->yaw) * DEG;
            float a = clampf(1.2f - age, 0, 1);
            Vector2 c = {W / 2, H / 2};
            float rad = HS(150);
            float ang = rel / DEG - 90;
            DrawRing(c, rad, rad + HS(10), ang - 18, ang + 18, 16, Color{255, 40, 40, (unsigned char)(200 * a)});
        }
        i++;
    }

    // crosshair
    if (vp && vp->alive && !scoped) {
        float dyn = 0;
        float vfov = v.fovY;
        float pxPerDeg = H / (2 * tanf(vfov * 0.5f * DEG)) * DEG;
        if (g_settings.chStyle == 1) dyn = w.currentInaccuracy(*vp) * pxPerDeg * 0.6f;
        float cx = W / 2, cy = H / 2;
        float vpf = 0.5f * g_settings.viewPunchScale;
        if (g_settings.chFollowRecoil) {
            cx += vp->punchYaw * (1 - vpf) * pxPerDeg;
            cy -= vp->punchPitch * (1 - vpf) * pxPerDeg;
        }
        if (vp->activeW != W_KNIFE && !(weaponDef(vp->activeW).cls == WC_SNIPER)) drawCrosshair(cx, cy, dyn, ui::scale());
        else if (weaponDef(vp->activeW).cls == WC_SNIPER) DrawCircle((int)cx, (int)cy, 1.5f * ui::scale(), g_settings.crosshairColor());
        else drawCrosshair(cx, cy, 0, ui::scale() * 0.6f);
        if (g_settings.showRecoilCrosshair && (fabsf(vp->punchPitch) > 0.05f || fabsf(vp->punchYaw) > 0.05f)) {
            float rx = W / 2 + vp->punchYaw * (1 - vpf) * pxPerDeg, ry = H / 2 - vp->punchPitch * (1 - vpf) * pxPerDeg;
            DrawCircleLines((int)rx, (int)ry, 3, Color{255, 80, 80, 200});
        }
    }
    // hitmarker
    if (g_settings.showHitmarker && h.time - h.hitTime < 0.25f) {
        float a = 1 - (h.time - h.hitTime) / 0.25f;
        float s = HS(h.hitKill ? 14 : 10), g = HS(5);
        Color c = h.hitKill ? Color{255, 60, 60, (unsigned char)(255 * a)} : (h.hitHead ? Color{255, 210, 70, (unsigned char)(255 * a)} : Color{255, 255, 255, (unsigned char)(255 * a)});
        float cx = W / 2, cy = H / 2;
        DrawLineEx({cx - g - s, cy - g - s}, {cx - g, cy - g}, 2.5f, c);
        DrawLineEx({cx + g + s, cy - g - s}, {cx + g, cy - g}, 2.5f, c);
        DrawLineEx({cx - g - s, cy + g + s}, {cx - g, cy + g}, 2.5f, c);
        DrawLineEx({cx + g + s, cy + g + s}, {cx + g, cy + g}, 2.5f, c);
    }
    // damage numbers
    for (size_t i = 0; i < h.numbers.size();) {
        float age = h.time - h.numbers[i].time;
        if (age > 0.9f) { h.numbers.erase(h.numbers.begin() + i); continue; }
        Vector2 sp = GetWorldToScreen(h.numbers[i].pos + V3(0, age * 0.6f, 0), g_renderer.lastCamera);
        text(fmt("%d", h.numbers[i].value), sp.x, sp.y, HS(h.numbers[i].head ? 26 : 20), withAlpha(h.numbers[i].head ? theme::gold : WHITE, 1 - age / 0.9f), true, AL_CENTER);
        i++;
    }

    if (w.mode != MODE_AIM) drawRadar(h, w, localId, viewId);
    drawTopBar(h, w, localId);
    drawKillfeed(h, w);
    if (vp && vp->alive) drawPlayerStatus(h, w, *vp);

    // plant / defuse progress
    if (vp && vp->alive) {
        float prog = -1; const char* lbl = "";
        if (vp->planting) { prog = (w.time - vp->plantStart) / 3.2f; lbl = "POSE DU NOYAU"; }
        if (vp->defusing && w.bomb.defuser == vp->id) { prog = (w.time - w.bomb.defuseStart) / (w.bomb.defuseEnd - w.bomb.defuseStart); lbl = vp->kit ? "DÉSAMORÇAGE (KIT)" : "DÉSAMORÇAGE"; }
        if (prog >= 0) {
            float bw = HS(360);
            panel({W / 2 - bw / 2 - HS(10), H * 0.68f - HS(36), bw + HS(20), HS(60)}, Color{10, 14, 20, 200});
            text(lbl, W / 2, H * 0.68f - HS(30), HS(18), theme::gold, true, AL_CENTER);
            drawBar(W / 2 - bw / 2, H * 0.68f, bw, HS(10), prog, theme::gold);
        }
        // hints
        if (w.bomb.state == BOMB_PLANTED && vp->team == TEAM_DEF && !vp->defusing && len2xz(vp->pos - w.bomb.pos) < 1.6f)
            text(fmt("Maintenez [%s] pour désamorcer", bindName(g_settings.binds[ACT_USE])), W / 2, H * 0.62f, HS(22), theme::text, true, AL_CENTER);
        if (vp->activeW == W_BOMB && !vp->planting) {
            uint8_t z = w.map.zoneAt(vp->pos);
            bool in = z & (ZONE_SITE_A | ZONE_SITE_B);
            text(in ? "Maintenez TIR pour poser le noyau" : "Rejoignez un site pour poser le noyau", W / 2, H * 0.62f, HS(22), in ? theme::gold : theme::dim, true, AL_CENTER);
        }
        if (w.canBuy(vp->id) && w.mode == MODE_COMPETITIVE) {
            float left = w.phase == PH_FREEZE ? (w.buyEndTime - w.time) : (w.buyEndTime - w.time);
            text(fmt("[%s] ACHAT  %ds", bindName(g_settings.binds[ACT_BUYMENU]), (int)left), HS(24), H - HS(160), HS(18), theme::good, true);
        }
    }

    // center message
    if (h.time - h.center.time < h.center.dur && !h.center.text.empty()) {
        float age = h.time - h.center.time;
        float a = clampf(std::min(age / 0.15f, (h.center.dur - age) / 0.4f), 0, 1);
        float y = H * 0.24f;
        float sc = 1.0f + 0.15f * clampf(1 - age / 0.2f, 0, 1);
        textShadow(h.center.text.c_str(), W / 2, y, HS(52) * sc, withAlpha(h.center.col, a), true, AL_CENTER);
        if (!h.center.sub.empty()) textShadow(h.center.sub.c_str(), W / 2, y + HS(62), HS(22), withAlpha(theme::text, a), false, AL_CENTER);
    }
    // round end banner
    if (h.time - h.roundBannerTime < 5.0f && w.mode == MODE_COMPETITIVE) {
        float age = h.time - h.roundBannerTime;
        float a = clampf(std::min(age / 0.2f, (5.0f - age) / 0.5f), 0, 1);
        float bh = HS(150);
        float y = H * 0.18f;
        Color c = h.roundWon ? Color{20, 120, 90, 255} : Color{140, 30, 40, 255};
        DrawRectangleGradientH(0, (int)y, (int)(W / 2), (int)bh, withAlpha(c, 0), withAlpha(c, 0.85f * a));
        DrawRectangleGradientH((int)(W / 2), (int)y, (int)(W / 2), (int)bh, withAlpha(c, 0.85f * a), withAlpha(c, 0));
        textShadow(h.roundWon ? "ROUND REMPORTÉ" : "ROUND PERDU", W / 2, y + HS(14), HS(56), withAlpha(WHITE, a), true, AL_CENTER);
        text(h.roundReason.c_str(), W / 2, y + HS(80), HS(20), withAlpha(theme::text, a), false, AL_CENTER);
        if (!h.roundMvp.empty()) text(fmt("MVP : %s", h.roundMvp.c_str()), W / 2, y + HS(110), HS(20), withAlpha(theme::gold, a), true, AL_CENTER);
    }
    // spectator label
    if (localId >= 0 && !w.players[localId].alive && vp && viewId != localId && vp->alive) {
        panel({W / 2 - HS(220), H - HS(150), HS(440), HS(56)}, Color{10, 14, 20, 200});
        text("SPECTATEUR", W / 2, H - HS(146), HS(15), theme::dim, true, AL_CENTER);
        text(vp->name, W / 2, H - HS(128), HS(26), teamCol(vp->team), true, AL_CENTER);
    }
    if (localId >= 0 && !w.players[localId].alive && (w.mode == MODE_DEATHMATCH || w.mode == MODE_PRACTICE)) {
        float t = std::max(0.f, w.players[localId].respawnTime - w.time);
        textShadow(fmt("RÉAPPARITION DANS %.1f", t), W / 2, H * 0.4f, HS(34), theme::text, true, AL_CENTER);
    }
    // fps / net graph
    if (g_settings.showFps || g_settings.showNetGraph) {
        float avg = 0; for (float f : h.frameTimes) avg += f; avg /= 160;
        const char* s = fmt("%d FPS  %.2f ms  TICK %d%s", GetFPS(), avg * 1000, TICK_RATE, h.pingMs > 0 ? fmt("  PING %d", h.pingMs) : "");
        text(s, W - HS(12), H - HS(22), HS(15), Color{200, 210, 220, 200}, true, AL_RIGHT);
        if (g_settings.showNetGraph) {
            float gx = W - HS(330), gy = H - HS(130), gw = HS(320), gh = HS(40);
            DrawRectangleRec({gx, gy, gw, gh}, Color{0, 0, 0, 120});
            for (int i = 0; i < 160; i++) {
                float ft = h.frameTimes[(h.ftIdx + 1 + i) % 160];
                float hh = clampf(ft / 0.033f, 0, 1) * gh;
                Color c = ft > 0.0167f ? theme::bad : (ft > 0.0069f ? theme::gold : theme::good);
                DrawRectangleRec({gx + i * gw / 160, gy + gh - hh, gw / 160, hh}, c);
            }
            if (h.lossPct > 0) text(fmt("PERTE %.0f%%", h.lossPct), gx, gy - HS(18), HS(14), theme::bad, true);
        }
    }
}

// ---------------------------------------------------------------------------
struct BuyCat { const char* name; std::vector<int> items; };
static std::vector<BuyCat> buyCats() {
    return {
        {"PISTOLETS", {W_VIPER, W_MAMBA, W_HORNET}},
        {"SMG & POMPE", {W_WASP, W_BREACH}},
        {"FUSILS", {W_RAPTOR, W_VANGUARD, W_SPECTER}},
        {"PRÉCISION & LOURD", {W_LANCER, W_ORACLE, W_TITAN}},
        {"ÉQUIPEMENT", {BUY_KEVLAR, BUY_HELMET, BUY_KIT}},
        {"GRENADES", {W_FLASH, W_SMOKE, W_FRAG, W_MOLLY}},
    };
}

static int itemPrice(const World& w, const Player& p, int item) {
    if (w.mode == MODE_DEATHMATCH) return 0;
    if (item < W_COUNT) return weaponDef((WeaponId)item).price;
    if (item == BUY_KEVLAR) return EQ_KEVLAR_PRICE;
    if (item == BUY_HELMET) return p.armor >= 100 ? EQ_HELMET_PRICE - EQ_KEVLAR_PRICE : EQ_HELMET_PRICE;
    return EQ_KIT_PRICE;
}

int hudBuyMenu(HudState& h, const World& w, int localId, bool& close) {
    const Player& p = w.players[localId];
    auto cats = buyCats();
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    DrawRectangle(0, 0, (int)W, (int)H, Color{0, 0, 0, 120});
    float pw = S(1240), ph = S(700);
    Rectangle pr = {W / 2 - pw / 2, H / 2 - ph / 2, pw, ph};
    panel(pr, theme::bg);
    DrawRectangle((int)pr.x, (int)pr.y, (int)pw, (int)S(4), theme::accent);
    text("ARMURERIE", pr.x + S(30), pr.y + S(22), S(34), theme::text, true);
    if (w.mode == MODE_COMPETITIVE) {
        text(fmt("$ %d", p.money), pr.x + pw - S(30), pr.y + S(22), S(34), theme::good, true, AL_RIGHT);
        float left = w.buyEndTime - w.time;
        text(fmt("Temps d'achat : %ds   •   Clic droit : revendre (ce round)", (int)std::max(0.f, left)), pr.x + pw - S(30), pr.y + S(64), S(16), theme::dim, false, AL_RIGHT);
    } else text(w.mode == MODE_DEATHMATCH ? "Choisissez votre arsenal de réapparition (gratuit)" : "Mode libre : tout est gratuit", pr.x + pw - S(30), pr.y + S(30), S(18), theme::dim, false, AL_RIGHT);

    int result = 0;
    // categories
    for (int i = 0; i < (int)cats.size(); i++) {
        Rectangle r = {pr.x + S(30), pr.y + S(100) + i * S(76), S(280), S(66)};
        bool act = h.buyCategory == i;
        bool hv = hovered(r);
        panel(r, act ? Color{30, 60, 80, 255} : (hv ? Color{30, 38, 56, 255} : theme::panel));
        if (act) DrawRectangle((int)r.x, (int)r.y, (int)S(4), (int)r.height, theme::accent);
        text(fmt("%d", i + 1), r.x + S(18), r.y + S(20), S(22), theme::dim, true);
        text(cats[i].name, r.x + S(50), r.y + S(20), S(21), act ? theme::accent : theme::text, true);
        if (hv && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { h.buyCategory = i; g_audio.play2D(SFX_UI_CLICK, 0.6f, 1, BUS_UI); }
    }
    // keyboard: number keys pick category / item
    static int stage = 0;
    for (int k = 0; k < 9; k++) {
        if (!IsKeyPressed(KEY_ONE + k)) continue;
        if (stage == 0) { if (k < (int)cats.size()) { h.buyCategory = k; stage = 1; } }
        else { auto& it = cats[h.buyCategory].items; if (k < (int)it.size()) result = it[k]; stage = 0; }
    }
    // items
    auto& items = cats[h.buyCategory].items;
    for (int i = 0; i < (int)items.size(); i++) {
        int item = items[i];
        int col = i % 2, row = i / 2;
        Rectangle r = {pr.x + S(340) + col * S(440), pr.y + S(100) + row * S(250), S(420), S(234)};
        int price = itemPrice(w, p, item);
        bool afford = w.infiniteMoney || p.money >= price;
        bool owned = (item < W_COUNT && p.hasWeapon((WeaponId)item) && !isGrenade((WeaponId)item)) || (item == BUY_KIT && p.kit) ||
                     (item == BUY_HELMET && p.helmet && p.armor >= 100) || (item == BUY_KEVLAR && p.armor >= 100);
        bool blocked = item == BUY_KIT && p.team != TEAM_DEF;
        bool hv = hovered(r);
        panel(r, hv ? Color{28, 36, 54, 255} : theme::panel);
        frame(r, owned ? theme::good : (hv ? theme::accent : Color{255, 255, 255, 20}), owned ? 2.f : 1.f);
        text(fmt("%d", i + 1), r.x + S(16), r.y + S(14), S(18), theme::dim, true);
        text(buyItemName(item), r.x + S(44), r.y + S(10), S(26), blocked ? theme::dim : theme::text, true);
        text(price == 0 ? "GRATUIT" : fmt("$ %d", price), r.x + r.width - S(16), r.y + S(14), S(22), afford ? theme::good : theme::bad, true, AL_RIGHT);
        if (item < W_COUNT) {
            const WeaponDef& d = weaponDef((WeaponId)item);
            text(d.desc, r.x + S(16), r.y + S(52), S(15), theme::dim);
            if (isGun((WeaponId)item)) {
                struct St { const char* n; float v; } st[5] = {
                    {"Dégâts", d.damage * d.pellets / 115.f}, {"Cadence", d.rpm / 900.f}, {"Mobilité", d.maxSpeed / 5.4f},
                    {"Perforation", d.armorPen}, {"Précision", 1.0f - clampf(d.spreadMove / 5.f + d.recoilScale / 6.f, 0, 0.95f)}};
                for (int k = 0; k < 5; k++) {
                    float yy = r.y + S(84) + k * S(24);
                    text(st[k].n, r.x + S(16), yy, S(15), theme::text);
                    drawBar(r.x + S(130), yy + S(6), S(170), S(6), st[k].v, theme::accent);
                }
                text(fmt("%d / %d   •   Prime $%d", d.magSize, d.reserve, d.killReward), r.x + S(16), r.y + S(206), S(15), theme::dim);
            } else {
                int idx = item - W_FLASH;
                text(fmt("Possédées : %d", p.nades[idx]), r.x + S(16), r.y + S(84), S(18), theme::text);
            }
        } else {
            const char* desc = item == BUY_KEVLAR ? "Réduit fortement les dégâts au torse." : item == BUY_HELMET ? "Protège aussi la tête : évite les one-shots au pistolet." : "Divise par deux le temps de désamorçage (10s → 5s).";
            text(desc, r.x + S(16), r.y + S(52), S(15), theme::dim);
            if (blocked) text("Réservé aux défenseurs", r.x + S(16), r.y + S(90), S(18), theme::bad, true);
        }
        if (hv && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) result = item;
        if (hv && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) result = -item;
    }
    // current loadout
    float ly = pr.y + ph - S(60);
    std::string loadout = std::string("ÉQUIPEMENT :  ") + (p.primary != W_NONE ? weaponDef(p.primary).name : "-") + "  •  " +
                          (p.secondary != W_NONE ? weaponDef(p.secondary).name : "-") + "  •  " + (p.armor >= 100 ? (p.helmet ? "GILET+CASQUE" : "GILET") : "SANS ARMURE");
    for (int i = 0; i < 4; i++) for (int k = 0; k < p.nades[i]; k++) loadout += std::string("  •  ") + weaponDef((WeaponId)(W_FLASH + i)).name;
    text(loadout.c_str(), pr.x + S(30), ly, S(17), theme::text, true);
    Rectangle closeR = {pr.x + pw - S(180), ly - S(12), S(150), S(44)};
    if (button(closeR, "FERMER")) close = true;
    if (!w.canBuy(localId)) close = true;
    return result;
}

void hudScoreboard(const World& w, int localId) {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float pw = S(1180), ph = S(820);
    Rectangle pr = {W / 2 - pw / 2, H / 2 - ph / 2, pw, ph};
    panel(pr, theme::bg);
    DrawRectangle((int)pr.x, (int)pr.y, (int)pw, (int)S(4), theme::accent);
    const char* title = w.mode == MODE_COMPETITIVE ? "COMPÉTITIF  •  CITADEL" : (w.mode == MODE_DEATHMATCH ? "DEATHMATCH  •  CITADEL" : "ENTRAÎNEMENT");
    text(title, pr.x + S(30), pr.y + S(20), S(26), theme::text, true);
    int rounds = std::max(1, (int)w.history.size());
    auto table = [&](float y, Team t, bool all) {
        float x = pr.x + S(30);
        const char* hdr[] = {"JOUEUR", "K", "D", "A", "ADR", "HS%", "MVP", "$", "PING"};
        float cols[] = {0, 430, 490, 550, 620, 700, 780, 860, 980};
        Color tc = all ? theme::text : teamCol(t);
        if (!all) {
            int sq = w.squadOfTeam(t);
            text(fmt("%s  •  %d", t == TEAM_ATK ? "ATTAQUANTS" : "DÉFENSEURS", w.squadScore[sq]), x, y, S(20), tc, true);
            y += S(30);
        }
        for (int i = 0; i < 9; i++) text(hdr[i], x + S(cols[i]), y, S(15), theme::dim, true);
        y += S(26);
        std::vector<const Player*> list;
        for (auto& p : w.players) if (p.active && (all || p.team == t)) list.push_back(&p);
        std::sort(list.begin(), list.end(), [](const Player* a, const Player* b) { return a->score != b->score ? a->score > b->score : a->kills > b->kills; });
        for (auto* p : list) {
            bool me = p->id == localId;
            Rectangle r = {x - S(10), y - S(4), pw - S(40), S(34)};
            panel(r, me ? Color{40, 60, 80, 200} : Color{20, 26, 38, 180});
            DrawRectangle((int)r.x, (int)r.y, (int)S(4), (int)r.height, all ? theme::accent : teamCol(p->team));
            Color nc = p->alive ? theme::text : theme::dim;
            text(fmt("%s%s", p->name, p->isBot ? "  [BOT]" : ""), x + S(cols[0]), y + S(2), S(19), nc, true);
            if (p->hasBomb && localId >= 0 && w.players[localId].team == p->team) text("NOYAU", x + S(330), y + S(4), S(14), theme::bad, true);
            text(fmt("%d", p->kills), x + S(cols[1]), y + S(2), S(19), nc, true);
            text(fmt("%d", p->deaths), x + S(cols[2]), y + S(2), S(19), nc);
            text(fmt("%d", p->assists), x + S(cols[3]), y + S(2), S(19), nc);
            text(fmt("%d", p->damage / rounds), x + S(cols[4]), y + S(2), S(19), nc);
            text(fmt("%d", p->kills > 0 ? p->headshots * 100 / std::max(1, p->kills) : 0), x + S(cols[5]), y + S(2), S(19), nc);
            text(fmt("%d", p->mvps), x + S(cols[6]), y + S(2), S(19), theme::gold);
            bool showMoney = localId < 0 || w.players[localId].team == p->team || w.mode != MODE_COMPETITIVE;
            text(showMoney ? fmt("%d", p->money) : "-", x + S(cols[7]), y + S(2), S(19), theme::good);
            text(fmt("%d", p->isBot ? 0 : p->ping), x + S(cols[8]), y + S(2), S(19), theme::dim);
            y += S(38);
        }
        return y;
    };
    float y = pr.y + S(70);
    if (w.mode == MODE_COMPETITIVE) {
        Team mine = localId >= 0 ? w.players[localId].team : TEAM_DEF;
        y = table(y, mine, false) + S(20);
        y = table(y, otherTeam(mine), false) + S(10);
        // round history
        float hx = pr.x + S(30), hy = pr.y + ph - S(70);
        text("HISTORIQUE", hx, hy - S(24), S(15), theme::dim, true);
        int mySquad = localId >= 0 ? w.players[localId].squad : 0;
        for (size_t i = 0; i < w.history.size(); i++) {
            auto& r = w.history[i];
            Rectangle b = {hx + i * S(36), hy, S(32), S(32)};
            bool won = r.winnerSquad == mySquad;
            panel(b, withAlpha(won ? theme::good : theme::bad, 0.8f));
            const char* ic = r.reason == RE_BOMB_EXPLODED ? "B" : r.reason == RE_BOMB_DEFUSED ? "D" : r.reason == RE_TIME ? "T" : "E";
            text(ic, b.x + S(16), b.y + S(5), S(18), BLACK, true, AL_CENTER);
            if ((int)i == w.roundsToWin - 2) DrawRectangle((int)(b.x + S(34)), (int)b.y, 2, (int)S(32), WHITE);
        }
    } else {
        table(y, TEAM_NONE, true);
    }
}

} // namespace ax
