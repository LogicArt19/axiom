// AXIOM dedicated server : headless, authoritative, 128 tick.
//   AxiomServer [--port 27115] [--mode comp|dm] [--bots 0-4] [--teamsize 1-5] [--rounds 13] [--name "AXIOM Server"]
#include "../src/world.h"
#include "../src/bot.h"
#include "../src/net.h"
#include "../src/config.h"
#include <thread>
#include <csignal>
#include <atomic>

#ifdef _WIN32
extern "C" __declspec(dllimport) unsigned int __stdcall timeBeginPeriod(unsigned int);
#endif

using namespace ax;

static std::atomic<bool> g_run{true};
static void onSignal(int) { g_run = false; }

int main(int argc, char** argv) {
    int port = 27115, skill = 2, teamSize = 5;
    GameMode mode = MODE_COMPETITIVE;
    g_settings.load("server.ini");
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--port") port = atoi(next().c_str());
        else if (a == "--mode") mode = next() == "dm" ? MODE_DEATHMATCH : MODE_COMPETITIVE;
        else if (a == "--bots") skill = atoi(next().c_str());
        else if (a == "--teamsize") teamSize = atoi(next().c_str());
        else if (a == "--rounds") g_settings.roundsToWin = atoi(next().c_str());
        else if (a == "--roundtime") g_settings.roundTime = atoi(next().c_str());
        else if (a == "--help" || a == "-h") {
            printf("AxiomServer --port N --mode comp|dm --bots 0-4 --teamsize 1-5 --rounds N --roundtime S\n");
            return 0;
        }
    }
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
#ifdef _WIN32
    timeBeginPeriod(1);
#endif
    initWeapons();
    g_settings.netPort = port;
    World w;
    BotSystem bots;
    NetServer server;
    w.init(mode);
    const char* names[] = {"Nova", "Kairo", "Vex", "Lumen", "Rook", "Sable", "Onyx", "Talon", "Echo", "Drift", "Zephyr", "Raze", "Quill", "Hex"};
    teamSize = std::clamp(teamSize, 1, 7);
    for (int i = 0; i < teamSize * 2; i++) w.addPlayer(names[i % 14], true, i % 2, skill);
    w.startMatch();
    if (!server.start(port)) { fprintf(stderr, "Impossible d'ouvrir le port UDP %d\n", port); return 1; }
    printf("AXIOM %s dedicated server | port %d | mode %s | bots %s | 128 tick\n", AXIOM_VERSION_STR, port,
           mode == MODE_COMPETITIVE ? "competitive" : "deathmatch", botSkillName(skill));
    for (auto& ip : sock::localAddresses()) printf("  adresse locale : %s:%d\n", ip.c_str(), port);
    fflush(stdout);
    size_t logShown = server.log.size();
    double next = nowSeconds();
    double lastStatus = 0;
    while (g_run) {
        double now = nowSeconds();
        if (now < next) {
            double wait = next - now;
            if (wait > 0.002) std::this_thread::sleep_for(std::chrono::microseconds((int)((wait - 0.001) * 1e6)));
            continue;
        }
        next += TICK_DT;
        if (now - next > 0.25) next = now;   // we fell behind: don't spiral
        server.poll(w);
        UserCmd cmds[MAX_PLAYERS];
        for (int i = 0; i < MAX_PLAYERS; i++) {
            cmds[i] = UserCmd{};
            Player& p = w.players[i];
            if (!p.active) continue;
            if (p.isBot) bots.think(w, i, cmds[i]);
            else if (p.isRemote) server.takeCmd(w, i, cmds[i]);
        }
        w.step(cmds);
        server.afterStep(w);
        server.recordEvents(w.events);
        int every = std::max(1, TICK_RATE / std::clamp(g_settings.rate, 16, 128));
        if (w.tick % every == 0) server.sendSnapshots(w);
        for (auto& e : w.events) {
            bots.onEvent(w, e);
            if (e.type == EV_ROUND_END)
                printf("[round %d] %s gagnent (%s) | score %d - %d\n", w.round, e.a == TEAM_ATK ? "ATTAQUANTS" : "DEFENSEURS",
                       reasonText((RoundEndReason)e.value, (Team)e.a), w.squadScore[0], w.squadScore[1]);
        }
        w.events.clear();
        while (logShown < server.log.size()) printf("%s\n", server.log[logShown++].c_str());
        if (w.phase == PH_MATCH_END) {
            printf("Fin du match : %d - %d. Nouveau match dans 10 s.\n", w.squadScore[0], w.squadScore[1]);
            fflush(stdout);
            for (int k = 0; k < 10 * TICK_RATE && g_run; k++) { server.poll(w); std::this_thread::sleep_for(std::chrono::milliseconds(8)); }
            w.startMatch();
            bots.reset();
        }
        if (now - lastStatus > 30) {
            lastStatus = now;
            printf("[status] tick %u | joueurs humains %d | round %d\n", w.tick, server.humanCount(), w.round);
            fflush(stdout);
        }
    }
    server.stop(w);
    printf("Serveur arrete.\n");
    return 0;
}
