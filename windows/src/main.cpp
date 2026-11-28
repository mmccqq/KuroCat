#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <objidl.h>
#include <gdiplus.h>
#include <wtsapi32.h>
#include <string>
#include <vector>
#include <thread>
#include <cmath>
#include <cwchar>
#include <algorithm>
#include "KeyMap.hpp"
#include "InputEngine.hpp"
#include "CatPhysics.hpp"
#include "CatRenderer.hpp"
#include "CatLink.hpp"
#include "ClaudeAlerts.hpp"
#include "BreakReminder.hpp"
#include <UIAutomation.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace {

// Whisku's version; release tags are "v" + it (keep in step with Resources/Info.plist CFBundleShortVersionString).
constexpr const wchar_t* APP_VERSION = L"1.2.0";
constexpr const wchar_t* VERSION_PROP = L"WhiskuVersion"; // set on the tray owner window so newer builds can take over older ones
constexpr const wchar_t* TRAY_OWNER_CLASS = L"WhiskuTrayOwnerClass";
constexpr const wchar_t* OVERLAY_CLASS = L"WhiskuOverlayWindowClass";
constexpr const wchar_t* FRIEND_CLASS = L"WhiskuFriendWindowClass";
// Updates come only from this fork's own GitHub Releases, never from upstream.
constexpr bool UPDATE_CHECKS_ENABLED = true;
constexpr const wchar_t* UPDATE_API_URL = L"https://api.github.com/repos/whisku-app/whisku/releases/latest";
/// The Windows download, newest name first: Whiskerkey became Whisku in 1.1.0 (1.0.2 was the first to accept both).
constexpr const char* UPDATE_WIN_ASSET_NAMES[] = { "Whisku-Windows-x64.exe", "Whiskerkey-Windows-x64.exe" };
constexpr double UPDATE_CHECK_INTERVAL = 6.0 * 3600.0;
constexpr double FRAME_INTERVAL = 1.0 / 60.0;
constexpr const wchar_t* UPDATE_PAGE_URL = L"https://github.com/whisku-app/whisku/releases/latest";
constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT WM_WAKE_EXISTING = WM_APP + 2;
constexpr UINT WM_UPDATE_CHECK_DONE = WM_APP + 3;
constexpr UINT WM_UPDATE_DOWNLOAD_DONE = WM_APP + 4;
constexpr UINT WM_REQUEST_QUIT = WM_APP + 5; // posted by a newer / elevated instance that is taking over
constexpr UINT_PTR TIMER_RENDER = 1;
constexpr int HOTKEY_CLICKTHROUGH_ID = 101;
UINT g_msgTaskbarCreated = 0;

enum MenuCmd : UINT {
    CMD_TOGGLE_VISIBLE = 1001,
    CMD_TOGGLE_CLICKTHROUGH,
    CMD_TOGGLE_TOPMOST,
    CMD_TOGGLE_AUTOSTART,
    CMD_TOGGLE_BADGES,
    CMD_TOGGLE_MOUSE_BADGES,
    CMD_TOGGLE_KEY_LEGENDS,
    CMD_TOGGLE_FOOT,
    CMD_TOGGLE_PET,
    CMD_COAT_BLACK,
    CMD_COAT_SIAMESE,
    CMD_COAT_SHORTHAIR,
    CMD_COAT_ORANGE_WHITE,
    CMD_KB_MILK,
    CMD_KB_HACKER,
    CMD_KB_RETRO,
    CMD_EYE_LIME,
    CMD_EYE_BLUE,
    CMD_EYE_AMBER,
    CMD_EYE_CYAN,
    CMD_EYE_SOFT_AMBER,
    CMD_EYE_REFERENCE_HAZEL,
    CMD_SCALE_65,
    CMD_SCALE_85,
    CMD_SCALE_100,
    CMD_SCALE_125,
    CMD_SCALE_150,
    CMD_DEMO_BURST,
    CMD_RESET_POS,
    CMD_CHECK_UPDATE,
    CMD_OPEN_UPDATE,
    CMD_RELAUNCH_ADMIN,
    CMD_TOGGLE_RUN_AS_ADMIN,
    CMD_LINK_CREATE,
    CMD_LINK_JOIN,
    CMD_LINK_RECONNECT,
    CMD_LINK_COPY,
    CMD_LINK_DISCONNECT,
    CMD_LINK_SERVER_CN,
    CMD_LINK_SERVER_GLOBAL,
    CMD_LINK_SHOW_FRIEND,
    CMD_LINK_EXPLAIN,
    CMD_LINK_NICKNAME,
    CMD_LINK_DEMO,
    CMD_LINK_PAUSE,
    CMD_TOGGLE_EARBUDS,
    CMD_SHOW_ALL,
    CMD_HIDE_ALL,
    CMD_TOGGLE_MOSQUITO,
    CMD_MOSQUITO_DEMO,
    CMD_LANG_SYSTEM,
    CMD_LANG_CHINESE,
    CMD_LANG_ENGLISH,
    CMD_CLAUDE_TOGGLE,
    CMD_CLAUDE_SOUND,
    CMD_CLAUDE_TEST_DONE,
    CMD_CLAUDE_TEST_NEEDS,
    CMD_CLAUDE_COPY,
    CMD_BREAK_OFF,
    CMD_BREAK_30,
    CMD_BREAK_60,
    CMD_BREAK_120,
    CMD_BREAK_180,
    CMD_BREAK_PREVIEW,
    CMD_QUIT
};

struct AppState {
    HWND hwnd = nullptr;
    HWND menuOwnerHwnd = nullptr; // Non-NOACTIVATE helper window so TrackPopupMenu from tray NEVER fails!
    HICON trayIcon = nullptr;
    NOTIFYICONDATAW nid = {};

    CatSettings settings;
    InputEngine input;
    CatPhysics physics;

    bool isVisible = true;
    bool isDragging = false;
    POINT dragStartCursor = {};
    POINT dragStartWinOrigin = {};
    bool hasLastHover = false;
    Pt lastHoverCanvas = {};

    // Reusable 32-bit premultiplied DIB backing buffer
    HDC memDC = nullptr;
    HBITMAP dibBmp = nullptr;
    HBITMAP oldBmp = nullptr;
    void* dibBits = nullptr;
    int bufW = 0;
    int bufH = 0;

    double lastTickTime = 0.0;
    bool demoActive = false;
    double demoStartTime = 0.0;
    int demoStepIdx = 0;

    std::wstring availableUpdateVersion;
    std::wstring availableUpdateUrl;
    std::wstring availableAssetUrl;   // the .exe on the release
    long long availableAssetSize = 0; // its exact size, as GitHub lists it
    std::wstring availableAssetName;  // Whisku-Windows-x64.exe
    double nextUpdateCheckTime = 0.0;
    bool isUpdating = false;

    // Robustness against games, Win+L, sleep and display changes
    double nextFrameTime = 0.0;
    double lastRenderTime = 0.0;
    double nextHousekeepingTime = 0.0;
    double pendingTopmostTime = 0.0;
    int ulwFailures = 0;
    bool isElevated = false;
    bool foregroundBlocksKeys = false;   // foreground app runs as admin -> Windows (UIPI) hides its keys from us
    double lastBlockedForegroundTime = 0.0;
    bool warnedElevatedForeground = false;
    HWINEVENTHOOK foregroundHook = nullptr;
    bool sessionNotifyRegistered = false;

    // Cat Link: our cat on a friend's screen, theirs on ours
    catlink::Session link;
    catlink::Status lastLinkStatus = catlink::Status::Off;
    char linkServer = 'C';
    bool showFriendCat = true;
    std::wstring savedLinkCode;
    std::wstring nickname;
    std::deque<catlink::Session::Due> demoQueue;   // a pretend friend (menu demo), never sent anywhere
    double demoUntil = 0.0;
    double lastHeart = 0.0;
    int lastHeadphones = -1;                        // -1 = not known yet

    // Claude Code alerts: bubbles when a session finishes or waits for an approval (local only)
    claude::Center claude;
    // Break reminder: the cat suggests a break after a long stretch of work (local only)
    breaks::Center breaks;
    struct FriendCat {
        HWND hwnd = nullptr;
        bool shown = false;
        CatSettings settings;
        catlink::RemoteDriver driver;
        CatPhysics physics;
        bool dragging = false;
        POINT dragStartCursor = {};
        POINT dragStartWinOrigin = {};
        bool hasLastHover = false;
        Pt lastHover = {};
        std::wstring label;
        bool labelDim = false;
        HDC memDC = nullptr;
        HBITMAP dibBmp = nullptr;
        HBITMAP oldBmp = nullptr;
        void* dibBits = nullptr;
        int bufW = 0, bufH = 0;

        void destroyBuffer() {
            if (memDC && oldBmp) SelectObject(memDC, oldBmp);
            if (dibBmp) DeleteObject(dibBmp);
            if (memDC) DeleteDC(memDC);
            memDC = nullptr; dibBmp = nullptr; oldBmp = nullptr; dibBits = nullptr; bufW = bufH = 0;
        }
        void ensureBuffer(int w, int h) {
            if (w == bufW && h == bufH && memDC && dibBits) return;
            destroyBuffer();
            HDC screenDC = GetDC(nullptr);
            memDC = CreateCompatibleDC(screenDC);
            ReleaseDC(nullptr, screenDC);
            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = w;
            bmi.bmiHeader.biHeight = -h;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            dibBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
            if (dibBmp) { oldBmp = (HBITMAP)SelectObject(memDC, dibBmp); bufW = w; bufH = h; }
        }
    } friendCat;

    void destroyBuffer() {
        if (memDC && oldBmp) SelectObject(memDC, oldBmp);
        if (dibBmp) DeleteObject(dibBmp);
        if (memDC) DeleteDC(memDC);
        memDC = nullptr; dibBmp = nullptr; oldBmp = nullptr; dibBits = nullptr;
        bufW = 0; bufH = 0;
    }

    void ensureBuffer(int w, int h) {
        if (w == bufW && h == bufH && memDC && dibBits) return;
        destroyBuffer();

        HDC screenDC = GetDC(nullptr);
        memDC = CreateCompatibleDC(screenDC);
        ReleaseDC(nullptr, screenDC);

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h; // top-down 32bpp DIB
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        dibBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
        if (dibBmp) {
            oldBmp = (HBITMAP)SelectObject(memDC, dibBmp);
            bufW = w;
            bufH = h;
        }
    }
};

AppState* g_app = nullptr;

HICON createCatTrayIcon();

// MARK: - Windows Auto-Start (HKCU\Software\Microsoft\Windows\CurrentVersion\Run)

std::wstring getCurrentExeQuotedPath() {
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD len = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0]) - 1));
    if (len == 0) return L"";
    return L"\"" + std::wstring(buf, len) + L"\"";
}

/// The "start with Windows" entry (HKCU\...\Run): "Whisku", or "Whiskerkey" from before the rename.
constexpr const wchar_t* AUTO_START_NAMES[] = { L"Whisku", L"Whiskerkey" };

bool isAutoStartEnabled() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    bool exists = false;
    for (const wchar_t* name : AUTO_START_NAMES) {
        DWORD type = 0, size = 0;
        exists = exists || (RegQueryValueExW(hKey, name, nullptr, &type, nullptr, &size) == ERROR_SUCCESS && size > 0);
    }
    RegCloseKey(hKey);
    return exists;
}

bool setAutoStartEnabled(bool enable) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    bool ok = false;
    if (enable) {
        std::wstring cmd = getCurrentExeQuotedPath();
        if (!cmd.empty()) {
            ok = (RegSetValueExW(hKey, AUTO_START_NAMES[0], 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS);
            if (ok) RegDeleteValueW(hKey, AUTO_START_NAMES[1]);   // the entry from before the rename
        }
    } else {
        ok = true;
        for (const wchar_t* name : AUTO_START_NAMES) {
            LSTATUS st = RegDeleteValueW(hKey, name);
            ok = ok && (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND);
        }
    }
    RegCloseKey(hKey);
    return ok;
}

void showTrayBalloon(AppState& app, const wchar_t* title, const wchar_t* body) {
    NOTIFYICONDATAW b = app.nid;
    b.uFlags = NIF_INFO;
    b.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    wcsncpy_s(b.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(b.szInfo, body, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &b);
}

// MARK: - Elevation (UIPI) helpers
//
// Many games (and launchers such as WeGame / anti-cheat clients) run as administrator. Windows'
// User Interface Privilege Isolation then refuses to deliver that window's keystrokes to normal
// programs — Raw Input, GetAsyncKeyState and low-level hooks all go silent — while mouse movement
// is still visible. That is exactly the "cat only sees the mouse in games" report. The only fix
// is for Whisku to run at the same level, so we detect it and offer an admin relaunch.

bool isCurrentProcessElevated() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION te = {};
    DWORD sz = 0;
    bool elevated = GetTokenInformation(tok, TokenElevation, &te, sizeof(te), &sz) && te.TokenIsElevated != 0;
    CloseHandle(tok);
    return elevated;
}

bool isProcessElevatedOrProtected(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return GetLastError() == ERROR_ACCESS_DENIED; // protected / anti-cheat processes
    bool elevated = false;
    HANDLE tok = nullptr;
    if (OpenProcessToken(h, TOKEN_QUERY, &tok)) {
        TOKEN_ELEVATION te = {};
        DWORD sz = 0;
        if (GetTokenInformation(tok, TokenElevation, &te, sizeof(te), &sz)) elevated = te.TokenIsElevated != 0;
        CloseHandle(tok);
    } else {
        elevated = (GetLastError() == ERROR_ACCESS_DENIED);
    }
    CloseHandle(h);
    return elevated;
}

bool commandLineHasFlag(const wchar_t* flag) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;
    bool found = false;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], flag) == 0) { found = true; break; }
    }
    LocalFree(argv);
    return found;
}

/// Starts a new elevated copy of this exe (UAC prompt). Returns false if the user declined.
bool relaunchSelfElevated(const wchar_t* params) {
    wchar_t exe[MAX_PATH * 2] = {};
    if (GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof(exe) / sizeof(exe[0]) - 1)) == 0) return false;
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC;
    sei.lpVerb = L"runas";
    sei.lpFile = exe;
    sei.lpParameters = params;
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

/// True when the foreground window covers its whole monitor (borderless / fullscreen games).
bool isForegroundFullscreen() {
    HWND fg = GetForegroundWindow();
    if (!fg || fg == GetDesktopWindow() || fg == GetShellWindow()) return false;
    wchar_t cls[64] = {};
    GetClassNameW(fg, cls, 63);
    if (wcscmp(cls, L"WorkerW") == 0 || wcscmp(cls, L"Progman") == 0) return false;
    RECT wr = {};
    if (!GetWindowRect(fg, &wr)) return false;
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST), &mi)) return false;
    return wr.left <= mi.rcMonitor.left && wr.top <= mi.rcMonitor.top &&
           wr.right >= mi.rcMonitor.right && wr.bottom >= mi.rcMonitor.bottom;
}

// MARK: - Auto Update Checker (GitHub Releases via WinHTTP)

struct UpdateCheckResult {
    bool ok = false;
    bool manual = false;
    bool hasUpdate = false;
    bool noRelease = false;           // the fork has not published a release yet (HTTP 404)
    std::wstring latestTag;
    std::wstring releaseUrl;
    std::wstring assetUrl;
    long long assetSize = 0;
    std::wstring assetName;
};

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

/// Every number in a version, in order: "v1.3.3-mmccqq.5" -> {1, 3, 3, 5}.
std::vector<int> versionParts(const std::wstring& v) {
    std::vector<int> parts;
    int cur = 0;
    bool inNum = false;
    for (wchar_t c : v) {
        if (c >= L'0' && c <= L'9') { cur = cur * 10 + (c - L'0'); inNum = true; }
        else if (inNum) { parts.push_back(cur); cur = 0; inNum = false; }
    }
    if (inNum) parts.push_back(cur);
    return parts;
}

bool isSemVerNewer(const std::wstring& lhs, const std::wstring& rhs) {
    auto parse = versionParts;
    auto a = parse(lhs);
    auto b = parse(rhs);
    size_t n = std::max(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        int va = i < a.size() ? a[i] : 0;
        int vb = i < b.size() ? b[i] : 0;
        if (va != vb) return va > vb;
    }
    return false;
}

/// "1.3.3-mmccqq.5" -> 1030305. Stored as a window property so a newly launched (newer) build can
/// tell which version is already running. Builds up to 1.3.2 never set it (reads back as 0); older
/// builds stored "1.3.3" as 10303, which is still smaller than every fork release.
int versionToNumber(const std::wstring& v) {
    std::vector<int> p = versionParts(v);
    p.resize(4, 0);
    return p[0] * 1000000 + p[1] * 10000 + p[2] * 100 + std::min(p[3], 99);
}

std::wstring currentExePath() {
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD len = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0]) - 1));
    return std::wstring(buf, len);
}

/// HTTPS GET of a full URL (redirects followed, e.g. GitHub -> its download servers). Returns the body
/// for a 2xx answer; `status` gets the HTTP status (0 if there was no answer).
std::string httpsGet(const std::wstring& url, DWORD& status, DWORD receiveTimeoutMs, size_t maxBytes) {
    status = 0;
    std::string body;
    URL_COMPONENTS uc = { sizeof(uc) };
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;  uc.dwUrlPathLength = 2047;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) return body;
    std::wstring ua = std::wstring(L"Whisku/") + APP_VERSION;
    HINTERNET hSession = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return body;
    WinHttpSetTimeouts(hSession, 10000, 10000, 15000, (int)receiveTimeoutMs);
    if (HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0)) {
        if (HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)) {
            const wchar_t* headers = L"Accept: application/vnd.github+json, application/octet-stream\r\n";
            if (WinHttpSendRequest(hRequest, headers, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hRequest, nullptr)) {
                DWORD size = sizeof(status);
                WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
                if (status >= 200 && status < 300) {
                    DWORD avail = 0;
                    while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0 && body.size() < maxBytes) {
                        std::vector<char> buf(avail);
                        DWORD got = 0;
                        if (!WinHttpReadData(hRequest, buf.data(), avail, &got) || got == 0) break;
                        body.append(buf.data(), got);
                    }
                }
            }
            WinHttpCloseHandle(hRequest);
        }
        WinHttpCloseHandle(hConnect);
    }
    WinHttpCloseHandle(hSession);
    return body;
}

/// KuroCat's releases (v1.3.3-mmccqq.N) are never updates for Whisku, even though their numbers are
/// higher than 1.x.
bool isWhiskerkeyRelease(const std::wstring& tag) { return tag.find(L"mmccqq") == std::wstring::npos; }

/// GitHub's "latest release" JSON -> tag, release page and the Windows .exe (URL + exact size). The first
/// of UPDATE_WIN_ASSET_NAMES found wins; `assetName` (tests) looks for that one name only.
bool parseLatestRelease(const std::string& json, UpdateCheckResult& out, const char* assetName = nullptr) {
    auto v = catlink::JsonReader::parse(json);
    if (!v || v->type != catlink::JsonReader::Value::Object) return false;
    auto str = [](const catlink::JsonReader::Value* x) { return x && x->type == catlink::JsonReader::Value::String ? x->str : std::string(); };
    std::string tag = str(v->get("tag_name"));
    if (tag.empty()) return false;
    out.latestTag = utf8ToWide(tag);
    std::string page = str(v->get("html_url"));
    out.releaseUrl = page.empty() ? UPDATE_PAGE_URL : utf8ToWide(page);
    const auto* assets = v->get("assets");
    if (!assets || assets->type != catlink::JsonReader::Value::Array) return true;
    std::vector<const char*> names;
    if (assetName) names.push_back(assetName);
    else names.assign(std::begin(UPDATE_WIN_ASSET_NAMES), std::end(UPDATE_WIN_ASSET_NAMES));
    for (const char* name : names) {
        for (const auto& asset : assets->items) {
            if (str(asset.get("name")) != name) continue;
            out.assetName = utf8ToWide(name);
            out.assetUrl = utf8ToWide(str(asset.get("browser_download_url")));
            if (const auto* size = asset.get("size"); size && size->type == catlink::JsonReader::Value::Number) out.assetSize = (long long)size->number;
            return true;
        }
    }
    return true;
}

void startAsyncUpdateCheck(HWND notifyHwnd, bool manual, int delayMs = 0) {
    std::thread([notifyHwnd, manual, delayMs]() {
        if (delayMs > 0) Sleep((DWORD)delayMs);
        auto* res = new UpdateCheckResult();
        res->manual = manual;
        DWORD status = 0;
        std::string body = httpsGet(UPDATE_API_URL, status, 15000, 1u << 20);
        if (status == 404) {
            res->noRelease = true;
        } else if (status == 200 && parseLatestRelease(body, *res)) {
            res->ok = true;
            res->hasUpdate = isWhiskerkeyRelease(res->latestTag) && isSemVerNewer(res->latestTag, APP_VERSION);
        }
        if (!PostMessageW(notifyHwnd, WM_UPDATE_CHECK_DONE, 0, reinterpret_cast<LPARAM>(res))) {
            delete res;
        }
    }).detach();
}

struct UpdateDownloadResult {
    bool ok = false;
    std::wstring batPath;
};

/// Where a downloaded update goes: <name>.exe in the running exe's folder, where <name> is the
/// download's app name (Whisku-Windows-x64.exe -> Whisku.exe). If the running exe has another name (a
/// browser's "Whisku (1).exe", or the old Whisku.exe), that one is removed afterwards. If the folder
/// can't be written, the update goes to %LOCALAPPDATA%\Programs\<name> instead.
struct UpdateTarget { std::wstring exe; bool renamed = false; std::wstring fallbackDir; std::wstring fallbackExe; };
UpdateTarget updateTarget(const std::wstring& curExe, const std::wstring& assetName, const std::wstring& localAppData, const std::wstring& tempDir) {
    UpdateTarget t;
    size_t slash = curExe.find_last_of(L"\\/");
    std::wstring dir = slash == std::wstring::npos ? L"." : curExe.substr(0, slash);
    std::wstring file = slash == std::wstring::npos ? curExe : curExe.substr(slash + 1);
    std::wstring name = assetName.rfind(L"Whisku-", 0) == 0 ? L"Whisku" : L"Whiskerkey";
    bool known = assetName.rfind(L"Whisku-", 0) == 0 || assetName.rfind(L"Whiskerkey", 0) == 0;
    t.renamed = known && _wcsicmp(file.c_str(), (name + L".exe").c_str()) != 0;
    t.exe = t.renamed ? dir + L"\\" + name + L".exe" : curExe;
    t.fallbackDir = !localAppData.empty() ? localAppData + L"\\Programs\\" + name : tempDir + name;
    t.fallbackExe = t.fallbackDir + L"\\" + name + L".exe";
    return t;
}

/// The batch file that waits for this process to quit, moves the download into place (removing the old
/// exe if the update has a new name), and starts it. If the move keeps failing, the update is copied to the
/// per-user fallback folder instead; if even that fails, the old exe is started again. `launch` starts a
/// program (the self-test passes "rem" so nothing runs). Paths are in the local ANSI code page, for cmd.exe.
std::string updateBatch(DWORD pid, const std::string& src, const std::string& dst, const std::string& oldExe, bool renamed,
                        const std::string& fbDir, const std::string& fbExe, const std::string& launch = "start \"\"") {
    std::string bat = "@echo off\r\n:wait\r\n"
        "tasklist /FI \"PID eq " + std::to_string(pid) + "\" 2>nul | find \"" + std::to_string(pid) + "\" >nul\r\n"
        "if not errorlevel 1 (\r\n"
        "  ping -n 2 127.0.0.1 >nul\r\n"
        "  goto wait\r\n"
        ")\r\n"
        "set /a tries=0\r\n"
        ":retry\r\n"
        "move /y \"" + src + "\" \"" + dst + "\" >nul 2>&1\r\n"
        "if not errorlevel 1 goto moved\r\n"
        "set /a tries+=1\r\n"
        "if %tries% lss 15 (\r\n"
        "  ping -n 2 127.0.0.1 >nul\r\n"
        "  goto retry\r\n"
        ")\r\n"
        "if not exist \"" + fbDir + "\" mkdir \"" + fbDir + "\"\r\n"
        "copy /y \"" + src + "\" \"" + fbExe + "\" >nul 2>&1\r\n"
        "if errorlevel 1 goto launchold\r\n"
        "del /q \"" + src + "\" >nul 2>&1\r\n" +
        launch + " \"" + fbExe + "\"\r\n"
        "goto done\r\n"
        ":moved\r\n" +
        (renamed ? "del /q \"" + oldExe + "\" >nul 2>&1\r\n" : std::string()) +
        launch + " \"" + dst + "\"\r\n"
        "goto done\r\n"
        ":launchold\r\n" +
        launch + " \"" + oldExe + "\"\r\n"
        ":done\r\n"
        "del \"%~f0\"\r\n";
    return bat;
}

void startInAppUpdate(AppState& app) {
    if (app.isUpdating || !app.menuOwnerHwnd) return;
    app.isUpdating = true;
    HWND notifyHwnd = app.menuOwnerHwnd;

    std::wstring assetUrl = app.availableAssetUrl;
    long long expectedSize = app.availableAssetSize;
    std::wstring assetName = app.availableAssetName;
    std::thread([notifyHwnd, assetUrl, expectedSize, assetName]() {
        auto* res = new UpdateDownloadResult();
        DWORD status = 0;
        std::string exeBytes = assetUrl.empty() ? std::string() : httpsGet(assetUrl, status, 60000, 64u << 20);

        // Must be a complete Windows program: exactly the size GitHub lists, starting with "MZ".
        bool complete = expectedSize <= 0 || (long long)exeBytes.size() == expectedSize;
        if (complete && exeBytes.size() > 50000 && exeBytes[0] == 'M' && exeBytes[1] == 'Z') {
            wchar_t curExe[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, curExe, MAX_PATH);
            wchar_t tempDir[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tempDir);

            // Keep a real .exe extension so the fallback below can launch it directly.
            std::wstring curExeStr(curExe);
            std::wstring newExePath = (curExeStr.size() > 4 && _wcsicmp(curExeStr.c_str() + curExeStr.size() - 4, L".exe") == 0)
                ? curExeStr.substr(0, curExeStr.size() - 4) + L".update.exe"
                : curExeStr + L".update.exe";
            HANDLE hNew = CreateFileW(newExePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hNew == INVALID_HANDLE_VALUE) {
                newExePath = std::wstring(tempDir) + L"Whisku_update.exe";
                hNew = CreateFileW(newExePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            }
            if (hNew != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                BOOL okWrite = WriteFile(hNew, exeBytes.data(), (DWORD)exeBytes.size(), &written, nullptr);
                CloseHandle(hNew);
                if (okWrite && written == (DWORD)exeBytes.size()) {
                    std::wstring batPath = std::wstring(tempDir) + L"whisku_update.bat";
                    // Use local ANSI codepage for cmd.exe .bat compatibility
                    auto wideToAcp = [](const std::wstring& w) -> std::string {
                        if (w.empty()) return "";
                        int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
                        std::string s(n, '\0');
                        WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
                        return s;
                    };
                    // Per-user fallback location (no admin rights needed) for when the old exe cannot be
                    // overwritten (read-only folder, antivirus lock...). Previously the move failed silently,
                    // the OLD exe was restarted and users kept being told a new version was available.
                    wchar_t localAppData[MAX_PATH] = {};
                    GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
                    UpdateTarget target = updateTarget(curExe, assetName, localAppData, tempDir);
                    std::string batUtf8 = updateBatch(GetCurrentProcessId(), wideToAcp(newExePath), wideToAcp(target.exe), wideToAcp(curExe),
                                                      target.renamed, wideToAcp(target.fallbackDir), wideToAcp(target.fallbackExe));

                    HANDLE hBat = CreateFileW(batPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (hBat != INVALID_HANDLE_VALUE) {
                        DWORD bw = 0;
                        BOOL okBat = WriteFile(hBat, batUtf8.data(), (DWORD)batUtf8.size(), &bw, nullptr);
                        CloseHandle(hBat);
                        if (okBat && bw == (DWORD)batUtf8.size()) {
                            res->ok = true;
                            res->batPath = batPath;
                        }
                    }
                }
            }
        }

        if (!PostMessageW(notifyHwnd, WM_UPDATE_DOWNLOAD_DONE, 0, reinterpret_cast<LPARAM>(res))) {
            delete res;
        }
    }).detach();
}

void registerTrayIcon(AppState& app) {
    if (!app.trayIcon) app.trayIcon = createCatTrayIcon();
    app.nid = {};
    app.nid.cbSize = sizeof(app.nid);
    app.nid.hWnd = app.menuOwnerHwnd ? app.menuOwnerHwnd : app.hwnd;
    app.nid.uID = 1;
    app.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    app.nid.uCallbackMessage = WM_TRAYICON;
    app.nid.hIcon = app.trayIcon;
    wcscpy_s(app.nid.szTip, tr(L"Whisku \u9ED1\u732B\u952E\u76D8\u684C\u5BA0 (\u53F3\u952E/\u5DE6\u952E\u6253\u5F00\u83DC\u5355)", L"Whisku keyboard cat (click for the menu)"));
    Shell_NotifyIconW(NIM_ADD, &app.nid);
}

HICON createCatTrayIcon() {
    using namespace Gdiplus;
    constexpr int sz = 32;
    Bitmap bmp(sz, sz, PixelFormat32bppPARGB);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.Clear(Color(0, 0, 0, 0));

    SolidBrush black(Color(255, 22, 22, 26));
    PointF lEar[3] = { {4, 15}, {6, 3}, {14, 11} };
    PointF rEar[3] = { {28, 15}, {26, 3}, {18, 11} };
    g.FillPolygon(&black, lEar, 3);
    g.FillPolygon(&black, rEar, 3);
    g.FillEllipse(&black, 4.0f, 8.0f, 24.0f, 21.0f);

    SolidBrush lime(Color(255, 201, 242, 64));
    g.FillEllipse(&lime, 8.5f, 14.5f, 6.0f, 5.2f);
    g.FillEllipse(&lime, 17.5f, 14.5f, 6.0f, 5.2f);
    g.FillEllipse(&black, 10.7f, 15.2f, 1.8f, 3.8f);
    g.FillEllipse(&black, 19.7f, 15.2f, 1.8f, 3.8f);

    HICON hIcon = nullptr;
    bmp.GetHICON(&hIcon);
    return hIcon;
}

void computeDefaultOrigin(int w, int h, int& outX, int& outY) {
    RECT work = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    outX = work.right - w - 24;
    outY = work.bottom - h - 12;
}

void applyWindowScaleAndPosition(AppState& app, bool anchorBottomCenter, bool persist = true) {
    int newW = (int)std::round(CatPhysics::CanvasWidth * app.settings.scale);
    int newH = (int)std::round(CatPhysics::CanvasHeight * app.settings.scale);

    int x = app.settings.posX;
    int y = app.settings.posY;
    if (anchorBottomCenter && app.hwnd) {
        RECT r = {};
        GetWindowRect(app.hwnd, &r);
        int oldMidX = (r.left + r.right) / 2;
        int oldBotY = r.bottom;
        x = oldMidX - newW / 2;
        y = oldBotY - newH;
    } else if (x == -99999 || y == -99999) {
        computeDefaultOrigin(newW, newH, x, y);
    }

    // Clamp onto virtual desktop so saved off-monitor coords never get lost
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (x + newW < vx + 40 || x > vx + vw - 40 || y + newH < vy + 40 || y > vy + vh - 40) {
        computeDefaultOrigin(newW, newH, x, y);
    }

    HWND insertAfter = app.settings.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST;
    SetWindowPos(app.hwnd, insertAfter, x, y, newW, newH, SWP_NOACTIVATE);
    if (persist) {
        app.settings.posX = x;
        app.settings.posY = y;
        app.settings.saveToRegistry();
    }
}

/// Puts the cat back on top of other topmost windows (borderless/fullscreen games that make
/// themselves topmost after launching used to bury the cat, which looked like it "vanished").
void assertTopmost(AppState& app) {
    if (!app.hwnd || !app.isVisible || !app.settings.alwaysOnTop) return;
    SetWindowPos(app.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
}

/// Full self-repair after Win+L unlock, sleep/resume, display-mode changes (games switching
/// resolution) or a rendering stall: clears stuck keys, re-registers Raw Input, rebuilds the
/// layered-window buffer, restarts the frame timer and re-places the window on a visible monitor
/// WITHOUT overwriting the user's saved position.
void recoverAfterSystemEvent(AppState& app) {
    if (!app.hwnd) return;
    double now = nowSeconds();
    app.input.resetAllState();
    app.input.registerRawInput(app.hwnd);
    app.input.lastMouseMoveTime = now; // wake the cat up so it visibly greets the user again
    app.destroyBuffer();
    app.ulwFailures = 0;
    app.isDragging = false;
    app.lastTickTime = now;
    app.nextFrameTime = 0.0;
    app.lastRenderTime = now;
    KillTimer(app.hwnd, TIMER_RENDER);
    SetTimer(app.hwnd, TIMER_RENDER, 16, nullptr);
    if (app.isVisible) ShowWindow(app.hwnd, SW_SHOWNOACTIVATE);
    applyWindowScaleAndPosition(app, false, false);
    app.pendingTopmostTime = now + 1.5; // some games re-assert topmost shortly after a mode switch
}

/// Once per second: watchdog + game helpers.
void housekeeping(AppState& app) {
    double now = nowSeconds();
    if (now < app.nextHousekeepingTime) return;
    app.nextHousekeepingTime = now + 1.0;
    if (!app.hwnd) return;

    // Rendering stalled for >2s while visible (e.g. the layered window lost its surface): self-heal.
    if (app.isVisible && app.lastRenderTime > 0.0 && now - app.lastRenderTime > 2.0) {
        recoverAfterSystemEvent(app);
        return;
    }
    if (app.isVisible && !IsWindowVisible(app.hwnd)) ShowWindow(app.hwnd, SW_SHOWNOACTIVATE);

    // Check this fork's releases again every few hours (the first check runs shortly after launch).
    if (UPDATE_CHECKS_ENABLED && app.menuOwnerHwnd && !app.isUpdating) {
        if (app.nextUpdateCheckTime == 0.0) app.nextUpdateCheckTime = now + UPDATE_CHECK_INTERVAL;
        else if (now >= app.nextUpdateCheckTime) {
            app.nextUpdateCheckTime = now + UPDATE_CHECK_INTERVAL;
            startAsyncUpdateCheck(app.menuOwnerHwnd, false);
        }
    }

    bool fullscreenFg = isForegroundFullscreen();
    if (app.pendingTopmostTime > 0.0 && now >= app.pendingTopmostTime) {
        app.pendingTopmostTime = 0.0;
        assertTopmost(app);
    } else if (fullscreenFg) {
        assertTopmost(app);
    }

    // Detect admin-level games whose keystrokes Windows withholds from us.
    app.foregroundBlocksKeys = false;
    if (!app.isElevated) {
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (fg) GetWindowThreadProcessId(fg, &pid);
        if (pid && pid != GetCurrentProcessId() && isProcessElevatedOrProtected(pid)) {
            app.foregroundBlocksKeys = true;
            app.lastBlockedForegroundTime = now;
            if (fullscreenFg && !app.warnedElevatedForeground) {
                app.warnedElevatedForeground = true;
                showTrayBalloon(app,
                    tr(L"Whisku 听不到这个游戏的键盘", L"Whisku can't hear this game's keyboard"),
                    tr(L"该游戏以管理员权限运行，Windows 会拦截它的键盘输入。右键小猫/托盘图标 →「以管理员身份重新启动」即可恢复。", L"This game runs as administrator, so Windows hides its keyboard input. Right-click the cat or the tray icon \u2192 \u201CRestart as administrator\u201D to fix it."));
            }
        }
    }
}

void quitApp(AppState& app) {
    // Destroying the overlay runs the normal WM_DESTROY cleanup (tray icon, hotkey...) and quits.
    if (app.hwnd && IsWindow(app.hwnd)) DestroyWindow(app.hwnd);
    else PostQuitMessage(0);
}

void setClickThrough(HWND hwnd, bool transparent) {
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    bool isTrans = (ex & WS_EX_TRANSPARENT) != 0;
    if (isTrans != transparent) {
        if (transparent) ex |= WS_EX_TRANSPARENT;
        else ex &= ~WS_EX_TRANSPARENT;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
    }
}

// MARK: - Cat Link

/// Tracks whether the focused control is a password field (UI Automation, on a background thread), so
/// no keyboard activity is shared while the user types a password. Best effort: it covers browsers,
/// Win32 and UWP apps that expose UI Automation.
class PasswordFocusWatcher {
public:
    std::atomic<bool> isPassword{false};
    void start() {
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event_) return;
        std::thread([this] { run(); }).detach();
        hook_ = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS, nullptr, onFocus, 0, 0,
                                WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    }
    void stop() { if (hook_) UnhookWinEvent(hook_); hook_ = nullptr; }
private:
    HANDLE event_ = nullptr;
    HWINEVENTHOOK hook_ = nullptr;
    static void CALLBACK onFocus(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
    void run() {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;
        IUIAutomation* uia = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                    __uuidof(IUIAutomation), reinterpret_cast<void**>(&uia))) || !uia) return;
        while (WaitForSingleObject(event_, INFINITE) == WAIT_OBJECT_0) {
            IUIAutomationElement* el = nullptr;
            BOOL pw = FALSE;
            if (SUCCEEDED(uia->GetFocusedElement(&el)) && el) {
                el->get_CurrentIsPassword(&pw);
                el->Release();
            }
            isPassword = pw != FALSE;
        }
    }
};
PasswordFocusWatcher g_passwordWatcher;
void CALLBACK PasswordFocusWatcher::onFocus(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (g_passwordWatcher.event_) SetEvent(g_passwordWatcher.event_);
}

/// Watches the default sound output on a background thread: headphones (form factor Headphones /
/// Headset, or a headphone-like name) and whether anything is audible. Only the device type and
/// the output level are read, never any audio.
class AudioOutputWatcher {
public:
    std::atomic<int> headphones{-1};   // -1 unknown, 0 speakers, 1 headphones
    std::atomic<bool> playing{false};
    void start() { std::thread([this] { run(); }).detach(); }
private:
    void run() {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;
        IMMDeviceEnumerator* enumerator = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void**>(&enumerator))) || !enumerator) return;
        static const PROPERTYKEY kFormFactor = { {0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}}, 0 };
        static const PROPERTYKEY kFriendlyName = { {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14 };
        double lastSound = -100.0;
        while (true) {
            bool isHeadphones = false;
            float peak = 0.0f;
            IMMDevice* device = nullptr;
            if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) && device) {
                IPropertyStore* props = nullptr;
                if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)) && props) {
                    PROPVARIANT v;
                    PropVariantInit(&v);
                    if (SUCCEEDED(props->GetValue(kFormFactor, &v)) && v.vt == VT_UI4) {
                        isHeadphones = v.ulVal == 3 /* Headphones */ || v.ulVal == 5 /* Headset */;
                    }
                    PropVariantClear(&v);
                    PropVariantInit(&v);
                    if (!isHeadphones && SUCCEEDED(props->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal) {
                        std::wstring name = v.pwszVal;
                        for (auto& ch : name) ch = (wchar_t)towlower(ch);
                        for (const wchar_t* word : { L"headphone", L"headset", L"airpods", L"buds", L"\u8033\u673A" /* 耳机 */ }) {
                            if (name.find(word) != std::wstring::npos) isHeadphones = true;
                        }
                    }
                    PropVariantClear(&v);
                    props->Release();
                }
                IAudioMeterInformation* meter = nullptr;
                if (SUCCEEDED(device->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr,
                                               reinterpret_cast<void**>(&meter))) && meter) {
                    meter->GetPeakValue(&peak);
                    meter->Release();
                }
                device->Release();
            }
            double now = nowSeconds();
            if (peak > 0.0005f) lastSound = now;
            headphones = isHeadphones ? 1 : 0;
            playing = now - lastSound < 3.0;   // quiet moments in a song don't count as "stopped"
            Sleep(700);
        }
    }
};
AudioOutputWatcher g_audioWatcher;

/// A small modal text prompt (Win32 has no built-in one): returns true and updates `text` on OK.
struct PromptData { const wchar_t* title; const wchar_t* prompt; std::wstring text; };

INT_PTR CALLBACK promptProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* data = reinterpret_cast<PromptData*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
        case WM_INITDIALOG:
            SetWindowLongPtrW(dlg, DWLP_USER, lParam);
            data = reinterpret_cast<PromptData*>(lParam);
            SetWindowTextW(dlg, data->title);
            SetDlgItemTextW(dlg, 102, data->prompt);
            SetDlgItemTextW(dlg, 101, data->text.c_str());
            SendDlgItemMessageW(dlg, 101, EM_SETLIMITTEXT, 16, 0);
            SetForegroundWindow(dlg);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK && data) {
                wchar_t buf[64] = {};
                GetDlgItemTextW(dlg, 101, buf, 64);
                data->text = buf;
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL) { EndDialog(dlg, IDCANCEL); return TRUE; }
            break;
    }
    return FALSE;
}

bool promptText(HWND owner, const wchar_t* title, const wchar_t* prompt, std::wstring& text) {
    std::vector<WORD> t;
    auto dword = [&](DWORD d) { t.push_back(LOWORD(d)); t.push_back(HIWORD(d)); };
    auto str = [&](const wchar_t* w) { while (*w) t.push_back((WORD)*w++); t.push_back(0); };
    auto align = [&] { if (t.size() % 2) t.push_back(0); };
    auto item = [&](DWORD style, short x, short y, short cx, short cy, WORD id, WORD cls, const wchar_t* label) {
        align();
        dword(style | WS_CHILD | WS_VISIBLE); dword(0);
        t.push_back(x); t.push_back(y); t.push_back(cx); t.push_back(cy); t.push_back(id);
        t.push_back(0xFFFF); t.push_back(cls); str(label); t.push_back(0);
    };
    dword(DS_MODALFRAME | DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU); dword(WS_EX_TOPMOST);
    t.push_back(4); t.push_back(0); t.push_back(0); t.push_back(220); t.push_back(78);
    t.push_back(0); t.push_back(0); str(L"");
    t.push_back(9); str(L"Microsoft YaHei UI");
    item(SS_LEFT, 8, 8, 204, 20, 102, 0x0082, L"");
    item(WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 8, 32, 204, 14, 101, 0x0081, L"");
    item(BS_DEFPUSHBUTTON | WS_TABSTOP, 100, 56, 54, 16, IDOK, 0x0080, tr(L"\u4FDD\u5B58", L"Save"));       // 保存
    item(BS_PUSHBUTTON | WS_TABSTOP, 158, 56, 54, 16, IDCANCEL, 0x0080, tr(L"\u53D6\u6D88", L"Cancel"));      // 取消
    PromptData data{ title, prompt, text };
    if (DialogBoxIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<LPCDLGTEMPLATEW>(t.data()), owner,
                                promptProc, reinterpret_cast<LPARAM>(&data)) != IDOK) return false;
    text = data.text;
    return true;
}

/// What a Cat Link friend can see, in the UI language.
const wchar_t* linkWhatIsShared() {
    return tr(L"对方会看到：你在打字（只知道是左手还是右手在敲，不知道按了哪个键）、鼠标的移动、点击和滚动（包括光标在屏幕上的大致位置）、是否戴着耳机、你的猫咪皮肤和昵称。你们也能隔空摸对方的猫。\n"
              L"输入密码时会自动暂停发送键盘动作。所有内容都用配对码加密，中转服务器也看不到内容。",
              L"Your friend sees: that you're typing (only whether your left or right paw is typing, never which key), mouse movement, clicks and scrolling (including roughly where the cursor is on your screen), whether you're wearing headphones, and your cat's coat and nickname. You can also pet each other's cats from afar.\n"
              L"Keyboard activity pauses while you type a password. Everything is encrypted with the pairing code, so the relay server can't read it.");
}

/// What pausing and disconnecting do (shown in "What does my friend see?").
const wchar_t* linkPauseAndDisconnect() {
    return tr(L"\n\n勾选本菜单里的「⏸ 暂停分享我的猫」后，你的猫会从朋友的屏幕上消失，你的任何动作都不再发送，只发一个“已暂停”的信号，让对方知道你们还连着；你仍然能看到朋友的猫。断开连线后就不再发送任何内容。",
              L"\n\nWith \u23F8 Pause sharing my cat (in this menu) on, your cat disappears from your friend's screen and none of your activity is sent, only a \"paused\" signal so their app knows you're still linked. You still see their cat. Nothing is sent after you disconnect.");
}

std::wstring regReadString(const wchar_t* name) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Whisku", 0, KEY_READ, &k) != ERROR_SUCCESS) return L"";
    wchar_t buf[128] = {};
    DWORD size = sizeof(buf) - sizeof(wchar_t), type = 0;
    std::wstring out;
    if (RegQueryValueExW(k, name, nullptr, &type, (LPBYTE)buf, &size) == ERROR_SUCCESS && type == REG_SZ) out = buf;
    RegCloseKey(k);
    return out;
}

DWORD regReadDword(const wchar_t* name, DWORD def) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Whisku", 0, KEY_READ, &k) != ERROR_SUCCESS) return def;
    DWORD val = def, size = sizeof(val);
    if (RegQueryValueExW(k, name, nullptr, nullptr, (LPBYTE)&val, &size) != ERROR_SUCCESS) val = def;
    RegCloseKey(k);
    return val;
}

void regWrite(const wchar_t* name, const std::wstring* str, DWORD dword) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Whisku", 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    if (str) RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)str->c_str(), (DWORD)((str->size() + 1) * sizeof(wchar_t)));
    else RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&dword, sizeof(dword));
    RegCloseKey(k);
}

std::string coatName(CatCoatTheme t) {
    switch (t) {
        case CatCoatTheme::Siamese: return "siamese";
        case CatCoatTheme::Shorthair: return "shorthair";
        case CatCoatTheme::OrangeWhite: return "orangeWhite";
        default: return "classicBlack";
    }
}
std::string keyboardName(KeyboardTheme t) {
    switch (t) {
        case KeyboardTheme::MidnightHacker: return "midnightHacker";
        case KeyboardTheme::RetroCream: return "retroCream";
        default: return "classicMilk";
    }
}
std::string eyeName(EyeColorTheme t) {
    switch (t) {
        case EyeColorTheme::SiameseBlue: return "siameseBlue";
        case EyeColorTheme::WarmAmber: return "warmAmber";
        case EyeColorTheme::IceCyan: return "iceCyan";
        case EyeColorTheme::SoftAmber: return "softAmber";
        case EyeColorTheme::ReferenceHazel: return "referenceHazel";
        default: return "neonChartreuse";
    }
}

void applyFriendProfile(AppState& app) {
    auto& f = app.friendCat;
    const auto& d = f.driver;
    if (d.coat) {
        const std::string& c = *d.coat;
        f.settings.coatTheme = c == "siamese" ? CatCoatTheme::Siamese : c == "shorthair" ? CatCoatTheme::Shorthair
                             : c == "orangeWhite" ? CatCoatTheme::OrangeWhite : CatCoatTheme::ClassicBlack;
    }
    if (d.kb) {
        const std::string& k = *d.kb;
        f.settings.keyboardTheme = k == "midnightHacker" ? KeyboardTheme::MidnightHacker
                                 : k == "retroCream" ? KeyboardTheme::RetroCream : KeyboardTheme::ClassicMilk;
    }
    if (d.eye) {
        const std::string& e = *d.eye;
        f.settings.eyeTheme = e == "siameseBlue" ? EyeColorTheme::SiameseBlue : e == "warmAmber" ? EyeColorTheme::WarmAmber
                            : e == "iceCyan" ? EyeColorTheme::IceCyan : e == "softAmber" ? EyeColorTheme::SoftAmber
                            : e == "referenceHazel" ? EyeColorTheme::ReferenceHazel : EyeColorTheme::NeonChartreuse;
    }
}

void updateFriendLabel(AppState& app) {
    auto& f = app.friendCat;
    if (nowSeconds() < app.demoUntil) { f.label = tr(L"演示 · 模拟的朋友", L"Demo \u00B7 pretend friend"); f.labelDim = false; return; }
    std::string name = app.link.friendName();
    std::wstring who = name.empty() ? tr(L"朋友的猫", L"Friend's cat") : catlink::widen(name) + tr(L" 的猫", L"'s cat");
    switch (app.link.status()) {
        case catlink::Status::Linked: f.label = who + tr(L" · 在线", L" \u00B7 online"); f.labelDim = false; break;
        case catlink::Status::FriendOffline: f.label = who + tr(L" · 离线", L" \u00B7 offline"); f.labelDim = true; break;
        case catlink::Status::Connecting:
        case catlink::Status::Reconnecting: f.label = who + tr(L" · 正在重新连接…", L" \u00B7 reconnecting\u2026"); f.labelDim = true; break;
        default: f.label = who; f.labelDim = true; break;
    }
}

void setTrayTip(AppState& app, const wchar_t* tip) {
    wcsncpy_s(app.nid.szTip, tip, _TRUNCATE);
    app.nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &app.nid);
    app.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
}

/// The tray tooltip: whether Cat Link is on, and whether our sharing is paused.
const wchar_t* linkTrayTip(AppState& app) {
    catlink::Status st = app.link.status();
    if (st != catlink::Status::Linked && st != catlink::Status::FriendPaused) {
        return tr(L"Whisku 黑猫键盘桌宠 (右键/左键打开菜单)", L"Whisku keyboard cat (click for the menu)");
    }
    return app.link.paused() ? tr(L"Whisku 黑猫桌宠 · 猫咪连线中（暂停分享）", L"Whisku \u00B7 Cat Link on (sharing paused)")
                             : tr(L"Whisku 黑猫桌宠 · 猫咪连线中", L"Whisku \u00B7 Cat Link on");
}

void showFriendWindow(AppState& app, bool show) {
    auto& f = app.friendCat;
    if (!f.hwnd) return;
    bool want = show && app.showFriendCat;
    if (want == f.shown) return;
    f.shown = want;
    ShowWindow(f.hwnd, want ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void ensureFriendCat(AppState& app) {
    auto& f = app.friendCat;
    if (!f.hwnd) {
        f.settings = app.settings;
        RECT mr = {};
        GetWindowRect(app.hwnd, &mr);
        int w = mr.right - mr.left, h = mr.bottom - mr.top;
        int x = mr.left - w - 12, y = mr.top;
        if (regReadDword(L"FriendHasPos", 0)) { x = (int)regReadDword(L"FriendPosX", x); y = (int)regReadDword(L"FriendPosY", y); }
        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (x + w < vx + 40 || x > vx + vw - 40 || y + h < vy + 40 || y > vy + vh - 40) { x = mr.left - w - 12; y = mr.top; }
        DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT
                      | (app.settings.alwaysOnTop ? WS_EX_TOPMOST : 0);
        f.hwnd = CreateWindowExW(exStyle, FRIEND_CLASS, L"Whisku Friend", WS_POPUP, x, y, w, h,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    showFriendWindow(app, true);
}

void onLinkStatusChanged(AppState& app, catlink::Status before, catlink::Status now) {
    using catlink::Status;
    auto& f = app.friendCat;
    if (now == Status::FriendOffline || now == Status::Off || now == Status::FriendPaused) f.driver.releaseAll();
    if (now == Status::Off || now == Status::FriendPaused) showFriendWindow(app, false);
    if (now == Status::Linked && before != Status::Linked) {
        std::string name = app.link.friendName();
        std::wstring body = (name.empty() ? std::wstring(tr(L"朋友", L"Your friend")) : catlink::widen(name)) + tr(L" 的猫出现在你的桌面上了，它会跟着对方一起打字、动鼠标。", L"'s cat is on your desktop now. It types and moves the mouse along with them.");
        showTrayBalloon(app, tr(L"🔗 猫咪连线成功", L"\U0001F517 Cat Link connected"), body.c_str());
    }
    setTrayTip(app, linkTrayTip(app));
    updateFriendLabel(app);
}

catlink::Event linkProfile(const AppState& app) {
    catlink::Event e;
    e.k = "hello";
    e.coat = coatName(app.settings.coatTheme);
    e.kb = keyboardName(app.settings.keyboardTheme);
    e.eye = eyeName(app.settings.eyeTheme);
    if (!app.nickname.empty()) e.n = catlink::narrow(app.nickname);
    e.hp = g_audioWatcher.headphones.load() == 1;
    e.pl = g_audioWatcher.playing.load();
    return e;
}

void startLink(AppState& app, const catlink::Code& code) {
    app.savedLinkCode = code.display();
    regWrite(L"LinkCode", &app.savedLinkCode, 0);
    regWrite(L"LinkEnabled", nullptr, 1);
    app.link.start(code);
}

/// Every main-loop pass: networking, the friend's events, status changes.
void linkTick(AppState& app) {
    double now = nowSeconds();
    app.link.update(now, app.input);

    // Our sound output -> AirPods on our cat (and tell the friend)
    int hp = g_audioWatcher.headphones.load();
    if (hp >= 0 && hp != app.lastHeadphones) {
        app.physics.setHeadphonesConnected(hp == 1, app.lastHeadphones >= 0, app.settings);
        app.lastHeadphones = hp;
        app.link.profileChanged();
    }
    bool playing = g_audioWatcher.playing.load();
    if (playing != app.physics.headphonesPlaying) {
        app.physics.headphonesPlaying = playing;
        app.link.profileChanged();
    }

    std::vector<catlink::Session::Due> due = app.link.takeDue(now);
    while (!app.demoQueue.empty() && app.demoQueue.front().applyAt <= now) {
        due.push_back(app.demoQueue.front());
        app.demoQueue.pop_front();
    }
    for (auto& d : due) {
        const catlink::Event& e = d.e;
        if (e.k == "pet") {   // the friend is petting OUR cat from their screen
            Pt cp = { (float)e.x.value_or(184.0), (float)e.y.value_or(100.0) };
            app.physics.registerPetStroke(cp, (float)e.d.value_or(6.0), e.h.value_or(true), app.settings);
            if (now - app.lastHeart > 1.1) { app.lastHeart = now; app.physics.spawnFloatingNote(L"\u2665"); }   // ♥
            continue;
        }
        ensureFriendCat(app);
        auto& f = app.friendCat;
        f.settings.showKeyBadges = app.settings.showKeyBadges;
        f.settings.showMouseBadges = app.settings.showMouseBadges;
        bool hadProfile = f.driver.hasProfile;
        int button = -1;
        bool down = false;
        PhysicalKey key = f.driver.apply(e, now, button, down);
        if (key != PhysicalKey::None) f.physics.triggerKeyDown(key, f.settings);
        if (button >= 0) f.physics.triggerMouseClick(button, down, f.settings);
        if (e.k == "w") f.physics.triggerScroll((float)e.d.value_or(0.0), f.settings);
        if (e.k == "hello") {
            applyFriendProfile(app);
            f.physics.setHeadphonesConnected(e.hp.value_or(false), hadProfile, f.settings);
            f.physics.headphonesPlaying = e.pl.value_or(false);
            updateFriendLabel(app);
        }
    }
    if (app.demoUntil > 0.0 && now >= app.demoUntil) {   // demo over
        app.demoUntil = 0.0;
        catlink::Status st = app.link.status();
        if (st == catlink::Status::Off || st == catlink::Status::FriendPaused) showFriendWindow(app, false);
        updateFriendLabel(app);
    }
    catlink::Status st = app.link.status();
    if (st != app.lastLinkStatus) {
        catlink::Status before = app.lastLinkStatus;
        app.lastLinkStatus = st;
        onLinkStatusChanged(app, before, st);
    }
}

std::wstring readClipboardText(HWND owner) {
    std::wstring s;
    if (!OpenClipboard(owner)) return s;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* p = static_cast<const wchar_t*>(GlobalLock(h))) { s = p; GlobalUnlock(h); }
    }
    CloseClipboard();
    return s;
}

void writeClipboardText(HWND owner, const std::wstring& s) {
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* dst = GlobalLock(g)) { memcpy(dst, s.c_str(), bytes); GlobalUnlock(g); }
        if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
    }
    CloseClipboard();
}

/// A pretend friend for the menu demo: typing, fast mouse (hunting eyes), scrolling, a click, then
/// headphones with music. Entirely local: nothing is sent.
void startLinkDemo(AppState& app) {
    double t0 = nowSeconds();
    app.demoUntil = t0 + 24.0;
    app.demoQueue.clear();
    CatCoatTheme coats[] = { CatCoatTheme::ClassicBlack, CatCoatTheme::Siamese, CatCoatTheme::Shorthair, CatCoatTheme::OrangeWhite };
    std::vector<CatCoatTheme> others;
    for (auto c : coats) if (c != app.settings.coatTheme) others.push_back(c);
    std::minstd_rand rng((unsigned)GetTickCount64());
    CatCoatTheme coat = others[rng() % others.size()];
    auto hello = [&](bool hpOn) {
        catlink::Event e;
        e.k = "hello"; e.coat = coatName(coat); e.kb = "classicMilk";
        e.eye = coat == CatCoatTheme::Siamese ? "siameseBlue" : "softAmber";
        e.hp = hpOn; e.pl = hpOn;
        return e;
    };
    auto add = [&](double at, catlink::Event e) { app.demoQueue.push_back({ e, t0 + at }); };
    add(0, hello(false));
    double t = 0.5;
    int id = 1;
    while (t < 5.0) {
        catlink::Event kd; kd.k = "kd"; kd.id = id; kd.s = (rng() % 2) ? "l" : "r"; add(t, kd);
        catlink::Event ku; ku.k = "ku"; ku.id = id; add(t + 0.07, ku);
        ++id;
        t += 0.1 + (rng() % 120) / 1000.0;
        if (rng() % 9 == 0) t += 0.6;
    }
    for (int i = 0; i < 25; ++i) {
        double phase = i * 0.6;
        catlink::Event m; m.k = "m"; m.x = 0.5 + 0.3 * std::sin(phase); m.y = 0.5; m.vx = 1.6 * std::cos(phase); m.vy = 0.0;
        add(6.0 + i * 0.1, m);
    }
    catlink::Event stop; stop.k = "m"; stop.x = 0.6; stop.y = 0.5; stop.vx = 0.0; stop.vy = 0.0; add(8.6, stop);
    for (int i = 0; i < 15; ++i) { catlink::Event w; w.k = "w"; w.d = 5.0; add(9.0 + i * 0.1, w); }
    catlink::Event cd; cd.k = "c"; cd.b = 0; cd.d = 1.0; add(11.0, cd);
    catlink::Event cu; cu.k = "c"; cu.b = 0; cu.d = 0.0; add(11.12, cu);
    add(12.5, hello(true));
    add(21.0, hello(false));
    std::sort(app.demoQueue.begin(), app.demoQueue.end(), [](const auto& a, const auto& b) { return a.applyAt < b.applyAt; });
}

void appendLinkMenu(AppState& app, HMENU menu) {
    using catlink::Status;
    HMENU sub = CreatePopupMenu();
    Status st = app.link.status();
    std::string fname = app.link.friendName();
    std::wstring friendW = fname.empty() ? tr(L"朋友", L"your friend") : catlink::widen(fname);
    std::wstring status;
    switch (st) {
        case Status::Off: status = tr(L"未连线", L"not linked"); break;
        case Status::Connecting: status = tr(L"正在连接服务器…", L"connecting to the server\u2026"); break;
        case Status::WaitingForFriend: status = tr(L"已连接，等待朋友输入配对码", L"connected, waiting for your friend to enter the code"); break;
        case Status::Linked: status = tr(L"已和 ", L"linked with ") + friendW + tr(L" 连线 \u25CF", L" \u25CF"); break;
        case Status::FriendOffline: status = friendW + tr(L" 已离线，等待对方回来", L" is offline, waiting for them to come back"); break;
        case Status::Reconnecting: status = tr(L"网络中断，正在重新连接…", L"network lost, reconnecting\u2026"); break;
        case Status::FriendPaused: status = friendW + tr(L" 暂停了分享，暂时看不到 TA 的猫", L" paused sharing, so their cat is hidden"); break;
    }
    AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, (tr(L"状态：", L"Status: ") + status).c_str());
    std::wstring err = app.link.lastError();
    if (st == Status::Reconnecting && !err.empty()) AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, (tr(L"原因：", L"Reason: ") + err).c_str());
    AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
    if (st == Status::Off) {
        AppendMenuW(sub, MF_STRING, CMD_LINK_CREATE, tr(L"创建配对码（自动复制，发给朋友）", L"Create a pairing code (copied for you to send to a friend)"));
        AppendMenuW(sub, MF_STRING, CMD_LINK_JOIN, tr(L"粘贴朋友的配对码并连线（请先复制配对码）", L"Paste a friend's pairing code and link (copy it first)"));
        if (!app.savedLinkCode.empty()) AppendMenuW(sub, MF_STRING, CMD_LINK_RECONNECT, (tr(L"重新连线（", L"Reconnect (") + app.savedLinkCode + tr(L"）", L")")).c_str());
    } else if (auto code = app.link.code()) {
        AppendMenuW(sub, MF_STRING, CMD_LINK_COPY, (tr(L"复制配对码 ", L"Copy pairing code ") + code->display()).c_str());
        AppendMenuW(sub, MF_STRING, CMD_LINK_DISCONNECT, tr(L"断开连线", L"Disconnect"));
    }
    bool paused = app.link.paused();
    AppendMenuW(sub, MF_STRING | (paused ? MF_CHECKED : 0), CMD_LINK_PAUSE, tr(L"\u23F8 暂停分享我的猫", L"\u23F8 Pause sharing my cat"));
    if (paused) AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, tr(L"    朋友看不到你的猫，也收不到你的任何动作", L"    Your friend can't see your cat or any of your activity"));
    AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
    HMENU server = CreatePopupMenu();
    AppendMenuW(server, MF_STRING | (app.linkServer == 'C' ? MF_CHECKED : 0), CMD_LINK_SERVER_CN, tr(L"EMQX 中国（上海）", L"EMQX China (Shanghai)"));
    AppendMenuW(server, MF_STRING | (app.linkServer == 'G' ? MF_CHECKED : 0), CMD_LINK_SERVER_GLOBAL, tr(L"EMQX 国际（美国）", L"EMQX global (US)"));
    AppendMenuW(sub, MF_POPUP, (UINT_PTR)server, tr(L"新配对码使用的服务器", L"Server for new pairing codes"));
    std::wstring nickItem = tr(L"我的昵称：", L"My nickname: ") + (app.nickname.empty() ? std::wstring(tr(L"未设置", L"not set")) : app.nickname) + L"…";
    AppendMenuW(sub, MF_STRING, CMD_LINK_NICKNAME, nickItem.c_str());
    AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sub, MF_STRING, CMD_LINK_DEMO, tr(L"\U0001F3AC 演示：模拟一位朋友", L"\U0001F3AC Demo: pretend friend"));
    AppendMenuW(sub, MF_STRING, CMD_LINK_EXPLAIN, tr(L"对方能看到什么？", L"What does my friend see?"));
    const wchar_t* title = paused && st != Status::Off ? tr(L"\U0001F517 猫咪连线（暂停分享中）", L"\U0001F517 Cat Link (sharing paused)")
                         : st == Status::Linked || st == Status::FriendPaused ? tr(L"\U0001F517 猫咪连线（已连线）", L"\U0001F517 Cat Link (linked)")
                         : tr(L"\U0001F517 猫咪连线", L"\U0001F517 Cat Link");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)sub, title);
}

void handleLinkCommand(AppState& app, UINT cmd) {
    HWND owner = app.menuOwnerHwnd ? app.menuOwnerHwnd : app.hwnd;
    switch (cmd) {
        case CMD_LINK_CREATE: {
            catlink::Code code = catlink::Code::generate(app.linkServer);
            startLink(app, code);
            writeClipboardText(owner, code.display());
            std::wstring msg = tr(L"配对码已复制到剪贴板：\n\n    ", L"The pairing code is copied to the clipboard:\n\n    ") + code.display()
                + tr(L"\n\n把它发给你的朋友（比如用微信）。朋友在 Whisku 菜单「猫咪连线」里粘贴它，你们的猫就会出现在对方的屏幕上。", L"\n\nSend it to your friend (for example on WeChat). When they paste it in Whisku's Cat Link menu, your cats appear on each other's screens.")
                + tr(L"\n配对码就是你们的密钥，请只发给这位朋友。\n\n", L"\nThe code is your shared key, so send it only to this friend.\n\n") + linkWhatIsShared();
            MessageBoxW(nullptr, msg.c_str(), tr(L"Whisku 猫咪连线", L"Whisku Cat Link"), MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
            break;
        }
        case CMD_LINK_JOIN: {
            auto code = catlink::Code::parse(readClipboardText(owner));
            if (!code) {
                MessageBoxW(nullptr, tr(L"剪贴板里没有找到配对码。\n\n请先复制朋友发来的配对码（形如 WK-C-XXXX-XXXX-XXXX-XXXX-XXXX），再点这里。", L"No pairing code found on the clipboard.\n\nCopy the code your friend sent you (it looks like WK-C-XXXX-XXXX-XXXX-XXXX-XXXX), then click here."),
                            tr(L"Whisku 猫咪连线", L"Whisku Cat Link"), MB_OK | MB_ICONWARNING | MB_TOPMOST);
                break;
            }
            std::wstring msg = tr(L"要和这个配对码连线吗？\n\n    ", L"Link with this pairing code?\n\n    ") + code->display() + L"\n\n" + linkWhatIsShared();
            if (MessageBoxW(nullptr, msg.c_str(), tr(L"Whisku 猫咪连线", L"Whisku Cat Link"), MB_OKCANCEL | MB_ICONQUESTION | MB_TOPMOST) == IDOK) {
                startLink(app, *code);
            }
            break;
        }
        case CMD_LINK_RECONNECT:
            if (auto code = catlink::Code::parse(app.savedLinkCode)) startLink(app, *code);
            break;
        case CMD_LINK_COPY:
            if (auto code = app.link.code()) writeClipboardText(owner, code->display());
            break;
        case CMD_LINK_DISCONNECT:
            regWrite(L"LinkEnabled", nullptr, 0);
            app.link.stop();
            break;
        case CMD_LINK_SERVER_CN:
        case CMD_LINK_SERVER_GLOBAL: {
            app.linkServer = cmd == CMD_LINK_SERVER_CN ? 'C' : 'G';
            std::wstring v(1, (wchar_t)app.linkServer);
            regWrite(L"LinkServer", &v, 0);
            break;
        }
        case CMD_LINK_SHOW_FRIEND:
            app.showFriendCat = !app.showFriendCat;
            regWrite(L"LinkShowFriend", nullptr, app.showFriendCat ? 1 : 0);
            showFriendWindow(app, app.showFriendCat && (app.link.status() != catlink::Status::Off || nowSeconds() < app.demoUntil));
            break;
        case CMD_LINK_NICKNAME: {
            std::wstring name = app.nickname;
            if (promptText(owner, tr(L"你的昵称", L"Your nickname"), tr(L"会显示在朋友屏幕上你的猫的上方。", L"Shown above your cat on your friend's screen."), name)) {
                while (!name.empty() && iswspace(name.back())) name.pop_back();
                while (!name.empty() && iswspace(name.front())) name.erase(name.begin());
                if (name.size() > 16) name.resize(16);
                app.nickname = name;
                regWrite(L"LinkNickname", &app.nickname, 0);
                app.link.profileChanged();
            }
            break;
        }
        case CMD_LINK_DEMO:
            startLinkDemo(app);
            break;
        case CMD_LINK_PAUSE:
            app.link.setPaused(!app.link.paused());
            regWrite(L"LinkPaused", nullptr, app.link.paused() ? 1 : 0);
            setTrayTip(app, linkTrayTip(app));
            break;
        case CMD_LINK_EXPLAIN: {
            std::wstring msg = std::wstring(linkWhatIsShared()) + linkPauseAndDisconnect();
            MessageBoxW(nullptr, msg.c_str(), tr(L"猫咪连线会分享什么？", L"What does Cat Link share?"), MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
            break;
        }
        default:
            break;
    }
}

/// A Claude Code bubble appeared: cheer for "done", play the sound if our cat is on screen.
void onClaudeAlert(AppState& app, const claude::Alert* alert) {
    if (!alert) return;
    if (alert->kind == claude::Kind::Done) app.physics.cheer();
    if (app.claude.sound && app.isVisible) MessageBeep(alert->kind == claude::Kind::Done ? MB_OK : MB_ICONASTERISK);
}

void showContextMenu(AppState& app, POINT screenPt) {
    HMENU menu = CreatePopupMenu();

    // Status & live stats header
    std::wstring statusStr = app.input.isRawInputRegistered
        ? tr(L"\u25CF \u786C\u4EF6\u7EA7\u5168\u72B6\u6001\u76D1\u542C\u4E2D (Raw Input \u6E38\u620F/\u6253\u5B57)", L"\u25CF Hardware-level listening (Raw Input, typing and games)")
        : tr(L"\u26A0 Raw Input \u672A\u542F\u52A8", L"\u26A0 Raw Input not running");
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, statusStr.c_str());
    if (!app.isElevated && app.lastBlockedForegroundTime > 0.0 && nowSeconds() - app.lastBlockedForegroundTime < 600.0) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, tr(L"⚠ 刚才的游戏以管理员权限运行，键盘被 Windows 拦截了", L"\u26A0 The last game ran as administrator, so Windows hid its keyboard"));
    }

    std::wstring stats = tr(L"\u4ECA\u65E5\u51FB\u952E ", L"Keys today ") + std::to_wstring(app.input.totalKeystrokesToday)
        + L"  \u00B7  KPM " + std::to_wstring(app.input.currentKPM())
        + tr(L"  \u00B7  \u6478\u732B ", L"  \u00B7  Pets ") + std::to_wstring(app.physics.totalPetSessions) + tr(L" \u6B21", L"");
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, stats.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // Which cats to show: ours and the friend's (Cat Link) independently, or all at once.
    HMENU showMenu = CreatePopupMenu();
    AppendMenuW(showMenu, MF_STRING | (app.isVisible ? MF_CHECKED : 0), CMD_TOGGLE_VISIBLE, tr(L"我的猫", L"My cat"));
    AppendMenuW(showMenu, MF_STRING | (app.showFriendCat ? MF_CHECKED : 0), CMD_LINK_SHOW_FRIEND, tr(L"朋友的猫", L"Friend's cat"));
    AppendMenuW(showMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(showMenu, MF_STRING, CMD_SHOW_ALL, tr(L"全部显示", L"Show all"));
    AppendMenuW(showMenu, MF_STRING, CMD_HIDE_ALL, tr(L"全部隐藏", L"Hide all"));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)showMenu, tr(L"显示的猫咪", L"Cats to show"));
    AppendMenuW(menu, MF_STRING | (app.settings.fullClickThrough ? MF_CHECKED : 0), CMD_TOGGLE_CLICKTHROUGH, tr(L"\u5B8C\u5168\u9F20\u6807\u7A7F\u900F (Ctrl+Alt+Shift+P / \u6309\u4F4FAlt\u53EF\u53F3\u952E)", L"Full click-through (Ctrl+Alt+Shift+P / hold Alt to right-click)"));
    AppendMenuW(menu, MF_STRING | (app.settings.alwaysOnTop ? MF_CHECKED : 0), CMD_TOGGLE_TOPMOST, tr(L"\u7A97\u53E3\u7F6E\u9876\u663E\u793A", L"Always on top"));
    AppendMenuW(menu, MF_STRING | (isAutoStartEnabled() ? MF_CHECKED : 0), CMD_TOGGLE_AUTOSTART, tr(L"\u5F00\u673A\u81EA\u52A8\u542F\u52A8", L"Start with Windows"));
    if (app.isElevated) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, tr(L"\u2713 \u5DF2\u4EE5\u7BA1\u7406\u5458\u8EAB\u4EFD\u8FD0\u884C\uFF08\u7BA1\u7406\u5458\u6743\u9650\u6E38\u620F\u7684\u952E\u76D8\u4E5F\u80FD\u76D1\u542C\uFF09", L"\u2713 Running as administrator (sees keys in admin games too)"));
    } else {
        AppendMenuW(menu, MF_STRING, CMD_RELAUNCH_ADMIN, tr(L"\u4EE5\u7BA1\u7406\u5458\u8EAB\u4EFD\u91CD\u65B0\u542F\u52A8\uFF08\u6E38\u620F\u91CC\u952E\u76D8\u6CA1\u53CD\u5E94\u65F6\u70B9\u8FD9\u91CC\uFF09", L"Restart as administrator (if keys don't show in a game)"));
    }
    AppendMenuW(menu, MF_STRING | (app.settings.runAsAdmin ? MF_CHECKED : 0), CMD_TOGGLE_RUN_AS_ADMIN, tr(L"\u6BCF\u6B21\u542F\u52A8\u90FD\u4EE5\u7BA1\u7406\u5458\u8EAB\u4EFD\u8FD0\u884C", L"Always run as administrator"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING | (app.settings.showKeyBadges ? MF_CHECKED : 0), CMD_TOGGLE_BADGES, tr(L"\u663E\u793A\u6309\u952E\u6C14\u6CE1 ([S] [W] [\u21E7])", L"Show key bubbles ([S] [W] [\u21E7])"));
    AppendMenuW(menu, MF_STRING | (app.settings.showMouseBadges ? MF_CHECKED : 0), CMD_TOGGLE_MOUSE_BADGES, tr(L"\u663E\u793A\u9F20\u6807\u70B9\u51FB\u6C14\u6CE1 ([\u5DE6\u952E] [\u53F3\u952E])", L"Show mouse click bubbles ([Left] [Right])"));
    AppendMenuW(menu, MF_STRING | (app.settings.showKeyLegends ? MF_CHECKED : 0), CMD_TOGGLE_KEY_LEGENDS, tr(L"\u663E\u793A\u952E\u76D8\u5B57\u7B26", L"Show key labels"));
    AppendMenuW(menu, MF_STRING | (app.settings.footEasterEgg ? MF_CHECKED : 0), CMD_TOGGLE_FOOT, tr(L"3\u952E\u540C\u65F6\u6309\u300C\u7528\u811A\u6309\u300D\u5F69\u86CB", L"3 keys at once: press one with a foot"));
    AppendMenuW(menu, MF_STRING | (app.settings.petInteraction ? MF_CHECKED : 0), CMD_TOGGLE_PET, tr(L"\u6478\u5934/\u6478\u80CC\u95ED\u773C\u4E92\u52A8", L"Close eyes when petted"));
    AppendMenuW(menu, MF_STRING | (app.settings.earbudAnimation ? MF_CHECKED : 0), CMD_TOGGLE_EARBUDS, tr(L"连接耳机时戴上 AirPods \U0001F3A7", L"Wear AirPods with headphones \U0001F3A7"));
    AppendMenuW(menu, MF_STRING | (app.settings.mosquitoVisits ? MF_CHECKED : 0), CMD_TOGGLE_MOSQUITO, tr(L"睡着时会有蚊子来捣乱 \U0001F99F", L"Mosquito visits while asleep \U0001F99F"));
    AppendMenuW(menu, MF_STRING, CMD_MOSQUITO_DEMO, tr(L"\U0001F3AC 演示：蚊子来了", L"\U0001F3AC Demo: here comes a mosquito"));
    HMENU claudeMenu = CreatePopupMenu();
    AppendMenuW(claudeMenu, MF_STRING | (app.claude.enabled ? MF_CHECKED : 0), CMD_CLAUDE_TOGGLE, tr(L"完成或等你确认时提醒", L"Alert when a session is done or needs you"));
    AppendMenuW(claudeMenu, MF_STRING | (app.claude.sound ? MF_CHECKED : 0), CMD_CLAUDE_SOUND, tr(L"提示音", L"Sound"));
    AppendMenuW(claudeMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(claudeMenu, MF_STRING | (app.claude.enabled ? 0 : MF_GRAYED), CMD_CLAUDE_TEST_DONE, tr(L"试一下「完成了」", L"Try \u201Cdone\u201D"));
    AppendMenuW(claudeMenu, MF_STRING | (app.claude.enabled ? 0 : MF_GRAYED), CMD_CLAUDE_TEST_NEEDS, tr(L"试一下「等你确认」", L"Try \u201Cneeds you\u201D"));
    AppendMenuW(claudeMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(claudeMenu, MF_STRING, CMD_CLAUDE_COPY, tr(L"复制 Claude Code 钩子设置", L"Copy Claude Code hook settings"));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)claudeMenu, tr(L"Claude Code 提醒 \U0001F916", L"Claude Code alerts \U0001F916"));
    HMENU breakMenu = CreatePopupMenu();
    if (app.breaks.minutes > 0) {
        double now = breaks::clock();
        std::wstring status = app.breaks.logic.phase == breaks::Phase::Rest ? std::wstring(tr(L"正在休息", L"On a break"))
            : tr(L"已连续工作 ", L"Working for ") + breaks::duration(app.breaks.logic.workedSeconds(now));
        AppendMenuW(breakMenu, MF_STRING | MF_GRAYED, 0, status.c_str());
        AppendMenuW(breakMenu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(breakMenu, MF_STRING | MF_GRAYED, 0, tr(L"工作多久后提醒：", L"Remind me after working:"));
    for (int m : breaks::Center::Choices) {
        int cmd = m == 0 ? CMD_BREAK_OFF : m == 30 ? CMD_BREAK_30 : m == 60 ? CMD_BREAK_60 : m == 120 ? CMD_BREAK_120 : CMD_BREAK_180;
        std::wstring title = L"    " + (m == 0 ? std::wstring(tr(L"关闭", L"Off")) : breaks::duration(m * 60.0));
        AppendMenuW(breakMenu, MF_STRING | (app.breaks.minutes == m ? MF_CHECKED : 0), cmd, title.c_str());
    }
    AppendMenuW(breakMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(breakMenu, MF_STRING, CMD_BREAK_PREVIEW, tr(L"预览提醒", L"Preview the reminder"));
    AppendMenuW(breakMenu, MF_STRING | MF_GRAYED, 0, tr(L"离开 15 分钟、点「好的」后离开 2 分钟，或锁屏 2 分钟，都算休息",
                                                        L"A break: 15 min away, 2 min after OK, or 2 min locked"));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)breakMenu, tr(L"休息提醒 \u2615", L"Break reminder \u2615"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // Cat Coat Skin submenu
    HMENU coatMenu = CreatePopupMenu();
    AppendMenuW(coatMenu, MF_STRING | (app.settings.coatTheme == CatCoatTheme::ClassicBlack ? MF_CHECKED : 0), CMD_COAT_BLACK, tr(L"\u7ECF\u5178\u5C0F\u9ED1\u732B (\u539F\u7248)", L"Classic black cat (original)"));
    AppendMenuW(coatMenu, MF_STRING | (app.settings.coatTheme == CatCoatTheme::Siamese ? MF_CHECKED : 0), CMD_COAT_SIAMESE, tr(L"\u6316\u7164\u66B9\u7F57\u732B", L"Seal-point Siamese"));
    AppendMenuW(coatMenu, MF_STRING | (app.settings.coatTheme == CatCoatTheme::Shorthair ? MF_CHECKED : 0), CMD_COAT_SHORTHAIR, tr(L"美短（银虎斑加白）", L"American Shorthair (silver tabby & white)"));
    AppendMenuW(coatMenu, MF_STRING | (app.settings.coatTheme == CatCoatTheme::OrangeWhite ? MF_CHECKED : 0), CMD_COAT_ORANGE_WHITE, tr(L"橘白猫", L"Orange & white"));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)coatMenu, tr(L"\u732B\u54AA\u76AE\u80A4", L"Cat coat"));

    // Keyboard Theme submenu
    HMENU kbMenu = CreatePopupMenu();
    AppendMenuW(kbMenu, MF_STRING | (app.settings.keyboardTheme == KeyboardTheme::ClassicMilk ? MF_CHECKED : 0), CMD_KB_MILK, tr(L"\u7ECF\u5178\u5976\u767D\u6696\u9EC4 (\u539F\u7248)", L"Classic cream & warm yellow (original)"));
    AppendMenuW(kbMenu, MF_STRING | (app.settings.keyboardTheme == KeyboardTheme::MidnightHacker ? MF_CHECKED : 0), CMD_KB_HACKER, tr(L"\u6697\u591C\u6781\u5BA2\u9ED1", L"Midnight hacker black"));
    AppendMenuW(kbMenu, MF_STRING | (app.settings.keyboardTheme == KeyboardTheme::RetroCream ? MF_CHECKED : 0), CMD_KB_RETRO, tr(L"\u590D\u53E4\u5976\u6CB9\u7070", L"Retro cream grey"));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)kbMenu, tr(L"\u952E\u76D8\u914D\u8272", L"Keyboard colors"));

    // Eye Color submenu
    HMENU eyeMenu = CreatePopupMenu();
    AppendMenuW(eyeMenu, MF_STRING | (app.settings.eyeTheme == EyeColorTheme::NeonChartreuse ? MF_CHECKED : 0), CMD_EYE_LIME, tr(L"\u7ECF\u5178\u8367\u5149\u9EC4\u7EFF (\u539F\u7248)", L"Classic neon chartreuse (original)"));
    AppendMenuW(eyeMenu, MF_STRING | (app.settings.eyeTheme == EyeColorTheme::SiameseBlue ? MF_CHECKED : 0), CMD_EYE_BLUE, tr(L"\u66B9\u7F57\u5B9D\u77F3\u84DD", L"Siamese sapphire blue"));
    AppendMenuW(eyeMenu, MF_STRING | (app.settings.eyeTheme == EyeColorTheme::WarmAmber ? MF_CHECKED : 0), CMD_EYE_AMBER, tr(L"\u7425\u73C0\u91D1\u77B3", L"Amber gold"));
    AppendMenuW(eyeMenu, MF_STRING | (app.settings.eyeTheme == EyeColorTheme::IceCyan ? MF_CHECKED : 0), CMD_EYE_CYAN, tr(L"\u51B0\u6676\u6D45\u84DD", L"Ice blue"));
    AppendMenuW(eyeMenu, MF_STRING | (app.settings.eyeTheme == EyeColorTheme::SoftAmber ? MF_CHECKED : 0), CMD_EYE_SOFT_AMBER, tr(L"柔和琥珀", L"Soft amber"));
    AppendMenuW(eyeMenu, MF_STRING | (app.settings.eyeTheme == EyeColorTheme::ReferenceHazel ? MF_CHECKED : 0), CMD_EYE_REFERENCE_HAZEL, tr(L"淡金青绿", L"Pale gold-green"));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)eyeMenu, tr(L"\u77B3\u8272\u5207\u6362", L"Eye color"));

    // Scale submenu
    HMENU scaleMenu = CreatePopupMenu();
    auto chk = [&](float v) -> UINT { return std::fabs(app.settings.scale - v) < 0.05f ? MF_CHECKED : 0; };
    AppendMenuW(scaleMenu, MF_STRING | chk(0.65f), CMD_SCALE_65,  tr(L"\u8FF7\u4F60 (0.65x)", L"Mini (0.65x)"));
    AppendMenuW(scaleMenu, MF_STRING | chk(0.85f), CMD_SCALE_85,  tr(L"\u504F\u5C0F (0.85x)", L"Small (0.85x)"));
    AppendMenuW(scaleMenu, MF_STRING | chk(1.00f), CMD_SCALE_100, tr(L"\u6807\u51C6 (1.0x)", L"Standard (1.0x)"));
    AppendMenuW(scaleMenu, MF_STRING | chk(1.25f), CMD_SCALE_125, tr(L"\u504F\u5927 (1.25x)", L"Large (1.25x)"));
    AppendMenuW(scaleMenu, MF_STRING | chk(1.50f), CMD_SCALE_150, tr(L"\u8D85\u5927 (1.5x)", L"Extra large (1.5x)"));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)scaleMenu, tr(L"\u5C3A\u5BF8\u5927\u5C0F", L"Size"));

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    appendLinkMenu(app, menu);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CMD_DEMO_BURST, tr(L"\u6F14\u793A\u6253\u5B57\u52A8\u753B", L"Demo: typing"));
    AppendMenuW(menu, MF_STRING, CMD_RESET_POS, tr(L"\u91CD\u7F6E\u4F4D\u7F6E\u5230\u53F3\u4E0B\u89D2", L"Move back to the bottom-right corner"));
    // Language: the title is in both languages so it can be found whichever one is showing.
    HMENU langMenu = CreatePopupMenu();
    l10n::Language lang = l10n::current();
    AppendMenuW(langMenu, MF_STRING | (lang == l10n::Language::System ? MF_CHECKED : 0), CMD_LANG_SYSTEM, L"跟随系统 / System");
    AppendMenuW(langMenu, MF_STRING | (lang == l10n::Language::Chinese ? MF_CHECKED : 0), CMD_LANG_CHINESE, L"中文");
    AppendMenuW(langMenu, MF_STRING | (lang == l10n::Language::English ? MF_CHECKED : 0), CMD_LANG_ENGLISH, L"English");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)langMenu, L"语言 / Language");
    if (!UPDATE_CHECKS_ENABLED) {
        std::wstring verTxt = std::wstring(tr(L"版本 v", L"Version v")) + APP_VERSION + tr(L"（分支版，已关闭更新检查）", L" (fork, update check off)");
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, verTxt.c_str());
    } else if (!app.availableUpdateVersion.empty()) {
        std::wstring upTxt = app.isUpdating
            ? (tr(L"\u23F3 \u6B63\u5728\u4E0B\u8F7D\u66F4\u65B0 ", L"\u23F3 Downloading update ") + app.availableUpdateVersion + L"...")
            : (tr(L"\U0001F195 \u53D1\u73B0\u65B0\u7248\u672C ", L"\U0001F195 New version ") + app.availableUpdateVersion + tr(L" (\u70B9\u51FB\u81EA\u52A8\u66F4\u65B0)", L" (click to update)"));
        AppendMenuW(menu, MF_STRING | (app.isUpdating ? MF_GRAYED : 0), CMD_OPEN_UPDATE, upTxt.c_str());
    } else {
        std::wstring chkTxt = std::wstring(tr(L"\u68C0\u67E5\u66F4\u65B0 (\u5F53\u524D v", L"Check for updates (now v")) + APP_VERSION + L")";
        AppendMenuW(menu, MF_STRING, CMD_CHECK_UPDATE, chkTxt.c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CMD_QUIT, tr(L"\u9000\u51FA Whisku", L"Quit Whisku"));

    HWND menuOwner = app.menuOwnerHwnd ? app.menuOwnerHwnd : app.hwnd;
    SetForegroundWindow(menuOwner);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, menuOwner, nullptr);
    PostMessageW(menuOwner, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (cmd) {
        case CMD_TOGGLE_VISIBLE:
            app.isVisible = !app.isVisible;
            ShowWindow(app.hwnd, app.isVisible ? SW_SHOWNOACTIVATE : SW_HIDE);
            break;
        case CMD_SHOW_ALL:
        case CMD_HIDE_ALL: {
            bool show = cmd == CMD_SHOW_ALL;
            app.isVisible = show;
            ShowWindow(app.hwnd, show ? SW_SHOWNOACTIVATE : SW_HIDE);
            app.showFriendCat = show;
            regWrite(L"LinkShowFriend", nullptr, show ? 1 : 0);
            showFriendWindow(app, show && (app.link.status() != catlink::Status::Off || nowSeconds() < app.demoUntil));
            break;
        }
        case CMD_TOGGLE_CLICKTHROUGH:
            app.settings.fullClickThrough = !app.settings.fullClickThrough;
            app.settings.saveToRegistry();
            if (app.settings.fullClickThrough) {
                showTrayBalloon(app,
                    tr(L"Whisku \u5DF2\u5F00\u542F\u5B8C\u5168\u9F20\u6807\u7A7F\u900F", L"Whisku full click-through is on"),
                    tr(L"\u53D6\u6D88\u65B9\u5F0F\uFF1A\u6309 Ctrl+Alt+Shift+P\uFF0C\u6216\u6309\u4F4F Alt \u952E\u53F3\u952E\u5C0F\u732B\uFF0C\u6216\u70B9\u53F3\u4E0B\u89D2\u6258\u76D8\u5C0F\u732B\u56FE\u6807\uFF0C\u6216\u518D\u6B21\u53CC\u51FB Whisku.exe", L"To turn it off: press Ctrl+Alt+Shift+P, hold Alt and right-click the cat, click the cat in the tray, or open Whisku.exe again"));
            }
            break;
        case CMD_TOGGLE_TOPMOST:
            app.settings.alwaysOnTop = !app.settings.alwaysOnTop;
            applyWindowScaleAndPosition(app, false);
            break;
        case CMD_TOGGLE_AUTOSTART:
            setAutoStartEnabled(!isAutoStartEnabled());
            break;
        case CMD_TOGGLE_BADGES:
            app.settings.showKeyBadges = !app.settings.showKeyBadges;
            app.settings.saveToRegistry();
            break;
        case CMD_TOGGLE_MOUSE_BADGES:
            app.settings.showMouseBadges = !app.settings.showMouseBadges;
            app.settings.saveToRegistry();
            break;
        case CMD_TOGGLE_KEY_LEGENDS:
            app.settings.showKeyLegends = !app.settings.showKeyLegends;
            app.settings.saveToRegistry();
            break;
        case CMD_TOGGLE_FOOT:
            app.settings.footEasterEgg = !app.settings.footEasterEgg;
            app.settings.saveToRegistry();
            break;
        case CMD_TOGGLE_PET:
            app.settings.petInteraction = !app.settings.petInteraction;
            app.settings.saveToRegistry();
            break;
        case CMD_COAT_BLACK:
            app.settings.coatTheme = CatCoatTheme::ClassicBlack;
            app.settings.eyeTheme = EyeColorTheme::NeonChartreuse;
            app.settings.saveToRegistry();
            break;
        case CMD_COAT_SIAMESE:
            app.settings.coatTheme = CatCoatTheme::Siamese;
            app.settings.eyeTheme = EyeColorTheme::SiameseBlue;
            app.settings.saveToRegistry();
            break;
        case CMD_COAT_SHORTHAIR:
        case CMD_COAT_ORANGE_WHITE:
            app.settings.coatTheme = cmd == CMD_COAT_SHORTHAIR ? CatCoatTheme::Shorthair : CatCoatTheme::OrangeWhite;
            app.settings.eyeTheme = EyeColorTheme::SoftAmber;
            app.settings.saveToRegistry();
            break;
        case CMD_EYE_SOFT_AMBER: app.settings.eyeTheme = EyeColorTheme::SoftAmber; app.settings.saveToRegistry(); break;
        case CMD_EYE_REFERENCE_HAZEL: app.settings.eyeTheme = EyeColorTheme::ReferenceHazel; app.settings.saveToRegistry(); break;
        case CMD_KB_MILK:   app.settings.keyboardTheme = KeyboardTheme::ClassicMilk; app.settings.saveToRegistry(); break;
        case CMD_KB_HACKER: app.settings.keyboardTheme = KeyboardTheme::MidnightHacker; app.settings.saveToRegistry(); break;
        case CMD_KB_RETRO:  app.settings.keyboardTheme = KeyboardTheme::RetroCream; app.settings.saveToRegistry(); break;
        case CMD_EYE_LIME:  app.settings.eyeTheme = EyeColorTheme::NeonChartreuse; app.settings.saveToRegistry(); break;
        case CMD_EYE_BLUE:  app.settings.eyeTheme = EyeColorTheme::SiameseBlue; app.settings.saveToRegistry(); break;
        case CMD_EYE_AMBER: app.settings.eyeTheme = EyeColorTheme::WarmAmber; app.settings.saveToRegistry(); break;
        case CMD_EYE_CYAN:  app.settings.eyeTheme = EyeColorTheme::IceCyan; app.settings.saveToRegistry(); break;
        case CMD_SCALE_65:  app.settings.scale = 0.65f; applyWindowScaleAndPosition(app, true); break;
        case CMD_SCALE_85:  app.settings.scale = 0.85f; applyWindowScaleAndPosition(app, true); break;
        case CMD_SCALE_100: app.settings.scale = 1.00f; applyWindowScaleAndPosition(app, true); break;
        case CMD_SCALE_125: app.settings.scale = 1.25f; applyWindowScaleAndPosition(app, true); break;
        case CMD_SCALE_150: app.settings.scale = 1.50f; applyWindowScaleAndPosition(app, true); break;
        case CMD_DEMO_BURST:
            app.demoActive = true;
            app.demoStartTime = nowSeconds();
            app.demoStepIdx = 0;
            break;
        case CMD_RESET_POS:
            app.settings.posX = -99999;
            app.settings.posY = -99999;
            applyWindowScaleAndPosition(app, false);
            break;
        case CMD_CHECK_UPDATE:
            if (app.menuOwnerHwnd) startAsyncUpdateCheck(app.menuOwnerHwnd, true, 0);
            break;
        case CMD_OPEN_UPDATE: {
            startInAppUpdate(app);
            break;
        }
        case CMD_RELAUNCH_ADMIN:
            // The elevated copy starts with --takeover and waits for this one to exit.
            if (relaunchSelfElevated(L"--takeover --no-elevate")) quitApp(app);
            break;
        case CMD_TOGGLE_RUN_AS_ADMIN:
            app.settings.runAsAdmin = !app.settings.runAsAdmin;
            app.settings.saveToRegistry();
            if (app.settings.runAsAdmin && !app.isElevated && relaunchSelfElevated(L"--takeover --no-elevate")) {
                quitApp(app);
            }
            break;
        case CMD_LINK_CREATE: case CMD_LINK_JOIN: case CMD_LINK_RECONNECT: case CMD_LINK_COPY:
        case CMD_LINK_DISCONNECT: case CMD_LINK_SERVER_CN: case CMD_LINK_SERVER_GLOBAL:
        case CMD_LINK_SHOW_FRIEND: case CMD_LINK_EXPLAIN: case CMD_LINK_NICKNAME: case CMD_LINK_DEMO:
        case CMD_LINK_PAUSE:
            handleLinkCommand(app, cmd);
            break;
        case CMD_TOGGLE_EARBUDS:
            app.settings.earbudAnimation = !app.settings.earbudAnimation;
            app.settings.saveToRegistry();
            break;
        case CMD_TOGGLE_MOSQUITO:
            app.settings.mosquitoVisits = !app.settings.mosquitoVisits;
            app.settings.saveToRegistry();
            break;
        case CMD_MOSQUITO_DEMO:
            app.physics.summonMosquito();
            break;
        case CMD_LANG_SYSTEM: case CMD_LANG_CHINESE: case CMD_LANG_ENGLISH:
            l10n::set(cmd == CMD_LANG_CHINESE ? l10n::Language::Chinese : cmd == CMD_LANG_ENGLISH ? l10n::Language::English : l10n::Language::System);
            // Menus are rebuilt each time they open; refresh the tray tip and the friend's name tag.
            setTrayTip(app, linkTrayTip(app));
            updateFriendLabel(app);
            break;
        case CMD_CLAUDE_TOGGLE:
            app.claude.enabled = !app.claude.enabled;
            if (!app.claude.enabled) app.claude.alerts.clear();
            regWrite(L"ClaudeAlerts", nullptr, app.claude.enabled ? 1 : 0);
            break;
        case CMD_CLAUDE_SOUND:
            app.claude.sound = !app.claude.sound;
            regWrite(L"ClaudeAlertSound", nullptr, app.claude.sound ? 1 : 0);
            break;
        case CMD_CLAUDE_TEST_DONE:
            onClaudeAlert(app, app.claude.receive(claude::Kind::Done, L"Whisku", L"menu-test-done", nullptr, nowSeconds()));
            break;
        case CMD_CLAUDE_TEST_NEEDS:
            onClaudeAlert(app, app.claude.receive(claude::Kind::NeedsYou, L"Whisku", L"menu-test-needs", nullptr, nowSeconds()));
            break;
        case CMD_BREAK_OFF: case CMD_BREAK_30: case CMD_BREAK_60: case CMD_BREAK_120: case CMD_BREAK_180: {
            int m = cmd == CMD_BREAK_OFF ? 0 : cmd == CMD_BREAK_30 ? 30 : cmd == CMD_BREAK_60 ? 60 : cmd == CMD_BREAK_120 ? 120 : 180;
            app.breaks.setMinutes(m, breaks::clock());
            regWrite(L"BreakReminderMinutes", nullptr, (DWORD)m);
            break;
        }
        case CMD_BREAK_PREVIEW:
            app.breaks.showPreview(breaks::clock());
            break;
        case CMD_CLAUDE_COPY: {
            wchar_t exe[MAX_PATH * 2] = {};
            GetModuleFileNameW(nullptr, exe, MAX_PATH * 2 - 1);
            writeClipboardText(app.menuOwnerHwnd, claude::settingsSnippet(exe));
            MessageBoxW(app.menuOwnerHwnd,
                tr(L"把它合并到 %USERPROFILE%\\.claude\\settings.json 的 \"hooks\" 里，然后重新打开 Claude Code。\n\n"
                   L"之后每当一个会话完成，或者 Claude 等你确认时，小猫都会提醒你。只在本机，不会通过猫咪连线发给朋友。\n\n"
                   L"如果以后移动了 Whisku.exe，需要重新复制一次。",
                   L"Merge it into the \"hooks\" section of %USERPROFILE%\\.claude\\settings.json, then reopen Claude Code.\n\n"
                   L"From then on, the cat tells you whenever a session finishes or Claude needs your OK. It stays on this computer and never goes over Cat Link.\n\n"
                   L"If you move Whisku.exe later, copy the settings again."),
                tr(L"已复制 Claude Code 钩子设置", L"Claude Code hook settings copied"), MB_OK | MB_ICONINFORMATION);
            break;
        }
        case CMD_QUIT:
            quitApp(app);
            break;
        default:
            break;
    }
    // Theme changes: tell the friend right away.
    if (cmd >= CMD_COAT_BLACK && cmd <= CMD_EYE_REFERENCE_HAZEL) app.link.profileChanged();
}

bool physicsLooksSane(const CatPhysics& p) {
    const float vals[] = {
        p.leftPaw.x.value, p.leftPaw.y.value, p.rightPaw.x.value, p.rightPaw.y.value,
        p.hindFoot.x.value, p.hindFoot.y.value, p.bodyShiftX.value, p.bodyTilt.value,
        p.bodySquash.value, p.headTilt.value, p.headYaw.value, p.headPitch.value,
        p.headOffsetY.value, p.eyeGaze.x.value, p.eyeGaze.y.value, p.pupilScale.value,
        p.huntIntensity.value, p.mouseOffset.x.value, p.mouseOffset.y.value, p.time
    };
    for (float v : vals) {
        if (!std::isfinite(v) || std::fabs(v) > 1.0e6f) return false;
    }
    return true;
}

void drawFriendLabel(Gdiplus::Graphics& g, const std::wstring& text, bool dim) {
    if (text.empty()) return;
    Gdiplus::FontFamily yahei(L"Microsoft YaHei UI");
    const Gdiplus::FontFamily* family = yahei.IsAvailable() ? &yahei : Gdiplus::FontFamily::GenericSansSerif();
    Gdiplus::Font font(family, 11.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    Gdiplus::RectF bounds;
    g.MeasureString(text.c_str(), -1, &font, Gdiplus::PointF(0.0f, 0.0f), &bounds);
    float h = bounds.Height + 4.0f, w = bounds.Width + 14.0f;
    float x = CatPhysics::SeatAnchor.x - w / 2.0f, y = 46.0f;   // just above the ear tips (same as macOS nameLabelTop)
    Gdiplus::GraphicsPath pill;
    pill.AddArc(x, y, h, h, 90.0f, 180.0f);
    pill.AddArc(x + w - h, y, h, h, 270.0f, 180.0f);
    pill.CloseFigure();
    Gdiplus::SolidBrush bg(Gdiplus::Color(dim ? 90 : 158, 0, 0, 0));
    g.FillPath(&bg, &pill);
    Gdiplus::SolidBrush fg(Gdiplus::Color(dim ? 185 : 255, 255, 255, 255));
    g.DrawString(text.c_str(), -1, &font, Gdiplus::PointF(x + 7.0f, y + 2.0f), &fg);
}

/// The friend's cat: same physics and renderer, driven by their events instead of this PC's input.
void renderFriendCat(AppState& app, float dt) {
    auto& f = app.friendCat;
    if (!f.hwnd || !f.shown) return;
    double now = nowSeconds();
    RECT wr = {};
    GetWindowRect(f.hwnd, &wr);
    int w = std::max(1, (int)(wr.right - wr.left));
    int h = std::max(1, (int)(wr.bottom - wr.top));

    f.settings.fullClickThrough = app.settings.fullClickThrough;
    f.settings.petInteraction = app.settings.petInteraction;
    f.settings.footEasterEgg = app.settings.footEasterEgg;
    f.settings.showMouseBadges = app.settings.showMouseBadges;
    f.settings.showKeyBadges = app.settings.showKeyBadges;   // bubbles show the random stand-in keys
    f.settings.showKeyLegends = app.settings.showKeyLegends;
    f.driver.prepareFrame(now);
    f.physics.step(dt, wr, f.driver.input, f.settings);
    if (!physicsLooksSane(f.physics)) f.physics = CatPhysics();

    // Petting the friend's cat: it purrs here, and their own cat feels it too.
    if (!f.dragging) {
        POINT cur = {};
        GetCursorPos(&cur);
        if (PtInRect(&wr, cur)) {
            Pt cp = { (float)(cur.x - wr.left) / (float)w * CatPhysics::CanvasWidth,
                      (float)(cur.y - wr.top) / (float)h * CatPhysics::CanvasHeight };
            CatHitZone zone = CatRenderer::hitTest(cp, f.physics);
            if (zone == CatHitZone::Head || zone == CatHitZone::Body) {
                if (f.hasLastHover) {
                    float d = std::hypot(cp.x - f.lastHover.x, cp.y - f.lastHover.y);
                    if (d > 0.5f && d < 80.0f) {
                        f.physics.registerPetStroke(cp, d, zone == CatHitZone::Head, f.settings);
                        if (now >= app.demoUntil) app.link.notePet(cp, d, zone == CatHitZone::Head);
                    }
                }
                f.lastHover = cp;
                f.hasLastHover = true;
            } else {
                f.hasLastHover = false;
            }
            bool altHeld = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
            setClickThrough(f.hwnd, (app.settings.fullClickThrough && !altHeld) || zone == CatHitZone::None);
        } else {
            f.hasLastHover = false;
            setClickThrough(f.hwnd, true);
        }
    }

    f.ensureBuffer(w, h);
    if (!f.memDC || !f.dibBits) return;
    Gdiplus::Bitmap bmp(w, h, w * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(f.dibBits));
    Gdiplus::Graphics g(&bmp);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    g.ScaleTransform((float)w / CatPhysics::CanvasWidth, (float)h / CatPhysics::CanvasHeight);
    CatRenderer::draw(g, f.physics, f.settings);
    drawFriendLabel(g, f.label, f.labelDim);

    POINT ptDst = { wr.left, wr.top };
    SIZE szWnd = { w, h };
    POINT ptSrc = { 0, 0 };
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    HDC screenDC = GetDC(nullptr);
    if (!UpdateLayeredWindow(f.hwnd, screenDC, &ptDst, &szWnd, f.memDC, &ptSrc, 0, &blend, ULW_ALPHA)) f.destroyBuffer();
    ReleaseDC(nullptr, screenDC);
}

void tickAndRender(AppState& app) {
    if (!app.hwnd) return;

    double now = nowSeconds();
    app.lastRenderTime = now;
    double elapsed = now - app.lastTickTime;
    float dt = (float)elapsed;
    app.lastTickTime = now;
    if (dt <= 0.0f || dt > 0.2f) dt = 1.0f / 60.0f;

    if (!app.isVisible) {
        // Our cat is hidden: still track mouse speed (Cat Link sends it) and draw the friend's cat.
        app.input.pollMouse((float)std::clamp(elapsed, 0.001, 0.5));
        renderFriendCat(app, dt);
        return;
    }

    // Optional menu demo typing burst
    if (app.demoActive) {
        static const PhysicalKey demoKeys[] = {
            PhysicalKey::W, PhysicalKey::A, PhysicalKey::S, PhysicalKey::D,
            PhysicalKey::F, PhysicalKey::J, PhysicalKey::O, PhysicalKey::E,
            PhysicalKey::Space, PhysicalKey::LeftShift
        };
        int targetIdx = (int)((now - app.demoStartTime) / 0.13);
        while (app.demoStepIdx <= targetIdx && app.demoStepIdx < 10) {
            app.physics.triggerKeyDown(demoKeys[app.demoStepIdx], app.settings);
            app.demoStepIdx++;
        }
        if (app.demoStepIdx >= 10) app.demoActive = false;
    }

    RECT wr = {};
    GetWindowRect(app.hwnd, &wr);
    int w = std::max(1, (int)(wr.right - wr.left));
    int h = std::max(1, (int)(wr.bottom - wr.top));

    app.claude.step(now);
    {
        POINT cursor = {};
        GetCursorPos(&cursor);
        double t = breaks::clock();
        app.breaks.step(cursor, t);
        app.physics.wantsAttention = app.claude.needsAttention() || app.breaks.isWaving(t);
    }
    app.physics.step(dt, wr, app.input, app.settings);
    if (!physicsLooksSane(app.physics)) {
        // A single non-finite value would otherwise make every later frame draw nothing (the cat
        // "vanishes" until restart). Rebuild the pose but keep the session statistics.
        int pets = app.physics.totalPetSessions;
        app.physics = CatPhysics();
        app.physics.totalPetSessions = pets;
    }

    // Smart Click-Through + Petting Detection
    if (!app.isDragging) {
        POINT cur = {};
        GetCursorPos(&cur);
        if (PtInRect(&wr, cur)) {
            Pt cp = {
                (float)(cur.x - wr.left) / (float)w * CatPhysics::CanvasWidth,
                (float)(cur.y - wr.top)  / (float)h * CatPhysics::CanvasHeight
            };
            CatHitZone zone = CatRenderer::hitTest(cp, app.physics);
            if (zone == CatHitZone::Head || zone == CatHitZone::Body) {
                if (app.hasLastHover) {
                    float d = std::hypot(cp.x - app.lastHoverCanvas.x, cp.y - app.lastHoverCanvas.y);
                    if (d > 0.5f && d < 80.0f) {
                        app.physics.registerPetStroke(cp, d, zone == CatHitZone::Head, app.settings);
                    }
                }
                app.lastHoverCanvas = cp;
                app.hasLastHover = true;
            } else {
                app.hasLastHover = false;
            }
            // Holding Alt temporarily unlocks click-through so user can ALWAYS right-click or drag the cat!
            bool altHeld = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
            bool onBreakBubble = false;
            app.breaks.hit(cp, &onBreakBubble);
            bool overAlert = onBreakBubble || (!app.breaks.isShowingBubble() && app.claude.hit(cp, now) != 0);
            bool clickThrough = ((app.settings.fullClickThrough && !altHeld) || (zone == CatHitZone::None)) && !overAlert;
            setClickThrough(app.hwnd, clickThrough);
        } else {
            app.hasLastHover = false;
            setClickThrough(app.hwnd, true);
        }
    }

    renderFriendCat(app, dt);

    // Render 32-bit Premultiplied ARGB frame directly into DIB and present via UpdateLayeredWindow
    app.ensureBuffer(w, h);
    if (!app.memDC || !app.dibBits) return;

    Gdiplus::Bitmap bmp(w, h, w * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(app.dibBits));
    Gdiplus::Graphics g(&bmp);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    g.ScaleTransform((float)w / CatPhysics::CanvasWidth, (float)h / CatPhysics::CanvasHeight);
    CatRenderer::draw(g, app.physics, app.settings);
    // While the break reminder is up, it takes the Claude bubbles' place above the cat.
    if (app.breaks.isShowingBubble()) app.breaks.draw(g, breaks::clock());
    else app.claude.draw(g, now);

    POINT ptDst = { wr.left, wr.top };
    SIZE szWnd = { w, h };
    POINT ptSrc = { 0, 0 };
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    HDC screenDC = GetDC(nullptr);
    BOOL presented = UpdateLayeredWindow(app.hwnd, screenDC, &ptDst, &szWnd, app.memDC, &ptSrc, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screenDC);
    if (presented) {
        app.ulwFailures = 0;
    } else if (++app.ulwFailures > 30) {
        // Surface lost (typically after Win+L / display reconnect): rebuild the DIB next frame.
        app.ulwFailures = 0;
        app.destroyBuffer();
    }
}

/// Renders at most once per FRAME_INTERVAL; called from both the main loop and WM_TIMER (the latter
/// keeps the cat alive while a menu / message box runs its own modal loop).
void renderIfDue(AppState& app) {
    double now = nowSeconds();
    if (now < app.nextFrameTime) return;
    app.nextFrameTime = (now - app.nextFrameTime < FRAME_INTERVAL) ? app.nextFrameTime + FRAME_INTERVAL
                                                                    : now + FRAME_INTERVAL;
    tickAndRender(app);
}

/// Wheel scrolling (mouse wheels and touchpads) anywhere on screen -> the paw on the mini mouse.
HHOOK g_mouseHook = nullptr;
LRESULT CALLBACK mouseHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && wParam == WM_MOUSEWHEEL && g_app) {
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        float delta = (float)(short)HIWORD(info->mouseData) / 12.0f;   // one notch (120) ~ 10 points, like macOS
        g_app->input.lastMouseMoveTime = nowSeconds();
        g_app->physics.triggerScroll(delta, g_app->settings);
        g_app->link.noteScroll(delta);
        g_app->breaks.noteInput(breaks::clock());
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

/// The friend's cat window: drag it around (its own saved position) and right-click for the menu.
LRESULT CALLBACK friendWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppState* app = g_app;
    if (!app) return DefWindowProcW(hwnd, msg, wParam, lParam);
    auto& f = app->friendCat;
    switch (msg) {
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
            break;
        case WM_LBUTTONDOWN: {
            bool altHeld = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
            if (!app->settings.fullClickThrough || altHeld) {
                f.dragging = true;
                GetCursorPos(&f.dragStartCursor);
                RECT wr = {};
                GetWindowRect(hwnd, &wr);
                f.dragStartWinOrigin = { wr.left, wr.top };
                SetCapture(hwnd);
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if (f.dragging) {
                POINT cur = {};
                GetCursorPos(&cur);
                SetWindowPos(hwnd, nullptr, f.dragStartWinOrigin.x + (cur.x - f.dragStartCursor.x),
                             f.dragStartWinOrigin.y + (cur.y - f.dragStartCursor.y), 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (f.dragging) {
                f.dragging = false;
                ReleaseCapture();
                RECT wr = {};
                GetWindowRect(hwnd, &wr);
                regWrite(L"FriendHasPos", nullptr, 1);
                regWrite(L"FriendPosX", nullptr, (DWORD)wr.left);
                regWrite(L"FriendPosY", nullptr, (DWORD)wr.top);
            }
            return 0;
        case WM_RBUTTONUP: {
            POINT pt = {};
            GetCursorPos(&pt);
            showContextMenu(*app, pt);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppState* app = g_app;
    switch (msg) {
        case WM_INPUT:
            if (app) app->input.handleRawInput(lParam);
            return 0;

        case WM_TIMER:
            if (wParam == TIMER_RENDER && app) renderIfDue(*app);
            return 0;

        case WM_WTSSESSION_CHANGE:
            if (app && hwnd == app->hwnd) {
                switch (wParam) {
                    case WTS_SESSION_LOCK:
                    case WTS_CONSOLE_DISCONNECT:
                    case WTS_REMOTE_DISCONNECT:
                        // Win+L: the key-ups for Win and L go to the secure desktop, never to us.
                        app->input.resetAllState();
                        app->breaks.logic.screenLocked(breaks::clock());
                        break;
                    case WTS_SESSION_UNLOCK:
                    case WTS_SESSION_LOGON:
                    case WTS_CONSOLE_CONNECT:
                    case WTS_REMOTE_CONNECT:
                        app->breaks.logic.screenUnlocked(breaks::clock());
                        recoverAfterSystemEvent(*app);
                        break;
                    default:
                        break;
                }
            }
            return 0;

        case WM_POWERBROADCAST:
            if (app && hwnd == app->hwnd && wParam == PBT_APMSUSPEND) {
                app->breaks.logic.willSleep(breaks::clock());
            }
            if (app && hwnd == app->hwnd &&
                (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)) {
                app->breaks.logic.didWake(breaks::clock());
                recoverAfterSystemEvent(*app);
            }
            return TRUE;

        case WM_DISPLAYCHANGE:
            // Fullscreen games often switch resolution; the cat could end up off-screen or with a
            // stale surface. Re-place it (without touching the saved position) and rebuild the buffer.
            if (app && hwnd == app->hwnd) recoverAfterSystemEvent(*app);
            return 0;

        case WM_REQUEST_QUIT:
            if (app) quitApp(*app);
            return 0;

        case WM_COPYDATA: {   // from `Whisku.exe --claude-hook` (a Claude Code hook)
            const auto* cds = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
            if (!app || !cds || cds->dwData != claude::COPYDATA_ID || !cds->lpData || cds->cbData < sizeof(wchar_t) || cds->cbData > 8192) return FALSE;
            std::wstring payload((const wchar_t*)cds->lpData, cds->cbData / sizeof(wchar_t));
            while (!payload.empty() && payload.back() == L'\0') payload.pop_back();
            onClaudeAlert(*app, app->claude.receivePayload(payload, nowSeconds()));
            return TRUE;
        }

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;

        case WM_LBUTTONDOWN: {
            if (app && hwnd == app->hwnd) {
                // A Claude Code bubble: close it and bring back the session's window.
                RECT wr = {};
                GetWindowRect(hwnd, &wr);
                POINT cur = {};
                GetCursorPos(&cur);
                float w = (float)std::max(1L, wr.right - wr.left), h = (float)std::max(1L, wr.bottom - wr.top);
                Pt cp = { (cur.x - wr.left) / w * CatPhysics::CanvasWidth, (cur.y - wr.top) / h * CatPhysics::CanvasHeight };
                bool onBreakBubble = false;
                breaks::Button button = app->breaks.hit(cp, &onBreakBubble);
                if (onBreakBubble) {
                    app->breaks.tap(button, breaks::clock());
                    return 0;
                }
                if (int id = app->breaks.isShowingBubble() ? 0 : app->claude.hit(cp, nowSeconds())) {
                    app->claude.activate(id);
                    return 0;
                }
            }
            bool altHeld = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
            if (app && (!app->settings.fullClickThrough || altHeld)) {
                app->isDragging = true;
                GetCursorPos(&app->dragStartCursor);
                RECT wr = {};
                GetWindowRect(hwnd, &wr);
                app->dragStartWinOrigin = { wr.left, wr.top };
                SetCapture(hwnd);
            }
            return 0;
        }

        case WM_MOUSEMOVE:
            if (app && app->isDragging) {
                POINT cur = {};
                GetCursorPos(&cur);
                int nx = app->dragStartWinOrigin.x + (cur.x - app->dragStartCursor.x);
                int ny = app->dragStartWinOrigin.y + (cur.y - app->dragStartCursor.y);
                SetWindowPos(hwnd, nullptr, nx, ny, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;

        case WM_LBUTTONUP:
            if (app && app->isDragging) {
                app->isDragging = false;
                ReleaseCapture();
                RECT wr = {};
                GetWindowRect(hwnd, &wr);
                app->settings.posX = wr.left;
                app->settings.posY = wr.top;
                app->settings.saveToRegistry();
            }
            return 0;

        case WM_RBUTTONUP:
            if (app) {
                POINT pt = {};
                GetCursorPos(&pt);
                showContextMenu(*app, pt);
            }
            return 0;

        case WM_TRAYICON:
            if (app && (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP || lParam == WM_CONTEXTMENU)) {
                POINT pt = {};
                GetCursorPos(&pt);
                showContextMenu(*app, pt);
            }
            return 0;

        case WM_HOTKEY:
            if (app && wParam == HOTKEY_CLICKTHROUGH_ID) {
                app->settings.fullClickThrough = !app->settings.fullClickThrough;
                app->settings.saveToRegistry();
                if (app->settings.fullClickThrough) {
                    showTrayBalloon(*app,
                        tr(L"Whisku \u5DF2\u5F00\u542F\u5B8C\u5168\u9F20\u6807\u7A7F\u900F", L"Whisku full click-through is on"),
                        tr(L"\u518D\u6B21\u6309 Ctrl+Alt+Shift+P \u6216\u6309\u4F4F Alt \u53F3\u952E\u5C0F\u732B\u5373\u53EF\u53D6\u6D88", L"Press Ctrl+Alt+Shift+P again, or hold Alt and right-click the cat, to turn it off"));
                } else {
                    showTrayBalloon(*app,
                        tr(L"Whisku \u5DF2\u53D6\u6D88\u5B8C\u5168\u9F20\u6807\u7A7F\u900F", L"Whisku full click-through is off"),
                        tr(L"\u73B0\u5728\u53EF\u4EE5\u6B63\u5E38\u62D6\u52A8\u3001\u6478\u5934\u548C\u53F3\u952E\u6253\u5F00\u83DC\u5355\u4E86", L"You can drag, pet and right-click the cat again"));
                }
            }
            return 0;

        case WM_WAKE_EXISTING:
            if (app) {
                // Re-launching Whisku.exe unlocks click-through, restores visibility & opens settings menu!
                if (app->settings.fullClickThrough) {
                    app->settings.fullClickThrough = false;
                    app->settings.saveToRegistry();
                }
                if (!app->isVisible) {
                    app->isVisible = true;
                    ShowWindow(app->hwnd, SW_SHOWNOACTIVATE);
                }
                POINT pt = {};
                GetCursorPos(&pt);
                showContextMenu(*app, pt);
            }
            return 0;

        case WM_UPDATE_CHECK_DONE: {
            auto* res = reinterpret_cast<UpdateCheckResult*>(lParam);
            if (res) {
                bool askNow = false;
                if (app && res->ok && res->hasUpdate) {
                    app->availableUpdateVersion = res->latestTag;
                    app->availableUpdateUrl = res->releaseUrl;
                    app->availableAssetUrl = res->assetUrl;
                    app->availableAssetSize = res->assetSize;
                    app->availableAssetName = res->assetName;
                    // Ask once per new version; after "no" the menu keeps offering it.
                    askNow = res->manual || regReadString(L"UpdatePromptedTag") != res->latestTag;
                    if (askNow) regWrite(L"UpdatePromptedTag", &res->latestTag, 0);
                }
                if (app && res->ok && res->hasUpdate && askNow) {
                    // Show WHICH exe is running: the usual cause of "I already updated but it still
                    // says there's a new version" is an old copy (Desktop / autostart) still running.
                    std::wstring msg = tr(L"\u53D1\u73B0 Whisku \u65B0\u7248\u672C ", L"A new Whisku version is available: ") + res->latestTag
                        + tr(L"\n\u5F53\u524D\u7248\u672C\uFF1Av", L"\nCurrent version: v") + APP_VERSION
                        + tr(L"\n\u5F53\u524D\u8FD0\u884C\u7684\u7A0B\u5E8F\uFF1A", L"\nRunning from: ") + currentExePath()
                        + tr(L"\n\n\u5982\u679C\u4F60\u5DF2\u7ECF\u4E0B\u8F7D\u8FC7\u65B0\u7248\uFF0C\u76F4\u63A5\u53CC\u51FB\u65B0\u7248 exe \u5373\u53EF\u81EA\u52A8\u66FF\u6362\u6389\u8FD9\u4E2A\u65E7\u7248\u3002", L"\n\nIf you've already downloaded the new version, just open the new exe and it replaces this old one.")
                        + tr(L"\n\n\u662F\u5426\u7ACB\u5373\u5728\u8F6F\u4EF6\u5185\u81EA\u52A8\u4E0B\u8F7D\u5E76\u66F4\u65B0\u91CD\u542F\uFF1F", L"\n\nDownload and install it now, then restart?");
                    if (MessageBoxW(nullptr, msg.c_str(), tr(L"Whisku \u66F4\u65B0\u63D0\u9192", L"Whisku update"),
                                    MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST) == IDYES) {
                        startInAppUpdate(*app);
                    }
                } else if (app && res->ok && res->hasUpdate) {
                    // Already asked about this version: the menu shows it.
                } else if (res->manual) {
                    if (res->noRelease) {
                        std::wstring msg = std::wstring(tr(L"当前版本 v", L"Version v")) + APP_VERSION + tr(L"，还没有发布更新的版本。", L". No newer version has been published yet.");
                        MessageBoxW(nullptr, msg.c_str(), tr(L"Whisku 检查更新", L"Whisku updates"), MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
                    } else if (res->ok) {
                        std::wstring msg = std::wstring(tr(L"\u5F53\u524D\u7248\u672C v", L"Version v")) + APP_VERSION + tr(L" \u5DF2\u662F\u6700\u65B0\u7248\u672C\u3002", L" is the latest version.");
                        MessageBoxW(nullptr, msg.c_str(), tr(L"Whisku \u68C0\u67E5\u66F4\u65B0", L"Whisku updates"), MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
                    } else {
                        MessageBoxW(nullptr, tr(L"无法连接到 GitHub，请稍后重试或检查网络连接。", L"Couldn't reach GitHub. Please try again later or check your network connection."),
                                    tr(L"Whisku \u68C0\u67E5\u66F4\u65B0", L"Whisku updates"), MB_OK | MB_ICONWARNING | MB_TOPMOST);
                    }
                }
                delete res;
            }
            return 0;
        }

        case WM_UPDATE_DOWNLOAD_DONE: {
            auto* res = reinterpret_cast<UpdateDownloadResult*>(lParam);
            if (res) {
                if (app) app->isUpdating = false;
                if (res->ok && !res->batPath.empty()) {
                    ShellExecuteW(nullptr, L"open", res->batPath.c_str(), nullptr, nullptr, SW_HIDE);
                    PostQuitMessage(0);
                } else {
                    if (MessageBoxW(nullptr,
                                    tr(L"\u81EA\u52A8\u4E0B\u8F7D\u6216\u66FF\u6362\u5B89\u88C5\u5305\u5931\u8D25\uFF0C\u662F\u5426\u6253\u5F00\u4E0B\u8F7D\u9875\u624B\u52A8\u4E0B\u8F7D\uFF1F", L"Downloading or installing the update failed. Open the download page to download it manually?"),
                                    tr(L"Whisku \u81EA\u52A8\u66F4\u65B0", L"Whisku update"), MB_YESNO | MB_ICONWARNING | MB_TOPMOST) == IDYES) {
                        const wchar_t* url = (app && !app->availableUpdateUrl.empty())
                            ? app->availableUpdateUrl.c_str()
                            : UPDATE_PAGE_URL;
                        ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
                    }
                }
                delete res;
            }
            return 0;
        }

        case WM_DESTROY:
            if (hwnd == (app ? app->hwnd : nullptr)) {
                KillTimer(hwnd, TIMER_RENDER);
                if (app) {
                    if (app->sessionNotifyRegistered) {
                        WTSUnRegisterSessionNotification(hwnd);
                        app->sessionNotifyRegistered = false;
                    }
                    if (app->foregroundHook) {
                        UnhookWinEvent(app->foregroundHook);
                        app->foregroundHook = nullptr;
                    }
                    if (app->menuOwnerHwnd) {
                        UnregisterHotKey(app->menuOwnerHwnd, HOTKEY_CLICKTHROUGH_ID);
                        RemovePropW(app->menuOwnerHwnd, VERSION_PROP);
                        DestroyWindow(app->menuOwnerHwnd);
                        app->menuOwnerHwnd = nullptr;
                    }
                    Shell_NotifyIconW(NIM_DELETE, &app->nid);
                    if (app->trayIcon) DestroyIcon(app->trayIcon);
                    app->destroyBuffer();
                    app->link.stop();
                    g_passwordWatcher.stop();
                    if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = nullptr; }
                    if (app->friendCat.hwnd) { DestroyWindow(app->friendCat.hwnd); app->friendCat.hwnd = nullptr; }
                    app->friendCat.destroyBuffer();
                }
                PostQuitMessage(0);
            }
            return 0;
    }
    if (g_msgTaskbarCreated != 0 && msg == g_msgTaskbarCreated && app && hwnd == app->menuOwnerHwnd) {
        registerTrayIcon(*app);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void CALLBACK onForegroundChanged(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    // A game just came to the front: many make themselves topmost right after showing, so
    // re-assert our own topmost state shortly afterwards instead of staying buried ("vanished").
    if (g_app) g_app->pendingTopmostTime = nowSeconds() + 0.4;
}

/// Asks the already-running Whisku to exit and waits for it. Builds <= 1.3.2 do not know
/// WM_REQUEST_QUIT, but they shut down cleanly when their overlay window receives WM_CLOSE.
bool shutDownRunningInstance(HWND owner, DWORD timeoutMs) {
    DWORD pid = 0;
    if (owner) GetWindowThreadProcessId(owner, &pid);
    HANDLE hProc = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr;
    if (owner) PostMessageW(owner, WM_REQUEST_QUIT, 0, 0);
    if (HWND overlay = FindWindowW(OVERLAY_CLASS, nullptr)) PostMessageW(overlay, WM_CLOSE, 0, 0);
    bool gone = false;
    if (hProc) {
        gone = (WaitForSingleObject(hProc, timeoutMs) == WAIT_OBJECT_0);
        CloseHandle(hProc);
    } else {
        for (DWORD waited = 0; waited <= timeoutMs; waited += 100) {
            if (!FindWindowW(TRAY_OWNER_CLASS, nullptr) && !FindWindowW(OVERLAY_CLASS, nullptr)) { gone = true; break; }
            Sleep(100);
        }
    }
    return gone;
}

// MARK: - Preview sheet (CI renders it so the drawing can be checked without a Windows screen)

bool pngEncoder(CLSID& clsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (!size) return false;
    std::vector<BYTE> buf(size);
    auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    Gdiplus::GetImageEncoders(num, size, info);
    for (UINT i = 0; i < num; ++i) if (wcscmp(info[i].MimeType, L"image/png") == 0) { clsid = info[i].Clsid; return true; }
    return false;
}

/// `Whiskerkey.exe --render-preview out.png`: drives the real physics + renderer through the fork's
/// features (resting paws, scroll paw, AirPods on/off, petting with AirPods, a friend's cat label).
int renderPreviewSheet(const std::wstring& outPath) {
    Gdiplus::GdiplusStartupInput in;
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartup(&token, &in, nullptr);
    struct Frame { std::wstring label; std::unique_ptr<Gdiplus::Bitmap> bmp; };
    std::vector<Frame> frames;
    const float zoom = 1.5f;
    const int fw = (int)(CatPhysics::CanvasWidth * zoom), fh = (int)(CatPhysics::CanvasHeight * zoom);
    RECT rect = { 600, 300, 600 + fw, 300 + fh };

    auto snap = [&](const CatPhysics& p, const CatSettings& st, const std::wstring& label, const std::wstring& tag = L"",
                    const claude::Center* bubbles = nullptr, const breaks::Center* reminder = nullptr) {
        auto bmp = std::make_unique<Gdiplus::Bitmap>(fw, fh, PixelFormat32bppARGB);
        Gdiplus::Graphics g(bmp.get());
        g.Clear(Gdiplus::Color(255, 219, 222, 217));
        g.ScaleTransform(zoom, zoom);
        CatRenderer::draw(g, p, st);
        if (!tag.empty()) drawFriendLabel(g, tag, false);
        if (bubbles) bubbles->draw(g, nowSeconds());
        if (reminder) reminder->draw(g, breaks::clock());
        frames.push_back({ label, std::move(bmp) });
    };
    auto run = [&](CatPhysics& p, InputEngine& eng, const CatSettings& st, double seconds, std::function<void()> each) {
        double end = nowSeconds() + seconds;
        while (nowSeconds() < end) {
            if (each) each();
            p.step(1.0f / 60.0f, rect, eng, st);
            Sleep(16);
        }
    };
    auto feed = [] { InputEngine e; e.externalFeed = true; e.lastCursorPos = { 600 + 276, 300 + 190 }; return e; };

    CatSettings st;
    st.showKeyBadges = st.showMouseBadges = true;
    {   // resting paws on every coat
        for (CatCoatTheme coat : { CatCoatTheme::ClassicBlack, CatCoatTheme::Siamese, CatCoatTheme::Shorthair, CatCoatTheme::OrangeWhite }) {
            CatSettings cs = st; cs.coatTheme = coat;
            cs.eyeTheme = coat == CatCoatTheme::Siamese ? EyeColorTheme::SiameseBlue : coat == CatCoatTheme::ClassicBlack ? EyeColorTheme::NeonChartreuse : EyeColorTheme::SoftAmber;
            CatPhysics p; InputEngine eng = feed();
            eng.lastMouseMoveTime = nowSeconds() - 10;
            run(p, eng, cs, 0.6, nullptr);
            snap(p, cs, L"resting paws");
        }
    }
    {   // scroll paw
        CatPhysics p; InputEngine eng = feed();
        run(p, eng, st, 0.8, [&] { eng.lastMouseMoveTime = nowSeconds(); p.triggerScroll(4.0f, st); });
        snap(p, st, L"scrolling");
    }
    {   // AirPods on, petting with AirPods, off
        CatPhysics p; InputEngine eng = feed();
        eng.lastMouseMoveTime = nowSeconds() - 10;
        run(p, eng, st, 0.5, nullptr);
        p.setHeadphonesConnected(true, true, st);
        double start = nowSeconds();
        for (double u : { 0.2, 0.45, 0.7 }) {
            run(p, eng, st, start + u * CatPhysics::EarbudPutOnDuration - nowSeconds(), nullptr);
            snap(p, st, L"AirPods on " + std::to_wstring((int)(u * 100)) + L"%");
        }
        run(p, eng, st, 1.0, nullptr);
        snap(p, st, L"wearing");
        int i = 0;
        run(p, eng, st, 1.8, [&] { ++i; p.registerPetStroke({ 172.0f + (i % 4) * 5.0f, 100.0f }, 6.0f, true, st); });
        p.spawnFloatingNote(L"\u2665");
        run(p, eng, st, 0.3, nullptr);
        snap(p, st, L"petted (ears fold, \u2665)");
        p.setHeadphonesConnected(false, true, st);
        start = nowSeconds();
        run(p, eng, st, start + 0.42 * CatPhysics::EarbudTakeOffDuration - nowSeconds(), nullptr);
        snap(p, st, L"AirPods off 42%");
    }
    {   // a friend's cat with a name tag, wearing AirPods, music note
        CatSettings fs = st; fs.coatTheme = CatCoatTheme::OrangeWhite; fs.eyeTheme = EyeColorTheme::SoftAmber; fs.showKeyBadges = false;
        CatPhysics p; InputEngine eng = feed();
        eng.lastMouseMoveTime = nowSeconds() - 10;
        p.setHeadphonesConnected(true, false, fs);
        p.headphonesPlaying = true;
        run(p, eng, fs, 3.0, nullptr);
        snap(p, fs, L"friend's cat", L"小明 的猫 · 在线");
    }

    {   // Claude Code: a "done" cheer, then waving while a session waits for an approval
        claude::Center c;
        CatPhysics p; InputEngine eng = feed();
        eng.lastMouseMoveTime = nowSeconds() - 10;
        auto sync = [&] { p.wantsAttention = c.needsAttention(); c.step(nowSeconds()); };
        run(p, eng, st, 0.5, sync);
        c.receive(claude::Kind::Done, L"x_project", L"a", nullptr, nowSeconds());
        p.cheer();
        run(p, eng, st, 0.1, sync);
        snap(p, st, L"Claude done +0.1s", L"", &c);
        run(p, eng, st, 0.4, sync);
        snap(p, st, L"Claude done +0.5s", L"", &c);
        c.receive(claude::Kind::NeedsYou, L"Whiskerkey-vibecoding-long-name", L"b", nullptr, nowSeconds());
        run(p, eng, st, 0.5, sync);
        snap(p, st, L"Claude needs you +0.5s", L"", &c);
        run(p, eng, st, 0.35, sync);
        snap(p, st, L"Claude needs you +0.85s", L"", &c);
    }

    {   // English UI: Claude Code bubbles and the scroll badge (not saved: only this process switches)
        l10n::cachedChinese() = 0;
        claude::Center c;
        CatPhysics p; InputEngine eng = feed();
        auto sync = [&] { c.step(nowSeconds()); };
        run(p, eng, st, 0.3, sync);
        c.receive(claude::Kind::Done, L"x_project", L"a", nullptr, nowSeconds());
        run(p, eng, st, 0.4, sync);
        c.receive(claude::Kind::NeedsYou, L"Whiskerkey-vibecoding", L"b", nullptr, nowSeconds());
        p.triggerScroll(4.0f, st);
        run(p, eng, st, 0.5, [&] { sync(); eng.lastMouseMoveTime = nowSeconds(); });
        snap(p, st, L"English: bubbles + Wheel badge", L"", &c);
        l10n::cachedChinese() = -1;
    }
    {   // Break reminder: the first one (OK / Later) waving, a later one with "I've rested" in English,
        // and "Enjoy your break" after OK
        breaks::Center c;
        double t0 = breaks::clock();
        c.begin(60, t0);
        c.logic = breaks::Logic(3600.0, t0 - 3601);   // worked an hour...
        c.logic.lastInput = t0;                       // ...without a break
        c.logic.tick(t0);
        CatPhysics p; InputEngine eng = feed();
        auto sync = [&] { p.wantsAttention = c.isWaving(breaks::clock()); };
        l10n::cachedChinese() = 0;
        run(p, eng, st, 0.75, sync);
        snap(p, st, L"English: first break reminder", L"", nullptr, &c);
        breaks::Center v;   // the preview: a later reminder, then "Enjoy your break" after OK
        v.begin(60, breaks::clock());
        CatPhysics q; InputEngine eng2 = feed();
        auto sync2 = [&] { q.wantsAttention = v.isWaving(breaks::clock()); };
        l10n::cachedChinese() = 1;
        v.showPreview(breaks::clock());
        run(q, eng2, st, 0.5, sync2);
        snap(q, st, L"Chinese: later break reminder", L"", nullptr, &v);
        l10n::cachedChinese() = 0;
        v.tap(breaks::Button::Ok, breaks::clock());
        run(q, eng2, st, 0.5, sync2);
        snap(q, st, L"English: after OK", L"", nullptr, &v);
        l10n::cachedChinese() = -1;
    }
    {   // Mosquito: sleeping cat, mosquito circles, two swats, it leaves, the cat dozes off again.
        // The mosquito counts its own steps, so wait on its age (a slow CI machine falls behind the clock).
        CatPhysics p; InputEngine eng = feed();   // the demo's nap keeps the cat asleep throughout
        auto runUntil = [&](std::function<bool()> done, double maxSeconds) {
            double end = nowSeconds() + maxSeconds;
            while (!done() && nowSeconds() < end) { p.step(1.0f / 60.0f, rect, eng, st); Sleep(16); }
        };
        p.summonMosquito();
        for (float age : { 2.0f, 2.95f, 3.95f }) {
            runUntil([&] { return p.mosquito.active && p.mosquito.age >= age; }, 30.0);
            snap(p, st, L"mosquito age " + std::to_wstring(age).substr(0, 4));
        }
        runUntil([&] { return !p.mosquito.active && p.mosquitoDisturb < 0.05f && p.sleepiness > 0.9f; }, 30.0);
        snap(p, st, L"mosquito gone, asleep again");
    }

    const int cols = 4, labelH = 28;
    const int rows = (int)(frames.size() + cols - 1) / cols;
    Gdiplus::Bitmap sheet(cols * fw, rows * (fh + labelH), PixelFormat32bppARGB);
    Gdiplus::Graphics g(&sheet);
    g.Clear(Gdiplus::Color(255, 255, 255, 255));
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font font(&ff, 16.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush black(Gdiplus::Color(255, 0, 0, 0));
    for (size_t i = 0; i < frames.size(); ++i) {
        int x = (int)(i % cols) * fw, y = (int)(i / cols) * (fh + labelH);
        g.DrawString(frames[i].label.c_str(), -1, &font, Gdiplus::PointF((float)x + 6, (float)y + 4), &black);
        g.DrawImage(frames[i].bmp.get(), x, y + labelH, fw, fh);
    }
    CLSID png;
    bool ok = pngEncoder(png) && sheet.Save(outPath.c_str(), &png, nullptr) == Gdiplus::Ok;
    frames.clear();
    Gdiplus::GdiplusShutdown(token);
    return ok ? 0 : 1;
}

// MARK: - Cat Link self-test (run by .github/workflows/catlink-selftest.yml on a real Windows machine)

catlink::Event testHello(const char* name, const char* coat) {
    catlink::Event e;
    e.k = "hello"; e.n = name; e.coat = coat; e.kb = "classicMilk"; e.eye = "softAmber"; e.hp = false; e.pl = false;
    return e;
}

/// `Whiskerkey.exe --link-selftest [pairing code]`: checks crypto + JSON against macOS test vectors,
/// runs two sessions against the real relay, and (with a code) talks to a macOS peer. Writes
/// whisku-selftest.txt and returns the number of failures.
int runLinkSelfTest(const std::wstring& joinCode) {
    std::string log;
    int failures = 0;
    auto check = [&](bool ok, const std::string& what) {
        log += (ok ? "PASS " : "FAIL ") + what + "\n";
        if (!ok) ++failures;
    };
    auto fromHex = [](const std::string& h) {
        catlink::Bytes b;
        for (size_t i = 0; i + 1 < h.size(); i += 2) b.push_back((uint8_t)strtoul(h.substr(i, 2).c_str(), nullptr, 16));
        return b;
    };
    auto save = [&] {
        FILE* f = nullptr;
        if (fopen_s(&f, "whisku-selftest.txt", "wb") == 0 && f) { fwrite(log.data(), 1, log.size(), f); fclose(f); }
    };

    // 1. macOS test vectors
    const char* macSealed = "000102030405060708090a0b7a53a828b253c9f6a2a2e34df3dbfd4c3b45eac42e182cf3af2a52a88bbeedf341243adf3decd7d50f9378a60dad44af94ac7c178dc520ace4903d6f6c30545caf90d9ba2e4bbfcbacda99c3ea79f80861dbc6ffee6cab88a1de4abf0f91b8b7a2767253eed47919dba93b90bc2c8b999482b7767684296890ba2eea43930dd74aa820f9ae895549b21e10d923a080065ffcc1278517d9c93140726ce9779da3bb4a600232d09403a56d1c1ea3c2778d413bf97e75ea769d642d88d83a108a0ce0d8cae4f23311c7d6c0e06ca3a2afeead6fd173609143274efc0702c8e3e6a20ffd27a63b6cdb3ec5aab003a13919918eeafefa40d225d3c5eb4b7a9818e110f350aecbdc97d1c37311ec55153e26e968943ce62071f4a79dbba4852090e59e2958fd3e1aa3c3e6de0876c3a55fd58606d7fc2b9e2f702e";
    auto code = catlink::Code::parse(L"wk-c-2345-6789-abcd-efgh-jkmn");
    check(code.has_value(), "parse pairing code");
    auto kuroCatCode = catlink::Code::parse(L"KC-C-2345-6789-ABCD-EFGH-JKMN");
    check(kuroCatCode && code && kuroCatCode->secret == code->secret && kuroCatCode->server == code->server, "a KuroCat code (KC-…) is the same code");
    if (code) {
        check(code->display() == L"WK-C-2345-6789-ABCD-EFGH-JKMN", "display format");
        check(code->topic() == "whiskerkey/v1/7ec2a518abf1e18f8212a0099ef14038", "topic matches macOS");
        catlink::Bytes key = code->key();
        check(catlink::toHex(key.data(), key.size()) == "a5de3d642d78922fe3cc9f9c112ebb9082b41a76d67f3c862aa98357d4cfb7c9", "AES key matches macOS");
        auto plain = catlink::Session::openForTest(*code, fromHex(macSealed));
        check(plain.has_value(), "decrypt a macOS message");
        if (plain) {
            log += "  macOS json: " + *plain + "\n";
            auto b = catlink::decodeBatch(*plain);
            check(b && b->f == "aaaa1111" && b->q == 7 && b->e.size() == 4, "decode the macOS batch");
            if (b && b->e.size() == 4) {
                const auto& h = b->e[0]; const auto& kd = b->e[1]; const auto& m = b->e[2]; const auto& pet = b->e[3];
                check(h.k == "hello" && h.n && *h.n == "小猫/Mimi \"q\"" && h.coat == std::string("siamese") &&
                      h.kb == std::string("retroCream") && h.eye == std::string("siameseBlue") && h.hp == true && h.pl == false,
                      "hello fields (Chinese text, escaped / and quotes)");
                check(kd.k == "kd" && kd.id == 3 && kd.s == std::string("l") && kd.o == 12, "key-down fields");
                check(m.k == "m" && m.x == 0.625 && m.y == 0.25 && m.vx == -1.5 && m.vy && std::fabs(*m.vy - 0.001) < 1e-9 && m.o == 40, "mouse fields");
                check(pet.k == "pet" && pet.x == 172.5 && pet.y == 98.0 && pet.d == 7.0 && pet.h == true && pet.o == 90, "pet fields");
            }
        }
        // 2. A Windows-made message for macOS to check (fixed nonce 00..0b)
        catlink::Batch wb;
        wb.f = "bbbb2222"; wb.q = 9;
        wb.e.push_back(testHello("Windows 小猫", "orangeWhite"));
        catlink::Event kd; kd.k = "kd"; kd.o = 15; kd.id = 5; kd.s = "r"; wb.e.push_back(kd);
        catlink::Event m; m.k = "m"; m.o = 50; m.x = 0.1; m.y = 0.9; m.vx = 0.25; m.vy = -0.5; wb.e.push_back(m);
        catlink::Event c; c.k = "c"; c.o = 70; c.b = 1; c.d = 1.0; wb.e.push_back(c);
        std::string json = catlink::encodeBatch(wb);
        auto round = catlink::decodeBatch(json);
        check(round && round->e.size() == 4 && round->e[0].n == std::string("Windows 小猫") && round->e[2].vy == -0.5, "JSON round trip");
        uint8_t nonce[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
        log += "WINDOWS_JSON=" + json + "\n";
        log += "WINDOWS_SEALED=" + catlink::Session::sealForTest(*code, json, nonce) + "\n";
    }

    // 3. Two sessions over the real relay (broker-cn.emqx.io)
    {
        catlink::Code live = catlink::Code::generate('C');
        catlink::Session a, b, c;
        a.profile = [] { return testHello("A", "siamese"); };
        b.profile = [] { return testHello("B", "orangeWhite"); };
        InputEngine idle;
        idle.externalFeed = true;
        std::map<std::string, int> gotB, gotA;
        std::string helloOnB;
        auto pump = [&](double seconds, std::function<bool()> until) {
            double end = nowSeconds() + seconds;
            while (nowSeconds() < end) {
                double now = nowSeconds();
                a.update(now, idle);
                b.update(now, idle);
                c.update(now, idle);
                for (auto& d : b.takeDue(now)) { gotB[d.e.k]++; if (d.e.k == "hello") helloOnB = d.e.n.value_or("") + "/" + d.e.coat.value_or(""); }
                for (auto& d : a.takeDue(now)) gotA[d.e.k]++;
                if (until && until()) return;
                Sleep(15);
            }
        };
        double t0 = nowSeconds();
        a.start(live);
        b.start(live);
        pump(25, [&] { return a.status() == catlink::Status::Linked && b.status() == catlink::Status::Linked; });
        char buf[64];
        snprintf(buf, sizeof buf, " (%.1f s)", nowSeconds() - t0);
        check(a.status() == catlink::Status::Linked && b.status() == catlink::Status::Linked, std::string("two sessions linked over broker-cn.emqx.io") + buf);
        if (a.status() != catlink::Status::Linked) log += "  last error: " + catlink::narrow(a.lastError()) + "\n";
        const PhysicalKey keys[] = { PhysicalKey::H, PhysicalKey::E, PhysicalKey::L, PhysicalKey::L, PhysicalKey::O };
        for (PhysicalKey k : keys) {
            a.noteKeyDown(k, prefersViewerLeftPaw(k), false); pump(0.04, nullptr);
            a.noteKeyUp(k); pump(0.05, nullptr);
        }
        a.noteKeyDown(PhysicalKey::X, false, true);   // password field focused: must not be sent
        a.noteKeyUp(PhysicalKey::X);
        a.noteClick(0, true); pump(0.08, nullptr); a.noteClick(0, false);
        for (int i = 0; i < 12; ++i) { b.notePet({172.0f + (i % 4) * 5.0f, 100.0f}, 6.0f, true); pump(0.05, nullptr); }
        pump(2.5, nullptr);
        check(gotB["kd"] == 5 && gotB["ku"] == 5, "5 key presses arrive (password-field press not sent): kd=" + std::to_string(gotB["kd"]) + " ku=" + std::to_string(gotB["ku"]));
        check(gotB["c"] == 2, "click down + up arrive");
        check(gotA["pet"] == 12, "12 petting strokes arrive the other way: " + std::to_string(gotA["pet"]));
        check(helloOnB == "A/siamese", "friend's hello arrives: " + helloOnB);

        // Pausing: only a "hide" heartbeat goes out, so the friend hides our cat; we still see theirs.
        auto kinds = [](const std::map<std::string, int>& got) {
            std::string out;
            for (auto& kv : got) out += " " + kv.first + "=" + std::to_string(kv.second);
            return out.empty() ? std::string(" none") : out;
        };
        a.setPaused(true);
        pump(5, [&] { return b.status() == catlink::Status::FriendPaused; });
        check(b.status() == catlink::Status::FriendPaused, "pausing: the friend sees 'paused'");
        check(a.status() == catlink::Status::Linked, "pausing: we stay linked");
        gotA.clear(); gotB.clear();
        for (PhysicalKey k : keys) {
            a.noteKeyDown(k, prefersViewerLeftPaw(k), false); pump(0.04, nullptr);
            a.noteKeyUp(k); pump(0.05, nullptr);
        }
        a.noteClick(0, true); a.noteClick(0, false); a.noteScroll(4.0f);
        a.notePet({172.0f, 100.0f}, 6.0f, true);
        a.profileChanged();
        for (int i = 0; i < 4; ++i) { b.notePet({172.0f, 100.0f}, 6.0f, true); pump(0.05, nullptr); }
        pump(2.5, nullptr);
        check(gotB.empty(), "pausing: none of our activity arrives:" + kinds(gotB));
        check(gotA["pet"] == 4, "pausing: the friend's petting still reaches us:" + kinds(gotA));
        pump(28, nullptr);
        check(b.status() == catlink::Status::FriendPaused, "pausing: after 28 s still 'paused', not 'offline' (heartbeat)");
        check(gotB.empty(), "pausing: still nothing but the heartbeat:" + kinds(gotB));
        helloOnB.clear();
        a.setPaused(false);
        pump(5, [&] { return b.status() == catlink::Status::Linked && !helloOnB.empty(); });
        check(b.status() == catlink::Status::Linked && helloOnB == "A/siamese", "resuming: our profile arrives and we're linked again");

        a.stop();
        pump(5, [&] { return b.status() == catlink::Status::FriendOffline; });
        check(b.status() == catlink::Status::FriendOffline, "friend leaving is noticed");
        pump(0.5, nullptr);   // let A's last events (still in the 120 ms playback buffer) play out

        // A friend who links while already paused never shows up as a cat.
        c.profile = [] { return testHello("C", "shorthair"); };
        c.setPaused(true);
        gotB.clear();
        c.start(live);
        pump(25, [&] { return b.status() == catlink::Status::FriendPaused; });
        check(b.status() == catlink::Status::FriendPaused && gotB.empty(), "linking while paused: the friend sees 'paused' and no events:" + kinds(gotB));
        c.stop();
        b.stop();
    }

    // 4. Optional: talk to a macOS peer that joined `joinCode`
    if (!joinCode.empty()) {
        auto code2 = catlink::Code::parse(joinCode);
        check(code2.has_value(), "parse the macOS peer's code");
        if (code2) {
            catlink::Session w;
            w.profile = [] { return testHello("Windows CI", "shorthair"); };
            InputEngine idle;
            idle.externalFeed = true;
            std::map<std::string, int> got;
            std::string macHello;
            auto pump = [&](double seconds, std::function<bool()> until) {
                double end = nowSeconds() + seconds;
                while (nowSeconds() < end) {
                    double now = nowSeconds();
                    w.update(now, idle);
                    for (auto& d : w.takeDue(now)) { got[d.e.k]++; if (d.e.k == "hello") macHello = d.e.n.value_or("") + "/" + d.e.coat.value_or(""); }
                    if (until && until()) return;
                    Sleep(15);
                }
            };
            w.start(*code2);
            pump(240, [&] { return !macHello.empty(); });
            check(!macHello.empty(), "macOS peer's hello received: " + macHello);
            for (int i = 0; i < 6; ++i) {
                PhysicalKey k = i % 2 ? PhysicalKey::J : PhysicalKey::F;
                w.noteKeyDown(k, prefersViewerLeftPaw(k), false); pump(0.06, nullptr);
                w.noteKeyUp(k); pump(0.1, nullptr);
            }
            for (int i = 0; i < 40; ++i) { w.notePet({172.0f + (i % 4) * 5.0f, 98.0f}, 7.0f, true); pump(0.05, nullptr); }
            pump(20, [&] { return got["kd"] >= 3; });
            log += "  from macOS:";
            for (auto& kv : got) log += " " + kv.first + "=" + std::to_string(kv.second);
            log += "\n";
            check(got["kd"] >= 3, "macOS key presses received");
            w.stop();
            pump(1, nullptr);
        }
    }

    log += failures ? "RESULT: " + std::to_string(failures) + " failure(s)\n" : "RESULT: all passed\n";
    save();
    return failures;
}

} // namespace

// MARK: - Claude Code hook self-test (CI)

LRESULT CALLBACK claudeTestWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_COPYDATA) {
        const auto* cds = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (cds && cds->dwData == claude::COPYDATA_ID && cds->lpData) {
            auto* out = reinterpret_cast<std::wstring*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (out) out->assign((const wchar_t*)cds->lpData);
        }
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/// Runs `Whiskerkey.exe --claude-hook` with `json` on stdin, like Claude Code does. Returns its exit code.
DWORD runHookProcess(const std::string& json, double& seconds) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) return 999;
    SetHandleInformation(writeEnd, HANDLE_FLAG_INHERIT, 0);
    wchar_t exe[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH * 2 - 1);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --claude-hook";
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = readEnd;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi = {};
    double start = nowSeconds();
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(readEnd); CloseHandle(writeEnd); return 998;
    }
    CloseHandle(readEnd);
    DWORD wrote = 0;
    WriteFile(writeEnd, json.data(), (DWORD)json.size(), &wrote, nullptr);
    CloseHandle(writeEnd);
    DWORD code = 997;
    // Pump messages while waiting: the hook process SendMessage()s our window.
    while (MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, 10000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
    }
    GetExitCodeProcess(pi.hProcess, &code);
    seconds = nowSeconds() - start;
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return code;
}

/// `Whiskerkey.exe --claude-selftest`: hook parsing, bubble rules, and the real hook process talking to a
/// stand-in Whiskerkey window over WM_COPYDATA. Writes whisku-claude-selftest.txt; exit code 0 = pass.
int runClaudeSelfTest() {
    // Bubbles measure their text with GDI+, which must be started first (as the app does at launch).
    Gdiplus::GdiplusStartupInput gdipInput;
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, nullptr);
    std::string report;
    int failures = 0;
    auto save = [&] {   // after every check, so a crash still leaves a log that shows where
        HANDLE f = CreateFileW(L"whisku-claude-selftest.txt", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) { DWORD n = 0; WriteFile(f, report.data(), (DWORD)report.size(), &n, nullptr); CloseHandle(f); }
    };
    auto check = [&](bool ok, const std::string& what) {
        report += (ok ? "PASS  " : "FAIL  ") + what + "\r\n";
        if (!ok) ++failures;
        save();
    };
    report += "started\r\n";
    save();
    using claude::Kind;
    {
        auto e = claude::parseHook(R"({"hook_event_name":"Stop","cwd":"C:\\Users\\\u5f20\u4e09\\projects\\x_project\\","session_id":"s1"})");
        check(e && e->kind == Kind::Done && e->project == L"x_project" && e->session == L"s1", "Stop -> done, project from a Windows path");
        auto n = claude::parseHook(R"({"hook_event_name":"Notification","notification_type":"permission_prompt","message":"Claude needs your permission to use Bash","cwd":"/home/a/Whiskerkey","session_id":"s2"})");
        check(n && n->kind == Kind::NeedsYou && n->project == L"Whiskerkey", "Notification permission_prompt -> needs you");
        check(!claude::parseHook(R"({"hook_event_name":"Notification","notification_type":"idle_prompt","message":"x","cwd":"C:\\a"})"), "idle_prompt ignored");
        check(!claude::parseHook(R"({"hook_event_name":"Notification","message":"Claude is waiting for your input","cwd":"C:\\a"})"), "'waiting for your input' ignored");
        auto r = claude::parseHook(R"({"hook_event_name":"PermissionRequest","tool_name":"Bash","cwd":"D:\\","session_id":"s3"})");
        check(r && r->kind == Kind::NeedsYou && r->project == L"Claude Code", "PermissionRequest at a drive root -> needs you, 'Claude Code'");
        auto u = claude::parseHook("{\"hook_event_name\":\"Stop\",\"cwd\":\"C:\\\\\xe9\xa1\xb9\xe7\x9b\xae\"}");
        check(u && u->project == L"\u9879\u76ee", "UTF-8 folder name");
        check(!claude::parseHook("not json") && !claude::parseHook(R"({"hook_event_name":"SessionStart"})"), "garbage and other events ignored");
    }
    {
        claude::Center c;
        double t = 1000.0;
        check(c.receive(Kind::NeedsYou, L"p", L"s", nullptr, t) != nullptr, "first needs-you bubble shown");
        check(c.receive(Kind::NeedsYou, L"p", L"s", nullptr, t + 1) == nullptr, "duplicate needs-you within 3 s dropped");
        c.receive(Kind::Done, L"p", L"s", nullptr, t + 5);
        check(c.alerts.size() == 1 && c.alerts[0].kind == Kind::Done && !c.needsAttention(), "done replaces the session's needs-you bubble");
        for (int i = 0; i < 5; ++i) c.receive(Kind::Done, L"p", L"x" + std::to_wstring(i), nullptr, t + 6 + i);
        check(c.alerts.size() == 3, "at most 3 bubbles");
        c.step(t + 6 + 4 + 46);
        check(c.alerts.empty(), "done bubbles expire after 45 s");
        auto a = c.receivePayload(L"needs\nWhiskerkey\nsess\n0", t);
        check(a && a->kind == Kind::NeedsYou && a->project == L"Whiskerkey" && c.hit({ CatPhysics::SeatAnchor.x, claude::Center::NewestBubbleY + 10.0f }, t + 1) == a->id, "payload parsed, bubble clickable");
        check(c.receivePayload(L"bogus\nx\ny", t) == nullptr, "bad payload ignored");
        c.enabled = false;
        check(c.receive(Kind::Done, L"p", L"z", nullptr, t) == nullptr, "nothing while disabled");
    }
    {   // End to end: the real hook process finds a stand-in Whiskerkey window and delivers the event.
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = claudeTestWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = TRAY_OWNER_CLASS;
        RegisterClassExW(&wc);
        std::wstring got;
        double secs = 0;
        DWORD code = runHookProcess(R"({"hook_event_name":"Stop","cwd":"C:\\Users\\a\\x_project","session_id":"e2e"})", secs);
        check(code == 0, "hook exits 0 with no Whiskerkey running");   // window not created yet
        HWND w = CreateWindowExW(0, TRAY_OWNER_CLASS, L"WhiskerkeyTray", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
        SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)&got);
        code = runHookProcess(R"({"hook_event_name":"Stop","cwd":"C:\\Users\\a\\x_project","session_id":"e2e"})", secs);
        check(code == 0 && got.rfind(L"done\nx_project\ne2e\n", 0) == 0, "hook process -> WM_COPYDATA delivered");
        report += "      hook process took " + std::to_string((int)(secs * 1000)) + " ms\r\n";
        got.clear();
        code = runHookProcess("garbage", secs);
        check(code == 0 && got.empty(), "garbage input: exit 0, nothing sent");
        DestroyWindow(w);
    }
    std::wstring snippet = claude::settingsSnippet(L"C:\\Users\\a\\Whiskerkey.exe");
    check(snippet.find(L"\"command\": \"C:/Users/a/Whiskerkey.exe\", \"args\": [\"--claude-hook\"]") != std::wstring::npos, "settings snippet uses exec form");
    {   // After the rename to Whisku.exe, hooks that start Whiskerkey.exe are pointed at the new exe.
        std::string before = catlink::narrow(snippet) + R"({"x": "C:\\Users\\a\\whiskerkey.EXE", "keep": "D:/Other/Whiskerkey.exe"})";
        std::string after = claude::pointHooks(before, "C:\\Users\\a\\Whiskerkey.exe", "C:\\Users\\a\\Whisku.exe");
        size_t n = 0;
        for (size_t p = 0; (p = after.find("\"C:/Users/a/Whisku.exe\", \"args\": [\"--claude-hook\"]", p)) != std::string::npos; ++p) ++n;
        check(n == 3 && after.find("C:/Users/a/Whiskerkey.exe") == std::string::npos, "renamed exe: all three hooks point at Whisku.exe");
        check(after.find(R"("C:\\Users\\a\\Whisku.exe")") != std::string::npos, "renamed exe: escaped backslash path, any case, replaced too");
        check(after.find("D:/Other/Whiskerkey.exe") != std::string::npos, "renamed exe: another folder's exe is left alone");
        check(claude::pointHooks("{}", "C:\\a\\Whiskerkey.exe", "C:\\a\\Whisku.exe") == "{}", "renamed exe: nothing to change -> unchanged");
    }
    check(catlink::JsonReader::parse(catlink::narrow(snippet)).has_value(), "settings snippet is valid JSON");
    report += failures == 0 ? "RESULT: all passed\r\n" : "RESULT: " + std::to_string(failures) + " failed\r\n";
    save();
    Gdiplus::GdiplusShutdown(gdipToken);
    return failures == 0 ? 0 : 1;
}

/// `Whiskerkey.exe --break-selftest`: the break reminder rules (same cases as the macOS test), the
/// tiny-movement filter, and the bubble's buttons. Writes whisku-break-selftest.txt; exit code 0 = pass.
int runBreakSelfTest() {
    Gdiplus::GdiplusStartupInput gdipInput;
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, nullptr);
    std::string report;
    int failures = 0;
    auto save = [&] {
        HANDLE f = CreateFileW(L"whisku-break-selftest.txt", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) { DWORD n = 0; WriteFile(f, report.data(), (DWORD)report.size(), &n, nullptr); CloseHandle(f); }
    };
    auto check = [&](bool ok, const std::string& what) {
        report += (ok ? "PASS  " : "FAIL  ") + what + "\r\n";
        if (!ok) ++failures;
        save();
    };
    report += "started\r\n";
    save();
    using breaks::Logic; using breaks::Phase; using breaks::Button;
    const double H = 3600, M = 60;
    auto work = [](Logic& l, double from, double to) {
        for (double t = from; t < to; t += 1) { if ((long long)t % 20 == 0) l.noteInput(t); l.tick(t); }
    };
    auto idle = [](Logic& l, double from, double to) { for (double t = from; t < to; t += 1) l.tick(t); };
    auto shownAt = [](const Logic& l, double t) { return l.bubbleShownAt && *l.bubbleShownAt == t; };

    {   // first reminder after 1 h; ignored; I've rested
        Logic l(H, 0);
        work(l, 0, H - 1);
        check(l.phase == Phase::Work && !l.bubbleShownAt, "no reminder before 1 h");
        work(l, H - 1, H + 1);
        check(l.phase == Phase::Remind && shownAt(l, H) && !l.bubbleOffersRested, "first reminder at 1 h, without I've rested");
        check(l.isWaving(H + 6.9) && !l.isWaving(H + 7.1), "waves for 7 s");
        work(l, H + 1, H + 31);
        check(!l.bubbleShownAt && l.phase == Phase::Remind, "bubble gone after 30 s, still reminding (ignored)");
        work(l, H + 31, H + 30 * M + 1);
        check(shownAt(l, H + 30 * M) && l.bubbleOffersRested && l.remindersShown == 2, "ignored + working: second reminder 30 min later, with I've rested");
        l.tap(Button::Rested, H + 30 * M + 5);
        check(l.phase == Phase::Work && l.workStart == H + 30 * M + 5 && !l.bubbleShownAt, "I've rested -> work, fresh timer");
        work(l, H + 30 * M + 6, 2 * H + 30 * M + 4);
        check(l.phase == Phase::Work, "no reminder until a full hour after I've rested");
        work(l, 2 * H + 30 * M + 4, 2 * H + 30 * M + 7);
        check(l.phase == Phase::Remind && !l.bubbleOffersRested, "next round starts with a first reminder again");
    }
    {   // Later
        Logic l(H, 0);
        work(l, 0, H + 2);
        l.tap(Button::Later, H + 3);
        check(l.phase == Phase::Remind && !l.bubbleShownAt, "Later -> remind, bubble closed");
        work(l, H + 4, H + 30 * M + 1);
        check(shownAt(l, H + 30 * M) && l.bubbleOffersRested, "Later: next reminder in 30 min, with I've rested");
    }
    {   // OK, save for 25 s, then away
        Logic l(H, 0);
        work(l, 0, H + 2);
        l.tap(Button::Ok, H + 3);
        for (double s : { 5.0, 12.0, 20.0, 28.0 }) { l.noteInput(H + s); l.tick(H + s); }
        check(l.okAt.has_value() && l.phase == Phase::Remind, "input during the 30 s after OK is ignored");
        idle(l, H + 29, H + 28 + 2 * M - 1);
        check(l.phase == Phase::Remind, "not yet a break 1:59 after the last input");
        idle(l, H + 28 + 2 * M - 1, H + 28 + 2 * M + 1);
        check(l.phase == Phase::Rest && !l.bubbleShownAt, "OK + 2 min without input -> break");
        idle(l, H + 3 * M, H + 20 * M);
        check(l.phase == Phase::Rest, "stays on a break while away");
        l.noteInput(H + 20 * M);
        check(l.phase == Phase::Work && l.workStart == H + 20 * M, "back -> work, fresh timer");
    }
    {   // OK, then keeps working
        Logic l(H, 0);
        work(l, 0, H + 2);
        l.tap(Button::Ok, H + 3);
        work(l, H + 4, H + 30 * M + 1);
        check(l.phase == Phase::Remind && !l.okAt, "OK + keeps working -> still reminding");
        check(shownAt(l, H + 30 * M) && l.bubbleOffersRested, "OK + keeps working: reminder 30 min later, with I've rested");
    }
    {   // OK, back within 2 min; then thinking for 5 min
        Logic l(H, 0);
        work(l, 0, H + 2);
        l.tap(Button::Ok, H + 3);
        idle(l, H + 4, H + 3 + 100);
        l.noteInput(H + 3 + 100);
        check(l.phase == Phase::Remind && !l.okAt, "OK + back after 1:40 -> still reminding");
        idle(l, H + 104, H + 104 + 5 * M);
        check(l.phase == Phase::Remind, "after that, 5 min without input is not a break");
    }
    {   // OK, back after 3 min
        Logic l(H, 0);
        work(l, 0, H + 2);
        l.tap(Button::Ok, H + 3);
        idle(l, H + 4, H + 3 + 3 * M);
        l.noteInput(H + 3 + 3 * M);
        check(l.phase == Phase::Work && l.workStart == H + 3 + 3 * M, "OK + back after 3 min -> work");
    }
    {   // ignored: 14 min without input is thinking, 15 min is a break
        Logic l(H, 0);
        work(l, 0, H + 2);
        idle(l, H + 2, H + 2 + 14 * M);
        check(l.phase == Phase::Remind, "ignored + 14 min without input: still reminding");
        idle(l, H + 2 + 14 * M, H + 2 + 15 * M + 2);
        check(l.phase == Phase::Rest, "ignored + 15 min without input -> break");
    }
    {   // work: a 14 min pause keeps the timer, 15 min resets it
        Logic l(H, 0);
        work(l, 0, 20 * M);
        idle(l, 20 * M, 34 * M);
        l.noteInput(34 * M);
        check(l.phase == Phase::Work && l.workStart == 0, "14 min pause in work: timer keeps counting");
        idle(l, 34 * M, 49 * M + 1);
        check(l.phase == Phase::Rest, "15 min pause in work -> break");
        l.noteInput(50 * M);
        check(l.workStart == 50 * M, "back after the break: fresh timer");
    }
    {   // locked screen
        Logic l(H, 0);
        work(l, 0, 30 * M);
        l.screenLocked(30 * M);
        idle(l, 30 * M, 31 * M);
        l.screenUnlocked(31 * M); l.noteInput(31 * M);
        check(l.phase == Phase::Work && l.workStart == 0, "locked 1 min: not a break");
        l.screenLocked(40 * M);
        idle(l, 40 * M, 42 * M + 1);
        check(l.phase == Phase::Rest, "locked 2 min -> break");
        l.screenUnlocked(45 * M); l.noteInput(45 * M);
        check(l.phase == Phase::Work && l.workStart == 45 * M, "unlock + input: fresh timer");
    }
    {   // asleep (no ticks while asleep)
        Logic l(H, 0);
        work(l, 0, 50 * M);
        l.willSleep(50 * M);
        l.didWake(60 * M);
        check(l.phase == Phase::Rest, "asleep 10 min -> break, seen at wake");
        l.noteInput(60 * M + 5);
        check(l.phase == Phase::Work && l.workStart == 60 * M + 5, "first input after waking: fresh timer");
        l.willSleep(70 * M);
        l.didWake(71 * M);
        l.noteInput(71 * M);
        check(l.phase == Phase::Work && l.workStart == 60 * M + 5, "asleep 1 min: not a break");
    }
    {   // no reminder while locked
        Logic l(H, 0);
        work(l, 0, H - 30);
        l.screenLocked(H - 30);
        idle(l, H - 30, H + 60);
        check(!l.bubbleShownAt && l.remindersShown == 0, "no reminder while the screen is locked");
    }
    {   // settings
        Logic l(H, 0);
        work(l, 0, H + 2);
        l.setInterval(2 * H, H + 2);
        check(l.phase == Phase::Work && !l.bubbleShownAt, "raising the setting past the time worked stops reminding");
        l.setInterval(0, H + 3);
        work(l, H + 3, 4 * H);
        check(l.phase == Phase::Work && l.remindersShown == 0, "Off: never reminds");
        l.setInterval(30 * M, 4 * H);
        work(l, 4 * H, 4 * H + 2);
        check(l.phase == Phase::Remind, "turning it back on with less than the time worked reminds right away");
    }
    {   // the clock, tiny movements, the bubble's buttons and the preview
        double a = breaks::clock();
        check(a > 0 && breaks::clock() >= a, "GetTickCount64 clock");
        breaks::Center c;
        c.begin(60, 0);
        POINT p = {}; GetCursorPos(&p);
        c.logic.lastInput = 0;
        c.step({ p.x + 5, p.y + 5 }, 100);
        check(c.logic.lastInput == 0, "a 7 px nudge is not input");
        c.step({ p.x + 14, p.y }, 101);
        check(c.logic.lastInput == 101, "a 14 px move is input");

        breaks::Center v;
        double t = breaks::clock();
        v.begin(60, t);
        l10n::cachedChinese() = 0;
        check(breaks::spokenDuration(60) == L"1 minute" && breaks::spokenDuration(1800) == L"30 minutes" &&
              breaks::spokenDuration(3600) == L"1 hour" && breaks::spokenDuration(5400) == L"1 hour 30 min" &&
              breaks::spokenDuration(7200) == L"2 hours", "spoken durations");
        v.showPreview(t);
        check(v.isShowingBubble() && v.isWaving(t + 1), "preview shows the bubble and waves");
        std::vector<Button> found;
        for (float x = 0; x < CatPhysics::CanvasWidth; x += 2) {
            Button b = v.hit({ x, 46.0f + 21.0f - 16.0f });
            if (b != Button::None && (found.empty() || found.back() != b)) found.push_back(b);
        }
        check(found == std::vector<Button>{ Button::Ok, Button::Later, Button::Rested }, "buttons hit-test left to right: OK, Later, I've rested");
        bool onBubble = false;
        v.hit({ CatPhysics::SeatAnchor.x, 46.0f + 21.0f - 40.0f }, &onBubble);
        check(onBubble, "the title row is part of the bubble (clicks don't fall through)");
        v.tap(Button::Ok, t + 2);
        check(v.isShowingBubble() && !v.isWaving(t + 2), "OK in the preview: 'enjoy your break' shows");
        l10n::cachedChinese() = 1;
        v.showPreview(t + 3);
        v.tap(Button::Later, t + 4);
        check(!v.isShowingBubble(), "Later in the preview closes it");
        check(v.logic.phase == Phase::Work && v.logic.remindersShown == 0, "preview taps don't change the real state");
        l10n::cachedChinese() = -1;
    }
    report += failures == 0 ? "RESULT: all passed\r\n" : "RESULT: " + std::to_string(failures) + " failed\r\n";
    save();
    Gdiplus::GdiplusShutdown(gdipToken);
    return failures == 0 ? 0 : 1;
}

bool copySettingsKey(const wchar_t* from, const wchar_t* to, bool deleteOld);   // defined with the migration below

/// `Whisku.exe --update-selftest`: version rules, release parsing, and a real download through
/// GitHub (upstream's latest release has the same file names). Writes whisku-update-selftest.txt.
int runUpdateSelfTest() {
    std::string report;
    int failures = 0;
    auto save = [&] {
        HANDLE f = CreateFileW(L"whisku-update-selftest.txt", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) { DWORD n = 0; WriteFile(f, report.data(), (DWORD)report.size(), &n, nullptr); CloseHandle(f); }
    };
    auto check = [&](bool ok, const std::string& what) {
        report += (ok ? "PASS  " : "FAIL  ") + what + "\r\n";
        if (!ok) ++failures;
        save();
    };
    struct Case { const wchar_t* a; const wchar_t* b; bool newer; };
    for (const Case& c : { Case{ L"v1.3.3-mmccqq.6", L"1.3.3-mmccqq.5", true }, Case{ L"v1.3.3-mmccqq.5", L"1.3.3-mmccqq.5", false },
                           Case{ L"v1.3.3-mmccqq.4", L"1.3.3-mmccqq.5", false }, Case{ L"v1.3.3-mmccqq.10", L"1.3.3-mmccqq.9", true },
                           Case{ L"v1.3.3-mmccqq.5", L"1.3.3", true }, Case{ L"v1.3.4-mmccqq.1", L"1.3.3-mmccqq.9", true } }) {
        check(isSemVerNewer(c.a, c.b) == c.newer, catlink::narrow(std::wstring(c.a) + L" newer than " + c.b + (c.newer ? L" == true" : L" == false")));
    }
    check(versionToNumber(L"1.3.3-mmccqq.5") > versionToNumber(L"1.3.3-mmccqq.4") && versionToNumber(L"1.3.3-mmccqq.4") > 10303,
          "running-copy numbers grow with the fork number and beat the old 1.3.3 value (10303)");
    check(versionToNumber(APP_VERSION) >= 1000000, "APP_VERSION is a Whiskerkey version (1.0.0 or later)");
    check(!isWhiskerkeyRelease(L"v1.3.3-mmccqq.8") && isWhiskerkeyRelease(L"v1.0.1"), "KuroCat's releases are never updates for Whiskerkey");

    UpdateCheckResult r;
    check(parseLatestRelease(R"({"tag_name":"v1.3.3-mmccqq.9","html_url":"https://github.com/x/y/releases/tag/v1.3.3-mmccqq.9","assets":[{"name":"Whiskerkey-macOS-universal.zip","size":1,"browser_download_url":"https://a/mac.zip"},{"name":"Whiskerkey-Windows-x64.exe","size":585728,"browser_download_url":"https://a/win.exe"}]})", r)
          && r.latestTag == L"v1.3.3-mmccqq.9" && r.assetUrl == L"https://a/win.exe" && r.assetSize == 585728 && r.assetName == L"Whiskerkey-Windows-x64.exe", "release JSON -> tag, .exe URL and size");
    UpdateCheckResult kuroOnly;
    check(parseLatestRelease(R"({"tag_name":"v1.3.3-mmccqq.8","assets":[{"name":"KuroCat-Windows-x64.exe","size":1,"browser_download_url":"https://a/old.exe"}]})", kuroOnly)
          && kuroOnly.assetUrl.empty(), "a KuroCat exe is not a Whiskerkey download");
    UpdateCheckResult both;
    check(parseLatestRelease(R"({"tag_name":"v1.1.0","assets":[{"name":"Whiskerkey-Windows-x64.exe","size":1,"browser_download_url":"https://a/old.exe"},{"name":"Whisku-Windows-x64.exe","size":2,"browser_download_url":"https://a/new.exe"}]})", both)
          && both.assetName == L"Whisku-Windows-x64.exe" && both.assetUrl == L"https://a/new.exe", "a Whisku exe is preferred over a Whiskerkey one");
    UpdateCheckResult whiskuOnly;
    check(parseLatestRelease(R"({"tag_name":"v1.1.0","assets":[{"name":"Whisku-Windows-x64.exe","size":2,"browser_download_url":"https://a/new.exe"}]})", whiskuOnly)
          && whiskuOnly.assetUrl == L"https://a/new.exe", "a Whisku-only release is a download (the rename)");
    {
        UpdateTarget a = updateTarget(L"C:\\Users\\a\\Downloads\\Whiskerkey (1).exe", L"Whiskerkey-Windows-x64.exe", L"C:\\Users\\a\\AppData\\Local", L"C:\\T\\");
        check(a.renamed && a.exe == L"C:\\Users\\a\\Downloads\\Whiskerkey.exe" && a.fallbackExe == L"C:\\Users\\a\\AppData\\Local\\Programs\\Whiskerkey\\Whiskerkey.exe",
              "\"Whiskerkey (1).exe\" + update -> Whiskerkey.exe in the same folder");
        UpdateTarget b = updateTarget(L"C:\\Users\\a\\Downloads\\Whiskerkey.exe", L"Whiskerkey-Windows-x64.exe", L"", L"C:\\T\\");
        check(!b.renamed && b.exe == L"C:\\Users\\a\\Downloads\\Whiskerkey.exe" && b.fallbackExe == L"C:\\T\\Whiskerkey\\Whiskerkey.exe", "an update replaces Whiskerkey.exe");
        UpdateTarget c = updateTarget(L"D:\\Apps\\whiskerkey.EXE", L"Whiskerkey-Windows-x64.exe", L"C:\\L", L"C:\\T\\");
        check(!c.renamed && c.exe == L"D:\\Apps\\whiskerkey.EXE", "Whiskerkey.exe updates in place");
        UpdateTarget d = updateTarget(L"C:\\Users\\a\\Downloads\\Whiskerkey.exe", L"Whisku-Windows-x64.exe", L"C:\\Users\\a\\AppData\\Local", L"C:\\T\\");
        check(d.renamed && d.exe == L"C:\\Users\\a\\Downloads\\Whisku.exe" && d.fallbackExe == L"C:\\Users\\a\\AppData\\Local\\Programs\\Whisku\\Whisku.exe",
              "Whiskerkey.exe + Whisku update -> Whisku.exe in the same folder, Whiskerkey.exe removed");
        UpdateTarget e = updateTarget(L"C:\\Apps\\Whisku.exe", L"Whisku-Windows-x64.exe", L"C:\\L", L"C:\\T\\");
        check(!e.renamed && e.exe == L"C:\\Apps\\Whisku.exe", "Whisku.exe updates in place");
    }
    {   // Run the real batch file on throwaway files (`rem` instead of starting anything; PID 99999999 isn't running)
        wchar_t tmpW[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tmpW);
        std::string tmp = catlink::narrow(tmpW);
        auto writeFile = [](const std::string& path, const std::string& text) {
            HANDLE f = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f == INVALID_HANDLE_VALUE) return false;
            DWORD n = 0; BOOL ok = WriteFile(f, text.data(), (DWORD)text.size(), &n, nullptr); CloseHandle(f);
            return ok && n == text.size();
        };
        auto readFile = [](const std::string& path) -> std::string {
            HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            if (f == INVALID_HANDLE_VALUE) return "<missing>";
            char buf[64] = {}; DWORD n = 0; ReadFile(f, buf, sizeof(buf), &n, nullptr); CloseHandle(f);
            return std::string(buf, n);
        };
        auto exists = [](const std::string& path) { return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES; };
        auto runBatch = [&](const std::string& bat, const std::string& path) {
            if (!writeFile(path, bat)) return false;
            std::wstring cmd = L"cmd.exe /c \"" + utf8ToWide(path) + L"\"";
            STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi = {};
            if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
            bool done = WaitForSingleObject(pi.hProcess, 60000) == WAIT_OBJECT_0;
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            return done;
        };
        std::string dir = tmp + "whisku-update-test";
        CreateDirectoryA(dir.c_str(), nullptr);
        std::string oldExe = dir + "\\Whiskerkey (1).exe", src = dir + "\\Whiskerkey.update.exe", dst = dir + "\\Whiskerkey.exe", bat = dir + "\\u.bat";
        DeleteFileA(dst.c_str());
        writeFile(oldExe, "old"); writeFile(src, "new");
        bool ran = runBatch(updateBatch(99999999, src, dst, oldExe, true, dir + "\\fb", dir + "\\fb\\Whiskerkey.exe", "rem"), bat);
        check(ran && readFile(dst) == "new" && !exists(oldExe) && !exists(src) && !exists(bat),
              "batch: Whiskerkey.exe in place, \"Whiskerkey (1).exe\" and the download removed, batch deleted itself");
        writeFile(oldExe, "old"); writeFile(src, "new");
        writeFile(dst, "old");
        ran = runBatch(updateBatch(99999999, src, dst, dst, false, dir + "\\fb", dir + "\\fb\\Whiskerkey.exe", "rem"), bat);
        check(ran && readFile(dst) == "new" && !exists(src), "batch: same name replaces Whiskerkey.exe in place");
        DeleteFileA(oldExe.c_str()); DeleteFileA(dst.c_str());
        RemoveDirectoryA(dir.c_str());
    }
    UpdateCheckResult none;
    check(!parseLatestRelease(R"({"message":"Not Found"})", none) && !parseLatestRelease("garbage", none), "404 body and garbage rejected");
    check(ExtractIconExW(currentExePath().c_str(), -1, nullptr, nullptr, 0) >= 1, "the exe carries its icon (windows/app.rc)");

    DWORD status = 0;
    std::string body = httpsGet(L"https://api.github.com/repos/chanke17/KuroCat/releases/latest", status, 15000, 1u << 20);
    UpdateCheckResult live;
    bool parsed = status == 200 && parseLatestRelease(body, live, "KuroCat-Windows-x64.exe");   // upstream's file name
    check(parsed && !live.assetUrl.empty() && live.assetSize > 50000, "live GitHub API: " + catlink::narrow(live.latestTag) + ", exe " + std::to_string(live.assetSize) + " bytes (HTTP " + std::to_string(status) + ")");
    if (parsed && !live.assetUrl.empty()) {
        double t0 = nowSeconds();
        std::string exe = httpsGet(live.assetUrl, status, 60000, 64u << 20);
        report += "      download took " + std::to_string((int)((nowSeconds() - t0) * 1000)) + " ms\r\n";
        check((long long)exe.size() == live.assetSize && exe.size() > 2 && exe[0] == 'M' && exe[1] == 'Z',
              "download follows GitHub's redirect, exact size, starts with MZ (" + std::to_string(exe.size()) + " bytes)");
    }
    l10n::cachedChinese() = 1;
    bool zh = std::wstring(tr(L"中文", L"English")) == L"中文";
    l10n::cachedChinese() = 0;
    bool en = std::wstring(tr(L"中文", L"English")) == L"English";
    l10n::cachedChinese() = -1;
    check(zh && en, std::string("language switch works; this machine's display language -> ") + (l10n::isChinese() ? "Chinese" : "English"));

    // Older versions still ask the repo's old addresses (1.0.1: whiskerkey/whiskerkey, 1.0.2: whiskerkey/whisku);
    // GitHub redirects both to whisku-app/whisku.
    for (const wchar_t* old : { L"https://api.github.com/repos/whiskerkey/whiskerkey/releases/latest",
                                L"https://api.github.com/repos/whiskerkey/whisku/releases/latest" }) {
        DWORD moved = 0;
        std::string movedBody = httpsGet(old, moved, 15000, 1u << 20);
        UpdateCheckResult viaOldName;
        bool movedParsed = moved == 200 && parseLatestRelease(movedBody, viaOldName);   // before the message reads the tag
        check(movedParsed && !viaOldName.latestTag.empty(), "old address " + catlink::narrow(old).substr(29) + " still answers (HTTP "
              + std::to_string(moved) + ", " + catlink::narrow(viaOldName.latestTag) + ")");
    }
    {   // Moving settings to HKCU\Software\Whisku (throwaway keys, removed afterwards)
        const wchar_t* oldK = L"Software\\WhiskuSelfTestOld";
        const wchar_t* newK = L"Software\\WhiskuSelfTestNew";
        RegDeleteTreeW(HKEY_CURRENT_USER, oldK); RegDeleteTreeW(HKEY_CURRENT_USER, newK);
        const wchar_t code[] = L"WK-C-TEST";
        DWORD scale = 150;
        RegSetKeyValueW(HKEY_CURRENT_USER, oldK, L"CatLinkCode", REG_SZ, code, sizeof(code));
        RegSetKeyValueW(HKEY_CURRENT_USER, oldK, L"Scale", REG_DWORD, &scale, sizeof(scale));
        bool copied = copySettingsKey(oldK, newK, true);
        wchar_t got[64] = {}; DWORD gotSize = sizeof(got), gotScale = 0, scaleSize = sizeof(gotScale);
        bool haveCode = RegGetValueW(HKEY_CURRENT_USER, newK, L"CatLinkCode", RRF_RT_REG_SZ, nullptr, got, &gotSize) == ERROR_SUCCESS;
        bool haveScale = RegGetValueW(HKEY_CURRENT_USER, newK, L"Scale", RRF_RT_REG_DWORD, nullptr, &gotScale, &scaleSize) == ERROR_SUCCESS;
        HKEY probe = nullptr;
        bool oldGone = RegOpenKeyExW(HKEY_CURRENT_USER, oldK, 0, KEY_READ, &probe) != ERROR_SUCCESS;
        if (probe) RegCloseKey(probe);
        check(copied && haveCode && wcscmp(got, code) == 0 && haveScale && gotScale == 150 && oldGone, "settings moved to the new key, old key removed");
        RegSetKeyValueW(HKEY_CURRENT_USER, oldK, L"Scale", REG_DWORD, &scale, sizeof(scale));
        check(!copySettingsKey(oldK, newK, true), "settings are never copied over an existing key");
        check(!copySettingsKey(L"Software\\WhiskuSelfTestMissing", L"Software\\WhiskuSelfTestNew2", false), "nothing to copy -> false");
        RegDeleteTreeW(HKEY_CURRENT_USER, oldK); RegDeleteTreeW(HKEY_CURRENT_USER, newK);
    }
    DWORD missing = 0;
    httpsGet(L"https://api.github.com/repos/whisku-app/whisku/releases/tags/no-such-release-tag", missing, 15000, 1u << 16);
    check(missing == 404, "a missing release answers 404 (shown as 'no release yet'): HTTP " + std::to_string(missing));
    report += failures == 0 ? "RESULT: all passed\r\n" : "RESULT: " + std::to_string(failures) + " failed\r\n";
    save();
    return failures == 0 ? 0 : 1;
}

/// Copies an earlier app's settings (HKCU\<from>) to HKCU\<to> if <to> doesn't exist yet. With
/// `deleteOld`, the old key is removed once the copy worked. Returns true if it copied.
bool copySettingsKey(const wchar_t* from, const wchar_t* to, bool deleteOld) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, to, 0, KEY_READ, &key) == ERROR_SUCCESS) { RegCloseKey(key); return false; }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, from, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    HKEY created = nullptr;
    bool copied = false;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, to, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &created, nullptr) == ERROR_SUCCESS) {
        copied = RegCopyTreeW(key, nullptr, created) == ERROR_SUCCESS;
        RegCloseKey(created);
    }
    RegCloseKey(key);
    if (copied && deleteOld) RegDeleteTreeW(HKEY_CURRENT_USER, from);
    return copied;
}

/// Asks a running copy of an earlier app (found by its window class names) to quit and waits for it:
/// two cats would both react to the keyboard. Same WM_REQUEST_QUIT number in all versions.
void quitEarlierApp(const wchar_t* trayClass, const wchar_t* overlayClass) {
    HWND tray = FindWindowW(trayClass, nullptr);
    if (!tray) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(tray, &pid);
    HANDLE proc = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr;
    PostMessageW(tray, WM_REQUEST_QUIT, 0, 0);
    if (HWND overlay = FindWindowW(overlayClass, nullptr)) PostMessageW(overlay, WM_CLOSE, 0, 0);
    if (proc) { WaitForSingleObject(proc, 5000); CloseHandle(proc); }
}

/// Whisku was called Whiskerkey up to 1.1.0 and KuroCat before that. On the first launch of 1.2.0 it
/// moves Whiskerkey's settings (HKCU\Software\Whiskerkey, incl. the Cat Link pairing code and nickname)
/// to HKCU\Software\Whisku, or copies KuroCat's (HKCU\Software\KuroCat) if there's no Whiskerkey; moves
/// "start with Windows" over; and asks a Whiskerkey or KuroCat that is still running to quit. Old exes
/// stay where they are; they can simply be deleted.
void migrateFromKuroCat() {
    if (!copySettingsKey(L"Software\\Whiskerkey", L"Software\\Whisku", true))
        copySettingsKey(L"Software\\KuroCat", L"Software\\Whisku", false);
    HKEY run = nullptr;
    bool kuroCatStartedWithWindows = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ | KEY_SET_VALUE, &run) == ERROR_SUCCESS) {
        DWORD type = 0, size = 0;
        kuroCatStartedWithWindows = RegQueryValueExW(run, L"KuroCat", nullptr, &type, nullptr, &size) == ERROR_SUCCESS;
        if (kuroCatStartedWithWindows) RegDeleteValueW(run, L"KuroCat");
        RegCloseKey(run);
    }
    if (kuroCatStartedWithWindows) setAutoStartEnabled(true);   // a "Whiskerkey" entry is renamed by setAutoStartEnabled
    quitEarlierApp(L"WhiskerkeyTrayOwnerClass", L"WhiskerkeyOverlayWindowClass");
    quitEarlierApp(L"KuroCatTrayOwnerClass", L"KuroCatOverlayWindowClass");
}

/// Whiskerkey became Whisku in 1.1.0, so Claude Code's hooks may still start Whiskerkey.exe next to this
/// exe. Once that file is gone, they're pointed at this exe (the old file is kept as settings.json.before-whisku).
void repointClaudeHooksAfterRename() {
    std::wstring cur = currentExePath();
    size_t slash = cur.find_last_of(L"\\/");
    if (slash == std::wstring::npos || _wcsicmp(cur.c_str() + slash + 1, L"Whiskerkey.exe") == 0) return;
    std::wstring old = cur.substr(0, slash) + L"\\Whiskerkey.exe";
    if (GetFileAttributesW(old.c_str()) != INVALID_FILE_ATTRIBUTES) return;   // still there: leave the hooks alone
    wchar_t profile[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    std::wstring settingsPath = std::wstring(profile) + L"\\.claude\\settings.json";
    HANDLE f = CreateFileW(settingsPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    std::string text;
    char buf[4096];
    for (DWORD got = 0; ReadFile(f, buf, sizeof(buf), &got, nullptr) && got > 0 && text.size() < (4u << 20);) text.append(buf, got);
    CloseHandle(f);
    std::string updated = claude::pointHooks(text, catlink::narrow(old), catlink::narrow(cur));
    if (updated == text) return;
    if (!CopyFileW(settingsPath.c_str(), (settingsPath + L".before-whisku").c_str(), FALSE)) return;
    f = CreateFileW(settingsPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, updated.data(), (DWORD)updated.size(), &written, nullptr);
    CloseHandle(f);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    // Enable crisp Per-Monitor V2 DPI awareness on Windows 10/11
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    if (commandLineHasFlag(L"--claude-hook")) {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::wstring manual;
        for (int i = 1; argv && i + 1 < argc; ++i) if (wcscmp(argv[i], L"--claude-hook") == 0) manual = argv[i + 1];
        if (argv) LocalFree(argv);
        return claude::runHook(TRAY_OWNER_CLASS, manual);
    }
    if (commandLineHasFlag(L"--claude-selftest")) return runClaudeSelfTest();
    if (commandLineHasFlag(L"--break-selftest")) return runBreakSelfTest();
    if (commandLineHasFlag(L"--update-selftest")) return runUpdateSelfTest();
    if (commandLineHasFlag(L"--link-selftest")) {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::wstring join;
        for (int i = 1; argv && i + 1 < argc; ++i) if (wcscmp(argv[i], L"--link-selftest") == 0) join = argv[i + 1];
        if (argv) LocalFree(argv);
        return runLinkSelfTest(join);
    }
    if (commandLineHasFlag(L"--render-preview")) {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::wstring out = L"preview.png";
        for (int i = 1; argv && i + 1 < argc; ++i) if (wcscmp(argv[i], L"--render-preview") == 0) out = argv[i + 1];
        if (argv) LocalFree(argv);
        return renderPreviewSheet(out);
    }

    // Before anything reads the settings (the language is read on the first text shown).
    migrateFromKuroCat();

    const int myVersion = versionToNumber(APP_VERSION);
    const bool forceTakeover = commandLineHasFlag(L"--takeover");   // admin relaunch
    const bool noElevate = commandLineHasFlag(L"--no-elevate");
    const wchar_t* kMutexName = L"Local\\WhiskuDesktopPetSingleInstance";

    // Single-instance guard. Re-launching the SAME version wakes the existing cat (cancels full
    // click-through and pops the menu so users never get stuck). Launching a NEWER version (e.g. a
    // freshly downloaded exe while the old one still sits in the tray or autostarts from another
    // folder) now replaces the old process instead of silently waking it — previously the old
    // version kept running and kept reporting "new version available".
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, kMutexName);
    DWORD mutexErr = GetLastError();
    bool alreadyRunning = (hMutex && mutexErr == ERROR_ALREADY_EXISTS) || (!hMutex && mutexErr == ERROR_ACCESS_DENIED);
    if (alreadyRunning) {
        HWND owner = FindWindowW(TRAY_OWNER_CLASS, nullptr);
        if (!owner && forceTakeover) {
            // The instance we replace is already shutting down; give it a moment to disappear.
            for (int i = 0; i < 50 && FindWindowW(OVERLAY_CLASS, nullptr); ++i) Sleep(100);
        } else if (!owner) {
            if (hMutex) CloseHandle(hMutex);
            return 0;
        }
        if (owner) {
            int runningVersion = (int)(INT_PTR)GetPropW(owner, VERSION_PROP);
            bool replace = forceTakeover || runningVersion < myVersion;
            if (!replace || !shutDownRunningInstance(owner, 6000)) {
                PostMessageW(owner, WM_WAKE_EXISTING, 0, 0);
                if (hMutex) CloseHandle(hMutex);
                return 0;
            }
        }
        if (!hMutex) hMutex = CreateMutexW(nullptr, FALSE, kMutexName);
    }

    AppState app;
    g_app = &app;
    app.settings.loadFromRegistry();
    app.isElevated = isCurrentProcessElevated();
    app.savedLinkCode = regReadString(L"LinkCode");
    app.linkServer = regReadString(L"LinkServer") == L"G" ? 'G' : 'C';
    app.showFriendCat = regReadDword(L"LinkShowFriend", 1) != 0;
    app.nickname = regReadString(L"LinkNickname");
    app.claude.enabled = regReadDword(L"ClaudeAlerts", 1) != 0;
    app.claude.sound = regReadDword(L"ClaudeAlertSound", 1) != 0;
    app.breaks.begin((int)std::min<DWORD>(regReadDword(L"BreakReminderMinutes", 60), 24 * 60), breaks::clock());
    app.link.profile = [&app] { return linkProfile(app); };

    // "Always run as administrator": hand over to an elevated copy (one UAC prompt). If the user
    // declines, simply carry on non-elevated.
    if (app.settings.runAsAdmin && !app.isElevated && !noElevate) {
        if (hMutex) { CloseHandle(hMutex); hMutex = nullptr; }
        if (relaunchSelfElevated(L"--takeover --no-elevate")) {
            g_app = nullptr;
            return 0;
        }
        hMutex = CreateMutexW(nullptr, FALSE, kMutexName);
    }

    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    Gdiplus::GdiplusStartupInput gdipInput;
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, nullptr);

    // Refresh auto-start exe path if user enabled auto-start and moved Whisku.exe
    if (isAutoStartEnabled()) {
        setAutoStartEnabled(true);
    }
    repointClaudeHooksAfterRename();

    app.input.onKeyDown = [&](PhysicalKey k) {
        app.physics.triggerKeyDown(k, app.settings);
        app.link.noteKeyDown(k, prefersViewerLeftPaw(k), g_passwordWatcher.isPassword.load());
        app.breaks.noteInput(breaks::clock());
    };
    app.input.onKeyUp = [&](PhysicalKey k) { app.link.noteKeyUp(k); };
    app.input.onMouseClick = [&](int btn, bool down) {
        app.physics.triggerMouseClick(btn, down, app.settings);
        app.link.noteClick(btn, down);
        if (down) app.breaks.noteInput(breaks::clock());
    };

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = OVERLAY_CLASS;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    // Dedicated hidden tray/menu owner window (NO WS_EX_NOACTIVATE / WS_EX_TRANSPARENT)
    WNDCLASSEXW trayWc = {};
    trayWc.cbSize = sizeof(trayWc);
    trayWc.lpfnWndProc = wndProc;
    trayWc.hInstance = hInst;
    trayWc.lpszClassName = TRAY_OWNER_CLASS;
    RegisterClassExW(&trayWc);

    WNDCLASSEXW friendWc = {};
    friendWc.cbSize = sizeof(friendWc);
    friendWc.lpfnWndProc = friendWndProc;
    friendWc.hInstance = hInst;
    friendWc.lpszClassName = FRIEND_CLASS;
    friendWc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&friendWc);

    app.menuOwnerHwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        trayWc.lpszClassName,
        L"WhiskuTray",
        WS_POPUP,
        0, 0, 0, 0,
        nullptr, nullptr, hInst, nullptr
    );
    if (app.menuOwnerHwnd) {
        SetPropW(app.menuOwnerHwnd, VERSION_PROP, (HANDLE)(INT_PTR)myVersion);
        // When elevated, UIPI would drop messages from the (non-elevated) taskbar and from a
        // re-launched Whisku. Explicitly allow the few messages we rely on.
        const UINT allowed[] = { WM_TRAYICON, WM_WAKE_EXISTING, WM_REQUEST_QUIT, WM_COPYDATA };
        for (UINT m : allowed) ChangeWindowMessageFilterEx(app.menuOwnerHwnd, m, MSGFLT_ALLOW, nullptr);
        if (g_msgTaskbarCreated) ChangeWindowMessageFilterEx(app.menuOwnerHwnd, g_msgTaskbarCreated, MSGFLT_ALLOW, nullptr);
    }

    int initW = (int)std::round(CatPhysics::CanvasWidth * app.settings.scale);
    int initH = (int)std::round(CatPhysics::CanvasHeight * app.settings.scale);
    int initX = 0, initY = 0;
    if (app.settings.posX != -99999 && app.settings.posY != -99999) {
        initX = app.settings.posX; initY = app.settings.posY;
    } else {
        computeDefaultOrigin(initW, initH, initX, initY);
    }

    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT
                  | (app.settings.alwaysOnTop ? WS_EX_TOPMOST : 0);

    app.hwnd = CreateWindowExW(
        exStyle,
        wc.lpszClassName,
        L"Whisku",
        WS_POPUP,
        initX, initY, initW, initH,
        nullptr, nullptr, hInst, nullptr
    );
    if (!app.hwnd) {
        if (app.menuOwnerHwnd) DestroyWindow(app.menuOwnerHwnd);
        Gdiplus::GdiplusShutdown(gdipToken);
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    applyWindowScaleAndPosition(app, false);
    app.input.registerRawInput(app.hwnd);
    registerTrayIcon(app);

    // Win+L / fast-user-switch notifications (see recoverAfterSystemEvent)
    app.sessionNotifyRegistered = WTSRegisterSessionNotification(app.hwnd, NOTIFY_FOR_THIS_SESSION) != FALSE;
    // Foreground changes (games launching) -> re-assert topmost
    app.foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                                         onForegroundChanged, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    // Global shortcut Ctrl+Alt+Shift+P to toggle full click-through anytime!
    if (app.menuOwnerHwnd) {
        RegisterHotKey(app.menuOwnerHwnd, HOTKEY_CLICKTHROUGH_ID, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'P');
        if (UPDATE_CHECKS_ENABLED) startAsyncUpdateCheck(app.menuOwnerHwnd, false, 2500);
    }

    ShowWindow(app.hwnd, SW_SHOWNOACTIVATE);

    // Cat Link: hide keyboard activity in password fields; reconnect if it was on.
    g_passwordWatcher.start();
    g_audioWatcher.start();
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, mouseHookProc, hInst, 0);
    app.link.setPaused(regReadDword(L"LinkPaused", 0) != 0);
    if (regReadDword(L"LinkEnabled", 0)) {
        if (auto code = catlink::Code::parse(app.savedLinkCode)) app.link.start(*code);
    }

    app.lastTickTime = nowSeconds();
    tickAndRender(app);
    app.nextFrameTime = nowSeconds() + FRAME_INTERVAL;
    SetTimer(app.hwnd, TIMER_RENDER, 16, nullptr);

    // Frame loop. WM_TIMER is only generated when the queue is empty, so a 1000-8000 Hz gaming mouse
    // (one WM_INPUT per report) could starve it and freeze the cat. Here frames are driven by time:
    // drain messages for at most ~10 ms, then render if a frame is due. WM_TIMER still calls
    // renderIfDue() so the cat keeps animating inside menus / message boxes (their own modal loops).
    MSG msg = {};
    int exitCode = 0;
    bool running = true;
    while (running) {
        double now = nowSeconds();
        bool frameRate = app.isVisible || app.friendCat.shown || app.link.connected();
        double waitSec = frameRate ? (app.nextFrameTime - now) : 0.25;
        if (waitSec > 0.0) {
            MsgWaitForMultipleObjectsEx(0, nullptr, (DWORD)(waitSec * 1000.0), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        double drainUntil = nowSeconds() + 0.010;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                exitCode = (int)msg.wParam;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (nowSeconds() > drainUntil) break;
        }
        if (!running) break;
        renderIfDue(app);
        linkTick(app);
        housekeeping(app);
    }

    g_app = nullptr;
    Gdiplus::GdiplusShutdown(gdipToken);
    if (hMutex) CloseHandle(hMutex);
    return exitCode;
}
