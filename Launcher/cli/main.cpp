#include "installer.h"
#include "sha256.h"
#include <iostream>

static void usage() {
    std::cout << "Usage:\n"
                 "  installer-cli <input.iso|folder|default.xex> <outdir> [--app-dir DIR] [--desktop-shortcut]\n"
                 "  installer-cli --hash <file>      print SHA-256 (use it to fill manifest.json)\n";
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--hash") {
        std::string h = sha256File(fs::u8path(argv[2]));
        if (h.empty()) { std::cerr << "Cannot read file\n"; return 1; }
        std::cout << h << "\n";
        return 0;
    }
    if (argc < 3) { usage(); return 2; }
    InstallOptions o;
    o.input = fs::u8path(argv[1]);
    o.outDir = fs::u8path(argv[2]);
    o.appDir = fs::absolute(fs::u8path(argv[0])).parent_path();
    for (int i = 3; i + 1 < argc; i++)
        if (std::string(argv[i]) == "--app-dir") o.appDir = fs::u8path(argv[i + 1]);
    for (int i = 3; i < argc; i++)
        if (std::string(argv[i]) == "--desktop-shortcut") o.desktopShortcut = true;

    Hooks h;
    h.log = [](const std::string& s) { std::cout << s << "\n"; };
    h.progress = [last = -1](const std::string& s, float f) mutable {
        int pct = (int)(f * 100);
        if (pct != last) { last = pct; std::cout << "[" << pct << "%] " << s << "\n"; }
    };
    auto r = install(o, h);
    if (!r.ok) { std::cerr << "ERROR: " << r.error << "\n"; return 1; }
    std::cout << "Ready: " << r.exePath.u8string() << "\n";
    return 0;
}
