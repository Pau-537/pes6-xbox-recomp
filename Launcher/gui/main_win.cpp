// Native Windows GUI (no Qt). Works with MSVC and MinGW.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <atomic>
#include <memory>
#include <string>
#include "installer.h"
#include "manifest.h"

#ifdef _MSC_VER
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace {
enum { ID_IN = 101, ID_BROWSE_ISO, ID_BROWSE_DIR, ID_OUT, ID_BROWSE_OUT, ID_INSTALL, ID_CANCEL,
       ID_PLAY, ID_OPEN, ID_STATUS, ID_BAR, ID_LOG, ID_TITLE, ID_DESKTOP };
constexpr UINT WM_PROGRESS = WM_APP + 1, WM_LOG = WM_APP + 2, WM_DONE = WM_APP + 3;

struct DoneInfo { bool ok; std::wstring msg, exe, args; };
struct Job { HWND hwnd; InstallOptions opt; };

struct App {
    HWND hwnd = nullptr;
    HFONT font = nullptr, titleFont = nullptr;
    HANDLE thread = nullptr;
    std::atomic<bool> cancel{false};
    std::wstring title, appDir, exe, args, outDone;
} g;

std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
std::string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
std::wstring getText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, 0);
    GetWindowTextW(h, &s[0], n + 1);
    s.resize(n);
    return s;
}
std::wstring trimW(std::wstring s) {
    size_t a = s.find_first_not_of(L" \t\r\n\"");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n\"");
    return s.substr(a, b - a + 1);
}
HWND item(int id) { return GetDlgItem(g.hwnd, id); }
void show(int id, bool v) { ShowWindow(item(id), v ? SW_SHOW : SW_HIDE); }

HWND make(HWND p, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id,
          DWORD ex = 0) {
    return CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, p,
                           (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
}

void appendLog(const std::wstring& line) {
    HWND e = item(ID_LOG);
    int len = GetWindowTextLengthW(e);
    SendMessageW(e, EM_SETSEL, len, len);
    std::wstring t = line + L"\r\n";
    SendMessageW(e, EM_REPLACESEL, FALSE, (LPARAM)t.c_str());
}

void refresh() {
    std::wstring in = trimW(getText(item(ID_IN))), out = trimW(getText(item(ID_OUT)));
    bool ok = !in.empty() && !out.empty() && GetFileAttributesW(in.c_str()) != INVALID_FILE_ATTRIBUTES;
    EnableWindow(item(ID_INSTALL), ok);
}

void setBusy(bool busy) {
    for (int id : {ID_IN, ID_BROWSE_ISO, ID_BROWSE_DIR, ID_OUT, ID_BROWSE_OUT, ID_DESKTOP}) EnableWindow(item(id), !busy);
    if (busy) EnableWindow(item(ID_INSTALL), FALSE); else refresh();
    show(ID_BAR, busy);
    show(ID_CANCEL, busy);
    EnableWindow(item(ID_CANCEL), TRUE);
    show(ID_PLAY, false);
    show(ID_OPEN, false);
}

std::wstring pickFolder(const wchar_t* title) {
    BROWSEINFOW bi{};
    bi.hwndOwner = g.hwnd;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return L"";
    wchar_t p[MAX_PATH];
    std::wstring r;
    if (SHGetPathFromIDListW(pidl, p)) r = p;
    CoTaskMemFree(pidl);
    return r;
}

DWORD WINAPI worker(LPVOID p) {
    std::unique_ptr<Job> job((Job*)p);
    HWND hw = job->hwnd;
    Hooks h;
    h.progress = [hw, lastPct = -1, lastStep = std::string()](const std::string& s, float f) mutable {
        int pct = (int)(f * 100);
        if (pct == lastPct && s == lastStep) return;      // don't flood the UI thread
        lastPct = pct; lastStep = s;
        PostMessageW(hw, WM_PROGRESS, (WPARAM)pct, (LPARAM) new std::wstring(widen(s)));
    };
    h.log = [hw](const std::string& s) { PostMessageW(hw, WM_LOG, 0, (LPARAM) new std::wstring(widen(s))); };
    h.cancelled = [] { return g.cancel.load(); };
    InstallResult r = install(job->opt, h);
    auto* d = new DoneInfo{r.ok, widen(r.error), r.exePath.wstring(), widen(r.args)};
    PostMessageW(hw, WM_DONE, 0, (LPARAM)d);
    return 0;
}

void startInstall() {
    g.cancel = false;
    auto* job = new Job;
    job->hwnd = g.hwnd;
    job->opt.input = fs::path(trimW(getText(item(ID_IN))));
    job->opt.outDir = fs::path(trimW(getText(item(ID_OUT))));
    job->opt.appDir = fs::path(g.appDir);
    job->opt.desktopShortcut = SendMessageW(item(ID_DESKTOP), BM_GETCHECK, 0, 0) == BST_CHECKED;
    g.outDone = trimW(getText(item(ID_OUT)));
    SetWindowTextW(item(ID_LOG), L"");
    SetWindowTextW(item(ID_STATUS), L"Starting...");
    SendMessageW(item(ID_BAR), PBM_SETPOS, 0, 0);
    setBusy(true);
    g.thread = CreateThread(nullptr, 0, worker, job, 0, nullptr);
    if (!g.thread) {
        delete job;
        setBusy(false);
        MessageBoxW(g.hwnd, L"Could not start the installer thread.", g.title.c_str(), MB_ICONERROR);
    }
}

void joinWorker() {
    if (g.thread) { WaitForSingleObject(g.thread, 20000); CloseHandle(g.thread); g.thread = nullptr; }
}

BOOL CALLBACK setFont(HWND h, LPARAM f) { SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE); return TRUE; }

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        g.hwnd = hwnd;
        HINSTANCE hi = GetModuleHandleW(nullptr); (void)hi;
        NONCLIENTMETRICSW ncm{}; ncm.cbSize = sizeof ncm;
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
        g.font = CreateFontIndirectW(&ncm.lfMessageFont);
        LOGFONTW tf = ncm.lfMessageFont; tf.lfHeight = tf.lfHeight * 3 / 2; tf.lfWeight = FW_BOLD;
        g.titleFont = CreateFontIndirectW(&tf);

        wchar_t docs[MAX_PATH] = {0};
        std::wstring defOut;
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, docs)))
            defOut = std::wstring(docs) + L"\\" + g.title;

        make(hwnd, L"STATIC", (L"Install " + g.title).c_str(), 0, 16, 12, 568, 30, ID_TITLE);
        make(hwnd, L"STATIC", L"Select your own copy of the game: a disc image (.iso), an extracted folder, "
                              L"or its default.xex. You can also drag and drop it onto this window.",
             0, 16, 46, 568, 34, 0);
        make(hwnd, L"STATIC", L"Game files", 0, 16, 90, 300, 18, 0);
        make(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 16, 110, 380, 24, ID_IN, WS_EX_CLIENTEDGE);
        make(hwnd, L"BUTTON", L"ISO / XEX...", WS_TABSTOP, 404, 109, 100, 26, ID_BROWSE_ISO);
        make(hwnd, L"BUTTON", L"Folder...", WS_TABSTOP, 510, 109, 74, 26, ID_BROWSE_DIR);
        make(hwnd, L"STATIC", L"Install to", 0, 16, 146, 300, 18, 0);
        make(hwnd, L"EDIT", defOut.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, 16, 166, 488, 24, ID_OUT, WS_EX_CLIENTEDGE);
        make(hwnd, L"BUTTON", L"Browse...", WS_TABSTOP, 510, 165, 74, 26, ID_BROWSE_OUT);
        make(hwnd, L"BUTTON", L"Create a desktop shortcut", WS_TABSTOP | BS_AUTOCHECKBOX, 16, 196, 568, 20, ID_DESKTOP);
        SendMessageW(item(ID_DESKTOP), BM_SETCHECK, BST_CHECKED, 0);
        make(hwnd, L"BUTTON", L"Install", WS_TABSTOP | BS_DEFPUSHBUTTON, 16, 222, 568, 36, ID_INSTALL);
        make(hwnd, L"STATIC", L"", SS_ENDELLIPSIS, 16, 266, 568, 20, ID_STATUS);
        make(hwnd, PROGRESS_CLASSW, L"", PBS_SMOOTH, 16, 288, 568, 22, ID_BAR);
        make(hwnd, L"BUTTON", L"Cancel", WS_TABSTOP, 484, 316, 100, 28, ID_CANCEL);
        make(hwnd, L"BUTTON", L"Play", WS_TABSTOP, 16, 316, 120, 32, ID_PLAY);
        make(hwnd, L"BUTTON", L"Open folder", WS_TABSTOP, 144, 316, 120, 32, ID_OPEN);
        make(hwnd, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 16, 356, 568, 74, ID_LOG,
             WS_EX_CLIENTEDGE);
        SendMessageW(item(ID_LOG), EM_LIMITTEXT, 2000000, 0);
        SendMessageW(item(ID_BAR), PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        EnumChildWindows(hwnd, setFont, (LPARAM)g.font);
        SendMessageW(item(ID_TITLE), WM_SETFONT, (WPARAM)g.titleFont, TRUE);
        show(ID_BAR, false); show(ID_CANCEL, false); show(ID_PLAY, false); show(ID_OPEN, false);
        refresh();
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        if ((id == ID_IN || id == ID_OUT) && code == EN_CHANGE) { refresh(); return 0; }
        if (code != BN_CLICKED) break;
        switch (id) {
        case ID_BROWSE_ISO: {
            wchar_t buf[MAX_PATH * 4] = {0};
            OPENFILENAMEW o{};
            o.lStructSize = sizeof o;
            o.hwndOwner = hwnd;
            o.lpstrFilter = L"Game files (*.iso, default.xex)\0*.iso;default.xex\0All files (*.*)\0*.*\0";
            o.lpstrFile = buf;
            o.nMaxFile = (DWORD)(sizeof buf / sizeof buf[0]);
            o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
            if (GetOpenFileNameW(&o)) SetWindowTextW(item(ID_IN), buf);
            return 0;
        }
        case ID_BROWSE_DIR: {
            std::wstring d = pickFolder(L"Select the extracted game folder");
            if (!d.empty()) SetWindowTextW(item(ID_IN), d.c_str());
            return 0;
        }
        case ID_BROWSE_OUT: {
            std::wstring d = pickFolder(L"Select the install folder");
            if (!d.empty()) SetWindowTextW(item(ID_OUT), d.c_str());
            return 0;
        }
        case ID_INSTALL: startInstall(); return 0;
        case ID_CANCEL:
            g.cancel = true;
            EnableWindow(item(ID_CANCEL), FALSE);
            SetWindowTextW(item(ID_STATUS), L"Cancelling...");
            return 0;
        case ID_PLAY:
            ShellExecuteW(hwnd, L"open", g.exe.c_str(), g.args.c_str(), fs::path(g.exe).parent_path().c_str(), SW_SHOWNORMAL);
            return 0;
        case ID_OPEN:
            ShellExecuteW(hwnd, L"open", g.outDone.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        break;
    }
    case WM_DROPFILES: {
        HDROP d = (HDROP)w;
        UINT n = DragQueryFileW(d, 0, nullptr, 0);
        std::wstring s(n + 1, 0);
        DragQueryFileW(d, 0, &s[0], n + 1);
        s.resize(n);
        DragFinish(d);
        if (!g.thread) SetWindowTextW(item(ID_IN), s.c_str());
        return 0;
    }
    case WM_PROGRESS: {
        std::unique_ptr<std::wstring> s((std::wstring*)l);
        SendMessageW(item(ID_BAR), PBM_SETPOS, w, 0);
        if (!g.cancel) SetWindowTextW(item(ID_STATUS), s->c_str());
        return 0;
    }
    case WM_LOG: {
        std::unique_ptr<std::wstring> s((std::wstring*)l);
        appendLog(*s);
        return 0;
    }
    case WM_DONE: {
        std::unique_ptr<DoneInfo> d((DoneInfo*)l);
        joinWorker();
        setBusy(false);
        if (d->ok) {
            g.exe = d->exe;
            g.args = d->args;
            SendMessageW(item(ID_BAR), PBM_SETPOS, 100, 0);
            show(ID_BAR, true);
            SetWindowTextW(item(ID_STATUS), L"Ready to play!");
            show(ID_PLAY, true);
            show(ID_OPEN, true);
        } else if (d->msg == L"Cancelled.") {
            SetWindowTextW(item(ID_STATUS), L"Cancelled.");
        } else {
            SetWindowTextW(item(ID_STATUS), (L"Failed: " + d->msg).c_str());
            appendLog(L"ERROR: " + d->msg);
            MessageBoxW(hwnd, d->msg.c_str(), (g.title + L" Installer").c_str(), MB_ICONERROR);
        }
        return 0;
    }
    case WM_CLOSE:
        if (g.thread) { g.cancel = true; joinWorker(); }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
}  // namespace

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int showCmd) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX ic{sizeof ic, ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&ic);

    std::wstring exe(32768, 0);
    exe.resize(GetModuleFileNameW(nullptr, &exe[0], (DWORD)exe.size()));
    g.appDir = fs::path(exe).parent_path().wstring();

    Manifest man; std::string err;
    if (!loadManifest(fs::path(g.appDir) / "manifest.txt", man, err)) {
        MessageBoxW(nullptr, widen(err).c_str(), L"Installer", MB_ICONERROR);
        return 1;
    }
    g.title = widen(man.title);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"GameInstallerWnd";
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r{0, 0, 600, 440};
    AdjustWindowRectEx(&r, style, FALSE, WS_EX_ACCEPTFILES);
    HWND hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, (g.title + L" Installer").c_str(), style,
                                CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                                nullptr, nullptr, hi, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, showCmd);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    return (int)m.wParam;
}
