#pragma once
// Break reminder (same rules as macOS Sources/BreakReminder.swift): after you have worked for a while
// (menu: 30 min – 3 h), the cat waves and suggests a break. Local only: nothing goes over Cat Link.
//
//   work ──(worked longer than the setting)──▶ remind ──(a break)──▶ rest ──(any input)──▶ work
//
// A pause counts as a break when it is long enough for what the cat knows:
//   - 15 min without input, in any state (thinking can easily take a few minutes)
//   - 2 min without input after you click OK (input in the first 30 s is ignored: saving your work)
//   - 2 min with the screen locked or the PC asleep
// In remind, a reminder shows right away and then every 30 min: the cat waves for 7 s, and the bubble
// stays for 30 s with [OK] [Later], plus [I've rested] from the second reminder on.
// Tiny mouse movements are not input. Times come from GetTickCount64, which keeps counting during sleep.

#include <windows.h>
#include <gdiplus.h>
#include <optional>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include "CatPhysics.hpp"
#include "Localization.hpp"

namespace breaks {

/// Seconds; keeps running while the PC sleeps or hibernates.
inline double clock() { return (double)GetTickCount64() / 1000.0; }

enum class Phase { Work, Remind, Rest };
enum class Button { None, Ok, Later, Rested };

constexpr double NaturalBreak = 15 * 60;
constexpr double OkBreak = 2 * 60;
constexpr double OkPreparation = 30;
constexpr double AwayBreak = 2 * 60;
constexpr double RepeatEvery = 30 * 60;
constexpr double BubbleLife = 30;
constexpr double WaveDuration = 7;
constexpr double AckLife = 4;

struct Logic {
    double interval = 3600;   // seconds of work before the first reminder; 0 = off
    Phase phase = Phase::Work;
    double workStart = 0;
    double lastInput = 0;
    int remindersShown = 0;   // since this remind period began (the first one has no "I've rested")
    double lastReminderAt = 0;
    std::optional<double> okAt;          // OK was clicked: waiting to see whether a break follows
    std::optional<double> lockedSince;
    std::optional<double> asleepSince;
    std::optional<double> bubbleShownAt; // the reminder bubble on screen
    bool bubbleOffersRested = false;
    double bubbleWorked = 0;

    Logic() = default;
    Logic(double intervalSeconds, double now) : interval(intervalSeconds), workStart(now), lastInput(now) {}

    /// A new setting applies to the time already worked; reminding stops if that is now too short.
    void setInterval(double seconds, double now) {
        interval = seconds;
        if (phase == Phase::Remind && (seconds <= 0 || now - workStart < seconds)) {
            phase = Phase::Work;
            remindersShown = 0;
            bubbleShownAt.reset();
        }
    }

    bool isWaving(double now) const { return bubbleShownAt && now - *bubbleShownAt < WaveDuration; }
    double workedSeconds(double now) const { return phase == Phase::Rest ? 0 : std::max(0.0, now - workStart); }

    void noteInput(double now) {
        if (okAt && now - *okAt < OkPreparation) { lastInput = now; return; }
        bool tookBreak = phase == Phase::Rest || hasTakenBreak(now);
        lastInput = now;
        if (tookBreak) { startWork(now); return; }
        okAt.reset();   // busy again within 2 min of OK: not a break, keep reminding
    }

    void screenLocked(double now) { if (!lockedSince) lockedSince = now; }
    void screenUnlocked(double now) { tick(now); lockedSince.reset(); }
    void willSleep(double now) { if (!asleepSince) asleepSince = now; }
    void didWake(double now) { tick(now); asleepSince.reset(); }

    void tick(double now) {
        if (phase != Phase::Rest && hasTakenBreak(now)) {
            phase = Phase::Rest;
            okAt.reset();
            bubbleShownAt.reset();
            remindersShown = 0;
            return;
        }
        if (bubbleShownAt && now - *bubbleShownAt >= BubbleLife) bubbleShownAt.reset();
        if (interval <= 0 || lockedSince || asleepSince) return;
        if (phase == Phase::Work) {
            if (now - workStart >= interval) { phase = Phase::Remind; showReminder(now); }
        } else if (phase == Phase::Remind) {
            if (now - lastReminderAt >= RepeatEvery) showReminder(now);
        }
    }

    void tap(Button button, double now) {
        bubbleShownAt.reset();
        if (button == Button::Ok) { okAt = now; lastInput = now; }
        else if (button == Button::Rested) startWork(now);
    }

private:
    bool hasTakenBreak(double now) const {
        double idle = now - lastInput;
        if (idle >= NaturalBreak) return true;
        if (okAt && idle >= OkBreak) return true;
        std::optional<double> away = lockedSince;
        if (asleepSince && (!away || *asleepSince < *away)) away = asleepSince;
        return away && now - *away >= AwayBreak;
    }

    void startWork(double now) {
        phase = Phase::Work;
        workStart = now;
        okAt.reset();
        remindersShown = 0;
        bubbleShownAt.reset();
    }

    void showReminder(double now) {
        remindersShown += 1;
        lastReminderAt = now;
        bubbleShownAt = now;
        bubbleOffersRested = remindersShown > 1;
        bubbleWorked = now - workStart;
    }
};

/// "45 min", "1 h", "1 h 30 min" (menu).
inline std::wstring duration(double seconds) {
    int total = std::max(0, (int)(seconds / 60)), h = total / 60, m = total % 60;
    if (h == 0) return std::to_wstring(m) + tr(L" 分钟", L" min");
    std::wstring s = std::to_wstring(h) + tr(L" 小时", L" h");
    return m == 0 ? s : s + L" " + std::to_wstring(m) + tr(L" 分钟", L" min");
}

/// The same, spelled out for a sentence: "1 hour 30 min".
inline std::wstring spokenDuration(double seconds) {
    int total = std::max(0, (int)(seconds / 60)), h = total / 60, m = total % 60;
    if (h == 0) return std::to_wstring(m) + tr(L" 分钟", m == 1 ? L" minute" : L" minutes");
    std::wstring s = l10n::isChinese() ? std::to_wstring(h) + L" 小时" : h == 1 ? L"1 hour" : std::to_wstring(h) + L" hours";
    return m == 0 ? s : s + L" " + std::to_wstring(m) + tr(L" 分钟", L" min");
}

class Center {
public:
    static constexpr int Choices[] = { 0, 30, 60, 120, 180 };
    /// Input counts only once the pointer is this far from where it last counted (pixels).
    static constexpr float TinyMove = 12.0f;

    Logic logic;
    int minutes = 60;

    void begin(int savedMinutes, double now) {
        minutes = savedMinutes;
        logic = Logic(minutes * 60.0, now);
        GetCursorPos(&anchor_);
    }

    void setMinutes(int m, double now) {
        minutes = m;
        logic.setInterval(m * 60.0, now);
    }

    /// A key press, click or scroll on this PC.
    void noteInput(double now) { logic.noteInput(now); }

    /// Every frame: sample the pointer (ignoring tiny movements) and advance the state.
    void step(POINT cursor, double now) {
        if (std::hypot((float)(cursor.x - anchor_.x), (float)(cursor.y - anchor_.y)) >= TinyMove) {
            anchor_ = cursor;
            logic.noteInput(now);
        }
        logic.tick(now);
        if (preview_ && now - preview_->shownAt >= BubbleLife) preview_.reset();
        if (ackShownAt_ && now - *ackShownAt_ >= AckLife) ackShownAt_.reset();
    }

    bool isWaving(double now) const {
        if (preview_ && now - preview_->shownAt < WaveDuration) return true;
        return logic.isWaving(now);
    }

    /// While the reminder (or its "enjoy your break") is up, it takes the space above the cat.
    bool isShowingBubble() const { return current().has_value() || ackShownAt_.has_value(); }

    void showPreview(double now) {
        preview_ = Bubble{ now, minutes > 0 ? minutes * 60.0 : 3600.0, true, true };
        ackShownAt_.reset();
    }

    void tap(Button button, double now) {
        if (button == Button::None) return;
        if (preview_) preview_.reset();
        else logic.tap(button, now);
        if (button == Button::Ok) ackShownAt_ = now; else ackShownAt_.reset();
    }

    /// The button under a canvas point; `onBubble` is set when the point is anywhere on the bubble.
    Button hit(Pt canvas, bool* onBubble = nullptr) const {
        if (onBubble) *onBubble = false;
        auto b = current();
        if (!b) return Button::None;
        Layout l = layout(*b);
        for (auto& [button, r] : l.buttons) {
            if (canvas.x >= r.X - 2 && canvas.x <= r.X + r.Width + 2 && canvas.y >= r.Y - 3 && canvas.y <= r.Y + r.Height + 3) {
                if (onBubble) *onBubble = true;
                return button;
            }
        }
        const auto& r = l.rect;
        if (onBubble) *onBubble = canvas.x >= r.X - 2 && canvas.x <= r.X + r.Width + 2 && canvas.y >= r.Y - 2 && canvas.y <= r.Y + r.Height + 2;
        return Button::None;
    }

    void draw(Gdiplus::Graphics& g, double now) const {
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        if (auto b = current()) drawReminder(g, *b, now);
        else if (ackShownAt_) drawAck(g, now - *ackShownAt_);
    }

private:
    struct Bubble { double shownAt; double worked; bool offersRested; bool isPreview; };
    std::optional<Bubble> preview_;
    std::optional<double> ackShownAt_;   // "Enjoy your break" after OK
    POINT anchor_ = {};

    std::optional<Bubble> current() const {
        if (preview_) return preview_;
        if (!logic.bubbleShownAt) return std::nullopt;
        return Bubble{ *logic.bubbleShownAt, logic.bubbleWorked, logic.bubbleOffersRested, false };
    }

    static std::wstring title(double worked) {
        return l10n::isChinese() ? L"已经连续工作 " + spokenDuration(worked) + L"了，休息一下吧"
                                 : L"You've worked for " + spokenDuration(worked) + L". Time for a break";
    }

    static const wchar_t* label(Button b) {
        switch (b) {
            case Button::Ok: return tr(L"好的", L"OK");
            case Button::Later: return tr(L"等会儿", L"Later");
            case Button::Rested: return tr(L"我休息过了", L"I've rested");
            default: return L"";
        }
    }

    // Canvas layout (360 x 320); the bubble's bottom edge sits where the newest Claude Code bubble's does.
    static constexpr float BubbleBottom = 46.0f + 21.0f;
    static constexpr float ButtonHeight = 19.0f;
    static constexpr float CupWidth = 15.0f;

    struct Layout {
        Gdiplus::RectF rect;
        float titleX, titleY, titleSize, cupX;
        std::vector<std::pair<Button, Gdiplus::RectF>> buttons;
    };

    static const Gdiplus::FontFamily* family(Gdiplus::FontFamily& yahei) {
        return yahei.IsAvailable() ? &yahei : Gdiplus::FontFamily::GenericSansSerif();
    }

    static float measure(const std::wstring& text, float size) {
        Gdiplus::Bitmap bmp(1, 1, PixelFormat32bppARGB);
        Gdiplus::Graphics g(&bmp);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        Gdiplus::FontFamily yahei(L"Microsoft YaHei UI");
        Gdiplus::Font font(family(yahei), size, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::RectF box;
        g.MeasureString(text.c_str(), -1, &font, Gdiplus::PointF(0, 0), &box);
        return box.Width - 4.0f;
    }

    /// Measuring text with GDI+ is slow; the layout only changes with the bubble and the language.
    struct Cached { double shownAt; bool rested; double worked; bool chinese; Layout layout; };
    mutable std::optional<Cached> cache_;

    Layout layout(const Bubble& b) const {
        bool zh = l10n::isChinese();
        if (cache_ && cache_->shownAt == b.shownAt && cache_->rested == b.offersRested && cache_->worked == b.worked && cache_->chinese == zh) {
            return cache_->layout;
        }
        Layout l = computeLayout(b);
        cache_ = Cached{ b.shownAt, b.offersRested, b.worked, zh, l };
        return l;
    }

    Layout computeLayout(const Bubble& b) const {
        std::vector<Button> list = { Button::Ok, Button::Later };
        if (b.offersRested) list.push_back(Button::Rested);
        std::wstring t = title(b.worked);
        float room = CatPhysics::CanvasWidth - 8 - 24;
        float size = 12.0f;
        float titleW = measure(t, size) + CupWidth;
        if (titleW > room) { size *= room / titleW; titleW = room; }
        std::vector<float> widths;
        float buttonsW = 0;
        for (Button btn : list) { widths.push_back(measure(label(btn), 11.0f) + 20.0f); buttonsW += widths.back(); }
        buttonsW += 6.0f * (float)(widths.size() - 1);
        float w = std::min(CatPhysics::CanvasWidth - 8, std::max(titleW, buttonsW) + 24);
        float h = 8 + 16 + 6 + ButtonHeight + 7;
        float x = CatPhysics::SeatAnchor.x - w / 2;
        x = std::clamp(x, 4.0f, CatPhysics::CanvasWidth - 4 - w);
        Layout l;
        l.rect = Gdiplus::RectF(x, BubbleBottom - h, w, h);
        l.titleX = x + w / 2 - titleW / 2;
        l.titleY = l.rect.Y + 7;
        l.titleSize = size;
        l.cupX = l.titleX + measure(t, size) + 1;
        float bx = x + w / 2 - buttonsW / 2;
        for (size_t i = 0; i < list.size(); ++i) {
            l.buttons.push_back({ list[i], Gdiplus::RectF(bx, l.rect.Y + h - 7 - ButtonHeight, widths[i], ButtonHeight) });
            bx += widths[i] + 6.0f;
        }
        return l;
    }

    static void addRounded(Gdiplus::GraphicsPath& p, const Gdiplus::RectF& r, float radius) {
        float d = radius * 2;
        p.AddArc(r.X, r.Y, d, d, 180, 90);
        p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
        p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
        p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
        p.CloseFigure();
    }

    static float popScale(double age) {
        float t = (float)std::min(1.0, age / 0.35);
        return 0.6f + 0.4f * CatPhysics::easeOutBack(t);
    }

    /// A small coffee cup (GDI+ has no font fallback for ☕).
    static void drawCup(Gdiplus::Graphics& g, float x, float y, BYTE alpha) {
        Gdiplus::Color ink(alpha, 247, 247, 247);
        Gdiplus::SolidBrush brush(ink);
        Gdiplus::Pen pen(ink, 1.4f);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        Gdiplus::GraphicsPath cup;
        cup.AddLine(x, y + 6, x + 8, y + 6);
        cup.AddBezier(x + 8, y + 6, x + 8, y + 11, x + 6.5f, y + 12, x + 4, y + 12);
        cup.AddBezier(x + 4, y + 12, x + 1.5f, y + 12, x, y + 11, x, y + 6);
        g.FillPath(&brush, &cup);
        g.DrawArc(&pen, x + 6.6f, y + 7.0f, 3.6f, 3.4f, -90.0f, 180.0f);   // handle
        g.DrawLine(&pen, x - 1, y + 13.6f, x + 9, y + 13.6f);         // saucer
        g.DrawBezier(&pen, x + 2.5f, y + 4, x + 1.5f, y + 2.6f, x + 3.5f, y + 1.8f, x + 2.5f, y + 0.2f);   // steam
        g.DrawBezier(&pen, x + 5.5f, y + 4, x + 4.5f, y + 2.6f, x + 6.5f, y + 1.8f, x + 5.5f, y + 0.2f);
    }

    static void fillBubble(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, float a) {
        Gdiplus::GraphicsPath shadow;
        addRounded(shadow, Gdiplus::RectF(r.X, r.Y + 1.5f, r.Width, r.Height), radius);
        Gdiplus::SolidBrush shadowBrush(Gdiplus::Color((BYTE)(a * 0.22f * 255), 0, 0, 0));
        g.FillPath(&shadowBrush, &shadow);
        Gdiplus::GraphicsPath body;
        addRounded(body, r, radius);
        Gdiplus::SolidBrush fill(Gdiplus::Color((BYTE)(a * 0.92f * 255), 28, 28, 28));
        g.FillPath(&fill, &body);
        Gdiplus::PointF tail[3] = { { r.X + r.Width / 2 - 5, r.Y + r.Height - 0.5f },
                                    { r.X + r.Width / 2 + 5, r.Y + r.Height - 0.5f },
                                    { r.X + r.Width / 2 + 1, r.Y + r.Height + 6 } };
        g.FillPolygon(&fill, tail, 3);
    }

    void drawReminder(Gdiplus::Graphics& g, const Bubble& b, double now) const {
        Layout l = layout(b);
        const auto& r = l.rect;
        double age = now - b.shownAt;
        float a = std::clamp(std::min((float)(age / 0.25), (float)((BubbleLife - age) / 1.5)), 0.0f, 1.0f);
        BYTE al = (BYTE)(a * 255);
        Gdiplus::GraphicsState state = g.Save();
        float pop = popScale(age);
        g.TranslateTransform(r.X + r.Width / 2, r.Y + r.Height);
        g.ScaleTransform(pop, pop);
        g.TranslateTransform(-(r.X + r.Width / 2), -(r.Y + r.Height));
        fillBubble(g, r, 11.0f, a);

        Gdiplus::FontFamily yahei(L"Microsoft YaHei UI");
        Gdiplus::Font titleFont(family(yahei), l.titleSize, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush ink(Gdiplus::Color(al, 247, 247, 247));
        std::wstring t = title(b.worked);
        g.DrawString(t.c_str(), -1, &titleFont, Gdiplus::PointF(l.titleX - 2, l.titleY), &ink);
        drawCup(g, l.cupX, l.titleY + 1.5f, al);

        Gdiplus::Font buttonFont(family(yahei), 11.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        for (auto& [button, br] : l.buttons) {
            bool ok = button == Button::Ok;
            Gdiplus::GraphicsPath pill;
            addRounded(pill, br, br.Height / 2);
            Gdiplus::SolidBrush fill(ok ? Gdiplus::Color(al, 158, 242, 115) : Gdiplus::Color((BYTE)(a * 0.16f * 255), 255, 255, 255));
            g.FillPath(&fill, &pill);
            Gdiplus::SolidBrush text(ok ? Gdiplus::Color(al, 20, 56, 10) : Gdiplus::Color(al, 242, 242, 242));
            Gdiplus::RectF box;
            g.MeasureString(label(button), -1, &buttonFont, Gdiplus::PointF(0, 0), &box);
            g.DrawString(label(button), -1, &buttonFont,
                         Gdiplus::PointF(br.X + (br.Width - box.Width) / 2, br.Y + (br.Height - box.Height) / 2 + 0.5f), &text);
        }
        g.Restore(state);
    }

    void drawAck(Gdiplus::Graphics& g, double age) const {
        std::wstring text = tr(L"好好休息一下", L"Enjoy your break");
        float w = measure(text, 11.5f) + 22 + CupWidth;
        Gdiplus::RectF r(CatPhysics::SeatAnchor.x - w / 2, 46.0f, w, 21.0f);
        float a = std::clamp(std::min((float)(age / 0.2), (float)((AckLife - age) / 0.8)), 0.0f, 1.0f);
        BYTE al = (BYTE)(a * 255);
        Gdiplus::GraphicsState state = g.Save();
        float pop = popScale(age);
        g.TranslateTransform(r.X + r.Width / 2, r.Y + r.Height / 2);
        g.ScaleTransform(pop, pop);
        g.TranslateTransform(-(r.X + r.Width / 2), -(r.Y + r.Height / 2));
        fillBubble(g, r, r.Height / 2, a);
        Gdiplus::FontFamily yahei(L"Microsoft YaHei UI");
        Gdiplus::Font font(family(yahei), 11.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush ink(Gdiplus::Color(al, 247, 247, 247));
        Gdiplus::RectF box;
        g.MeasureString(text.c_str(), -1, &font, Gdiplus::PointF(0, 0), &box);
        g.DrawString(text.c_str(), -1, &font, Gdiplus::PointF(r.X + 9, r.Y + (r.Height - box.Height) / 2 + 0.5f), &ink);
        drawCup(g, r.X + 11 + measure(text, 11.5f) + 3, r.Y + 3.5f, al);
        g.Restore(state);
    }
};

}  // namespace breaks
