#pragma once
#define NOMINMAX
#include <windows.h>
#include <cmath>
#include <vector>
#include <unordered_map>
#include <random>
#include <algorithm>
#include "KeyMap.hpp"
#include "InputEngine.hpp"

enum class CatCoatTheme : int {
    ClassicBlack = 0,
    Siamese = 1,
    Shorthair = 2,
    OrangeWhite = 3
};

enum class KeyboardTheme : int {
    ClassicMilk = 0,
    MidnightHacker = 1,
    RetroCream = 2
};

enum class EyeColorTheme : int {
    NeonChartreuse = 0,
    SiameseBlue = 1,
    WarmAmber = 2,
    IceCyan = 3,
    SoftAmber = 4,
    ReferenceHazel = 5
};

struct CatSettings {
    bool showKeyBadges = true;
    bool showMouseBadges = true;
    bool showKeyLegends = true;
    bool footEasterEgg = true;
    bool petInteraction = true;
    bool fullClickThrough = false;
    bool alwaysOnTop = true;
    bool runAsAdmin = false; // relaunch elevated so keys typed into admin-level games are visible (UIPI)
    bool earbudAnimation = true; // put on AirPods when the sound output switches to headphones
    float scale = 1.0f;
    CatCoatTheme coatTheme = CatCoatTheme::ClassicBlack;
    KeyboardTheme keyboardTheme = KeyboardTheme::ClassicMilk;
    EyeColorTheme eyeTheme = EyeColorTheme::NeonChartreuse;
    int posX = -99999;
    int posY = -99999;

    void loadFromRegistry() {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\KuroCat", 0, KEY_READ, &hKey) != ERROR_SUCCESS) return;
        auto readDword = [&](const wchar_t* name, DWORD defVal) -> DWORD {
            DWORD val = defVal, sz = sizeof(val);
            if (RegQueryValueExW(hKey, name, nullptr, nullptr, (LPBYTE)&val, &sz) == ERROR_SUCCESS) return val;
            return defVal;
        };
        showKeyBadges    = readDword(L"ShowKeyBadges", 1) != 0;
        showMouseBadges  = readDword(L"ShowMouseBadges", 1) != 0;
        showKeyLegends   = readDword(L"ShowKeyLegends", 1) != 0;
        footEasterEgg    = readDword(L"FootEasterEgg", 1) != 0;
        petInteraction   = readDword(L"PetInteraction", 1) != 0;
        fullClickThrough = readDword(L"FullClickThrough", 0) != 0;
        alwaysOnTop      = readDword(L"AlwaysOnTop", 1) != 0;
        runAsAdmin       = readDword(L"RunAsAdmin", 0) != 0;
        earbudAnimation  = readDword(L"EarbudAnimation", 1) != 0;
        scale            = std::clamp((float)readDword(L"ScalePct", 100) / 100.0f, 0.5f, 2.0f);
        coatTheme        = (CatCoatTheme)std::clamp((int)readDword(L"CoatTheme", 0), 0, 3);
        keyboardTheme    = (KeyboardTheme)std::clamp((int)readDword(L"KeyboardTheme", 0), 0, 2);
        eyeTheme         = (EyeColorTheme)std::clamp((int)readDword(L"EyeTheme", 0), 0, 5);
        DWORD hasPos     = readDword(L"HasPos", 0);
        if (hasPos) {
            posX = (int)readDword(L"PosX", 0);
            posY = (int)readDword(L"PosY", 0);
        }
        RegCloseKey(hKey);
    }

    void saveToRegistry() const {
        HKEY hKey = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\KuroCat", 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) return;
        auto writeDword = [&](const wchar_t* name, DWORD val) {
            RegSetValueExW(hKey, name, 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        };
        writeDword(L"ShowKeyBadges", showKeyBadges ? 1 : 0);
        writeDword(L"ShowMouseBadges", showMouseBadges ? 1 : 0);
        writeDword(L"ShowKeyLegends", showKeyLegends ? 1 : 0);
        writeDword(L"FootEasterEgg", footEasterEgg ? 1 : 0);
        writeDword(L"PetInteraction", petInteraction ? 1 : 0);
        writeDword(L"FullClickThrough", fullClickThrough ? 1 : 0);
        writeDword(L"AlwaysOnTop", alwaysOnTop ? 1 : 0);
        writeDword(L"RunAsAdmin", runAsAdmin ? 1 : 0);
        writeDword(L"EarbudAnimation", earbudAnimation ? 1 : 0);
        writeDword(L"ScalePct", (DWORD)(scale * 100.0f + 0.5f));
        writeDword(L"CoatTheme", (DWORD)coatTheme);
        writeDword(L"KeyboardTheme", (DWORD)keyboardTheme);
        writeDword(L"EyeTheme", (DWORD)eyeTheme);
        if (posX != -99999 && posY != -99999) {
            writeDword(L"HasPos", 1);
            writeDword(L"PosX", (DWORD)posX);
            writeDword(L"PosY", (DWORD)posY);
        }
        RegCloseKey(hKey);
    }
};

struct Spring1D {
    float value = 0.0f;
    float velocity = 0.0f;
    float target = 0.0f;

    explicit Spring1D(float v = 0.0f) : value(v), velocity(0.0f), target(v) {}

    void step(float dt, float stiffness = 260.0f, float damping = 22.0f) {
        float force = (target - value) * stiffness;
        float damp = velocity * damping;
        velocity += (force - damp) * dt;
        value += velocity * dt;
    }
};

struct Spring2D {
    Spring1D x;
    Spring1D y;

    explicit Spring2D(Pt p = {0.0f, 0.0f}) : x(p.x), y(p.y) {}
    Pt point() const { return { x.value, y.value }; }
    void setTarget(Pt p) { x.target = p.x; y.target = p.y; }
    void step(float dt, float stiffness = 340.0f, float damping = 25.0f) {
        x.step(dt, stiffness, damping);
        y.step(dt, stiffness, damping);
    }
};

struct FloatingKeycap {
    std::wstring label;
    Pt position;
    Pt velocity;
    float age = 0.0f;
    float lifetime = 1.25f;
    float rotation = 0.0f;
};

struct PurrSparkle {
    Pt position;
    Pt velocity;
    float age = 0.0f;
    float lifetime = 0.9f;
    bool isSleepZ = false;
    std::wstring note;   // drawn as text when set: ♪ ♫ while wearing AirPods, ♥ when a friend pets this cat
};

class CatPhysics {
public:
    static constexpr float CanvasWidth  = 360.0f;
    static constexpr float CanvasHeight = 320.0f;
    static constexpr Pt SeatAnchor      = { 184.0f, 226.0f };
    static constexpr Pt LeftPawRest     = { 164.0f, 235.0f };
    static constexpr Pt RightPawRest    = { 204.0f, 235.0f };
    static constexpr Pt HindFootRest    = { 214.0f, 234.0f };
    static constexpr Pt MouseBaseCenter = { 78.0f, 258.0f };

    Spring2D leftPaw{LeftPawRest};
    Spring2D rightPaw{RightPawRest};
    Spring2D hindFoot{HindFootRest};
    Spring1D hindFootActive{0.0f};

    Spring1D bodyShiftX{0.0f};
    Spring1D bodyTilt{0.0f};
    Spring1D bodySquash{1.0f};
    Spring1D headTilt{0.0f};
    Spring1D headYaw{0.0f};
    Spring1D headPitch{0.0f};
    Spring1D headOffsetY{0.0f};
    Spring1D earFold{0.0f};
    Spring1D earTwitch{0.0f};

    Spring2D eyeGaze{{0.0f, 0.0f}};
    Spring1D pupilScale{1.0f};
    Spring1D huntIntensity{0.0f};
    Spring2D mouseOffset{{0.0f, 0.0f}};
    Spring1D mousePressLeft{0.0f};
    Spring1D mousePressRight{0.0f};

    bool leftPawOnMouse = false;
    bool leftPawTyping = false;
    bool rightPawTyping = false;

    std::unordered_map<PhysicalKey, float> keyGlow;
    std::vector<FloatingKeycap> floatingKeycaps;
    std::vector<PurrSparkle> sparkles;

    float time = 0.0f;
    float tailPhase = 0.0f;

    float petHeadIntensity = 0.0f;
    float petBodyIntensity = 0.0f;
    int totalPetSessions = 0;
    float sleepiness = 0.0f;

    float effectiveEyeClosure() const {
        float petClose = std::max(petHeadIntensity, petBodyIntensity * 0.62f);
        float sleepClose = sleepiness * 0.92f;
        return std::min(1.0f, std::max(blinkProgress_, std::max(petClose, sleepClose)));
    }

    bool isHappyClosedEyes() const { return petHeadIntensity > 0.45f; }
    bool isBlissfulSquint() const { return petBodyIntensity > 0.35f && !isHappyClosedEyes(); }

    // Petting must be deliberate: ~1 s of stroking (and some distance) before the cat reacts.
    static constexpr double PetEngageDuration = 1.0;
    static constexpr float PetEngageDistance = 60.0f;
    static constexpr double PetStrokeGap = 0.6;      // a longer pause starts a new stroke
    // Hunting eyes only after sustained fast movement (brief slowdowns don't reset the timer).
    static constexpr double HuntArmDuration = 0.8;
    static constexpr double HuntGap = 0.25;
    static constexpr double ScrollHoldDuration = 0.35;

    // Scroll wheel paw
    Spring1D scrollReach{0.0f};
    float wheelPhase = 0.0f;

    // AirPods: the sound output switched to headphones -> both paws put AirPods in the ears.
    enum class EarbudPhase { Off, PuttingOn, On, TakingOff };
    struct EarbudPose { float inEar; float scale; float alpha; };   // inEar: 0 = held in the paw, 1 = in the ear
    struct EarPoint { float x, y, z; };
    struct EarCorners { EarPoint inner, outer, tip; float angle; };
    static constexpr double EarbudPutOnDuration = 1.3;
    static constexpr double EarbudTakeOffDuration = 1.15;
    static constexpr float EarbudStemAngle = 0.75f;
    EarbudPhase earbudPhase = EarbudPhase::Off;
    float earbudProgress = 0.0f;
    bool headphonesPlaying = false;

    void triggerScroll(float delta, const CatSettings& settings) {
        if (delta == 0.0f) return;
        sleepiness = 0.0f;
        double now = nowSeconds();
        if (now - lastScrollTime_ > 0.6 && settings.showMouseBadges) {
            Pt pt = { MouseBaseCenter.x + mouseOffset.point().x, MouseBaseCenter.y + mouseOffset.point().y - 6.0f };
            spawnFloatingKeycap(L"\u6EDA\u8F6E", pt, now);   // 滚轮
        }
        lastScrollTime_ = now;
        wheelPhase += std::clamp(delta * 0.25f, -1.1f, 1.1f);
        float step = std::min(0.55f, 0.12f + std::fabs(delta) * 0.05f);
        scrollStrokePhase_ += delta > 0.0f ? step : -step;
    }

    /// The sound output switched to headphones (true) or back to speakers. `animated` = false at launch.
    void setHeadphonesConnected(bool connected, bool animated, const CatSettings& settings) {
        headphonesConnected_ = connected;
        if (!animated) {
            earbudPhase = connected && settings.earbudAnimation ? EarbudPhase::On : EarbudPhase::Off;
            earbudProgress = 0.0f;
        }
    }

    bool isEarbudGesture() const { return earbudPhase == EarbudPhase::PuttingOn || earbudPhase == EarbudPhase::TakingOff; }

    /// Where the AirPods are drawn, or false when the cat has none.
    bool earbudPose(EarbudPose& out) const {
        float u = earbudProgress;
        switch (earbudPhase) {
            case EarbudPhase::Off: return false;
            case EarbudPhase::On: out = { 1.0f, 1.0f, 1.0f }; return true;
            case EarbudPhase::PuttingOn: {
                float pop = u < 0.16f ? easeOutBack(u / 0.16f) : 1.0f;
                out = { smoothstep(0.40f, 0.52f, u), pop, std::min(1.0f, u / 0.06f) };
                return true;
            }
            case EarbudPhase::TakingOff: {
                float away = 1.0f - smoothstep(0.62f, 0.88f, u);
                out = { 1.0f - smoothstep(0.34f, 0.46f, u), 0.35f + away * 0.65f, away };
                return true;
            }
        }
        return false;
    }

    Pt headCenter() const {
        float breath = std::sin(time * 2.1f) * 1.6f;
        return { SeatAnchor.x + bodyShiftX.value + bodyTilt.value * 36.0f + headYaw.value * 7.5f,
                 126.0f + headOffsetY.value + headPitch.value * 4.8f + breath * 1.1f };
    }

    /// Head-local 3D ear corners before projection, plus the ear's lean (shared with the renderer).
    static EarCorners earCorners(bool isLeft, float fold, float twitch) {
        float sign = isLeft ? -1.0f : 1.0f;
        float angle = sign * (0.06f + fold * 0.34f) + twitch;
        return { { sign * 9.0f, -16.0f, 0.0f }, { sign * 35.0f, -5.0f, -2.0f },
                 { sign * 24.5f + std::sin(angle) * 32.0f, -20.0f - std::cos(angle) * 32.0f, -3.0f }, angle };
    }

    EarCorners earCornersNow(bool isLeft) const {
        float twitch = earTwitch.value * 0.045f;
        return earCorners(isLeft, earFold.value, isLeft ? twitch : -twitch);
    }

    /// 2.5D projection on the head sphere (same as the renderer), also returning depth.
    static Pt project3D(float x, float y, float z, float yawRad, float pitchRad, float* depth = nullptr) {
        float cy = std::cos(yawRad), sy = std::sin(yawRad);
        float x1 = x * cy + z * sy;
        float z1 = -x * sy + z * cy;
        float cp = std::cos(pitchRad), sp = std::sin(pitchRad);
        if (depth) *depth = -y * sp + z1 * cp;
        return { x1, y * cp + z1 * sp };
    }

    /// Canvas point of an AirPod: a fixed blend of the ear's corners, so it stays in the ear as it folds.
    Pt earbudAnchor(bool isLeft, float* depth = nullptr) const {
        EarCorners e = earCornersNow(isLeft);
        const float wi = 0.30f, wo = 0.216f, wt = 0.484f;
        Pt p = project3D(wi * e.inner.x + wo * e.outer.x + wt * e.tip.x,
                         wi * e.inner.y + wo * e.outer.y + wt * e.tip.y,
                         wi * e.inner.z + wo * e.outer.z + wt * e.tip.z,
                         headYaw.value * 0.50f, headPitch.value * 0.36f, depth);
        float c = std::cos(headTilt.value), s = std::sin(headTilt.value);
        Pt hc = headCenter();
        return { hc.x + p.x * c - p.y * s, hc.y + p.x * s + p.y * c };
    }

    float earbudLean(bool isLeft) const { return earCornersNow(isLeft).angle - (isLeft ? -0.06f : 0.06f); }

    /// An AirPod held in a paw sits at the paw's inner top (the arm stays clear of the eyes).
    static Pt earbudHoldOffset(bool isLeft) { return { isLeft ? 8.0f : -8.0f, -5.0f }; }

    /// A small floating symbol above the head (♥ when a friend pets this cat from afar).
    void spawnFloatingNote(const std::wstring& note) {
        Pt hc = headCenter();
        PurrSparkle sp;
        sp.position = { hc.x + randRange(-30.0f, 22.0f), hc.y - 58.0f };
        sp.velocity = { randRange(-8.0f, 8.0f), -22.0f };
        sp.lifetime = 1.6f;
        sp.note = note;
        sparkles.push_back(sp);
    }

    static float smoothstep(float a, float b, float x) {
        float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
    static float easeOutBack(float x) {
        float t = std::clamp(x, 0.0f, 1.0f) - 1.0f;
        return 1.0f + 2.4f * t * t * t + 1.4f * t * t;
    }

    void triggerKeyDown(PhysicalKey key, const CatSettings& settings) {
        double now = nowSeconds();
        PhysicalKey vk = visualKeyOf(key);
        keyGlow[vk] = 1.0f;

        Pt pt = KeyboardLayout::canvasPointForKey(vk);
        hasLastTypedPoint_ = true;
        lastTypedPoint_ = pt;

        if (prefersViewerLeftPaw(key)) {
            recentLeftKey_ = vk;
            recentLeftTime_ = now;
            leftPaw.y.velocity -= 180.0f;
        } else {
            recentRightKey_ = vk;
            recentRightTime_ = now;
            rightPaw.y.velocity -= 180.0f;
        }

        bodySquash.velocity -= 1.3f;
        earTwitch.velocity += (prefersViewerLeftPaw(key) ? -3.8f : 3.8f);
        headTilt.velocity += (prefersViewerLeftPaw(key) ? -1.4f : 1.4f);
        pupilScale.target = 1.16f;
        sleepiness = 0.0f;

        if (settings.showKeyBadges) {
            spawnFloatingKeycap(keyBadgeLabel(key), pt, now);
        }
    }

    void triggerMouseClick(int button, bool isDown, const CatSettings& settings) {
        sleepiness = 0.0f;
        if (isDown) {
            double now = nowSeconds();
            if (button == 0) {
                mousePressLeft.target = 1.0f;
                leftPaw.y.velocity += 130.0f;
            } else {
                mousePressRight.target = 1.0f;
                leftPaw.y.velocity += 110.0f;
            }
            pupilScale.target = 1.12f;
            earTwitch.velocity -= 2.2f;
            if (settings.showMouseBadges) {
                std::wstring lbl = (button == 0) ? L"\u5DE6\u952E" : (button == 1 ? L"\u53F3\u952E" : L"\u4E2D\u952E");
                Pt pt = { MouseBaseCenter.x + mouseOffset.point().x, MouseBaseCenter.y + mouseOffset.point().y - 6.0f };
                spawnFloatingKeycap(lbl, pt, now);
            }
        } else {
            if (button == 0) mousePressLeft.target = 0.0f;
            else mousePressRight.target = 0.0f;
        }
    }

    void registerPetStroke(Pt canvasPoint, float delta, bool isHead, const CatSettings& settings) {
        if (!settings.petInteraction) return;
        double now = nowSeconds();

        // Petting must be deliberate: a cursor just passing over the cat shouldn't count.
        // A pause longer than the stroke gap starts a new stroke.
        if (now - lastStrokeTime_ > PetStrokeGap) {
            petStrokeStartTime_ = now;
            if (!isCurrentlyPetting_) petStrokeAccum_ = 0.0f;
        }
        lastStrokeTime_ = now;
        petStrokeAccum_ += delta;
        if (!isCurrentlyPetting_) {
            if (now - petStrokeStartTime_ < PetEngageDuration || petStrokeAccum_ < PetEngageDistance) return;
            isCurrentlyPetting_ = true;
            totalPetSessions++;
        }

        lastPetTime_ = now;
        sleepiness = 0.0f;

        float energy = std::min(delta / 26.0f, 0.35f);
        if (isHead) {
            petHeadIntensity = std::min(1.0f, petHeadIntensity + energy + 0.08f);
            petBodyIntensity = std::max(petBodyIntensity, petHeadIntensity * 0.6f);
        } else {
            petBodyIntensity = std::min(1.0f, petBodyIntensity + energy + 0.07f);
        }

        float dx = (canvasPoint.x - SeatAnchor.x) / 60.0f;
        headTilt.target = std::clamp(dx * 0.16f, -0.18f, 0.18f);
        headOffsetY.velocity += 18.0f;
    }

    void step(float rawDt, const RECT& windowRect, InputEngine& engine, const CatSettings& settings) {
        float dt = std::min(rawDt, 1.0f / 25.0f);
        time += dt;
        engine.pollMouse(dt);

        double now = nowSeconds();
        const auto& activeKeys = engine.activeKeys;
        const auto& orderedKeys = engine.pressedOrder;

        // 1. Petting decay & sparkles
        if (now - lastPetTime_ > 0.28) {
            petHeadIntensity = std::max(0.0f, petHeadIntensity - dt * 1.4f);
            petBodyIntensity = std::max(0.0f, petBodyIntensity - dt * 1.2f);
            if (petHeadIntensity < 0.05f && petBodyIntensity < 0.05f) {
                // Progress toward the next session is reset in registerPetStroke (after a pause),
                // not here: this runs every frame before petting engages.
                isCurrentlyPetting_ = false;
            }
        } else if (petHeadIntensity > 0.55f || petBodyIntensity > 0.55f) {
            petSparkleCooldown_ -= dt;
            if (petSparkleCooldown_ <= 0.0f) {
                petSparkleCooldown_ = 0.42f;
                sparkles.push_back({
                    { SeatAnchor.x + randRange(-44.0f, 44.0f), 88.0f + randRange(-14.0f, 18.0f) },
                    { randRange(-10.0f, 10.0f), -24.0f },
                    0.0f, 0.9f, false
                });
            }
        }

        // 2. Sleepy idle detection
        double lastAny = std::max(engine.lastKeyActivityTime, std::max(engine.lastMouseMoveTime, lastPetTime_));
        double idleSec = lastAny > 0.0 ? (now - lastAny) : 0.0;
        if (activeKeys.empty() && idleSec > 45.0) {
            float targetSleep = std::min(1.0f, (float)((idleSec - 45.0) / 35.0));
            sleepiness += (targetSleep - sleepiness) * dt * 1.5f;
            if (sleepiness > 0.8f) {
                sleepZCooldown_ -= dt;
                if (sleepZCooldown_ <= 0.0f) {
                    sleepZCooldown_ = 2.6f;
                    sparkles.push_back({ { SeatAnchor.x + 42.0f, 100.0f }, { 12.0f, -18.0f }, 0.0f, 1.1f, true });
                }
            }
        } else {
            sleepiness = std::max(0.0f, sleepiness - dt * 5.0f);
        }

        updateEarbudPhase(now, dt, settings);

        // 2b. Fast Mouse Movement -> Hunting Mode (big round boba pupils + low crouch!)
        float mouseSpeed = std::hypot(engine.mouseVelocity.x, engine.mouseVelocity.y);
        bool isFastMove = mouseSpeed > 460.0f && petHeadIntensity < 0.25f && petBodyIntensity < 0.25f;
        // Ordinary cursor moves across the screen easily exceed 460 px/s, so only sustained
        // fast waving counts: brief slowdowns (direction reversals) don't reset the timer.
        if (isFastMove) {
            if (now - lastFastMoveTime_ > HuntGap) fastMoveStartTime_ = now;
            lastFastMoveTime_ = now;
        }
        if (isFastMove && now - fastMoveStartTime_ >= HuntArmDuration) {
            float boost = std::min(1.0f, (mouseSpeed - 460.0f) / 1100.0f);
            huntIntensity.target = std::min(1.0f, huntIntensity.target + (0.45f + boost * 1.15f) * dt);
            sleepiness = 0.0f;
        } else {
            huntIntensity.target = std::max(0.0f, huntIntensity.target - dt * 0.65f);
        }
        huntIntensity.step(dt, 85.0f, 16.0f);
        float hunt = std::clamp(huntIntensity.value, 0.0f, 1.0f);

        // 3. Limb assignment (Left Paw, Right Paw, 3-Key Hind Foot!)
        constexpr double tapHoldWindow = 0.14;
        bool mouseRecent = (now - engine.lastMouseMoveTime < 2.2) || engine.leftMouseDown || engine.rightMouseDown;
        bool mouseActivelyUsed = (now - engine.lastMouseMoveTime < 0.75) || engine.leftMouseDown || engine.rightMouseDown;

        std::vector<PhysicalKey> leftCands, rightCands;
        for (PhysicalKey k : orderedKeys) {
            if (prefersViewerLeftPaw(k)) leftCands.push_back(k);
            else rightCands.push_back(k);
        }

        PhysicalKey leftKey = leftCands.empty() ? PhysicalKey::None : leftCands.back();
        PhysicalKey rightKey = rightCands.empty() ? PhysicalKey::None : rightCands.back();
        PhysicalKey footKey = PhysicalKey::None;

        if (leftKey == PhysicalKey::None && rightCands.size() >= 2 && !mouseActivelyUsed) {
            auto sorted = rightCands;
            std::sort(sorted.begin(), sorted.end(), [](PhysicalKey a, PhysicalKey b) {
                return KeyboardLayout::canvasPointForKey(a).x < KeyboardLayout::canvasPointForKey(b).x;
            });
            leftKey = sorted.front();
            rightKey = sorted.back();
        } else if (rightKey == PhysicalKey::None && leftCands.size() >= 2) {
            auto sorted = leftCands;
            std::sort(sorted.begin(), sorted.end(), [](PhysicalKey a, PhysicalKey b) {
                return KeyboardLayout::canvasPointForKey(a).x < KeyboardLayout::canvasPointForKey(b).x;
            });
            leftKey = sorted.front();
            rightKey = sorted.back();
        }

        if (settings.footEasterEgg) {
            if (orderedKeys.size() >= 3) {
                for (PhysicalKey k : orderedKeys) {
                    if (k != leftKey && k != rightKey) { footKey = k; break; }
                }
            } else if (mouseActivelyUsed && leftKey == PhysicalKey::None && rightCands.size() >= 2) {
                for (PhysicalKey k : rightCands) {
                    if (k != rightKey) { footKey = k; break; }
                }
            }
        }

        if (leftKey == PhysicalKey::None && recentLeftKey_ != PhysicalKey::None && now - recentLeftTime_ < tapHoldWindow) {
            leftKey = recentLeftKey_;
        }
        if (rightKey == PhysicalKey::None && recentRightKey_ != PhysicalKey::None && now - recentRightTime_ < tapHoldWindow) {
            rightKey = recentRightKey_;
        }

        // Subtle mini-mouse movement
        mouseOffset.setTarget({
            std::clamp(engine.mouseVelocity.x * 0.0045f, -5.5f, 5.5f),
            std::clamp(engine.mouseVelocity.y * 0.0045f, -4.5f, 4.5f)
        });
        mouseOffset.step(dt, 120.0f, 16.0f);
        mousePressLeft.step(dt, 400.0f, 24.0f);
        mousePressRight.step(dt, 400.0f, 24.0f);
        scrollReach.target = (now - lastScrollTime_ < ScrollHoldDuration) ? 1.0f : 0.0f;
        scrollReach.step(dt, 220.0f, 22.0f);

        Pt liveMouse = { MouseBaseCenter.x + mouseOffset.point().x, MouseBaseCenter.y + mouseOffset.point().y };

        if (leftKey != PhysicalKey::None) {
            leftPaw.setTarget(KeyboardLayout::canvasPointForKey(leftKey));
            leftPawTyping = true;
            leftPawOnMouse = false;
        } else if (mouseRecent && sleepiness < 0.3f) {
            // While scrolling, slide from the palm rest (+2, -3) forward onto the wheel and rub
            // back and forth along it. The wheel axis is the mouse's 0.22 rad tilt: (-0.22, 0.98).
            float reach = std::clamp(scrollReach.value, 0.0f, 1.0f);
            float stroke = std::sin(scrollStrokePhase_) * 3.2f * reach;
            leftPaw.setTarget({ liveMouse.x + 2.0f - reach * 3.0f - stroke * 0.22f,
                                liveMouse.y - 3.0f + reach * 5.0f + stroke * 0.98f });
            leftPawTyping = false;
            leftPawOnMouse = true;
        } else {
            leftPaw.setTarget(LeftPawRest);
            leftPawTyping = false;
            leftPawOnMouse = false;
        }

        if (rightKey != PhysicalKey::None) {
            rightPaw.setTarget(KeyboardLayout::canvasPointForKey(rightKey));
            rightPawTyping = true;
        } else {
            rightPaw.setTarget(RightPawRest);
            rightPawTyping = false;
        }

        // AirPods gesture: both paws rise beside the head, press the buds in (or pull them out), return.
        if (isEarbudGesture()) {
            float u = earbudProgress;
            bool putting = earbudPhase == EarbudPhase::PuttingOn;
            bool up = u < 0.56f;
            bool atEar = putting ? u >= 0.36f : u < 0.34f;
            for (int side = 0; side < 2; ++side) {
                bool isLeft = side == 0;
                float sign = isLeft ? -1.0f : 1.0f;
                Pt ear = earbudAnchor(isLeft);
                Pt off = earbudHoldOffset(isLeft);
                Pt hold = { ear.x - off.x, ear.y - off.y };
                Pt target = !up ? (isLeft ? LeftPawRest : RightPawRest)
                          : atEar ? hold : Pt{ hold.x + sign * 13.0f, hold.y + 9.0f };
                (isLeft ? leftPaw : rightPaw).setTarget(target);
            }
            leftPawTyping = rightPawTyping = leftPawOnMouse = false;
        }

        if (footKey != PhysicalKey::None) {
            hindFoot.setTarget(KeyboardLayout::canvasPointForKey(footKey));
            hindFootActive.target = 1.0f;
        } else {
            hindFoot.setTarget(HindFootRest);
            hindFootActive.target = 0.0f;
        }

        leftPaw.step(dt, 360.0f, 24.0f);
        rightPaw.step(dt, 360.0f, 24.0f);
        hindFoot.step(dt, 320.0f, 22.0f);
        hindFootActive.step(dt, 260.0f, 22.0f);

        // 4. Body lean, squash & ear motion
        float targetShiftX = 0.0f, targetBodyTilt = 0.0f;
        if (leftPawOnMouse && !rightPawTyping) {
            targetShiftX = -10.0f;
            targetBodyTilt = -0.075f;
        } else if (leftPawOnMouse && rightPawTyping) {
            targetShiftX = -3.0f;
            targetBodyTilt = (rightPaw.point().x - 196.0f) * 0.0008f;
        } else if (leftPawTyping || rightPawTyping) {
            float avgX = ((leftPawTyping ? leftPaw.point().x : 184.0f) + (rightPawTyping ? rightPaw.point().x : 184.0f)) * 0.5f;
            targetShiftX = (avgX - 184.0f) * 0.14f;
            targetBodyTilt = (avgX - 184.0f) * 0.0011f;
        }
        bodyShiftX.target = std::clamp(targetShiftX, -16.0f, 16.0f);
        bodyTilt.target = std::clamp(targetBodyTilt, -0.12f, 0.12f);
        bodySquash.target = 1.0f - hunt * 0.11f;
        headOffsetY.target = (leftPawTyping || rightPawTyping) ? 3.5f : (sleepiness * 5.0f + hunt * 10.5f);
        earFold.target = std::max(petHeadIntensity * 0.7f, petBodyIntensity * 0.45f) + sleepiness * 0.25f - hunt * 0.18f;
        earTwitch.target = 0.0f;

        bodyShiftX.step(dt, 140.0f, 18.0f);
        bodyTilt.step(dt, 140.0f, 18.0f);
        bodySquash.step(dt, 280.0f, 18.0f);
        headOffsetY.step(dt, 180.0f, 19.0f);
        earFold.step(dt, 120.0f, 16.0f);
        earTwitch.step(dt, 320.0f, 16.0f);

        // 5. 2.5D Head Turning (Yaw + Pitch) & Eye Tracking (Windows screen Y is top-down!)
        bool typingRecently = !activeKeys.empty() || (now - engine.lastKeyActivityTime < 0.65);
        bool mouseMovedRecently = (now - engine.lastMouseMoveTime < 1.2);
        Pt eyeOrigin = { SeatAnchor.x + bodyShiftX.value, 124.0f + headOffsetY.value };

        float winW = std::max(1.0f, (float)(windowRect.right - windowRect.left));
        float winScale = std::max(0.2f, winW / CanvasWidth);
        float headScreenX = (float)windowRect.left + eyeOrigin.x * winScale;
        float headScreenY = (float)windowRect.top  + eyeOrigin.y * winScale;

        float curDx = ((float)engine.lastCursorPos.x - headScreenX) / 290.0f;
        float curDy = ((float)engine.lastCursorPos.y - headScreenY) / 260.0f;
        float curMag = std::hypot(curDx, curDy);
        float clampedCur = std::min(1.0f, curMag);
        float cursorNX = curMag > 0.001f ? (curDx / curMag) * clampedCur : 0.0f;
        float cursorNY = curMag > 0.001f ? (curDy / curMag) * clampedCur : 0.0f;

        float lookX = cursorNX, lookY = cursorNY;
        if (typingRecently && !mouseMovedRecently && hasLastTypedPoint_) {
            lookX = std::clamp((lastTypedPoint_.x - eyeOrigin.x) / 95.0f, -0.85f, 0.85f);
            lookY = std::clamp((lastTypedPoint_.y - eyeOrigin.y) / 125.0f, -0.3f, 0.92f);
        } else if (typingRecently && mouseMovedRecently && hasLastTypedPoint_) {
            float kdx = std::clamp((lastTypedPoint_.x - eyeOrigin.x) / 95.0f, -0.85f, 0.85f);
            lookX = cursorNX * 0.72f + kdx * 0.28f;
            lookY = cursorNY * 0.72f + 0.25f;
        }

        if (sleepiness > 0.45f) {
            headYaw.target = 0.0f;
            headPitch.target = 0.45f * sleepiness;
        } else {
            headYaw.target = std::clamp(lookX, -1.0f, 1.0f);
            headPitch.target = std::clamp(lookY, -1.0f, 1.0f);
        }
        float headStiff = 88.0f + hunt * 75.0f;
        float headDamp  = 14.5f + hunt * 4.5f;
        headYaw.step(dt, headStiff, headDamp);
        headPitch.step(dt, headStiff, headDamp);

        if (now - lastPetTime_ > 0.28) {
            headTilt.target = bodyTilt.target * 0.45f + headYaw.value * (0.11f + hunt * 0.05f) - headYaw.value * headPitch.value * 0.04f;
        }
        headTilt.step(dt, 130.0f, 16.0f);

        eyeGaze.setTarget({ lookX * 0.88f, lookY * 0.85f });
        eyeGaze.step(dt, 210.0f + hunt * 80.0f, 21.0f);

        if (hindFootActive.value > 0.3f) {
            pupilScale.target = 1.24f;
        } else if (now - engine.lastKeyActivityTime > 0.45 && now - engine.lastMouseClickTime > 0.45) {
            pupilScale.target = 1.0f;
        }
        pupilScale.step(dt, 150.0f, 18.0f);

        // 6. Blinking (no blinking during locked-on hunting!)
        if (isBlinking_) {
            blinkTimer_ += dt;
            constexpr float blinkDur = 0.14f;
            if (blinkTimer_ >= blinkDur) {
                isBlinking_ = false;
                blinkProgress_ = 0.0f;
                nextBlinkTime_ = time + randRange(2.6f, 5.6f);
            } else {
                blinkProgress_ = std::sin((blinkTimer_ / blinkDur) * 3.14159265f);
            }
        } else if (time >= nextBlinkTime_ && sleepiness < 0.5f && petHeadIntensity < 0.3f && hunt < 0.35f) {
            isBlinking_ = true;
            blinkTimer_ = 0.0f;
        }

        // 7. Tail wag speed
        float kpmBoost = std::min(2.4f, (float)engine.currentKPM() / 160.0f);
        float petBoost = std::max(petHeadIntensity, petBodyIntensity) * 1.4f;
        float tailSpeed = 1.85f + kpmBoost + petBoost + hunt * 2.8f - sleepiness * 1.1f;
        tailPhase += dt * tailSpeed;

        // 8. Keycap glow decay
        std::unordered_set<PhysicalKey> visualActive;
        for (PhysicalKey k : activeKeys) visualActive.insert(visualKeyOf(k));
        for (auto it = keyGlow.begin(); it != keyGlow.end(); ) {
            if (visualActive.count(it->first)) {
                it->second = 1.0f;
                ++it;
            } else {
                it->second -= dt * 4.5f;
                if (it->second <= 0.01f) it = keyGlow.erase(it);
                else ++it;
            }
        }
        for (PhysicalKey vk : visualActive) keyGlow[vk] = 1.0f;

        // 9. Floating keycaps
        for (int i = (int)floatingKeycaps.size() - 1; i >= 0; --i) {
            auto& b = floatingKeycaps[i];
            b.age += dt;
            b.position.x += b.velocity.x * dt;
            b.position.y += b.velocity.y * dt;
            b.velocity.x *= (1.0f - dt * 2.4f);
            b.velocity.y *= (1.0f - dt * 1.6f);
            if (b.age >= b.lifetime) floatingKeycaps.erase(floatingKeycaps.begin() + i);
        }

        // 10. Sparkles / Sleep Z
        for (int i = (int)sparkles.size() - 1; i >= 0; --i) {
            auto& sp = sparkles[i];
            sp.age += dt;
            sp.position.x += sp.velocity.x * dt;
            sp.position.y += sp.velocity.y * dt;
            if (sp.age >= sp.lifetime) sparkles.erase(sparkles.begin() + i);
        }
    }

private:
    float nextBlinkTime_ = 3.2f;
    float blinkProgress_ = 0.0f;
    bool isBlinking_ = false;
    float blinkTimer_ = 0.0f;

    float petStrokeAccum_ = 0.0f;
    double lastStrokeTime_ = 0.0;
    double petStrokeStartTime_ = 0.0;
    double fastMoveStartTime_ = 0.0;
    double lastFastMoveTime_ = 0.0;
    double lastScrollTime_ = 0.0;
    float scrollStrokePhase_ = 0.0f;
    bool headphonesConnected_ = false;
    double earbudPhaseStart_ = 0.0;
    float musicNoteCooldown_ = 0.6f;
    double lastPetTime_ = 0.0;
    bool isCurrentlyPetting_ = false;
    float petSparkleCooldown_ = 0.0f;
    float sleepZCooldown_ = 0.0f;

    PhysicalKey recentLeftKey_ = PhysicalKey::None;
    double recentLeftTime_ = 0.0;
    PhysicalKey recentRightKey_ = PhysicalKey::None;
    double recentRightTime_ = 0.0;
    bool hasLastTypedPoint_ = false;
    Pt lastTypedPoint_ = { 184.0f, 260.0f };

    int badgeCascade_ = 0;
    double lastBadgeTime_ = 0.0;

    void updateEarbudPhase(double now, float dt, const CatSettings& settings) {
        bool wants = headphonesConnected_ && settings.earbudAnimation;
        if ((earbudPhase == EarbudPhase::Off && wants) || (earbudPhase == EarbudPhase::On && !wants)) {
            earbudPhase = wants ? EarbudPhase::PuttingOn : EarbudPhase::TakingOff;
            earbudPhaseStart_ = now;
            sleepiness = 0.0f;
        } else if (isEarbudGesture()) {
            double duration = earbudPhase == EarbudPhase::PuttingOn ? EarbudPutOnDuration : EarbudTakeOffDuration;
            float previous = earbudProgress;
            earbudProgress = (float)std::min(1.0, (now - earbudPhaseStart_) / duration);
            if (earbudPhase == EarbudPhase::PuttingOn && previous < 0.5f && earbudProgress >= 0.5f) earTwitch.velocity += 34.0f;
            if (earbudPhase == EarbudPhase::TakingOff && previous < 0.4f && earbudProgress >= 0.4f) earTwitch.velocity -= 26.0f;
            if (earbudProgress >= 1.0f) {
                earbudPhase = earbudPhase == EarbudPhase::PuttingOn ? EarbudPhase::On : EarbudPhase::Off;
                earbudProgress = 0.0f;
                if (earbudPhase == EarbudPhase::Off) {   // little poof where they were tucked away
                    for (Pt paw : { leftPaw.point(), rightPaw.point() }) {
                        PurrSparkle sp;
                        sp.position = { paw.x, paw.y - 8.0f };
                        sp.velocity = { 0.0f, -20.0f };
                        sparkles.push_back(sp);
                    }
                }
            }
        }
        // Music notes drift out of the ears while something is playing through the headphones.
        if (earbudPhase == EarbudPhase::On && headphonesPlaying && sleepiness < 0.5f) {
            musicNoteCooldown_ -= dt;
            if (musicNoteCooldown_ <= 0.0f) {
                musicNoteCooldown_ = randRange(1.5f, 2.6f);
                bool isLeft = randRange(0.0f, 1.0f) < 0.5f;
                float sign = isLeft ? -1.0f : 1.0f;
                Pt ear = earbudAnchor(isLeft);
                PurrSparkle sp;
                sp.position = { ear.x + sign * 8.0f - 4.0f, ear.y - 16.0f };
                sp.velocity = { sign * 13.0f, -20.0f };
                sp.lifetime = 1.7f;
                sp.note = randRange(0.0f, 1.0f) < 0.5f ? L"\u266A" : L"\u266B";   // ♪ ♫
                sparkles.push_back(sp);
            }
        }
    }

    static float randRange(float a, float b) {
        static thread_local std::mt19937 rng{ std::random_device{}() };
        std::uniform_real_distribution<float> dist(a, b);
        return dist(rng);
    }

    void spawnFloatingKeycap(const std::wstring& label, Pt pt, double now) {
        if (label.empty()) return;
        if (now - lastBadgeTime_ < 0.38) badgeCascade_ = (badgeCascade_ + 1) % 6;
        else badgeCascade_ = 0;
        lastBadgeTime_ = now;

        static const float spread[6] = { -10.0f, 14.0f, -22.0f, 26.0f, 2.0f, 36.0f };
        float dx = spread[badgeCascade_] + randRange(-3.0f, 3.0f);
        float startX = std::clamp(pt.x + dx * 0.45f, 55.0f, 315.0f);
        float startY = pt.y - 26.0f - (float)(badgeCascade_ % 2) * 8.0f;

        floatingKeycaps.push_back({
            label,
            { startX, startY },
            { dx * 0.65f, -36.0f - randRange(0.0f, 14.0f) },
            0.0f, 1.25f, randRange(-0.07f, 0.07f)
        });
        if (floatingKeycaps.size() > 16) {
            floatingKeycaps.erase(floatingKeycaps.begin(), floatingKeycaps.begin() + (floatingKeycaps.size() - 16));
        }
    }
};
