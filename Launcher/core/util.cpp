#include "common.h"
#include <algorithm>
#include <cctype>

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

fs::path resolveCI(const fs::path& root, const std::string& rel) {
    fs::path cur = root;
    std::string comp;
    auto step = [&](const std::string& c) -> bool {
        if (c.empty()) return true;
        std::error_code ec;
        fs::path direct = cur / fs::u8path(c);
        if (fs::exists(direct, ec)) { cur = direct; return true; }
        std::string want = toLower(c);
        for (fs::directory_iterator it(cur, ec), end; !ec && it != end; it.increment(ec)) {
            if (toLower(it->path().filename().u8string()) == want) { cur = it->path(); return true; }
        }
        return false;
    };
    for (char ch : rel) {
        if (ch == '/' || ch == '\\') { if (!step(comp)) return {}; comp.clear(); }
        else comp += ch;
    }
    if (!step(comp)) return {};
    return cur;
}

uint64_t treeSize(const fs::path& p) {
    std::error_code ec;
    if (fs::is_regular_file(p, ec)) return fs::file_size(p, ec);
    uint64_t total = 0;
    for (fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2)) total += it->file_size(e2);
    }
    return total;
}

bool copyTree(const fs::path& src, const fs::path& dst, const Hooks& h, std::string& err,
              const std::function<void(uint64_t)>& onBytes) {
    std::error_code ec;
    auto copyOne = [&](const fs::path& s, const fs::path& d) -> bool {
        if (h.isCancelled()) { err = "Cancelled."; return false; }
        std::error_code e;
        fs::create_directories(d.parent_path(), e);
        fs::copy_file(s, d, fs::copy_options::overwrite_existing, e);
        if (e) { err = "Copy failed (" + s.filename().u8string() + "): " + e.message(); return false; }
        if (onBytes) onBytes(fs::file_size(s, e));
        return true;
    };
    if (fs::is_regular_file(src, ec)) return copyOne(src, dst);
    fs::create_directories(dst, ec);
    for (fs::recursive_directory_iterator it(src, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        fs::path rel = fs::relative(it->path(), src, e2);
        fs::path target = dst / rel;
        if (it->is_directory(e2)) fs::create_directories(target, e2);
        else if (!copyOne(it->path(), target)) return false;
    }
    if (ec) { err = "Cannot read " + src.u8string() + ": " + ec.message(); return false; }
    return true;
}
