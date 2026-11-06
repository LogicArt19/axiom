// net.cpp : authoritative server + snapshot client
//
// Packets (UDP):
//   C->S HELLO    magic ver name[24] hero
//   S->C WELCOME  magic player
//   S->C REJECT   magic reason
//   C->S INPUT    magic ackSnap ackEvent clientTime  [cmdSeq cmd]*      (unacked commands are resent)
//   S->C SNAP     magic seq chunk chunks  payload-part                   (payload split in ~1100 byte chunks)
//   C->S BYE / S->C BYE
// A snapshot only contains what the client's team can see (fog of war is enforced
// by the server, so a modified client cannot reveal hidden units).
#include "net.h"
#include <cstring>

namespace ax {

enum PktType : uint8_t { P_HELLO = 1, P_WELCOME, P_REJECT, P_INPUT, P_SNAP, P_BYE };
static const int CHUNK = 1100;
static const double TIMEOUT = 10.0;

struct Writer {
    std::vector<uint8_t> b;
    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint16_t v) { b.push_back(v & 255); b.push_back(v >> 8); }
    void u32(uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((v >> (8 * i)) & 255); }
    void i16(int16_t v) { u16((uint16_t)v); }
    void f32(float v) { uint32_t u; memcpy(&u, &v, 4); u32(u); }
    void pos(Vector2 p) { u16((uint16_t)clampf(p.x * 300.f, 0, 65535)); u16((uint16_t)clampf(p.y * 300.f, 0, 65535)); }
    void str(const char* s, int n) { for (int i = 0; i < n; i++) b.push_back(i < (int)strlen(s) ? (uint8_t)s[i] : 0); }
};
struct Reader {
    const uint8_t* d; int n, i = 0;
    bool bad = false;
    Reader(const uint8_t* d_, int n_) : d(d_), n(n_) {}
    bool need(int k) { if (i + k > n) { bad = true; return false; } return true; }
    uint8_t u8() { if (!need(1)) return 0; return d[i++]; }
    uint16_t u16() { if (!need(2)) return 0; uint16_t v = d[i] | (d[i + 1] << 8); i += 2; return v; }
    uint32_t u32() { if (!need(4)) return 0; uint32_t v = 0; for (int k = 0; k < 4; k++) v |= (uint32_t)d[i + k] << (8 * k); i += 4; return v; }
    int16_t i16() { return (int16_t)u16(); }
    float f32() { uint32_t u = u32(); float v; memcpy(&v, &u, 4); return v; }
    Vector2 pos() { float x = u16() / 300.f; float y = u16() / 300.f; return {x, y}; }
    void str(char* out, int n) { for (int k = 0; k < n; k++) out[k] = (char)u8(); out[n - 1] = 0; }
};

static void writeCommand(Writer& w, const Command& c) {
    w.u8(c.type); w.u8(c.queue); w.u8(c.a); w.f32(c.pt.x); w.f32(c.pt.y); w.u16(c.target);
    w.u8((uint8_t)std::min<size_t>(c.units.size(), 200));
    for (size_t i = 0; i < c.units.size() && i < 200; i++) w.u16(c.units[i]);
}
static Command readCommand(Reader& r) {
    Command c;
    c.type = (CmdType)r.u8(); c.queue = r.u8() != 0; c.a = r.u8(); c.pt.x = r.f32(); c.pt.y = r.f32(); c.target = r.u16();
    int n = r.u8();
    for (int i = 0; i < n && !r.bad; i++) c.units.push_back(r.u16());
    c.pt.x = clampf(c.pt.x, 0, Map::SIZE); c.pt.y = clampf(c.pt.y, 0, Map::SIZE);
    return c;
}

static void writeEvent(Writer& w, uint32_t seq, const GameEvent& e) {
    w.u32(seq); w.u8(e.type); w.i16(e.a); w.i16(e.b); w.u8(e.c); w.u8((uint8_t)e.player); w.pos(e.p); w.pos(e.p2); w.i16(e.value);
}
static GameEvent readEvent(Reader& r, uint32_t& seq) {
    GameEvent e;
    seq = r.u32(); e.type = (EvType)r.u8(); e.a = r.i16(); e.b = r.i16(); e.c = r.u8(); e.player = (int8_t)r.u8(); e.p = r.pos(); e.p2 = r.pos(); e.value = r.i16();
    return e;
}

static bool cosmetic(EvType t) { return t == EV_ATTACK || t == EV_HIT; }
static bool privateEvent(EvType t) {
    return t == EV_NO_GOLD || t == EV_NO_SUPPLY || t == EV_CANT_BUILD || t == EV_UNIT_DONE || t == EV_BUILD_DONE || t == EV_RESEARCH_DONE ||
           t == EV_UNDER_ATTACK || t == EV_ITEM || t == EV_BUILD_START;
}
static bool globalEvent(EvType t) {
    return t == EV_HERO_KILL || t == EV_TITAN_SLAIN || t == EV_TOWER_DOWN || t == EV_GAME_OVER || t == EV_WAVE || t == EV_GAME_START;
}

// ===========================================================================
// server
// ===========================================================================
struct ServerExtra {
    // per client: last event seq sent (for cosmetic events), visibility masks per event
    std::map<uint32_t, uint8_t> visMask;
    uint32_t lastSent[MAX_PLAYERS * 2] = {0};
    double clientTime[MAX_PLAYERS * 2] = {0};
};
static ServerExtra g_sx;

bool NetServer::start(int p) {
    sock::init();
    sock_ = sock::open((uint16_t)p);
    if (sock_ < 0) return false;
    port = p;
    running = true;
    clients.assign(MAX_PLAYERS * 2, RemoteClient{});
    eventLog.clear();
    g_sx = ServerExtra{};
    snapSeq = eventSeq = 0;
    log.push_back(TextFormat("Serveur démarré sur le port %d", p));
    return true;
}

void NetServer::stop(World& w) {
    (void)w;
    if (sock_ >= 0) {
        uint8_t bye[5]; uint32_t m = NET_MAGIC; memcpy(bye, &m, 4); bye[4] = P_BYE;
        for (auto& c : clients) if (c.active) sock::sendTo(sock_, c.addr, bye, 5);
        sock::close(sock_);
    }
    sock_ = -1;
    running = false;
    clients.clear();
}

int NetServer::humanCount() const {
    int n = 0;
    for (auto& c : clients) if (c.active) n++;
    return n;
}

static int takeSlot(World& w, const char* name, int hero) {
    // the team with fewer humans gets the new player; an AI slot is replaced
    int humans[2] = {0, 0};
    for (auto& p : w.players) if (p.active && !p.isAI) humans[p.team]++;
    int pref = humans[0] <= humans[1] ? 0 : 1;
    for (int pass = 0; pass < 2; pass++) {
        int team = pass == 0 ? pref : 1 - pref;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            PlayerState& p = w.players[i];
            if (!p.active || !p.isAI || p.team != team) continue;
            p.isAI = false; p.isRemote = true; p.ready = false;
            snprintf(p.name, sizeof p.name, "%s", name);
            if (w.phase == PH_LOBBY) p.hero = (UnitType)(HERO_FIRST + std::clamp(hero, 0, 5));
            return i;
        }
    }
    return -1;
}

void NetServer::poll(World& w) {
    if (sock_ < 0) return;
    double now = nowSeconds();
    uint8_t buf[4096];
    sock::Addr from;
    int n;
    while ((n = sock::recvFrom(sock_, from, buf, sizeof buf)) > 0) {
        Reader r(buf, n);
        if (r.u32() != NET_MAGIC) continue;
        uint8_t type = r.u8();
        int ci = -1;
        for (size_t i = 0; i < clients.size(); i++) if (clients[i].active && clients[i].addr == from) ci = (int)i;
        if (type == P_HELLO) {
            uint32_t ver = r.u32();
            char name[24]; r.str(name, 24);
            int hero = r.u8();
            Writer o; o.u32(NET_MAGIC);
            if (ver != NET_VERSION) { o.u8(P_REJECT); o.str("Version différente : mettez le jeu à jour", 64); sock::sendTo(sock_, from, o.b.data(), (int)o.b.size()); continue; }
            if (ci < 0) {
                int slot = -1;
                for (size_t i = 0; i < clients.size(); i++) if (!clients[i].active) { slot = (int)i; break; }
                int pl = slot >= 0 ? takeSlot(w, name[0] ? name : "Joueur", hero) : -1;
                if (pl < 0) { o.u8(P_REJECT); o.str("Le salon est complet", 64); sock::sendTo(sock_, from, o.b.data(), (int)o.b.size()); continue; }
                RemoteClient& c = clients[slot];
                c = RemoteClient{};
                c.active = true; c.addr = from; c.player = pl; c.name = name; c.lastHeard = now;
                g_sx.lastSent[slot] = eventSeq;
                c.ackEvent = eventSeq;
                ci = slot;
                log.push_back(std::string(name) + " a rejoint (" + sock::toString(from) + ")");
            }
            o.u8(P_WELCOME); o.u8((uint8_t)clients[ci].player);
            sock::sendTo(sock_, from, o.b.data(), (int)o.b.size());
            continue;
        }
        if (ci < 0) continue;
        RemoteClient& c = clients[ci];
        c.lastHeard = now;
        if (type == P_BYE) {
            PlayerState& p = w.players[c.player];
            p.isAI = true; p.isRemote = false;
            log.push_back(c.name + " est parti (remplacé par l'IA)");
            c.active = false;
            continue;
        }
        if (type == P_INPUT) {
            r.u32();                         // ack snapshot (unused, snapshots are full states)
            uint32_t ackEv = r.u32();
            if (ackEv > c.ackEvent && ackEv <= eventSeq) c.ackEvent = ackEv;
            g_sx.clientTime[ci] = r.f32();
            int cnt = r.u8();
            for (int k = 0; k < cnt && !r.bad; k++) {
                uint32_t seq = r.u32();
                Command cmd = readCommand(r);
                if (r.bad) break;
                if (seq <= c.lastCmdSeq) continue;
                c.lastCmdSeq = seq;
                cmd.player = (uint8_t)c.player;      // never trust the client about who it is
                w.issue(cmd);
            }
        }
    }
    for (size_t i = 0; i < clients.size(); i++) {
        RemoteClient& c = clients[i];
        if (c.active && now - c.lastHeard > TIMEOUT) {
            PlayerState& p = w.players[c.player];
            p.isAI = true; p.isRemote = false;
            log.push_back(c.name + " a perdu la connexion (remplacé par l'IA)");
            c.active = false;
        }
    }
}

void NetServer::recordEvents(const std::vector<GameEvent>& evs) {
    if (!running) return;
    // visibility is captured now, when the world matches the event
    for (auto& e : evs) {
        eventSeq++;
        eventLog.push_back({eventSeq, e});
        g_sx.visMask[eventSeq] = 0;
    }
    while (eventLog.size() > 4000) { g_sx.visMask.erase(eventLog.front().first); eventLog.pop_front(); }
}

static void writeEntity(Writer& w, const World& W, const Entity& e, int player, int team) {
    bool own = e.owner == player || (e.team == team && e.kind == K_BUILDING);
    bool ally = e.team == team;
    w.u16(e.id); w.u8(e.type); w.u8((uint8_t)e.owner); w.u8(e.team);
    w.pos(e.pos);
    w.u8((uint8_t)(fmodf(e.facing + 6.2831853f * 4, 6.2831853f) / 6.2831853f * 255));
    w.f32(e.hp); w.f32(e.maxHp);
    uint16_t flags = 0;
    if (e.constructing) flags |= 1;
    if (W.time < e.stunUntil) flags |= 2;
    if (W.time < e.slowUntil) flags |= 4;
    if (W.time < e.stealthUntil) flags |= 8;
    if (W.time < e.hasteUntil) flags |= 16;
    if (W.time < e.dmgUntil) flags |= 32;
    if (e.shield > 0 && W.time < e.shieldUntil) flags |= 64;
    if (e.carry > 0) flags |= 128;
    if (own) flags |= 256;
    if (e.kind == K_HERO) flags |= 512;
    if (e.summoned) flags |= 1024;
    if (e.kind == K_RESOURCE || e.type == B_EXTRACTOR) flags |= 2048;
    if (e.carryFlux) flags |= 4096;
    w.u16(flags);
    w.u8((uint8_t)clampf((W.time - e.attackAnim) * 100, 0, 255));
    if (flags & 1) w.u8((uint8_t)(clampf(e.buildProgress / std::max(0.1f, unitDef(e.type).buildTime), 0, 1) * 255));
    if (flags & 64) w.f32(e.shield);
    if (flags & 2048) { w.u16((uint16_t)std::max(0, e.amount)); w.u8(e.rich); }
    if (flags & 512) {
        w.f32(e.mana); w.f32(e.maxMana); w.u8((uint8_t)e.level);
        for (int k = 0; k < 4; k++) w.u8(e.items[k]);
    }
    if (flags & 256) {
        w.u8(e.order.type); w.pos(e.order.pt); w.u16(e.order.target);
        w.u8(e.hasRally); w.pos(e.rally);
        w.u8((uint8_t)std::min<size_t>(e.prod.size(), 10));
        for (size_t k = 0; k < e.prod.size() && k < 10; k++) w.u8(e.prod[k]);
        w.f32(e.prodProgress);
        w.u8((uint8_t)e.research); w.f32(e.researchProgress);
        w.u16((uint16_t)e.carry); w.f32(e.gatherTimer);
        if (e.kind == K_HERO) { w.u32((uint32_t)e.xp); for (int k = 0; k < 4; k++) w.f32(e.abilCd[k]); }
    }
    (void)ally;
}

void NetServer::send(World& w) {
    if (sock_ < 0) return;
    snapSeq++;
    double now = nowSeconds();
    for (auto& e : eventLog) {
        uint8_t& m = g_sx.visMask[e.first];
        if (m & 0x80) continue;
        // mark visibility once (first send after the event)
        for (int t = 0; t < 2; t++) if (w.cellVisible(t, e.second.p)) m |= (uint8_t)(1 << t);
        m |= 0x80;
    }
    for (size_t ci = 0; ci < clients.size(); ci++) {
        RemoteClient& c = clients[ci];
        if (!c.active) continue;
        int player = c.player, team = w.players[player].team;
        Writer s;
        s.u32(w.tick); s.u8((uint8_t)w.mapId); s.f32(w.time); s.u8(w.phase); s.u8((uint8_t)(int8_t)w.winner); s.f32(w.lobbyEnd - w.time);
        s.u32(c.lastCmdSeq); s.f32((float)g_sx.clientTime[ci]);
        s.f32(w.titanBuffUntil[0] - w.time); s.f32(w.titanBuffUntil[1] - w.time); s.f32(w.nextWave - w.time);
        s.u16(w.core[0]); s.u16(w.core[1]);
        // players
        for (int i = 0; i < MAX_PLAYERS; i++) {
            const PlayerState& p = w.players[i];
            s.u8(p.active | (p.isAI << 1) | (p.ready << 2));
            if (!p.active) continue;
            s.str(p.name, 24); s.u8(p.team); s.u8(p.hero); s.u16(p.heroId);
            s.u16((uint16_t)(p.team == team ? std::min(p.gold, 65535) : 0));
            s.u16((uint16_t)(p.team == team ? std::min(p.flux, 65535) : 0));
            s.u8((uint8_t)p.upgrades[0]); s.u8((uint8_t)p.upgrades[1]);
            s.u32((uint32_t)p.goldEarned); s.u16((uint16_t)p.unitsProduced); s.u16((uint16_t)p.unitsKilled); s.u16((uint16_t)p.unitsLost); s.u32((uint32_t)p.actions);
            int ping = 0;
            for (auto& cc : clients) if (cc.active && cc.player == i) ping = cc.ping;
            s.u16((uint16_t)ping);
            const Entity* h = p.heroId < w.ents.size() ? &w.ents[p.heroId] : nullptr;
            if (h && h->id) {
                s.u8((uint8_t)h->level); s.u16((uint16_t)h->kills); s.u16((uint16_t)h->deaths); s.u16((uint16_t)h->assists); s.u16((uint16_t)h->lastHits);
                for (int k = 0; k < 4; k++) s.u8(h->items[k]);
                s.f32(h->alive ? 0 : std::max(0.f, h->respawnAt - w.time));
            } else { s.u8(1); for (int k = 0; k < 4; k++) s.u16(0); for (int k = 0; k < 4; k++) s.u8(0); s.f32(0); }
        }
        // entities
        s.u16((uint16_t)w.ents.size());
        std::vector<const Entity*> list;
        for (auto& e : w.ents) {
            if (!e.alive) continue;
            bool known = e.team == team || e.kind == K_RESOURCE || e.kind == K_TOWER || e.type == B_CORE || w.visibleTo(team, e);
            if (e.team != team && e.kind != K_RESOURCE && e.kind != K_TOWER && e.type != B_CORE && !w.cellVisible(team, e.pos)) known = false;
            if (known) list.push_back(&e);
        }
        s.u16((uint16_t)list.size());
        for (auto* e : list) writeEntity(s, w, *e, player, team);
        // projectiles / zones that can be seen
        std::vector<const Projectile*> pl;
        for (auto& p : w.projs) if (p.alive && (p.team == team || w.cellVisible(team, p.pos))) pl.push_back(&p);
        s.u16((uint16_t)std::min<size_t>(pl.size(), 400));
        for (size_t k = 0; k < pl.size() && k < 400; k++) {
            auto* p = pl[k];
            s.pos(p->pos); s.f32(p->dir.x); s.f32(p->dir.y); s.u8((uint8_t)clampf(p->height * 40, 0, 255)); s.u8(p->visual); s.u8(p->team); s.f32(p->speed); s.u8(p->srcType);
        }
        std::vector<const Zone*> zl;
        for (auto& z : w.zones) if (z.team == team || w.cellVisible(team, z.pos)) zl.push_back(&z);
        s.u16((uint16_t)zl.size());
        for (auto* z : zl) { s.u8(z->kind); s.pos(z->pos); s.f32(z->radius); s.f32(z->start - w.time); s.f32(z->end - w.time); s.f32(z->delay); s.u8(z->team); s.u8(z->fired); }
        // events
        std::vector<std::pair<uint32_t, const GameEvent*>> ev;
        for (auto& e : eventLog) {
            if (e.first <= c.ackEvent) continue;
            const GameEvent& g = e.second;
            if (cosmetic(g.type) && e.first <= g_sx.lastSent[ci]) continue;
            bool ok;
            if (privateEvent(g.type)) ok = g.player == player;
            else if (globalEvent(g.type)) ok = true;
            else if (g.type == EV_LEVELUP || g.type == EV_RESPAWN) ok = g.player >= 0 && w.players[g.player].team == team;
            else ok = (g_sx.visMask[e.first] & (1 << team)) != 0;
            if (ok) ev.push_back({e.first, &g});
            if (ev.size() >= 250) break;
        }
        g_sx.lastSent[ci] = eventSeq;
        s.u32(eventSeq);
        s.u16((uint16_t)ev.size());
        for (auto& e : ev) writeEvent(s, e.first, *e.second);
        // send in chunks
        int total = (int)s.b.size();
        int chunks = (total + CHUNK - 1) / CHUNK;
        for (int k = 0; k < chunks; k++) {
            Writer o;
            o.u32(NET_MAGIC); o.u8(P_SNAP); o.u32(snapSeq); o.u8((uint8_t)k); o.u8((uint8_t)chunks);
            int len = std::min(CHUNK, total - k * CHUNK);
            o.b.insert(o.b.end(), s.b.begin() + k * CHUNK, s.b.begin() + k * CHUNK + len);
            sock::sendTo(sock_, c.addr, o.b.data(), (int)o.b.size());
        }
        (void)now;
    }
}

// ===========================================================================
// client
// ===========================================================================
struct ClientExtra {
    uint32_t assemblingSeq = 0;
    int chunksExpected = 0, chunksGot = 0;
    std::vector<std::vector<uint8_t>> parts;
    bool welcomed = false;
};
static ClientExtra g_cx;

bool NetClient::connect(const char* host, int port, const char* nm, int h) {
    disconnect();
    sock::init();
    if (!sock::resolve(host, (uint16_t)port, server)) { state = FAILED; reason = std::string("Adresse introuvable : ") + host; return false; }
    sock_ = sock::open(0);
    if (sock_ < 0) { state = FAILED; reason = "Impossible d'ouvrir un socket"; return false; }
    name = nm; hero = h;
    state = CONNECTING;
    reason.clear();
    connectStart = lastHeard = nowSeconds();
    lastSend = 0; lastSnap = lastEvent = cmdSeq = 0; lastSnapTime = 0;
    unacked.clear();
    localPlayer = -1;
    g_cx = ClientExtra{};
    return true;
}

void NetClient::disconnect() {
    if (sock_ >= 0) {
        uint8_t bye[5]; uint32_t m = NET_MAGIC; memcpy(bye, &m, 4); bye[4] = P_BYE;
        if (state == CONNECTED) for (int i = 0; i < 3; i++) sock::sendTo(sock_, server, bye, 5);
        sock::close(sock_);
    }
    sock_ = -1;
    if (state != FAILED) state = IDLE;
}

void NetClient::sendCommand(const Command& c) {
    if (state != CONNECTED) return;
    Writer w;
    writeCommand(w, c);
    unacked.push_back({++cmdSeq, w.b});
    flush();
}

void NetClient::flush() {
    if (sock_ < 0) return;
    Writer w;
    w.u32(NET_MAGIC); w.u8(P_INPUT); w.u32(lastSnap); w.u32(lastEvent); w.f32((float)fmod(nowSeconds(), 100000.0));
    int cnt = 0;
    size_t countPos = w.b.size();
    w.u8(0);
    for (auto& u : unacked) {
        if (w.b.size() + u.second.size() + 4 > 1200) break;
        w.u32(u.first);
        w.b.insert(w.b.end(), u.second.begin(), u.second.end());
        cnt++;
    }
    w.b[countPos] = (uint8_t)cnt;
    sock::sendTo(sock_, server, w.b.data(), (int)w.b.size());
    lastSend = nowSeconds();
}

float NetClient::alpha() const {
    if (lastSnapTime <= 0) return 1;
    return clampf((float)((nowSeconds() - lastSnapTime) / snapInterval), 0, 1);
}

bool NetClient::update(World& cw, std::vector<GameEvent>& out) {
    if (sock_ < 0) return false;
    double now = nowSeconds();
    bool initialised = false;
    if (state == CONNECTING) {
        if (now - lastSend > 0.5) {
            Writer w; w.u32(NET_MAGIC); w.u8(P_HELLO); w.u32(NET_VERSION); w.str(name.c_str(), 24); w.u8((uint8_t)hero);
            sock::sendTo(sock_, server, w.b.data(), (int)w.b.size());
            lastSend = now;
        }
        if (now - connectStart > 8) { state = FAILED; reason = "Aucune réponse du serveur (adresse, port UDP ou pare-feu ?)"; return false; }
    }
    uint8_t buf[4096];
    sock::Addr from;
    int n;
    while ((n = sock::recvFrom(sock_, from, buf, sizeof buf)) > 0) {
        if (from != server) continue;
        Reader r(buf, n);
        if (r.u32() != NET_MAGIC) continue;
        uint8_t type = r.u8();
        lastHeard = now;
        if (type == P_REJECT) { char why[64]; r.str(why, 64); state = FAILED; reason = why; return false; }
        if (type == P_BYE) { state = DISCONNECTED; reason = "Le serveur a fermé la partie"; return false; }
        if (type == P_WELCOME) {
            if (state == CONNECTING) {
                localPlayer = r.u8();
                state = CONNECTED;
                cw = World{};
                cw.setup(1);
                cw.clientSide = true;
                initialised = true;
            }
            continue;
        }
        if (type == P_SNAP && state == CONNECTED) {
            uint32_t seq = r.u32();
            int chunk = r.u8(), chunks = r.u8();
            if (seq <= lastSnap || chunks == 0) continue;
            if (seq != g_cx.assemblingSeq) {
                if (seq < g_cx.assemblingSeq) continue;
                g_cx.assemblingSeq = seq; g_cx.chunksExpected = chunks; g_cx.chunksGot = 0;
                g_cx.parts.assign(chunks, {});
            }
            if (chunk >= (int)g_cx.parts.size() || !g_cx.parts[chunk].empty()) continue;
            g_cx.parts[chunk].assign(buf + r.i, buf + n);
            if (g_cx.parts[chunk].empty()) g_cx.parts[chunk].push_back(0);
            if (++g_cx.chunksGot == g_cx.chunksExpected) {
                std::vector<uint8_t> all;
                for (auto& p : g_cx.parts) all.insert(all.end(), p.begin(), p.end());
                lastSnap = seq;
                double dtSnap = lastSnapTime > 0 ? now - lastSnapTime : snapInterval;
                snapInterval = clampf((float)(snapInterval * 0.85 + dtSnap * 0.15), 1.0f / 40, 0.25f);
                lastSnapTime = now;
                mapChanged = false;
                applySnapshot(cw, all.data(), (int)all.size(), out);
                if (mapChanged) initialised = true;
            }
        }
    }
    if (state == CONNECTED) {
        if (now - lastHeard > TIMEOUT) { state = DISCONNECTED; reason = "Connexion au serveur perdue"; return initialised; }
        if (now - lastSend > 1.0 / 30) flush();
    }
    return initialised;
}

void NetClient::applySnapshot(World& cw, const uint8_t* d, int n, std::vector<GameEvent>& out) {
    Reader r(d, n);
    cw.tick = r.u32();
    int mid = r.u8();
    if (mid != cw.mapId || cw.map.vheight.empty()) {
        cw.mapId = mid; cw.map.generate(mid);
        cw.ents.clear(); cw.ents.resize(1);
        for (int t = 0; t < 2; t++) { cw.vis[t].assign(Map::N * Map::N, 0); cw.explored[t].assign(Map::N * Map::N, 0); }
        mapChanged = true;
    }
    cw.time = r.f32();
    Phase ph = (Phase)r.u8();
    cw.winner = (int8_t)r.u8();
    cw.lobbyEnd = cw.time + r.f32();
    uint32_t ackCmd = r.u32();
    float echo = r.f32();
    if (echo > 0) { float rtt = (float)fmod(nowSeconds(), 100000.0) - echo; if (rtt >= 0 && rtt < 5) rttMs = (int)(rtt * 1000); }
    while (!unacked.empty() && unacked.front().first <= ackCmd) unacked.pop_front();
    cw.titanBuffUntil[0] = cw.time + r.f32(); cw.titanBuffUntil[1] = cw.time + r.f32(); cw.nextWave = cw.time + r.f32();
    cw.core[0] = r.u16(); cw.core[1] = r.u16();
    struct HeroInfo { uint16_t id; int level, k, de, as, lh; uint8_t items[4]; float respawn; };
    HeroInfo hi[MAX_PLAYERS] = {};
    for (int i = 0; i < MAX_PLAYERS; i++) {
        PlayerState& p = cw.players[i];
        uint8_t f = r.u8();
        p.active = f & 1; p.isAI = (f & 2) != 0; p.ready = (f & 4) != 0;
        if (!p.active) continue;
        r.str(p.name, 24); p.team = r.u8(); p.hero = (UnitType)r.u8(); p.heroId = r.u16();
        p.gold = r.u16();
        p.flux = r.u16();
        p.upgrades[0] = r.u8(); p.upgrades[1] = r.u8();
        p.goldEarned = (int)r.u32(); p.unitsProduced = r.u16(); p.unitsKilled = r.u16(); p.unitsLost = r.u16(); p.actions = (int)r.u32();
        p.ping = r.u16();
        hi[i].id = p.heroId; hi[i].level = r.u8(); hi[i].k = r.u16(); hi[i].de = r.u16(); hi[i].as = r.u16(); hi[i].lh = r.u16();
        for (int k = 0; k < 4; k++) hi[i].items[k] = r.u8();
        hi[i].respawn = r.f32();
    }
    int team = localPlayer >= 0 ? cw.players[localPlayer].team : 0;
    size_t total = r.u16();
    if (cw.ents.size() < total) cw.ents.resize(total);
    for (size_t i = 0; i < cw.ents.size(); i++) cw.ents[i].id = (uint16_t)i;
    std::vector<uint8_t> seen(cw.ents.size(), 0);
    int cnt = r.u16();
    for (int k = 0; k < cnt && !r.bad; k++) {
        uint16_t id = r.u16();
        if (id >= cw.ents.size()) { r.bad = true; break; }
        Entity& e = cw.ents[id];
        seen[id] = 1;
        UnitType t = (UnitType)r.u8();
        bool fresh = !e.alive || e.type != t;
        const UnitDef& def = unitDef(t);
        int8_t owner = (int8_t)r.u8(); uint8_t eteam = r.u8();
        Vector2 np = r.pos();
        if (fresh) {
            if (e.alive && (e.kind == K_BUILDING || e.kind == K_TOWER || e.kind == K_RESOURCE)) cw.map.setBlocked(e.pos, std::max(1, unitDef(e.type).footprint), 0);
            e = Entity{};
            e.id = id; e.radius = def.radius; e.alive = true;
            e.pos = e.prevPos = np;
            if (def.kind == K_BUILDING || def.kind == K_TOWER || def.kind == K_RESOURCE) cw.map.setBlocked(np, std::max(1, def.footprint), def.kind == K_RESOURCE ? 3 : 2);
        } else { e.prevPos = e.pos; e.pos = np; }
        e.type = t; e.kind = def.kind; e.owner = owner; e.team = eteam;
        e.facing = r.u8() / 255.f * 6.2831853f;
        e.hp = r.f32(); e.maxHp = r.f32();
        uint16_t f = r.u16();
        e.attackAnim = cw.time - r.u8() / 100.f;
        e.constructing = f & 1;
        float st = cw.time + 0.35f;
        e.stunUntil = (f & 2) ? st : 0; e.slowUntil = (f & 4) ? st : 0; e.stealthUntil = (f & 8) ? st : 0;
        e.hasteUntil = (f & 16) ? st : 0; e.dmgUntil = (f & 32) ? st : 0;
        e.summoned = (f & 1024) != 0;
        if (f & 1) e.buildProgress = r.u8() / 255.f * def.buildTime;
        if (f & 64) { e.shield = r.f32(); e.shieldUntil = st; } else e.shield = 0;
        if (f & 2048) { e.amount = r.u16(); e.rich = r.u8() != 0; }
        e.carryFlux = (f & 4096) != 0;
        e.carry = (f & 128) ? 1 : 0;
        if (f & 512) {
            e.mana = r.f32(); e.maxMana = r.f32(); e.level = r.u8();
            for (int q = 0; q < 4; q++) e.items[q] = r.u8();
        }
        if (f & 256) {
            e.order.type = (OrderType)r.u8(); e.order.pt = r.pos(); e.order.target = r.u16();
            e.hasRally = r.u8() != 0; e.rally = r.pos();
            int np2 = r.u8();
            e.prod.clear();
            for (int q = 0; q < np2; q++) e.prod.push_back((UnitType)r.u8());
            e.prodProgress = r.f32();
            e.research = (int8_t)r.u8(); e.researchProgress = r.f32();
            e.carry = r.u16(); e.gatherTimer = r.f32();
            if (e.kind == K_HERO) { e.xp = (int)r.u32(); for (int q = 0; q < 4; q++) e.abilCd[q] = r.f32(); }
        }
    }
    // entities that vanished: dead if we can see their spot, otherwise remembered (structures) or hidden
    cw.updateVision();
    for (size_t i = 1; i < cw.ents.size(); i++) {
        Entity& e = cw.ents[i];
        if (!e.alive || (i < seen.size() && seen[i])) continue;
        bool structure = e.kind == K_BUILDING || e.kind == K_TOWER || e.kind == K_RESOURCE;
        if (structure && e.team != team && !cw.cellVisible(team, e.pos)) continue;   // last known state
        if (structure) cw.map.setBlocked(e.pos, std::max(1, unitDef(e.type).footprint), 0);
        e.alive = false;
    }
    // hero stats are public (scoreboard)
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!cw.players[i].active || hi[i].id == 0) continue;
        if (cw.ents.size() <= hi[i].id) cw.ents.resize(hi[i].id + 1);
        Entity& h = cw.ents[hi[i].id];
        h.id = hi[i].id; h.kind = K_HERO; h.type = cw.players[i].hero; h.owner = (int8_t)i; h.team = cw.players[i].team;
        if (h.radius <= 0) h.radius = unitDef(h.type).radius;
        h.level = hi[i].level; h.kills = hi[i].k; h.deaths = hi[i].de; h.assists = hi[i].as; h.lastHits = hi[i].lh;
        for (int q = 0; q < 4; q++) h.items[q] = hi[i].items[q];
        h.respawnAt = hi[i].respawn > 0 ? cw.time + hi[i].respawn : 0;
        if (h.maxHp <= 1) h.maxHp = h.hp = unitDef(h.type).hp;
    }
    // projectiles & zones
    cw.projs.clear();
    int np = r.u16();
    for (int k = 0; k < np && !r.bad; k++) {
        Projectile p;
        p.pos = r.pos(); p.dir.x = r.f32(); p.dir.y = r.f32(); p.height = r.u8() / 40.f; p.visual = r.u8(); p.team = r.u8(); p.speed = r.f32(); p.srcType = (UnitType)r.u8();
        cw.projs.push_back(p);
    }
    cw.zones.clear();
    int nz = r.u16();
    for (int k = 0; k < nz && !r.bad; k++) {
        Zone z;
        z.kind = (ZoneKind)r.u8(); z.pos = r.pos(); z.radius = r.f32(); z.start = cw.time + r.f32(); z.end = cw.time + r.f32(); z.delay = r.f32(); z.team = r.u8(); z.fired = r.u8() != 0;
        cw.zones.push_back(z);
    }
    uint32_t newest = r.u32();
    int ne = r.u16();
    for (int k = 0; k < ne && !r.bad; k++) {
        uint32_t seq;
        GameEvent e = readEvent(r, seq);
        if (r.bad) break;
        if (seq <= lastEvent) continue;
        if (!cosmetic(e.type)) lastEvent = seq;
        out.push_back(e);
    }
    // cosmetic-only gaps are not resent, so acknowledge up to what we were told when nothing reliable is pending
    if (ne == 0 || lastEvent < newest) {
        bool pendingReliable = false;
        (void)pendingReliable;
        if (ne < 250) lastEvent = std::max(lastEvent, newest);
    }
    if (cw.phase != ph) cw.phase = ph;
}

} // namespace ax
