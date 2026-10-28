#include "xiso.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <vector>

namespace {
constexpr uint64_t kSector = 2048;
constexpr uint64_t kOffsets[] = {0, 0x18300000ULL, 0x0FD90000ULL, 0x02080000ULL};  // raw, XGD1, XGD2, XGD3

uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

struct Ctx {
    std::ifstream in;
    uint64_t base = 0, done = 0, total = 1;
    const Hooks* h = nullptr;
    float p0 = 0, p1 = 1;
    std::string err;

    bool readAt(uint64_t off, void* dst, size_t n) {
        in.clear();
        in.seekg((std::streamoff)off);
        in.read((char*)dst, (std::streamsize)n);
        return (size_t)in.gcount() == n;
    }
    void report(const std::string& what) {
        float f = total ? (float)((double)done / (double)total) : 1.f;
        if (f > 1.f) f = 1.f;
        h->report("Extracting " + what, p0 + (p1 - p0) * f);
    }
};

bool goodName(const std::string& n) {
    return !n.empty() && n != "." && n != ".." && n.find_first_of("/\\:") == std::string::npos;
}

bool extractFile(Ctx& c, uint32_t sector, uint32_t size, const fs::path& dst, const std::string& name) {
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) { c.err = "Cannot write " + dst.u8string(); return false; }
    std::vector<char> buf(1 << 20);
    uint64_t off = c.base + (uint64_t)sector * kSector;
    uint64_t left = size;
    c.report(name);
    while (left) {
        if (c.h->isCancelled()) { c.err = "Cancelled."; return false; }
        size_t n = (size_t)std::min<uint64_t>(left, buf.size());
        if (!c.readAt(off, buf.data(), n)) { c.err = "Unexpected end of disc image in " + name; return false; }
        out.write(buf.data(), (std::streamsize)n);
        if (!out) { c.err = "Write failed (disk full?) for " + name; return false; }
        off += n; left -= n; c.done += n;
        c.report(name);
    }
    return true;
}

bool walk(Ctx& c, uint32_t sector, uint32_t size, const fs::path& dest, int depth) {
    if (depth > 64) { c.err = "Directory nesting too deep (corrupt image?)"; return false; }
    std::error_code ec;
    fs::create_directories(dest, ec);
    if (size == 0) return true;
    if (size > (64u << 20)) { c.err = "Directory table too large (corrupt image?)"; return false; }
    std::vector<uint8_t> dir(size);
    if (!c.readAt(c.base + (uint64_t)sector * kSector, dir.data(), size)) {
        c.err = "Cannot read directory table"; return false;
    }
    if (size >= 2 && rd16(dir.data()) == 0xFFFF) return true;   // empty directory

    std::vector<uint32_t> stack{0};
    std::set<uint32_t> seen;
    while (!stack.empty()) {
        uint32_t o = stack.back(); stack.pop_back();
        if (!seen.insert(o).second) continue;                    // loop guard
        if ((uint64_t)o + 14 > size) { c.err = "Corrupt directory entry"; return false; }
        const uint8_t* e = dir.data() + o;
        uint16_t left = rd16(e), right = rd16(e + 2);
        uint32_t sec = rd32(e + 4), sz = rd32(e + 8);
        uint8_t attr = e[12], nl = e[13];
        if ((uint64_t)o + 14 + nl > size) { c.err = "Corrupt directory entry"; return false; }
        std::string name((const char*)e + 14, nl);
        if (left && left != 0xFFFF) stack.push_back((uint32_t)left * 4);
        if (right && right != 0xFFFF) stack.push_back((uint32_t)right * 4);
        if (!goodName(name)) { c.h->say("Skipping entry with unsafe name"); continue; }
        fs::path target = dest / fs::u8path(name);
        if (attr & 0x10) {
            if (!walk(c, sec, sz, target, depth + 1)) return false;
        } else {
            if (!extractFile(c, sec, sz, target, name)) return false;
        }
    }
    return true;
}
}  // namespace

bool extractXiso(const fs::path& iso, const fs::path& dest, const Hooks& h, float p0, float p1, std::string& err) {
    Ctx c;
    c.h = &h; c.p0 = p0; c.p1 = p1;
    c.in.open(iso, std::ios::binary);
    if (!c.in) { err = "Cannot open " + iso.u8string(); return false; }
    c.in.seekg(0, std::ios::end);
    uint64_t isoSize = (uint64_t)c.in.tellg();

    bool found = false;
    uint8_t hdr[0x20];
    for (uint64_t off : kOffsets) {
        if (off + 0x10000 + sizeof(hdr) > isoSize) continue;
        if (c.readAt(off + 0x10000, hdr, sizeof(hdr)) && memcmp(hdr, "MICROSOFT*XBOX*MEDIA", 20) == 0) {
            c.base = off; found = true; break;
        }
    }
    if (!found) { err = "Not a valid Xbox disc image (XDVDFS signature not found)."; return false; }
    c.total = isoSize - c.base;
    char b[32];
    snprintf(b, sizeof b, "%llX", (unsigned long long)c.base);
    h.say(std::string("Disc image layout offset: 0x") + b);

    bool ok = walk(c, rd32(hdr + 0x14), rd32(hdr + 0x18), dest, 0);
    if (!ok) err = c.err;
    return ok;
}
