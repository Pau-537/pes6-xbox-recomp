#pragma once
#include "common.h"
#include <vector>

struct CopyJob { std::string from, to; };
struct VersionEntry {
    std::string id, xexSha256;
    std::vector<std::string> requiredFiles;
    std::vector<CopyJob> copy;
};
struct Manifest {
    std::string title, gameExe, gameRoot = "game";
    std::vector<VersionEntry> versions;
    const VersionEntry* findByHash(const std::string& sha) const;
};
bool loadManifest(const fs::path& file, Manifest& out, std::string& err);
