#include "hud.h"
#include "ui.h"
#include "config.h"
#include "audio.h"

namespace ax {

using namespace ui;

static float HS(float v) { return S(v) * g_settings.hudScale; }

void hudInit(HudState& h, const World& w) {
    if (h.minimapReady) { UnloadTexture(h.minimap); UnloadTexture(h.fogTex); UnloadImage(h.fogImg); }
    const int K = 3;
    Image img = GenImageColor(Map::N * K, Map::N * K, BLACK);
    for (int i = 0; i < Map::N * Map::N; i++) {
        Color c;
        switch (w.map.type[i]) {
        case CELL_ROCK: c = w.map.height[i] < Map::HIGH + 1 ? Color{70, 66, 58, 255} : Color{34, 36, 32, 255}; break;
        case CELL_PATH: c = {128, 116, 90, 255}; break;
        case CELL_WATER: c = {50, 96, 140, 255}; break;
        case CELL_BASE0: c = {96, 112, 140, 255}; break;
        case CELL_BASE1: c = {140, 104, 100, 255}; break;
        case CELL_GRASS: c = {66, 98, 56, 255}; break;
        case CELL_HIGH: c = {126, 120, 92, 255}; break;
        case CELL_RAMP: c = {112, 108, 86, 255}; break;
        default: c = {92, 104, 70, 255}; break;
        }
        ImageDrawRectangle(&img, (i % Map::N) * K, (i / Map::N) * K, K, K, c);
    }
    for (auto& b : w.map.beacons) ImageDrawCircle(&img, (int)(b.x / Map::CELL * K), (int)(b.y / Map::CELL * K), K + 1, Color{230, 220, 160, 255});
    h.minimap = LoadTextureFromImage(img);
    SetTextureFilter(h.minimap, TEXTURE_FILTER_BILINEAR);
    UnloadImage(img);
    h.fogImg = GenImageColor(Map::N, Map::N, BLANK);
    h.fogTex = LoadTextureFromImage(h.fogImg);
    SetTextureFilter(h.fogTex, TEXTURE_FILTER_BILINEAR);
    h.minimapReady = true;
}

void hudMessage(HudState& h, const std::string& s, Color c) {
    for (auto& m : h.msgs) if (m.text == s && h.time - m.time < 1.5f) { m.time = h.time; return; }
    h.msgs.push_back({s, c, h.time});
    if (h.msgs.size() > 4) h.msgs.erase(h.msgs.begin());
}

void hudOnEvent(HudState& h, const World& w, const GameEvent& e, int player) {
    int myTeam = player >= 0 ? w.players[player].team : 0;
    switch (e.type) {
    case EV_NO_GOLD: if (e.player == player) { hudMessage(h, e.c == 1 ? "Flux insuffisant" : "Cristaux insuffisants", e.c == 1 ? Color{110, 255, 150, 255} : theme::gold); g_audio.play2D(SFX_UI_DENY, 0.6f, 1, BUS_UI); } break;
    case EV_NO_SUPPLY: if (e.player == player) { hudMessage(h, "Ravitaillement insuffisant : construisez un Relais", theme::bad); g_audio.play2D(SFX_UI_DENY, 0.6f, 1, BUS_UI); } break;
    case EV_CANT_BUILD: if (e.player == player) { static const char* why[] = {"Emplacement invalide (terrain libre requis)", "Technologie requise manquante", "Trop près des ressources", "À construire sur un geyser de flux libre", "Trop près d'une autre base"};
            hudMessage(h, why[std::min<int>(e.c, 4)], theme::bad); g_audio.play2D(SFX_UI_DENY, 0.6f, 1, BUS_UI); } break;
    case EV_UNDER_ATTACK:
        if (e.player == player) {
            const Entity* t = w.get((uint16_t)e.a);
            hudMessage(h, t && t->kind == K_BUILDING ? "Votre base est attaquée !" : "Vos unités sont attaquées", theme::bad);
            g_audio.play2D(SFX_ALERT, 0.5f, 1, BUS_ANNOUNCER);
            h.pings.push_back(e.p); h.pingTimes.push_back(h.time);
        }
        break;
    case EV_TOWER_DOWN:
        hudMessage(h, e.c == myTeam ? "Une de vos tours est tombée" : "Tour ennemie détruite !", e.c == myTeam ? theme::bad : theme::good);
        break;
    case EV_TITAN_SLAIN:
        hudMessage(h, e.c == myTeam ? "Votre équipe a vaincu le TITAN : +20% dégâts pendant 2 min" : "L'ennemi a vaincu le TITAN", e.c == myTeam ? theme::gold : theme::bad);
        g_audio.play2D(SFX_TITAN, 0.7f, 1, BUS_ANNOUNCER);
        break;
    case EV_HERO_KILL: {
        int victimOwner = -1;
        const Entity& v = w.ents[(uint16_t)e.a];
        victimOwner = v.owner;
        std::string killer = e.player >= 0 ? w.players[e.player].name : "Neutre";
        std::string victim = victimOwner >= 0 ? w.players[victimOwner].name : "?";
        std::string txt = killer + "  a éliminé  " + victim + " (" + unitDef(v.type).name + ")";
        bool good = victimOwner >= 0 && w.players[victimOwner].team != myTeam;
        h.feed.push_back({txt, good ? theme::good : theme::bad, h.time});
        if (h.feed.size() > 6) h.feed.erase(h.feed.begin());
        g_audio.play2D(SFX_HERO_KILL, 0.6f, good ? 1.0f : 0.8f, BUS_ANNOUNCER);
        if (victimOwner == player) hudMessage(h, "Votre héros est tombé", theme::bad);
        break;
    }
    case EV_LEVELUP: if (e.player == player) { hudMessage(h, std::string("Niveau ") + std::to_string(e.c) + (e.c == 6 ? " : ULTIME DÉBLOQUÉ (R)" : ""), theme::gold); g_audio.play2D(SFX_LEVELUP, 0.6f); } break;
    case EV_BUILD_DONE: if (e.player == player) { hudMessage(h, std::string(unitDef(e.c).name) + " terminé(e)", theme::accent); g_audio.play2D(SFX_BUILD_DONE, 0.5f); } break;
    case EV_RESEARCH_DONE: if (e.player == player) { hudMessage(h, e.c == UP_WEAPONS ? "Amélioration des armes terminée" : "Amélioration du blindage terminée", theme::accent); g_audio.play2D(SFX_BUILD_DONE, 0.5f, 1.2f); } break;
    case EV_WAVE: break;
    case EV_ITEM: if (e.player == player) g_audio.play2D(SFX_COIN, 0.6f, 1, BUS_UI); break;
    case EV_RESPAWN: if (e.player == player) hudMessage(h, "Votre héros est de retour", theme::accent); break;
    case EV_GAME_START: hudMessage(h, "Bataille engagée : détruisez le Noyau ennemi", theme::accent); g_audio.play2D(SFX_WAVE, 0.6f, 1.2f, BUS_ANNOUNCER); break;
    default: break;
    }
}

static void bar(float x, float y, float w, float h, float f, Color c, Color bg = {0, 0, 0, 150}) {
    DrawRectangleRec({x, y, w, h}, bg);
    DrawRectangleRec({x, y, w * clampf(f, 0, 1), h}, c);
}

static const char* shortName(int t) {
    switch (t) {
    case U_DRONE: return "DR"; case U_WARDEN: return "GA"; case U_STRIKER: return "TI"; case U_LANCER: return "LA";
    case U_MENDER: return "SO"; case U_SIEGE: return "AR"; case B_HUB: return "CE"; case B_EXTRACTOR: return "EX"; default: return unitDef(t).name ? unitDef(t).name : "?";
    }
}

bool mouseOverHud(const HudState& h) {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    Vector2 m = GetMousePosition();
    if (CheckCollisionPointRec(m, h.minimapRect)) return true;
    if (m.y > H - HS(230) && m.x > HS(300) && m.x < W - HS(20)) return true;
    if (m.y < HS(46)) return true;
    if (h.shopOpen && CheckCollisionPointRec(m, {W / 2 - HS(420), HS(90), HS(840), HS(470)})) return true;
    return false;
}

static void drawMinimap(HudState& h, const World& w, int player, const Camera3D& cam, HudInput& in) {
    float size = HS(260) * g_settings.minimapSize;
    float H = (float)GetScreenHeight();
    Rectangle r = {HS(14), H - size - HS(14), size, size};
    h.minimapRect = r;
    DrawRectangleRec({r.x - 4, r.y - 4, r.width + 8, r.height + 8}, Color{8, 11, 16, 240});
    DrawTexturePro(h.minimap, {0, 0, (float)h.minimap.width, (float)h.minimap.height}, r, {0, 0}, 0, WHITE);
    int team = player >= 0 ? w.players[player].team : -1;
    // fog overlay
    if (team >= 0) {
        unsigned char* px = (unsigned char*)h.fogImg.data;
        for (int i = 0; i < Map::N * Map::N; i++) {
            unsigned char a = w.vis[team][i] ? 0 : (w.explored[team][i] ? 120 : 210);
            px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = 0; px[i * 4 + 3] = a;
        }
        UpdateTexture(h.fogTex, px);
        DrawTexturePro(h.fogTex, {0, 0, (float)Map::N, (float)Map::N}, r, {0, 0}, 0, WHITE);
    }
    float k = size / Map::SIZE;
    auto toMM = [&](Vector2 p) { return Vector2{r.x + p.x * k, r.y + p.y * k}; };
    for (auto& e : w.ents) {
        if (!e.alive || e.kind == K_RESOURCE) continue;
        if (!w.visibleTo(team, e)) {
            bool structure = e.kind == K_BUILDING || e.kind == K_TOWER;
            int ci = w.map.cellIndex(e.pos);
            if (!(structure && team >= 0 && ci >= 0 && w.explored[team][ci])) continue;
        }
        Color c = e.team == TEAM_NEUTRAL ? Color{200, 190, 160, 255} : (e.owner == player ? Color{90, 255, 130, 255} : (e.team == team ? Color{90, 160, 255, 255} : Color{255, 80, 70, 255}));
        Vector2 p = toMM(e.pos);
        if (e.kind == K_BUILDING || e.kind == K_TOWER) {
            float s = std::max(3.f, e.radius * 2 * k);
            DrawRectangleRec({p.x - s / 2, p.y - s / 2, s, s}, c);
            DrawRectangleLinesEx({p.x - s / 2, p.y - s / 2, s, s}, 1, BLACK);
        } else if (e.kind == K_HERO) {
            DrawCircleV(p, HS(6), BLACK);
            DrawCircleV(p, HS(5), e.owner >= 0 ? playerColor(e.owner) : c);
            if (e.owner == player) DrawCircleLines((int)p.x, (int)p.y, HS(7), WHITE);
        } else DrawRectangleRec({p.x - 1.5f, p.y - 1.5f, 3, 3}, c);
    }
    // alerts
    for (size_t i = 0; i < h.pings.size();) {
        float age = h.time - h.pingTimes[i];
        if (age > 3) { h.pings.erase(h.pings.begin() + i); h.pingTimes.erase(h.pingTimes.begin() + i); continue; }
        Vector2 p = toMM(h.pings[i]);
        DrawCircleLines((int)p.x, (int)p.y, HS(6) + fmodf(age, 1.0f) * HS(14), Color{255, 60, 60, (unsigned char)(255 * (1 - age / 3))});
        i++;
    }
    // camera footprint
    Vector2 c = {cam.target.x, cam.target.z};
    float hw = (cam.position.y) * 0.95f, hh = (cam.position.y) * 0.55f;
    Vector2 a = toMM({c.x - hw, c.y - hh}), b = toMM({c.x + hw, c.y + hh});
    DrawRectangleLinesEx({a.x, a.y, b.x - a.x, b.y - a.y}, 1.5f, WHITE);
    if (CheckCollisionPointRec(GetMousePosition(), r)) {
        in.overUI = true;
        Vector2 m = GetMousePosition();
        Vector2 wpos = {(m.x - r.x) / k, (m.y - r.y) / k};
        in.minimapWorld = wpos;
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) in.minimapLeft = true;
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) in.minimapRight = true;
    }
}

static void drawHealthBars(const World& w, int player, const Camera3D& cam) {
    int team = player >= 0 ? w.players[player].team : -1;
    for (auto& e : w.ents) {
        if (!e.alive || e.kind == K_RESOURCE) continue;
        bool hero = e.kind == K_HERO;
        if (g_settings.healthBars == 0 && !hero) continue;
        if (g_settings.healthBars == 1 && !hero && e.hp >= e.maxHp - 0.5f && !e.constructing) continue;
        if (fabsf(e.pos.x - cam.target.x) > 60 || fabsf(e.pos.y - cam.target.z) > 50) continue;
        if (!w.visibleTo(team, e)) continue;
        float hgt = e.kind == K_BUILDING ? 6.0f : (e.kind == K_TOWER ? 9.0f : (hero ? 3.2f : (e.type == U_TITAN ? 7.0f : 2.0f)));
        Vector2 sp = GetWorldToScreen(to3(e.pos, hgt), cam);
        if (sp.x < -50 || sp.y < -50 || sp.x > GetScreenWidth() + 50 || sp.y > GetScreenHeight() + 50) continue;
        float bw = hero ? HS(76) : (e.kind == K_BUILDING || e.kind == K_TOWER ? HS(70) : HS(34));
        float bh = hero ? HS(8) : HS(5);
        Color c = e.team == TEAM_NEUTRAL ? Color{230, 200, 90, 255} : (e.owner == player ? Color{90, 255, 120, 255} : (e.team == team ? Color{80, 170, 255, 255} : Color{255, 70, 60, 255}));
        float x = sp.x - bw / 2, y = sp.y;
        DrawRectangleRec({x - 1, y - 1, bw + 2, bh + 2}, Color{0, 0, 0, 200});
        bar(x, y, bw, bh, e.hp / e.maxHp, c, Color{40, 20, 20, 220});
        if (e.shield > 0 && w.time < e.shieldUntil) DrawRectangleRec({x, y, bw * clampf(e.shield / e.maxHp, 0, 1), bh * 0.5f}, Color{220, 230, 255, 230});
        // tick marks every 100 hp for heroes
        if (hero) {
            for (float t = 200; t < e.maxHp; t += 200) DrawLineEx({x + bw * t / e.maxHp, y}, {x + bw * t / e.maxHp, y + bh}, 1, Color{0, 0, 0, 150});
            bar(x, y + bh + 1, bw, HS(3), e.maxMana > 0 ? e.mana / e.maxMana : 0, Color{80, 150, 255, 255});
            text(TextFormat("%d", e.level), x - HS(16), y - HS(4), HS(14), WHITE, true);
            if (e.owner >= 0) text(w.players[e.owner].name, sp.x, y - HS(18), HS(13), withAlpha(playerColor(e.owner), 0.95f), true, AL_CENTER);
        }
        if (e.constructing) bar(x, y + bh + 2, bw, HS(3), e.buildProgress, theme::gold);
    }
}

static void tooltipBox(const std::string& title, const std::string& body, int cost, int flux = 0) {
    Vector2 m = GetMousePosition();
    float w = std::max(textWidth(title.c_str(), HS(18), true), textWidth(body.c_str(), HS(15))) + HS(24);
    float hgt = HS(64);
    Rectangle r = {m.x - w / 2, m.y - hgt - HS(18), w, hgt};
    if (r.x < 4) r.x = 4;
    if (r.x + w > GetScreenWidth() - 4) r.x = GetScreenWidth() - 4 - w;
    panel(r, Color{10, 14, 22, 245});
    frame(r, Color{255, 255, 255, 40});
    text(title.c_str(), r.x + HS(12), r.y + HS(8), HS(18), theme::text, true);
    if (flux > 0) text(TextFormat("%d", flux), r.x + r.width - HS(12), r.y + HS(8), HS(18), Color{110, 255, 150, 255}, true, AL_RIGHT);
    if (cost > 0) text(TextFormat("%d", cost), r.x + r.width - HS(12) - (flux > 0 ? textWidth(TextFormat("%d", flux), HS(18), true) + HS(14) : 0), r.y + HS(8), HS(18), theme::gold, true, AL_RIGHT);
    text(body.c_str(), r.x + HS(12), r.y + HS(36), HS(15), theme::dim);
}

void hudDraw(HudState& h, const World& w, int player, const std::vector<uint16_t>& sel, const Camera3D& cam,
             const std::vector<CardButton>& card, HudInput& in, float dt, int apm, bool aiming) {
    h.time += dt;
    h.frameTimes[h.ftIdx = (h.ftIdx + 1) % 120] = dt;
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    const PlayerState& P = w.players[std::max(0, player)];
    drawHealthBars(w, player, cam);

    // ---- top bar
    DrawRectangleGradientV(0, 0, (int)W, (int)HS(46), Color{6, 9, 14, 230}, Color{6, 9, 14, 160});
    int mins = (int)w.time / 60, secs = (int)w.time % 60;
    text(TextFormat("%02d:%02d", mins, secs), W / 2, HS(8), HS(28), theme::text, true, AL_CENTER);
    int tk[2] = {0, 0};
    for (int i = 0; i < MAX_PLAYERS; i++) if (w.players[i].active) { const Entity& he = w.ents[w.players[i].heroId]; tk[w.players[i].team] += he.kills; }
    int my = P.team;
    text(TextFormat("%d", tk[my]), W / 2 - HS(80), HS(8), HS(28), theme::def, true, AL_RIGHT);
    text(TextFormat("%d", tk[1 - my]), W / 2 + HS(80), HS(8), HS(28), theme::bad, true);
    float rx = W - HS(20);
    int used = w.supplyUsed(player), cap = w.supplyMax(player);
    text(TextFormat("%d / %d", used, cap), rx, HS(10), HS(24), used >= cap ? theme::bad : theme::text, true, AL_RIGHT);
    rx -= textWidth(TextFormat("%d / %d", used, cap), HS(24), true) + HS(10);
    DrawRectangleRec({rx - HS(16), HS(16), HS(14), HS(14)}, Color{220, 220, 230, 255});
    rx -= HS(34);
    const Color fluxC = {110, 255, 150, 255}, cryC = {90, 200, 255, 255};
    text(TextFormat("%d", P.flux), rx, HS(10), HS(24), fluxC, true, AL_RIGHT);
    rx -= textWidth(TextFormat("%d", P.flux), HS(24), true) + HS(10);
    DrawPoly({rx - HS(8), HS(23)}, 6, HS(8), 0, fluxC);
    rx -= HS(30);
    text(TextFormat("%d", P.gold), rx, HS(10), HS(24), cryC, true, AL_RIGHT);
    rx -= textWidth(TextFormat("%d", P.gold), HS(24), true) + HS(10);
    DrawPoly({rx - HS(8), HS(23)}, 4, HS(9), 45, cryC);
    if (w.time < w.titanBuffUntil[my]) text(TextFormat("BÉNÉDICTION DU TITAN %ds", (int)(w.titanBuffUntil[my] - w.time)), HS(20), HS(13), HS(17), Color{200, 140, 255, 255}, true);
    if (g_settings.showFps) {
        float avg = 0; for (float f : h.frameTimes) avg += f; avg /= 120;
        text(TextFormat("%d FPS%s%s", GetFPS(), g_settings.showApm ? TextFormat("  •  APM %d", apm) : "", h.ping > 0 ? TextFormat("  •  %d ms", h.ping) : ""), W / 2 + HS(150), HS(14), HS(15), theme::dim, true);
    }
    // upgrades indicator
    if (P.upgrades[0] || P.upgrades[1]) text(TextFormat("ARMES +%d  BLINDAGE +%d", P.upgrades[0], P.upgrades[1]), W / 2 - HS(150), HS(14), HS(15), theme::dim, true, AL_RIGHT);

    drawMinimap(h, w, player, cam, in);

    // ---- bottom panel
    float panelX = HS(300), panelW = W - HS(320), panelY = H - HS(220);
    Rectangle bp = {panelX, panelY, panelW, HS(206)};
    panel(bp, Color{10, 14, 20, 225});
    DrawRectangle((int)bp.x, (int)bp.y, (int)bp.width, 2, Color{255, 255, 255, 30});

    // hero block (always visible)
    const Entity& hero = w.ents[P.heroId];
    float hx = bp.x + HS(14), hy = bp.y + HS(14);
    {
        Rectangle port = {hx, hy, HS(96), HS(96)};
        panel(port, withAlpha(unitDef(hero.type).color, 0.85f));
        text(unitDef(hero.type).name, port.x + port.width / 2, port.y + HS(36), HS(16), BLACK, true, AL_CENTER);
        text(TextFormat("NIV %d", hero.level), port.x + port.width / 2, port.y + HS(58), HS(15), Color{0, 0, 0, 200}, true, AL_CENTER);
        if (!hero.alive) {
            DrawRectangleRec(port, Color{0, 0, 0, 170});
            text(TextFormat("%d", (int)std::max(0.f, hero.respawnAt - w.time) + 1), port.x + port.width / 2, port.y + HS(28), HS(36), theme::bad, true, AL_CENTER);
        }
        if (hovered(port)) { in.overUI = true; if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) in.selectOnly = hero.id; }
        float xpPrev = hero.level > 1 ? (float)XP_PER_LEVEL[hero.level - 1] : 0;
        float xpNext = hero.level < MAX_LEVEL ? (float)XP_PER_LEVEL[hero.level] : xpPrev + 1;
        bar(port.x, port.y + port.height + HS(4), port.width, HS(5), hero.level < MAX_LEVEL ? (hero.xp - xpPrev) / (xpNext - xpPrev) : 1, Color{200, 140, 255, 255});
        bar(port.x, port.y + port.height + HS(12), port.width, HS(10), hero.maxHp > 0 ? hero.hp / hero.maxHp : 0, Color{90, 230, 120, 255});
        bar(port.x, port.y + port.height + HS(24), port.width, HS(7), hero.maxMana > 0 ? hero.mana / hero.maxMana : 0, Color{80, 150, 255, 255});
        text(TextFormat("%d / %d", (int)hero.hp, (int)hero.maxHp), port.x + port.width / 2, port.y + port.height + HS(34), HS(13), theme::text, true, AL_CENTER);
        text(TextFormat("%d/%d/%d", hero.kills, hero.deaths, hero.assists), port.x + port.width / 2, port.y + port.height + HS(52), HS(14), theme::dim, true, AL_CENTER);
        // abilities
        const char* keys[4] = {bindName(g_settings.binds[ACT_ABILITY_Q]), "", "", ""};
        std::string k1 = bindName(g_settings.binds[ACT_ABILITY_Q]), k2 = bindName(g_settings.binds[ACT_ABILITY_W]), k3 = bindName(g_settings.binds[ACT_ABILITY_E]), k4 = bindName(g_settings.binds[ACT_ABILITY_R]);
        std::string ks[4] = {k1, k2, k3, k4};
        (void)keys;
        for (int s = 0; s < 4; s++) {
            const AbilityDef& a = abilityDef(hero.type, s);
            Rectangle r = {hx + HS(110) + s * HS(70), hy, HS(62), HS(62)};
            bool locked = hero.level < a.unlockLevel;
            bool ready = !locked && hero.abilCd[s] <= 0 && hero.mana >= a.mana && hero.alive;
            Color base = s == 3 ? Color{120, 70, 160, 255} : Color{40, 70, 110, 255};
            panel(r, locked ? Color{30, 32, 38, 255} : base);
            text(a.name, r.x + r.width / 2, r.y + HS(22), HS(11), locked ? theme::dim : WHITE, true, AL_CENTER);
            if (!locked && hero.abilCd[s] > 0) {
                float f = hero.abilCd[s] / std::max(0.1f, a.cooldown);
                DrawRectangleRec({r.x, r.y, r.width, r.height * clampf(f, 0, 1)}, Color{0, 0, 0, 170});
                text(TextFormat("%.0f", ceilf(hero.abilCd[s])), r.x + r.width / 2, r.y + HS(34), HS(20), WHITE, true, AL_CENTER);
            }
            if (!locked && hero.mana < a.mana) DrawRectangleRec(r, Color{20, 40, 120, 110});
            if (locked) text(TextFormat("NIV %d", a.unlockLevel), r.x + r.width / 2, r.y + HS(38), HS(12), theme::dim, true, AL_CENTER);
            DrawRectangleLinesEx(r, ready ? 2.f : 1.f, ready ? theme::accent : Color{255, 255, 255, 40});
            text(ks[s].c_str(), r.x + HS(5), r.y + HS(3), HS(13), theme::gold, true);
            text(TextFormat("%d", (int)a.mana), r.x + r.width - HS(5), r.y + r.height - HS(16), HS(11), Color{120, 170, 255, 255}, true, AL_RIGHT);
            if (hovered(r)) {
                in.overUI = true;
                tooltipBox(std::string(a.name) + "  (" + ks[s] + ")", a.desc, 0);
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) in.abilityClicked = s;
            }
        }
        // items
        for (int s = 0; s < 4; s++) {
            Rectangle r = {hx + HS(110) + s * HS(70), hy + HS(70), HS(62), HS(40)};
            int it = hero.items[s];
            panel(r, it ? Color{60, 50, 30, 255} : Color{22, 26, 34, 255});
            DrawRectangleLinesEx(r, 1, Color{255, 255, 255, 30});
            if (it) {
                text(itemDef(it).name, r.x + r.width / 2, r.y + HS(12), HS(11), theme::gold, true, AL_CENTER);
                if (hovered(r)) {
                    in.overUI = true;
                    tooltipBox(itemDef(it).name, std::string(itemDef(it).desc) + "  •  clic droit : revendre (60%)", 0);
                    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) in.sellSlot = s;
                }
            }
        }
        Rectangle shopBtn = {hx + HS(110), hy + HS(118), HS(272), HS(30)};
        bool nearShop = w.heroNearShop(player);
        panel(shopBtn, hovered(shopBtn) ? Color{70, 60, 30, 255} : Color{40, 34, 20, 255});
        text(TextFormat("BOUTIQUE (%s)%s", bindName(g_settings.binds[ACT_SHOP]), nearShop ? "" : " - en base"), shopBtn.x + shopBtn.width / 2, shopBtn.y + HS(7), HS(14), nearShop ? theme::gold : theme::dim, true, AL_CENTER);
        if (hovered(shopBtn)) { in.overUI = true; if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) h.shopOpen = !h.shopOpen; }
    }

    // selection block
    float sx = hx + HS(400), sw = panelW - HS(400) - HS(330);
    Rectangle sr = {sx, hy, sw, HS(178)};
    panel(sr, Color{16, 20, 30, 200});
    std::vector<const Entity*> selE;
    for (auto id : sel) { const Entity* e = w.get(id); if (e) selE.push_back(e); }
    if (selE.size() == 1) {
        const Entity& e = *selE[0];
        const UnitDef& d = unitDef(e.type);
        Color tc = e.team == TEAM_NEUTRAL ? Color{200, 190, 160, 255} : (e.owner >= 0 ? playerColor(e.owner) : teamColor(e.team));
        text(d.name, sr.x + HS(14), sr.y + HS(10), HS(24), tc, true);
        if (e.owner >= 0) text(w.players[e.owner].name, sr.x + sr.width - HS(14), sr.y + HS(14), HS(15), theme::dim, true, AL_RIGHT);
        text(d.desc, sr.x + HS(14), sr.y + HS(42), HS(14), theme::dim);
        if (e.kind == K_RESOURCE || e.type == B_EXTRACTOR) {
            const char* what = e.type == R_CRYSTAL ? "cristaux" : "flux";
            if (e.type != B_EXTRACTOR || !e.constructing) text(TextFormat("%d %s restants%s", e.amount, what, e.rich ? "  (gisement riche)" : ""), sr.x + HS(14), sr.y + HS(150), HS(16), e.type == R_CRYSTAL ? Color{90, 200, 255, 255} : Color{110, 255, 150, 255}, true);
        }
        if (e.kind != K_RESOURCE) {
            bar(sr.x + HS(14), sr.y + HS(66), sr.width - HS(28), HS(10), e.hp / e.maxHp, Color{90, 230, 120, 255});
            text(TextFormat("%d / %d", (int)e.hp, (int)e.maxHp), sr.x + sr.width / 2, sr.y + HS(80), HS(14), theme::text, true, AL_CENTER);
            float dmg = w.attackDamage(e);
            text(TextFormat("Dégâts %.0f   Armure %.0f   Portée %.1f   Vitesse %.1f", d.dmg > 0 ? dmg : 0.f, w.armorOf(e), d.range, w.moveSpeed(e)), sr.x + HS(14), sr.y + HS(102), HS(14), theme::text);
            const char* ac = d.ac == AC_LIGHT ? "Léger" : d.ac == AC_HEAVY ? "Lourd" : d.ac == AC_STRUCTURE ? "Structure" : "Héros";
            text(TextFormat("Classe : %s%s", ac, d.vsHeavy > 1.2f ? "   •   bonus vs lourd" : (d.vsLight > 1.2f ? "   •   bonus vs léger" : "")), sr.x + HS(14), sr.y + HS(122), HS(14), theme::dim);
        }
        if ((e.kind == K_BUILDING) && e.owner == player || (e.type == B_CORE && e.team == P.team)) {
            if (e.constructing) {
                text(TextFormat("Construction %d%%", (int)(e.buildProgress * 100)), sr.x + HS(14), sr.y + HS(146), HS(16), theme::gold, true);
            } else {
                for (size_t i = 0; i < e.prod.size(); i++) {
                    Rectangle q = {sr.x + HS(14) + i * HS(64), sr.y + HS(142), HS(58), HS(30)};
                    panel(q, Color{40, 60, 90, 255});
                    text(shortName(e.prod[i]), q.x + q.width / 2, q.y + HS(6), HS(16), WHITE, true, AL_CENTER);
                    if (i == 0) DrawRectangleRec({q.x, q.y + q.height - HS(4), q.width * e.prodProgress / unitDef(e.prod[0]).buildTime, HS(4)}, theme::accent);
                    if (hovered(q)) { in.overUI = true; if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) in.cancelQueueIndex = (int)i; }
                }
                if (e.research >= 0) {
                    float f = e.researchProgress / upgradeTime(w.players[e.owner].upgrades[e.research]);
                    text(e.research == UP_WEAPONS ? "Recherche : armes" : "Recherche : blindage", sr.x + HS(14), sr.y + HS(146), HS(15), theme::accent, true);
                    bar(sr.x + HS(200), sr.y + HS(152), HS(160), HS(6), f, theme::accent);
                }
            }
        }
        if (e.kind == K_HERO) {
            std::string items;
            for (int s = 0; s < 4; s++) if (e.items[s]) items += std::string(itemDef(e.items[s]).name) + "  ";
            text(TextFormat("Niveau %d   •   %d/%d/%d   •   %s", e.level, e.kills, e.deaths, e.assists, items.c_str()), sr.x + HS(14), sr.y + HS(146), HS(14), theme::gold, true);
        }
    } else if (selE.size() > 1) {
        int cols = std::max(1, (int)(sr.width / HS(52)));
        for (size_t i = 0; i < selE.size() && i < (size_t)cols * 3; i++) {
            const Entity& e = *selE[i];
            Rectangle r = {sr.x + HS(10) + (i % cols) * HS(52), sr.y + HS(10) + (i / cols) * HS(56), HS(48), HS(50)};
            panel(r, e.kind == K_HERO ? withAlpha(unitDef(e.type).color, 0.7f) : Color{40, 56, 80, 255});
            text(shortName(e.type), r.x + r.width / 2, r.y + HS(10), HS(16), WHITE, true, AL_CENTER);
            float f = e.hp / e.maxHp;
            bar(r.x + HS(4), r.y + r.height - HS(9), r.width - HS(8), HS(5), f, f > 0.5f ? Color{90, 230, 120, 255} : (f > 0.25f ? theme::gold : theme::bad));
            if (hovered(r)) { in.overUI = true; if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) in.selectOnly = e.id; }
        }
    } else {
        text("Aucune sélection", sr.x + sr.width / 2, sr.y + HS(70), HS(18), theme::dim, true, AL_CENTER);
        int idle = 0;
        for (auto& e : w.ents) if (e.alive && e.owner == player && e.type == U_DRONE && e.order.type == O_IDLE) idle++;
        if (idle) text(TextFormat("%d ouvrier(s) inactif(s) - %s", idle, bindName(g_settings.binds[ACT_IDLE_WORKER])), sr.x + sr.width / 2, sr.y + HS(100), HS(16), theme::gold, true, AL_CENTER);
    }

    // command card
    float cx = bp.x + bp.width - HS(316), cy = hy;
    for (size_t i = 0; i < card.size() && i < 10; i++) {
        const CardButton& b = card[i];
        Rectangle r = {cx + (i % 5) * HS(62), cy + (i / 5) * HS(74), HS(58), HS(66)};
        bool hv = hovered(r);
        panel(r, !b.enabled ? Color{26, 28, 34, 255} : (hv ? Color{50, 80, 120, 255} : Color{34, 48, 70, 255}));
        DrawRectangleLinesEx(r, 1, Color{255, 255, 255, 40});
        text(b.label.c_str(), r.x + r.width / 2, r.y + HS(24), HS(11), b.enabled ? WHITE : theme::dim, true, AL_CENTER);
        if (b.hotkeyAction >= 0) text(bindName(g_settings.binds[b.hotkeyAction]), r.x + HS(4), r.y + HS(2), HS(13), theme::gold, true);
        if (b.cost > 0) text(TextFormat("%d", b.cost), r.x + r.width - HS(4), r.y + r.height - HS(16), HS(12), P.gold >= b.cost ? Color{90, 200, 255, 255} : theme::bad, true, AL_RIGHT);
        if (b.flux > 0) text(TextFormat("%d", b.flux), r.x + HS(4), r.y + r.height - HS(16), HS(12), P.flux >= b.flux ? Color{110, 255, 150, 255} : theme::bad, true);
        if (b.progress >= 0) DrawRectangleRec({r.x, r.y + r.height - HS(3), r.width * b.progress, HS(3)}, theme::accent);
        if (hv) {
            in.overUI = true;
            tooltipBox(b.label, b.tip, b.cost, b.flux);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && b.enabled) in.cardClicked = (int)i;
        }
    }

    // ---- shop
    if (h.shopOpen) {
        Rectangle r = {W / 2 - HS(420), HS(90), HS(840), HS(470)};
        panel(r, theme::bg);
        DrawRectangle((int)r.x, (int)r.y, (int)r.width, (int)HS(4), theme::gold);
        bool near = w.heroNearShop(player);
        text("BOUTIQUE", r.x + HS(24), r.y + HS(18), HS(28), theme::text, true);
        text(near ? "Clic : acheter  •  Clic droit sur un objet équipé : revendre" : "Ramenez votre héros dans votre base pour acheter", r.x + HS(24), r.y + HS(56), HS(15), near ? theme::dim : theme::bad);
        text(TextFormat("%d or", P.gold), r.x + r.width - HS(24), r.y + HS(20), HS(26), theme::gold, true, AL_RIGHT);
        for (int it = 1; it < I_COUNT; it++) {
            const ItemDef& d = itemDef(it);
            int col = (it - 1) % 2, row = (it - 1) / 2;
            Rectangle b = {r.x + HS(24) + col * HS(400), r.y + HS(90) + row * HS(72), HS(390), HS(64)};
            bool hv = hovered(b);
            bool can = near && P.gold >= d.cost;
            panel(b, hv ? Color{50, 44, 26, 255} : Color{26, 28, 36, 255});
            text(d.name, b.x + HS(14), b.y + HS(10), HS(18), can ? theme::text : theme::dim, true);
            text(d.desc, b.x + HS(14), b.y + HS(36), HS(14), theme::dim);
            text(TextFormat("%d", d.cost), b.x + b.width - HS(14), b.y + HS(12), HS(18), P.gold >= d.cost ? theme::gold : theme::bad, true, AL_RIGHT);
            if (hv) { in.overUI = true; if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) in.buyItem = it; }
        }
        if (hovered(r)) in.overUI = true;
    }

    // ---- messages & kill feed
    float my2 = HS(70);
    for (auto it = h.msgs.begin(); it != h.msgs.end();) {
        float age = h.time - it->time;
        if (age > 4) { it = h.msgs.erase(it); continue; }
        float a = clampf((4 - age) / 0.6f, 0, 1);
        textShadow(it->text.c_str(), W / 2, my2, HS(22), withAlpha(it->col, a), true, AL_CENTER);
        my2 += HS(30);
        ++it;
    }
    float fy = HS(60);
    for (auto it = h.feed.begin(); it != h.feed.end();) {
        float age = h.time - it->time;
        if (age > 9) { it = h.feed.erase(it); continue; }
        float tw = textWidth(it->text.c_str(), HS(16), true) + HS(20);
        panel({W - tw - HS(16), fy, tw, HS(28)}, Color{10, 14, 20, (unsigned char)(200 * clampf((9 - age) / 1, 0, 1))});
        text(it->text.c_str(), W - HS(26), fy + HS(5), HS(16), withAlpha(it->col, clampf((9 - age) / 1, 0, 1)), true, AL_RIGHT);
        fy += HS(32);
        ++it;
    }
    if (aiming) textShadow("Clic gauche : lancer  •  Clic droit / Échap : annuler", W / 2, H - HS(250), HS(16), theme::gold, true, AL_CENTER);
}

void hudScoreboard(const World& w, int player) {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float pw = S(1100), ph = S(120) + S(60) * 6;
    Rectangle r = {W / 2 - pw / 2, H / 2 - ph / 2 - S(60), pw, ph};
    panel(r, theme::bg);
    DrawRectangle((int)r.x, (int)r.y, (int)r.width, (int)S(4), theme::accent);
    text("TABLEAU DES SCORES", r.x + S(24), r.y + S(18), S(24), theme::text, true);
    const char* hdr[] = {"JOUEUR", "HÉROS", "NIV", "K / D / A", "LAST HITS", "OR GAGNÉ", "ARMÉE", "APM"};
    float cols[] = {0, 260, 420, 490, 620, 740, 870, 970};
    float y = r.y + S(64);
    for (int i = 0; i < 8; i++) text(hdr[i], r.x + S(24) + S(cols[i]), y, S(14), theme::dim, true);
    y += S(26);
    for (int t = 0; t < 2; t++)
        for (int i = 0; i < MAX_PLAYERS; i++) {
            const PlayerState& P = w.players[i];
            if (!P.active || P.team != t) continue;
            const Entity& h = w.ents[P.heroId];
            Rectangle row = {r.x + S(14), y - S(4), pw - S(28), S(44)};
            panel(row, i == player ? Color{40, 60, 80, 220} : Color{20, 26, 38, 200});
            DrawRectangle((int)row.x, (int)row.y, (int)S(5), (int)row.height, playerColor(i));
            float x = r.x + S(24);
            text(TextFormat("%s%s", P.name, P.isAI ? "  [IA]" : ""), x + S(cols[0]), y + S(8), S(18), t == w.players[player].team ? theme::def : theme::bad, true);
            text(unitDef(P.hero).name, x + S(cols[1]), y + S(8), S(17), unitDef(P.hero).color, true);
            text(TextFormat("%d", h.level), x + S(cols[2]), y + S(8), S(17), theme::text, true);
            text(TextFormat("%d / %d / %d", h.kills, h.deaths, h.assists), x + S(cols[3]), y + S(8), S(17), theme::text, true);
            text(TextFormat("%d", h.lastHits), x + S(cols[4]), y + S(8), S(17), theme::text);
            text(TextFormat("%d", P.goldEarned), x + S(cols[5]), y + S(8), S(17), theme::gold);
            int army = 0; for (auto& e : w.ents) if (e.alive && e.owner == i && e.kind == K_UNIT && e.type != U_DRONE) army += unitDef(e.type).supply;
            text(TextFormat("%d", army), x + S(cols[6]), y + S(8), S(17), theme::text);
            text(TextFormat("%d", (int)(P.actions / std::max(1.f, w.time / 60))), x + S(cols[7]), y + S(8), S(17), theme::dim);
            y += S(50);
        }
}

} // namespace ax
