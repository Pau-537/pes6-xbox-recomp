#include "installer.h"
#include "manifest.h"
#include "sha256.h"
#include "xiso.h"
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif
#include <utility>
#include <vector>

namespace {
struct TempGuard {
    fs::path p;
    ~TempGuard() { if (!p.empty()) { std::error_code ec; fs::remove_all(p, ec); } }
};
InstallResult fail(const std::string& m) { InstallResult r; r.error = m; return r; }

// Name of the settings file the game reads (placed next to the game exe).
const char* kConfigName = "pes6.toml";

std::string trimS(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

// value is raw TOML text: "\"xenos\"" for a string, "1" for a number.
// overwrite=false keeps whatever the user already put there.
struct TomlKey { std::string key, value; bool overwrite; };

// Sets top-level keys in a TOML file. Creates the file if missing; if it exists, all other
// settings are preserved. Missing keys are added at the top (top-level keys must precede [tables]).
void setTomlKeys(const fs::path& file, const std::vector<TomlKey>& kv) {
    std::vector<std::string> lines;
    {
        std::ifstream in(file);
        std::string l;
        while (std::getline(in, l)) { if (!l.empty() && l.back() == '\r') l.pop_back(); lines.push_back(l); }
    }
    std::vector<bool> done(kv.size(), false);
    bool inTable = false;
    for (auto& l : lines) {
        std::string t = trimS(l);
        if (!t.empty() && t[0] == '[') inTable = true;
        if (inTable || t.empty() || t[0] == '#') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trimS(t.substr(0, eq));
        for (size_t i = 0; i < kv.size(); i++)
            if (key == kv[i].key) {
                if (kv[i].overwrite) l = key + " = " + kv[i].value;
                done[i] = true;
            }
    }
    std::vector<std::string> head;
    for (size_t i = 0; i < kv.size(); i++)
        if (!done[i]) head.push_back(kv[i].key + " = " + kv[i].value);
    lines.insert(lines.begin(), head.begin(), head.end());
    std::ofstream out(file, std::ios::trunc);
    for (auto& l : lines) out << l << "\n";
}

std::string safeFileName(std::string s) {
    for (auto& c : s) if (std::string("<>:\"/\\|?*").find(c) != std::string::npos) c = '_';
    return s.empty() ? std::string("Game") : s;
}

#ifdef _WIN32
bool createShortcut(const fs::path& lnk, const fs::path& target, const std::wstring& args, const fs::path& workDir) {
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = false;
    IShellLinkW* link = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&link))) {
        link->SetPath(target.c_str());
        link->SetArguments(args.c_str());
        link->SetWorkingDirectory(workDir.c_str());
        link->SetIconLocation(target.c_str(), 0);
        IPersistFile* pf = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, (void**)&pf))) {
            ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
            pf->Release();
        }
        link->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return ok;
}
#endif
}  // namespace

InstallResult install(const InstallOptions& o, const Hooks& userHooks) {
    std::error_code ec;
    fs::create_directories(o.outDir, ec);
    if (ec) return fail("Cannot create output folder: " + ec.message());

    std::ofstream logf(o.outDir / "install.log", std::ios::app);
    auto userLog = userHooks.log;
    Hooks h = userHooks;
    h.log = [&logf, userLog](const std::string& s) {
        logf << s << "\n"; logf.flush();
        if (userLog) userLog(s);
    };

    // --- manifest + payload sanity ---
    Manifest man; std::string err;
    if (!loadManifest(o.appDir / "manifest.txt", man, err)) return fail(err);
    fs::path payload = o.appDir / "payload";
    if (!fs::exists(payload / fs::u8path(man.gameExe)))
        return fail("The installer package is incomplete: payload/" + man.gameExe + " is missing.");

    // --- classify input ---
    if (!fs::exists(o.input, ec)) return fail("Input not found: " + o.input.u8string());
    bool isIso = false;
    fs::path root;
    if (fs::is_directory(o.input, ec)) root = o.input;
    else if (toLower(o.input.filename().u8string()) == "default.xex") root = o.input.parent_path();
    else isIso = true;
    h.say("Input: " + o.input.u8string() + (isIso ? " (disc image)" : " (folder)"));

    // --- disk space ---
    uint64_t need = (isIso ? fs::file_size(o.input, ec) : treeSize(root)) + treeSize(payload) + (16u << 20);
    auto sp = fs::space(o.outDir, ec);
    if (!ec && sp.available < need)
        return fail("Not enough free disk space. Need about " + std::to_string(need >> 20) + " MB.");

    TempGuard guard;
    float copyStart = 0.10f;
    if (isIso) {
        fs::path stage = o.outDir / "_extract";
        fs::remove_all(stage, ec);
        guard.p = stage;
        if (!extractXiso(o.input, stage, h, 0.02f, 0.50f, err)) return fail(err);
        root = stage;
        copyStart = 0.55f;
    }
    if (h.isCancelled()) return fail("Cancelled.");

    // --- verify version ---
    h.report("Verifying game version...", copyStart - 0.03f);
    fs::path xex = resolveCI(root, "default.xex");
    if (xex.empty()) return fail("default.xex not found. Is this the right game?");
    std::string sha = sha256File(xex, h.cancelled);
    if (sha.empty()) return fail(h.isCancelled() ? "Cancelled." : "Could not read default.xex.");
    h.say("default.xex SHA-256: " + sha);
    const VersionEntry* ver = man.findByHash(sha);
    if (!ver)
        return fail("This version of the game is not supported (default.xex hash " + sha.substr(0, 12) +
                    "...). The recompilation targets a specific release / title update.");
    h.say("Detected version: " + ver->id);
    for (auto& f : ver->requiredFiles)
        if (resolveCI(root, f).empty()) return fail("Missing required file: " + f);

    // --- install game data ---
    const float copyEnd = 0.93f;
    uint64_t total = 1, done = 0;
    for (auto& j : ver->copy) total += treeSize(resolveCI(root, j.from));
    for (auto& j : ver->copy) {
        if (h.isCancelled()) return fail("Cancelled.");
        fs::path src = resolveCI(root, j.from);
        if (src.empty()) return fail("Missing file to install: " + j.from);
        fs::path dst = o.outDir / fs::u8path(j.to);
        std::string label = "Installing " + j.from + "...";
        auto onBytes = [&](uint64_t n) {
            done += n;
            h.report(label, copyStart + (copyEnd - copyStart) * (float)((double)done / (double)total));
        };
        uint64_t sz = treeSize(src);
        bool moved = false;
        if (isIso) {                       // temp data lives on the same volume: rename is instant
            fs::create_directories(dst.parent_path(), ec);
            fs::rename(src, dst, ec);
            moved = !ec;
            if (moved) onBytes(sz);
        }
        if (!moved && !copyTree(src, dst, h, err, onBytes)) return fail(err);
        h.say("Installed " + j.from + " -> " + j.to);
    }

    // --- recompiled exe + config ---
    h.report("Installing game executable...", 0.95f);
    if (!copyTree(payload, o.outDir, h, err, nullptr)) return fail(err);
    // --- settings file the game reads (pes6.toml) ---
    h.report("Writing settings...", 0.97f);
    std::string dataDir = man.gameRoot;
    for (auto& c : dataDir) if (c == '\\') c = '/';
    setTomlKeys(o.outDir / kConfigName, {{"gpu_plugin", "\"xenos\"", true},
                                          {"resolution_scale", "1", false}});
    const std::string gameArgs = "--game_data_root=game";

    fs::path exe = o.outDir / fs::u8path(man.gameExe);
    if (!fs::exists(exe)) return fail("Game executable missing after install.");
#ifdef _WIN32
    {
        fs::path exeAbs = fs::absolute(exe, ec), workAbs = fs::absolute(o.outDir, ec);
        std::wstring wargs = fs::u8path(gameArgs).wstring();
        fs::path lnkName = fs::u8path(safeFileName(man.title) + ".lnk");
        if (createShortcut(workAbs / lnkName, exeAbs, wargs, workAbs))
            h.say("Created shortcut: " + lnkName.u8string());
        else
            h.say("Warning: could not create the shortcut in the install folder.");
        if (o.desktopShortcut) {
            wchar_t desk[MAX_PATH] = {0};
            if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desk)) &&
                createShortcut(fs::path(desk) / lnkName, exeAbs, wargs, workAbs))
                h.say("Created desktop shortcut.");
            else
                h.say("Warning: could not create the desktop shortcut.");
        }
    }
#endif
    h.report("Done", 1.0f);
    h.say("Install finished.");
    InstallResult r; r.ok = true; r.exePath = exe; r.args = gameArgs;
    return r;
}
