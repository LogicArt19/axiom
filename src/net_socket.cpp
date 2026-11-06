// This translation unit must not include raylib (windows.h symbol clashes).
#include "net_socket.h"
#include <cstring>
#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif

namespace ax {
namespace sock {

bool init() {
#ifdef _WIN32
    WSADATA d;
    return WSAStartup(MAKEWORD(2, 2), &d) == 0;
#else
    return true;
#endif
}

void shutdown() {
#ifdef _WIN32
    WSACleanup();
#endif
}

intptr_t open(uint16_t port) {
    intptr_t s = (intptr_t)::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if ((SOCKET)s == INVALID_SOCKET) return -1;
#else
    if (s < 0) return -1;
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    int yes = 1;
    setsockopt((decltype(::socket(0, 0, 0)))s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
    int buf = 1 << 20;
    setsockopt((decltype(::socket(0, 0, 0)))s, SOL_SOCKET, SO_RCVBUF, (const char*)&buf, sizeof buf);
    setsockopt((decltype(::socket(0, 0, 0)))s, SOL_SOCKET, SO_SNDBUF, (const char*)&buf, sizeof buf);
    if (::bind((decltype(::socket(0, 0, 0)))s, (sockaddr*)&a, sizeof a) != 0) { close(s); return -1; }
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket((SOCKET)s, FIONBIO, &nb);
#else
    fcntl((int)s, F_SETFL, fcntl((int)s, F_GETFL, 0) | O_NONBLOCK);
#endif
    return s;
}

void close(intptr_t s) {
    if (s < 0) return;
#ifdef _WIN32
    closesocket((SOCKET)s);
#else
    ::close((int)s);
#endif
}

int sendTo(intptr_t s, const Addr& to, const void* data, int len) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(to.ip);
    a.sin_port = htons(to.port);
    return (int)::sendto((decltype(::socket(0, 0, 0)))s, (const char*)data, len, 0, (sockaddr*)&a, sizeof a);
}

int recvFrom(intptr_t s, Addr& from, void* buf, int cap) {
    sockaddr_in a{};
    socklen_t al = sizeof a;
    int n = (int)::recvfrom((decltype(::socket(0, 0, 0)))s, (char*)buf, cap, 0, (sockaddr*)&a, &al);
    if (n <= 0) return -1;
    from.ip = ntohl(a.sin_addr.s_addr);
    from.port = ntohs(a.sin_port);
    return n;
}

bool resolve(const char* host, uint16_t port, Addr& out) {
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, nullptr, &hints, &res) != 0 || !res) return false;
    out.ip = ntohl(((sockaddr_in*)res->ai_addr)->sin_addr.s_addr);
    out.port = port;
    freeaddrinfo(res);
    return true;
}

std::string toString(const Addr& a) {
    char b[32];
    snprintf(b, sizeof b, "%u.%u.%u.%u:%u", (a.ip >> 24) & 255, (a.ip >> 16) & 255, (a.ip >> 8) & 255, a.ip & 255, a.port);
    return b;
}

std::vector<std::string> localAddresses() {
    std::vector<std::string> out;
#ifdef _WIN32
    char name[256];
    if (gethostname(name, sizeof name) == 0) {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        if (getaddrinfo(name, nullptr, &hints, &res) == 0) {
            for (addrinfo* p = res; p; p = p->ai_next) {
                char b[64];
                inet_ntop(AF_INET, &((sockaddr_in*)p->ai_addr)->sin_addr, b, sizeof b);
                if (strncmp(b, "127.", 4) != 0) out.push_back(b);
            }
            freeaddrinfo(res);
        }
    }
#else
    ifaddrs* ifs = nullptr;
    if (getifaddrs(&ifs) == 0) {
        for (ifaddrs* p = ifs; p; p = p->ifa_next) {
            if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
            char b[64];
            inet_ntop(AF_INET, &((sockaddr_in*)p->ifa_addr)->sin_addr, b, sizeof b);
            if (strncmp(b, "127.", 4) != 0) out.push_back(b);
        }
        freeifaddrs(ifs);
    }
#endif
    return out;
}

} // namespace sock
} // namespace ax
