#include "manifest.h"
#include <fstream>

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

const VersionEntry* Manifest::findByHash(const std::string& sha) const {
    std::string s = toLower(sha);
    for (auto& v : versions) if (toLower(v.xexSha256) == s) return &v;
    return nullptr;
}

bool loadManifest(const fs::path& file, Manifest& out, std::string& err) {
    std::ifstream in(file, std::ios::binary);
    if (!in) { err = "manifest.txt not found next to the installer."; return false; }
    std::string line;
    int ln = 0;
    int cur = -1;
    auto bad = [&](const std::string& m) { err = "manifest.txt line " + std::to_string(ln) + ": " + m; return false; };
    while (std::getline(in, line)) {
        ln++;
        if (ln == 1 && line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF) line.erase(0, 3);   // UTF-8 BOM
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {
            if (line.back() != ']') return bad("missing ]");
            std::string inner = trim(line.substr(1, line.size() - 2));
            if (inner.rfind("version", 0) != 0) return bad("expected [version <name>]");
            out.versions.emplace_back();
            cur = (int)out.versions.size() - 1;
            out.versions[cur].id = trim(inner.substr(7));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) return bad("expected key = value");
        std::string key = toLower(trim(line.substr(0, eq))), val = trim(line.substr(eq + 1));
        if (cur < 0) {
            if (key == "title") out.title = val;
            else if (key == "game_exe") out.gameExe = val;
            else if (key == "game_root") out.gameRoot = val;
            else return bad("unknown setting '" + key + "'");
        } else {
            VersionEntry& v = out.versions[cur];
            if (key == "xex_sha256") v.xexSha256 = val;
            else if (key == "require") v.requiredFiles.push_back(val);
            else if (key == "copy") {
                size_t arrow = val.find("->");
                if (arrow == std::string::npos) return bad("copy needs  from -> to");
                v.copy.push_back({trim(val.substr(0, arrow)), trim(val.substr(arrow + 2))});
            } else return bad("unknown setting '" + key + "'");
        }
    }
    if (out.title.empty()) out.title = "Game";
    if (out.gameExe.empty()) { err = "manifest.txt: game_exe is not set."; return false; }
    if (out.versions.empty()) { err = "manifest.txt: no [version ...] block."; return false; }
    return true;
}
