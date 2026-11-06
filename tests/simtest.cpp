// Headless AI vs AI match: AxiomSimTest [playersPerTeam] [skillA] [skillB] [mapId]
#include "../src/sim.h"
#include "../src/ai.h"
using namespace ax;
int main(int argc, char** argv) {
    initData();
    int ppt = argc > 1 ? atoi(argv[1]) : 1, sa = argc > 2 ? atoi(argv[2]) : 2, sb = argc > 3 ? atoi(argv[3]) : 2;
    World w; AISystem ai;
    int mapId = argc > 4 ? atoi(argv[4]) : 0;
    w.setup(ppt, mapId);
    printf("map %d %s\n", mapId, mapInfo(mapId).name);
    const char* heroes = getenv("HEROES");   // e.g. "012345" : hero index per player slot
    for (int i = 0; i < ppt * 2; i++) {
        int hi = heroes && (int)strlen(heroes) > i ? heroes[i] - '0' : (i * 5 + 1) % HERO_COUNT;
        w.addPlayer(i % 2 ? "B" : "A", i % 2, (UnitType)(HERO_FIRST + hi), true, i % 2 ? sb : sa);
    }
    w.lobbyEnd = 0;
    w.step();
    ai.reset(w);
    int lastMin = -1;
    while (w.phase != PH_OVER && w.time < 60 * 40) {
        ai.think(w);
        w.step();
        w.events.clear();
        int m = (int)(w.time / 60);
        if (m != lastMin && m % (getenv("EVERY") ? atoi(getenv("EVERY")) : 3) == 0) {
            lastMin = m;
            int hubs[2] = {0, 0}, ext[2] = {0, 0};
            for (auto& e : w.ents) { if (e.alive && e.type == B_HUB && !e.constructing) hubs[e.team]++; if (e.alive && e.type == B_EXTRACTOR && !e.constructing) ext[e.team]++; }
            printf("t=%2dmin hubs %d-%d ext %d-%d ents %zu |", m, hubs[0], hubs[1], ext[0], ext[1], w.ents.size());
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (!w.players[i].active) continue;
                auto& P = w.players[i];
                Entity& h = w.ents[P.heroId];
                int army = 0; for (auto& e : w.ents) if (e.alive && e.owner == i && e.kind == K_UNIT && e.type != U_DRONE) army++;
                printf(" P%d[%s] $%d f%d sup %d/%d dr%d army%d L%d %d/%d/%d up%d%d |", i, unitDef(P.hero).name, P.gold, P.flux, w.supplyUsed(i), w.supplyMax(i),
                       w.countUnits(i, U_DRONE, false), army, h.level, h.kills, h.deaths, h.assists, P.upgrades[0], P.upgrades[1]);
            }
            printf("\n");
        }
    }
    printf("RESULT winner=%d time=%.0fs ents=%zu\n", w.winner, w.time, w.ents.size());
    for (int i = 0; i < MAX_PLAYERS; i++) if (w.players[i].active) { auto& P = w.players[i]; printf("  P%d crystals %d flux %d produced %d killed %d lost %d apm %.0f\n", i, P.goldEarned, P.fluxEarned, P.unitsProduced, P.unitsKilled, P.unitsLost, P.actions / (w.time / 60)); }
}
