// Headless bot-vs-bot match to validate the simulation
#include "../src/world.h"
#include "../src/bot.h"
#include "../src/config.h"
using namespace ax;
int main(int argc, char** argv) {
    initWeapons();
    int skillA = argc > 1 ? atoi(argv[1]) : 2, skillB = argc > 2 ? atoi(argv[2]) : 2;
    World w; BotSystem bots;
    w.init(MODE_COMPETITIVE);
    for (int i = 0; i < 5; i++) { char n[16]; snprintf(n, 16, "A%d", i); w.addPlayer(n, true, 0, skillA); }
    for (int i = 0; i < 5; i++) { char n[16]; snprintf(n, 16, "B%d", i); w.addPlayer(n, true, 1, skillB); }
    w.startMatch();
    UserCmd cmds[MAX_PLAYERS];
    int kills = 0, plants = 0, defuses = 0, hs = 0, nades = 0, buys = 0, stuckChecks = 0;
    long ticks = 0;
    while (w.phase != PH_MATCH_END && ticks < 128L * 60 * 90) {
        for (int i = 0; i < MAX_PLAYERS; i++) if (w.players[i].active) bots.think(w, i, cmds[i]);
        w.step(cmds);
        for (auto& e : w.events) {
            bots.onEvent(w, e);
            if (e.type == EV_KILL) { kills++; if (e.flags & 1) hs++; }
            if (e.type == EV_BOMB_PLANTED) plants++;
            if (e.type == EV_BOMB_DEFUSED) defuses++;
            if (e.type == EV_NADE_THROW) nades++;
            if (e.type == EV_BUY) buys++;
            if (e.type == EV_ROUND_END) {
                printf("R%2d %s  score %d-%d  reason=%d  t=%.0fs\n", w.round, e.a == TEAM_ATK ? "ATK" : "DEF", w.squadScore[0], w.squadScore[1], e.value, w.time - w.roundStartTime);
            }
        }
        w.events.clear();
        ticks++;
    }
    printf("MATCH END winner squad %d  %d-%d  rounds=%zu  kills=%d hs=%d plants=%d defuses=%d nades=%d buys=%d simtime=%.0fs\n",
           w.matchWinnerSquad, w.squadScore[0], w.squadScore[1], w.history.size(), kills, hs, plants, defuses, nades, buys, w.time);
    for (auto& p : w.players) if (p.active) printf("  %-4s %c K%2d D%2d A%2d HS%2d DMG%5d $%5d acc %.0f%%\n", p.name, p.team == TEAM_ATK ? 'T' : 'D', p.kills, p.deaths, p.assists, p.headshots, p.damage, p.money, p.shotsFiredTotal ? 100.f * p.shotsHitTotal / p.shotsFiredTotal : 0.f);
}
