#include "config.h"
#include <map>

extern "C" const char* GetKeyName(int key);

namespace ax {

Settings g_settings;
CareerStats g_stats;

static const char* kActionNames[ACT_COUNT] = {
    "Avancer", "Reculer", "Gauche", "Droite", "Sauter", "S'accroupir", "Marcher (silencieux)",
    "Tirer", "Tir secondaire / Lunette", "Recharger", "Utiliser / Désamorcer", "Inspecter", "Lâcher l'arme",
    "Arme principale", "Arme secondaire", "Couteau", "Grenades", "Bombe", "Dernière arme",
    "Grenade suivante", "Flash", "Fumigène", "Frag", "Incendiaire",
    "Menu d'achat", "Tableau des scores", "Console", "Ping"
};
const char* actionName(int a) { return (a >= 0 && a < ACT_COUNT) ? kActionNames[a] : "?"; }

static const char* kActionIds[ACT_COUNT] = {
    "forward", "back", "left", "right", "jump", "crouch", "walk",
    "fire", "altfire", "reload", "use", "inspect", "drop",
    "slot1", "slot2", "slot3", "slot4", "slot5", "lastweapon",
    "nextnade", "flash", "smoke", "frag", "molly",
    "buymenu", "scoreboard", "console", "ping"
};

const char* bindName(int code) {
    static char buf[32];
    switch (code) {
    case BIND_NONE: return "---";
    case BIND_MOUSE_LEFT: return "MOUSE1";
    case BIND_MOUSE_RIGHT: return "MOUSE2";
    case BIND_MOUSE_MIDDLE: return "MOUSE3";
    case BIND_MOUSE_X1: return "MOUSE4";
    case BIND_MOUSE_X2: return "MOUSE5";
    case BIND_WHEEL_UP: return "MWHEELUP";
    case BIND_WHEEL_DOWN: return "MWHEELDOWN";
    case KEY_SPACE: return "SPACE";
    case KEY_LEFT_SHIFT: return "LSHIFT";
    case KEY_RIGHT_SHIFT: return "RSHIFT";
    case KEY_LEFT_CONTROL: return "LCTRL";
    case KEY_RIGHT_CONTROL: return "RCTRL";
    case KEY_LEFT_ALT: return "LALT";
    case KEY_TAB: return "TAB";
    case KEY_ESCAPE: return "ESC";
    case KEY_ENTER: return "ENTER";
    case KEY_BACKSPACE: return "BACKSPACE";
    case KEY_CAPS_LOCK: return "CAPSLOCK";
    case KEY_GRAVE: return "`";
    case KEY_UP: return "UP";
    case KEY_DOWN: return "DOWN";
    case KEY_LEFT: return "LEFT";
    case KEY_RIGHT: return "RIGHT";
    default: break;
    }
    if (code >= KEY_A && code <= KEY_Z) {
        const char* n = IsWindowReady() ? GetKeyName(code) : nullptr;   // layout aware (AZERTY shows Z/Q)
        char ch = (n && n[0] && n[1] == 0) ? n[0] : (char)('A' + (code - KEY_A));
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
        snprintf(buf, sizeof buf, "%c", ch);
        return buf;
    }
    if (code >= KEY_ZERO && code <= KEY_NINE) { snprintf(buf, sizeof buf, "%c", '0' + (code - KEY_ZERO)); return buf; }
    if (code >= KEY_F1 && code <= KEY_F12) { snprintf(buf, sizeof buf, "F%d", 1 + code - KEY_F1); return buf; }
    if (code >= KEY_KP_0 && code <= KEY_KP_9) { snprintf(buf, sizeof buf, "KP%d", code - KEY_KP_0); return buf; }
    snprintf(buf, sizeof buf, "KEY%d", code);
    return buf;
}

Settings::Settings() { resetBinds(); }

void Settings::resetBinds() {
    for (int i = 0; i < ACT_COUNT; i++) { binds[i] = BIND_NONE; binds2[i] = BIND_NONE; }
    // AZERTY-friendly defaults: ZQSD primary, WASD secondary (raylib keys follow US layout scancodes)
    binds[ACT_FORWARD] = KEY_W;  binds2[ACT_FORWARD] = KEY_UP;
    binds[ACT_BACK] = KEY_S;     binds2[ACT_BACK] = KEY_DOWN;
    binds[ACT_LEFT] = KEY_A;     binds2[ACT_LEFT] = KEY_LEFT;
    binds[ACT_RIGHT] = KEY_D;    binds2[ACT_RIGHT] = KEY_RIGHT;
    binds[ACT_JUMP] = KEY_SPACE; binds2[ACT_JUMP] = BIND_WHEEL_DOWN;
    binds[ACT_CROUCH] = KEY_LEFT_CONTROL;
    binds[ACT_WALK] = KEY_LEFT_SHIFT;
    binds[ACT_FIRE] = BIND_MOUSE_LEFT;
    binds[ACT_ALTFIRE] = BIND_MOUSE_RIGHT;
    binds[ACT_RELOAD] = KEY_R;
    binds[ACT_USE] = KEY_E;
    binds[ACT_INSPECT] = KEY_F;
    binds[ACT_DROP] = KEY_G;
    binds[ACT_SLOT1] = KEY_ONE;
    binds[ACT_SLOT2] = KEY_TWO;
    binds[ACT_SLOT3] = KEY_THREE;
    binds[ACT_SLOT4] = KEY_FOUR;
    binds[ACT_SLOT5] = KEY_FIVE;
    binds[ACT_LASTWEAPON] = KEY_Q;
    binds[ACT_NEXTNADE] = BIND_NONE;
    binds[ACT_FLASH] = KEY_C;
    binds[ACT_SMOKE] = KEY_X;
    binds[ACT_FRAG] = KEY_V;
    binds[ACT_MOLLY] = KEY_Z;
    binds[ACT_BUYMENU] = KEY_B;
    binds[ACT_SCOREBOARD] = KEY_TAB;
    binds[ACT_CONSOLE] = KEY_GRAVE;
    binds[ACT_PING] = BIND_MOUSE_MIDDLE;
}

// ---------------------------------------------------------------------------
// ini persistence through a small reflection table
// ---------------------------------------------------------------------------
enum VT { VT_F, VT_I, VT_B, VT_S };
struct Var { const char* key; VT t; void* p; int cap; };

static std::vector<Var> varsFor(Settings& s) {
    return {
        {"sensitivity", VT_F, &s.sensitivity, 0}, {"zoom_sens_ratio", VT_F, &s.zoomSensRatio, 0},
        {"m_yaw", VT_F, &s.mYaw, 0}, {"invert_y", VT_B, &s.invertY, 0}, {"raw_input", VT_B, &s.rawInput, 0},
        {"fov", VT_F, &s.fov, 0}, {"viewmodel_fov", VT_F, &s.viewmodelFov, 0},
        {"viewmodel_x", VT_F, &s.viewmodelX, 0}, {"viewmodel_y", VT_F, &s.viewmodelY, 0}, {"viewmodel_z", VT_F, &s.viewmodelZ, 0},
        {"view_bob", VT_F, &s.viewBob, 0}, {"viewmodel_lefthand", VT_B, &s.viewmodelLeftHand, 0},
        {"recoil_crosshair", VT_B, &s.showRecoilCrosshair, 0}, {"auto_switch", VT_B, &s.autoSwitchOnPickup, 0},
        {"hold_crouch", VT_B, &s.holdToCrouch, 0}, {"hold_walk", VT_B, &s.holdToWalk, 0},
        {"view_punch", VT_F, &s.viewPunchScale, 0},
        {"ch_style", VT_I, &s.chStyle, 0}, {"ch_size", VT_F, &s.chSize, 0}, {"ch_gap", VT_F, &s.chGap, 0},
        {"ch_thickness", VT_F, &s.chThickness, 0}, {"ch_dot", VT_B, &s.chDot, 0}, {"ch_outline", VT_B, &s.chOutline, 0},
        {"ch_tstyle", VT_B, &s.chTStyle, 0}, {"ch_outline_thickness", VT_F, &s.chOutlineThickness, 0},
        {"ch_r", VT_I, &s.chR, 0}, {"ch_g", VT_I, &s.chG, 0}, {"ch_b", VT_I, &s.chB, 0}, {"ch_a", VT_I, &s.chA, 0},
        {"ch_sniper_lines", VT_B, &s.chSniperLines, 0}, {"ch_follow_recoil", VT_B, &s.chFollowRecoil, 0},
        {"res_w", VT_I, &s.resW, 0}, {"res_h", VT_I, &s.resH, 0}, {"display_mode", VT_I, &s.displayMode, 0},
        {"vsync", VT_B, &s.vsync, 0}, {"fps_max", VT_I, &s.fpsMax, 0}, {"msaa", VT_I, &s.msaa, 0},
        {"brightness", VT_F, &s.brightness, 0}, {"fog", VT_F, &s.renderDistanceFog, 0},
        {"muzzle_light", VT_B, &s.muzzleFlashLight, 0}, {"tracers", VT_B, &s.bulletTracers, 0},
        {"impact_particles", VT_B, &s.impactParticles, 0}, {"colorblind", VT_I, &s.colorblindMode, 0},
        {"enemy_highlight", VT_I, &s.enemyHighlight, 0},
        {"vol_master", VT_F, &s.volMaster, 0}, {"vol_effects", VT_F, &s.volEffects, 0}, {"vol_ui", VT_F, &s.volUI, 0},
        {"vol_footsteps", VT_F, &s.volFootsteps, 0}, {"vol_announcer", VT_F, &s.volAnnouncer, 0},
        {"stereo_wide", VT_B, &s.hrtfLike, 0}, {"mute_unfocused", VT_B, &s.muteWhenUnfocused, 0},
        {"hud_scale", VT_F, &s.hudScale, 0}, {"radar_zoom", VT_F, &s.radarZoom, 0}, {"radar_rotate", VT_B, &s.radarRotate, 0},
        {"radar_centered", VT_B, &s.radarCentered, 0}, {"show_fps", VT_B, &s.showFps, 0}, {"net_graph", VT_B, &s.showNetGraph, 0},
        {"damage_numbers", VT_B, &s.showDamageNumbers, 0}, {"killfeed", VT_B, &s.showKillfeed, 0}, {"hitmarker", VT_B, &s.showHitmarker, 0},
        {"hud_r", VT_I, &s.hudColorR, 0}, {"hud_g", VT_I, &s.hudColorG, 0}, {"hud_b", VT_I, &s.hudColorB, 0}, {"hud_alpha", VT_F, &s.hudAlpha, 0},
        {"bot_difficulty", VT_I, &s.botDifficulty, 0}, {"team_size", VT_I, &s.teamSize, 0}, {"rounds_to_win", VT_I, &s.roundsToWin, 0},
        {"round_time", VT_I, &s.roundTime, 0}, {"buy_time", VT_I, &s.buyTime, 0}, {"freeze_time", VT_I, &s.freezeTime, 0},
        {"start_money", VT_I, &s.startMoney, 0}, {"friendly_fire", VT_B, &s.friendlyFire, 0},
        {"dm_time", VT_I, &s.dmTimeLimit, 0}, {"dm_hs_only", VT_B, &s.dmHeadshotOnly, 0},
        {"name", VT_S, s.playerName, (int)sizeof(s.playerName)}, {"last_server", VT_S, s.lastServer, (int)sizeof(s.lastServer)},
        {"net_port", VT_I, &s.netPort, 0}, {"interp_ms", VT_I, &s.interpMs, 0}, {"rate", VT_I, &s.rate, 0},
    };
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

void Settings::load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return;
    auto vars = varsFor(*this);
    char line[512];
    while (fgets(line, sizeof line, f)) {
        std::string l = trim(line);
        if (l.empty() || l[0] == '#' || l[0] == ';' || l[0] == '[') continue;
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(l.substr(0, eq)), v = trim(l.substr(eq + 1));
        if (k.rfind("bind.", 0) == 0 || k.rfind("bind2.", 0) == 0) {
            bool second = k[4] == '2';
            std::string id = k.substr(second ? 6 : 5);
            for (int i = 0; i < ACT_COUNT; i++)
                if (id == kActionIds[i]) (second ? binds2 : binds)[i] = atoi(v.c_str());
            continue;
        }
        for (auto& var : vars) {
            if (k != var.key) continue;
            switch (var.t) {
            case VT_F: *(float*)var.p = (float)atof(v.c_str()); break;
            case VT_I: *(int*)var.p = atoi(v.c_str()); break;
            case VT_B: *(bool*)var.p = (v == "1" || v == "true"); break;
            case VT_S: snprintf((char*)var.p, var.cap, "%s", v.c_str()); break;
            }
        }
    }
    fclose(f);
}

void Settings::save(const char* path) const {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "# AXIOM configuration - generated by the game. Safe to edit while the game is closed.\n[settings]\n");
    auto vars = varsFor(const_cast<Settings&>(*this));
    for (auto& var : vars) {
        switch (var.t) {
        case VT_F: fprintf(f, "%s=%.4f\n", var.key, *(float*)var.p); break;
        case VT_I: fprintf(f, "%s=%d\n", var.key, *(int*)var.p); break;
        case VT_B: fprintf(f, "%s=%d\n", var.key, *(bool*)var.p ? 1 : 0); break;
        case VT_S: fprintf(f, "%s=%s\n", var.key, (const char*)var.p); break;
        }
    }
    fprintf(f, "[binds]\n");
    for (int i = 0; i < ACT_COUNT; i++) {
        fprintf(f, "bind.%s=%d\n", kActionIds[i], binds[i]);
        fprintf(f, "bind2.%s=%d\n", kActionIds[i], binds2[i]);
    }
    fclose(f);
}

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

bool settingSet(const std::string& key, const std::string& v) {
    auto vars = varsFor(g_settings);
    for (auto& var : vars) {
        if (key != var.key) continue;
        switch (var.t) {
        case VT_F: *(float*)var.p = (float)atof(v.c_str()); break;
        case VT_I: *(int*)var.p = atoi(v.c_str()); break;
        case VT_B: *(bool*)var.p = (v == "1" || v == "true"); break;
        case VT_S: snprintf((char*)var.p, var.cap, "%s", v.c_str()); break;
        }
        return true;
    }
    return false;
}
std::string settingGet(const std::string& key) {
    auto vars = varsFor(g_settings);
    char b[96];
    for (auto& var : vars) {
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
std::vector<std::string> settingKeys() {
    std::vector<std::string> k;
    for (auto& v : varsFor(g_settings)) k.push_back(v.key);
    return k;
}

// Crosshair share code: "AX-" + hex of packed values
std::string Settings::crosshairCode() const {
    char buf[96];
    snprintf(buf, sizeof buf, "AX-%01X%02X%02X%02X%01X%01X%01X%01X%02X%02X%02X%02X%01X",
             chStyle & 0xF, (int)(chSize * 4) & 0xFF, (int)((chGap + 10) * 4) & 0xFF, (int)(chThickness * 4) & 0xFF,
             chDot, chOutline, chTStyle, (int)chOutlineThickness & 0xF, chR & 0xFF, chG & 0xFF, chB & 0xFF, chA & 0xFF,
             chSniperLines);
    return buf;
}
bool Settings::importCrosshairCode(const std::string& code) {
    if (code.size() < 3 + 1 + 2 + 2 + 2 + 4 + 8 + 1 || code.rfind("AX-", 0) != 0) return false;
    const char* p = code.c_str() + 3;
    auto hx = [&](int n) { char t[4] = {0}; memcpy(t, p, n); p += n; return (int)strtol(t, nullptr, 16); };
    chStyle = hx(1); chSize = hx(2) / 4.f; chGap = hx(2) / 4.f - 10; chThickness = hx(2) / 4.f;
    chDot = hx(1); chOutline = hx(1); chTStyle = hx(1); chOutlineThickness = (float)hx(1);
    chR = hx(2); chG = hx(2); chB = hx(2); chA = hx(2); chSniperLines = hx(1);
    return true;
}

// ---------------------------------------------------------------------------
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
    gi("matches", matches); gi("wins", wins); gi("losses", losses); gi("kills", kills); gi("deaths", deaths);
    gi("assists", assists); gi("headshots", headshots); gi("shots_fired", shotsFired); gi("shots_hit", shotsHit);
    gi("rounds", roundsPlayed); gi("mvps", mvps); gi("plants", plants); gi("defuses", defuses);
    gi("aim_grid", aimGridBest); gi("aim_flick", aimFlickBest); gi("aim_track", aimTrackBest); gi("rating", rating);
    if (kv.count("damage")) damage = atoll(kv["damage"].c_str());
    if (kv.count("aim_reaction")) aimBestReactionMs = (float)atof(kv["aim_reaction"].c_str());
}
void CareerStats::save(const char* path) const {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "matches=%d\nwins=%d\nlosses=%d\nkills=%d\ndeaths=%d\nassists=%d\nheadshots=%d\nshots_fired=%d\nshots_hit=%d\n"
               "rounds=%d\nmvps=%d\nplants=%d\ndefuses=%d\ndamage=%lld\naim_grid=%d\naim_flick=%d\naim_track=%d\naim_reaction=%.1f\nrating=%d\n",
            matches, wins, losses, kills, deaths, assists, headshots, shotsFired, shotsHit, roundsPlayed, mvps, plants, defuses,
            damage, aimGridBest, aimFlickBest, aimTrackBest, aimBestReactionMs, rating);
    fclose(f);
}

// ---------------------------------------------------------------------------
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
static bool codeReleased(int c) {
    switch (c) {
    case BIND_NONE: return false;
    case BIND_MOUSE_LEFT: return IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    case BIND_MOUSE_RIGHT: return IsMouseButtonReleased(MOUSE_BUTTON_RIGHT);
    case BIND_MOUSE_MIDDLE: return IsMouseButtonReleased(MOUSE_BUTTON_MIDDLE);
    case BIND_MOUSE_X1: return IsMouseButtonReleased(MOUSE_BUTTON_SIDE);
    case BIND_MOUSE_X2: return IsMouseButtonReleased(MOUSE_BUTTON_EXTRA);
    case BIND_WHEEL_UP: case BIND_WHEEL_DOWN: return false;
    default: return IsKeyReleased(c);
    }
}
bool actionDown(int a) { return codeDown(g_settings.binds[a]) || codeDown(g_settings.binds2[a]); }
bool actionPressed(int a) { return codePressed(g_settings.binds[a]) || codePressed(g_settings.binds2[a]); }
bool actionReleased(int a) { return codeReleased(g_settings.binds[a]) || codeReleased(g_settings.binds2[a]); }

int pollAnyBindInput() {
    int k = GetKeyPressed();
    if (k) return k;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return BIND_MOUSE_LEFT;
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) return BIND_MOUSE_RIGHT;
    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) return BIND_MOUSE_MIDDLE;
    if (IsMouseButtonPressed(MOUSE_BUTTON_SIDE)) return BIND_MOUSE_X1;
    if (IsMouseButtonPressed(MOUSE_BUTTON_EXTRA)) return BIND_MOUSE_X2;
    float w = GetMouseWheelMove();
    if (w > 0) return BIND_WHEEL_UP;
    if (w < 0) return BIND_WHEEL_DOWN;
    return 0;
}

} // namespace ax
