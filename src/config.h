// config.h : settings, key bindings, career stats, persistence
#pragma once
#include "common.h"

namespace ax {

enum Action : int {
    ACT_ABILITY_Q, ACT_ABILITY_W, ACT_ABILITY_E, ACT_ABILITY_R,
    ACT_ATTACK_MOVE, ACT_STOP, ACT_HOLD, ACT_SELECT_HERO, ACT_SELECT_ARMY, ACT_IDLE_WORKER,
    ACT_CENTER_HERO, ACT_CAMERA_LOCK, ACT_SHOP, ACT_SCOREBOARD,
    ACT_CMD1, ACT_CMD2, ACT_CMD3, ACT_CMD4, ACT_CMD5, ACT_CMD6, ACT_CMD7, ACT_CANCEL,
    ACT_CAM_UP, ACT_CAM_DOWN, ACT_CAM_LEFT, ACT_CAM_RIGHT, ACT_CONSOLE,
    ACT_COUNT
};

constexpr int BIND_MOUSE_LEFT = -1, BIND_MOUSE_RIGHT = -2, BIND_MOUSE_MIDDLE = -3, BIND_MOUSE_X1 = -4, BIND_MOUSE_X2 = -5,
              BIND_WHEEL_UP = -6, BIND_WHEEL_DOWN = -7, BIND_NONE = 0;

const char* actionName(int a);
const char* bindName(int code);

struct Settings {
    // gameplay
    bool  quickCast = false;        // cast abilities at the cursor immediately
    bool  edgePan = true;
    float panSpeed = 1.0f;
    float zoomDefault = 1.0f;
    bool  cameraLockDefault = false;
    int   healthBars = 1;           // 0 off, 1 damaged only, 2 always
    bool  showRanges = true;
    bool  confirmCast = true;       // show range indicator while aiming
    bool  showApm = true;
    // video
    int   resW = 1920, resH = 1080;
    int   displayMode = 1;
    bool  vsync = false;
    int   fpsMax = 240;
    int   msaa = 1;
    float brightness = 1.0f;
    bool  shadows = true;
    bool  particles = true;
    int   colorblind = 0;
    // audio
    float volMaster = 0.8f, volEffects = 1.0f, volUI = 0.7f, volAnnouncer = 0.9f;
    bool  muteWhenUnfocused = true;
    // hud
    float hudScale = 1.0f;
    float minimapSize = 1.0f;
    bool  showFps = true;
    // match
    int   botDifficulty = 2;
    int   teamSize = 1;
    int   mapChoice = -1;           // -1 random
    int   heroChoice = 0;
    // network
    char  playerName[24] = "Commandant";
    char  lastServer[64] = "127.0.0.1";
    int   netPort = 27115;
    // keys
    int binds[ACT_COUNT];
    int binds2[ACT_COUNT];

    Settings();
    void resetBinds();
    void load(const char* path);
    void save(const char* path) const;
};
extern Settings g_settings;

struct CareerStats {
    int matches = 0, wins = 0, losses = 0, heroKills = 0, heroDeaths = 0, assists = 0, unitsKilled = 0, unitsProduced = 0;
    long long goldEarned = 0;
    int rating = 1000;
    int bestApm = 0;
    int heroWins[6] = {0, 0, 0, 0, 0, 0};
    int heroGames[6] = {0, 0, 0, 0, 0, 0};
    void load(const char* path);
    void save(const char* path) const;
};
extern CareerStats g_stats;

std::string userFile(const char* name);
bool settingSet(const std::string& key, const std::string& value);
std::string settingGet(const std::string& key);
std::vector<std::string> settingKeys();

bool actionDown(int a);
bool actionPressed(int a);
int  pollAnyBindInput();

} // namespace ax
