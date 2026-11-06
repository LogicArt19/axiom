#include "config.h"
#include <map>

extern "C" const char* GetKeyName(int key);

namespace ax {

Settings g_settings;
CareerStats g_stats;

static const char* kActionNames[ACT_COUNT] = {
    "Compétence Q", "Compétence W", "Compétence E", "Ultime R",
    "Attaque-déplacement", "Stop", "Tenir la position", "Sélectionner le héros", "Sélectionner l'armée", "Ouvrier inactif",
    "Centrer sur le héros", "Verrouiller la caméra", "Boutique", "Tableau des scores",
    "Commande 1", "Commande 2", "Commande 3", "Commande 4", "Commande 5", "Commande 6", "Commande 7", "Annuler (production)",
    "Caméra haut", "Caméra bas", "Caméra gauche", "Caméra droite", "Console"};
static const char* kActionIds[ACT_COUNT] = {
    "ab_q", "ab_w", "ab_e", "ab_r", "attack_move", "stop", "hold", "select_hero", "select_army", "idle_worker",
    "center_hero", "camera_lock", "shop", "scoreboard", "cmd1", "cmd2", "cmd3", "cmd4", "cmd5", "cmd6", "cmd7", "cancel",
    "cam_up", "cam_down", "cam_left", "cam_right", "console"};

const char* actionName(int a) { return (a >= 0 && a < ACT_COUNT) ? kActionNames[a] : "?"; }

const char* bindName(int code) {
    static char buf[32];
    switch (code) {
    case BIND_NONE: return "---";
    case BIND_MOUSE_LEFT: return "CLIC G";
    case BIND_MOUSE_RIGHT: return "CLIC D";
    case BIND_MOUSE_MIDDLE: return "CLIC MOLETTE";
    case BIND_MOUSE_X1: return "SOURIS 4";
    case BIND_MOUSE_X2: return "SOURIS 5";
    case BIND_WHEEL_UP: return "MOLETTE HAUT";
    case BIND_WHEEL_DOWN: return "MOLETTE BAS";
    case KEY_SPACE: return "ESPACE";
    case KEY_LEFT_SHIFT: return "MAJ";
    case KEY_LEFT_CONTROL: return "CTRL";
    case KEY_LEFT_ALT: return "ALT";
    case KEY_TAB: return "TAB";
    case KEY_ESCAPE: return "ÉCHAP";
    case KEY_ENTER: return "ENTRÉE";
    case KEY_BACKSPACE: return "RETOUR";
    case KEY_GRAVE: return "²";
    case KEY_UP: return "HAUT";
    case KEY_DOWN: return "BAS";
    case KEY_LEFT: return "GAUCHE";
    case KEY_RIGHT: return "DROITE";
    default: break;
    }
    if (code >= KEY_A && code <= KEY_Z) {
        const char* n = IsWindowReady() ? GetKeyName(code) : nullptr;
        char ch = (n && n[0] && n[1] == 0) ? n[0] : (char)('A' + (code - KEY_A));
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
        snprintf(buf, sizeof buf, "%c", ch);
        return buf;
    }
    if (code >= KEY_ZERO && code <= KEY_NINE) { snprintf(buf, sizeof buf, "%c", '0' + (code - KEY_ZERO)); return buf; }
    if (code >= KEY_F1 && code <= KEY_F12) { snprintf(buf, sizeof buf, "F%d", 1 + code - KEY_F1); return buf; }
    snprintf(buf, sizeof buf, "KEY%d", code);
    return buf;
}

Settings::Settings() { resetBinds(); }

void Settings::resetBinds() {
    for (int i = 0; i < ACT_COUNT; i++) { binds[i] = BIND_NONE; binds2[i] = BIND_NONE; }
    // physical positions (QWERTY scancodes): on AZERTY Q W -> A Z
    binds[ACT_ABILITY_Q] = KEY_Q; binds[ACT_ABILITY_W] = KEY_W; binds[ACT_ABILITY_E] = KEY_E; binds[ACT_ABILITY_R] = KEY_R;
    binds[ACT_ATTACK_MOVE] = KEY_A; binds[ACT_STOP] = KEY_S; binds[ACT_HOLD] = KEY_H;
    binds[ACT_SELECT_HERO] = KEY_F1; binds[ACT_SELECT_ARMY] = KEY_F2; binds[ACT_IDLE_WORKER] = KEY_F3;
    binds[ACT_CENTER_HERO] = KEY_SPACE; binds[ACT_CAMERA_LOCK] = KEY_Y; binds[ACT_SHOP] = KEY_P; binds[ACT_SCOREBOARD] = KEY_TAB;
    binds[ACT_CMD1] = KEY_Z; binds[ACT_CMD2] = KEY_X; binds[ACT_CMD3] = KEY_C; binds[ACT_CMD4] = KEY_V; binds[ACT_CMD5] = KEY_B; binds[ACT_CMD6] = KEY_N; binds[ACT_CMD7] = KEY_M;
    binds[ACT_CANCEL] = KEY_ESCAPE;
    binds[ACT_CAM_UP] = KEY_UP; binds[ACT_CAM_DOWN] = KEY_DOWN; binds[ACT_CAM_LEFT] = KEY_LEFT; binds[ACT_CAM_RIGHT] = KEY_RIGHT;
    binds[ACT_CONSOLE] = KEY_GRAVE;
}

enum VT { VT_F, VT_I, VT_B, VT_S };
struct Var { const char* key; VT t; void* p; int cap; };
static std::vector<Var> varsFor(Settings& s) {
    return {
        {"quick_cast", VT_B, &s.quickCast, 0}, {"edge_pan", VT_B, &s.edgePan, 0}, {"pan_speed", VT_F, &s.panSpeed, 0},
        {"zoom", VT_F, &s.zoomDefault, 0}, {"camera_lock", VT_B, &s.cameraLockDefault, 0}, {"health_bars", VT_I, &s.healthBars, 0},
        {"show_ranges", VT_B, &s.showRanges, 0}, {"confirm_cast", VT_B, &s.confirmCast, 0}, {"show_apm", VT_B, &s.showApm, 0},
        {"res_w", VT_I, &s.resW, 0}, {"res_h", VT_I, &s.resH, 0}, {"display_mode", VT_I, &s.displayMode, 0}, {"vsync", VT_B, &s.vsync, 0},
        {"fps_max", VT_I, &s.fpsMax, 0}, {"msaa", VT_I, &s.msaa, 0}, {"brightness", VT_F, &s.brightness, 0}, {"shadows", VT_B, &s.shadows, 0},
        {"particles", VT_B, &s.particles, 0}, {"colorblind", VT_I, &s.colorblind, 0},
        {"vol_master", VT_F, &s.volMaster, 0}, {"vol_effects", VT_F, &s.volEffects, 0}, {"vol_ui", VT_F, &s.volUI, 0},
        {"vol_announcer", VT_F, &s.volAnnouncer, 0}, {"mute_unfocused", VT_B, &s.muteWhenUnfocused, 0},
        {"hud_scale", VT_F, &s.hudScale, 0}, {"minimap_size", VT_F, &s.minimapSize, 0}, {"show_fps", VT_B, &s.showFps, 0},
        {"bot_difficulty", VT_I, &s.botDifficulty, 0}, {"team_size", VT_I, &s.teamSize, 0}, {"map", VT_I, &s.mapChoice, 0}, {"hero", VT_I, &s.heroChoice, 0},
        {"name", VT_S, s.playerName, (int)sizeof(s.playerName)}, {"last_server", VT_S, s.lastServer, (int)sizeof(s.lastServer)},
        {"net_port", VT_I, &s.netPort, 0},
    };
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

static void setVar(const Var& var, const std::string& v) {
    switch (var.t) {
    case VT_F: *(float*)var.p = (float)atof(v.c_str()); break;
    case VT_I: *(int*)var.p = atoi(v.c_str()); break;
    case VT_B: *(bool*)var.p = (v == "1" || v == "true"); break;
    case VT_S: snprintf((char*)var.p, var.cap, "%s", v.c_str()); break;
    }
}

void Settings::load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return;
    auto vars = varsFor(*this);
    char line[512];
    while (fgets(line, sizeof line, f)) {
        std::string l = trim(line);
        size_t eq = l.find('=');
        if (l.empty() || l[0] == '#' || eq == std::string::npos) continue;
        std::string k = trim(l.substr(0, eq)), v = trim(l.substr(eq + 1));
        if (k.rfind("bind.", 0) == 0 || k.rfind("bind2.", 0) == 0) {
            bool second = k[4] == '2';
            std::string id = k.substr(second ? 6 : 5);
            for (int i = 0; i < ACT_COUNT; i++) if (id == kActionIds[i]) (second ? binds2 : binds)[i] = atoi(v.c_str());
            continue;
        }
        for (auto& var : vars) if (k == var.key) setVar(var, v);
    }
    fclose(f);
}

void Settings::save(const char* path) const {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "# AXIOM configuration\n");
    for (auto& var : varsFor(const_cast<Settings&>(*this))) {
        switch (var.t) {
        case VT_F: fprintf(f, "%s=%.4f\n", var.key, *(float*)var.p); break;
        case VT_I: fprintf(f, "%s=%d\n", var.key, *(int*)var.p); break;
        case VT_B: fprintf(f, "%s=%d\n", var.key, *(bool*)var.p ? 1 : 0); break;
        case VT_S: fprintf(f, "%s=%s\n", var.key, (const char*)var.p); break;
        }
    }
    for (int i = 0; i < ACT_COUNT; i++) fprintf(f, "bind.%s=%d\nbind2.%s=%d\n", kActionIds[i], binds[i], kActionIds[i], binds2[i]);
    fclose(f);
}

bool settingSet(const std::string& key, const std::string& v) {
    for (auto& var : varsFor(g_settings)) if (key == var.key) { setVar(var, v); return true; }
    return false;
}
std::string settingGet(const std::string& key) {
    char b[96];
    for (auto& var : varsFor(g_settings)) {
        if (key != var.key) continue;
        switch (var.t) {
        case VT_F: snprintf(b, sizeof b, "%.4g", *(float*)var.p); break;
        case VT_I: snprintf(b, sizeof b, "%d", *(int*)var.p); break;
        case VT_B: snprintf(b, sizeof b, "%d", *(bool*)var.p ? 1 : 0); break;
        case VT_S: snprintf(b, sizeof b, "%s", (const char*)var.p); break;
        }
        return b;
    }
    return "";
}
std::vector<std::string> settingKeys() { std::vector<std::string> k; for (auto& v : varsFor(g_settings)) k.push_back(v.key); return k; }

std::string userFile(const char* name) {
    std::string dir;
#ifdef _WIN32
    const char* app = getenv("APPDATA");
    dir = app ? std::string(app) + "\\AXIOM" : std::string(".");
#else
    const char* xdg = getenv("XDG_DATA_HOME");
    const char* home = getenv("HOME");
    if (xdg && xdg[0]) dir = std::string(xdg) + "/axiom";
    else if (home) dir = std::string(home) + "/.local/share/axiom";
    else dir = ".";
#endif
    if (getenv("AXIOM_PORTABLE")) dir = ".";
    if (dir != "." && !DirectoryExists(dir.c_str())) MakeDirectory(dir.c_str());
#ifdef _WIN32
    return dir + "\\" + name;
#else
    return dir + "/" + name;
#endif
}

void CareerStats::load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return;
    std::map<std::string, std::string> kv;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        std::string l = trim(line);
        size_t eq = l.find('=');
        if (eq != std::string::npos) kv[l.substr(0, eq)] = l.substr(eq + 1);
    }
    fclose(f);
    auto gi = [&](const char* k, int& v) { if (kv.count(k)) v = atoi(kv[k].c_str()); };
    gi("matches", matches); gi("wins", wins); gi("losses", losses); gi("hero_kills", heroKills); gi("hero_deaths", heroDeaths);
    gi("assists", assists); gi("units_killed", unitsKilled); gi("units_produced", unitsProduced); gi("rating", rating); gi("best_apm", bestApm);
    if (kv.count("gold")) goldEarned = atoll(kv["gold"].c_str());
    for (int i = 0; i < 6; i++) {
        char k[32];
        snprintf(k, sizeof k, "hero_wins_%d", i); gi(k, heroWins[i]);
        snprintf(k, sizeof k, "hero_games_%d", i); gi(k, heroGames[i]);
    }
}
void CareerStats::save(const char* path) const {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "matches=%d\nwins=%d\nlosses=%d\nhero_kills=%d\nhero_deaths=%d\nassists=%d\nunits_killed=%d\nunits_produced=%d\nrating=%d\nbest_apm=%d\ngold=%lld\n",
            matches, wins, losses, heroKills, heroDeaths, assists, unitsKilled, unitsProduced, rating, bestApm, goldEarned);
    for (int i = 0; i < 6; i++) fprintf(f, "hero_wins_%d=%d\nhero_games_%d=%d\n", i, heroWins[i], i, heroGames[i]);
    fclose(f);
}

static bool codeDown(int c) {
    switch (c) {
    case BIND_NONE: return false;
    case BIND_MOUSE_LEFT: return IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    case BIND_MOUSE_RIGHT: return IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    case BIND_MOUSE_MIDDLE: return IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);
    case BIND_MOUSE_X1: return IsMouseButtonDown(MOUSE_BUTTON_SIDE);
    case BIND_MOUSE_X2: return IsMouseButtonDown(MOUSE_BUTTON_EXTRA);
    case BIND_WHEEL_UP: return GetMouseWheelMove() > 0;
    case BIND_WHEEL_DOWN: return GetMouseWheelMove() < 0;
    default: return IsKeyDown(c);
    }
}
static bool codePressed(int c) {
    switch (c) {
    case BIND_NONE: return false;
    case BIND_MOUSE_LEFT: return IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    case BIND_MOUSE_RIGHT: return IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    case BIND_MOUSE_MIDDLE: return IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE);
    case BIND_MOUSE_X1: return IsMouseButtonPressed(MOUSE_BUTTON_SIDE);
    case BIND_MOUSE_X2: return IsMouseButtonPressed(MOUSE_BUTTON_EXTRA);
    case BIND_WHEEL_UP: return GetMouseWheelMove() > 0;
    case BIND_WHEEL_DOWN: return GetMouseWheelMove() < 0;
    default: return IsKeyPressed(c);
    }
}
bool actionDown(int a) { return codeDown(g_settings.binds[a]) || codeDown(g_settings.binds2[a]); }
bool actionPressed(int a) { return codePressed(g_settings.binds[a]) || codePressed(g_settings.binds2[a]); }

int pollAnyBindInput() {
    int k = GetKeyPressed();
    if (k) return k;
    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) return BIND_MOUSE_MIDDLE;
    if (IsMouseButtonPressed(MOUSE_BUTTON_SIDE)) return BIND_MOUSE_X1;
    if (IsMouseButtonPressed(MOUSE_BUTTON_EXTRA)) return BIND_MOUSE_X2;
    float w = GetMouseWheelMove();
    if (w > 0) return BIND_WHEEL_UP;
    if (w < 0) return BIND_WHEEL_DOWN;
    return 0;
}

} // namespace ax
