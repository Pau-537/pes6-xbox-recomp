#pragma once
#include "common.h"

struct InstallOptions {
    fs::path input;    // .iso, extracted folder, or default.xex
    fs::path outDir;   // where the playable game ends up
    fs::path appDir;   // folder holding manifest.txt and payload/
    bool desktopShortcut = false;   // also put a shortcut on the Desktop (Windows)
};
struct InstallResult {
    bool ok = false;
    std::string error;
    fs::path exePath;
    std::string args;  // arguments the game must be started with (UTF-8)
};
InstallResult install(const InstallOptions& opt, const Hooks& hooks);
