// Quasar Monitor: tray resident system monitor with a skinnable overlay.
// One translation unit on purpose: the whole shell (tray, overlay window, settings, watcher) is ~600 lines.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include "export_filter.h"
#include "json.h"
#include "render.h"
#include "samplers.h"
#include "skin.h"
#include "util.h"
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

using namespace qm;

static const wchar_t* kCtrlClass = L"QuasarMonitor.Control";
static const wchar_t* kOverlayClass = L"QuasarMonitor.Overlay";
static const wchar_t* kAppName = L"Quasar Monitor";
enum : UINT { WM_TRAY = WM_APP + 10, WM_WATCH = WM_APP + 1, WM_SHOW_EXISTING = WM_APP + 2, WM_OVERLAY_MOVED = WM_APP + 3 };
enum : UINT_PTR { T_TICK = 1, T_RELOAD = 2 };
enum : int { ID_SHOW = 1000, ID_CLICK, ID_RELOAD, ID_CUSTOMIZE, ID_FOLDER, ID_AUTOSTART, ID_LOG, ID_EXIT, ID_SKIN0 = 2000 };

// ------------------------------------------------------------------ settings
struct Settings {
    std::string skin = "glass";
    bool visible = true;
    int clickThrough = -1;                       // -1: follow the skin, 0/1: user override from the tray menu
    std::string hotkey = "ctrl+alt+f10";
    bool exportOn = false, exportLan = false;
    int exportPort = 8099;
    std::map<std::string, std::pair<int, int>> pos;   // skin id -> absolute screen position (after the user dragged it)

    static std::wstring path() { return joinPath(appDataDir(), L"settings.json"); }
    void load() {
        std::string t; Json j; std::string err;
        if (!readFile(path(), t) || !parseJson(t, j, err) || !j.isObj()) return;
        skin = j.str("skin", skin); visible = j.boolean("visible", true); hotkey = j.str("hotkey", hotkey);
        if (const Json* e = j.find("export")) if (e->isObj()) {
            exportOn = e->boolean("enabled", false); exportLan = e->boolean("lan", false); exportPort = (int)e->num("port", 8099);
            if (exportPort < 1024 || exportPort > 65535) exportPort = 8099;
        }
        if (const Json* c = j.find("clickThrough")) if (c->type == Json::Bool) clickThrough = c->b ? 1 : 0;
        if (const Json* p = j.find("positions")) if (p->isObj()) for (auto& kv : p->o)
            if (kv.second.isArr() && kv.second.a.size() == 2 && kv.second.a[0].isNum() && kv.second.a[1].isNum())
                pos[kv.first] = {(int)kv.second.a[0].n, (int)kv.second.a[1].n};
    }
    void save() const {
        std::string o = "{\n  \"skin\": \"" + jsonEscape(skin) + "\",\n  \"visible\": " + (visible ? "true" : "false") +
                        ",\n  \"hotkey\": \"" + jsonEscape(hotkey) + "\"";
        if (clickThrough >= 0) o += std::string(",\n  \"clickThrough\": ") + (clickThrough ? "true" : "false");
        o += std::string(",\n  \"export\": { \"enabled\": ") + (exportOn ? "true" : "false") + ", \"port\": " + std::to_string(exportPort) + ", \"lan\": " + (exportLan ? "true" : "false") + " }";
        o += ",\n  \"positions\": {";
        bool first = true;
        for (auto& kv : pos) { o += std::string(first ? "\n" : ",\n") + "    \"" + jsonEscape(kv.first) + "\": [" + std::to_string(kv.second.first) + ", " + std::to_string(kv.second.second) + "]"; first = false; }
        o += first ? "}\n}\n" : "\n  }\n}\n";
        writeFileAtomic(path(), o);
    }
};

// ------------------------------------------------------------------ skin folders
struct SkinRef { std::string id; std::wstring dir; bool user; };

static std::wstring userSkinsDir() { std::wstring d = joinPath(appDataDir(), L"skins"); CreateDirectoryW(d.c_str(), nullptr); return d; }

static std::vector<SkinRef> findSkins() {
    std::vector<SkinRef> out;
    std::vector<std::pair<std::wstring, bool>> roots = {{userSkinsDir(), true}, {joinPath(exeDir(), L"skins"), false}, {joinPath(exeDir(), L"..\\skins"), false}};
    for (auto& r : roots)
        for (const std::wstring& n : listSubdirs(r.first)) {
            std::wstring dir = joinPath(r.first, n);
            if (!fileExists(joinPath(dir, L"skin.json"))) continue;
            std::string id = narrow(n);
            bool dup = false;
            for (auto& s : out) if (_stricmp(s.id.c_str(), id.c_str()) == 0) dup = true;   // user folder wins over the bundled one
            if (!dup) out.push_back({id, dir, r.second});
        }
    return out;
}

static void copyTree(const std::wstring& from, const std::wstring& to, int depth = 0) {
    if (depth > 3) return;
    CreateDirectoryW(to.c_str(), nullptr);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(joinPath(from, L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) copyTree(joinPath(from, fd.cFileName), joinPath(to, fd.cFileName), depth + 1);
        else CopyFileW(joinPath(from, fd.cFileName).c_str(), joinPath(to, fd.cFileName).c_str(), FALSE);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// ------------------------------------------------------------------ folder watcher (hot reload)
struct Watcher {
    HANDLE dir = INVALID_HANDLE_VALUE, th = nullptr;
    HWND notify = nullptr;
    volatile LONG stop = 0;
    static DWORD WINAPI run(LPVOID p) {
        Watcher* w = (Watcher*)p;
        alignas(8) char buf[4096];
        DWORD ret;
        while (!w->stop) {
            if (!ReadDirectoryChangesW(w->dir, buf, sizeof buf, FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE, &ret, nullptr, nullptr)) break;
            if (w->stop) break;
            PostMessageW(w->notify, WM_WATCH, 0, 0);
        }
        return 0;
    }
    void start(const std::wstring& path, HWND hwnd) {
        end();
        dir = CreateFileW(path.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (dir == INVALID_HANDLE_VALUE) return;
        notify = hwnd; stop = 0;
        th = CreateThread(nullptr, 0, run, this, 0, nullptr);
    }
    void end() {
        if (th) { InterlockedExchange(&stop, 1); CancelIoEx(dir, nullptr); WaitForSingleObject(th, 2000); CloseHandle(th); th = nullptr; }
        if (dir != INVALID_HANDLE_VALUE) { CloseHandle(dir); dir = INVALID_HANDLE_VALUE; }
    }
};

// ------------------------------------------------------------------ metrics export (for ESP32 / SPI displays, scripts, other panels)
// GET /metrics[?keys=cpu.load,gpu.temp]  -> {"cpu.load":12.3,"gpu.name":"...","gpu.temp":null,...}      GET /health -> ok
// Off by default. Loopback only unless "export": {"lan": true} is set in settings.json.
struct Exporter {
    SOCKET ls = INVALID_SOCKET;
    HANDLE th = nullptr;
    volatile LONG stop = 0;
    CRITICAL_SECTION cs;
    std::vector<std::pair<std::string, std::string>> snap;   // name -> JSON value
    bool inited = false, lan = false;

    static std::string numJson(double v) {
        if (std::isnan(v) || std::isinf(v)) return "null";
        char b[48];
        snprintf(b, sizeof b, "%.3f", v);
        std::string s = b;
        while (s.size() > 1 && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        return s;
    }
    void update(const Metrics& m) {
        if (!inited) return;
        std::vector<std::string> names = m.names();
        std::sort(names.begin(), names.end());
        std::vector<std::pair<std::string, std::string>> v;
        v.reserve(names.size());
        for (auto& n : names) {
            if (!exportAllowed(n, lan)) continue;
            if (const std::string* s = m.getStr(n)) v.emplace_back(n, "\"" + jsonEscape(*s) + "\"");
            else v.emplace_back(n, numJson(m.get(n)));
        }
        EnterCriticalSection(&cs); snap.swap(v); LeaveCriticalSection(&cs);
    }
    std::string body(const std::string& keysCsv) {
        std::vector<std::string> want;
        for (size_t i = 0; i < keysCsv.size();) {
            size_t j = keysCsv.find(',', i); if (j == std::string::npos) j = keysCsv.size();
            if (j > i) want.push_back(keysCsv.substr(i, j - i));
            i = j + 1;
        }
        std::string o = "{";
        bool first = true;
        EnterCriticalSection(&cs);
        for (auto& kv : snap) {
            if (!want.empty() && std::find(want.begin(), want.end(), kv.first) == want.end()) continue;
            o += std::string(first ? "" : ",") + "\"" + jsonEscape(kv.first) + "\":" + kv.second;
            first = false;
        }
        LeaveCriticalSection(&cs);
        return o + "}";
    }
    static DWORD WINAPI run(LPVOID p) {
        Exporter* e = (Exporter*)p;
        while (!e->stop) {
            SOCKET c = accept(e->ls, nullptr, nullptr);
            if (c == INVALID_SOCKET) { if (e->stop) break; Sleep(50); continue; }
            DWORD to = 2000;
            setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof to);
            setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, (const char*)&to, sizeof to);
            char req[1024];
            int n = recv(c, req, sizeof req - 1, 0);
            std::string status = "400 Bad Request", ctype = "text/plain", payload = "bad request";
            if (n > 0) {
                req[n] = 0;
                std::string line(req, strcspn(req, "\r\n"));
                if (line.compare(0, 4, "GET ") == 0) {
                    size_t sp = line.find(' ', 4);
                    std::string target = line.substr(4, sp == std::string::npos ? std::string::npos : sp - 4);
                    if (target == "/health") { status = "200 OK"; payload = "ok"; }
                    else if (target == "/metrics" || target.compare(0, 9, "/metrics?") == 0) {
                        std::string keys;
                        size_t k = target.find("keys=");
                        if (k != std::string::npos) keys = target.substr(k + 5, target.find('&', k) == std::string::npos ? std::string::npos : target.find('&', k) - k - 5);
                        status = "200 OK"; ctype = "application/json"; payload = e->body(keys);
                    } else { status = "404 Not Found"; payload = "try /metrics or /health"; }
                } else { status = "405 Method Not Allowed"; payload = "GET only"; }
            }
            std::string resp = "HTTP/1.1 " + status + "\r\nContent-Type: " + ctype + "\r\nContent-Length: " + std::to_string(payload.size()) +
                               "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n" + payload;
            send(c, resp.data(), (int)resp.size(), 0);
            shutdown(c, SD_SEND);
            closesocket(c);
        }
        return 0;
    }
    bool start(int port, bool lan) {
        end();
        WSADATA w;
        if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return false;
        ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (ls == INVALID_SOCKET) return false;
        sockaddr_in a{};
        a.sin_family = AF_INET; a.sin_port = htons((u_short)port);
        a.sin_addr.s_addr = lan ? htonl(INADDR_ANY) : htonl(INADDR_LOOPBACK);
        if (bind(ls, (sockaddr*)&a, sizeof a) != 0 || listen(ls, 4) != 0) { logLine("export: cannot listen on port " + std::to_string(port) + " (in use?)"); closesocket(ls); ls = INVALID_SOCKET; return false; }
        InitializeCriticalSection(&cs); inited = true; stop = 0; this->lan = lan;
        th = CreateThread(nullptr, 0, run, this, 0, nullptr);
        logLine(std::string("export: http://") + (lan ? "0.0.0.0" : "127.0.0.1") + ":" + std::to_string(port) + "/metrics");
        return true;
    }
    void end() {
        if (th) { InterlockedExchange(&stop, 1); closesocket(ls); ls = INVALID_SOCKET; WaitForSingleObject(th, 2000); CloseHandle(th); th = nullptr; }
        else if (ls != INVALID_SOCKET) { closesocket(ls); ls = INVALID_SOCKET; }
        if (inited) { DeleteCriticalSection(&cs); inited = false; }
    }
};

// ------------------------------------------------------------------ the app
static bool parseHotkey(const std::string& s, UINT& mods, UINT& vk) {
    mods = MOD_NOREPEAT; vk = 0;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find('+', i);
        if (j == std::string::npos) j = s.size();
        std::string t = s.substr(i, j - i);
        for (auto& c : t) c = (char)tolower((unsigned char)c);
        if (t == "ctrl" || t == "control") mods |= MOD_CONTROL; else if (t == "alt") mods |= MOD_ALT; else if (t == "shift") mods |= MOD_SHIFT; else if (t == "win") mods |= MOD_WIN;
        else if (t.size() == 1 && isalnum((unsigned char)t[0])) vk = (UINT)toupper((unsigned char)t[0]);
        else if (t.size() >= 2 && t[0] == 'f' && atoi(t.c_str() + 1) >= 1 && atoi(t.c_str() + 1) <= 24) vk = VK_F1 + atoi(t.c_str() + 1) - 1;
        else return false;
        i = j + 1;
    }
    return vk != 0 && (mods & ~MOD_NOREPEAT) != 0;
}

struct App {
    HINSTANCE inst = nullptr;
    HWND ctrl = nullptr, overlay = nullptr;
    Settings cfg;
    Skin skin;
    bool haveSkin = false;
    std::string skinId;
    std::vector<SkinRef> skins;
    std::unique_ptr<Renderer> renderer;
    std::vector<std::unique_ptr<Sampler>> samplers;
    std::vector<double> lastRun;
    Metrics metrics;
    Watcher watcher;
    Exporter exporter;
    HDC memDC = nullptr; HBITMAP dib = nullptr; void* bits = nullptr; int dibW = 0, dibH = 0;
    NOTIFYICONDATAW nid{};
    HICON icon = nullptr;
    bool clickThrough = true, shown = true;
    UINT taskbarCreated = 0;
    LARGE_INTEGER freq, t0;
    int lastWarnCount = 0;

    double now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (double)(t.QuadPart - t0.QuadPart) / (double)freq.QuadPart; }

    // ---------------- tray
    void trayAdd() {
        nid = NOTIFYICONDATAW{};
        nid.cbSize = sizeof nid; nid.hWnd = ctrl; nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; nid.uCallbackMessage = WM_TRAY; nid.hIcon = icon;
        wcscpy(nid.szTip, kAppName);
        Shell_NotifyIconW(NIM_ADD, &nid);
    }
    void trayTip(const std::wstring& tip) {
        wcsncpy(nid.szTip, tip.c_str(), 127); nid.szTip[127] = 0;
        nid.uFlags = NIF_TIP;
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
    void balloon(const std::wstring& title, const std::wstring& text) {
        NOTIFYICONDATAW n = nid;
        n.uFlags = NIF_INFO; n.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
        wcsncpy(n.szInfoTitle, title.c_str(), 63); wcsncpy(n.szInfo, text.c_str(), 255);
        Shell_NotifyIconW(NIM_MODIFY, &n);
    }
    HICON makeIcon() {
        Gdiplus::Bitmap bmp(32, 32, PixelFormat32bppARGB);
        {
            Gdiplus::Graphics g(&bmp);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.Clear(Gdiplus::Color(0, 0, 0, 0));
            Gdiplus::LinearGradientBrush br(Gdiplus::PointF(0, 0), Gdiplus::PointF(32, 32), Gdiplus::Color(255, 139, 108, 255), Gdiplus::Color(255, 56, 217, 240));
            Gdiplus::Pen pen(&br, 5.f);
            g.DrawArc(&pen, 4.f, 4.f, 24.f, 24.f, 130.f, 280.f);
            Gdiplus::SolidBrush dot(Gdiplus::Color(255, 238, 241, 251));
            g.FillEllipse(&dot, 13.f, 13.f, 6.f, 6.f);
        }
        HICON h = nullptr;
        bmp.GetHICON(&h);
        return h;
    }

    // ---------------- overlay window
    void createOverlay() {
        overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TRANSPARENT, kOverlayClass, kAppName, WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, inst, this);
    }
    void applyClickThrough(bool ct) {
        clickThrough = ct;
        LONG_PTR ex = GetWindowLongPtrW(overlay, GWL_EXSTYLE);
        ex = ct ? (ex | WS_EX_TRANSPARENT) : (ex & ~WS_EX_TRANSPARENT);
        SetWindowLongPtrW(overlay, GWL_EXSTYLE, ex);
    }
    struct Mon { RECT work; };
    static BOOL CALLBACK enumMon(HMONITOR m, HDC, LPRECT, LPARAM p) {
        MONITORINFO mi{}; mi.cbSize = sizeof mi;
        if (GetMonitorInfoW(m, &mi)) {
            auto* v = (std::vector<std::pair<bool, RECT>>*)p;
            v->push_back({(mi.dwFlags & MONITORINFOF_PRIMARY) != 0, mi.rcWork});
        }
        return TRUE;
    }
    float currentScale() {
        typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
        static GetDpiForWindowFn fn = (GetDpiForWindowFn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
        UINT dpi = fn ? fn(overlay) : 96;
        if (!dpi) dpi = 96;
        return dpi / 96.f * skin.scale;
    }
    POINT placement(int pxW, int pxH, float scale) {
        std::vector<std::pair<bool, RECT>> mons;
        EnumDisplayMonitors(nullptr, nullptr, enumMon, (LPARAM)&mons);
        std::stable_sort(mons.begin(), mons.end(), [](auto& a, auto& b) { return a.first > b.first; });   // primary first
        auto sp = cfg.pos.find(skinId);
        if (sp != cfg.pos.end()) {
            POINT p{sp->second.first, sp->second.second};
            for (auto& m : mons) if (p.x >= m.second.left - 50 && p.x < m.second.right && p.y >= m.second.top - 20 && p.y < m.second.bottom) return p;   // saved drag position, if still on screen
        }
        RECT wa = mons.empty() ? RECT{0, 0, 1920, 1080} : mons[std::min<size_t>(std::max(0, skin.monitor), mons.size() - 1)].second;
        const std::string& a = skin.anchor;
        bool left = a.find("left") != std::string::npos, right = a.find("right") != std::string::npos;
        bool top = a.find("top") != std::string::npos, bottom = a.find("bottom") != std::string::npos;
        int mx = (int)(skin.marginX * scale), my = (int)(skin.marginY * scale);
        POINT p;
        p.x = left ? wa.left + mx : right ? wa.right - pxW - mx : wa.left + (wa.right - wa.left - pxW) / 2 + mx;
        p.y = top ? wa.top + my : bottom ? wa.bottom - pxH - my : wa.top + (wa.bottom - wa.top - pxH) / 2 + my;
        return p;
    }
    void present(bool reposition) {
        if (!haveSkin || !overlay) return;
        float scale = currentScale();
        int w = std::max(1, (int)std::ceil(skin.width * scale)), h = std::max(1, (int)std::ceil(skin.height * scale));
        if (!dib || w != dibW || h != dibH) {
            if (dib) DeleteObject(dib);
            BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
            dib = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            SelectObject(memDC, dib);
            dibW = w; dibH = h; reposition = true;
        }
        if (!dib) return;
        renderer->draw(bits, w, h, skin, metrics, scale);
        BLENDFUNCTION bf{AC_SRC_OVER, 0, (BYTE)(skin.opacity * 255), AC_SRC_ALPHA};
        POINT src{0, 0}, dst; SIZE sz{w, h};
        POINT* pdst = nullptr;
        if (reposition) { dst = placement(w, h, scale); pdst = &dst; }
        HDC scr = GetDC(nullptr);
        UpdateLayeredWindow(overlay, scr, pdst, &sz, memDC, &src, 0, &bf, ULW_ALPHA);
        ReleaseDC(nullptr, scr);
        if (reposition) SetWindowPos(overlay, skin.topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    void setShown(bool s) {
        shown = s; cfg.visible = s;
        ShowWindow(overlay, s ? SW_SHOWNOACTIVATE : SW_HIDE);
        if (s) present(false); else SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);   // hidden tray app: let Windows reclaim the pages
        cfg.save();
        resetTimer();
    }

    // ---------------- skins
    bool loadSkinById(const std::string& id, bool announce) {
        skins = findSkins();
        const SkinRef* ref = nullptr;
        for (auto& s : skins) if (_stricmp(s.id.c_str(), id.c_str()) == 0) ref = &s;
        if (!ref && !skins.empty() && !haveSkin) ref = &skins[0];
        if (!ref) { if (announce) balloon(kAppName, L"No skins found. Put a folder with skin.json into " + userSkinsDir()); return false; }
        Skin fresh; std::string err;
        if (!loadSkin(ref->dir, fresh, err)) {
            logLine("skin '" + ref->id + "' rejected: " + err);
            if (announce) balloon(L"Skin error (previous skin kept)", widen(err));
            return false;
        }
        for (auto& w : fresh.warnings) logLine("skin '" + ref->id + "': " + w);
        logLine("skin '" + ref->id + "' loaded: " + std::to_string(fresh.widgets.size()) + " widgets, " + std::to_string(fresh.warnings.size()) + " warnings");
        // Keep the graph history when the same skin is saved again (same graph at the same position), so editing a skin
        // does not blank the graphs; switching to another skin starts clean.
        std::vector<std::vector<float>> oldHist;
        if (haveSkin && skinId == ref->id) for (const Widget& w : skin.widgets) oldHist.push_back(w.type == WType::Graph ? w.hist : std::vector<float>());
        skin = std::move(fresh);
        skinId = ref->id; cfg.skin = ref->id; haveSkin = true;
        renderer->clearCaches();
        for (size_t i = 0; i < skin.widgets.size(); ++i) {
            Widget& w = skin.widgets[i];
            w.hist.clear();
            if (i < oldHist.size() && w.type == WType::Graph && !oldHist[i].empty()) {
                w.hist = oldHist[i];
                while ((int)w.hist.size() > w.history) w.hist.erase(w.hist.begin());
            }
        }
        updateSkinState(skin, metrics);
        applyClickThrough(cfg.clickThrough >= 0 ? cfg.clickThrough == 1 : skin.clickThrough);
        watcher.start(ref->dir, ctrl);
        resetTimer();
        if (shown) present(true);
        if (announce && !skin.warnings.empty()) balloon(L"Skin loaded with warnings", widen(std::to_string(skin.warnings.size()) + " problem(s), see monitor.log (tray menu > Open log). First: " + skin.warnings[0]));
        return true;
    }
    void customize() {
        if (!haveSkin) return;
        const SkinRef* cur = nullptr;
        for (auto& s : skins) if (s.id == skinId) cur = &s;
        std::wstring target;
        std::string newId = skinId;
        if (cur && cur->user) target = cur->dir;   // already your own copy
        else {
            std::wstring base = userSkinsDir();
            newId = skinId + "-mine";
            for (int i = 2; dirExists(joinPath(base, widen(newId))); ++i) newId = skinId + "-mine" + std::to_string(i);
            target = joinPath(base, widen(newId));
            copyTree(cur ? cur->dir : skin.dir, target);
            cfg.save();
            loadSkinById(newId, false);
        }
        std::wstring file = joinPath(target, L"skin.json");
        if ((INT_PTR)ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL) <= 32)
            ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + file + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
        balloon(kAppName, L"Edit and save skin.json: the overlay updates instantly. Guide: docs/CUSTOMIZE.md");
    }

    // ---------------- sampling
    void resetTimer() {
        int ms = haveSkin ? skin.updateMs : 1000;
        if (!shown) ms = std::max(ms, 3000);
        SetTimer(ctrl, T_TICK, ms, nullptr);
    }
    void tick() {
        double t = now();
        if (lastRun.size() != samplers.size()) lastRun.assign(samplers.size(), -1e9);
        for (size_t i = 0; i < samplers.size(); ++i) {
            double iv = samplers[i]->intervalSec();
            double dt = t - lastRun[i];
            if (lastRun[i] > -1e8 && iv > 0 && dt < iv - 0.05) continue;
            samplers[i]->sample(metrics, lastRun[i] > -1e8 ? dt : 1.0);
            lastRun[i] = t;
        }
        if (haveSkin) updateSkinState(skin, metrics);
        exporter.update(metrics);
        if (shown) present(false);
        char tip[128];
        snprintf(tip, sizeof tip, "Quasar Monitor  CPU %.0f%%  GPU %.0f%%  RAM %.0f%%", metrics.get("cpu.load"), metrics.get("gpu.load"), metrics.get("ram.used_pct"));
        for (char* p = tip; *p; ++p) {}
        std::string s = tip;
        for (size_t k = s.find("nan"); k != std::string::npos; k = s.find("nan")) s.replace(k, 3, "-");   // no GPU: show a dash, not "nan"
        trayTip(widen(s));
    }

    // ---------------- menu
    void showMenu() {
        HMENU m = CreatePopupMenu(), sub = CreatePopupMenu();
        skins = findSkins();
        for (size_t i = 0; i < skins.size() && i < 60; ++i)
            AppendMenuW(sub, MF_STRING | (_stricmp(skins[i].id.c_str(), skinId.c_str()) == 0 ? MF_CHECKED : 0), ID_SKIN0 + (UINT)i, (widen(skins[i].id) + (skins[i].user ? L"  (mine)" : L"")).c_str());
        AppendMenuW(m, MF_STRING | (shown ? MF_CHECKED : 0), ID_SHOW, widen("Show overlay\t" + cfg.hotkey).c_str());
        AppendMenuW(m, MF_STRING | (clickThrough ? MF_CHECKED : 0), ID_CLICK, L"Click-through (uncheck to drag the overlay)");
        AppendMenuW(m, MF_POPUP, (UINT_PTR)sub, L"Skins");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, ID_CUSTOMIZE, L"Customize this skin...");
        AppendMenuW(m, MF_STRING, ID_RELOAD, L"Reload skin");
        AppendMenuW(m, MF_STRING, ID_FOLDER, L"Open my skins folder");
        AppendMenuW(m, MF_STRING, ID_LOG, L"Open log");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | (autostartOn() ? MF_CHECKED : 0), ID_AUTOSTART, L"Start with Windows");
        AppendMenuW(m, MF_STRING, ID_EXIT, L"Exit");
        POINT pt; GetCursorPos(&pt);
        SetForegroundWindow(ctrl);
        int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, ctrl, nullptr);
        PostMessageW(ctrl, WM_NULL, 0, 0);
        DestroyMenu(m);
        if (cmd) command(cmd);
    }
    static bool autostartOn() {
        HKEY k; bool on = false;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &k) == ERROR_SUCCESS) { on = RegQueryValueExW(k, L"QuasarMonitor", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS; RegCloseKey(k); }
        return on;
    }
    static void setAutostart(bool on) {
        HKEY k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
        if (on) {
            wchar_t exe[MAX_PATH * 2]; GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
            std::wstring v = std::wstring(L"\"") + exe + L"\"";
            RegSetValueExW(k, L"QuasarMonitor", 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * 2));
        } else RegDeleteValueW(k, L"QuasarMonitor");
        RegCloseKey(k);
    }
    void command(int id) {
        if (id >= ID_SKIN0) { size_t i = id - ID_SKIN0; if (i < skins.size()) { cfg.clickThrough = -1; loadSkinById(skins[i].id, true); cfg.save(); } return; }
        switch (id) {
            case ID_SHOW: setShown(!shown); break;
            case ID_CLICK: applyClickThrough(!clickThrough); cfg.clickThrough = clickThrough ? 1 : 0; cfg.save(); break;
            case ID_RELOAD: loadSkinById(skinId, true); break;
            case ID_CUSTOMIZE: customize(); break;
            case ID_FOLDER: ShellExecuteW(nullptr, L"open", userSkinsDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
            case ID_LOG: ShellExecuteW(nullptr, L"open", joinPath(appDataDir(), L"monitor.log").c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
            case ID_AUTOSTART: setAutostart(!autostartOn()); break;
            case ID_EXIT: DestroyWindow(ctrl); break;
        }
    }
    void registerHotkey() {
        UINT mods, vk;
        if (!parseHotkey(cfg.hotkey, mods, vk)) { logLine("hotkey '" + cfg.hotkey + "' is not valid (examples: ctrl+alt+f10, ctrl+shift+m)"); return; }
        if (RegisterHotKey(ctrl, 1, mods, vk)) return;
        logLine("hotkey '" + cfg.hotkey + "' is already used by another program (error " + std::to_string(GetLastError()) + "); change \"hotkey\" in settings.json");
        balloon(kAppName, widen("The hotkey " + cfg.hotkey + " is taken by another program. Use the tray icon, or change \"hotkey\" in settings.json."));
    }
};

static App* g_app = nullptr;

static LRESULT CALLBACK overlayProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_NCHITTEST: return g_app && !g_app->clickThrough ? HTCAPTION : HTTRANSPARENT;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_EXITSIZEMOVE: PostMessageW(g_app->ctrl, WM_OVERLAY_MOVED, 0, 0); return 0;
        case WM_DPICHANGED: if (g_app) g_app->present(true); return 0;
        case WM_DISPLAYCHANGE: if (g_app) g_app->present(true); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static LRESULT CALLBACK ctrlProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    App* a = g_app;
    if (a && m == a->taskbarCreated) { a->trayAdd(); return 0; }   // Explorer restarted: re-add the tray icon
    switch (m) {
        case WM_TIMER:
            if (w == T_TICK) a->tick();
            else if (w == T_RELOAD) { KillTimer(h, T_RELOAD); a->loadSkinById(a->skinId, true); }
            return 0;
        case WM_WATCH: SetTimer(h, T_RELOAD, 250, nullptr); return 0;   // debounce: editors write files in several steps
        case WM_HOTKEY: a->setShown(!a->shown); return 0;
        case WM_SHOW_EXISTING: a->setShown(true); return 0;
        case WM_OVERLAY_MOVED: {
            RECT r; GetWindowRect(a->overlay, &r);
            a->cfg.pos[a->skinId] = {r.left, r.top};
            a->cfg.save();
            return 0;
        }
        case WM_TRAY:
            if (l == WM_RBUTTONUP || l == WM_CONTEXTMENU) a->showMenu();
            else if (l == WM_LBUTTONDBLCLK || l == WM_LBUTTONUP) a->setShown(!a->shown);
            return 0;
        case WM_ENDSESSION: a->cfg.save(); return 0;
        case WM_DESTROY:
            a->watcher.end();
            a->exporter.end();
            UnregisterHotKey(h, 1);
            Shell_NotifyIconW(NIM_DELETE, &a->nid);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    typedef BOOL(WINAPI * SetCtxFn)(DPI_AWARENESS_CONTEXT);
    if (auto fn = (SetCtxFn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext")) fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\QuasarMonitor.Single");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(kCtrlClass, nullptr)) PostMessageW(other, WM_SHOW_EXISTING, 0, 0);
        return 0;
    }

    App app;
    g_app = &app;
    app.inst = inst;
    QueryPerformanceFrequency(&app.freq); QueryPerformanceCounter(&app.t0);
    app.cfg.load();

    std::string wantSkin;
    int argc; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) if (!wcscmp(argv[i], L"--skin") && i + 1 < argc) wantSkin = narrow(argv[++i]);
    if (argv) LocalFree(argv);
    if (!wantSkin.empty()) app.cfg.skin = wantSkin;

    app.renderer.reset(new Renderer);   // starts GDI+
    app.samplers = createSamplers();

    WNDCLASSEXW wc{}; wc.cbSize = sizeof wc; wc.hInstance = inst; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpfnWndProc = ctrlProc; wc.lpszClassName = kCtrlClass; RegisterClassExW(&wc);
    wc.lpfnWndProc = overlayProc; wc.lpszClassName = kOverlayClass; RegisterClassExW(&wc);
    app.ctrl = CreateWindowExW(0, kCtrlClass, kAppName, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    app.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    app.createOverlay();
    HDC scr = GetDC(nullptr); app.memDC = CreateCompatibleDC(scr); ReleaseDC(nullptr, scr);
    app.icon = app.makeIcon();
    app.trayAdd();
    app.shown = app.cfg.visible;
    app.registerHotkey();
    if (app.cfg.exportOn) app.exporter.start(app.cfg.exportPort, app.cfg.exportLan);

    app.tick();                       // first sample primes the deltas
    if (!app.loadSkinById(app.cfg.skin, true)) logLine("no skin could be loaded");
    app.tick();
    if (app.shown) ShowWindow(app.overlay, SW_SHOWNOACTIVATE);
    app.resetTimer();
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }

    app.cfg.save();
    if (app.dib) DeleteObject(app.dib);
    DeleteDC(app.memDC);
    app.renderer.reset();
    app.samplers.clear();
    if (app.icon) DestroyIcon(app.icon);
    ReleaseMutex(mutex); CloseHandle(mutex);
    return 0;
}
