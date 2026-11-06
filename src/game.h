// game.h : application, screens and the main loop
#pragma once
#include "world.h"
#include "bot.h"
#include "hud.h"
#include "net.h"

namespace ax {

enum Screen { SCR_MAIN, SCR_PLAY, SCR_SETTINGS, SCR_STATS, SCR_GAME, SCR_MATCHEND, SCR_CONNECT };
enum NetRole { ROLE_OFFLINE, ROLE_HOST, ROLE_CLIENT };

struct AimTarget { Vector3 pos, vel; float r; float spawn; bool alive; };

struct AimTrainer {
    int kind = 0;                 // 0 grid, 1 flick, 2 tracking
    bool running = false, finished = false;
    float start = 0, duration = 60;
    int hits = 0, shots = 0, score = 0;
    std::vector<float> reactions;
    std::vector<AimTarget> targets;
    Rng rng{55};
    Vector3 origin{}, fwd{0, 0, -1}, right{1, 0, 0};
};

class App {
public:
    void run();

private:
    World world;
    BotSystem bots;
    HudState hud;
    NetServer server;
    NetClient client;
    NetRole role = ROLE_OFFLINE;

    World menuWorld;
    BotSystem menuBots;
    float menuAcc = 0, menuCam = 0;
    bool menuReady = false;

    Screen screen = SCR_MAIN;
    Screen settingsReturn = SCR_MAIN;
    bool inGameSettings = false;
    int settingsTab = 0;
    float settingsScroll = 0;
    int playMode = 0;
    int aimKind = 0;

    int localId = -1;
    float acc = 0;
    float viewYaw = 0, viewPitch = 0;
    uint32_t pendingButtons = 0;
    uint8_t pendingSelect = 0;
    bool crouchToggled = false, walkToggled = false;
    bool paused = false, buyOpen = false, consoleOpen = false;
    int spectateId = -1;
    float deathTime = -10;
    int killerId = -1;
    bool cursorLocked = false;
    bool quit = false;
    bool statsCommitted = false;
    int ratingDelta = 0;
    float fireSoundTimer = 0;
    char joinAddr[64] = "127.0.0.1";
    std::string connectError;
    std::vector<Vector3> nadePath;
    AimTrainer aim;
    float gameTime = 0;
    bool autopilot = false;
    bool forceScoreboard = false;
    void autotest(const char* test, int frame);

    std::vector<std::string> conLog;
    char conInput[160] = "";

    void initWindow();
    void applyVideo(bool initial);
    void setCursor(bool locked);
    void startOffline(GameMode m);
    void startHost(GameMode m);
    void startJoin();
    void leaveMatch();
    void setupMatchPlayers(GameMode m);

    void frame(float dt);
    void updateGame(float dt);
    void localInput(float dt);
    void buildCmd(UserCmd& c);
    void processEvents(std::vector<GameEvent>& evs, const World& w);
    void drawGame(float dt);
    void drawPause();
    void drawConsole();
    void execCommand(const std::string& cmd);
    void commitMatchStats();
    void updateAim(float dt);
    void drawAimHud();

    void updateMenuWorld(float dt);
    void drawMenuBackground(float dt);
    void drawMainMenu();
    void drawPlayMenu();
    void drawSettings();
    void drawStats();
    void drawMatchEnd();
    void drawConnect();
    void drawSettingsPanel(Rectangle area);
};

} // namespace ax
