// net.h : listen/dedicated server + client. Authoritative simulation on the server,
// fog-filtered snapshots (anti map-hack) and reliable command / event streams.
#pragma once
#include "sim.h"
#include "net_socket.h"
#include <deque>
#include <map>

namespace ax {

constexpr uint32_t NET_MAGIC = 0x41584D32;   // 'AXM2'
constexpr uint32_t NET_VERSION = 21;

struct RemoteClient {
    bool active = false;
    sock::Addr addr;
    int player = -1;
    std::string name;
    uint32_t lastCmdSeq = 0;
    uint32_t ackEvent = 0;
    double lastHeard = 0;
    int ping = 0;
};

class NetServer {
public:
    bool running = false;
    int port = 0;
    std::vector<RemoteClient> clients;
    std::vector<std::string> log;
    bool start(int port);
    void stop(World& w);
    void poll(World& w);
    void recordEvents(const std::vector<GameEvent>& evs);
    void send(World& w);
    int humanCount() const;
private:
    intptr_t sock_ = -1;
    uint32_t snapSeq = 0, eventSeq = 0;
    std::deque<std::pair<uint32_t, GameEvent>> eventLog;
};

class NetClient {
public:
    enum State { IDLE, CONNECTING, CONNECTED, FAILED, DISCONNECTED };
    State state = IDLE;
    std::string reason;
    int localPlayer = -1;
    int rttMs = 0;
    bool connect(const char* host, int port, const char* name, int hero);
    void disconnect();
    bool update(World& cw, std::vector<GameEvent>& eventsOut);   // true when the world was just initialised
    void sendCommand(const Command& c);
    float alpha() const;
private:
    intptr_t sock_ = -1;
    sock::Addr server;
    std::string name;
    int hero = 0;
    double connectStart = 0, lastHeard = 0, lastSend = 0, lastSnapTime = 0, snapInterval = 1.0 / 15;
    uint32_t lastSnap = 0, lastEvent = 0, cmdSeq = 0;
    std::deque<std::pair<uint32_t, std::vector<uint8_t>>> unacked;
    std::map<uint32_t, double> sendTimes;
    bool mapChanged = false;
    void applySnapshot(World& cw, const uint8_t* d, int n, std::vector<GameEvent>& out);
    void flush();
};

} // namespace ax
