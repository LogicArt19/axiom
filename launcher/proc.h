// proc.h : process helpers for the launcher (kept free of raylib to avoid windows.h clashes)
#pragma once
#include <string>

namespace axl {
// Runs a command line without showing a console window, waits for it, returns the exit code (-1 on failure)
int runHidden(const std::string& cmdline);
// Starts a program detached from the launcher with the given working directory
bool launchDetached(const std::string& exe, const std::string& workDir, const std::string& args = "");
// Opens a folder or URL with the system shell
void openShell(const std::string& target);
}
