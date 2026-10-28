#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace fs = std::filesystem;

// Callbacks the GUI/CLI provides. All optional.
struct Hooks {
    std::function<void(const std::string&, float)> progress;  // step text, 0..1
    std::function<void(const std::string&)> log;
    std::function<bool()> cancelled;                          // return true to abort

    void report(const std::string& s, float f) const { if (progress) progress(s, f); }
    void say(const std::string& s) const { if (log) log(s); }
    bool isCancelled() const { return cancelled && cancelled(); }
};

std::string toLower(std::string s);
// Case-insensitive path lookup under root ("Media/Data.pak"). Empty path if not found.
fs::path resolveCI(const fs::path& root, const std::string& rel);
uint64_t treeSize(const fs::path& p);   // file or directory, total bytes
// Recursively copies file/dir (overwrites). onBytes gets bytes copied per file.
bool copyTree(const fs::path& src, const fs::path& dst, const Hooks& h, std::string& err,
              const std::function<void(uint64_t)>& onBytes);
