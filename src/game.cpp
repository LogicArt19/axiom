#include "game.h"
#include "config.h"
#include "audio.h"
#include "render.h"
#include "ui.h"

namespace ax {

using namespace ui;

static std::string g_cfgPath, g_statsPath;
#define kConfigPath g_cfgPath.c_str()
#define kStatsPath g_statsPath.c_str()

static const char* rankName(int rating) {
    if (rating < 800) return "FER";
    if (rating < 1000) return "BRONZE";
    if (rating < 1200) return "ARGENT";
    if (rating < 1400) return "OR";
    if (rating < 1600) return "PLATINE";
    if (rating < 1800) return "DIAMANT";
    if (rating < 2000) return "MAÎTRE";
    return "LÉGENDE";
}
static Color rankColor(int rating) {
    if (rating < 800) return {130, 130, 140, 255};
    if (rating < 1000) return {205, 127, 50, 255};
    if (rating < 1200) return {192, 200, 210, 255};
    if (rating < 1400) return {255, 200, 60, 255};
    if (rating < 1600) return {80, 220, 200, 255};
    if (rating < 1800) return {120, 170, 255, 255};
    if (rating < 2000) return {200, 110, 255, 255};
    return {255, 80, 110, 255};
}

static float hfovToVfov(float hfov) { return 2.f * atanf(tanf(hfov * 0.5f * DEG) * 0.75f) / DEG; }

// ---------------------------------------------------------------------------
void App::initWindow() {
    unsigned flags = FLAG_WINDOW_RESIZABLE;
    if (g_settings.msaa) flags |= FLAG_MSAA_4X_HINT;
    if (g_settings.vsync) flags |= FLAG_VSYNC_HINT;
    SetConfigFlags(flags);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(g_settings.resW, g_settings.resH, "AXIOM");
    SetExitKey(KEY_NULL);
    // procedural window icon
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
    } else if (g_settings.displayMode == 1) {
        ToggleBorderlessWindowed();
    } else {
        SetWindowSize(g_settings.resW, g_settings.resH);
        ToggleFullscreen();
    }
    SetTargetFPS(g_settings.fpsMax);
}

void App::setCursor(bool locked) {
    if (locked == cursorLocked) return;
    cursorLocked = locked;
    if (locked) DisableCursor(); else EnableCursor();
}

// ---------------------------------------------------------------------------
void App::setupMatchPlayers(GameMode m) {
    const char* names[] = {"Nova", "Kairo", "Vex", "Lumen", "Rook", "Sable", "Onyx", "Talon", "Echo", "Drift", "Zephyr", "Raze", "Quill", "Hex", "Pulse"};
    int skill = g_settings.botDifficulty;
    int ts = std::clamp(g_settings.teamSize, 1, 5);
    localId = world.addPlayer(g_settings.playerName, false, 0, skill);
    int ni = 0;
    if (m == MODE_COMPETITIVE) {
        for (int i = 0; i < ts - 1; i++) world.addPlayer(names[ni++], true, 0, skill);
        for (int i = 0; i < ts; i++) world.addPlayer(names[ni++], true, 1, skill);
    } else if (m == MODE_DEATHMATCH) {
        for (int i = 0; i < ts * 2 - 1; i++) world.addPlayer(names[ni++], true, i % 2, skill);
    } else if (m == MODE_PRACTICE) {
        for (int i = 0; i < 5; i++) world.addPlayer(fmt("Cible %d", i + 1), true, 1, skill);
    }
}

void App::startOffline(GameMode m) {
    role = ROLE_OFFLINE;
    world = World{};
    world.init(m);
    bots.reset();
    hud.reset();
    setupMatchPlayers(m);
    world.startMatch();
    if (m == MODE_PRACTICE) {
        std::vector<HoldSpot> spots;
        for (auto& h : world.map.holdA) spots.push_back(h);
        for (auto& h : world.map.holdB) spots.push_back(h);
        int k = 0;
        for (auto& p : world.players) if (p.active && p.isBot) { p.pos = p.prevPos = spots[k % spots.size()].pos; p.yaw = spots[k % spots.size()].yaw + 180; k++; }
    }
    if (m == MODE_AIM) {
        aim = AimTrainer{};
        aim.kind = aimKind;
        aim.start = gameTime + 3.0f;
        aim.origin = world.players[localId].pos;
        aim.fwd = flatForward(world.players[localId].yaw);
        aim.right = flatRight(world.players[localId].yaw);
        Player& p = world.players[localId];
        p.primary = W_NONE;
        world.giveWeapon(p, aimKind == 2 ? W_SPECTER : W_VANGUARD);
        p.activeW = p.primary;
    }
    g_renderer.onMapLoaded(world.map);
    hudBuildRadar(hud, world.map);
    g_audio.setMap(&world.map);
    viewYaw = world.players[localId].yaw; viewPitch = 0;
    acc = 0; paused = false; buyOpen = false; spectateId = -1; deathTime = -10;
    statsCommitted = false;
    screen = SCR_GAME;
    hud.time = 0;
    g_audio.stopAll();
}

void App::startHost(GameMode m) {
    startOffline(m);
    if (!server.start(g_settings.netPort)) {
        connectError = fmt("Impossible d'ouvrir le port %d (déjà utilisé ?)", g_settings.netPort);
        leaveMatch();
        screen = SCR_CONNECT;
        return;
    }
    role = ROLE_HOST;
    conLog.push_back(fmt("Serveur démarré sur le port %d", g_settings.netPort));
}

void App::startJoin() {
    role = ROLE_CLIENT;
    world = World{};
    hud.reset();
    std::string a = joinAddr;
    int port = g_settings.netPort;
    size_t colon = a.find(':');
    if (colon != std::string::npos) { port = atoi(a.c_str() + colon + 1); a = a.substr(0, colon); }
    snprintf(g_settings.lastServer, sizeof g_settings.lastServer, "%s", joinAddr);
    connectError.clear();
    if (!client.connect(a.c_str(), port, g_settings.playerName)) { connectError = client.reason; role = ROLE_OFFLINE; }
    screen = SCR_CONNECT;
}

void App::leaveMatch() {
    if (role == ROLE_HOST) server.stop(world);
    if (role == ROLE_CLIENT) client.disconnect();
    role = ROLE_OFFLINE;
    g_audio.stopAll();
    g_renderer.extra3D = nullptr;
    g_renderer.onMapLoaded(menuWorld.map);
    hudBuildRadar(hud, menuWorld.map);
    g_audio.setMap(&menuWorld.map);
    localId = -1;
    screen = SCR_MAIN;
    paused = buyOpen = inGameSettings = false;
    setCursor(false);
    g_settings.save(kConfigPath);
}

// ---------------------------------------------------------------------------
void App::run() {
    g_cfgPath = userFile("config.ini");
    g_statsPath = userFile("career.ini");
    g_settings.load(kConfigPath);
    g_stats.load(kStatsPath);
    initWeapons();
    initWindow();
    g_audio.init();
    g_renderer.init();
    ui::init();
    // live bot match in the main menu background
    menuWorld.init(MODE_COMPETITIVE);
    menuWorld.freezeTimeSec = 3; menuWorld.buyTimeSec = 5;
    const char* names[] = {"Nova", "Kairo", "Vex", "Lumen", "Rook", "Sable", "Onyx", "Talon", "Echo", "Drift"};
    for (int i = 0; i < 10; i++) menuWorld.addPlayer(names[i], true, i / 5, 3);
    menuWorld.startMatch();
    menuWorld.collectEvents = true;
    g_renderer.onMapLoaded(menuWorld.map);
    hudBuildRadar(hud, menuWorld.map);
    g_audio.setMap(&menuWorld.map);
    menuReady = true;
    snprintf(joinAddr, sizeof joinAddr, "%s", g_settings.lastServer);

    const char* test = getenv("AXIOM_TEST");
    int frameNo = 0;
    while (!quit) {
        if (WindowShouldClose()) quit = true;
        float dt = std::min(GetFrameTime(), 0.1f);
        if (test) { autotest(test, frameNo++); dt = 1.0f / 60.0f; }
        frame(dt);
    }
    if (role != ROLE_OFFLINE) leaveMatch();
    g_settings.save(kConfigPath);
    g_stats.save(kStatsPath);
    ui::shutdown();
    g_renderer.shutdown();
    g_audio.shutdown();
    CloseWindow();
}

void App::frame(float dt) {
    g_audio.update();
    ui::beginFrame();
    if (IsKeyPressed(KEY_GRAVE) || (actionPressed(ACT_CONSOLE) && g_settings.binds[ACT_CONSOLE] != KEY_GRAVE)) {
        consoleOpen = !consoleOpen;
        conInput[0] = 0;
        if (consoleOpen) ui::clearFocus();
    }
    if (screen == SCR_GAME) {
        updateGame(dt);
        BeginDrawing();
        drawGame(dt);
        if (consoleOpen) drawConsole();
        EndDrawing();
        return;
    }
    setCursor(false);
    updateMenuWorld(dt);
    if (screen == SCR_CONNECT && role == ROLE_CLIENT) {
        std::vector<GameEvent> evs;
        if (client.update(world, evs)) {
            localId = client.localId;
            g_renderer.onMapLoaded(world.map);
            hudBuildRadar(hud, world.map);
            g_audio.setMap(&world.map);
            hud.reset();
            screen = SCR_GAME;
            acc = 0; paused = false;
            viewYaw = 0; viewPitch = 0;
            statsCommitted = false;
        } else if (client.state == NetClient::FAILED || client.state == NetClient::DISCONNECTED) {
            connectError = client.reason;
            client.disconnect();
            role = ROLE_OFFLINE;
        }
    }
    BeginDrawing();
    drawMenuBackground(dt);
    switch (screen) {
    case SCR_MAIN: drawMainMenu(); break;
    case SCR_PLAY: drawPlayMenu(); break;
    case SCR_SETTINGS: drawSettings(); break;
    case SCR_STATS: drawStats(); break;
    case SCR_MATCHEND: drawMatchEnd(); break;
    case SCR_CONNECT: drawConnect(); break;
    default: break;
    }
    if (consoleOpen) drawConsole();
    EndDrawing();
}

// ---------------------------------------------------------------------------
// In-game
// ---------------------------------------------------------------------------
static WeaponId nextNade(const Player& p) {
    int start = isGrenade(p.activeW) ? (p.activeW - W_FLASH + 1) : 0;
    for (int k = 0; k < 4; k++) { int i = (start + k) % 4; if (p.nades[i]) return (WeaponId)(W_FLASH + i); }
    return W_NONE;
}

void App::localInput(float dt) {
    (void)dt;
    if (localId < 0) return;
    Player& p = world.players[localId];
    if (cursorLocked) {
        Vector2 d = GetMouseDelta();
        float sens = g_settings.sensitivity * g_settings.mYaw;
        if (p.alive && p.scope > 0) {
            const WeaponDef& wd = weaponDef(p.activeW);
            float zf = wd.scopeFov[std::clamp(p.scope - 1, 0, 1)];
            sens *= g_settings.zoomSensRatio * tanf(hfovToVfov(zf) * 0.5f * DEG) / tanf(hfovToVfov(g_settings.fov) * 0.5f * DEG);
        }
        float dy = d.y * sens * (g_settings.invertY ? -1.f : 1.f);
        viewYaw = angleNorm(viewYaw + d.x * sens);
        viewPitch = clampf(viewPitch - dy, -89.f, 89.f);
        g_renderer.notifyMouse(d.x * sens, dy);
    }
    if (buyOpen || ui::anyTextFocused()) {
        if (actionPressed(ACT_BUYMENU) || !world.canBuy(localId)) buyOpen = false;
        return;
    }
    if (actionPressed(ACT_SLOT1) && p.primary != W_NONE) pendingSelect = p.primary;
    if (actionPressed(ACT_SLOT2) && p.secondary != W_NONE) pendingSelect = p.secondary;
    if (actionPressed(ACT_SLOT3)) pendingSelect = W_KNIFE;
    if (actionPressed(ACT_SLOT4) || actionPressed(ACT_NEXTNADE)) { WeaponId n = nextNade(p); if (n) pendingSelect = n; }
    if (actionPressed(ACT_SLOT5) && p.hasBomb) pendingSelect = W_BOMB;
    if (actionPressed(ACT_LASTWEAPON) && p.hasWeapon(p.lastW)) pendingSelect = p.lastW;
    if (actionPressed(ACT_FLASH) && p.nades[0]) pendingSelect = W_FLASH;
    if (actionPressed(ACT_SMOKE) && p.nades[1]) pendingSelect = W_SMOKE;
    if (actionPressed(ACT_FRAG) && p.nades[2]) pendingSelect = W_FRAG;
    if (actionPressed(ACT_MOLLY) && p.nades[3]) pendingSelect = W_MOLLY;
    if (pendingSelect) hud.weaponListTime = hud.time;
    if (actionPressed(ACT_JUMP)) pendingButtons |= IN_JUMP;
    if (actionPressed(ACT_DROP)) pendingButtons |= IN_DROP;
    if (actionPressed(ACT_INSPECT)) pendingButtons |= IN_INSPECT;
    if (actionPressed(ACT_RELOAD)) pendingButtons |= IN_RELOAD;
    if (!g_settings.holdToCrouch && actionPressed(ACT_CROUCH)) crouchToggled = !crouchToggled;
    if (!g_settings.holdToWalk && actionPressed(ACT_WALK)) walkToggled = !walkToggled;
    if (actionPressed(ACT_BUYMENU) && world.canBuy(localId)) { buyOpen = true; hud.buyCategory = 0; }
    // spectator cycling
    if (!p.alive && actionPressed(ACT_FIRE)) {
        int start = spectateId < 0 ? 0 : spectateId + 1;
        for (int k = 0; k < MAX_PLAYERS; k++) {
            int i = (start + k) % MAX_PLAYERS;
            const Player& q = world.players[i];
            if (q.active && q.alive && i != localId && (q.team == p.team || world.aliveCount(p.team) == 0)) { spectateId = i; break; }
        }
    }
}

void App::buildCmd(UserCmd& c) {
    c = UserCmd{};
    c.yaw = viewYaw;
    c.pitch = viewPitch;
    bool overlay = paused || consoleOpen || inGameSettings || ui::anyTextFocused();
    if (overlay) { pendingButtons = 0; pendingSelect = 0; return; }
    float f = (actionDown(ACT_FORWARD) ? 1.f : 0.f) - (actionDown(ACT_BACK) ? 1.f : 0.f);
    float s = (actionDown(ACT_RIGHT) ? 1.f : 0.f) - (actionDown(ACT_LEFT) ? 1.f : 0.f);
    c.fwd = f; c.side = s;
    uint32_t b = pendingButtons;
    if (!buyOpen && cursorLocked) {
        if (actionDown(ACT_FIRE)) b |= IN_ATTACK;
        if (actionDown(ACT_ALTFIRE)) b |= IN_ATTACK2;
    }
    if (actionDown(ACT_JUMP) && g_settings.binds[ACT_JUMP] >= 0) b |= IN_JUMP;
    if (g_settings.holdToCrouch ? actionDown(ACT_CROUCH) : crouchToggled) b |= IN_DUCK;
    if (g_settings.holdToWalk ? actionDown(ACT_WALK) : walkToggled) b |= IN_WALK;
    if (actionDown(ACT_USE)) b |= IN_USE;
    if (actionDown(ACT_RELOAD)) b |= IN_RELOAD;
    if (autopilot && role == ROLE_CLIENT) { c.fwd = 1; c.side = sinf(gameTime) > 0 ? 0.5f : -0.5f; if (fmodf(gameTime, 3.0f) < 0.4f) b |= IN_ATTACK; }
    c.buttons = b;
    c.select = pendingSelect;
    pendingButtons = 0;
    pendingSelect = 0;
}

void App::processEvents(std::vector<GameEvent>& evs, const World& w) {
    int viewId = localId;
    if (localId >= 0 && !w.players[localId].alive && spectateId >= 0) viewId = spectateId;
    static int impactsThisFrame = 0;
    static double lastFrameStamp = 0;
    if (GetTime() != lastFrameStamp) { impactsThisFrame = 0; lastFrameStamp = GetTime(); }
    Vector3 listener = g_renderer.lastCamera.position;
    for (auto& e : evs) {
        hudOnEvent(hud, w, e, localId);
        g_renderer.handleEvent(w, e, viewId);
        bool mine = e.a == viewId && viewId >= 0;
        switch (e.type) {
        case EV_SHOT:
            g_audio.playShot((WeaponId)e.weapon, e.pos, mine);
            if (mine && localId == viewId) {
                const WeaponDef& wd = weaponDef((WeaponId)e.weapon);
                g_renderer.addViewKick(wd.cls == WC_SNIPER ? 1.6f : (wd.cls == WC_SHOTGUN ? 1.4f : 0.6f));
            }
            if (!mine && viewId >= 0) {
                // bullet whiz when a shot passes close to our head
                Vector3 a = e.pos, b = e.pos2, ab = b - a;
                float L2 = dot3(ab, ab);
                if (L2 > 1) {
                    float t = clampf(dot3(listener - a, ab) / L2, 0, 1);
                    Vector3 cp = a + ab * t;
                    if (len3(cp - listener) < 1.6f && t > 0.05f && t < 0.98f) g_audio.play3D(SFX_WHIZ, cp, 0.7f, 2, 8);
                }
            }
            if (w.mode == MODE_AIM && e.a == localId && aim.running) {
                aim.shots++;
                Vector3 d = norm3(e.pos2 - e.pos);
                float best = len3(e.pos2 - e.pos); int bi = -1;
                for (size_t i = 0; i < aim.targets.size(); i++) {
                    float t;
                    if (aim.targets[i].alive && raySphere(e.pos, d, aim.targets[i].pos, aim.targets[i].r, t) && t < best) { best = t; bi = (int)i; }
                }
                if (bi >= 0) {
                    aim.hits++; aim.score += aim.kind == 2 ? 1 : 100;
                    g_audio.play2D(SFX_HIT_HEAD, 0.45f, 1.0f + aim.rng.range(-0.05f, 0.05f));
                    hud.hitTime = hud.time; hud.hitKill = aim.kind != 2; hud.hitHead = false;
                    if (aim.kind != 2) {
                        aim.reactions.push_back(gameTime - aim.targets[bi].spawn);
                        aim.targets[bi].alive = false;
                    }
                }
            }
            break;
        case EV_IMPACT:
            if (impactsThisFrame++ < 6) {
                Sfx s = e.value == MAT_WOOD ? SFX_IMPACT_WOOD : (e.value == MAT_METAL ? SFX_IMPACT_METAL : SFX_IMPACT_CONCRETE);
                g_audio.play3D(s, e.pos, 0.45f, 2.0f, 30.0f);
            }
            break;
        case EV_HIT:
            if (e.a == localId && e.b != localId) {
                if (e.flags & 1) g_audio.play2D((e.flags & 4) ? SFX_HIT_HELMET : SFX_HIT_HEAD, 0.8f);
                else g_audio.play2D(SFX_HIT_BODY, 0.55f);
            } else if (e.b == viewId) g_audio.play2D(SFX_HURT, 0.6f);
            else g_audio.play3D(SFX_HIT_BODY, e.pos, 0.6f, 3, 30);
            break;
        case EV_KILL:
            if (e.a == localId && e.b != localId) g_audio.play2D(SFX_KILL, 0.7f, 1.0f + 0.08f * std::min(4, w.players[localId].roundKills - 1));
            if (e.b == localId) { deathTime = gameTime; killerId = e.a; spectateId = -1; setCursor(true); }
            if (e.b == spectateId) spectateId = -1;
            break;
        case EV_FOOTSTEP:
            if (mine) g_audio.play2D((Sfx)(SFX_STEP0 + (w.tick & 3)), 0.22f, 1.0f, BUS_FOOTSTEPS);
            else g_audio.play3D((Sfx)(SFX_STEP0 + ((w.tick + e.a) & 3)), e.pos, 1.0f, 3.0f, 30.0f, BUS_FOOTSTEPS, 0.06f);
            break;
        case EV_JUMP: if (!mine) g_audio.play3D(SFX_JUMP, e.pos, 0.6f, 2, 14, BUS_FOOTSTEPS); break;
        case EV_LAND:
            if (mine) g_audio.play2D(SFX_LAND, clampf(e.value / 80.f, 0.15f, 0.6f), 1.0f, BUS_FOOTSTEPS);
            else g_audio.play3D(SFX_LAND, e.pos, clampf(e.value / 60.f, 0.3f, 1.0f), 3, 24, BUS_FOOTSTEPS);
            break;
        case EV_RELOAD: if (mine) g_audio.play2D(SFX_RELOAD, 0.5f); else g_audio.play3D(SFX_RELOAD, e.pos, 0.6f, 2, 14); break;
        case EV_DRAW: if (mine) g_audio.play2D(SFX_DRAW, 0.4f); break;
        case EV_DRYFIRE: if (mine) g_audio.play2D(SFX_DRYFIRE, 0.6f); break;
        case EV_SCOPE: if (mine) g_audio.play2D(SFX_SCOPE, 0.6f); break;
        case EV_KNIFE_SWING: if (mine) g_audio.play2D(SFX_KNIFE_SWING, 0.5f); else g_audio.play3D(SFX_KNIFE_SWING, e.pos, 0.6f, 2, 12); break;
        case EV_KNIFE_HIT: g_audio.play3D(SFX_KNIFE_HIT, e.pos, 0.8f, 2, 16); break;
        case EV_NADE_THROW: g_audio.play3D(SFX_NADE_PIN, e.pos, 0.6f, 2, 16); break;
        case EV_NADE_BOUNCE: g_audio.play3D(SFX_NADE_BOUNCE, e.pos, 0.7f, 2, 25); break;
        case EV_FLASH_POP: g_audio.play3D(SFX_FLASHBANG, e.pos, 1.0f, 10, 90); break;
        case EV_FLASHED: if (e.b == viewId) g_audio.play2D(SFX_EAR_RING, clampf(e.value / 400.f, 0.1f, 0.8f)); break;
        case EV_FRAG_BOOM: g_audio.play3D(SFX_FRAG, e.pos, 1.0f, 12, 130); break;
        case EV_SMOKE_POP: g_audio.play3D(SFX_SMOKE, e.pos, 0.9f, 5, 40); break;
        case EV_MOLLY_POP: g_audio.play3D(SFX_FIRE, e.pos, 1.0f, 5, 40); break;
        case EV_FIRE_OUT: g_audio.play3D(SFX_FIRE_OUT, e.pos, 0.8f, 4, 30); break;
        case EV_HURT_FIRE: if (e.b == viewId) g_audio.play2D(SFX_HURT, 0.4f); break;
        case EV_BOMB_BEEP: g_audio.play3D(SFX_BOMB_BEEP, e.pos, 0.8f, 6, 60); break;
        case EV_PLANT_START: g_audio.play3D(SFX_PLANTING, e.pos, 0.8f, 4, 30); break;
        case EV_DEFUSE_START: g_audio.play3D(SFX_DEFUSING, e.pos, 0.9f, 4, 35); break;
        case EV_BOMB_PLANTED: g_audio.play2D(SFX_BOMB_PLANTED, 0.7f, 1.0f, BUS_ANNOUNCER); break;
        case EV_BOMB_DEFUSED: g_audio.play2D(SFX_ROUND_WIN, 0.4f, 1.3f, BUS_ANNOUNCER); break;
        case EV_BOMB_EXPLODE: g_audio.play3D(SFX_BOMB_EXPLODE, e.pos, 1.0f, 30, 250, BUS_EFFECTS, 0); break;
        case EV_LIVE: g_audio.play2D(SFX_ROUND_START, 0.6f, 1.0f, BUS_ANNOUNCER); break;
        case EV_ROUND_END: {
            Team lt = localId >= 0 ? w.players[localId].team : TEAM_NONE;
            g_audio.play2D((Team)e.a == lt ? SFX_ROUND_WIN : SFX_ROUND_LOSE, 0.6f, 1.0f, BUS_ANNOUNCER);
            break;
        }
        case EV_MATCH_END: g_audio.play2D(SFX_MATCH_WIN, 0.6f, 1.0f, BUS_ANNOUNCER); break;
        case EV_BUY: if (e.a == localId) g_audio.play2D(SFX_UI_BUY, 0.6f, 1.0f, BUS_UI); break;
        case EV_PICKUP: case EV_BOMB_PICKUP: if (mine) g_audio.play2D(SFX_PICKUP, 0.6f); break;
        case EV_RESPAWN:
            if (e.a == localId) { viewYaw = w.players[localId].yaw; viewPitch = 0; spectateId = -1; }
            if (w.mode == MODE_PRACTICE && e.a >= 0 && w.players[e.a].isBot) {
                auto& p = world.players[e.a];
                const auto& L = (e.a % 2) ? world.map.holdA : world.map.holdB;
                if (!L.empty()) { const HoldSpot& h = L[(e.a + (int)world.tick) % L.size()]; p.pos = p.prevPos = h.pos; p.yaw = h.yaw + 180; }
            }
            break;
        case EV_ROUND_START:
            if (localId >= 0) { viewYaw = w.players[localId].yaw; viewPitch = 0; spectateId = -1; buyOpen = false; }
            break;
        default: break;
        }
    }
}

void App::updateAim(float dt) {
    (void)dt;
    if (world.mode != MODE_AIM || localId < 0) return;
    if (!aim.running && !aim.finished && gameTime >= aim.start) { aim.running = true; aim.start = gameTime; }
    if (!aim.running) return;
    float t = gameTime - aim.start;
    if (t >= aim.duration) {
        aim.running = false; aim.finished = true;
        int* best = aim.kind == 0 ? &g_stats.aimGridBest : aim.kind == 1 ? &g_stats.aimFlickBest : &g_stats.aimTrackBest;
        if (aim.score > *best) *best = aim.score;
        if (!aim.reactions.empty()) {
            float avg = 0; for (float r : aim.reactions) avg += r; avg = avg / aim.reactions.size() * 1000;
            if (g_stats.aimBestReactionMs == 0 || avg < g_stats.aimBestReactionMs) g_stats.aimBestReactionMs = avg;
        }
        g_stats.save(kStatsPath);
        setCursor(false);
        return;
    }
    Vector3 o = world.players[localId].eye();
    aim.targets.erase(std::remove_if(aim.targets.begin(), aim.targets.end(), [](const AimTarget& a) { return !a.alive; }), aim.targets.end());
    if (aim.kind == 0) {
        while (aim.targets.size() < 3) {
            int gx = aim.rng.irange(0, 4), gy = aim.rng.irange(0, 3);
            Vector3 p = aim.origin + aim.fwd * 13.f + aim.right * (-4.8f + gx * 2.4f) + V3(0, 0.9f + gy * 0.95f, 0);
            bool dup = false;
            for (auto& a : aim.targets) if (len3(a.pos - p) < 0.5f) dup = true;
            if (!dup) aim.targets.push_back({p, {0, 0, 0}, 0.42f, gameTime, true});
        }
    } else if (aim.kind == 1) {
        if (aim.targets.empty()) {
            float baseYaw, bp; anglesFromDir(aim.fwd, baseYaw, bp);
            float yaw = baseYaw + aim.rng.range(-55, 55), pitch = aim.rng.range(-6, 18), d = aim.rng.range(8, 16);
            Vector3 p = o + forwardFromAngles(yaw, pitch) * d;
            p.y = std::max(p.y, 0.5f);
            aim.targets.push_back({p, {0, 0, 0}, 0.32f, gameTime + 0.0f, true});
        }
    } else {
        if (aim.targets.empty()) aim.targets.push_back({aim.origin + aim.fwd * 12.f + V3(0, 1.5f, 0), {3, 0, 0}, 0.45f, gameTime, true});
        AimTarget& a = aim.targets[0];
        // vel.x = lateral speed along aim.right, vel.y = vertical speed
        if (aim.rng.f01() < 0.012f) a.vel.x = aim.rng.range(-6, 6);
        if (aim.rng.f01() < 0.008f) a.vel.y = aim.rng.range(-1.5f, 1.5f);
        a.pos += aim.right * (a.vel.x * dt) + V3(0, a.vel.y * dt, 0);
        float lat = dot3(a.pos - aim.origin, aim.right);
        if (fabsf(lat) > 7) { a.vel.x = -a.vel.x; a.pos -= aim.right * (lat - clampf(lat, -7, 7)); }
        if (a.pos.y < 0.8f || a.pos.y > 3.0f) { a.vel.y = -a.vel.y; a.pos.y = clampf(a.pos.y, 0.8f, 3.0f); }
    }
}

void App::commitMatchStats() {
    if (statsCommitted || localId < 0) return;
    statsCommitted = true;
    ratingDelta = 0;
    if (world.mode != MODE_COMPETITIVE) return;
    const Player& p = world.players[localId];
    bool won = world.matchWinnerSquad == p.squad;
    g_stats.matches++;
    if (won) g_stats.wins++; else g_stats.losses++;
    g_stats.kills += p.kills; g_stats.deaths += p.deaths; g_stats.assists += p.assists; g_stats.headshots += p.headshots;
    g_stats.shotsFired += p.shotsFiredTotal; g_stats.shotsHit += p.shotsHitTotal; g_stats.roundsPlayed += (int)world.history.size();
    g_stats.mvps += p.mvps; g_stats.plants += p.plants; g_stats.defuses += p.defuses; g_stats.damage += p.damage;
    if (role == ROLE_OFFLINE) {
        float botRating = 700.f + 220.f * g_settings.botDifficulty;
        float expected = 1.f / (1.f + powf(10.f, (botRating - g_stats.rating) / 400.f));
        float kd = (float)p.kills / std::max(1, p.deaths);
        ratingDelta = (int)roundf(48.f * ((won ? 1.f : 0.f) - expected) + clampf((kd - 1.f) * 4.f, -5, 6));
        g_stats.rating = std::max(100, g_stats.rating + ratingDelta);
    }
    g_stats.save(kStatsPath);
}

void App::updateGame(float dt) {
    gameTime += dt;
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (consoleOpen) consoleOpen = false;
        else if (ui::capturingKey()) ui::cancelCapture();
        else if (buyOpen) buyOpen = false;
        else if (inGameSettings) { inGameSettings = false; g_settings.save(kConfigPath); }
        else if (world.mode == MODE_AIM && aim.finished) leaveMatch();
        else paused = !paused;
        if (screen != SCR_GAME) return;
    }
    bool overlay = paused || consoleOpen || inGameSettings || buyOpen || (world.mode == MODE_AIM && aim.finished);
    if (!paused && !consoleOpen && !inGameSettings) localInput(dt);
    bool scoreboard = actionDown(ACT_SCOREBOARD) && !overlay;
    (void)scoreboard;
    setCursor(!overlay && IsWindowFocused());

    std::vector<GameEvent> evs;
    if (role == ROLE_CLIENT) {
        if (client.update(world, evs)) {
            localId = client.localId;
            g_renderer.onMapLoaded(world.map);
            hudBuildRadar(hud, world.map);
        }
        if (client.state == NetClient::FAILED || client.state == NetClient::DISCONNECTED) {
            connectError = client.reason.empty() ? "Déconnecté" : client.reason;
            leaveMatch();
            screen = SCR_CONNECT;
            return;
        }
        localId = client.localId;
        acc += dt;
        int n = 0;
        while (acc >= TICK_DT && n < 8) {
            UserCmd c;
            buildCmd(c);
            c.tick = client.nextCmdTick();
            float vt = (client.serverTimeEstimate() - g_settings.interpMs * 0.001f) / TICK_DT;
            c.viewTick = (uint32_t)std::max(0.f, vt);
            client.predict(world, c);
            for (auto& e : world.events) evs.push_back(e);
            world.events.clear();
            acc -= TICK_DT; n++;
        }
        if (n == 8) acc = 0;
        client.interpolate(world);
        hud.pingMs = client.rttMs;
        hud.lossPct = client.lossPct;
        processEvents(evs, world);
    } else {
        bool simPaused = paused && role == ROLE_OFFLINE;
        if (!simPaused) acc += dt;
        int n = 0;
        while (acc >= TICK_DT && n < 8) {
            if (role == ROLE_HOST) server.poll(world);
            UserCmd cmds[MAX_PLAYERS];
            for (int i = 0; i < MAX_PLAYERS; i++) {
                cmds[i] = UserCmd{};
                Player& p = world.players[i];
                if (!p.active) continue;
                if (p.isBot) bots.think(world, i, cmds[i]);
                else if (i == localId) buildCmd(cmds[i]);
                else if (p.isRemote && role == ROLE_HOST) server.takeCmd(world, i, cmds[i]);
            }
            world.step(cmds);
            if (role == ROLE_HOST) {
                server.afterStep(world);
                server.recordEvents(world.events);
                int every = std::max(1, TICK_RATE / std::clamp(g_settings.rate, 16, 128));
                if (world.tick % every == 0) server.sendSnapshots(world);
            }
            for (auto& e : world.events) bots.onEvent(world, e);
            processEvents(world.events, world);
            world.events.clear();
            acc -= TICK_DT; n++;
        }
        if (n == 8) acc = 0;
    }
    updateAim(dt);
    // grenade trajectory preview (practice)
    nadePath.clear();
    if (localId >= 0 && world.mode == MODE_PRACTICE) {
        const Player& p = world.players[localId];
        if (p.alive && isGrenade(p.activeW)) {
            float s = actionDown(ACT_ALTFIRE) && !actionDown(ACT_FIRE) ? 0.38f : (actionDown(ACT_ALTFIRE) ? 0.68f : 1.0f);
            world.predictGrenade(p, s, nadePath);
        }
    }
    g_renderer.update(dt, world);
    if (world.phase == PH_MATCH_END) {
        static float endSeen = -1;
        if (endSeen < 0) endSeen = gameTime;
        if (gameTime - endSeen > 4.0f) {
            endSeen = -1;
            commitMatchStats();
            screen = SCR_MATCHEND;
            setCursor(false);
            if (role == ROLE_HOST) server.stop(world);
            if (role == ROLE_CLIENT) client.disconnect();
            role = ROLE_OFFLINE;
        }
    }
}

void App::drawGame(float dt) {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    int viewId = localId;
    ViewState v;
    v.frameTime = dt;
    v.alpha = clampf(acc / TICK_DT, 0, 1);
    float vpf = 0.5f * g_settings.viewPunchScale;
    float hfov = g_settings.fov;
    if (localId >= 0) {
        const Player& me = world.players[localId];
        if (autopilot) { viewYaw = me.yaw; viewPitch = me.pitch; }
        if (me.alive) {
            Vector3 pos = lerp3(me.prevPos, me.pos, v.alpha);
            float duck = lerpf(me.prevDuck, me.duck, v.alpha);
            v.eye = pos + V3(0, lerpf(STAND_EYE, CROUCH_EYE, duck), 0);
            v.yaw = viewYaw + me.punchYaw * vpf;
            v.pitch = clampf(viewPitch + me.punchPitch * vpf, -89, 89);
            v.localId = localId;
            if (me.scope > 0) hfov = weaponDef(me.activeW).scopeFov[std::clamp(me.scope - 1, 0, 1)];
            if (world.time < me.flashEnd) {}   // handled in hud
        } else {
            float since = gameTime - deathTime;
            bool freeMode = world.mode != MODE_COMPETITIVE;
            if (since < 2.5f || freeMode) {
                v.eye = me.pos + V3(0, 1.0f + 1.2f * clampf(since / 1.0f, 0, 1), 0);
                v.yaw = viewYaw; v.pitch = viewPitch;
                if (killerId >= 0 && killerId != localId && world.players[killerId].active) {
                    float y, p; anglesFromDir(world.players[killerId].eye() - v.eye, y, p);
                    v.yaw = y; v.pitch = p;
                }
                v.localId = -1;
            } else {
                if (spectateId < 0 || !world.players[spectateId].alive) {
                    spectateId = -1;
                    for (auto& q : world.players) if (q.active && q.alive && q.id != localId && q.team == me.team) { spectateId = q.id; break; }
                    if (spectateId < 0) for (auto& q : world.players) if (q.active && q.alive && q.id != localId) { spectateId = q.id; break; }
                }
                if (spectateId >= 0) {
                    const Player& t = world.players[spectateId];
                    Vector3 pos = lerp3(t.prevPos, t.pos, v.alpha);
                    v.eye = pos + V3(0, t.eyeHeight(), 0);
                    v.yaw = t.yaw + t.punchYaw * vpf; v.pitch = t.pitch + t.punchPitch * vpf;
                    v.localId = spectateId;
                    viewId = spectateId;
                    if (t.scope > 0) hfov = weaponDef(t.activeW).scopeFov[std::clamp(t.scope - 1, 0, 1)];
                } else {
                    v.eye = V3(0, 30, 40); v.yaw = 180; v.pitch = -40; v.localId = -1;
                }
            }
        }
    }
    v.fovY = hfovToVfov(hfov);
    Vector3 f = forwardFromAngles(v.yaw, v.pitch);
    g_audio.setListener(v.eye, f, norm3(cross3(f, V3(0, 1, 0))));

    // custom 3D: aim targets + grenade path
    g_renderer.extra3D = [&]() {
        for (auto& t : aim.targets) {
            if (!t.alive) continue;
            g_renderer.drawSphere(t.pos, t.r, theme::accent, 0.55f);
            g_renderer.drawSphere(t.pos, t.r * 0.45f, WHITE, 0.9f);
        }
        if (nadePath.size() > 1) {
            for (size_t i = 0; i + 1 < nadePath.size(); i++) DrawLine3D(nadePath[i], nadePath[i + 1], Color{255, 220, 80, 255});
            DrawSphere(nadePath.back(), 0.15f, Color{255, 120, 60, 200});
        }
    };

    ClearBackground(Color{150, 180, 210, 255});
    DrawRectangleGradientV(0, 0, (int)W, (int)H, Color{86, 128, 182, 255}, Color{196, 212, 230, 255});
    g_renderer.render(world, v);
    hudDraw(hud, world, localId, viewId, v, dt);

    if (world.mode == MODE_AIM) drawAimHud();

    // buy menu
    if (buyOpen && localId >= 0) {
        bool close = false;
        int item = hudBuyMenu(hud, world, localId, close);
        if (item != 0) {
            bool ok;
            if (role == ROLE_CLIENT) { client.sendBuy(item); ok = true; }
            else ok = item > 0 ? world.tryBuy(localId, item) : world.trySell(localId, -item);
            if (!ok) g_audio.play2D(SFX_UI_DENY, 0.6f, 1.0f, BUS_UI);
            else if (item < 0) g_audio.play2D(SFX_PICKUP, 0.5f, 1.0f, BUS_UI);
        }
        if (close) buyOpen = false;
    }
    bool overlay = paused || inGameSettings;
    if ((actionDown(ACT_SCOREBOARD) && !overlay && !consoleOpen) || (world.phase == PH_MATCH_END) || forceScoreboard) hudScoreboard(world, localId);
    if (paused && !inGameSettings) drawPause();
    if (inGameSettings) {
        DrawRectangle(0, 0, (int)W, (int)H, Color{0, 0, 0, 170});
        drawSettingsPanel({W / 2 - S(700), S(70), S(1400), H - S(140)});
    }
    if (world.phase == PH_MATCH_END) {
        Team lt = localId >= 0 ? world.players[localId].team : TEAM_NONE;
        bool won = localId >= 0 && world.matchWinnerSquad == world.players[localId].squad;
        if (world.mode == MODE_DEATHMATCH) {
            int best = -1, bk = -1;
            for (auto& p : world.players) if (p.active && p.kills > bk) { bk = p.kills; best = p.id; }
            won = best == localId;
        }
        (void)lt;
        textShadow(won ? "VICTOIRE" : "DÉFAITE", W / 2, S(90), S(90), won ? theme::good : theme::bad, true, AL_CENTER);
    }
}

void App::drawAimHud() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    const char* kinds[] = {"GRIDSHOT", "FLICK", "TRACKING"};
    if (!aim.running && !aim.finished) {
        float c = aim.start - gameTime;
        textShadow(kinds[aim.kind], W / 2, H * 0.3f, S(48), theme::accent, true, AL_CENTER);
        textShadow(fmt("%d", (int)ceilf(c)), W / 2, H * 0.38f, S(96), WHITE, true, AL_CENTER);
        return;
    }
    float left = aim.running ? aim.duration - (gameTime - aim.start) : 0;
    float accp = aim.shots ? 100.f * aim.hits / aim.shots : 0;
    float avg = 0; for (float r : aim.reactions) avg += r;
    avg = aim.reactions.empty() ? 0 : avg / aim.reactions.size() * 1000;
    panel({W / 2 - S(260), S(16), S(520), S(70)}, Color{10, 14, 20, 200});
    text(fmt("%.1f", left), W / 2, S(22), S(40), WHITE, true, AL_CENTER);
    text(fmt("SCORE %d", aim.score), W / 2 - S(240), S(34), S(22), theme::accent, true);
    text(fmt("PRÉCISION %.0f%%", accp), W / 2 + S(240), S(34), S(22), theme::text, true, AL_RIGHT);
    if (aim.finished) {
        Rectangle r = {W / 2 - S(320), H / 2 - S(220), S(640), S(440)};
        panel(r, theme::bg);
        DrawRectangle((int)r.x, (int)r.y, (int)r.width, (int)S(4), theme::accent);
        text(fmt("%s TERMINÉ", kinds[aim.kind]), r.x + r.width / 2, r.y + S(30), S(36), theme::text, true, AL_CENTER);
        int best = aim.kind == 0 ? g_stats.aimGridBest : aim.kind == 1 ? g_stats.aimFlickBest : g_stats.aimTrackBest;
        text(fmt("%d", aim.score), r.x + r.width / 2, r.y + S(90), S(80), theme::accent, true, AL_CENTER);
        text(fmt("Record personnel : %d", best), r.x + r.width / 2, r.y + S(190), S(20), aim.score >= best ? theme::gold : theme::dim, true, AL_CENTER);
        text(fmt("Précision %.1f%%   •   Touches %d / %d", accp, aim.hits, aim.shots), r.x + r.width / 2, r.y + S(230), S(20), theme::text, false, AL_CENTER);
        if (aim.kind != 2) text(fmt("Temps de réaction moyen : %.0f ms", avg), r.x + r.width / 2, r.y + S(262), S(20), theme::text, false, AL_CENTER);
        if (button({r.x + S(40), r.y + r.height - S(90), S(260), S(54)}, "REJOUER", true)) { aimKind = aim.kind; startOffline(MODE_AIM); }
        if (button({r.x + r.width - S(300), r.y + r.height - S(90), S(260), S(54)}, "MENU")) leaveMatch();
    }
}

void App::drawPause() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    DrawRectangle(0, 0, (int)W, (int)H, Color{0, 0, 0, 150});
    Rectangle r = {W / 2 - S(240), H / 2 - S(240), S(480), S(480)};
    panel(r, theme::bg);
    DrawRectangle((int)r.x, (int)r.y, (int)r.width, (int)S(4), theme::accent);
    text("PAUSE", r.x + r.width / 2, r.y + S(26), S(38), theme::text, true, AL_CENTER);
    if (role == ROLE_HOST) {
        auto ips = sock::localAddresses();
        std::string s = "Votre IP : ";
        for (size_t i = 0; i < ips.size() && i < 2; i++) s += (i ? ", " : "") + ips[i];
        text(fmt("%s   •   port %d", s.c_str(), g_settings.netPort), r.x + r.width / 2, r.y + S(76), S(16), theme::accent, true, AL_CENTER);
        text(fmt("%d joueur(s) connecté(s)", server.humanCount()), r.x + r.width / 2, r.y + S(100), S(16), theme::dim, false, AL_CENTER);
    } else if (role == ROLE_OFFLINE) text("La partie est en pause", r.x + r.width / 2, r.y + S(80), S(17), theme::dim, false, AL_CENTER);
    float y = r.y + S(140);
    if (button({r.x + S(40), y, r.width - S(80), S(58)}, "REPRENDRE", true)) paused = false;
    y += S(74);
    if (button({r.x + S(40), y, r.width - S(80), S(58)}, "PARAMÈTRES")) { inGameSettings = true; settingsScroll = 0; }
    y += S(74);
    if (world.mode != MODE_COMPETITIVE && role == ROLE_OFFLINE) {
        if (button({r.x + S(40), y, r.width - S(80), S(58)}, "RECOMMENCER")) { startOffline(world.mode); return; }
        y += S(74);
    }
    if (button({r.x + S(40), y, r.width - S(80), S(58)}, "QUITTER LE MATCH")) leaveMatch();
}

void App::drawConsole() {
    float W = (float)GetScreenWidth();
    float h = S(420);
    DrawRectangle(0, 0, (int)W, (int)h, Color{6, 9, 14, 235});
    DrawRectangle(0, (int)h, (int)W, 2, theme::accent);
    float y = h - S(70);
    for (int i = (int)conLog.size() - 1; i >= 0 && y > S(10); i--) {
        text(conLog[i].c_str(), S(14), y, S(17), theme::text);
        y -= S(22);
    }
    Rectangle in = {S(10), h - S(44), W - S(20), S(36)};
    panel(in, Color{20, 26, 38, 255});
    ui::frame(in, theme::accent, 1);
    text(conInput[0] ? conInput : "Commande (help pour la liste)", in.x + S(10), in.y + S(8), S(18), conInput[0] ? theme::text : theme::dim);
    if (fmodf((float)GetTime(), 1.0f) < 0.5f) DrawRectangle((int)(in.x + S(12) + textWidth(conInput, S(18))), (int)(in.y + S(8)), 2, (int)S(20), theme::accent);
    int c;
    while ((c = GetCharPressed()) > 0) {
        size_t len = strlen(conInput);
        if (c >= 32 && c < 127 && c != '`' && len < sizeof conInput - 1) { conInput[len] = (char)c; conInput[len + 1] = 0; }
    }
    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && strlen(conInput) > 0) conInput[strlen(conInput) - 1] = 0;
    if (IsKeyPressed(KEY_ENTER) && conInput[0]) {
        conLog.push_back(std::string("> ") + conInput);
        execCommand(conInput);
        conInput[0] = 0;
    }
    if (conLog.size() > 200) conLog.erase(conLog.begin(), conLog.begin() + 50);
}

void App::execCommand(const std::string& line) {
    std::vector<std::string> t;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && line[i] == ' ') i++;
        size_t j = i;
        while (j < line.size() && line[j] != ' ') j++;
        if (j > i) t.push_back(line.substr(i, j - i));
        i = j;
    }
    if (t.empty()) return;
    const std::string& c = t[0];
    if (c == "help") {
        conLog.push_back("Commandes : quit, disconnect, connect <ip[:port]>, host, restart, crosshair <code>, clear, stats, cvars");
        conLog.push_back("Toute variable de config.ini : <nom> [valeur]   ex: sensitivity 1.8 | fps_max 400 | net_graph 1");
    } else if (c == "quit" || c == "exit") quit = true;
    else if (c == "disconnect") { if (screen == SCR_GAME) leaveMatch(); }
    else if (c == "connect" && t.size() > 1) { snprintf(joinAddr, sizeof joinAddr, "%s", t[1].c_str()); startJoin(); }
    else if (c == "host") startHost(MODE_COMPETITIVE);
    else if (c == "restart") { if (screen == SCR_GAME && role == ROLE_OFFLINE) startOffline(world.mode); }
    else if (c == "clear") conLog.clear();
    else if (c == "crosshair") {
        if (t.size() > 1) conLog.push_back(g_settings.importCrosshairCode(t[1]) ? "Réticule importé." : "Code invalide.");
        else conLog.push_back(g_settings.crosshairCode());
    } else if (c == "stats") {
        conLog.push_back(fmt("Matchs %d  Victoires %d  K %d  D %d  HS %d  Rating %d", g_stats.matches, g_stats.wins, g_stats.kills, g_stats.deaths, g_stats.headshots, g_stats.rating));
    } else if (c == "cvars") {
        std::string s;
        for (auto& k : settingKeys()) { s += k + " "; if (s.size() > 120) { conLog.push_back(s); s.clear(); } }
        if (!s.empty()) conLog.push_back(s);
    } else {
        std::string cur = settingGet(c);
        if (cur.empty() && c != "name") { conLog.push_back("Commande inconnue : " + c); return; }
        if (t.size() > 1) {
            std::string v = t[1];
            for (size_t k = 2; k < t.size(); k++) v += " " + t[k];
            settingSet(c, v);
            conLog.push_back(c + " = " + settingGet(c));
            if (c == "fps_max") SetTargetFPS(g_settings.fpsMax);
            g_settings.save(kConfigPath);
        } else conLog.push_back(c + " = " + cur);
    }
}

// ---------------------------------------------------------------------------
// Menus
// ---------------------------------------------------------------------------
void App::updateMenuWorld(float dt) {
    if (!menuReady) return;
    menuAcc += dt;
    int n = 0;
    while (menuAcc >= TICK_DT && n < 4) {
        UserCmd cmds[MAX_PLAYERS];
        for (int i = 0; i < MAX_PLAYERS; i++) if (menuWorld.players[i].active) menuBots.think(menuWorld, i, cmds[i]);
        menuWorld.step(cmds);
        for (auto& e : menuWorld.events) { menuBots.onEvent(menuWorld, e); g_renderer.handleEvent(menuWorld, e, -1); }
        menuWorld.events.clear();
        if (menuWorld.phase == PH_MATCH_END) { menuWorld.startMatch(); menuBots.reset(); }
        menuAcc -= TICK_DT; n++;
    }
    if (n == 4) menuAcc = 0;
    g_renderer.update(dt, menuWorld);
    menuCam += dt * 0.035f;
}

void App::drawMenuBackground(float dt) {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    ClearBackground(Color{150, 180, 210, 255});
    DrawRectangleGradientV(0, 0, (int)W, (int)H, Color{86, 128, 182, 255}, Color{196, 212, 230, 255});
    if (screen != SCR_MATCHEND || true) {
        ViewState v;
        v.frameTime = dt;
        v.alpha = clampf(menuAcc / TICK_DT, 0, 1);
        float r = 46.f;
        v.eye = V3(cosf(menuCam) * r, 30.f, sinf(menuCam) * r);
        float y, p; anglesFromDir(V3(0, 0, 0) - v.eye, y, p);
        v.yaw = y; v.pitch = p + 6;
        v.fovY = 60;
        v.localId = -1;
        v.thirdPerson = true;
        g_renderer.extra3D = nullptr;
        g_audio.setListener(v.eye, forwardFromAngles(v.yaw, v.pitch), flatRight(v.yaw));
        g_renderer.render(menuWorld, v);
    }
    DrawRectangleGradientH(0, 0, (int)(W * 0.62f), (int)H, Color{6, 9, 15, 245}, Color{6, 9, 15, 0});
    DrawRectangleGradientV(0, (int)(H * 0.75f), (int)W, (int)(H * 0.25f), Color{6, 9, 15, 0}, Color{6, 9, 15, 200});
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
    logo(x, S(150), S(150));
    text("TACTICAL COMPETITIVE SHOOTER", x + S(4), S(330), S(22), theme::dim, true);
    float y = S(420);
    float bw = S(420), bh = S(64);
    if (button({x, y, bw, bh}, "JOUER", true)) { screen = SCR_PLAY; playMode = 0; }
    y += bh + S(14);
    if (button({x, y, bw, bh}, "ENTRAÎNEMENT")) { screen = SCR_PLAY; playMode = 3; }
    y += bh + S(14);
    if (button({x, y, bw, bh}, "MULTIJOUEUR")) { screen = SCR_PLAY; playMode = 4; }
    y += bh + S(14);
    if (button({x, y, bw, bh}, "PARAMÈTRES")) { screen = SCR_SETTINGS; settingsScroll = 0; }
    y += bh + S(14);
    if (button({x, y, bw, bh}, "CARRIÈRE")) screen = SCR_STATS;
    y += bh + S(14);
    if (button({x, y, bw, bh}, "QUITTER")) quit = true;

    // player card
    Rectangle card = {W - S(520), S(110), S(420), S(250)};
    panel(card, theme::bg);
    DrawRectangle((int)card.x, (int)card.y, (int)S(4), (int)card.height, rankColor(g_stats.rating));
    text(g_settings.playerName, card.x + S(28), card.y + S(22), S(30), theme::text, true);
    text(rankName(g_stats.rating), card.x + S(28), card.y + S(66), S(40), rankColor(g_stats.rating), true);
    text(fmt("%d PTS", g_stats.rating), card.x + card.width - S(28), card.y + S(78), S(22), theme::dim, true, AL_RIGHT);
    float kd = (float)g_stats.kills / std::max(1, g_stats.deaths);
    float wr = g_stats.matches ? 100.f * g_stats.wins / g_stats.matches : 0;
    text(fmt("MATCHS %d", g_stats.matches), card.x + S(28), card.y + S(140), S(18), theme::text, true);
    text(fmt("VICTOIRES %.0f%%", wr), card.x + S(28), card.y + S(170), S(18), theme::text, true);
    text(fmt("K/D %.2f", kd), card.x + S(230), card.y + S(140), S(18), theme::text, true);
    text(fmt("HS %.0f%%", g_stats.kills ? 100.f * g_stats.headshots / g_stats.kills : 0.f), card.x + S(230), card.y + S(170), S(18), theme::text, true);
    text("Classement contre les bots (Elo)", card.x + S(28), card.y + S(212), S(15), theme::dim);

    Rectangle news = {W - S(520), S(384), S(420), S(360)};
    panel(news, theme::bg);
    text("NOUVEAUTÉS  v" AXIOM_VERSION_STR, news.x + S(28), news.y + S(22), S(20), theme::accent, true);
    const char* lines[] = {
        "• Simulation serveur 128 tick, rendu illimité",
        "• 12 armes, sprays déterministes à maîtriser",
        "• Contre-strafe, air-strafe, crouch-jump",
        "• Bots à 5 niveaux avec stratégies d'équipe",
        "• Wallbangs, fumigènes, flashs, incendiaires",
        "• Multijoueur : serveur autoritaire + lag comp",
        "• Entraînement de visée et mode pratique",
        "• Mises à jour automatiques via le launcher",
    };
    for (int i = 0; i < 8; i++) text(lines[i], news.x + S(28), news.y + S(64) + i * S(34), S(17), theme::text);
    text(fmt("AXIOM %s  •  carte : CITADEL  •  [ ` ] console", AXIOM_VERSION_STR), S(24), H - S(36), S(15), theme::dim);
}

void App::drawPlayMenu() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    text("JOUER", S(110), S(70), S(56), theme::text, true);
    if (button({W - S(260), S(80), S(160), S(50)}, "RETOUR")) { screen = SCR_MAIN; g_settings.save(kConfigPath); }
    const char* titles[] = {"COMPÉTITIF", "DEATHMATCH", "PRATIQUE", "VISÉE", "MULTIJOUEUR"};
    const char* subs[] = {"5v5 contre des bots • désamorçage", "Réapparition instantanée", "Grenades, sprays, angles", "Gridshot • Flick • Tracking", "Héberger ou rejoindre (LAN/IP)"};
    float cw = (W - S(220) - S(4 * 18)) / 5;
    for (int i = 0; i < 5; i++) {
        Rectangle r = {S(110) + i * (cw + S(18)), S(170), cw, S(150)};
        bool act = playMode == i;
        bool hv = hovered(r);
        panel(r, act ? Color{20, 50, 66, 240} : (hv ? Color{26, 34, 50, 240} : theme::bg));
        DrawRectangle((int)r.x, (int)(r.y + r.height - S(4)), (int)r.width, (int)S(4), act ? theme::accent : Color{255, 255, 255, 20});
        text(titles[i], r.x + S(22), r.y + S(36), S(28), act ? theme::accent : theme::text, true);
        text(subs[i], r.x + S(22), r.y + S(84), S(16), theme::dim);
        if (hv && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { playMode = i; g_audio.play2D(SFX_UI_CLICK, 0.6f, 1, BUS_UI); }
    }
    Rectangle opt = {S(110), S(350), W - S(220), H - S(470)};
    panel(opt, theme::bg);
    float x = opt.x + S(30), y = opt.y + S(30), w = S(640), rh = S(50), gap = S(10);
    static const char* diffs[] = {"RECRUE", "RÉGULIER", "VÉTÉRAN", "EXPERT", "ÉLITE"};
    static const char* rtw[] = {"5 (MR8)", "8 (MR14)", "13 (MR24)", "16 (MR30)"};
    static const int rtwv[] = {5, 8, 13, 16};
    auto R = [&]() { Rectangle r = {x, y, w, rh}; y += rh + gap; return r; };
    float rx = opt.x + opt.width - S(560);
    if (playMode == 0) {
        choice(R(), "Difficulté des bots", &g_settings.botDifficulty, diffs, 5);
        sliderInt(R(), "Joueurs par équipe", &g_settings.teamSize, 1, 5);
        int ri = 2; for (int i = 0; i < 4; i++) if (rtwv[i] == g_settings.roundsToWin) ri = i;
        if (choice(R(), "Rounds pour gagner", &ri, rtw, 4)) g_settings.roundsToWin = rtwv[ri];
        sliderInt(R(), "Durée d'un round (s)", &g_settings.roundTime, 60, 150);
        sliderInt(R(), "Argent de départ", &g_settings.startMoney, 800, 16000);
        toggle(R(), "Tir allié", &g_settings.friendlyFire);
        text("Les sides s'inversent à la mi-temps. Prolongations en MR3 avec 10 000 $.", rx, opt.y + S(40), S(17), theme::dim);
        text("Votre classement évolue selon la difficulté battue.", rx, opt.y + S(70), S(17), theme::dim);
        if (button({rx, opt.y + opt.height - S(100), S(500), S(70)}, "LANCER LE MATCH", true)) startOffline(MODE_COMPETITIVE);
    } else if (playMode == 1) {
        choice(R(), "Difficulté des bots", &g_settings.botDifficulty, diffs, 5);
        sliderInt(R(), "Taille (x2 joueurs)", &g_settings.teamSize, 1, 7);
        sliderInt(R(), "Durée (minutes)", &g_settings.dmTimeLimit, 2, 20);
        toggle(R(), "Headshots uniquement", &g_settings.dmHeadshotOnly);
        text("Chacun pour soi. Ouvrez l'armurerie (B) pour choisir vos armes.", rx, opt.y + S(40), S(17), theme::dim);
        if (button({rx, opt.y + opt.height - S(100), S(500), S(70)}, "LANCER LE DEATHMATCH", true)) startOffline(MODE_DEATHMATCH);
    } else if (playMode == 2) {
        text("Pratique libre sur CITADEL", x, y, S(26), theme::text, true); y += S(50);
        const char* l[] = {"• Argent et munitions infinis, achat partout (B)", "• Trajectoire des grenades affichée en direct",
                           "• Cibles immobiles sur les positions de défense", "• Idéal pour apprendre les sprays et les lineups"};
        for (auto s : l) { text(s, x, y, S(19), theme::text); y += S(36); }
        if (button({rx, opt.y + opt.height - S(100), S(500), S(70)}, "LANCER LA PRATIQUE", true)) startOffline(MODE_PRACTICE);
    } else if (playMode == 3) {
        static const char* kinds[] = {"GRIDSHOT", "FLICK", "TRACKING"};
        choice(R(), "Exercice", &aimKind, kinds, 3);
        const char* desc[] = {"3 cibles simultanées sur une grille : vitesse + précision.", "Une cible à la fois, n'importe où : réflexes et flicks.",
                              "Une cible mobile qui change de direction : suivi continu."};
        text(desc[aimKind], x, y + S(10), S(18), theme::dim);
        text(fmt("Records :  Gridshot %d  •  Flick %d  •  Tracking %d  •  Réaction %.0f ms", g_stats.aimGridBest, g_stats.aimFlickBest, g_stats.aimTrackBest, g_stats.aimBestReactionMs), x, y + S(60), S(18), theme::gold, true);
        if (button({rx, opt.y + opt.height - S(100), S(500), S(70)}, "COMMENCER (60 s)", true)) startOffline(MODE_AIM);
    } else if (playMode == 4) {
        text("Pseudo", x, y, S(18), theme::dim, true); y += S(26);
        textField({x, y, w, S(46)}, g_settings.playerName, sizeof g_settings.playerName, 1, "Votre pseudo"); y += S(70);
        text("HÉBERGER", x, y, S(24), theme::accent, true); y += S(40);
        static int hostMode = 0;
        static const char* hm[] = {"COMPÉTITIF 5v5", "DEATHMATCH"};
        choice(R(), "Mode", &hostMode, hm, 2);
        sliderInt(R(), "Port UDP", &g_settings.netPort, 1024, 65000);
        choice(R(), "Difficulté des bots (places libres)", &g_settings.botDifficulty, diffs, 5);
        if (button({x, y, S(300), S(56)}, "HÉBERGER", true)) startHost(hostMode == 0 ? MODE_COMPETITIVE : MODE_DEATHMATCH);
        auto ips = sock::localAddresses();
        std::string s = "Vos adresses : ";
        for (size_t i = 0; i < ips.size() && i < 3; i++) s += (i ? ", " : "") + ips[i];
        text(s.c_str(), x + S(320), y + S(16), S(16), theme::dim);
        float jx = rx, jy = opt.y + S(30);
        text("REJOINDRE", jx, jy, S(24), theme::accent, true); jy += S(40);
        text("Adresse IP du serveur (ip ou ip:port)", jx, jy, S(16), theme::dim); jy += S(26);
        textField({jx, jy, S(500), S(46)}, joinAddr, sizeof joinAddr, 2, "127.0.0.1"); jy += S(64);
        if (button({jx, jy, S(500), S(56)}, "REJOINDRE", true)) startJoin();
        jy += S(80);
        text("Les joueurs qui rejoignent remplacent un bot.", jx, jy, S(16), theme::dim); jy += S(24);
        text("Ouvrez/redirigez le port UDP sur votre box pour jouer via Internet.", jx, jy, S(16), theme::dim);
    }
}

void App::drawConnect() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    Rectangle r = {W / 2 - S(320), H / 2 - S(150), S(640), S(300)};
    panel(r, theme::bg);
    DrawRectangle((int)r.x, (int)r.y, (int)r.width, (int)S(4), theme::accent);
    if (role == ROLE_CLIENT && connectError.empty()) {
        text("CONNEXION…", r.x + r.width / 2, r.y + S(50), S(36), theme::text, true, AL_CENTER);
        text(joinAddr, r.x + r.width / 2, r.y + S(110), S(20), theme::dim, false, AL_CENTER);
        float t = (float)GetTime();
        for (int i = 0; i < 3; i++) DrawCircle((int)(r.x + r.width / 2 - S(30) + i * S(30)), (int)(r.y + S(170)), S(6) * (0.6f + 0.4f * sinf(t * 6 + i)), theme::accent);
        if (button({r.x + r.width / 2 - S(120), r.y + r.height - S(80), S(240), S(54)}, "ANNULER")) { client.disconnect(); role = ROLE_OFFLINE; screen = SCR_PLAY; }
    } else {
        text("ÉCHEC DE CONNEXION", r.x + r.width / 2, r.y + S(50), S(32), theme::bad, true, AL_CENTER);
        text(connectError.c_str(), r.x + r.width / 2, r.y + S(110), S(20), theme::text, false, AL_CENTER);
        if (button({r.x + r.width / 2 - S(120), r.y + r.height - S(80), S(240), S(54)}, "RETOUR")) { connectError.clear(); screen = SCR_PLAY; playMode = 4; }
    }
}

void App::drawStats() {
    float W = (float)GetScreenWidth();
    text("CARRIÈRE", S(110), S(70), S(56), theme::text, true);
    if (button({W - S(260), S(80), S(160), S(50)}, "RETOUR")) screen = SCR_MAIN;
    const CareerStats& s = g_stats;
    struct Tile { const char* label; std::string value; Color c; };
    float kd = (float)s.kills / std::max(1, s.deaths);
    std::vector<Tile> tiles = {
        {"RANG", rankName(s.rating), rankColor(s.rating)}, {"POINTS", std::to_string(s.rating), theme::text},
        {"MATCHS", std::to_string(s.matches), theme::text}, {"VICTOIRES", std::to_string(s.wins), theme::good},
        {"DÉFAITES", std::to_string(s.losses), theme::bad}, {"RATIO V/D", fmt("%.0f%%", s.matches ? 100.f * s.wins / s.matches : 0.f), theme::text},
        {"ÉLIMINATIONS", std::to_string(s.kills), theme::text}, {"MORTS", std::to_string(s.deaths), theme::text},
        {"K/D", fmt("%.2f", kd), kd >= 1 ? theme::good : theme::bad}, {"ASSISTANCES", std::to_string(s.assists), theme::text},
        {"HEADSHOTS", fmt("%.0f%%", s.kills ? 100.f * s.headshots / s.kills : 0.f), theme::gold}, {"PRÉCISION", fmt("%.1f%%", s.shotsFired ? 100.f * s.shotsHit / s.shotsFired : 0.f), theme::text},
        {"ADR", fmt("%.0f", s.roundsPlayed ? (float)s.damage / s.roundsPlayed : 0.f), theme::text}, {"MVP", std::to_string(s.mvps), theme::gold},
        {"POSES", std::to_string(s.plants), theme::atk}, {"DÉSAMORÇAGES", std::to_string(s.defuses), theme::def},
        {"GRIDSHOT", std::to_string(s.aimGridBest), theme::accent}, {"FLICK", std::to_string(s.aimFlickBest), theme::accent},
        {"TRACKING", std::to_string(s.aimTrackBest), theme::accent}, {"RÉACTION", fmt("%.0f ms", s.aimBestReactionMs), theme::accent},
    };
    float tw = (W - S(220) - S(3 * 16)) / 4, th = S(120);
    for (size_t i = 0; i < tiles.size(); i++) {
        Rectangle r = {S(110) + (i % 4) * (tw + S(16)), S(170) + (i / 4) * (th + S(16)), tw, th};
        panel(r, theme::bg);
        text(tiles[i].label, r.x + S(22), r.y + S(20), S(16), theme::dim, true);
        text(tiles[i].value.c_str(), r.x + S(22), r.y + S(50), S(44), tiles[i].c, true);
    }
}

void App::drawMatchEnd() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    DrawRectangle(0, 0, (int)W, (int)H, Color{6, 9, 15, 200});
    bool won = localId >= 0 && world.matchWinnerSquad == world.players[localId].squad;
    if (world.mode == MODE_DEATHMATCH) {
        int best = -1, bk = -1;
        for (auto& p : world.players) if (p.active && p.kills > bk) { bk = p.kills; best = p.id; }
        won = best == localId;
    }
    textShadow(won ? "VICTOIRE" : "DÉFAITE", W / 2, S(40), S(96), won ? theme::good : theme::bad, true, AL_CENTER);
    if (world.mode == MODE_COMPETITIVE && localId >= 0) {
        int my = world.players[localId].squad;
        text(fmt("%d  -  %d", world.squadScore[my], world.squadScore[1 - my]), W / 2, S(150), S(48), theme::text, true, AL_CENTER);
        if (ratingDelta != 0)
            text(fmt("%s  %+d PTS  (%d)", rankName(g_stats.rating), ratingDelta, g_stats.rating), W / 2, S(210), S(22), ratingDelta > 0 ? theme::good : theme::bad, true, AL_CENTER);
    }
    hudScoreboard(world, localId);
    if (button({W / 2 - S(260), H - S(110), S(240), S(60)}, "MENU PRINCIPAL", true)) leaveMatch();
    if (button({W / 2 + S(20), H - S(110), S(240), S(60)}, "REJOUER")) startOffline(world.mode == MODE_DEATHMATCH ? MODE_DEATHMATCH : MODE_COMPETITIVE);
}

void App::drawSettings() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    text("PARAMÈTRES", S(110), S(50), S(48), theme::text, true);
    if (button({W - S(260), S(56), S(160), S(50)}, "RETOUR")) { screen = SCR_MAIN; g_settings.save(kConfigPath); }
    drawSettingsPanel({S(110), S(130), W - S(220), H - S(170)});
}

void App::drawSettingsPanel(Rectangle area) {
    panel(area, theme::bg);
    const char* tabs[] = {"JEU", "RÉTICULE", "VIDÉO", "AUDIO", "TOUCHES", "HUD", "MATCH"};
    float tw = S(150);
    for (int i = 0; i < 7; i++)
        if (tab({area.x + S(20) + i * tw, area.y + S(10), tw, S(50)}, tabs[i], settingsTab == i)) { settingsTab = i; settingsScroll = 0; }
    DrawRectangle((int)area.x, (int)(area.y + S(60)), (int)area.width, 1, theme::line);
    if (inGameSettings && button({area.x + area.width - S(180), area.y + S(10), S(160), S(44)}, "FERMER")) { inGameSettings = false; g_settings.save(kConfigPath); }
    Rectangle content = {area.x + S(20), area.y + S(80), area.width * 0.58f, area.height - S(100)};
    Rectangle side = {content.x + content.width + S(30), content.y, area.width - content.width - S(70), content.height};
    static float contentH = 1000;
    std::function<void()> sideFn;
    beginScroll(content, &settingsScroll, contentH);
    float x = content.x, y = content.y - settingsScroll, w = content.width - S(16), rh = S(48), gap = S(8);
    float y0 = y;
    auto R = [&]() { Rectangle r = {x, y, w, rh}; y += rh + gap; return r; };
    auto header = [&](const char* t) { y += S(10); text(t, x, y, S(16), theme::accent, true); y += S(30); };
    Settings& s = g_settings;
    switch (settingsTab) {
    case 0: {
        header("SOURIS");
        slider(R(), "Sensibilité", &s.sensitivity, 0.05f, 8.0f, "%.2f", 0.01f);
        slider(R(), "Ratio sensibilité zoomée", &s.zoomSensRatio, 0.2f, 2.0f, "%.2f", 0.01f);
        toggle(R(), "Inverser l'axe vertical", &s.invertY);
        toggle(R(), "Entrée brute (raw input)", &s.rawInput);
        header("CAMÉRA");
        slider(R(), "Champ de vision (horizontal 4:3)", &s.fov, 74, 110, "%.0f", 1);
        slider(R(), "Recul visuel de la caméra", &s.viewPunchScale, 0.0f, 1.5f, "%.2f", 0.05f);
        slider(R(), "Balancement de la vue", &s.viewBob, 0, 1, "%.2f", 0.05f);
        header("ARME EN MAIN");
        slider(R(), "FOV de l'arme", &s.viewmodelFov, 54, 90, "%.0f", 1);
        slider(R(), "Position X", &s.viewmodelX, -2.5f, 2.5f, "%.1f", 0.1f);
        slider(R(), "Position Y", &s.viewmodelY, -2.5f, 2.5f, "%.1f", 0.1f);
        slider(R(), "Position Z", &s.viewmodelZ, -2.5f, 2.5f, "%.1f", 0.1f);
        toggle(R(), "Main gauche", &s.viewmodelLeftHand);
        header("COMPORTEMENT");
        toggle(R(), "Maintenir pour s'accroupir", &s.holdToCrouch);
        toggle(R(), "Maintenir pour marcher", &s.holdToWalk);
        toggle(R(), "Équiper automatiquement les armes ramassées", &s.autoSwitchOnPickup);
        toggle(R(), "Afficher le point d'impact du recul", &s.showRecoilCrosshair);
        sideFn = [&s, side]() {
        float cm360 = 360.f / (s.sensitivity * s.mYaw * 800.f) * 2.54f;
        text("CALCULATEUR", side.x, side.y, S(16), theme::accent, true);
        text(fmt("%.1f cm / 360°", cm360), side.x, side.y + S(30), S(40), theme::text, true);
        text("à 800 DPI", side.x, side.y + S(80), S(16), theme::dim);
        text(fmt("eDPI 400 : %.0f   •   800 : %.0f   •   1600 : %.0f", s.sensitivity * 400, s.sensitivity * 800, s.sensitivity * 1600), side.x, side.y + S(110), S(16), theme::dim);
        text("Même échelle que les shooters compétitifs", side.x, side.y + S(150), S(15), theme::dim);
        text("du genre (0,022° par count) : importez votre", side.x, side.y + S(172), S(15), theme::dim);
        text("sensibilité habituelle telle quelle.", side.x, side.y + S(194), S(15), theme::dim);
        };
        break;
    }
    case 1: {
        static const char* styles[] = {"CLASSIQUE STATIQUE", "CLASSIQUE DYNAMIQUE", "POINT", "CERCLE"};
        header("FORME");
        choice(R(), "Style", &s.chStyle, styles, 4);
        slider(R(), "Longueur", &s.chSize, 0, 15, "%.1f", 0.5f);
        slider(R(), "Écart", &s.chGap, -4, 10, "%.1f", 0.5f);
        slider(R(), "Épaisseur", &s.chThickness, 0.5f, 6, "%.1f", 0.5f);
        toggle(R(), "Point central", &s.chDot);
        toggle(R(), "Style T (sans trait haut)", &s.chTStyle);
        toggle(R(), "Contour", &s.chOutline);
        slider(R(), "Épaisseur du contour", &s.chOutlineThickness, 1, 3, "%.0f", 1);
        toggle(R(), "Suit le recul", &s.chFollowRecoil);
        toggle(R(), "Lignes de lunette", &s.chSniperLines);
        header("COULEUR");
        sliderInt(R(), "Rouge", &s.chR, 0, 255);
        sliderInt(R(), "Vert", &s.chG, 0, 255);
        sliderInt(R(), "Bleu", &s.chB, 0, 255);
        sliderInt(R(), "Opacité", &s.chA, 0, 255);
        sideFn = [&s, side]() {
        Rectangle pv = {side.x, side.y, side.width, S(300)};
        int bgIdx = (int)(GetTime() / 3) % 3;
        Color bgs[3] = {{120, 128, 140, 255}, {196, 180, 150, 255}, {40, 46, 56, 255}};
        DrawRectangleRec(pv, bgs[bgIdx]);
        DrawRectangleRec({pv.x, pv.y + pv.height * 0.55f, pv.width, pv.height * 0.45f}, mixColor(bgs[bgIdx], BLACK, 0.2f));
        drawCrosshair(pv.x + pv.width / 2, pv.y + pv.height / 2, s.chStyle == 1 ? 4 * fabsf(sinf((float)GetTime() * 2)) * ui::scale() : 0, ui::scale());
        float py = pv.y + pv.height + S(20);
        Color presets[6] = {{0, 255, 120, 255}, {0, 230, 255, 255}, {255, 255, 0, 255}, {255, 0, 255, 255}, {255, 255, 255, 255}, {255, 40, 40, 255}};
        for (int i = 0; i < 6; i++) {
            Rectangle b = {side.x + i * S(54), py, S(46), S(46)};
            DrawRectangleRec(b, presets[i]);
            if (hovered(b)) DrawRectangleLinesEx(b, 2, WHITE);
            if (hovered(b) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { s.chR = presets[i].r; s.chG = presets[i].g; s.chB = presets[i].b; }
        }
        py += S(70);
        text("CODE DE PARTAGE", side.x, py, S(16), theme::accent, true); py += S(28);
        std::string code = s.crosshairCode();
        panel({side.x, py, side.width, S(44)}, Color{20, 26, 38, 255});
        text(code.c_str(), side.x + S(12), py + S(11), S(19), theme::text, true);
        py += S(56);
        if (button({side.x, py, S(200), S(46)}, "COPIER")) SetClipboardText(code.c_str());
        if (button({side.x + S(212), py, S(240), S(46)}, "IMPORTER (presse-papier)")) {
            const char* clip = GetClipboardText();
            if (clip) s.importCrosshairCode(clip);
        }
        };
        break;
    }
    case 2: {
        static const char* modes[] = {"FENÊTRÉ", "PLEIN ÉCRAN FENÊTRÉ", "PLEIN ÉCRAN"};
        static const int resList[][2] = {{1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}, {1280, 960}, {1440, 1080}, {1680, 1050}, {1920, 1200}};
        static const char* resNames[] = {"1280 x 720", "1366 x 768", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "1280 x 960 (4:3)", "1440 x 1080 (4:3)", "1680 x 1050", "1920 x 1200"};
        static const int fpsList[] = {0, 60, 120, 144, 165, 240, 300, 360, 500, 1000};
        static const char* fpsNames[] = {"ILLIMITÉ", "60", "120", "144", "165", "240", "300", "360", "500", "1000"};
        static const char* cb[] = {"AUCUN", "DEUTÉRANOPIE", "PROTANOPIE", "TRITANOPIE"};
        static const char* hl[] = {"AUCUNE", "COULEUR D'ÉQUIPE", "JAUNE VIF"};
        static const char* aa[] = {"DÉSACTIVÉ", "MSAA 4X"};
        header("AFFICHAGE");
        choice(R(), "Mode d'affichage", &s.displayMode, modes, 3);
        int ri = 3; for (int i = 0; i < 10; i++) if (resList[i][0] == s.resW && resList[i][1] == s.resH) ri = i;
        if (choice(R(), "Résolution", &ri, resNames, 10)) { s.resW = resList[ri][0]; s.resH = resList[ri][1]; }
        toggle(R(), "Synchronisation verticale", &s.vsync);
        int fi = 0; for (int i = 0; i < 10; i++) if (fpsList[i] == s.fpsMax) fi = i;
        if (choice(R(), "Limite d'images / s", &fi, fpsNames, 10)) { s.fpsMax = fpsList[fi]; SetTargetFPS(s.fpsMax); }
        choice(R(), "Anticrénelage (redémarrage)", &s.msaa, aa, 2);
        Rectangle ap = R();
        if (button({ap.x, ap.y, S(320), ap.height}, "APPLIQUER L'AFFICHAGE", true)) applyVideo(false);
        header("IMAGE");
        slider(R(), "Luminosité", &s.brightness, 0.6f, 1.6f, "%.2f", 0.01f);
        slider(R(), "Brouillard de distance", &s.renderDistanceFog, 0, 2, "%.2f", 0.05f);
        toggle(R(), "Lumière des tirs", &s.muzzleFlashLight);
        toggle(R(), "Traçantes", &s.bulletTracers);
        toggle(R(), "Particules d'impact", &s.impactParticles);
        header("LISIBILITÉ");
        choice(R(), "Mode daltonien", &s.colorblindMode, cb, 4);
        choice(R(), "Surbrillance des ennemis", &s.enemyHighlight, hl, 3);
        sideFn = [side]() {
        text("CONSEIL ESPORT", side.x, side.y, S(16), theme::accent, true);
        text("Désactivez la V-Sync et laissez les FPS", side.x, side.y + S(30), S(17), theme::text);
        text("illimités pour la latence d'entrée minimale.", side.x, side.y + S(54), S(17), theme::text);
        text("La simulation tourne à 128 tick quel que soit", side.x, side.y + S(94), S(17), theme::text);
        text("votre framerate : l'image est interpolée.", side.x, side.y + S(118), S(17), theme::text);
        text(fmt("Actuel : %d FPS  •  %dx%d", GetFPS(), GetScreenWidth(), GetScreenHeight()), side.x, side.y + S(160), S(17), theme::good, true);
        };
        break;
    }
    case 3: {
        header("VOLUME");
        if (slider(R(), "Volume général", &s.volMaster, 0, 1, "%.2f", 0.01f)) SetMasterVolume(s.volMaster);
        slider(R(), "Effets", &s.volEffects, 0, 1, "%.2f", 0.01f);
        slider(R(), "Pas", &s.volFootsteps, 0, 1.5f, "%.2f", 0.01f);
        slider(R(), "Interface", &s.volUI, 0, 1, "%.2f", 0.01f);
        slider(R(), "Annonces", &s.volAnnouncer, 0, 1, "%.2f", 0.01f);
        header("SPATIALISATION");
        toggle(R(), "Stéréo élargie (meilleur repérage)", &s.hrtfLike);
        toggle(R(), "Couper le son hors focus", &s.muteWhenUnfocused);
        Rectangle tb = R();
        if (button({tb.x, tb.y, S(160), tb.height}, "TEST GAUCHE")) { g_audio.setListener({0, 0, 0}, {0, 0, -1}, {1, 0, 0}); g_audio.play3D(SFX_STEP0, {-3, 0, 0}, 1, 3, 30, BUS_FOOTSTEPS); }
        if (button({tb.x + S(170), tb.y, S(160), tb.height}, "TEST DROITE")) { g_audio.setListener({0, 0, 0}, {0, 0, -1}, {1, 0, 0}); g_audio.play3D(SFX_STEP0, {3, 0, 0}, 1, 3, 30, BUS_FOOTSTEPS); }
        if (button({tb.x + S(340), tb.y, S(160), tb.height}, "TEST TIR")) g_audio.play2D(SFX_SHOT_RIFLE, 0.6f);
        sideFn = [side]() {
        text("Tous les sons sont synthétisés en temps réel.", side.x, side.y, S(17), theme::dim);
        text("Les pas sont audibles jusqu'à ~30 m ; marcher", side.x, side.y + S(28), S(17), theme::dim);
        text("ou s'accroupir rend silencieux.", side.x, side.y + S(52), S(17), theme::dim);
        };
        break;
    }
    case 4: {
        header("ACTIONS  (clic puis touche • Retour arrière = effacer • Échap = annuler)");
        for (int a = 0; a < ACT_COUNT; a++) {
            Rectangle r = R();
            panel(r, Color{18, 23, 34, 200});
            text(actionName(a), r.x + S(14), r.y + (r.height - S(19)) / 2, S(19), theme::text);
            keybind({r.x + r.width - S(380), r.y + S(6), S(180), r.height - S(12)}, &s.binds[a], a * 2);
            keybind({r.x + r.width - S(190), r.y + S(6), S(180), r.height - S(12)}, &s.binds2[a], a * 2 + 1);
        }
        Rectangle rb = R();
        if (button({rb.x, rb.y, S(320), rb.height}, "RÉINITIALISER LES TOUCHES")) s.resetBinds();
        sideFn = [side]() {
        text("Les touches suivent la position physique", side.x, side.y, S(17), theme::dim);
        text("du clavier : sur AZERTY, ZQSD par défaut.", side.x, side.y + S(26), S(17), theme::dim);
        };
        break;
    }
    case 5: {
        static const char* hudColors[] = {"BLANC", "CYAN", "VERT", "JAUNE", "ROSE"};
        static const Color hcv[] = {{230, 240, 255, 255}, {120, 230, 255, 255}, {120, 255, 160, 255}, {255, 220, 100, 255}, {255, 140, 200, 255}};
        header("HUD");
        slider(R(), "Échelle du HUD", &s.hudScale, 0.6f, 1.4f, "%.2f", 0.05f);
        slider(R(), "Opacité du HUD", &s.hudAlpha, 0.3f, 1.0f, "%.2f", 0.05f);
        int hc = 0; for (int i = 0; i < 5; i++) if (hcv[i].r == s.hudColorR && hcv[i].g == s.hudColorG && hcv[i].b == s.hudColorB) hc = i;
        if (choice(R(), "Couleur du HUD", &hc, hudColors, 5)) { s.hudColorR = hcv[hc].r; s.hudColorG = hcv[hc].g; s.hudColorB = hcv[hc].b; }
        toggle(R(), "Fil des éliminations", &s.showKillfeed);
        toggle(R(), "Hitmarkers", &s.showHitmarker);
        toggle(R(), "Chiffres de dégâts", &s.showDamageNumbers);
        header("RADAR");
        slider(R(), "Zoom du radar", &s.radarZoom, 0.4f, 2.0f, "%.2f", 0.05f);
        toggle(R(), "Radar rotatif", &s.radarRotate);
        toggle(R(), "Radar centré sur le joueur", &s.radarCentered);
        header("PERFORMANCES");
        toggle(R(), "Afficher les FPS", &s.showFps);
        toggle(R(), "Net graph (frametime, ping, perte)", &s.showNetGraph);
        break;
    }
    case 6: {
        static const char* diffs[] = {"RECRUE", "RÉGULIER", "VÉTÉRAN", "EXPERT", "ÉLITE"};
        header("PROFIL");
        Rectangle nr = R();
        text("Pseudo", nr.x + S(14), nr.y + S(14), S(19), theme::text);
        textField({nr.x + nr.width * 0.5f, nr.y + S(4), nr.width * 0.5f - S(10), nr.height - S(8)}, s.playerName, sizeof s.playerName, 77, "Pseudo");
        header("PARTIE CONTRE BOTS");
        choice(R(), "Difficulté des bots", &s.botDifficulty, diffs, 5);
        sliderInt(R(), "Joueurs par équipe", &s.teamSize, 1, 5);
        sliderInt(R(), "Rounds pour gagner", &s.roundsToWin, 3, 16);
        sliderInt(R(), "Durée d'un round (s)", &s.roundTime, 60, 150);
        sliderInt(R(), "Temps d'achat (s)", &s.buyTime, 5, 40);
        sliderInt(R(), "Temps de préparation (s)", &s.freezeTime, 0, 20);
        sliderInt(R(), "Argent de départ", &s.startMoney, 800, 16000);
        toggle(R(), "Tir allié", &s.friendlyFire);
        header("RÉSEAU");
        sliderInt(R(), "Port", &s.netPort, 1024, 65000);
        sliderInt(R(), "Interpolation (ms)", &s.interpMs, 15, 100);
        sliderInt(R(), "Fréquence des snapshots (Hz)", &s.rate, 32, 128);
        break;
    }
    }
    contentH = y - y0 + S(20);
    endScroll();
    if (sideFn) sideFn();
}

} // namespace ax

namespace ax {
// Automated smoke test: drives the UI and captures screenshots (used by CI / development)
void App::autotest(const char* test, int f) {
    std::string t = test;
    auto shot = [&](const char* name) { TakeScreenshot(name); };
    if (t == "menu") {
        if (f == 150) shot("t_menu.png");
        if (f == 151) { screen = SCR_PLAY; playMode = 0; }
        if (f == 160) shot("t_play.png");
        if (f == 161) { playMode = 4; }
        if (f == 170) shot("t_multi.png");
        if (f == 171) { screen = SCR_SETTINGS; settingsTab = 0; }
        for (int k = 0; k < 7; k++) {
            if (f == 180 + k * 10) settingsTab = k;
            if (f == 188 + k * 10) shot(fmt("t_settings_%d.png", k));
        }
        if (f == 260) screen = SCR_STATS;
        if (f == 268) shot("t_stats.png");
        if (f == 270) quit = true;
    } else if (t == "match") {
        if (f == 2) { startOffline(MODE_COMPETITIVE); world.players[localId].isBot = true; autopilot = true; }
        if (f == 200) { buyOpen = true; hud.buyCategory = 2; }
        if (f == 210) shot("t_buy.png");
        if (f == 211) buyOpen = false;
        if (f == 1290) forceScoreboard = true;
        if (f == 1295) { shot("t_scoreboard.png"); forceScoreboard = false; }
        if (f == 1300) shot("t_live1.png");
        if (f == 2200) shot("t_live2.png");
        if (f == 3000) shot("t_live3.png");
        if (f == 3001) { paused = true; }
        if (f == 3010) { shot("t_pause.png"); paused = false; }
        if (f == 3011) quit = true;
    } else if (t == "dm") {
        if (f == 2) { startOffline(MODE_DEATHMATCH); world.players[localId].isBot = true; autopilot = true; }
        if (f == 600) shot("t_dm1.png");
        if (f == 1200) shot("t_dm2.png");
        if (f == 1201) quit = true;
    } else if (t == "aim") {
        if (f == 2) { aimKind = 0; startOffline(MODE_AIM); }
        if (f == 300) shot("t_aim.png");
        if (f == 301) quit = true;
    } else if (t == "join") {
        if (f == 2) { snprintf(joinAddr, sizeof joinAddr, "127.0.0.1:27115"); startJoin(); autopilot = true; }
        if (screen == SCR_GAME && role == ROLE_CLIENT) viewYaw += 0.4f;
        if (f % 120 == 0 && localId >= 0 && screen == SCR_GAME) {
            const Player& me = world.players[localId];
            int act = 0; for (auto& p : world.players) if (p.active) act++;
            printf("[client f%d] id=%d alive=%d pos=(%.1f %.1f %.1f) hp=%d w=%s rtt=%dms loss=%.0f%% players=%d phase=%d round=%d score=%d-%d pending-ok\n",
                   f, localId, me.alive, me.pos.x, me.pos.y, me.pos.z, me.health, weaponDef(me.activeW).name, client.rttMs, client.lossPct, act,
                   world.phase, world.round, world.squadScore[0], world.squadScore[1]);
            fflush(stdout);
        }
        if (f == 1500) shot("t_client.png");
        if (f == 2400) shot("t_client2.png");
        if (f == 2401) quit = true;
    } else if (t == "practice") {
        if (f == 2) { startOffline(MODE_PRACTICE); }
        if (f == 60) { world.players[localId].nades[1] = 1; world.players[localId].activeW = W_SMOKE; viewPitch = 15; }
        if (f == 200) shot("t_practice.png");
        if (f == 201) quit = true;
    }
}
} // namespace ax
