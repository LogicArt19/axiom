#include "net.h"
#include "config.h"

namespace ax {

enum PacketType : uint8_t { PK_CONNECT = 1, PK_ACCEPT, PK_REJECT, PK_CMDS, PK_BUY, PK_BYE, PK_SNAP, PK_KICK };

// ---------------------------------------------------------------------------
struct Writer {
    std::vector<uint8_t> b;
    void u8(uint8_t v) { b.push_back(v); }
    void i8(int8_t v) { b.push_back((uint8_t)v); }
    void u16(uint16_t v) { u8(v & 255); u8(v >> 8); }
    void i16(int16_t v) { u16((uint16_t)v); }
    void u32(uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); }
    void f32(float f) { uint32_t v; memcpy(&v, &f, 4); u32(v); }
    void v3(Vector3 v) { f32(v.x); f32(v.y); f32(v.z); }
    void str(const char* s, int n) { for (int i = 0; i < n; i++) u8(s && i < (int)strlen(s) ? (uint8_t)s[i] : 0); }
    void header(PacketType t) { u32(NET_MAGIC); u8(t); }
};
struct Reader {
    const uint8_t* d; int n; int p = 0; bool bad = false;
    Reader(const uint8_t* d_, int n_) : d(d_), n(n_) {}
    bool need(int k) { if (p + k > n) { bad = true; return false; } return true; }
    uint8_t u8() { if (!need(1)) return 0; return d[p++]; }
    int8_t i8() { return (int8_t)u8(); }
    uint16_t u16() { uint16_t a = u8(); return (uint16_t)(a | (u8() << 8)); }
    int16_t i16() { return (int16_t)u16(); }
    uint32_t u32() { uint32_t a = u16(); return a | ((uint32_t)u16() << 16); }
    float f32() { uint32_t v = u32(); float f; memcpy(&f, &v, 4); return f; }
    Vector3 v3() { Vector3 v; v.x = f32(); v.y = f32(); v.z = f32(); return v; }
    void str(char* out, int n_) { for (int i = 0; i < n_; i++) out[i] = (char)u8(); out[n_ - 1] = 0; }
};

static void writeEvent(Writer& w, const GameEvent& e) {
    w.u8(e.type); w.i8(e.a); w.i8(e.b); w.u8(e.weapon); w.u8(e.flags); w.i16(e.value); w.v3(e.pos); w.v3(e.pos2);
}
static GameEvent readEvent(Reader& r) {
    GameEvent e{};
    e.type = (EvType)r.u8(); e.a = r.i8(); e.b = r.i8(); e.weapon = r.u8(); e.flags = r.u8(); e.value = r.i16(); e.pos = r.v3(); e.pos2 = r.v3();
    return e;
}

// ===========================================================================
// SERVER
// ===========================================================================
bool NetServer::start(int p) {
    if (!sock::init()) return false;
    sock_ = sock::open((uint16_t)p);
    if (sock_ < 0) return false;
    port = p;
    running = true;
    clients.clear();
    eventLog.clear();
    eventSeq = 0;
    log.push_back(std::string("Serveur ouvert sur le port ") + std::to_string(p));
    return true;
}

void NetServer::stop(World& w) {
    if (!running) return;
    for (auto& c : clients) {
        Writer wr; wr.header(PK_KICK); wr.str("Le serveur a ferme la partie", 48);
        for (int k = 0; k < 3; k++) sock::sendTo(sock_, c.addr, wr.b.data(), (int)wr.b.size());
        if (c.playerId >= 0) { w.players[c.playerId].isRemote = false; w.players[c.playerId].isBot = true; }
    }
    clients.clear();
    sock::close(sock_);
    sock_ = -1;
    running = false;
}

int NetServer::humanCount() const { int n = 0; for (auto& c : clients) if (c.active) n++; return n; }

int NetServer::assignPlayer(World& w, const char* name) {
    // pick the squad with fewer humans, then take over one of its bots
    int humans[2] = {0, 0};
    for (auto& p : w.players) if (p.active && !p.isBot) humans[p.squad & 1]++;
    int order[2] = {humans[0] <= humans[1] ? 0 : 1, humans[0] <= humans[1] ? 1 : 0};
    for (int k = 0; k < 2; k++) {
        for (auto& p : w.players) {
            if (!p.active || !p.isBot) continue;
            if (w.mode != MODE_DEATHMATCH && p.squad != order[k]) continue;
            p.isBot = false; p.isRemote = true;
            snprintf(p.name, sizeof p.name, "%s", name);
            return p.id;
        }
    }
    int id = w.addPlayer(name, false, order[0], 2);
    if (id >= 0) {
        w.players[id].isRemote = true;
        if (w.mode != MODE_COMPETITIVE) w.respawnPlayer(w.players[id], false);
    }
    return id;
}

void NetServer::handleConnect(World& w, const sock::Addr& from, const uint8_t* d, int n) {
    Reader r(d, n);
    uint32_t ver = r.u32();
    char name[24]; r.str(name, 24);
    if (r.bad) return;
    RemoteClient* existing = nullptr;
    for (auto& c : clients) if (c.active && c.addr == from) existing = &c;
    if (ver != NET_VERSION) {
        Writer wr; wr.header(PK_REJECT); wr.str("Version du jeu differente", 48);
        sock::sendTo(sock_, from, wr.b.data(), (int)wr.b.size());
        return;
    }
    if (!existing) {
        if (humanCount() >= MAX_PLAYERS - 1) {
            Writer wr; wr.header(PK_REJECT); wr.str("Serveur plein", 48);
            sock::sendTo(sock_, from, wr.b.data(), (int)wr.b.size());
            return;
        }
        int id = assignPlayer(w, name[0] ? name : "Joueur");
        if (id < 0) {
            Writer wr; wr.header(PK_REJECT); wr.str("Aucune place disponible", 48);
            sock::sendTo(sock_, from, wr.b.data(), (int)wr.b.size());
            return;
        }
        RemoteClient c;
        c.active = true; c.addr = from; c.playerId = id; c.name = name; c.lastHeard = nowSeconds();
        c.ackEventSeq = eventSeq;     // don't replay old history
        clients.push_back(c);
        existing = &clients.back();
        log.push_back(std::string(name) + " a rejoint (" + sock::toString(from) + ")");
    }
    Writer wr; wr.header(PK_ACCEPT);
    wr.u8((uint8_t)existing->playerId); wr.u8(w.mode); wr.u8((uint8_t)w.roundsToWin);
    wr.u16((uint16_t)w.roundTimeSec); wr.u16((uint16_t)w.buyTimeSec); wr.u16((uint16_t)w.freezeTimeSec);
    wr.u32(w.tick);
    sock::sendTo(sock_, from, wr.b.data(), (int)wr.b.size());
}

void NetServer::poll(World& w) {
    if (!running) return;
    uint8_t buf[4096];
    sock::Addr from;
    int n;
    double now = nowSeconds();
    while ((n = sock::recvFrom(sock_, from, buf, sizeof buf)) > 0) {
        Reader r(buf, n);
        if (r.u32() != NET_MAGIC) continue;
        uint8_t type = r.u8();
        if (type == PK_CONNECT) { handleConnect(w, from, buf + 5, n - 5); continue; }
        RemoteClient* c = nullptr;
        for (auto& cl : clients) if (cl.active && cl.addr == from) c = &cl;
        if (!c) continue;
        c->lastHeard = now;
        if (type == PK_CMDS) {
            r.u32();                       // last snapshot seq (unused)
            uint32_t ackEv = r.u32();
            c->pingMs = r.u16();
            if (ackEv > c->ackEventSeq && ackEv <= eventSeq) c->ackEventSeq = ackEv;
            int cnt = r.u8();
            for (int i = 0; i < cnt && !r.bad; i++) {
                UserCmd cmd;
                cmd.tick = r.u32(); cmd.yaw = r.f32(); cmd.pitch = r.f32();
                cmd.fwd = r.i8() / 127.f; cmd.side = r.i8() / 127.f;
                cmd.buttons = r.u16(); cmd.select = r.u8(); cmd.viewTick = r.u32();
                if (r.bad) break;
                if (cmd.tick > c->lastRecvCmdTick) { c->queue.push_back(cmd); c->lastRecvCmdTick = cmd.tick; }
            }
            while (c->queue.size() > 8) c->queue.pop_front();
            if (c->playerId >= 0) w.players[c->playerId].ping = c->pingMs;
        } else if (type == PK_BUY) {
            int16_t item = r.i16();
            uint32_t nonce = r.u32();
            if (!r.bad && !c->buyNonces.count(nonce)) {
                c->buyNonces[nonce] = true;
                if (item > 0) w.tryBuy(c->playerId, item); else if (item < 0) w.trySell(c->playerId, -item);
            }
        } else if (type == PK_BYE) {
            c->lastHeard = -100;
        }
    }
    // timeouts -> the bot takes over again
    for (auto& c : clients) {
        if (!c.active) continue;
        if (now - c.lastHeard > 10.0) {
            c.active = false;
            if (c.playerId >= 0) {
                Player& p = w.players[c.playerId];
                p.isRemote = false; p.isBot = true;
                snprintf(p.name, sizeof p.name, "BOT %.18s", c.name.c_str());
            }
            log.push_back(c.name + " a quitte la partie");
        }
    }
    clients.erase(std::remove_if(clients.begin(), clients.end(), [](const RemoteClient& c) { return !c.active; }), clients.end());
}

static thread_local bool g_processedThisTick[MAX_PLAYERS];

bool NetServer::takeCmd(World& w, int pid, UserCmd& out) {
    for (auto& c : clients) {
        if (!c.active || c.playerId != pid) continue;
        g_processedThisTick[pid] = false;
        if (!c.queue.empty()) {
            out = c.queue.front();
            c.queue.pop_front();
            c.lastCmd = out;
            c.lastProcessedTick = out.tick;
            g_processedThisTick[pid] = true;
        } else {
            out = c.lastCmd;
            out.select = 0;
        }
        int lag = (int)(w.tick + 1) - (int)out.viewTick;
        out.lagTicks = (int16_t)std::clamp(lag, 0, HISTORY - 4);
        return true;
    }
    return false;
}

void NetServer::afterStep(World& w) {
    for (auto& c : clients)
        if (c.active && c.playerId >= 0 && g_processedThisTick[c.playerId]) { c.procTime = w.time; g_processedThisTick[c.playerId] = false; }
}

void NetServer::recordEvents(const std::vector<GameEvent>& evs) {
    for (auto& e : evs) eventLog.push_back({++eventSeq, e});
    while (eventLog.size() > 2048) eventLog.pop_front();
}

void NetServer::sendSnapshots(World& w) {
    if (!running || clients.empty()) return;
    snapSeq++;
    float st = w.time;
    for (auto& c : clients) {
        if (!c.active) continue;
        Writer wr;
        wr.header(PK_SNAP);
        wr.u32(snapSeq); wr.u32(w.tick); wr.u32(c.lastProcessedTick);
        // game state
        wr.u8(w.mode); wr.u8(w.phase); wr.f32(w.phaseEnd - st); wr.f32(w.roundStartTime - st); wr.f32(w.buyEndTime - st);
        wr.u8((uint8_t)w.round); wr.u8((uint8_t)w.squadScore[0]); wr.u8((uint8_t)w.squadScore[1]);
        wr.u8(w.squadSide[0]); wr.u8(w.squadSide[1]); wr.u8(w.overtime); wr.i8((int8_t)w.matchWinnerSquad);
        wr.i8((int8_t)w.lastMvp); wr.u8(w.lastWinner); wr.u8(w.lastReason); wr.f32(w.dmEndTime - st); wr.u8((uint8_t)w.roundsToWin);
        wr.u8((uint8_t)std::min<size_t>(w.history.size(), 60));
        for (size_t i = 0; i < std::min<size_t>(w.history.size(), 60); i++) {
            auto& h = w.history[i];
            wr.u8((uint8_t)(h.winner | (h.reason << 2) | (h.winnerSquad << 5)));
        }
        // bomb
        wr.u8(w.bomb.state); wr.i8((int8_t)w.bomb.carrier); wr.v3(w.bomb.pos); wr.f32(w.bomb.explodeTime - st);
        wr.i8((int8_t)w.bomb.defuser); wr.f32(w.bomb.defuseStart - st); wr.f32(w.bomb.defuseEnd - st); wr.u8(w.bomb.site);
        // players
        int cnt = 0;
        for (auto& p : w.players) if (p.active) cnt++;
        wr.u8((uint8_t)cnt);
        for (auto& p : w.players) {
            if (!p.active) continue;
            uint8_t f1 = (p.alive ? 1 : 0) | (p.isBot ? 2 : 0) | (p.onGround ? 4 : 0) | (p.hasBomb ? 8 : 0) | (p.kit ? 16 : 0) |
                         (p.helmet ? 32 : 0) | (p.defusing ? 64 : 0) | (p.planting ? 128 : 0);
            uint8_t f2 = (p.reloading ? 1 : 0) | (p.pinPulled ? 2 : 0) | (p.isRemote ? 4 : 0);
            wr.u8((uint8_t)p.id); wr.u8(f1); wr.u8(f2); wr.u8(p.team); wr.u8((uint8_t)p.squad);
            wr.str(p.name, 16);
            wr.v3(p.pos); wr.v3(p.vel); wr.f32(p.yaw); wr.f32(p.pitch); wr.u8((uint8_t)(p.duck * 255));
            wr.u8((uint8_t)std::max(0, p.health)); wr.u8((uint8_t)p.armor); wr.u16((uint16_t)p.money);
            wr.u8(p.activeW); wr.u8(p.primary); wr.u8(p.secondary);
            wr.u8((uint8_t)(p.nades[0] | (p.nades[1] << 2) | (p.nades[2] << 4) | (p.nades[3] << 6)));
            wr.u8((uint8_t)p.scope);
            wr.i16((int16_t)p.kills); wr.i16((int16_t)p.deaths); wr.i16((int16_t)p.assists); wr.i16((int16_t)p.headshots);
            wr.i16((int16_t)std::min(p.damage, 32000)); wr.i16((int16_t)p.mvps); wr.i16((int16_t)p.score); wr.u16((uint16_t)p.ping);
            wr.i16((int16_t)p.clip[0]); wr.i16((int16_t)p.clip[1]); wr.i16((int16_t)p.reserve[0]); wr.i16((int16_t)p.reserve[1]);
            wr.f32(p.flashEnd - st); wr.f32(p.flashDur); wr.f32(p.punchYaw); wr.f32(p.punchPitch);
            if (p.id == c.playerId) {
                float pt = c.procTime;
                wr.f32(p.nextAttack - pt); wr.f32(p.drawEnd - pt); wr.f32(p.reloadEnd - pt); wr.f32(p.lastShotTime - pt);
                wr.f32(p.plantStart - pt); wr.f32(p.inspectUntil - pt);
                wr.f32(p.spreadAccum); wr.u8((uint8_t)std::min(p.shotsFired, 255)); wr.u8((uint8_t)p.scopeResume); wr.f32(p.stamina);
                wr.u8((uint8_t)((p.attackHeld ? 1 : 0) | (p.attack2Held ? 2 : 0) | (p.jumpHeld ? 4 : 0) | (p.dropHeld ? 8 : 0)));
                wr.u8(p.lastW); wr.u8(p.throwStrength); wr.f32(p.stepAccum); wr.f32(p.tagUntil - pt); wr.f32(p.tagAmount);
            }
        }
        // smokes, fires, projectiles, drops
        wr.u8((uint8_t)std::min<size_t>(w.smokes.size(), 32));
        for (size_t i = 0; i < std::min<size_t>(w.smokes.size(), 32); i++) { wr.v3(w.smokes[i].pos); wr.f32(w.smokes[i].start - st); wr.f32(w.smokes[i].end - st); }
        wr.u8((uint8_t)std::min<size_t>(w.fires.size(), 32));
        for (size_t i = 0; i < std::min<size_t>(w.fires.size(), 32); i++) { wr.v3(w.fires[i].pos); wr.f32(w.fires[i].start - st); wr.f32(w.fires[i].end - st); wr.f32(w.fires[i].radius); }
        wr.u8((uint8_t)std::min<size_t>(w.projectiles.size(), 32));
        for (size_t i = 0; i < std::min<size_t>(w.projectiles.size(), 32); i++) { wr.u8(w.projectiles[i].type); wr.v3(w.projectiles[i].pos); }
        wr.u8((uint8_t)std::min<size_t>(w.drops.size(), 40));
        for (size_t i = 0; i < std::min<size_t>(w.drops.size(), 40); i++) { wr.u8(w.drops[i].w); wr.v3(w.drops[i].pos); wr.f32(w.drops[i].spawnTime - st); }
        // reliable event stream
        uint32_t first = c.ackEventSeq + 1;
        if (!eventLog.empty() && first < eventLog.front().first) first = eventLog.front().first;
        std::vector<const GameEvent*> evs;
        for (auto& pe : eventLog) if (pe.first >= first && evs.size() < 40) evs.push_back(&pe.second);
        wr.u32(first);
        wr.u8((uint8_t)evs.size());
        for (auto* e : evs) writeEvent(wr, *e);
        sock::sendTo(sock_, c.addr, wr.b.data(), (int)wr.b.size());
    }
}

// ===========================================================================
// CLIENT
// ===========================================================================
bool NetClient::connect(const char* host, int port, const char* nm) {
    disconnect();
    if (!sock::init()) { state = FAILED; reason = "Initialisation reseau impossible"; return false; }
    if (!sock::resolve(host, (uint16_t)port, server)) { state = FAILED; reason = "Adresse introuvable"; return false; }
    sock_ = sock::open(0);
    if (sock_ < 0) { state = FAILED; reason = "Socket indisponible"; return false; }
    name = nm;
    state = CONNECTING;
    connectStart = nowSeconds();
    lastConnectSend = 0;
    lastSnapSeq = 0; lastEventSeq = 0; latestServerTick = 0;
    pending.clear(); snaps.clear(); sendTimes.clear();
    lastCmdTick = 0; snapsReceived = snapsExpected = 0;
    localId = -1;
    return true;
}

void NetClient::disconnect() {
    if (sock_ >= 0) {
        Writer wr; wr.header(PK_BYE);
        for (int i = 0; i < 3; i++) sock::sendTo(sock_, server, wr.b.data(), (int)wr.b.size());
        sock::close(sock_);
    }
    sock_ = -1;
    if (state == CONNECTED || state == CONNECTING) state = IDLE;
}

float NetClient::serverTimeEstimate() const {
    double since = std::min(0.25, nowSeconds() - latestRecvTime);
    return latestServerTick * TICK_DT + (float)since;
}

uint32_t NetClient::nextCmdTick() {
    double est = latestServerTick + (nowSeconds() - latestRecvTime) / TICK_DT + (rttMs * 0.001 * 0.5) / TICK_DT + 2.0;
    uint32_t t = (uint32_t)std::max(0.0, est + 0.5);
    if (lastCmdTick == 0 || t > lastCmdTick + 12) lastCmdTick = t;
    else lastCmdTick = lastCmdTick + 1;
    return lastCmdTick;
}

void NetClient::sendCmd(const UserCmd& c) {
    (void)c;
    if (state != CONNECTED) return;
    Writer wr; wr.header(PK_CMDS);
    wr.u32(lastSnapSeq); wr.u32(lastEventSeq); wr.u16((uint16_t)std::min(rttMs, 65000));
    int cnt = (int)std::min<size_t>(pending.size(), 4);
    wr.u8((uint8_t)cnt);
    for (size_t i = pending.size() - cnt; i < pending.size(); i++) {
        const UserCmd& u = pending[i];
        wr.u32(u.tick); wr.f32(u.yaw); wr.f32(u.pitch);
        wr.i8((int8_t)clampf(u.fwd * 127, -127, 127)); wr.i8((int8_t)clampf(u.side * 127, -127, 127));
        wr.u16((uint16_t)u.buttons); wr.u8(u.select); wr.u32(u.viewTick);
    }
    sock::sendTo(sock_, server, wr.b.data(), (int)wr.b.size());
}

void NetClient::sendBuy(int item) {
    if (state != CONNECTED) return;
    Writer wr; wr.header(PK_BUY); wr.i16((int16_t)item); wr.u32(buyNonce++);
    for (int i = 0; i < 3; i++) sock::sendTo(sock_, server, wr.b.data(), (int)wr.b.size());
}

void NetClient::predict(World& cw, const UserCmd& c) {
    if (state != CONNECTED) return;
    UserCmd cmd = c;
    pending.push_back(cmd);
    while (pending.size() > 256) pending.pop_front();
    sendTimes[cmd.tick] = nowSeconds();
    if (sendTimes.size() > 512) sendTimes.erase(sendTimes.begin());
    sendCmd(cmd);
    if (localId < 0 || !cw.players[localId].alive) return;
    Player& p = cw.players[localId];
    p.prevPos = p.pos; p.prevDuck = p.duck;
    cw.time = cmd.tick * TICK_DT;
    cw.tick = cmd.tick;
    cw.collectEvents = true;
    cw.predictPlayer(localId, cmd);
}

bool NetClient::update(World& cw, std::vector<GameEvent>& eventsOut) {
    if (sock_ < 0) return false;
    double now = nowSeconds();
    bool reinit = false;
    if (state == CONNECTING) {
        if (now - lastConnectSend > 0.5) {
            lastConnectSend = now;
            Writer wr; wr.header(PK_CONNECT); wr.u32(NET_VERSION); wr.str(name.c_str(), 24);
            sock::sendTo(sock_, server, wr.b.data(), (int)wr.b.size());
        }
        if (now - connectStart > 8.0) { state = FAILED; reason = "Le serveur ne repond pas"; return false; }
    }
    uint8_t buf[8192];
    sock::Addr from;
    int n;
    while ((n = sock::recvFrom(sock_, from, buf, sizeof buf)) > 0) {
        if (from != server) continue;
        Reader r(buf, n);
        if (r.u32() != NET_MAGIC) continue;
        uint8_t type = r.u8();
        lastHeard = now;
        if (type == PK_ACCEPT && state == CONNECTING) {
            localId = r.u8();
            mode = (GameMode)r.u8();
            int rtw = r.u8(); int rt = r.u16(); int bt = r.u16(); int ft = r.u16();
            uint32_t st = r.u32();
            cw.init(mode);
            cw.clientSide = true;
            cw.roundsToWin = rtw; cw.roundTimeSec = (float)rt; cw.buyTimeSec = (float)bt; cw.freezeTimeSec = (float)ft;
            latestServerTick = st; latestRecvTime = now;
            state = CONNECTED;
            reinit = true;
        } else if (type == PK_REJECT) {
            char rs[48]; r.str(rs, 48);
            state = FAILED; reason = rs;
        } else if (type == PK_KICK) {
            char rs[48]; r.str(rs, 48);
            state = DISCONNECTED; reason = rs;
        } else if (type == PK_SNAP && state == CONNECTED) {
            applySnapshot(cw, buf + 5, n - 5, eventsOut);
        }
    }
    if (state == CONNECTED && now - lastHeard > 8.0) { state = DISCONNECTED; reason = "Connexion perdue"; }
    return reinit;
}

static bool isPredictedEvent(EvType t) {
    switch (t) {
    case EV_SHOT: case EV_IMPACT: case EV_RELOAD: case EV_DRAW: case EV_DRYFIRE: case EV_SCOPE:
    case EV_KNIFE_SWING: case EV_JUMP: case EV_LAND: case EV_FOOTSTEP: case EV_NADE_THROW: case EV_PLANT_START:
        return true;
    default: return false;
    }
}

void NetClient::applySnapshot(World& cw, const uint8_t* d, int n, std::vector<GameEvent>& eventsOut) {
    Reader r(d, n);
    uint32_t seq = r.u32();
    uint32_t tick = r.u32();
    uint32_t ackCmd = r.u32();
    if (seq <= lastSnapSeq) return;
    snapsExpected += (int)(seq - lastSnapSeq);
    snapsReceived++;
    if (snapsExpected > 200) { lossPct = 100.f * (1.f - (float)snapsReceived / snapsExpected); snapsExpected = snapsReceived = 0; }
    lastSnapSeq = seq;
    latestServerTick = tick;
    latestRecvTime = nowSeconds();
    float st = tick * TICK_DT;
    if (sendTimes.count(ackCmd)) {
        int sample = (int)((nowSeconds() - sendTimes[ackCmd]) * 1000.0);
        rttMs = rttMs == 0 ? sample : (rttMs * 7 + sample) / 8;
    }
    // ---- game state
    cw.mode = (GameMode)r.u8(); cw.phase = (Phase)r.u8();
    cw.phaseEnd = st + r.f32(); cw.roundStartTime = st + r.f32(); cw.buyEndTime = st + r.f32();
    cw.round = r.u8(); cw.squadScore[0] = r.u8(); cw.squadScore[1] = r.u8();
    cw.squadSide[0] = (Team)r.u8(); cw.squadSide[1] = (Team)r.u8(); cw.overtime = r.u8() != 0; cw.matchWinnerSquad = r.i8();
    cw.lastMvp = r.i8(); cw.lastWinner = (Team)r.u8(); cw.lastReason = (RoundEndReason)r.u8(); cw.dmEndTime = st + r.f32(); cw.roundsToWin = r.u8();
    int hc = r.u8();
    cw.history.clear();
    for (int i = 0; i < hc; i++) { uint8_t v = r.u8(); cw.history.push_back({(Team)(v & 3), (RoundEndReason)((v >> 2) & 7), (v >> 5) & 1}); }
    cw.bomb.state = (BombState)r.u8(); cw.bomb.carrier = r.i8(); cw.bomb.pos = r.v3(); cw.bomb.explodeTime = st + r.f32();
    cw.bomb.defuser = r.i8(); cw.bomb.defuseStart = st + r.f32(); cw.bomb.defuseEnd = st + r.f32(); cw.bomb.site = r.u8();
    // ---- players
    SnapRecord rec{};
    rec.tick = tick;
    int cnt = r.u8();
    bool seen[MAX_PLAYERS] = {false};
    for (int i = 0; i < cnt && !r.bad; i++) {
        int id = r.u8();
        if (id >= MAX_PLAYERS) { r.bad = true; break; }
        Player& p = cw.players[id];
        seen[id] = true;
        uint8_t f1 = r.u8(), f2 = r.u8();
        bool wasAlive = p.alive;
        p.active = true; p.id = id;
        p.alive = f1 & 1; p.isBot = f1 & 2; p.onGround = f1 & 4; p.hasBomb = f1 & 8; p.kit = f1 & 16; p.helmet = f1 & 32;
        p.defusing = f1 & 64; p.planting = f1 & 128; p.reloading = f2 & 1; p.pinPulled = f2 & 2; p.isRemote = f2 & 4;
        p.team = (Team)r.u8(); p.squad = r.u8();
        r.str(p.name, 16);
        Vector3 pos = r.v3(), vel = r.v3();
        float yaw = r.f32(), pitch = r.f32(), duck = r.u8() / 255.f;
        p.health = r.u8(); p.armor = r.u8(); p.money = r.u16();
        WeaponId aw = (WeaponId)r.u8();
        p.primary = (WeaponId)r.u8(); p.secondary = (WeaponId)r.u8();
        uint8_t nd = r.u8();
        p.nades[0] = nd & 3; p.nades[1] = (nd >> 2) & 3; p.nades[2] = (nd >> 4) & 3; p.nades[3] = (nd >> 6) & 3;
        int scope = r.u8();
        p.kills = r.i16(); p.deaths = r.i16(); p.assists = r.i16(); p.headshots = r.i16(); p.damage = r.i16(); p.mvps = r.i16(); p.score = r.i16(); p.ping = r.u16();
        p.clip[0] = r.i16(); p.clip[1] = r.i16(); p.reserve[0] = r.i16(); p.reserve[1] = r.i16();
        float flashEnd = st + r.f32(); p.flashDur = r.f32();
        float punchYaw = r.f32(), punchPitch = r.f32();
        if (id == localId) {
            float base = ackCmd * TICK_DT;
            p.pos = pos; p.vel = vel; p.duck = duck;
            if (!wasAlive && p.alive) { p.yaw = yaw; p.pitch = pitch; }
            p.activeW = aw; p.scope = scope;
            p.punchYaw = punchYaw; p.punchPitch = punchPitch;
            p.nextAttack = base + r.f32(); p.drawEnd = base + r.f32(); p.reloadEnd = base + r.f32(); p.lastShotTime = base + r.f32();
            p.plantStart = base + r.f32(); p.inspectUntil = base + r.f32();
            p.spreadAccum = r.f32(); p.shotsFired = r.u8(); p.scopeResume = r.u8(); p.stamina = r.f32();
            uint8_t hb = r.u8();
            p.attackHeld = hb & 1; p.attack2Held = hb & 2; p.jumpHeld = hb & 4; p.dropHeld = hb & 8;
            p.lastW = (WeaponId)r.u8(); p.throwStrength = r.u8(); p.stepAccum = r.f32(); p.tagUntil = base + r.f32(); p.tagAmount = r.f32();
            p.flashEnd = flashEnd;
            // drop acknowledged commands, replay the rest (reconciliation)
            while (!pending.empty() && pending.front().tick <= ackCmd) pending.pop_front();
            if (p.alive) {
                bool ce = cw.collectEvents;
                cw.collectEvents = false;
                for (auto& c : pending) { cw.time = c.tick * TICK_DT; cw.tick = c.tick; cw.predictPlayer(id, c); }
                cw.collectEvents = ce;
            }
            p.prevPos = p.pos; p.prevDuck = p.duck;
        } else {
            p.vel = vel; p.activeW = aw; p.scope = scope; p.punchYaw = punchYaw; p.punchPitch = punchPitch; p.flashEnd = flashEnd;
            rec.p[id] = {pos, yaw, pitch, duck, p.alive, vel};
            rec.valid[id] = true;
            if (!wasAlive && p.alive) { p.pos = p.prevPos = pos; p.yaw = yaw; p.pitch = pitch; }
        }
    }
    for (int i = 0; i < MAX_PLAYERS; i++) if (!seen[i]) cw.players[i].active = false;
    snaps.push_back(rec);
    while (snaps.size() > 64) snaps.pop_front();
    // ---- world entities
    int ns = r.u8();
    cw.smokes.clear();
    for (int i = 0; i < ns && !r.bad; i++) { SmokeCloud s; s.active = true; s.pos = r.v3(); s.start = st + r.f32(); s.end = st + r.f32(); cw.smokes.push_back(s); }
    int nf = r.u8();
    cw.fires.clear();
    for (int i = 0; i < nf && !r.bad; i++) { FireArea f; f.active = true; f.pos = r.v3(); f.start = st + r.f32(); f.end = st + r.f32(); f.radius = r.f32(); cw.fires.push_back(f); }
    int np = r.u8();
    cw.projectiles.clear();
    for (int i = 0; i < np && !r.bad; i++) { Projectile pr; pr.active = true; pr.type = (WeaponId)r.u8(); pr.pos = pr.prevPos = r.v3(); cw.projectiles.push_back(pr); }
    int nd = r.u8();
    cw.drops.clear();
    for (int i = 0; i < nd && !r.bad; i++) { DroppedWeapon dw; dw.active = true; dw.w = (WeaponId)r.u8(); dw.pos = r.v3(); dw.spawnTime = st + r.f32(); dw.resting = true; cw.drops.push_back(dw); }
    // ---- events
    uint32_t first = r.u32();
    int ne = r.u8();
    for (int i = 0; i < ne && !r.bad; i++) {
        GameEvent e = readEvent(r);
        if (r.bad) break;
        uint32_t s = first + i;
        if (s <= lastEventSeq) continue;
        lastEventSeq = s;
        if (e.a == localId && localId >= 0 && isPredictedEvent(e.type)) continue;
        eventsOut.push_back(e);
    }
}

void NetClient::interpolate(World& cw) {
    if (snaps.empty()) return;
    float rt = serverTimeEstimate() - g_settings.interpMs * 0.001f;
    const SnapRecord* a = &snaps.front();
    const SnapRecord* b = &snaps.back();
    for (size_t i = 0; i + 1 < snaps.size(); i++) {
        if (snaps[i].tick * TICK_DT <= rt && snaps[i + 1].tick * TICK_DT >= rt) { a = &snaps[i]; b = &snaps[i + 1]; break; }
    }
    if (rt > snaps.back().tick * TICK_DT) a = b = &snaps.back();
    float span = (b->tick - a->tick) * TICK_DT;
    float t = span > 0 ? clampf((rt - a->tick * TICK_DT) / span, 0, 1) : 1;
    cw.time = serverTimeEstimate();
    cw.tick = (uint32_t)(cw.time / TICK_DT);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (i == localId || !cw.players[i].active) continue;
        if (!a->valid[i] || !b->valid[i]) { if (b->valid[i]) { cw.players[i].pos = cw.players[i].prevPos = b->p[i].pos; } continue; }
        Player& p = cw.players[i];
        p.pos = lerp3(a->p[i].pos, b->p[i].pos, t);
        p.prevPos = p.pos;
        p.yaw = a->p[i].yaw + angleDiff(b->p[i].yaw, a->p[i].yaw) * t;
        p.pitch = lerpf(a->p[i].pitch, b->p[i].pitch, t);
        p.duck = p.prevDuck = lerpf(a->p[i].duck, b->p[i].duck, t);
    }
}

} // namespace ax
