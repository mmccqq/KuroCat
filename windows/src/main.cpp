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

constexpr const wchar_t* APP_VERSION = L"1.3.3";
constexpr const wchar_t* VERSION_PROP = L"KuroCatVersion"; // set on the tray owner window so newer builds can take over older ones
constexpr const wchar_t* TRAY_OWNER_CLASS = L"KuroCatTrayOwnerClass";
constexpr const wchar_t* OVERLAY_CLASS = L"KuroCatOverlayWindowClass";
constexpr const wchar_t* FRIEND_CLASS = L"KuroCatFriendWindowClass";
// This fork has its own features (e.g. Cat Link): never offer to "update" it to the upstream release.
constexpr bool UPDATE_CHECKS_ENABLED = false;
constexpr double FRAME_INTERVAL = 1.0 / 60.0;
constexpr const wchar_t* UPDATE_PAGE_URL = L"https://kurocat-download.app.workbuddy.host/?sharecode=JVAABIVZ7L";
constexpr const wchar_t* UPDATE_WIN_EXE_PATH = L"/downloads/KuroCat-Windows-x64.exe?sharecode=JVAABIVZ7L";
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
    CMD_TOGGLE_EARBUDS,
    CMD_SHOW_ALL,
    CMD_HIDE_ALL,
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

bool isAutoStartEnabled() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0, size = 0;
    bool exists = (RegQueryValueExW(hKey, L"KuroCat", nullptr, &type, nullptr, &size) == ERROR_SUCCESS && size > 0);
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
            ok = (RegSetValueExW(hKey, L"KuroCat", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS);
        }
    } else {
        LSTATUS st = RegDeleteValueW(hKey, L"KuroCat");
        ok = (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND);
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
// is for KuroCat to run at the same level, so we detect it and offer an admin relaunch.

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
    std::wstring latestTag;
    std::wstring releaseUrl;
};

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string extractJsonStringField(const std::string& json, const char* fieldName) {
    std::string key = std::string("\"") + fieldName + "\"";
    size_t pos = json.find(key);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + key.size());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    size_t end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
}

bool isSemVerNewer(const std::wstring& lhs, const std::wstring& rhs) {
    auto parse = [](const std::wstring& v) {
        std::vector<int> parts;
        size_t i = 0;
        while (i < v.size() && (v[i] == L'v' || v[i] == L'V' || v[i] == L' ')) i++;
        int cur = 0; bool inNum = false;
        for (; i < v.size(); ++i) {
            if (v[i] >= L'0' && v[i] <= L'9') {
                cur = cur * 10 + (v[i] - L'0');
                inNum = true;
            } else if (v[i] == L'.') {
                parts.push_back(inNum ? cur : 0);
                cur = 0; inNum = false;
            } else {
                break;
            }
        }
        if (inNum) parts.push_back(cur);
        return parts;
    };
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

/// "1.3.3" -> 10303. Stored as a window property so a newly launched (newer) build can tell
/// which version is already running. Builds up to 1.3.2 never set it, which reads back as 0.
int versionToNumber(const std::wstring& v) {
    int parts[3] = { 0, 0, 0 };
    int idx = 0;
    for (wchar_t c : v) {
        if (c >= L'0' && c <= L'9') parts[idx] = parts[idx] * 10 + (c - L'0');
        else if (c == L'.') { if (++idx >= 3) break; }
    }
    return parts[0] * 10000 + parts[1] * 100 + parts[2];
}

std::wstring currentExePath() {
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD len = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0]) - 1));
    return std::wstring(buf, len);
}

std::string extractVersionFromHtml(const std::string& html) {
    auto parseVerAt = [&](size_t pos) -> std::string {
        if (pos >= html.size() || (html[pos] != 'v' && html[pos] != 'V')) return "";
        size_t i = pos + 1;
        int dots = 0;
        std::string ver = "v";
        while (i < html.size()) {
            char c = html[i];
            if (c >= '0' && c <= '9') {
                ver.push_back(c);
            } else if (c == '.') {
                dots++;
                ver.push_back(c);
            } else {
                break;
            }
            i++;
        }
        return (dots >= 2 && ver.size() > 5) ? ver : "";
    };

    const char* marker = "\xE5\xB7\xB2\xE5\x8F\x91\xE5\xB8\x83"; // "已发布" in UTF-8
    size_t mPos = html.find(marker);
    if (mPos != std::string::npos) {
        size_t searchStart = (mPos > 32) ? (mPos - 32) : 0;
        for (size_t i = mPos; i > searchStart; --i) {
            if (html[i - 1] == 'v' || html[i - 1] == 'V') {
                std::string v = parseVerAt(i - 1);
                if (!v.empty()) return v;
            }
        }
    }

    const char* verPrefix = "\xE7\x89\x88\xE6\x9C\xAC "; // "版本 " in UTF-8
    size_t vPos = html.find(verPrefix);
    if (vPos != std::string::npos) {
        std::string v = parseVerAt(vPos + 7);
        if (!v.empty()) return v;
    }
    return "";
}

std::string fetchWorkBuddyPath(HINTERNET hConnect, LPCWSTR path) {
    std::string body;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (hRequest) {
        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hRequest, nullptr)) {
            DWORD statusCode = 0, statusSize = sizeof(statusCode);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
            if (statusCode >= 200 && statusCode < 300) {
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
                    std::vector<char> buf(avail);
                    DWORD readBytes = 0;
                    if (WinHttpReadData(hRequest, buf.data(), avail, &readBytes) && readBytes > 0) {
                        body.append(buf.data(), readBytes);
                    } else {
                        break;
                    }
                }
            }
        }
        WinHttpCloseHandle(hRequest);
    }
    return body;
}

void startAsyncUpdateCheck(HWND notifyHwnd, bool manual, int delayMs = 0) {
    std::thread([notifyHwnd, manual, delayMs]() {
        if (delayMs > 0) Sleep((DWORD)delayMs);
        auto* res = new UpdateCheckResult();
        res->manual = manual;

        std::wstring ua = std::wstring(L"KuroCat/") + APP_VERSION;
        HINTERNET hSession = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (hSession) {
            WinHttpSetTimeouts(hSession, 8000, 8000, 8000, 8000);
            HINTERNET hConnect = WinHttpConnect(hSession, L"kurocat-download.app.workbuddy.host", INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (hConnect) {
                std::string body = fetchWorkBuddyPath(hConnect, L"/version.json?sharecode=JVAABIVZ7L");
                std::string tag = extractJsonStringField(body, "tag_name");
                std::string url = extractJsonStringField(body, "html_url");

                if (tag.empty()) {
                    std::string html = fetchWorkBuddyPath(hConnect, L"/?sharecode=JVAABIVZ7L");
                    tag = extractVersionFromHtml(html);
                }

                if (!tag.empty()) {
                    res->ok = true;
                    res->latestTag = utf8ToWide(tag);
                    res->releaseUrl = url.empty() ? UPDATE_PAGE_URL : utf8ToWide(url);
                    res->hasUpdate = isSemVerNewer(res->latestTag, APP_VERSION);
                }
                WinHttpCloseHandle(hConnect);
            }
            WinHttpCloseHandle(hSession);
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

void startInAppUpdate(AppState& app) {
    if (app.isUpdating || !app.menuOwnerHwnd) return;
    app.isUpdating = true;
    HWND notifyHwnd = app.menuOwnerHwnd;

    std::thread([notifyHwnd]() {
        auto* res = new UpdateDownloadResult();
        std::string exeBytes;

        std::wstring ua = std::wstring(L"KuroCat/") + APP_VERSION;
        HINTERNET hSession = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (hSession) {
            WinHttpSetTimeouts(hSession, 10000, 10000, 30000, 60000);
            HINTERNET hConnect = WinHttpConnect(hSession, L"kurocat-download.app.workbuddy.host", INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (hConnect) {
                exeBytes = fetchWorkBuddyPath(hConnect, UPDATE_WIN_EXE_PATH);
                WinHttpCloseHandle(hConnect);
            }
            WinHttpCloseHandle(hSession);
        }

        if (exeBytes.size() > 50000 && exeBytes[0] == 'M' && exeBytes[1] == 'Z') {
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
                newExePath = std::wstring(tempDir) + L"KuroCat_update.exe";
                hNew = CreateFileW(newExePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            }
            if (hNew != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                BOOL okWrite = WriteFile(hNew, exeBytes.data(), (DWORD)exeBytes.size(), &written, nullptr);
                CloseHandle(hNew);
                if (okWrite && written == (DWORD)exeBytes.size()) {
                    std::wstring batPath = std::wstring(tempDir) + L"kurocat_update.bat";
                    DWORD pid = GetCurrentProcessId();
                    std::string batUtf8 = "@echo off\r\n:wait\r\n"
                        "tasklist /FI \"PID eq " + std::to_string(pid) + "\" 2>nul | find \"" + std::to_string(pid) + "\" >nul\r\n"
                        "if not errorlevel 1 (\r\n"
                        "  ping -n 2 127.0.0.1 >nul\r\n"
                        "  goto wait\r\n"
                        ")\r\n";

                    auto wideToUtf8 = [](const std::wstring& w) -> std::string {
                        if (w.empty()) return "";
                        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
                        std::string s(n, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
                        return s;
                    };
                    // Use local ANSI codepage for cmd.exe .bat compatibility
                    auto wideToAcp = [](const std::wstring& w) -> std::string {
                        if (w.empty()) return "";
                        int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
                        std::string s(n, '\0');
                        WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
                        return s;
                    };
                    (void)wideToUtf8;
                    std::string srcAcp = wideToAcp(newExePath);
                    std::string dstAcp = wideToAcp(curExe);

                    // Per-user fallback location (no admin rights needed) for when the old exe cannot be
                    // overwritten (read-only folder, antivirus lock...). Previously the move failed silently,
                    // the OLD exe was restarted and users kept being told a new version was available.
                    wchar_t localAppData[MAX_PATH] = {};
                    GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
                    std::wstring fallbackDirW = localAppData[0] ? (std::wstring(localAppData) + L"\\Programs\\KuroCat")
                                                                : (std::wstring(tempDir) + L"KuroCat");
                    std::string fbDirAcp = wideToAcp(fallbackDirW);
                    std::string fbExeAcp = fbDirAcp + "\\KuroCat.exe";

                    batUtf8 += "set /a tries=0\r\n"
                               ":retry\r\n"
                               "move /y \"" + srcAcp + "\" \"" + dstAcp + "\" >nul 2>&1\r\n"
                               "if not errorlevel 1 goto launch\r\n"
                               "set /a tries+=1\r\n"
                               "if %tries% lss 15 (\r\n"
                               "  ping -n 2 127.0.0.1 >nul\r\n"
                               "  goto retry\r\n"
                               ")\r\n"
                               "if not exist \"" + fbDirAcp + "\" mkdir \"" + fbDirAcp + "\"\r\n"
                               "copy /y \"" + srcAcp + "\" \"" + fbExeAcp + "\" >nul 2>&1\r\n"
                               "if errorlevel 1 goto launch\r\n"
                               "del /q \"" + srcAcp + "\" >nul 2>&1\r\n"
                               "start \"\" \"" + fbExeAcp + "\"\r\n"
                               "goto done\r\n"
                               ":launch\r\n"
                               "start \"\" \"" + dstAcp + "\"\r\n"
                               ":done\r\n"
                               "del \"%~f0\"\r\n";

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
    wcscpy_s(app.nid.szTip, L"KuroCat \u9ED1\u732B\u952E\u76D8\u684C\u5BA0 (\u53F3\u952E/\u5DE6\u952E\u6253\u5F00\u83DC\u5355)");
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
                    L"KuroCat 听不到这个游戏的键盘",
                    L"该游戏以管理员权限运行，Windows 会拦截它的键盘输入。右键小猫/托盘图标 →「以管理员身份重新启动」即可恢复。");
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
    item(BS_DEFPUSHBUTTON | WS_TABSTOP, 100, 56, 54, 16, IDOK, 0x0080, L"\u4FDD\u5B58");       // 保存
    item(BS_PUSHBUTTON | WS_TABSTOP, 158, 56, 54, 16, IDCANCEL, 0x0080, L"\u53D6\u6D88");      // 取消
    PromptData data{ title, prompt, text };
    if (DialogBoxIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<LPCDLGTEMPLATEW>(t.data()), owner,
                                promptProc, reinterpret_cast<LPARAM>(&data)) != IDOK) return false;
    text = data.text;
    return true;
}

const wchar_t* LINK_WHAT_IS_SHARED =
    L"对方会看到：你在打字（只知道是左手还是右手在敲，不知道按了哪个键）、鼠标的移动、点击和滚动（包括光标在屏幕上的大致位置）、是否戴着耳机、你的猫咪皮肤和昵称。你们也能隔空摸对方的猫。\n"
    L"输入密码时会自动暂停发送键盘动作。所有内容都用配对码加密，中转服务器也看不到内容。";

std::wstring regReadString(const wchar_t* name) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\KuroCat", 0, KEY_READ, &k) != ERROR_SUCCESS) return L"";
    wchar_t buf[128] = {};
    DWORD size = sizeof(buf) - sizeof(wchar_t), type = 0;
    std::wstring out;
    if (RegQueryValueExW(k, name, nullptr, &type, (LPBYTE)buf, &size) == ERROR_SUCCESS && type == REG_SZ) out = buf;
    RegCloseKey(k);
    return out;
}

DWORD regReadDword(const wchar_t* name, DWORD def) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\KuroCat", 0, KEY_READ, &k) != ERROR_SUCCESS) return def;
    DWORD val = def, size = sizeof(val);
    if (RegQueryValueExW(k, name, nullptr, nullptr, (LPBYTE)&val, &size) != ERROR_SUCCESS) val = def;
    RegCloseKey(k);
    return val;
}

void regWrite(const wchar_t* name, const std::wstring* str, DWORD dword) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\KuroCat", 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
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
    if (nowSeconds() < app.demoUntil) { f.label = L"演示 · 模拟的朋友"; f.labelDim = false; return; }
    std::string name = app.link.friendName();
    std::wstring who = name.empty() ? L"朋友的猫" : catlink::widen(name) + L" 的猫";
    switch (app.link.status()) {
        case catlink::Status::Linked: f.label = who + L" · 在线"; f.labelDim = false; break;
        case catlink::Status::FriendOffline: f.label = who + L" · 离线"; f.labelDim = true; break;
        case catlink::Status::Connecting:
        case catlink::Status::Reconnecting: f.label = who + L" · 正在重新连接…"; f.labelDim = true; break;
        default: f.label = who; f.labelDim = true; break;
    }
}

void setTrayTip(AppState& app, const wchar_t* tip) {
    wcsncpy_s(app.nid.szTip, tip, _TRUNCATE);
    app.nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &app.nid);
    app.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
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
        f.hwnd = CreateWindowExW(exStyle, FRIEND_CLASS, L"KuroCat Friend", WS_POPUP, x, y, w, h,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    showFriendWindow(app, true);
}

void onLinkStatusChanged(AppState& app, catlink::Status before, catlink::Status now) {
    using catlink::Status;
    auto& f = app.friendCat;
    if (now == Status::FriendOffline || now == Status::Off) f.driver.releaseAll();
    if (now == Status::Off) showFriendWindow(app, false);
    if (now == Status::Linked && before != Status::Linked) {
        std::string name = app.link.friendName();
        std::wstring body = (name.empty() ? L"朋友" : catlink::widen(name)) + L" 的猫出现在你的桌面上了，它会跟着对方一起打字、动鼠标。";
        showTrayBalloon(app, L"🔗 猫咪连线成功", body.c_str());
    }
    setTrayTip(app, now == Status::Linked ? L"KuroCat 黑猫桌宠 · 猫咪连线中" : L"KuroCat 黑猫桌宠");
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
        if (app.link.status() == catlink::Status::Off) showFriendWindow(app, false);
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
    std::wstring friendW = fname.empty() ? L"朋友" : catlink::widen(fname);
    std::wstring status;
    switch (st) {
        case Status::Off: status = L"未连线"; break;
        case Status::Connecting: status = L"正在连接服务器…"; break;
        case Status::WaitingForFriend: status = L"已连接，等待朋友输入配对码"; break;
        case Status::Linked: status = L"已和 " + friendW + L" 连线 \u25CF"; break;
        case Status::FriendOffline: status = friendW + L" 已离线，等待对方回来"; break;
        case Status::Reconnecting: status = L"网络中断，正在重新连接…"; break;
    }
    AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, (L"状态：" + status).c_str());
    std::wstring err = app.link.lastError();
    if (st == Status::Reconnecting && !err.empty()) AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, (L"原因：" + err).c_str());
    AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
    if (st == Status::Off) {
        AppendMenuW(sub, MF_STRING, CMD_LINK_CREATE, L"创建配对码（自动复制，发给朋友）");
        AppendMenuW(sub, MF_STRING, CMD_LINK_JOIN, L"粘贴朋友的配对码并连线（请先复制配对码）");
        if (!app.savedLinkCode.empty()) AppendMenuW(sub, MF_STRING, CMD_LINK_RECONNECT, (L"重新连线（" + app.savedLinkCode + L"）").c_str());
    } else if (auto code = app.link.code()) {
        AppendMenuW(sub, MF_STRING, CMD_LINK_COPY, (L"复制配对码 " + code->display()).c_str());
        AppendMenuW(sub, MF_STRING, CMD_LINK_DISCONNECT, L"断开连线");
    }
    AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
    HMENU server = CreatePopupMenu();
    AppendMenuW(server, MF_STRING | (app.linkServer == 'C' ? MF_CHECKED : 0), CMD_LINK_SERVER_CN, L"EMQX 中国（上海）");
    AppendMenuW(server, MF_STRING | (app.linkServer == 'G' ? MF_CHECKED : 0), CMD_LINK_SERVER_GLOBAL, L"EMQX 国际（美国）");
    AppendMenuW(sub, MF_POPUP, (UINT_PTR)server, L"新配对码使用的服务器");
    std::wstring nickItem = L"我的昵称：" + (app.nickname.empty() ? std::wstring(L"未设置") : app.nickname) + L"…";
    AppendMenuW(sub, MF_STRING, CMD_LINK_NICKNAME, nickItem.c_str());
    AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sub, MF_STRING, CMD_LINK_DEMO, L"\U0001F3AC 演示：模拟一位朋友");
    AppendMenuW(sub, MF_STRING, CMD_LINK_EXPLAIN, L"对方能看到什么？");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)sub, st == Status::Linked ? L"\U0001F517 猫咪连线（已连线）" : L"\U0001F517 猫咪连线");
}

void handleLinkCommand(AppState& app, UINT cmd) {
    HWND owner = app.menuOwnerHwnd ? app.menuOwnerHwnd : app.hwnd;
    switch (cmd) {
        case CMD_LINK_CREATE: {
            catlink::Code code = catlink::Code::generate(app.linkServer);
            startLink(app, code);
            writeClipboardText(owner, code.display());
            std::wstring msg = L"配对码已复制到剪贴板：\n\n    " + code.display()
                + L"\n\n把它发给你的朋友（比如用微信）。朋友在 KuroCat 菜单「猫咪连线」里粘贴它，你们的猫就会出现在对方的屏幕上。"
                + L"\n配对码就是你们的密钥，请只发给这位朋友。\n\n" + LINK_WHAT_IS_SHARED;
            MessageBoxW(nullptr, msg.c_str(), L"KuroCat 猫咪连线", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
            break;
        }
        case CMD_LINK_JOIN: {
            auto code = catlink::Code::parse(readClipboardText(owner));
            if (!code) {
                MessageBoxW(nullptr, L"剪贴板里没有找到配对码。\n\n请先复制朋友发来的配对码（形如 KC-C-XXXX-XXXX-XXXX-XXXX-XXXX），再点这里。",
                            L"KuroCat 猫咪连线", MB_OK | MB_ICONWARNING | MB_TOPMOST);
                break;
            }
            std::wstring msg = L"要和这个配对码连线吗？\n\n    " + code->display() + L"\n\n" + LINK_WHAT_IS_SHARED;
            if (MessageBoxW(nullptr, msg.c_str(), L"KuroCat 猫咪连线", MB_OKCANCEL | MB_ICONQUESTION | MB_TOPMOST) == IDOK) {
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
            if (promptText(owner, L"你的昵称", L"会显示在朋友屏幕上你的猫的上方。", name)) {
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
        case CMD_LINK_EXPLAIN: {
            std::wstring msg = std::wstring(LINK_WHAT_IS_SHARED) + L"\n\n断开连线后就不再发送任何内容。";
            MessageBoxW(nullptr, msg.c_str(), L"猫咪连线会分享什么？", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
            break;
        }
        default:
            break;
    }
}

void showContextMenu(AppState& app, POINT screenPt) {
    HMENU menu = CreatePopupMenu();

    // Status & live stats header
    std::wstring statusStr = app.input.isRawInputRegistered
        ? L"\u25CF \u786C\u4EF6\u7EA7\u5168\u72B6\u6001\u76D1\u542C\u4E2D (Raw Input \u6E38\u620F/\u6253\u5B57)"
        : L"\u26A0 Raw Input \u672A\u542F\u52A8";
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, statusStr.c_str());
    if (!app.isElevated && app.lastBlockedForegroundTime > 0.0 && nowSeconds() - app.lastBlockedForegroundTime < 600.0) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"⚠ 刚才的游戏以管理员权限运行，键盘被 Windows 拦截了");
    }

    std::wstring stats = L"\u4ECA\u65E5\u51FB\u952E " + std::to_wstring(app.input.totalKeystrokesToday)
        + L"  \u00B7  KPM " + std::to_wstring(app.input.currentKPM())
        + L"  \u00B7  \u6478\u732B " + std::to_wstring(app.physics.totalPetSessions) + L" \u6B21";
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, stats.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // Which cats to show: ours and the friend's (Cat Link) independently, or all at once.
    HMENU showMenu = CreatePopupMenu();
    AppendMenuW(showMenu, MF_STRING | (app.isVisible ? MF_CHECKED : 0), CMD_TOGGLE_VISIBLE, L"我的猫");
    AppendMenuW(showMenu, MF_STRING | (app.showFriendCat ? MF_CHECKED : 0), CMD_LINK_SHOW_FRIEND, L"朋友的猫");
    AppendMenuW(showMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(showMenu, MF_STRING, CMD_SHOW_ALL, L"全部显示");
    AppendMenuW(showMenu, MF_STRING, CMD_HIDE_ALL, L"全部隐藏");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)showMenu, L"显示的猫咪");
    AppendMenuW(menu, MF_STRING | (app.settings.fullClickThrough ? MF_CHECKED : 0), CMD_TOGGLE_CLICKTHROUGH, L"\u5B8C\u5168\u9F20\u6807\u7A7F\u900F (Ctrl+Alt+Shift+P / \u6309\u4F4FAlt\u53EF\u53F3\u952E)");
    AppendMenuW(menu, MF_STRING | (app.settings.alwaysOnTop ? MF_CHECKED : 0), CMD_TOGGLE_TOPMOST, L"\u7A97\u53E3\u7F6E\u9876\u663E\u793A");
    AppendMenuW(menu, MF_STRING | (isAutoStartEnabled() ? MF_CHECKED : 0), CMD_TOGGLE_AUTOSTART, L"\u5F00\u673A\u81EA\u52A8\u542F\u52A8");
    if (app.isElevated) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"\u2713 \u5DF2\u4EE5\u7BA1\u7406\u5458\u8EAB\u4EFD\u8FD0\u884C\uFF08\u7BA1\u7406\u5458\u6743\u9650\u6E38\u620F\u7684\u952E\u76D8\u4E5F\u80FD\u76D1\u542C\uFF09");
    } else {
        AppendMenuW(menu, MF_STRING, CMD_RELAUNCH_ADMIN, L"\u4EE5\u7BA1\u7406\u5458\u8EAB\u4EFD\u91CD\u65B0\u542F\u52A8\uFF08\u6E38\u620F\u91CC\u952E\u76D8\u6CA1\u53CD\u5E94\u65F6\u70B9\u8FD9\u91CC\uFF09");
    }
    AppendMenuW(menu, MF_STRING | (app.settings.runAsAdmin ? MF_CHECKED : 0), CMD_TOGGLE_RUN_AS_ADMIN, L"\u6BCF\u6B21\u542F\u52A8\u90FD\u4EE5\u7BA1\u7406\u5458\u8EAB\u4EFD\u8FD0\u884C");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING | (app.settings.showKeyBadges ? MF_CHECKED : 0), CMD_TOGGLE_BADGES, L"\u663E\u793A\u6309\u952E\u6C14\u6CE1 ([S] [W] [\u21E7])");
    AppendMenuW(menu, MF_STRING | (app.settings.showMouseBadges ? MF_CHECKED : 0), CMD_TOGGLE_MOUSE_BADGES, L"\u663E\u793A\u9F20\u6807\u70B9\u51FB\u6C14\u6CE1 ([\u5DE6\u952E] [\u53F3\u952E])");
    AppendMenuW(menu, MF_STRING | (app.settings.showKeyLegends ? MF_CHECKED : 0), CMD_TOGGLE_KEY_LEGENDS, L"\u663E\u793A\u952E\u76D8\u5B57\u7B26");
    AppendMenuW(menu, MF_STRING | (app.settings.footEasterEgg ? MF_CHECKED : 0), CMD_TOGGLE_FOOT, L"3\u952E\u540C\u65F6\u6309\u300C\u7528\u811A\u6309\u300D\u5F69\u86CB");
    AppendMenuW(menu, MF_STRING | (app.settings.petInteraction ? MF_CHECKED : 0), CMD_TOGGLE_PET, L"\u6478\u5934/\u6478\u80CC\u95ED\u773C\u4E92\u52A8");
    AppendMenuW(menu, MF_STRING | (app.settings.earbudAnimation ? MF_CHECKED : 0), CMD_TOGGLE_EARBUDS, L"连接耳机时戴上 AirPods \U0001F3A7");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // Cat Coat Skin submenu
    HMENU coatMenu = CreatePopupMenu();
    AppendMenuW(coatMenu, MF_STRING | (app.settings.coatTheme == CatCoatTheme::ClassicBlack ? MF_CHECKED : 0), CMD_COAT_BLACK, L"\u7ECF\u5178\u5C0F\u9ED1\u732B (\u539F\u7248)");
    AppendMenuW(coatMenu, MF_STRING | (app.settings.coatTheme == CatCoatTheme::Siamese ? MF_CHECKED : 0), CMD_COAT_SIAMESE, L"\u6316\u7164\u66B9\u7F57\u732B");
    AppendMenuW(coatMenu, MF_STRING | (app.settings.coatTheme == CatCoatTheme::Shorthair ? MF_CHECKED : 0), CMD_COAT_SHORTHAIR, L"美短（银虎斑加白）");
    AppendMenuW(coatMenu, MF_STRING | (app.settings.coatTheme == CatCoatTheme::OrangeWhite ? MF_CHECKED : 0), CMD_COAT_ORANGE_WHITE, L"橘白猫");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)coatMenu, L"\u732B\u54AA\u76AE\u80A4");

    // Keyboard Theme submenu
    HMENU kbMenu = CreatePopupMenu();
    AppendMenuW(kbMenu, MF_STRING | (app.settings.keyboardTheme == KeyboardTheme::ClassicMilk ? MF_CHECKED : 0), CMD_KB_MILK, L"\u7ECF\u5178\u5976\u767D\u6696\u9EC4 (\u539F\u7248)");
    AppendMenuW(kbMenu, MF_STRING | (app.settings.keyboardTheme == KeyboardTheme::MidnightHacker ? MF_CHECKED : 0), CMD_KB_HACKER, L"\u6697\u591C\u6781\u5BA2\u9ED1");
    AppendMenuW(kbMenu, MF_STRING | (app.settings.keyboardTheme == KeyboardTheme::RetroCream ? MF_CHECKED : 0), CMD_KB_RETRO, L"\u590D\u53E4\u5976\u6CB9\u7070");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)kbMenu, L"\u952E\u76D8\u914D\u8272");

    // Eye Color submenu
    HMENU eyeMenu = CreatePopupMenu();
    AppendMenuW(eyeMenu, MF_STRING | (app.settings.eyeTheme == EyeColorTheme::NeonChartreuse ? MF_CHECKED : 0), CMD_EYE_LIME, L"\u7ECF\u5178\u8367\u5149\u9EC4\u7EFF (\u539F\u7248)");
    AppendMenuW(eyeMenu, MF_STRING | (app.settings.eyeTheme == EyeColorTheme::SiameseBlue ? MF_CHECKED : 0), CMD_EYE_BLUE, L"\u66B9\u7F57\u5B9D\u77F3\u84DD");
    AppendMenuW(eyeMenu, MF_STRING | (app.settings.eyeTheme == EyeColorTheme::WarmAmber ? MF_CHECKED : 0), CMD_EYE_AMBER, L"\u7425\u73C0\u91D1\u77B3");
    AppendMenuW(eyeMenu, MF_STRING | (app.settings.eyeTheme == EyeColorTheme::IceCyan ? MF_CHECKED : 0), CMD_EYE_CYAN, L"\u51B0\u6676\u6D45\u84DD");
    AppendMenuW(eyeMenu, MF_STRING | (app.settings.eyeTheme == EyeColorTheme::SoftAmber ? MF_CHECKED : 0), CMD_EYE_SOFT_AMBER, L"柔和琥珀");
    AppendMenuW(eyeMenu, MF_STRING | (app.settings.eyeTheme == EyeColorTheme::ReferenceHazel ? MF_CHECKED : 0), CMD_EYE_REFERENCE_HAZEL, L"淡金青绿");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)eyeMenu, L"\u77B3\u8272\u5207\u6362");

    // Scale submenu
    HMENU scaleMenu = CreatePopupMenu();
    auto chk = [&](float v) -> UINT { return std::fabs(app.settings.scale - v) < 0.05f ? MF_CHECKED : 0; };
    AppendMenuW(scaleMenu, MF_STRING | chk(0.65f), CMD_SCALE_65,  L"\u8FF7\u4F60 (0.65x)");
    AppendMenuW(scaleMenu, MF_STRING | chk(0.85f), CMD_SCALE_85,  L"\u504F\u5C0F (0.85x)");
    AppendMenuW(scaleMenu, MF_STRING | chk(1.00f), CMD_SCALE_100, L"\u6807\u51C6 (1.0x)");
    AppendMenuW(scaleMenu, MF_STRING | chk(1.25f), CMD_SCALE_125, L"\u504F\u5927 (1.25x)");
    AppendMenuW(scaleMenu, MF_STRING | chk(1.50f), CMD_SCALE_150, L"\u8D85\u5927 (1.5x)");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)scaleMenu, L"\u5C3A\u5BF8\u5927\u5C0F");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    appendLinkMenu(app, menu);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CMD_DEMO_BURST, L"\u6F14\u793A\u6253\u5B57\u52A8\u753B");
    AppendMenuW(menu, MF_STRING, CMD_RESET_POS, L"\u91CD\u7F6E\u4F4D\u7F6E\u5230\u53F3\u4E0B\u89D2");
    if (!UPDATE_CHECKS_ENABLED) {
        std::wstring verTxt = std::wstring(L"版本 v") + APP_VERSION + L"（分支版，已关闭更新检查）";
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, verTxt.c_str());
    } else if (!app.availableUpdateVersion.empty()) {
        std::wstring upTxt = app.isUpdating
            ? (L"\u23F3 \u6B63\u5728\u4E0B\u8F7D\u66F4\u65B0 " + app.availableUpdateVersion + L"...")
            : (L"\U0001F195 \u53D1\u73B0\u65B0\u7248\u672C " + app.availableUpdateVersion + L" (\u70B9\u51FB\u81EA\u52A8\u66F4\u65B0)");
        AppendMenuW(menu, MF_STRING | (app.isUpdating ? MF_GRAYED : 0), CMD_OPEN_UPDATE, upTxt.c_str());
    } else {
        std::wstring chkTxt = std::wstring(L"\u68C0\u67E5\u66F4\u65B0 (\u5F53\u524D v") + APP_VERSION + L")";
        AppendMenuW(menu, MF_STRING, CMD_CHECK_UPDATE, chkTxt.c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CMD_QUIT, L"\u9000\u51FA KuroCat");

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
                    L"KuroCat \u5DF2\u5F00\u542F\u5B8C\u5168\u9F20\u6807\u7A7F\u900F",
                    L"\u53D6\u6D88\u65B9\u5F0F\uFF1A\u6309 Ctrl+Alt+Shift+P\uFF0C\u6216\u6309\u4F4F Alt \u952E\u53F3\u952E\u5C0F\u732B\uFF0C\u6216\u70B9\u53F3\u4E0B\u89D2\u6258\u76D8\u5C0F\u732B\u56FE\u6807\uFF0C\u6216\u518D\u6B21\u53CC\u51FB KuroCat.exe");
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
            handleLinkCommand(app, cmd);
            break;
        case CMD_TOGGLE_EARBUDS:
            app.settings.earbudAnimation = !app.settings.earbudAnimation;
            app.settings.saveToRegistry();
            break;
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
    float x = CatPhysics::SeatAnchor.x - w / 2.0f, y = 22.0f;
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
            bool clickThrough = (app.settings.fullClickThrough && !altHeld) || (zone == CatHitZone::None);
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
                        break;
                    case WTS_SESSION_UNLOCK:
                    case WTS_SESSION_LOGON:
                    case WTS_CONSOLE_CONNECT:
                    case WTS_REMOTE_CONNECT:
                        recoverAfterSystemEvent(*app);
                        break;
                    default:
                        break;
                }
            }
            return 0;

        case WM_POWERBROADCAST:
            if (app && hwnd == app->hwnd &&
                (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)) {
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

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;

        case WM_LBUTTONDOWN: {
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
                        L"KuroCat \u5DF2\u5F00\u542F\u5B8C\u5168\u9F20\u6807\u7A7F\u900F",
                        L"\u518D\u6B21\u6309 Ctrl+Alt+Shift+P \u6216\u6309\u4F4F Alt \u53F3\u952E\u5C0F\u732B\u5373\u53EF\u53D6\u6D88");
                } else {
                    showTrayBalloon(*app,
                        L"KuroCat \u5DF2\u53D6\u6D88\u5B8C\u5168\u9F20\u6807\u7A7F\u900F",
                        L"\u73B0\u5728\u53EF\u4EE5\u6B63\u5E38\u62D6\u52A8\u3001\u6478\u5934\u548C\u53F3\u952E\u6253\u5F00\u83DC\u5355\u4E86");
                }
            }
            return 0;

        case WM_WAKE_EXISTING:
            if (app) {
                // Re-launching KuroCat.exe unlocks click-through, restores visibility & opens settings menu!
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
                if (app && res->ok && res->hasUpdate) {
                    app->availableUpdateVersion = res->latestTag;
                    app->availableUpdateUrl = res->releaseUrl;
                    // Show WHICH exe is running: the usual cause of "I already updated but it still
                    // says there's a new version" is an old copy (Desktop / autostart) still running.
                    std::wstring msg = L"\u53D1\u73B0 KuroCat \u65B0\u7248\u672C " + res->latestTag
                        + L"\n\u5F53\u524D\u7248\u672C\uFF1Av" + APP_VERSION
                        + L"\n\u5F53\u524D\u8FD0\u884C\u7684\u7A0B\u5E8F\uFF1A" + currentExePath()
                        + L"\n\n\u5982\u679C\u4F60\u5DF2\u7ECF\u4E0B\u8F7D\u8FC7\u65B0\u7248\uFF0C\u76F4\u63A5\u53CC\u51FB\u65B0\u7248 exe \u5373\u53EF\u81EA\u52A8\u66FF\u6362\u6389\u8FD9\u4E2A\u65E7\u7248\u3002"
                        + L"\n\n\u662F\u5426\u7ACB\u5373\u5728\u8F6F\u4EF6\u5185\u81EA\u52A8\u4E0B\u8F7D\u5E76\u66F4\u65B0\u91CD\u542F\uFF1F";
                    if (MessageBoxW(nullptr, msg.c_str(), L"KuroCat \u66F4\u65B0\u63D0\u9192",
                                    MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST) == IDYES) {
                        startInAppUpdate(*app);
                    }
                } else if (res->manual) {
                    if (res->ok) {
                        std::wstring msg = std::wstring(L"\u5F53\u524D\u7248\u672C v") + APP_VERSION + L" \u5DF2\u662F\u6700\u65B0\u7248\u672C\u3002";
                        MessageBoxW(nullptr, msg.c_str(), L"KuroCat \u68C0\u67E5\u66F4\u65B0", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
                    } else {
                        MessageBoxW(nullptr, L"\u65E0\u6CD5\u8FDE\u63A5\u5230\u66F4\u65B0\u670D\u52A1\u5668\uFF0C\u8BF7\u7A0D\u540E\u91CD\u8BD5\u6216\u68C0\u67E5\u7F51\u7EDC\u3002",
                                    L"KuroCat \u68C0\u67E5\u66F4\u65B0", MB_OK | MB_ICONWARNING | MB_TOPMOST);
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
                                    L"\u81EA\u52A8\u4E0B\u8F7D\u6216\u66FF\u6362\u5B89\u88C5\u5305\u5931\u8D25\uFF0C\u662F\u5426\u6253\u5F00\u4E0B\u8F7D\u9875\u624B\u52A8\u4E0B\u8F7D\uFF1F",
                                    L"KuroCat \u81EA\u52A8\u66F4\u65B0", MB_YESNO | MB_ICONWARNING | MB_TOPMOST) == IDYES) {
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

/// Asks the already-running KuroCat to exit and waits for it. Builds <= 1.3.2 do not know
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

/// `KuroCat.exe --render-preview out.png`: drives the real physics + renderer through the fork's
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

    auto snap = [&](const CatPhysics& p, const CatSettings& st, const std::wstring& label, const std::wstring& tag = L"") {
        auto bmp = std::make_unique<Gdiplus::Bitmap>(fw, fh, PixelFormat32bppARGB);
        Gdiplus::Graphics g(bmp.get());
        g.Clear(Gdiplus::Color(255, 219, 222, 217));
        g.ScaleTransform(zoom, zoom);
        CatRenderer::draw(g, p, st);
        if (!tag.empty()) drawFriendLabel(g, tag, false);
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

/// `KuroCat.exe --link-selftest [pairing code]`: checks crypto + JSON against macOS test vectors,
/// runs two sessions against the real relay, and (with a code) talks to a macOS peer. Writes
/// kurocat-selftest.txt and returns the number of failures.
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
        if (fopen_s(&f, "kurocat-selftest.txt", "wb") == 0 && f) { fwrite(log.data(), 1, log.size(), f); fclose(f); }
    };

    // 1. macOS test vectors
    const char* macSealed = "000102030405060708090a0b747a35c17c550600bf4337219e280fba1b09bde8c07c69f0d5a020105be399dc9aadc0b2ce8ca7ddd70b979e98206899b7de82fe73775ce7ed35cac2d16eb0e391e26bd284463ea0d26810780dbf60a723170dcbd93c5c047151b013a55eaed275ccee534e1473a377515f05f86c4996c4c9ae9222b3d4c5f8884cf76df88454232ade5a16480e3ac90e539617bd057d0b547e85040b794fd2f2b6afe8b407d9b6a862409e98dee7f6f578fadf163c5b1ba6d960c93d5ffda21a0047c37870f137384dd1080b7e69e9df3cd52f00181cf8d41f464feb1e9a851cab4bbdbfe37150f2d4cfe712fc04451ced8aa3393b09e4a7d06d694e0db80c7d70536cb05388520eb643be7f99ef67854a6984173412f3be93a6d49e5fe360badc040894795beba8d5d43db1c5aed772e4ef444f098c5adbb44016ea1545";
    auto code = catlink::Code::parse(L"kc-c-2345-6789-abcd-efgh-jkmn");
    check(code.has_value(), "parse pairing code");
    if (code) {
        check(code->display() == L"KC-C-2345-6789-ABCD-EFGH-JKMN", "display format");
        check(code->topic() == "kurocat/v1/065e44db6944bbcbcd98b612413d9438", "topic matches macOS");
        catlink::Bytes key = code->key();
        check(catlink::toHex(key.data(), key.size()) == "6333cf40ccf75b347e348d54cbc4d1780b3f22ffeb6285170276c09c94ab565d", "AES key matches macOS");
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
        catlink::Session a, b;
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
        a.stop();
        pump(5, [&] { return b.status() == catlink::Status::FriendOffline; });
        check(b.status() == catlink::Status::FriendOffline, "friend leaving is noticed");
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

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    // Enable crisp Per-Monitor V2 DPI awareness on Windows 10/11
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

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

    const int myVersion = versionToNumber(APP_VERSION);
    const bool forceTakeover = commandLineHasFlag(L"--takeover");   // admin relaunch
    const bool noElevate = commandLineHasFlag(L"--no-elevate");
    const wchar_t* kMutexName = L"Local\\KuroCatDesktopPetSingleInstance";

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

    // Refresh auto-start exe path if user enabled auto-start and moved KuroCat.exe
    if (isAutoStartEnabled()) {
        setAutoStartEnabled(true);
    }

    app.input.onKeyDown = [&](PhysicalKey k) {
        app.physics.triggerKeyDown(k, app.settings);
        app.link.noteKeyDown(k, prefersViewerLeftPaw(k), g_passwordWatcher.isPassword.load());
    };
    app.input.onKeyUp = [&](PhysicalKey k) { app.link.noteKeyUp(k); };
    app.input.onMouseClick = [&](int btn, bool down) {
        app.physics.triggerMouseClick(btn, down, app.settings);
        app.link.noteClick(btn, down);
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
        L"KuroCatTray",
        WS_POPUP,
        0, 0, 0, 0,
        nullptr, nullptr, hInst, nullptr
    );
    if (app.menuOwnerHwnd) {
        SetPropW(app.menuOwnerHwnd, VERSION_PROP, (HANDLE)(INT_PTR)myVersion);
        // When elevated, UIPI would drop messages from the (non-elevated) taskbar and from a
        // re-launched KuroCat. Explicitly allow the few messages we rely on.
        const UINT allowed[] = { WM_TRAYICON, WM_WAKE_EXISTING, WM_REQUEST_QUIT };
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
        L"KuroCat",
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
