// game.h : application, screens, RTS input and the main loop
#pragma once
#include "sim.h"
#include "ai.h"
#include "hud.h"
#include "net.h"
#include "render.h"

namespace ax {

enum Screen { SCR_MAIN, SCR_PLAY, SCR_MULTI, SCR_SETTINGS, SCR_STATS, SCR_GAME, SCR_END, SCR_CONNECT, SCR_LOBBY };
enum NetRole { ROLE_OFFLINE, ROLE_HOST, ROLE_CLIENT };
enum InputMode { M_NONE, M_ATTACK_MOVE, M_ABILITY, M_PLACE };

class App {
public:
    void run();

private:
    World world;
    AISystem ai;
    HudState hud;
    NetServer server;
    NetClient client;
    NetRole role = ROLE_OFFLINE;
    int player = 0;

    World menuWorld;
    AISystem menuAI;
    float menuAcc = 0, menuCam = 0;
    Vector2 menuFocus{100, 100};

    Screen screen = SCR_MAIN;
    int settingsTab = 0;
    float settingsScroll = 0;
    bool inGameSettings = false, paused = false, consoleOpen = false, showHelp = false;
    bool quit = false;
    float acc = 0, gameClock = 0, endTimer = -1;
    bool statsCommitted = false;
    int ratingDelta = 0;
    std::string connectError;
    char joinAddr[64] = "127.0.0.1";

    // camera
    Vector2 camTarget{40, 160};
    float camZoom = 1.0f;
    bool camLock = false;
    Camera3D cam{};

    // selection & input
    std::vector<uint16_t> selection;
    std::vector<uint16_t> groups[10];
    float groupTap[10] = {0};
    InputMode mode = M_NONE;
    int modeArg = 0;
    bool dragging = false;
    Vector2 dragStart{};
    double lastClick = 0;
    uint16_t lastClickId = 0;
    std::vector<double> actionTimes;
    int idleCycle = 0;
    std::vector<std::string> conLog;
    char conInput[128] = "";

    void initWindow();
    void applyVideo(bool initial);
    void startOffline();
    void startHost();
    void startJoin();
    void leaveMatch();
    void frame(float dt);

    void updateGame(float dt);
    void handleInput(float dt);
    void issue(Command c);
    Vector2 mouseGround() const;
    uint16_t pickEntity(Vector2 screen) const;
    std::vector<CardButton> buildCard(std::vector<std::function<void()>>& actions);
    void selectOwnedOnly();
    void processEvents(const std::vector<GameEvent>& evs);
    void updateCamera(float dt);
    void drawGame(float dt);
    void drawPause();
    void drawConsole();
    void execCommand(const std::string& s);
    void commitStats();
    void castAbility(int slot, bool atCursor);

    void tickMenuWorld(float dt);
    void drawMenuBackground(float dt);
    void drawMainMenu();
    void drawPlayMenu();
    void drawMultiMenu();
    void drawLobby();
    void drawSettings(Rectangle area);
    void drawStats();
    void drawEnd();
    void drawConnect();
    void drawHelp();
    void autotest(const char* t, int f);
};

} // namespace ax
