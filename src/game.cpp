#include "game.h"
#include "config.h"
#include "audio.h"
#include "ui.h"

namespace ax {

using namespace ui;

static std::string g_cfgPath, g_statsPath;

static const char* rankName(int r) {
    if (r < 800) return "FER";
    if (r < 1000) return "BRONZE";
    if (r < 1200) return "ARGENT";
    if (r < 1400) return "OR";
    if (r < 1600) return "PLATINE";
    if (r < 1800) return "DIAMANT";
    if (r < 2000) return "MAÎTRE";
    return "LÉGENDE";
}
static Color rankColor(int r) {
    if (r < 800) return {130, 130, 140, 255};
    if (r < 1000) return {205, 127, 50, 255};
    if (r < 1200) return {192, 200, 210, 255};
    if (r < 1400) return {255, 200, 60, 255};
    if (r < 1600) return {80, 220, 200, 255};
    if (r < 1800) return {120, 170, 255, 255};
    if (r < 2000) return {200, 110, 255, 255};
    return {255, 80, 110, 255};
}
static const char* kDiff[5] = {"RECRUE", "RÉGULIER", "VÉTÉRAN", "EXPERT", "ÉLITE"};

// ---------------------------------------------------------------------------
void App::initWindow() {
    unsigned flags = FLAG_WINDOW_RESIZABLE;
    if (g_settings.msaa) flags |= FLAG_MSAA_4X_HINT;
    if (g_settings.vsync) flags |= FLAG_VSYNC_HINT;
    SetConfigFlags(flags);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(g_settings.resW, g_settings.resH, "AXIOM");
    SetExitKey(KEY_NULL);
    Image icon = GenImageColor(64, 64, Color{10, 14, 22, 255});
    ImageDrawTriangle(&icon, {32, 6}, {6, 58}, {58, 58}, theme::accent);
    ImageDrawTriangle(&icon, {32, 26}, {20, 50}, {44, 50}, Color{10, 14, 22, 255});
    ImageDrawRectangle(&icon, 14, 40, 36, 5, theme::accent2);
    SetWindowIcon(icon);
    UnloadImage(icon);
    applyVideo(true);
}

void App::applyVideo(bool initial) {
    if (!initial) {
        if (IsWindowFullscreen()) ToggleFullscreen();
        if (IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) ToggleBorderlessWindowed();
        if (g_settings.vsync) SetWindowState(FLAG_VSYNC_HINT); else ClearWindowState(FLAG_VSYNC_HINT);
    }
    if (g_settings.displayMode == 0) {
        SetWindowSize(g_settings.resW, g_settings.resH);
        int m = GetCurrentMonitor();
        SetWindowPosition((GetMonitorWidth(m) - g_settings.resW) / 2, (GetMonitorHeight(m) - g_settings.resH) / 2);
    } else if (g_settings.displayMode == 1) ToggleBorderlessWindowed();
    else { SetWindowSize(g_settings.resW, g_settings.resH); ToggleFullscreen(); }
    SetTargetFPS(g_settings.fpsMax);
}

static void fillAI(World& w, int teamSize, int humanTeamSlots, int skill, int localHero) {
    // distinct heroes as much as possible
    std::vector<int> pool = {0, 1, 2, 3, 4, 5};
    pool.erase(std::remove(pool.begin(), pool.end(), localHero), pool.end());
    Rng r((uint32_t)(nowSeconds() * 1000) + 3);
    for (size_t i = 0; i < pool.size(); i++) std::swap(pool[i], pool[r.next() % pool.size()]);
    const char* names[] = {"Atlas", "Nyx", "Orion", "Vesper", "Kael", "Lyra", "Zephyr", "Iris"};
    int pi = 0, ni = 0;
    for (int t = 0; t < 2; t++) {
        int have = t == 0 ? humanTeamSlots : 0;
        for (int k = have; k < teamSize; k++) {
            int hero = pool[pi++ % pool.size()];
            w.addPlayer(names[ni++ % 8], t, (UnitType)(HERO_FIRST + hero), true, skill);
        }
    }
}

static int pickMap() {
    if (g_settings.mapChoice >= 0 && g_settings.mapChoice < MAP_COUNT) return g_settings.mapChoice;
    return GetRandomValue(0, MAP_COUNT - 1);
}

void App::startOffline() {
    role = ROLE_OFFLINE;
    world = World{};
    int ts = std::clamp(g_settings.teamSize, 1, 3);
    world.setup(ts, pickMap());
    player = world.addPlayer(g_settings.playerName, 0, (UnitType)(HERO_FIRST + std::clamp(g_settings.heroChoice, 0, 5)), false, 2);
    fillAI(world, ts, 1, g_settings.botDifficulty, g_settings.heroChoice);
    world.lobbyEnd = 0;
    world.step();
    ai.reset(world);
    g_renderer.onMapLoaded(world);
    hudInit(hud, world);
    hud.reset();
    selection.clear();
    for (auto& g : groups) g.clear();
    camTarget = world.map.base[world.players[player].team] + V2(0, 4);
    camZoom = g_settings.zoomDefault;
    camLock = g_settings.cameraLockDefault;
    mode = M_NONE;
    acc = 0; paused = false; endTimer = -1; statsCommitted = false;
    actionTimes.clear();
    showHelp = g_stats.matches < 3;
    screen = SCR_GAME;
    g_audio.stopAll();
    const Entity* h = world.get(world.players[player].heroId);
    if (h) selection = {h->id};
}

void App::startHost() {
    role = ROLE_HOST;
    world = World{};
    int ts = std::clamp(g_settings.teamSize, 1, 3);
    world.setup(ts, pickMap());
    player = world.addPlayer(g_settings.playerName, 0, (UnitType)(HERO_FIRST + std::clamp(g_settings.heroChoice, 0, 5)), false, 2);
    fillAI(world, ts, 1, g_settings.botDifficulty, g_settings.heroChoice);
    world.lobbyEnd = 1e9f;
    if (!server.start(g_settings.netPort)) {
        connectError = TextFormat("Impossible d'ouvrir le port UDP %d", g_settings.netPort);
        role = ROLE_OFFLINE;
        screen = SCR_CONNECT;
        return;
    }
    hud.reset();
    screen = SCR_LOBBY;
}

void App::startJoin() {
    role = ROLE_CLIENT;
    world = World{};
    std::string a = joinAddr;
    int port = g_settings.netPort;
    size_t colon = a.find(':');
    if (colon != std::string::npos) { port = atoi(a.c_str() + colon + 1); a = a.substr(0, colon); }
    snprintf(g_settings.lastServer, sizeof g_settings.lastServer, "%s", joinAddr);
    connectError.clear();
    if (!client.connect(a.c_str(), port, g_settings.playerName, g_settings.heroChoice)) { connectError = client.reason; role = ROLE_OFFLINE; }
    screen = SCR_CONNECT;
}

void App::leaveMatch() {
    if (role == ROLE_HOST) server.stop(world);
    if (role == ROLE_CLIENT) client.disconnect();
    role = ROLE_OFFLINE;
    g_audio.stopAll();
    g_renderer.onMapLoaded(menuWorld);
    hudInit(hud, menuWorld);
    screen = SCR_MAIN;
    paused = inGameSettings = false;
    selection.clear();
    g_settings.save(g_cfgPath.c_str());
}

// ---------------------------------------------------------------------------
void App::run() {
    g_cfgPath = userFile("config.ini");
    g_statsPath = userFile("career.ini");
    g_settings.load(g_cfgPath.c_str());
    g_stats.load(g_statsPath.c_str());
    g_settings.teamSize = std::clamp(g_settings.teamSize, 1, 3);
    g_settings.heroChoice = std::clamp(g_settings.heroChoice, 0, 5);
    g_settings.botDifficulty = std::clamp(g_settings.botDifficulty, 0, 4);
    initData();
    initWindow();
    g_audio.init();
    g_renderer.init();
    ui::init();
    // a live 3v3 between AIs plays behind the menus
    menuWorld.setup(3, GetRandomValue(0, MAP_COUNT - 1));
    for (int i = 0; i < 6; i++) menuWorld.addPlayer(i % 2 ? "Rouge" : "Bleu", i % 2, (UnitType)(HERO_FIRST + i), true, 3);
    menuWorld.lobbyEnd = 0;
    menuWorld.step();
    menuAI.reset(menuWorld);
    for (int k = 0; k < TICK_RATE * 60 * 6; k++) { menuAI.think(menuWorld); menuWorld.step(); menuWorld.events.clear(); }   // jump to an interesting moment
    g_renderer.onMapLoaded(menuWorld);
    hudInit(hud, menuWorld);
    snprintf(joinAddr, sizeof joinAddr, "%s", g_settings.lastServer);
    const char* test = getenv("AXIOM_TEST");
    int frameNo = 0;
    while (!quit) {
        if (WindowShouldClose()) quit = true;
        float dt = std::min(GetFrameTime(), 0.1f);
        if (test) { autotest(test, frameNo++); dt = 1.0f / 30.0f; }
        frame(dt);
    }
    if (role != ROLE_OFFLINE) leaveMatch();
    g_settings.save(g_cfgPath.c_str());
    g_stats.save(g_statsPath.c_str());
    ui::shutdown();
    g_renderer.shutdown();
    g_audio.shutdown();
    CloseWindow();
}

void App::frame(float dt) {
    g_audio.update();
    ui::beginFrame();
    if (IsKeyPressed(KEY_GRAVE)) { consoleOpen = !consoleOpen; conInput[0] = 0; }
    if (screen == SCR_GAME) {
        updateGame(dt);
        BeginDrawing();
        drawGame(dt);
        if (consoleOpen) drawConsole();
        EndDrawing();
        return;
    }
    if (screen == SCR_LOBBY && role == ROLE_HOST) {
        server.poll(world);
        world.step();
        server.recordEvents(world.events);
        world.events.clear();
        if (world.tick % 4 == 0) server.send(world);
        if (world.phase == PH_PLAYING) {
            ai.reset(world);
            g_renderer.onMapLoaded(world);
            hudInit(hud, world);
            camTarget = world.map.base[world.players[player].team];
            selection.clear();
            const Entity* h = world.get(world.players[player].heroId);
            if (h) selection = {h->id};
            acc = 0; endTimer = -1; statsCommitted = false;
            screen = SCR_GAME;
        }
    }
    if ((screen == SCR_CONNECT || screen == SCR_LOBBY) && role == ROLE_CLIENT) {
        std::vector<GameEvent> evs;
        if (client.update(world, evs)) {
            player = client.localPlayer;
            g_renderer.onMapLoaded(world);
            hudInit(hud, world);
            screen = SCR_LOBBY;
        }
        if (client.state == NetClient::FAILED || client.state == NetClient::DISCONNECTED) {
            connectError = client.reason;
            client.disconnect();
            role = ROLE_OFFLINE;
            screen = SCR_CONNECT;
        } else if (screen == SCR_LOBBY && world.phase == PH_PLAYING) {
            camTarget = world.map.base[world.players[player].team];
            selection.clear();
            endTimer = -1; statsCommitted = false;
            screen = SCR_GAME;
        }
    }
    tickMenuWorld(dt);
    BeginDrawing();
    drawMenuBackground(dt);
    switch (screen) {
    case SCR_MAIN: drawMainMenu(); break;
    case SCR_PLAY: drawPlayMenu(); break;
    case SCR_MULTI: drawMultiMenu(); break;
    case SCR_SETTINGS: {
        float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
        text("PARAMÈTRES", S(110), S(50), S(48), theme::text, true);
        if (button({W - S(260), S(56), S(160), S(50)}, "RETOUR")) { screen = SCR_MAIN; g_settings.save(g_cfgPath.c_str()); }
        drawSettings({S(110), S(130), W - S(220), H - S(170)});
        break;
    }
    case SCR_STATS: drawStats(); break;
    case SCR_END: drawEnd(); break;
    case SCR_CONNECT: drawConnect(); break;
    case SCR_LOBBY: drawLobby(); break;
    default: break;
    }
    if (consoleOpen) drawConsole();
    EndDrawing();
}

// ---------------------------------------------------------------------------
// in-game
// ---------------------------------------------------------------------------
Vector2 App::mouseGround() const {
    Ray r = GetMouseRay(GetMousePosition(), cam);
    if (fabsf(r.direction.y) < 1e-5f) return camTarget;
    float y = 0;
    Vector2 p{};
    for (int k = 0; k < 4; k++) {
        float t = (y - r.position.y) / r.direction.y;
        p = {r.position.x + r.direction.x * t, r.position.z + r.direction.z * t};
        y = world.map.groundHeight(p);
    }
    return p;
}

uint16_t App::pickEntity(Vector2 sp) const {
    int team = world.players[player].team;
    uint16_t best = 0;
    float bd = 1e9f;
    for (auto& e : world.ents) {
        if (!e.alive || !world.visibleTo(team, e)) continue;
        if (fabsf(e.pos.x - cam.target.x) > 60 || fabsf(e.pos.y - cam.target.z) > 50) continue;
        bool structure = e.kind == K_BUILDING || e.kind == K_TOWER || e.kind == K_RESOURCE;
        float h = structure ? 1.5f : (e.kind == K_HERO ? 1.4f : 0.8f);
        Vector2 p = GetWorldToScreen(to3(e.pos, h), cam);
        Vector2 edge = GetWorldToScreen(to3(e.pos + V2(e.radius + 0.4f, 0), h), cam);
        float rad = std::max(16.f, fabsf(edge.x - p.x) * 1.2f);
        float d = dist2(p, sp);
        if (d < rad && d < bd) { bd = d; best = e.id; }
    }
    return best;
}

void App::issue(Command c) {
    c.player = (uint8_t)player;
    if (role == ROLE_CLIENT) client.sendCommand(c);
    else world.issue(c);
    actionTimes.push_back(GetTime());
}

void App::selectOwnedOnly() {
    selection.erase(std::remove_if(selection.begin(), selection.end(), [&](uint16_t id) { return !world.get(id); }), selection.end());
}

void App::castAbility(int slot, bool atCursor) {
    const Entity* h = world.get(world.players[player].heroId);
    if (!h) return;
    const AbilityDef& a = abilityDef(h->type, slot);
    if (!world.abilityReady(*h, slot)) { g_audio.play2D(SFX_UI_DENY, 0.5f, 1, BUS_UI); hudMessage(hud, h->level < a.unlockLevel ? TextFormat("Disponible au niveau %d", a.unlockLevel) : "Compétence indisponible", theme::bad); return; }
    if (a.tm == TM_NONE) { Command c; c.type = CMD_ABILITY; c.a = (uint8_t)slot; c.pt = h->pos; issue(c); return; }
    if (atCursor) {
        Command c; c.type = CMD_ABILITY; c.a = (uint8_t)slot; c.pt = mouseGround();
        c.target = pickEntity(GetMousePosition());
        issue(c);
        mode = M_NONE;
        return;
    }
    mode = M_ABILITY; modeArg = slot;
}

std::vector<CardButton> App::buildCard(std::vector<std::function<void()>>& actions) {
    std::vector<CardButton> card;
    actions.clear();
    PlayerState& P = world.players[player];
    std::vector<Entity*> own;
    for (auto id : selection) { Entity* e = world.get(id); if (e && (e->owner == player || (e->type == B_CORE && e->team == P.team))) own.push_back(e); }
    if (own.empty()) return card;
    bool drones = false, army = false;
    Entity* bld = nullptr;
    for (auto* e : own) {
        if (e->type == U_DRONE) drones = true;
        else if (e->kind == K_UNIT || e->kind == K_HERO) army = true;
        else if (e->kind == K_BUILDING && !bld) bld = e;
    }
    const int keys[7] = {ACT_CMD1, ACT_CMD2, ACT_CMD3, ACT_CMD4, ACT_CMD5, ACT_CMD6, ACT_CMD7};
    if (drones) {
        auto& bl = buildList();
        for (size_t i = 0; i < bl.size(); i++) {
            const UnitDef& d = unitDef(bl[i]);
            bool tech = d.reqTech == U_NONE || world.hasBuilding(player, d.reqTech);
            std::string tip = d.desc;
            if (!tech) tip += std::string("  (requiert : ") + unitDef(d.reqTech).name + ")";
            CardButton cb{d.name, tip, i < 7 ? keys[i] : -1, d.cost, d.flux, tech};
            card.push_back(cb);
            UnitType t = bl[i];
            actions.push_back([this, t]() { mode = M_PLACE; modeArg = t; });
        }
    } else if (bld && !bld->constructing) {
        auto& tl = trainList(bld->type);
        for (size_t i = 0; i < tl.size(); i++) {
            const UnitDef& d = unitDef(tl[i]);
            bool tech = d.reqTech == U_NONE || world.hasBuilding(player, d.reqTech);
            std::string tip = std::string(d.desc) + TextFormat("  •  %d rav.  •  %.0f s", d.supply, d.buildTime);
            if (!tech) tip += std::string("  (requiert : ") + unitDef(d.reqTech).name + ")";
            card.push_back({d.name, tip, keys[i], d.cost, d.flux, tech});
            UnitType t = tl[i];
            actions.push_back([this, t]() {
                // train in the least busy selected building of that type
                Entity* best = nullptr;
                for (auto id : selection) { Entity* e = world.get(id); if (e && e->kind == K_BUILDING && !e->constructing && std::find(trainList(e->type).begin(), trainList(e->type).end(), t) != trainList(e->type).end()) if (!best || e->prod.size() < best->prod.size()) best = e; }
                if (!best) return;
                Command c; c.type = CMD_TRAIN; c.target = best->id; c.a = t; issue(c);
            });
        }
        if (bld->type == B_FORGE && bld->owner == player) {
            for (int u = 0; u < UP_COUNT; u++) {
                int lvl = P.upgrades[u];
                bool can = lvl < UPGRADE_MAX && bld->research < 0;
                std::string lab = u == UP_WEAPONS ? TextFormat("ARMES %d", lvl + 1) : TextFormat("BLINDAGE %d", lvl + 1);
                CardButton b{lab, u == UP_WEAPONS ? "+12% dégâts pour toutes vos unités" : "+6 armure pour toutes vos unités", keys[u], lvl < UPGRADE_MAX ? upgradeCost(u, lvl) : 0, lvl < UPGRADE_MAX ? upgradeFlux(u, lvl) : 0, can};
                if (bld->research == u) b.progress = bld->researchProgress / upgradeTime(lvl);
                card.push_back(b);
                uint16_t id = bld->id;
                actions.push_back([this, id, u]() { Command c; c.type = CMD_RESEARCH; c.target = id; c.a = (uint8_t)u; issue(c); });
            }
        }
    }
    if (army) {
        while (card.size() < 5 || (card.size() > 5 && card.size() < 7)) { card.push_back({"", "", -1, 0, 0, false}); actions.push_back([]() {}); }
        card.push_back({"ATTAQUE", "Attaque-déplacement : engage tout ennemi sur le chemin", ACT_ATTACK_MOVE, 0, 0, true});
        actions.push_back([this]() { mode = M_ATTACK_MOVE; });
        card.push_back({"STOP", "Annule les ordres", ACT_STOP, 0, 0, true});
        actions.push_back([this]() { Command c; c.type = CMD_STOP; c.units = selection; issue(c); });
        card.push_back({"TENIR", "Tient la position sans poursuivre", ACT_HOLD, 0, 0, true});
        actions.push_back([this]() { Command c; c.type = CMD_HOLD; c.units = selection; issue(c); });
    }
    return card;
}

void App::updateCamera(float dt) {
    float speed = 46.f * camZoom * g_settings.panSpeed * dt;
    Vector2 m = GetMousePosition();
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    bool overlay = paused || inGameSettings || consoleOpen;
    if (!overlay) {
        if (actionDown(ACT_CAM_UP)) camTarget.y -= speed;
        if (actionDown(ACT_CAM_DOWN)) camTarget.y += speed;
        if (actionDown(ACT_CAM_LEFT)) camTarget.x -= speed;
        if (actionDown(ACT_CAM_RIGHT)) camTarget.x += speed;
        if (g_settings.edgePan && IsWindowFocused() && !dragging) {
            if (m.x <= 3) camTarget.x -= speed;
            if (m.x >= W - 4) camTarget.x += speed;
            if (m.y <= 3) camTarget.y -= speed;
            if (m.y >= H - 4) camTarget.y += speed;
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
            Vector2 d = GetMouseDelta();
            camTarget.x -= d.x * 0.06f * camZoom;
            camTarget.y -= d.y * 0.08f * camZoom;
        }
        float wheel = GetMouseWheelMove();
        if (wheel != 0 && !mouseOverHud(hud)) camZoom = clampf(camZoom - wheel * 0.08f, 0.55f, 1.6f);
    }
    if (camLock) {
        const Entity* h = world.get(world.players[player].heroId);
        if (h) camTarget = lerp2(camTarget, h->pos, clampf(dt * 10, 0, 1));
    }
    camTarget.x = clampf(camTarget.x, 12, Map::SIZE - 12);
    camTarget.y = clampf(camTarget.y, 14, Map::SIZE - 6);
    cam.target = {camTarget.x, 0, camTarget.y};
    cam.position = {camTarget.x, 34.f * camZoom, camTarget.y + 22.f * camZoom};
    cam.up = {0, 1, 0};
    cam.fovy = 45;
    cam.projection = CAMERA_PERSPECTIVE;
}

void App::handleInput(float dt) {
    (void)dt;
    if (paused || inGameSettings || consoleOpen || world.phase != PH_PLAYING) { dragging = false; return; }
    PlayerState& P = world.players[player];
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    selectOwnedOnly();

    // control groups
    for (int k = 0; k < 10; k++) {
        int key = KEY_ZERO + ((k + 1) % 10);
        if (!IsKeyPressed(key)) continue;
        if (ctrl) { groups[k].clear(); for (auto id : selection) { const Entity* e = world.get(id); if (e && e->owner == player) groups[k].push_back(id); } hudMessage(hud, TextFormat("Groupe %d assigné", (k + 1) % 10), theme::accent); }
        else if (shift) { for (auto id : groups[k]) if (std::find(selection.begin(), selection.end(), id) == selection.end()) selection.push_back(id); }
        else {
            groups[k].erase(std::remove_if(groups[k].begin(), groups[k].end(), [&](uint16_t id) { return !world.get(id); }), groups[k].end());
            if (groups[k].empty()) continue;
            selection = groups[k];
            if (GetTime() - groupTap[k] < 0.35) { const Entity* e = world.get(groups[k][0]); if (e) camTarget = e->pos; }
            groupTap[k] = (float)GetTime();
        }
    }
    if (actionPressed(ACT_SELECT_HERO)) {
        const Entity* h = world.get(P.heroId);
        if (h) { if (selection.size() == 1 && selection[0] == h->id) camTarget = h->pos; selection = {h->id}; }
    }
    if (actionPressed(ACT_SELECT_ARMY)) {
        selection.clear();
        for (auto& e : world.ents) if (e.alive && e.owner == player && (e.kind == K_UNIT && e.type != U_DRONE)) selection.push_back(e.id);
    }
    if (actionPressed(ACT_IDLE_WORKER)) {
        std::vector<uint16_t> idle;
        for (auto& e : world.ents) if (e.alive && e.owner == player && e.type == U_DRONE && e.order.type == O_IDLE) idle.push_back(e.id);
        if (!idle.empty()) { uint16_t id = idle[idleCycle++ % idle.size()]; selection = {id}; camTarget = world.ents[id].pos; }
    }
    if (actionPressed(ACT_CENTER_HERO)) { const Entity* h = world.get(P.heroId); if (h) camTarget = h->pos; }
    if (actionPressed(ACT_CAMERA_LOCK)) { camLock = !camLock; hudMessage(hud, camLock ? "Caméra verrouillée sur le héros" : "Caméra libre", theme::accent); }
    if (actionPressed(ACT_SHOP)) hud.shopOpen = !hud.shopOpen;
    if (IsKeyPressed(KEY_F10)) showHelp = !showHelp;

    // abilities
    const int abilKeys[4] = {ACT_ABILITY_Q, ACT_ABILITY_W, ACT_ABILITY_E, ACT_ABILITY_R};
    for (int s = 0; s < 4; s++) if (actionPressed(abilKeys[s])) castAbility(s, g_settings.quickCast);

    // command card hotkeys
    std::vector<std::function<void()>> acts;
    auto card = buildCard(acts);
    for (size_t i = 0; i < card.size(); i++)
        if (card[i].hotkeyAction >= 0 && card[i].enabled && actionPressed(card[i].hotkeyAction) && mode != M_PLACE) { acts[i](); break; }
    if (actionPressed(ACT_CANCEL) && mode == M_NONE && !hud.shopOpen) {
        for (auto id : selection) { Entity* e = world.get(id); if (e && e->kind == K_BUILDING && (!e->prod.empty() || e->constructing || e->research >= 0)) { Command c; c.type = CMD_CANCEL; c.target = id; issue(c); break; } }
    }

    Vector2 m = GetMousePosition();
    Vector2 g = mouseGround();
    bool overHud = mouseOverHud(hud);
    // left mouse
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !overHud) {
        if (mode == M_ATTACK_MOVE) {
            uint16_t t = pickEntity(m);
            Command c; c.type = t && world.ents[t].team != P.team ? CMD_ATTACK : CMD_ATTACK_MOVE; c.pt = g; c.target = t; c.units = selection; c.queue = shift;
            issue(c); g_renderer.clickMarker(g, true); g_audio.play2D(SFX_ORDER, 0.4f, 1, BUS_UI);
            if (!shift) mode = M_NONE;
        } else if (mode == M_ABILITY) {
            Command c; c.type = CMD_ABILITY; c.a = (uint8_t)modeArg; c.pt = g; c.target = pickEntity(m);
            issue(c); mode = M_NONE;
        } else if (mode == M_PLACE) {
            Vector2 site = world.buildSite((UnitType)modeArg, g);
            std::vector<uint16_t> drones;
            for (auto id : selection) { const Entity* e = world.get(id); if (e && e->type == U_DRONE) drones.push_back(id); }
            Command c; c.type = CMD_BUILD; c.a = (uint8_t)modeArg; c.pt = site; c.units = drones; c.queue = shift;
            issue(c);
            g_audio.play2D(SFX_BUILD_START, 0.4f, 1, BUS_UI);
            if (!shift) mode = M_NONE;
        } else { dragging = true; dragStart = m; }
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && dragging) {
        dragging = false;
        bool box = fabsf(m.x - dragStart.x) > 6 || fabsf(m.y - dragStart.y) > 6;
        std::vector<uint16_t> picked;
        if (box) {
            Rectangle r = {std::min(m.x, dragStart.x), std::min(m.y, dragStart.y), fabsf(m.x - dragStart.x), fabsf(m.y - dragStart.y)};
            std::vector<uint16_t> blds;
            for (auto& e : world.ents) {
                if (!e.alive || e.owner != player) continue;
                Vector2 sp = GetWorldToScreen(to3(e.pos, 0.8f), cam);
                if (!CheckCollisionPointRec(sp, r)) continue;
                if (e.kind == K_BUILDING) blds.push_back(e.id); else picked.push_back(e.id);
            }
            if (picked.empty()) picked = blds;
        } else {
            uint16_t id = pickEntity(m);
            if (id) {
                const Entity& e = world.ents[id];
                bool dbl = GetTime() - lastClick < 0.35 && lastClickId == id;
                if ((ctrl || dbl) && e.owner == player) {
                    for (auto& o : world.ents) {
                        if (!o.alive || o.owner != player || o.type != e.type) continue;
                        Vector2 sp = GetWorldToScreen(to3(o.pos, 0.8f), cam);
                        if (sp.x >= 0 && sp.y >= 0 && sp.x <= GetScreenWidth() && sp.y <= GetScreenHeight()) picked.push_back(o.id);
                    }
                } else picked.push_back(id);
                lastClick = GetTime(); lastClickId = id;
            }
        }
        if (shift) {
            for (auto id : picked) {
                auto it = std::find(selection.begin(), selection.end(), id);
                if (it == selection.end()) selection.push_back(id); else if (picked.size() == 1) selection.erase(it);
            }
        } else selection = picked;
        if (!picked.empty()) g_audio.play2D(SFX_SELECT, 0.35f, 1, BUS_UI);
    }
    // right mouse
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !overHud) {
        if (mode != M_NONE) { mode = M_NONE; return; }
        std::vector<uint16_t> own;
        bool onlyBuildings = true;
        for (auto id : selection) { const Entity* e = world.get(id); if (e && e->owner == player) { own.push_back(id); if (e->kind != K_BUILDING) onlyBuildings = false; } }
        if (own.empty()) return;
        uint16_t t = pickEntity(m);
        Command c; c.type = onlyBuildings ? CMD_RALLY : CMD_SMART; c.pt = t ? world.ents[t].pos : g; c.target = t; c.units = own; c.queue = shift;
        if (onlyBuildings) c.pt = g;
        issue(c);
        bool attack = t && world.ents[t].team != P.team && world.ents[t].kind != K_RESOURCE;
        g_renderer.clickMarker(c.pt, attack);
        g_audio.play2D(attack ? SFX_ORDER : SFX_ORDER, 0.35f, attack ? 0.8f : 1.0f, BUS_UI);
    }
    if (IsKeyPressed(KEY_ESCAPE) && mode != M_NONE) mode = M_NONE;
}

void App::processEvents(const std::vector<GameEvent>& evs) {
    int team = world.players[player].team;
    for (auto& e : evs) {
        hudOnEvent(hud, world, e, player);
        g_renderer.handleEvent(world, e);
        bool seen = world.cellVisible(team, e.p);
        switch (e.type) {
        case EV_ATTACK: {
            if (!seen) break;
            const UnitDef& d = unitDef(e.c);
            Sfx s = d.kind == K_TOWER || e.c == B_CORE || e.c == B_WATCHTOWER ? SFX_ATTACK_TOWER : (e.c == U_SIEGE || e.c == U_MINION_SIEGE ? SFX_ATTACK_SIEGE : (d.kind == K_HERO ? SFX_ATTACK_HERO : (d.ranged ? SFX_ATTACK_RANGED : SFX_ATTACK_MELEE)));
            g_audio.playAt(s, e.p, d.kind == K_HERO ? 0.7f : 0.35f);
            break;
        }
        case EV_DEATH: {
            if (!seen) break;
            const UnitDef& d = unitDef(e.c);
            if (d.kind == K_BUILDING || d.kind == K_TOWER) g_audio.playAt(SFX_DEATH_BUILDING, e.p, 1.0f, 0);
            else if (d.kind != K_RESOURCE) g_audio.playAt(SFX_DEATH_UNIT, e.p, 0.45f);
            break;
        }
        case EV_ABILITY: {
            const Entity* h = world.get((uint16_t)e.a);
            if (!h || !seen) break;
            Sfx s = SFX_BUFF;
            switch (h->type) {
            case H_VANGUARD: s = e.c == 2 ? SFX_SLAM : (e.c == 0 ? SFX_BLINK : SFX_BUFF); break;
            case H_PYRA: s = e.c == 2 ? SFX_BLINK : SFX_FIREBALL; break;
            case H_HAWKE: s = e.c == 1 ? SFX_BUFF : (e.c == 2 ? SFX_BLINK : SFX_ATTACK_HERO); break;
            case H_SERAPH: s = (e.c == 0 || e.c == 3) ? SFX_HEAL : SFX_BUFF; break;
            case H_WARLORD: s = (e.c == 1 || e.c == 3) ? SFX_SUMMON : SFX_BUFF; break;
            case H_SHADE: s = e.c == 1 ? SFX_STEALTH : SFX_BLINK; break;
            default: break;
            }
            g_audio.playAt(s, e.p, 0.9f);
            break;
        }
        case EV_METEOR: if (seen) g_audio.playAt(SFX_METEOR, e.p, 1.0f, 0); break;
        case EV_UNIT_DONE: if (e.player == player) g_audio.play2D(SFX_UNIT_READY, 0.35f, 1, BUS_UI); break;
        case EV_BUILD_START: if (e.player == player) g_audio.playAt(SFX_BUILD_START, e.p, 0.6f); break;
        case EV_GAME_OVER:
            g_audio.play2D((int)e.c == team ? SFX_VICTORY : SFX_DEFEAT, 0.8f, 1, BUS_ANNOUNCER);
            endTimer = 3.5f;
            break;
        default: break;
        }
    }
}

void App::updateGame(float dt) {
    gameClock += dt;
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (consoleOpen) consoleOpen = false;
        else if (ui::capturingKey()) ui::cancelCapture();
        else if (mode != M_NONE) mode = M_NONE;
        else if (hud.shopOpen) hud.shopOpen = false;
        else if (inGameSettings) { inGameSettings = false; g_settings.save(g_cfgPath.c_str()); }
        else if (!(actionDown(ACT_CANCEL) && g_settings.binds[ACT_CANCEL] == KEY_ESCAPE && !selection.empty() && world.get(selection[0]) && world.get(selection[0])->kind == K_BUILDING)) paused = !paused;
    }
    updateCamera(dt);
    handleInput(dt);
    std::vector<GameEvent> evs;
    if (role == ROLE_CLIENT) {
        if (client.update(world, evs)) { g_renderer.onMapLoaded(world); hudInit(hud, world); }
        if (client.state == NetClient::FAILED || client.state == NetClient::DISCONNECTED) {
            connectError = client.reason.empty() ? "Déconnecté" : client.reason;
            leaveMatch(); screen = SCR_CONNECT; return;
        }
        hud.ping = client.rttMs;
        processEvents(evs);
    } else {
        bool simPaused = paused && role == ROLE_OFFLINE;
        if (!simPaused) acc += dt;
        int n = 0;
        while (acc >= TICK_DT && n < 6) {
            if (role == ROLE_HOST) server.poll(world);
            ai.think(world);
            world.step();
            if (role == ROLE_HOST) { server.recordEvents(world.events); if (world.tick % 2 == 0) server.send(world); }
            processEvents(world.events);
            world.events.clear();
            acc -= TICK_DT; n++;
        }
        if (n == 6) acc = 0;
    }
    g_renderer.update(dt);
    Vector2 lc = camTarget;
    g_audio.setListener(lc, 40 * camZoom);
    // apm window
    double now = GetTime();
    actionTimes.erase(std::remove_if(actionTimes.begin(), actionTimes.end(), [&](double t) { return now - t > 60; }), actionTimes.end());
    if (endTimer > 0) {
        endTimer -= dt;
        if (endTimer <= 0) { commitStats(); screen = SCR_END; if (role == ROLE_HOST) server.stop(world); if (role == ROLE_CLIENT) client.disconnect(); role = ROLE_OFFLINE; }
    } else if (world.phase == PH_OVER && endTimer < 0) endTimer = 3.5f;
}

void App::drawGame(float dt) {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    ClearBackground(Color{18, 24, 20, 255});
    RenderView v;
    v.cam = cam;
    v.team = world.players[player].team;
    v.player = player;
    v.alpha = role == ROLE_CLIENT ? client.alpha() : clampf(acc / TICK_DT, 0, 1);
    v.selection = &selection;
    v.hovered = mouseOverHud(hud) ? 0 : pickEntity(GetMousePosition());
    Vector2 g = mouseGround();
    if (mode == M_PLACE) {
        v.placeType = modeArg;
        v.placePos = world.buildSite((UnitType)modeArg, g);
        v.placeOk = world.canBuild((UnitType)modeArg, v.placePos, world.players[player].team) < 0;
    }
    const Entity* hero = world.get(world.players[player].heroId);
    if (mode == M_ABILITY && hero) {
        const AbilityDef& a = abilityDef(hero->type, modeArg);
        v.aiming = true; v.aimFrom = hero->pos; v.aimAt = g; v.aimRange = a.range; v.aimRadius = a.radius; v.aimMode = a.tm;
    }
    g_renderer.render(world, v);
    // drag box
    if (dragging) {
        Vector2 m = GetMousePosition();
        Rectangle r = {std::min(m.x, dragStart.x), std::min(m.y, dragStart.y), fabsf(m.x - dragStart.x), fabsf(m.y - dragStart.y)};
        DrawRectangleRec(r, Color{90, 255, 130, 30});
        DrawRectangleLinesEx(r, 1, Color{90, 255, 130, 200});
    }
    std::vector<std::function<void()>> acts;
    auto card = buildCard(acts);
    HudInput in;
    hudDraw(hud, world, player, selection, cam, card, in, dt, (int)actionTimes.size(), mode == M_ABILITY);
    if (!paused && !inGameSettings) {
        if (in.abilityClicked >= 0) castAbility(in.abilityClicked, false);
        if (in.cardClicked >= 0 && in.cardClicked < (int)acts.size()) acts[in.cardClicked]();
        if (in.buyItem) { Command c; c.type = CMD_BUY; c.a = (uint8_t)in.buyItem; issue(c); }
        if (in.sellSlot >= 0) { Command c; c.type = CMD_SELL; c.a = (uint8_t)in.sellSlot; issue(c); }
        if (in.selectOnly) selection = {in.selectOnly};
        if (in.cancelQueueIndex >= 0 && !selection.empty()) { Command c; c.type = CMD_CANCEL; c.target = selection[0]; issue(c); }
        if (in.minimapLeft && mode == M_NONE) camTarget = in.minimapWorld;
        if (in.minimapLeft && mode == M_ATTACK_MOVE && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { Command c; c.type = CMD_ATTACK_MOVE; c.pt = in.minimapWorld; c.units = selection; issue(c); mode = M_NONE; }
        if (in.minimapRight && !selection.empty()) { Command c; c.type = CMD_SMART; c.pt = in.minimapWorld; c.units = selection; issue(c); g_renderer.clickMarker(in.minimapWorld, false); }
    }
    if (actionDown(ACT_SCOREBOARD) && !paused) hudScoreboard(world, player);
    if (mode == M_ATTACK_MOVE) textShadow("ATTAQUE-DÉPLACEMENT : clic gauche sur une cible ou un point", W / 2, H - S(250), S(16), theme::bad, true, AL_CENTER);
    if (mode == M_PLACE) textShadow(TextFormat("Placer : %s  (Maj : en placer plusieurs, clic droit : annuler)", unitDef(modeArg).name), W / 2, H - S(250), S(16), theme::gold, true, AL_CENTER);
    if (showHelp) drawHelp();
    if (paused && !inGameSettings) drawPause();
    if (inGameSettings) { DrawRectangle(0, 0, (int)W, (int)H, Color{0, 0, 0, 170}); drawSettings({W / 2 - S(700), S(70), S(1400), H - S(140)}); }
    if (world.phase == PH_OVER) {
        bool won = world.winner == world.players[player].team;
        textShadow(won ? "VICTOIRE" : "DÉFAITE", W / 2, H * 0.3f, S(110), won ? theme::good : theme::bad, true, AL_CENTER);
    }
}

void App::drawHelp() {
    float W = (float)GetScreenWidth();
    Rectangle r = {W - S(470), S(60), S(450), S(470)};
    panel(r, Color{8, 12, 18, 225});
    DrawRectangle((int)r.x, (int)r.y, (int)r.width, (int)S(3), theme::accent);
    text("COMMANDES  (F10 pour masquer)", r.x + S(18), r.y + S(14), S(17), theme::accent, true);
    const char* lines[] = {
        "Clic gauche / glisser : sélectionner",
        "Clic droit : déplacer, attaquer, récolter",
        "A + clic : attaque-déplacement   S : stop   H : tenir",
        "Q W E R : compétences du héros (R au niv. 6)",
        "F1 : héros   F2 : armée   F3 : ouvrier inactif",
        "Ctrl + 1-9 : créer un groupe   1-9 : rappeler",
        "Z X C V B N M : construire / entraîner (selon sélection)",
        "Maj : enchaîner les ordres   Espace : centrer héros",
        "P : boutique (héros dans la base)   Tab : scores",
        "Bords d'écran / flèches / molette : caméra",
        "",
        "Objectif : détruisez le NOYAU ennemi.",
        "Drones : cristaux + flux (Extracteur). Centre = expansion.",
        "Le Titan au centre renforce toute l'équipe.",
    };
    for (int i = 0; i < 14; i++) text(lines[i], r.x + S(18), r.y + S(50) + i * S(29), S(15), i >= 11 ? theme::gold : theme::text, i >= 11);
}

void App::drawPause() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    DrawRectangle(0, 0, (int)W, (int)H, Color{0, 0, 0, 150});
    Rectangle r = {W / 2 - S(240), H / 2 - S(220), S(480), S(440)};
    panel(r, theme::bg);
    DrawRectangle((int)r.x, (int)r.y, (int)r.width, (int)S(4), theme::accent);
    text("PAUSE", r.x + r.width / 2, r.y + S(26), S(38), theme::text, true, AL_CENTER);
    if (role == ROLE_HOST) {
        auto ips = sock::localAddresses();
        std::string s = "IP : "; for (size_t i = 0; i < ips.size() && i < 2; i++) s += (i ? ", " : "") + ips[i];
        text(TextFormat("%s  •  port %d", s.c_str(), g_settings.netPort), r.x + r.width / 2, r.y + S(80), S(15), theme::accent, true, AL_CENTER);
    } else if (role == ROLE_OFFLINE) text("La partie est en pause", r.x + r.width / 2, r.y + S(80), S(16), theme::dim, false, AL_CENTER);
    float y = r.y + S(130);
    if (button({r.x + S(40), y, r.width - S(80), S(56)}, "REPRENDRE", true)) paused = false;
    y += S(70);
    if (button({r.x + S(40), y, r.width - S(80), S(56)}, "PARAMÈTRES")) { inGameSettings = true; settingsScroll = 0; }
    y += S(70);
    if (button({r.x + S(40), y, r.width - S(80), S(56)}, "AIDE (F10)")) { showHelp = !showHelp; paused = false; }
    y += S(70);
    if (button({r.x + S(40), y, r.width - S(80), S(56)}, "ABANDONNER")) leaveMatch();
}

void App::drawConsole() {
    float W = (float)GetScreenWidth(), h = S(380);
    DrawRectangle(0, 0, (int)W, (int)h, Color{6, 9, 14, 235});
    DrawRectangle(0, (int)h, (int)W, 2, theme::accent);
    float y = h - S(70);
    for (int i = (int)conLog.size() - 1; i >= 0 && y > S(10); i--) { text(conLog[i].c_str(), S(14), y, S(17), theme::text); y -= S(22); }
    Rectangle in = {S(10), h - S(44), W - S(20), S(36)};
    panel(in, Color{20, 26, 38, 255});
    text(conInput[0] ? conInput : "Commande (help)", in.x + S(10), in.y + S(8), S(18), conInput[0] ? theme::text : theme::dim);
    int c;
    while ((c = GetCharPressed()) > 0) { size_t l = strlen(conInput); if (c >= 32 && c < 127 && l < sizeof conInput - 1) { conInput[l] = (char)c; conInput[l + 1] = 0; } }
    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && strlen(conInput)) conInput[strlen(conInput) - 1] = 0;
    if (IsKeyPressed(KEY_ENTER) && conInput[0]) { conLog.push_back(std::string("> ") + conInput); execCommand(conInput); conInput[0] = 0; }
}

void App::execCommand(const std::string& line) {
    std::vector<std::string> t;
    size_t i = 0;
    while (i < line.size()) { while (i < line.size() && line[i] == ' ') i++; size_t j = i; while (j < line.size() && line[j] != ' ') j++; if (j > i) t.push_back(line.substr(i, j - i)); i = j; }
    if (t.empty()) return;
    if (t[0] == "help") conLog.push_back("quit, disconnect, connect <ip>, gold (solo), cvars, <variable> [valeur]");
    else if (t[0] == "quit") quit = true;
    else if (t[0] == "disconnect") { if (screen == SCR_GAME) leaveMatch(); }
    else if (t[0] == "connect" && t.size() > 1) { snprintf(joinAddr, sizeof joinAddr, "%s", t[1].c_str()); startJoin(); }
    else if (t[0] == "gold" && role == ROLE_OFFLINE && screen == SCR_GAME) { world.players[player].gold += 5000; conLog.push_back("+5000 or (triche, partie non classée)"); statsCommitted = true; }
    else if (t[0] == "cvars") { std::string s; for (auto& k : settingKeys()) s += k + " "; conLog.push_back(s); }
    else {
        std::string cur = settingGet(t[0]);
        if (cur.empty()) { conLog.push_back("Inconnu : " + t[0]); return; }
        if (t.size() > 1) { settingSet(t[0], t[1]); conLog.push_back(t[0] + " = " + settingGet(t[0])); if (t[0] == "fps_max") SetTargetFPS(g_settings.fpsMax); }
        else conLog.push_back(t[0] + " = " + cur);
    }
}

void App::commitStats() {
    if (statsCommitted) return;
    statsCommitted = true;
    ratingDelta = 0;
    const PlayerState& P = world.players[player];
    const Entity& h = world.ents[P.heroId];
    bool won = world.winner == P.team;
    g_stats.matches++;
    if (won) g_stats.wins++; else g_stats.losses++;
    g_stats.heroKills += h.kills; g_stats.heroDeaths += h.deaths; g_stats.assists += h.assists;
    g_stats.unitsKilled += P.unitsKilled; g_stats.unitsProduced += P.unitsProduced; g_stats.goldEarned += P.goldEarned;
    int apm = (int)(P.actions / std::max(1.f, world.time / 60));
    g_stats.bestApm = std::max(g_stats.bestApm, apm);
    int hi = std::clamp(P.hero - HERO_FIRST, 0, 5);
    g_stats.heroGames[hi]++;
    if (won) g_stats.heroWins[hi]++;
    if (role == ROLE_OFFLINE) {
        float botRating = 700.f + 220.f * g_settings.botDifficulty;
        float expected = 1.f / (1.f + powf(10.f, (botRating - g_stats.rating) / 400.f));
        ratingDelta = (int)roundf(48.f * ((won ? 1.f : 0.f) - expected));
        g_stats.rating = std::max(100, g_stats.rating + ratingDelta);
    }
    g_stats.save(g_statsPath.c_str());
}

// ---------------------------------------------------------------------------
// menus
// ---------------------------------------------------------------------------
void App::tickMenuWorld(float dt) {
    menuAcc += dt;
    int n = 0;
    while (menuAcc >= TICK_DT && n < 3) {
        menuAI.think(menuWorld);
        menuWorld.step();
        for (auto& e : menuWorld.events) g_renderer.handleEvent(menuWorld, e);
        menuWorld.events.clear();
        if (menuWorld.phase == PH_OVER) {
            menuWorld = World{};
            menuWorld.setup(3, GetRandomValue(0, MAP_COUNT - 1));
            for (int i = 0; i < 6; i++) menuWorld.addPlayer(i % 2 ? "Rouge" : "Bleu", i % 2, (UnitType)(HERO_FIRST + i), true, 3);
            menuWorld.lobbyEnd = 0; menuWorld.step(); menuAI.reset(menuWorld);
            g_renderer.onMapLoaded(menuWorld);
        }
        menuAcc -= TICK_DT; n++;
    }
    if (n == 3) menuAcc = 0;
    g_renderer.update(dt);
    menuCam += dt * 0.03f;
}

void App::drawMenuBackground(float dt) {
    (void)dt;
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    ClearBackground(Color{18, 24, 20, 255});
    RenderView v;
    // follow the action: the biggest crowd of units, eased
    static Vector2 focus = {100, 100};
    static float retarget = 0;
    retarget -= GetFrameTime();
    if (retarget <= 0) {
        retarget = 9;
        float best = -1;
        for (auto& e : menuWorld.ents) {
            if (!e.alive || (e.kind != K_UNIT && e.kind != K_HERO) || e.type == U_DRONE) continue;
            int n = 0;
            for (auto& o : menuWorld.ents) if (o.alive && (o.kind == K_UNIT || o.kind == K_HERO) && o.type != U_DRONE && dist2(o.pos, e.pos) < 14) n++;
            float sc = n + GetRandomValue(0, 30) / 10.f;
            if (sc > best) { best = sc; menuFocus = e.pos; }
        }
    }
    focus = lerp2(focus, menuFocus, clampf(GetFrameTime() * 0.6f, 0, 1));
    Vector2 c = focus + V2(cosf(menuCam) * 4, sinf(menuCam) * 3);
    v.cam.target = {c.x, 0, c.y};
    v.cam.position = {c.x, 30, c.y + 26};
    v.cam.up = {0, 1, 0};
    v.cam.fovy = 45;
    v.cam.projection = CAMERA_PERSPECTIVE;
    v.team = -1;
    v.player = 0;
    v.alpha = clampf(menuAcc / TICK_DT, 0, 1);
    g_audio.setListener(c, 1);
    g_renderer.render(menuWorld, v);
    DrawRectangleGradientH(0, 0, (int)(W * 0.62f), (int)H, Color{6, 9, 15, 245}, Color{6, 9, 15, 0});
    DrawRectangleGradientV(0, (int)(H * 0.7f), (int)W, (int)(H * 0.3f), Color{6, 9, 15, 0}, Color{6, 9, 15, 220});
}

static void logo(float x, float y, float size) {
    text("AXIOM", x + size * 0.04f, y + size * 0.04f, size, Color{0, 0, 0, 120}, true);
    text("AXIOM", x, y, size, theme::text, true);
    float w = textWidth("AXIOM", size, true);
    DrawRectangle((int)x, (int)(y + size * 1.02f), (int)(w * 0.35f), (int)(size * 0.06f), theme::accent);
    DrawRectangle((int)(x + w * 0.37f), (int)(y + size * 1.02f), (int)(w * 0.12f), (int)(size * 0.06f), theme::accent2);
}

void App::drawMainMenu() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float x = S(110);
    logo(x, S(140), S(150));
    text("HERO STRATEGY  •  COMMANDEZ UN HÉROS ET UNE ARMÉE", x + S(4), S(320), S(20), theme::dim, true);
    float y = S(400), bw = S(440), bh = S(62);
    if (button({x, y, bw, bh}, "JOUER", true)) screen = SCR_PLAY;
    y += bh + S(14);
    if (button({x, y, bw, bh}, "MULTIJOUEUR")) screen = SCR_MULTI;
    y += bh + S(14);
    if (button({x, y, bw, bh}, "PARAMÈTRES")) { screen = SCR_SETTINGS; settingsScroll = 0; }
    y += bh + S(14);
    if (button({x, y, bw, bh}, "CARRIÈRE")) screen = SCR_STATS;
    y += bh + S(14);
    if (button({x, y, bw, bh}, "QUITTER")) quit = true;
    Rectangle card = {W - S(520), S(110), S(420), S(230)};
    panel(card, theme::bg);
    DrawRectangle((int)card.x, (int)card.y, (int)S(4), (int)card.height, rankColor(g_stats.rating));
    text(g_settings.playerName, card.x + S(28), card.y + S(22), S(28), theme::text, true);
    text(rankName(g_stats.rating), card.x + S(28), card.y + S(62), S(40), rankColor(g_stats.rating), true);
    text(TextFormat("%d PTS", g_stats.rating), card.x + card.width - S(28), card.y + S(74), S(20), theme::dim, true, AL_RIGHT);
    text(TextFormat("MATCHS %d   •   VICTOIRES %.0f%%", g_stats.matches, g_stats.matches ? 100.f * g_stats.wins / g_stats.matches : 0.f), card.x + S(28), card.y + S(134), S(17), theme::text, true);
    text(TextFormat("ÉLIMINATIONS %d   •   MEILLEUR APM %d", g_stats.heroKills, g_stats.bestApm), card.x + S(28), card.y + S(164), S(17), theme::text, true);
    Rectangle news = {W - S(520), S(364), S(420), S(380)};
    panel(news, theme::bg);
    text("NOUVEAUTÉS  v" AXIOM_VERSION_STR, news.x + S(28), news.y + S(22), S(20), theme::accent, true);
    const char* lines[] = {"• Nouveau jeu : un héros ET une armée", "• 4 cartes, bases en hauteur, rampes, expansions", "• Deux ressources : cristaux et flux",
                           "• Hauteurs : vision bloquée depuis le bas", "• Phares de vision, camps neutres et Titan", "• 1v1, 2v2 et 3v3 contre l'IA ou en ligne",
                           "• 6 héros, 24 compétences, 9 objets", "• IA à 5 niveaux, classement et APM"};
    for (int i = 0; i < 8; i++) text(lines[i], news.x + S(28), news.y + S(64) + i * S(36), S(17), theme::text);
    text(TextFormat("AXIOM %s  •  [ ² ] console", AXIOM_VERSION_STR), S(24), H - S(36), S(15), theme::dim);
}

static void heroGrid(float x, float y, float w, int& choice) {
    float cw = (w - S(5 * 14)) / 6;
    for (int i = 0; i < 6; i++) {
        UnitType t = (UnitType)(HERO_FIRST + i);
        const UnitDef& d = unitDef(t);
        Rectangle r = {x + i * (cw + S(14)), y, cw, S(150)};
        bool act = choice == i, hv = hovered(r);
        panel(r, act ? withAlpha(d.color, 0.35f) : (hv ? Color{30, 38, 56, 240} : theme::bg));
        DrawRectangle((int)r.x, (int)(r.y + r.height - S(5)), (int)r.width, (int)S(5), act ? d.color : withAlpha(d.color, 0.3f));
        text(d.name, r.x + S(16), r.y + S(18), S(24), act ? WHITE : theme::text, true);
        text(heroRole(t), r.x + S(16), r.y + S(52), S(15), d.color, true);
        text(TextFormat("PV %.0f   Portée %.0f", d.hp, d.range), r.x + S(16), r.y + S(82), S(14), theme::dim);
        text(TextFormat("Victoires %d / %d", g_stats.heroWins[i], g_stats.heroGames[i]), r.x + S(16), r.y + S(106), S(13), theme::dim);
        if (hv && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { choice = i; g_audio.play2D(SFX_UI_CLICK, 0.6f, 1, BUS_UI); }
    }
    // abilities of the selected hero
    UnitType t = (UnitType)(HERO_FIRST + choice);
    float ay = y + S(170);
    text(unitDef(t).desc, x, ay, S(18), theme::text, true);
    for (int s = 0; s < 4; s++) {
        const AbilityDef& a = abilityDef(t, s);
        float bx = x + (s % 2) * (w / 2), by = ay + S(36) + (s / 2) * S(58);
        text(TextFormat("%c  %s", "QWER"[s], a.name), bx, by, S(17), s == 3 ? theme::gold : theme::accent, true);
        text(a.desc, bx, by + S(24), S(14), theme::dim);
    }
}

void App::drawPlayMenu() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    text("NOUVELLE PARTIE", S(110), S(60), S(48), theme::text, true);
    if (button({W - S(260), S(66), S(160), S(50)}, "RETOUR")) { screen = SCR_MAIN; g_settings.save(g_cfgPath.c_str()); }
    float x = S(110), y = S(150), w = W - S(220);
    text("FORMAT", x, y, S(16), theme::accent, true);
    const char* fm[3] = {"1 CONTRE 1", "2 CONTRE 2", "3 CONTRE 3"};
    for (int i = 0; i < 3; i++) {
        Rectangle r = {x + i * S(230), y + S(26), S(216), S(56)};
        bool act = g_settings.teamSize == i + 1;
        if (tab(r, fm[i], act)) g_settings.teamSize = i + 1;
        if (act) DrawRectangleLinesEx(r, 1, theme::accent);
    }
    text("DIFFICULTÉ DE L'IA", x + S(740), y, S(16), theme::accent, true);
    for (int i = 0; i < 5; i++) {
        Rectangle r = {x + S(740) + i * S(150), y + S(26), S(140), S(56)};
        bool act = g_settings.botDifficulty == i;
        if (tab(r, kDiff[i], act)) g_settings.botDifficulty = i;
        if (act) DrawRectangleLinesEx(r, 1, theme::accent);
    }
    text("CARTE", x, y + S(104), S(16), theme::accent, true);
    for (int i = -1; i < MAP_COUNT; i++) {
        Rectangle r = {x + (i + 1) * S(250), y + S(130), S(238), S(50)};
        bool act = g_settings.mapChoice == i;
        if (tab(r, i < 0 ? "ALÉATOIRE" : mapInfo(i).name, act)) g_settings.mapChoice = i;
        if (act) DrawRectangleLinesEx(r, 1, theme::accent);
        if (hovered(r) && i >= 0) text(mapInfo(i).desc, x, y + S(188), S(15), theme::dim);
    }
    text("CHOISISSEZ VOTRE HÉROS", x, y + S(214), S(16), theme::accent, true);
    heroGrid(x, y + S(244), w, g_settings.heroChoice);
    if (button({W - S(520), H - S(130), S(410), S(76)}, "LANCER LA PARTIE", true)) startOffline();
    text(TextFormat("Vos coéquipiers et adversaires sont contrôlés par l'IA (%s)", kDiff[g_settings.botDifficulty]), x, H - S(100), S(16), theme::dim);
}

void App::drawMultiMenu() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    text("MULTIJOUEUR", S(110), S(60), S(48), theme::text, true);
    if (button({W - S(260), S(66), S(160), S(50)}, "RETOUR")) { screen = SCR_MAIN; g_settings.save(g_cfgPath.c_str()); }
    float x = S(110), y = S(160);
    text("Pseudo", x, y, S(16), theme::dim, true);
    textField({x, y + S(26), S(420), S(46)}, g_settings.playerName, sizeof g_settings.playerName, 1, "Votre pseudo");
    text("Héros", x, y + S(96), S(16), theme::accent, true);
    heroGrid(x, y + S(124), W - S(220), g_settings.heroChoice);
    float by = H - S(250);
    Rectangle hostR = {x, by, S(620), S(200)}, joinR = {W - S(110) - S(620), by, S(620), S(200)};
    panel(hostR, theme::bg); panel(joinR, theme::bg);
    text("HÉBERGER", hostR.x + S(24), hostR.y + S(18), S(24), theme::accent, true);
    const char* fm[3] = {"1v1", "2v2", "3v3"};
    for (int i = 0; i < 3; i++) if (tab({hostR.x + S(24) + i * S(110), hostR.y + S(60), S(100), S(44)}, fm[i], g_settings.teamSize == i + 1)) g_settings.teamSize = i + 1;
    text(TextFormat("Port UDP %d  •  IA pour les places libres (%s)", g_settings.netPort, kDiff[g_settings.botDifficulty]), hostR.x + S(24), hostR.y + S(116), S(15), theme::dim);
    if (button({hostR.x + S(24), hostR.y + S(140), S(300), S(46)}, "CRÉER LE SALON", true)) startHost();
    text("REJOINDRE", joinR.x + S(24), joinR.y + S(18), S(24), theme::accent, true);
    textField({joinR.x + S(24), joinR.y + S(64), S(400), S(46)}, joinAddr, sizeof joinAddr, 2, "ip ou ip:port");
    if (button({joinR.x + S(24), joinR.y + S(140), S(300), S(46)}, "REJOINDRE", true)) startJoin();
    text("Les joueurs qui rejoignent remplacent une IA.", joinR.x + S(340), joinR.y + S(152), S(14), theme::dim);
}

void App::drawLobby() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    text("SALON", S(110), S(60), S(48), theme::text, true);
    if (role == ROLE_HOST) {
        auto ips = sock::localAddresses();
        std::string s = "Adresse à communiquer : ";
        for (size_t i = 0; i < ips.size() && i < 2; i++) s += (i ? "  ou  " : "") + ips[i];
        text(TextFormat("%s   (port %d)", s.c_str(), g_settings.netPort), S(110), S(124), S(18), theme::accent, true);
    } else text("Connecté. En attente du lancement par l'hôte…", S(110), S(124), S(18), theme::accent, true);
    for (int t = 0; t < 2; t++) {
        Rectangle col = {S(110) + t * (W - S(220)) / 2, S(170), (W - S(260)) / 2, S(320)};
        panel(col, theme::bg);
        text(t == 0 ? "ÉQUIPE BLEUE" : "ÉQUIPE ROUGE", col.x + S(20), col.y + S(16), S(20), t == 0 ? theme::def : theme::bad, true);
        float y = col.y + S(60);
        for (int i = 0; i < MAX_PLAYERS; i++) {
            const PlayerState& p = world.players[i];
            if (!p.active || p.team != t) continue;
            Rectangle r = {col.x + S(16), y, col.width - S(32), S(70)};
            panel(r, i == player ? Color{40, 60, 80, 230} : Color{22, 28, 40, 230});
            DrawRectangle((int)r.x, (int)r.y, (int)S(5), (int)r.height, playerColor(i));
            text(TextFormat("%s%s", p.name, p.isAI ? "  [IA]" : ""), r.x + S(20), r.y + S(12), S(20), theme::text, true);
            text(unitDef(p.hero).name, r.x + S(20), r.y + S(40), S(16), unitDef(p.hero).color, true);
            y += S(80);
        }
    }
    text(TextFormat("Carte : %s", mapInfo(world.mapId).name), W - S(110), S(124), S(18), theme::gold, true, AL_RIGHT);
    if (role == ROLE_HOST) {
        for (int i = 0; i < MAP_COUNT; i++)
            if (tab({W - S(110) - (MAP_COUNT - i) * S(200), S(70), S(192), S(40)}, mapInfo(i).name, world.mapId == i) && world.mapId != i) {
                world.mapId = i; world.map.generate(i); g_settings.mapChoice = i;
            }
    }
    text("Votre héros", S(110), S(510), S(16), theme::accent, true);
    int choice = std::clamp(world.players[std::max(0, player)].hero - HERO_FIRST, 0, 5);
    int before = choice;
    heroGrid(S(110), S(536), W - S(220), choice);
    if (choice != before) {
        g_settings.heroChoice = choice;
        Command c; c.type = CMD_PICK_HERO; c.a = (uint8_t)(HERO_FIRST + choice); c.player = (uint8_t)player;
        if (role == ROLE_CLIENT) client.sendCommand(c); else world.issue(c);
    }
    if (role == ROLE_HOST && button({W - S(520), H - S(110), S(410), S(70)}, "LANCER LA PARTIE", true)) world.lobbyEnd = world.time;
    if (role == ROLE_CLIENT && player >= 0) {
        bool rd = world.players[player].ready;
        if (button({W - S(520), H - S(110), S(410), S(70)}, rd ? "PRÊT  ✓" : "JE SUIS PRÊT", !rd) && !rd) { Command c; c.type = CMD_READY; client.sendCommand(c); }
        if (world.lobbyEnd - world.time < 600) text(TextFormat("Lancement automatique dans %.0f s", std::max(0.f, world.lobbyEnd - world.time)), W - S(520), H - S(140), S(16), theme::dim, true);
    }
    if (button({S(110), H - S(110), S(220), S(70)}, "QUITTER")) leaveMatch();
}

void App::drawConnect() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    Rectangle r = {W / 2 - S(320), H / 2 - S(150), S(640), S(300)};
    panel(r, theme::bg);
    DrawRectangle((int)r.x, (int)r.y, (int)r.width, (int)S(4), theme::accent);
    if (role == ROLE_CLIENT && connectError.empty()) {
        text("CONNEXION…", r.x + r.width / 2, r.y + S(50), S(36), theme::text, true, AL_CENTER);
        text(joinAddr, r.x + r.width / 2, r.y + S(110), S(20), theme::dim, false, AL_CENTER);
        if (button({r.x + r.width / 2 - S(120), r.y + r.height - S(80), S(240), S(54)}, "ANNULER")) { client.disconnect(); role = ROLE_OFFLINE; screen = SCR_MULTI; }
    } else {
        text("CONNEXION IMPOSSIBLE", r.x + r.width / 2, r.y + S(50), S(30), theme::bad, true, AL_CENTER);
        text(connectError.c_str(), r.x + r.width / 2, r.y + S(110), S(19), theme::text, false, AL_CENTER);
        if (button({r.x + r.width / 2 - S(120), r.y + r.height - S(80), S(240), S(54)}, "RETOUR")) { connectError.clear(); screen = SCR_MULTI; }
    }
}

void App::drawStats() {
    float W = (float)GetScreenWidth();
    text("CARRIÈRE", S(110), S(60), S(48), theme::text, true);
    if (button({W - S(260), S(66), S(160), S(50)}, "RETOUR")) screen = SCR_MAIN;
    const CareerStats& s = g_stats;
    struct T { const char* l; std::string v; Color c; };
    std::vector<T> tiles = {
        {"RANG", rankName(s.rating), rankColor(s.rating)}, {"POINTS", std::to_string(s.rating), theme::text},
        {"MATCHS", std::to_string(s.matches), theme::text}, {"VICTOIRES", TextFormat("%d (%.0f%%)", s.wins, s.matches ? 100.f * s.wins / s.matches : 0.f), theme::good},
        {"ÉLIMINATIONS", std::to_string(s.heroKills), theme::text}, {"MORTS", std::to_string(s.heroDeaths), theme::text},
        {"ASSISTANCES", std::to_string(s.assists), theme::text}, {"MEILLEUR APM", std::to_string(s.bestApm), theme::accent},
        {"UNITÉS PRODUITES", std::to_string(s.unitsProduced), theme::text}, {"UNITÉS DÉTRUITES", std::to_string(s.unitsKilled), theme::text},
        {"OR GAGNÉ", std::to_string((long long)s.goldEarned), theme::gold}, {"DÉFAITES", std::to_string(s.losses), theme::bad},
    };
    float tw = (W - S(220) - S(3 * 16)) / 4, th = S(110);
    for (size_t i = 0; i < tiles.size(); i++) {
        Rectangle r = {S(110) + (i % 4) * (tw + S(16)), S(150) + (i / 4) * (th + S(16)), tw, th};
        panel(r, theme::bg);
        text(tiles[i].l, r.x + S(22), r.y + S(18), S(15), theme::dim, true);
        text(tiles[i].v.c_str(), r.x + S(22), r.y + S(46), S(36), tiles[i].c, true);
    }
    float y = S(150) + 3 * (th + S(16)) + S(10);
    text("PAR HÉROS", S(110), y, S(16), theme::accent, true);
    for (int i = 0; i < 6; i++) {
        Rectangle r = {S(110) + i * ((W - S(220)) / 6), y + S(30), (W - S(220)) / 6 - S(12), S(90)};
        panel(r, theme::bg);
        text(unitDef(HERO_FIRST + i).name, r.x + S(16), r.y + S(14), S(18), unitDef(HERO_FIRST + i).color, true);
        text(TextFormat("%d V / %d parties", s.heroWins[i], s.heroGames[i]), r.x + S(16), r.y + S(48), S(15), theme::text);
    }
}

void App::drawEnd() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    DrawRectangle(0, 0, (int)W, (int)H, Color{6, 9, 15, 210});
    bool won = world.winner == world.players[player].team;
    textShadow(won ? "VICTOIRE" : "DÉFAITE", W / 2, S(40), S(96), won ? theme::good : theme::bad, true, AL_CENTER);
    text(TextFormat("Durée %02d:%02d", (int)world.time / 60, (int)world.time % 60), W / 2, S(150), S(22), theme::dim, true, AL_CENTER);
    if (ratingDelta) text(TextFormat("%s  %+d PTS  (%d)", rankName(g_stats.rating), ratingDelta, g_stats.rating), W / 2, S(184), S(22), ratingDelta > 0 ? theme::good : theme::bad, true, AL_CENTER);
    hudScoreboard(world, player);
    if (button({W / 2 - S(260), H - S(110), S(240), S(60)}, "MENU PRINCIPAL", true)) leaveMatch();
    if (button({W / 2 + S(20), H - S(110), S(240), S(60)}, "REJOUER")) startOffline();
}

void App::drawSettings(Rectangle area) {
    panel(area, theme::bg);
    const char* tabs[] = {"JEU", "VIDÉO", "AUDIO", "TOUCHES", "INTERFACE"};
    for (int i = 0; i < 5; i++) if (tab({area.x + S(20) + i * S(170), area.y + S(10), S(170), S(50)}, tabs[i], settingsTab == i)) { settingsTab = i; settingsScroll = 0; }
    DrawRectangle((int)area.x, (int)(area.y + S(60)), (int)area.width, 1, theme::line);
    if (inGameSettings && button({area.x + area.width - S(180), area.y + S(10), S(160), S(44)}, "FERMER")) { inGameSettings = false; g_settings.save(g_cfgPath.c_str()); }
    Rectangle content = {area.x + S(20), area.y + S(80), area.width * 0.62f, area.height - S(100)};
    static float contentH = 1000;
    beginScroll(content, &settingsScroll, contentH);
    float x = content.x, y = content.y - settingsScroll, w = content.width - S(16), rh = S(48), gap = S(8), y0 = y;
    auto R = [&]() { Rectangle r = {x, y, w, rh}; y += rh + gap; return r; };
    auto header = [&](const char* t) { y += S(10); text(t, x, y, S(16), theme::accent, true); y += S(30); };
    Settings& s = g_settings;
    switch (settingsTab) {
    case 0: {
        static const char* hb[] = {"AUCUNE", "UNITÉS BLESSÉES", "TOUJOURS"};
        header("CONTRÔLES");
        toggle(R(), "Lancement rapide (sort lancé au curseur)", &s.quickCast);
        toggle(R(), "Défilement par les bords de l'écran", &s.edgePan);
        slider(R(), "Vitesse de la caméra", &s.panSpeed, 0.4f, 2.5f, "%.2f", 0.05f);
        slider(R(), "Zoom par défaut", &s.zoomDefault, 0.55f, 1.6f, "%.2f", 0.05f);
        toggle(R(), "Caméra verrouillée sur le héros au départ", &s.cameraLockDefault);
        header("AFFICHAGE EN JEU");
        choice(R(), "Barres de vie", &s.healthBars, hb, 3);
        toggle(R(), "Afficher les portées", &s.showRanges);
        toggle(R(), "Afficher l'APM", &s.showApm);
        break;
    }
    case 1: {
        static const char* modes[] = {"FENÊTRÉ", "PLEIN ÉCRAN FENÊTRÉ", "PLEIN ÉCRAN"};
        static const int resList[][2] = {{1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}, {1920, 1200}};
        static const char* resNames[] = {"1280 x 720", "1366 x 768", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "1920 x 1200"};
        static const int fpsList[] = {0, 60, 120, 144, 165, 240, 360};
        static const char* fpsNames[] = {"ILLIMITÉ", "60", "120", "144", "165", "240", "360"};
        static const char* aa[] = {"DÉSACTIVÉ", "MSAA 4X"};
        static const char* cb[] = {"AUCUN", "ACTIVÉ"};
        header("AFFICHAGE");
        choice(R(), "Mode d'affichage", &s.displayMode, modes, 3);
        int ri = 3; for (int i = 0; i < 7; i++) if (resList[i][0] == s.resW && resList[i][1] == s.resH) ri = i;
        if (choice(R(), "Résolution", &ri, resNames, 7)) { s.resW = resList[ri][0]; s.resH = resList[ri][1]; }
        toggle(R(), "Synchronisation verticale", &s.vsync);
        int fi = 0; for (int i = 0; i < 7; i++) if (fpsList[i] == s.fpsMax) fi = i;
        if (choice(R(), "Limite d'images / s", &fi, fpsNames, 7)) { s.fpsMax = fpsList[fi]; SetTargetFPS(s.fpsMax); }
        choice(R(), "Anticrénelage (redémarrage)", &s.msaa, aa, 2);
        Rectangle ap = R();
        if (button({ap.x, ap.y, S(320), ap.height}, "APPLIQUER", true)) applyVideo(false);
        header("IMAGE");
        slider(R(), "Luminosité", &s.brightness, 0.6f, 1.6f, "%.2f", 0.01f);
        toggle(R(), "Ombres", &s.shadows);
        toggle(R(), "Particules", &s.particles);
        int cbi = s.colorblind ? 1 : 0;
        if (choice(R(), "Mode daltonien (rouge -> jaune)", &cbi, cb, 2)) s.colorblind = cbi;
        break;
    }
    case 2:
        header("VOLUME");
        if (slider(R(), "Volume général", &s.volMaster, 0, 1, "%.2f", 0.01f)) SetMasterVolume(s.volMaster);
        slider(R(), "Effets", &s.volEffects, 0, 1, "%.2f", 0.01f);
        slider(R(), "Interface", &s.volUI, 0, 1, "%.2f", 0.01f);
        slider(R(), "Annonces", &s.volAnnouncer, 0, 1, "%.2f", 0.01f);
        toggle(R(), "Couper le son hors focus", &s.muteWhenUnfocused);
        break;
    case 3:
        header("RACCOURCIS  (clic puis touche • Retour arrière = effacer)");
        for (int a = 0; a < ACT_COUNT; a++) {
            Rectangle r = R();
            panel(r, Color{18, 23, 34, 200});
            text(actionName(a), r.x + S(14), r.y + (r.height - S(19)) / 2, S(19), theme::text);
            keybind({r.x + r.width - S(380), r.y + S(6), S(180), r.height - S(12)}, &s.binds[a], a * 2);
            keybind({r.x + r.width - S(190), r.y + S(6), S(180), r.height - S(12)}, &s.binds2[a], a * 2 + 1);
        }
        { Rectangle rb = R(); if (button({rb.x, rb.y, S(320), rb.height}, "RÉINITIALISER")) s.resetBinds(); }
        break;
    case 4:
        header("INTERFACE");
        slider(R(), "Échelle de l'interface", &s.hudScale, 0.7f, 1.3f, "%.2f", 0.05f);
        slider(R(), "Taille de la minimap", &s.minimapSize, 0.7f, 1.5f, "%.2f", 0.05f);
        toggle(R(), "Afficher les FPS", &s.showFps);
        header("PROFIL");
        { Rectangle nr = R(); text("Pseudo", nr.x + S(14), nr.y + S(14), S(19), theme::text); textField({nr.x + nr.width * 0.5f, nr.y + S(4), nr.width * 0.5f - S(10), nr.height - S(8)}, s.playerName, sizeof s.playerName, 77, "Pseudo"); }
        sliderInt(R(), "Port réseau", &s.netPort, 1024, 65000);
        break;
    }
    contentH = y - y0 + S(20);
    endScroll();
}

// Automated smoke test (screenshots) - used during development
void App::autotest(const char* test, int f) {
    std::string t = test;
    auto shot = [&](const char* n) { TakeScreenshot(n); };
    if (t == "menu") {
        if (f == 60) shot("t_menu.png");
        if (f == 61) screen = SCR_PLAY;
        if (f == 70) shot("t_play.png");
        if (f == 71) screen = SCR_MULTI;
        if (f == 80) shot("t_multi.png");
        if (f == 81) { screen = SCR_SETTINGS; settingsTab = 3; }
        if (f == 90) shot("t_settings.png");
        if (f == 91) screen = SCR_STATS;
        if (f == 100) shot("t_stats.png");
        if (f == 101) quit = true;
    } else if (t == "maps") {
        int i = f / 120, k = f % 120;
        if (i >= MAP_COUNT) { quit = true; return; }
        if (k == 1) { g_settings.mapChoice = i; g_settings.teamSize = 1; startOffline(); showHelp = false; camZoom = 1.3f; }
        if (k == 40) shot(TextFormat("t_map%d_base.png", i));
        if (k == 41) { const Expansion* nat = nullptr; for (auto& x : world.map.expansions) if (x.mainOf < 0 && (!nat || dist2(x.hub, world.map.base[0]) < dist2(nat->hub, world.map.base[0]))) nat = &x; if (nat) camTarget = nat->hub; }
        if (k == 80) shot(TextFormat("t_map%d_nat.png", i));
    } else if (t == "game") {
        if (f == 2) { g_settings.teamSize = 3; g_settings.heroChoice = 1; startOffline(); world.players[player].isAI = true; ai.reset(world); showHelp = true; }
        if (f == 90) shot("t_game_start.png");
        if (f == 91) showHelp = false;
        if (f > 91 && screen == SCR_GAME) {
            const Entity* h = world.get(world.players[player].heroId);
            if (h && f % 30 == 0) camTarget = h->pos;
        }
        if (f == 2400) { selection.clear(); for (auto& e : world.ents) if (e.alive && e.owner == player && e.kind == K_BUILDING) { selection = {e.id}; break; } }
        if (f == 2410) shot("t_game_base.png");
        if (f == 5400) { const Entity* h = world.get(world.players[player].heroId); if (h) { camTarget = h->pos; selection = {h->id}; } }
        if (f == 5410) shot("t_game_mid.png");
        if (f == 5411) hud.shopOpen = true;
        if (f == 5420) shot("t_game_shop.png");
        if (f == 5421) hud.shopOpen = false;
        if (f == 9000) { camTarget = V2(100, 100); }
        if (f == 9010) shot("t_game_late.png");
        if (f == 9011) { camTarget = world.map.base[1]; }
        if (f == 9020) shot("t_game_enemybase.png");
        if (f == 9021) quit = true;
    }
}

} // namespace ax
