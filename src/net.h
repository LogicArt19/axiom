// net.h : authoritative listen-server + predicted client over UDP
#pragma once
#include "world.h"
#include "net_socket.h"
#include <deque>
#include <map>

namespace ax {

class BotSystem;

constexpr uint32_t NET_MAGIC = 0x41584D31;   // 'AXM1'
constexpr uint32_t NET_VERSION = 3;

struct RemoteClient {
    bool active = false;
    sock::Addr addr;
    int playerId = -1;
    std::string name;
    std::deque<UserCmd> queue;
    uint32_t lastRecvCmdTick = 0;
    uint32_t lastProcessedTick = 0;
    float procTime = 0;
    UserCmd lastCmd{};
    uint32_t ackEventSeq = 0;
    double lastHeard = 0;
    int pingMs = 0;
    std::map<uint32_t, bool> buyNonces;
};

class NetServer {
public:
    bool running = false;
    int port = 0;
    std::vector<RemoteClient> clients;
    std::vector<std::string> log;

    bool start(int port);
    void stop(World& w);
    void poll(World& w);                                       // receive packets
    bool takeCmd(World& w, int playerId, UserCmd& out);        // cmd for this tick
    void afterStep(World& w);                                  // record processing times
    void recordEvents(const std::vector<GameEvent>& evs);
    void sendSnapshots(World& w);
    int humanCount() const;
private:
    intptr_t sock_ = -1;
    uint32_t snapSeq = 0;
    uint32_t eventSeq = 0;                // seq of the last recorded event
    std::deque<std::pair<uint32_t, GameEvent>> eventLog;
    void handleConnect(World& w, const sock::Addr& from, const uint8_t* d, int n);
    int assignPlayer(World& w, const char* name);
};

struct SnapPlayer { Vector3 pos; float yaw, pitch, duck; bool alive; Vector3 vel; };
struct SnapRecord { uint32_t tick; SnapPlayer p[MAX_PLAYERS]; bool valid[MAX_PLAYERS]; };

class NetClient {
public:
    enum State { IDLE, CONNECTING, CONNECTED, FAILED, DISCONNECTED };
    State state = IDLE;
    std::string reason;
    int localId = -1;
    int rttMs = 0;
    float lossPct = 0;
    GameMode mode = MODE_COMPETITIVE;

    bool connect(const char* host, int port, const char* name);
    void disconnect();
    // returns true when the world was (re)initialised (map changed)
    bool update(World& cw, std::vector<GameEvent>& eventsOut);
    uint32_t nextCmdTick();
    void sendCmd(const UserCmd& c);
    void sendBuy(int item);
    void predict(World& cw, const UserCmd& c);
    void interpolate(World& cw);
    float serverTimeEstimate() const;
private:
    intptr_t sock_ = -1;
    sock::Addr server;
    double connectStart = 0, lastHeard = 0, lastConnectSend = 0;
    std::string name;
    uint32_t lastSnapSeq = 0;
    uint32_t lastEventSeq = 0;
    uint32_t latestServerTick = 0;
    double latestRecvTime = 0;
    std::deque<UserCmd> pending;          // unacknowledged commands
    std::map<uint32_t, double> sendTimes;
    std::deque<SnapRecord> snaps;
    uint32_t lastCmdTick = 0;
    uint32_t buyNonce = 1;
    int snapsReceived = 0, snapsExpected = 0;
    bool initWorld = false;
    void applySnapshot(World& cw, const uint8_t* d, int n, std::vector<GameEvent>& eventsOut);
};

} // namespace ax
