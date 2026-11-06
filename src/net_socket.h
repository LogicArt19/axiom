// net_socket.h : minimal cross-platform non-blocking UDP (kept free of raylib/windows.h clashes)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ax {
namespace sock {

struct Addr {
    uint32_t ip = 0;     // host byte order
    uint16_t port = 0;
    bool operator==(const Addr& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const Addr& o) const { return !(*this == o); }
};

bool init();
void shutdown();
intptr_t open(uint16_t port);     // bind to port (0 = any). returns -1 on failure
void close(intptr_t s);
int sendTo(intptr_t s, const Addr& a, const void* data, int len);
int recvFrom(intptr_t s, Addr& from, void* buf, int cap);   // returns bytes or -1 if nothing
bool resolve(const char* host, uint16_t port, Addr& out);
std::string toString(const Addr& a);
std::vector<std::string> localAddresses();

} // namespace sock
} // namespace ax
