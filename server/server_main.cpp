// AxiomServer : dedicated headless server. Empty slots are played by the AI,
// humans who join replace them. Usage: AxiomServer [--port 27115] [--size 1..3] [--bots 0..4] [--lobby 45] [--map 0..3]
#include "../src/sim.h"
#include "../src/ai.h"
#include "../src/net.h"
#include <thread>
#include <chrono>
#include <cstring>

using namespace ax;

int main(int argc, char** argv) {
    int port = 27115, size = 3, bots = 2, mapArg = -1;
    float lobby = 45;
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--port")) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--size")) size = std::clamp(atoi(argv[++i]), 1, 3);
        else if (!strcmp(argv[i], "--bots")) bots = std::clamp(atoi(argv[++i]), 0, 4);
        else if (!strcmp(argv[i], "--lobby")) lobby = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--map")) mapArg = std::clamp(atoi(argv[++i]), -1, MAP_COUNT - 1);
    }
    SetTraceLogLevel(LOG_WARNING);
    initData();
    printf("AXIOM server %s  -  port UDP %d  -  %dv%d  -  IA niveau %d\n", AXIOM_VERSION_STR, port, size, size, bots);
    fflush(stdout);
    for (;;) {
        World w;
        AISystem ai;
        NetServer srv;
        static int rotation = 0;
        int mapId = mapArg >= 0 ? mapArg : (rotation++ % MAP_COUNT);
        w.setup(size, mapId);
        printf("Carte : %s\n", mapInfo(mapId).name);
        const char* names[] = {"Atlas", "Nyx", "Orion", "Vesper", "Kael", "Lyra"};
        for (int i = 0; i < size * 2; i++) w.addPlayer(names[i], i % 2, (UnitType)(HERO_FIRST + i % 6), true, bots);
        w.lobbyEnd = 1e9f;
        if (!srv.start(port)) { fprintf(stderr, "Impossible d'ouvrir le port %d\n", port); return 1; }
        size_t logged = 0;
        bool countdown = false;
        float overAt = -1;
        auto next = std::chrono::steady_clock::now();
        for (;;) {
            srv.poll(w);
            if (w.phase == PH_LOBBY) {
                int humans = srv.humanCount();
                if (humans > 0 && !countdown) { countdown = true; w.lobbyEnd = w.time + lobby; printf("Lancement dans %.0f s (ou quand tous sont prêts)\n", lobby); }
                if (humans == 0 && countdown) { countdown = false; w.lobbyEnd = 1e9f; }
            }
            bool wasLobby = w.phase == PH_LOBBY;
            if (w.phase == PH_PLAYING) ai.think(w);
            w.step();
            if (wasLobby && w.phase == PH_PLAYING) { ai.reset(w); printf("Partie lancée\n"); }
            srv.recordEvents(w.events);
            w.events.clear();
            if (w.tick % 2 == 0) srv.send(w);
            while (logged < srv.log.size()) { printf("%s\n", srv.log[logged++].c_str()); fflush(stdout); }
            if (w.phase == PH_OVER && overAt < 0) { overAt = w.time; printf("Fin de partie : victoire équipe %d (%.0f s)\n", w.winner, w.time); }
            if (w.phase == PH_PLAYING && srv.humanCount() == 0 && w.time > 60) { printf("Plus aucun joueur : nouvelle partie\n"); break; }
            if (overAt >= 0 && w.time - overAt > 20) break;
            next += std::chrono::microseconds(1000000 / TICK_RATE);
            std::this_thread::sleep_until(next);
            if (std::chrono::steady_clock::now() > next + std::chrono::seconds(1)) next = std::chrono::steady_clock::now();
        }
        srv.stop(w);
    }
}
