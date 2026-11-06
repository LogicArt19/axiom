// config.h : all user settings, key bindings and persistence (config.ini)
#pragma once
#include "common.h"

namespace ax {

enum Action : int {
    ACT_FORWARD, ACT_BACK, ACT_LEFT, ACT_RIGHT, ACT_JUMP, ACT_CROUCH, ACT_WALK,
    ACT_FIRE, ACT_ALTFIRE, ACT_RELOAD, ACT_USE, ACT_INSPECT, ACT_DROP,
    ACT_SLOT1, ACT_SLOT2, ACT_SLOT3, ACT_SLOT4, ACT_SLOT5, ACT_LASTWEAPON,
    ACT_NEXTNADE, ACT_FLASH, ACT_SMOKE, ACT_FRAG, ACT_MOLLY,
    ACT_BUYMENU, ACT_SCOREBOARD, ACT_CONSOLE, ACT_PING,
    ACT_COUNT
};

// Special bindings for mouse buttons are encoded as negative numbers
constexpr int BIND_MOUSE_LEFT = -1, BIND_MOUSE_RIGHT = -2, BIND_MOUSE_MIDDLE = -3,
              BIND_MOUSE_X1 = -4, BIND_MOUSE_X2 = -5, BIND_WHEEL_UP = -6, BIND_WHEEL_DOWN = -7, BIND_NONE = 0;

const char* actionName(int a);
const char* bindName(int code);

struct Settings {
    // ---- Gameplay / mouse
    float sensitivity = 1.20f;     // CS-style (yaw = 0.022 deg/count)
    float zoomSensRatio = 1.00f;
    float mYaw = 0.022f;
    bool  invertY = false;
    bool  rawInput = true;
    float fov = 90.f;              // horizontal FOV (4:3 equivalent like CS)
    float viewmodelFov = 68.f;
    float viewmodelX = 1.0f, viewmodelY = 1.0f, viewmodelZ = -1.0f;
    float viewBob = 1.0f;
    bool  viewmodelLeftHand = false;
    bool  showRecoilCrosshair = false;
    bool  autoSwitchOnPickup = true;
    bool  holdToCrouch = true;
    bool  holdToWalk = true;
    bool  toggleScopeOnAlt = true;
    float viewPunchScale = 1.0f;   // visual camera recoil scale (0..1.5)

    // ---- Crosshair
    int   chStyle = 0;             // 0 classic static, 1 classic dynamic, 2 dot only, 3 circle
    float chSize = 5.f, chGap = 1.f, chThickness = 1.f;
    bool  chDot = false, chOutline = true, chTStyle = false;
    float chOutlineThickness = 1.f;
    int   chR = 0, chG = 255, chB = 120, chA = 255;
    bool  chSniperLines = true;
    bool  chFollowRecoil = false;

    // ---- Video
    int   resW = 1920, resH = 1080;
    int   displayMode = 1;         // 0 windowed, 1 borderless fullscreen, 2 exclusive fullscreen
    bool  vsync = false;
    int   fpsMax = 0;              // 0 = unlimited
    int   msaa = 1;                // 0 off / 1 4x (requires restart)
    float brightness = 1.0f;
    float renderDistanceFog = 1.0f;
    bool  muzzleFlashLight = true;
    bool  bulletTracers = true;
    bool  impactParticles = true;
    int   colorblindMode = 0;      // 0 none,1 deuteranopia,2 protanopia,3 tritanopia
    int   enemyHighlight = 1;      // 0 none, 1 team color, 2 bright yellow

    // ---- Audio
    float volMaster = 0.8f, volEffects = 1.0f, volUI = 0.7f, volFootsteps = 1.0f, volAnnouncer = 0.9f;
    bool  hrtfLike = true;         // stronger stereo separation
    bool  muteWhenUnfocused = true;

    // ---- HUD
    float hudScale = 1.0f;
    float radarZoom = 1.0f;
    bool  radarRotate = true;
    bool  radarCentered = true;
    bool  showFps = true;
    bool  showNetGraph = false;
    bool  showDamageNumbers = false;
    bool  showKillfeed = true;
    bool  showHitmarker = true;
    int   hudColorR = 230, hudColorG = 240, hudColorB = 255;
    float hudAlpha = 0.95f;

    // ---- Match / bots
    int   botDifficulty = 2;       // 0 easy .. 4 elite
    int   teamSize = 5;
    int   roundsToWin = 13;
    int   roundTime = 100;
    int   buyTime = 20;
    int   freezeTime = 10;
    int   startMoney = 800;
    bool  friendlyFire = false;
    int   dmTimeLimit = 10;        // minutes
    bool  dmHeadshotOnly = false;

    // ---- Network
    char  playerName[32] = "Player";
    char  lastServer[64] = "127.0.0.1";
    int   netPort = 27115;
    int   interpMs = 31;           // ~ 2 snapshots at 64Hz
    int   rate = 64;               // snapshot rate (server)

    // ---- Keys
    int binds[ACT_COUNT];
    int binds2[ACT_COUNT];

    Settings();
    void resetBinds();
    void load(const char* path);
    void save(const char* path) const;

    Color crosshairColor() const { return Color{(unsigned char)chR, (unsigned char)chG, (unsigned char)chB, (unsigned char)chA}; }
    Color hudColor() const { return Color{(unsigned char)hudColorR, (unsigned char)hudColorG, (unsigned char)hudColorB, 255}; }
    std::string crosshairCode() const;
    bool importCrosshairCode(const std::string& code);
};

extern Settings g_settings;

// Persistent career statistics
struct CareerStats {
    int matches = 0, wins = 0, losses = 0, kills = 0, deaths = 0, assists = 0, headshots = 0;
    int shotsFired = 0, shotsHit = 0, roundsPlayed = 0, mvps = 0, plants = 0, defuses = 0;
    long long damage = 0;
    int aimGridBest = 0, aimFlickBest = 0, aimTrackBest = 0;
    float aimBestReactionMs = 0;
    int rating = 1000;             // Elo-like skill rating vs bots
    void load(const char* path);
    void save(const char* path) const;
};
extern CareerStats g_stats;

// Per-user data folder (%APPDATA%/AXIOM or ~/.local/share/axiom) so updates never touch user files
std::string userFile(const char* name);

bool settingSet(const std::string& key, const std::string& value);
std::string settingGet(const std::string& key);
std::vector<std::string> settingKeys();

// Input helpers working with bindings ---------------------------------------
bool actionDown(int a);
bool actionPressed(int a);
bool actionReleased(int a);
int  pollAnyBindInput();          // returns a bind code if something was pressed this frame, else 0

} // namespace ax
