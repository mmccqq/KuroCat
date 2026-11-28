#pragma once
// Claude Code alerts, same behaviour as macOS (Sources/ClaudeCodeAlerts.swift).
//
// Claude Code hooks run `Whisku.exe --claude-hook`. That short-lived process reads the hook's JSON
// from stdin and hands the running Whisku a tiny WM_COPYDATA message; the cat then shows a bubble:
// "done" when a session finishes (Stop), "needs you" when Claude waits for an approval
// (Notification / PermissionRequest). Everything stays on this computer: nothing is sent over Cat Link.

#include <windows.h>
#include <tlhelp32.h>
#include <gdiplus.h>
#include <optional>
#include <string>
#include <vector>
#include <cwctype>
#include <algorithm>
#include <cmath>
#include "CatLink.hpp"
#include "CatPhysics.hpp"

namespace claude {

constexpr ULONG_PTR COPYDATA_ID = 0x4B43434C;   // 'KCCL'

enum class Kind { Done, NeedsYou };

struct HookEvent {
    Kind kind = Kind::Done;
    std::wstring project;
    std::wstring session;
};

/// Hook JSON -> event, or nothing for events the cat ignores.
inline std::optional<HookEvent> parseHook(const std::string& json) {
    auto v = catlink::JsonReader::parse(json);
    if (!v || v->type != catlink::JsonReader::Value::Object) return std::nullopt;
    auto str = [&](const char* k) -> std::string {
        const auto* x = v->get(k);
        return x && x->type == catlink::JsonReader::Value::String ? x->str : std::string();
    };
    std::string event = str("hook_event_name");
    HookEvent e;
    if (event == "Stop") {
        e.kind = Kind::Done;
    } else if (event == "PermissionRequest") {
        e.kind = Kind::NeedsYou;
    } else if (event == "Notification") {
        std::string type = str("notification_type"), msg = str("message");
        for (auto& c : msg) c = (char)tolower((unsigned char)c);
        // "Waiting for your input" repeats a finished turn; auth notices aren't requests.
        if (type == "idle_prompt" || type == "auth_success" || msg.find("waiting for your input") != std::string::npos) return std::nullopt;
        e.kind = Kind::NeedsYou;
    } else {
        return std::nullopt;
    }
    std::wstring cwd = catlink::widen(str("cwd"));
    while (!cwd.empty() && (cwd.back() == L'\\' || cwd.back() == L'/')) cwd.pop_back();
    size_t slash = cwd.find_last_of(L"\\/");
    std::wstring folder = slash == std::wstring::npos ? cwd : cwd.substr(slash + 1);
    if (folder.empty() || (folder.size() == 2 && folder[1] == L':')) folder = L"Claude Code";
    e.project = folder;
    e.session = catlink::widen(str("session_id"));
    return e;
}

/// The window the session runs in (VS Code, Windows Terminal, ...): the first ancestor process of
/// this hook process that owns a visible top-level window.
inline HWND findSessionWindow() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return nullptr;
    std::vector<std::pair<DWORD, DWORD>> procs;   // pid, parent pid
    PROCESSENTRY32W pe = { sizeof(pe) };
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) procs.push_back({ pe.th32ProcessID, pe.th32ParentProcessID });
    CloseHandle(snap);

    struct Find { DWORD pid; HWND hwnd; } find = { 0, nullptr };
    DWORD pid = GetCurrentProcessId();
    for (int depth = 0; depth < 12; ++depth) {
        DWORD parent = 0;
        for (auto& p : procs) if (p.first == pid) { parent = p.second; break; }
        if (!parent || parent == pid) break;
        pid = parent;
        find = { pid, nullptr };
        EnumWindows([](HWND w, LPARAM lp) -> BOOL {
            auto* f = reinterpret_cast<Find*>(lp);
            DWORD owner = 0;
            GetWindowThreadProcessId(w, &owner);
            if (owner == f->pid && IsWindowVisible(w) && !GetWindow(w, GW_OWNER) && GetWindowTextLengthW(w) > 0) {
                f->hwnd = w;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&find));
        if (find.hwnd) return find.hwnd;
    }
    return nullptr;
}

/// `Whisku.exe --claude-hook`: read the hook input, tell the running Whisku, exit 0 (never block
/// Claude Code). From a terminal, `--claude-hook done` / `--claude-hook needs` sends a test event.
inline int runHook(const wchar_t* ownerClass, const std::wstring& manualKind) {
    std::string json;
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    if (in && in != INVALID_HANDLE_VALUE && GetFileType(in) != FILE_TYPE_CHAR && GetFileType(in) != FILE_TYPE_UNKNOWN) {
        char buf[4096];
        DWORD got = 0;
        while (json.size() < (1u << 20) && ReadFile(in, buf, sizeof(buf), &got, nullptr) && got > 0) json.append(buf, got);
    }
    if (json.empty() && !manualKind.empty()) {
        json = manualKind == L"needs" ? R"({"hook_event_name":"PermissionRequest","cwd":"manual-test","session_id":"manual-test"})"
                                      : R"({"hook_event_name":"Stop","cwd":"manual-test","session_id":"manual-test"})";
    }
    auto e = parseHook(json);
    if (!e) return 0;
    HWND owner = FindWindowW(ownerClass, nullptr);
    if (!owner) return 0;
    HWND target = findSessionWindow();
    std::wstring payload = std::wstring(e->kind == Kind::Done ? L"done" : L"needs") + L"\n" + e->project + L"\n" + e->session
                         + L"\n" + std::to_wstring((unsigned long long)(uintptr_t)target);
    COPYDATASTRUCT cds = {};
    cds.dwData = COPYDATA_ID;
    cds.cbData = (DWORD)((payload.size() + 1) * sizeof(wchar_t));
    cds.lpData = (void*)payload.c_str();
    DWORD_PTR result = 0;
    SendMessageTimeoutW(owner, WM_COPYDATA, 0, (LPARAM)&cds, SMTO_ABORTIFHUNG, 2000, &result);
    return 0;
}

struct Alert {
    int id = 0;
    Kind kind = Kind::Done;
    std::wstring project, session;
    HWND app = nullptr;
    double start = 0;
    float width = 0;   // measured once
    float y = 46.0f;   // shown top edge (canvas y); slides toward its slot when bubbles come and go

    double lifetime() const { return kind == Kind::Done ? 45.0 : 600.0; }
    std::wstring text() const {
        std::wstring name = project.size() > 18 ? project.substr(0, 17) + L"…" : project;
        return kind == Kind::Done ? name + tr(L" 完成了", L" done") : name + tr(L" 等你确认", L" needs you");
    }
};

class Center {
public:
    std::vector<Alert> alerts;
    bool enabled = true;
    bool sound = true;

    bool needsAttention() const {
        for (auto& a : alerts) if (a.kind == Kind::NeedsYou) return true;
        return false;
    }

    /// Returns the new alert, or nullptr if it was dropped (disabled or a duplicate).
    const Alert* receive(Kind kind, const std::wstring& project, const std::wstring& session, HWND app, double now) {
        if (!enabled) return nullptr;
        // Notification and PermissionRequest often both fire for one approval.
        if (kind == Kind::NeedsYou) {
            for (auto& a : alerts) if (a.session == session && a.kind == Kind::NeedsYou && now - a.start < 3.0) return nullptr;
        }
        // A newer event for a session replaces its older bubble.
        if (!session.empty()) {
            alerts.erase(std::remove_if(alerts.begin(), alerts.end(), [&](const Alert& a) { return a.session == session; }), alerts.end());
        }
        Alert a;
        a.id = nextId_++;
        a.kind = kind;
        a.project = project.empty() ? L"Claude Code" : project;
        a.session = session;
        a.app = app;
        a.start = now;
        a.y = NewestBubbleY;
        a.width = measure(a.text()) + 22.0f + 13.0f;
        alerts.push_back(a);
        if (alerts.size() > 3) alerts.erase(alerts.begin(), alerts.end() - 3);
        return &alerts.back();
    }

    /// The `done\nproject\nsession\nhwnd` message from the hook process.
    const Alert* receivePayload(const std::wstring& payload, double now) {
        std::vector<std::wstring> parts;
        size_t pos = 0;
        while (parts.size() < 4) {
            size_t nl = payload.find(L'\n', pos);
            parts.push_back(payload.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos));
            if (nl == std::wstring::npos) break;
            pos = nl + 1;
        }
        if (parts.size() < 3 || (parts[0] != L"done" && parts[0] != L"needs")) return nullptr;
        HWND app = parts.size() > 3 ? (HWND)(uintptr_t)wcstoull(parts[3].c_str(), nullptr, 10) : nullptr;
        return receive(parts[0] == L"done" ? Kind::Done : Kind::NeedsYou, parts[1].substr(0, 200), parts[2].substr(0, 200), app, now);
    }

    /// The newest bubble sits just above the ears; older ones stack upward (same as macOS).
    static constexpr float NewestBubbleY = 46.0f;
    static constexpr float BubbleSpacing = 23.0f;

    void step(double now) {
        alerts.erase(std::remove_if(alerts.begin(), alerts.end(), [&](const Alert& a) { return now - a.start > a.lifetime(); }), alerts.end());
        float dt = (float)std::clamp(now - lastStep_, 0.0, 0.1);
        lastStep_ = now;
        float follow = std::min(1.0f, dt * 11.0f);
        for (size_t i = 0; i < alerts.size(); ++i) {
            float slot = NewestBubbleY - (float)(alerts.size() - 1 - i) * BubbleSpacing;
            alerts[i].y += (slot - alerts[i].y) * follow;
        }
    }

    /// Clicking a bubble closes it and brings back the window the session runs in.
    void activate(int id) {
        for (size_t i = 0; i < alerts.size(); ++i) {
            if (alerts[i].id != id) continue;
            HWND w = alerts[i].app;
            alerts.erase(alerts.begin() + i);
            if (w && IsWindow(w)) {
                if (IsIconic(w)) ShowWindow(w, SW_RESTORE);
                SetForegroundWindow(w);
            }
            return;
        }
    }

    struct Placed { const Alert* alert; Gdiplus::RectF rect; float alpha; };

    /// Bubbles stack above the cat's head (canvas coordinates); the newest is nearest the cat (bottom).
    std::vector<Placed> layout(double now) const {
        std::vector<Placed> out;
        for (auto& a : alerts) {
            Gdiplus::RectF r(CatPhysics::SeatAnchor.x - a.width / 2.0f, a.y, a.width, 21.0f);
            double age = now - a.start;
            float fadeIn = (float)std::min(1.0, age / 0.25), fadeOut = (float)std::min(1.0, (a.lifetime() - age) / 4.0);
            out.push_back({ &a, r, std::clamp(std::min(fadeIn, fadeOut), 0.0f, 1.0f) });
        }
        return out;
    }

    int hit(Pt canvas, double now) const {
        auto items = layout(now);
        for (auto it = items.rbegin(); it != items.rend(); ++it) {
            Gdiplus::RectF r = it->rect;
            if (canvas.x >= r.X - 3 && canvas.x <= r.X + r.Width + 3 && canvas.y >= r.Y - 3 && canvas.y <= r.Y + r.Height + 3) return it->alert->id;
        }
        return 0;
    }

    void draw(Gdiplus::Graphics& g, double now) const {
        auto items = layout(now);
        if (items.empty()) return;
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        Gdiplus::FontFamily yahei(L"Microsoft YaHei UI");
        const Gdiplus::FontFamily* family = yahei.IsAvailable() ? &yahei : Gdiplus::FontFamily::GenericSansSerif();
        Gdiplus::Font font(family, 11.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        for (size_t i = 0; i < items.size(); ++i) {
            const Alert& a = *items[i].alert;
            Gdiplus::RectF r = items[i].rect;
            bool needs = a.kind == Kind::NeedsYou;
            BYTE al = (BYTE)(items[i].alpha * 255.0f);
            auto alpha = [&](float f) { return (BYTE)(items[i].alpha * f * 255.0f); };

            Gdiplus::GraphicsState state = g.Save();
            float pop = popScale(now - a.start);
            g.TranslateTransform(r.X + r.Width / 2, r.Y + r.Height / 2);
            g.ScaleTransform(pop, pop);
            g.TranslateTransform(-(r.X + r.Width / 2), -(r.Y + r.Height / 2));

            Gdiplus::GraphicsPath pill;
            addPill(pill, r);
            Gdiplus::GraphicsPath shadow;
            addPill(shadow, Gdiplus::RectF(r.X, r.Y + 1.5f, r.Width, r.Height));
            Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(alpha(0.22f), 0, 0, 0));
            g.FillPath(&shadowBrush, &shadow);
            Gdiplus::SolidBrush fill(needs ? Gdiplus::Color(alpha(0.96f), 255, 168, 41) : Gdiplus::Color(alpha(0.9f), 28, 28, 28));
            g.FillPath(&fill, &pill);
            if (i + 1 == items.size()) {   // speech-bubble tail toward the cat
                Gdiplus::PointF tail[3] = { { r.X + r.Width / 2 - 5, r.Y + r.Height - 0.5f },
                                            { r.X + r.Width / 2 + 5, r.Y + r.Height - 0.5f },
                                            { r.X + r.Width / 2 + 1, r.Y + r.Height + 6 } };
                g.FillPolygon(&fill, tail, 3);
            }

            float textX = r.X + 11;
            if (needs) {   // pulsing dot: "waiting for you"
                float pulse = 0.55f + 0.45f * (float)(std::sin((now - a.start) * 5.0) * 0.5 + 0.5);
                Gdiplus::SolidBrush dot(Gdiplus::Color(alpha(pulse), 140, 31, 5));
                g.FillEllipse(&dot, textX, r.Y + r.Height / 2 - 3.5f, 7.0f, 7.0f);
            } else {       // check mark (drawn: GDI+ has no font fallback for ✓)
                Gdiplus::Pen pen(Gdiplus::Color(al, 158, 242, 115), 1.8f);
                pen.SetStartCap(Gdiplus::LineCapRound);
                pen.SetEndCap(Gdiplus::LineCapRound);
                pen.SetLineJoin(Gdiplus::LineJoinRound);
                Gdiplus::PointF check[3] = { { textX + 0.5f, r.Y + 10.5f }, { textX + 3.2f, r.Y + 13.5f }, { textX + 8.0f, r.Y + 7.0f } };
                g.DrawLines(&pen, check, 3);
            }
            textX += 13;
            Gdiplus::SolidBrush ink(needs ? Gdiplus::Color(al, 64, 26, 0) : Gdiplus::Color(al, 158, 242, 115));
            Gdiplus::RectF box;
            std::wstring text = a.text();
            g.MeasureString(text.c_str(), -1, &font, Gdiplus::PointF(0, 0), &box);
            g.DrawString(text.c_str(), -1, &font, Gdiplus::PointF(textX - 2, r.Y + (r.Height - box.Height) / 2 + 0.5f), &ink);
            g.Restore(state);
        }
    }

private:
    int nextId_ = 1;
    double lastStep_ = 0.0;

    static float popScale(double age) {
        float t = (float)std::min(1.0, age / 0.35);
        return 0.6f + 0.4f * CatPhysics::easeOutBack(t);
    }

    static void addPill(Gdiplus::GraphicsPath& p, const Gdiplus::RectF& r) {
        float d = r.Height;
        p.AddArc(r.X, r.Y, d, d, 90, 180);
        p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 180);
        p.CloseFigure();
    }

    static float measure(const std::wstring& text) {
        Gdiplus::Bitmap bmp(1, 1, PixelFormat32bppARGB);
        Gdiplus::Graphics g(&bmp);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        Gdiplus::FontFamily yahei(L"Microsoft YaHei UI");
        const Gdiplus::FontFamily* family = yahei.IsAvailable() ? &yahei : Gdiplus::FontFamily::GenericSansSerif();
        Gdiplus::Font font(family, 11.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::RectF box;
        g.MeasureString(text.c_str(), -1, &font, Gdiplus::PointF(0, 0), &box);
        return box.Width - 4.0f;
    }
};

/// The `hooks` block for ~/.claude/settings.json (exec form: no shell quoting on Windows).
inline std::wstring settingsSnippet(const std::wstring& exePath) {
    std::wstring path;
    for (wchar_t c : exePath) {
        if (c == L'\\') path += L'/';
        else if (c == L'"') path += L"\\\"";
        else path += c;
    }
    std::wstring entry = L"[{ \"hooks\": [{ \"type\": \"command\", \"command\": \"" + path +
                         L"\", \"args\": [\"--claude-hook\"], \"async\": true, \"timeout\": 5 }] }]";
    return L"{\r\n  \"hooks\": {\r\n    \"Stop\": " + entry + L",\r\n    \"Notification\": " + entry +
           L",\r\n    \"PermissionRequest\": " + entry + L"\r\n  }\r\n}\r\n";
}

/// `settings` (Claude Code's settings.json, UTF-8) with every hook that starts `oldExe` pointed at `newExe`
/// (both UTF-8 Windows paths). Paths are found as settingsSnippet writes them (forward slashes) or with
/// JSON-escaped backslashes, ignoring case, and replaced in the same form.
inline std::string pointHooks(const std::string& settings, const std::string& oldExe, const std::string& newExe) {
    auto form = [](const std::string& p, bool slashes) {
        std::string out;
        for (char c : p) {
            if (c == '\\') out += slashes ? "/" : "\\\\";
            else out += c;
        }
        return out;
    };
    auto lower = [](std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); return s; };
    std::string result = settings;
    for (bool slashes : { true, false }) {
        std::string from = form(oldExe, slashes), to = form(newExe, slashes);
        std::string hay = lower(result), needle = lower(from);
        std::string out;
        size_t pos = 0;
        for (size_t hit; !needle.empty() && (hit = hay.find(needle, pos)) != std::string::npos; pos = hit + needle.size())
            out += result.substr(pos, hit - pos) + to;
        result = out + result.substr(pos);
    }
    return result;
}

}  // namespace claude
