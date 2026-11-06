#include "proc.h"
#include <cstdlib>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#endif

namespace axl {

#ifdef _WIN32
int runHidden(const std::string& cmdline) {
    STARTUPINFOA si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    // executed directly (no shell): curl.exe / tar.exe are found through PATH (System32 on Windows 10+)
    std::vector<char> buf(cmdline.begin(), cmdline.end());
    buf.push_back(0);
    if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

bool launchDetached(const std::string& exe, const std::string& workDir, const std::string& args) {
    STARTUPINFOA si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    std::string cmd = "\"" + exe + "\" " + args;
    std::vector<char> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    BOOL ok = CreateProcessA(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, workDir.c_str(), &si, &pi);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

void openShell(const std::string& target) { ShellExecuteA(nullptr, "open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
#else
int runHidden(const std::string& cmdline) {
    int r = std::system(cmdline.c_str());
    if (r == -1) return -1;
    return WIFEXITED(r) ? WEXITSTATUS(r) : -1;
}

bool launchDetached(const std::string& exe, const std::string& workDir, const std::string& args) {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        setsid();
        if (chdir(workDir.c_str()) != 0) _exit(127);
        std::string cmd = "\"" + exe + "\" " + args;
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        _exit(127);
    }
    return true;
}

void openShell(const std::string& target) {
#ifdef __APPLE__
    std::string c = "open \"" + target + "\" &";
#else
    std::string c = "xdg-open \"" + target + "\" >/dev/null 2>&1 &";
#endif
    int r = std::system(c.c_str());
    (void)r;
}
#endif

} // namespace axl
