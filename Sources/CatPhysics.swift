import Foundation
import CoreGraphics
import AppKit

// MARK: - Themes & Settings

enum KeyboardTheme: String, CaseIterable {
    case classicMilk = "经典奶白暖黄 (原版)"
    case midnightHacker = "暗夜极客黑"
    case retroCream = "复古奶油灰"
}

enum CatCoatTheme: String, CaseIterable {
    case classicBlack = "经典小黑猫 (原版)"
    case siamese = "挖煤暹罗猫"
    case shorthair = "美短（银虎斑加白）"
    case orangeWhite = "橘白猫"
}

enum EyeColorTheme: String, CaseIterable {
    case neonChartreuse = "经典荧光黄绿 (原版)"
    case siameseBlue = "暹罗宝石蓝 (暹罗标配)"
    case warmAmber = "琥珀金瞳"
    case iceCyan = "冰晶浅蓝"
    case softAmber = "柔和琥珀"
    case referenceHazel = "淡金青绿"

    static func saved(_ raw: String) -> Self {
        raw == "淡金青绿（参考图）" ? .referenceHazel : (Self(rawValue: raw) ?? .neonChartreuse)
    }

    var primaryColor: NSColor {
        switch self {
        case .neonChartreuse: return NSColor(calibratedRed: 0.79, green: 0.95, blue: 0.25, alpha: 1.0) // #C9F240
        case .siameseBlue:    return NSColor(calibratedRed: 0.37, green: 0.78, blue: 0.98, alpha: 1.0) // #5EC7FA
        case .warmAmber:      return NSColor(calibratedRed: 0.98, green: 0.76, blue: 0.22, alpha: 1.0)
        case .iceCyan:        return NSColor(calibratedRed: 0.45, green: 0.92, blue: 0.96, alpha: 1.0)
        case .referenceHazel: return NSColor(calibratedRed: 218.0/255, green: 178.0/255, blue: 87.0/255, alpha: 1)
        case .softAmber: return NSColor(calibratedRed: 217.0/255, green: 173.0/255, blue: 85.0/255, alpha: 1)
        }
    }

    var glowColor: NSColor {
        switch self {
        case .neonChartreuse: return NSColor(calibratedRed: 0.77, green: 0.95, blue: 0.24, alpha: 0.85)
        case .siameseBlue:    return NSColor(calibratedRed: 0.38, green: 0.80, blue: 0.99, alpha: 0.85)
        case .warmAmber:      return NSColor(calibratedRed: 0.98, green: 0.74, blue: 0.20, alpha: 0.85)
        case .iceCyan:        return NSColor(calibratedRed: 0.42, green: 0.90, blue: 0.98, alpha: 0.85)
        case .referenceHazel: return NSColor(calibratedRed: 218.0/255, green: 178.0/255, blue: 87.0/255, alpha: 0.85)
        case .softAmber: return NSColor(calibratedRed: 217.0/255, green: 173.0/255, blue: 85.0/255, alpha: 0.85)
        }
    }
}

final class CatSettings {
    static let shared = CatSettings()

    var showKeyBadges: Bool {
        didSet { UserDefaults.standard.set(showKeyBadges, forKey: "KuroCat.ShowKeyBadges") }
    }
    var showMouseBadges: Bool {
        didSet { UserDefaults.standard.set(showMouseBadges, forKey: "KuroCat.ShowMouseBadges") }
    }
    var showKeyLegends: Bool {
        didSet { UserDefaults.standard.set(showKeyLegends, forKey: "KuroCat.ShowKeyLegends") }
    }
    var footEasterEgg: Bool {
        didSet { UserDefaults.standard.set(footEasterEgg, forKey: "KuroCat.FootEasterEgg") }
    }
    var petInteraction: Bool {
        didSet { UserDefaults.standard.set(petInteraction, forKey: "KuroCat.PetInteraction") }
    }
    var earbudAnimation: Bool {
        didSet { UserDefaults.standard.set(earbudAnimation, forKey: "KuroCat.EarbudAnimation") }
    }
    var fullClickThrough: Bool {
        didSet { UserDefaults.standard.set(fullClickThrough, forKey: "KuroCat.FullClickThrough") }
    }
    var fullScreenOverlay: Bool {
        didSet { UserDefaults.standard.set(fullScreenOverlay, forKey: "KuroCat.FullScreenOverlay") }
    }
    var scale: CGFloat {
        didSet { UserDefaults.standard.set(Double(scale), forKey: "KuroCat.Scale") }
    }
    var coatTheme: CatCoatTheme {
        didSet { UserDefaults.standard.set(coatTheme.rawValue, forKey: "KuroCat.CoatTheme") }
    }
    var keyboardTheme: KeyboardTheme {
        didSet { UserDefaults.standard.set(keyboardTheme.rawValue, forKey: "KuroCat.KeyboardTheme") }
    }
    var eyeTheme: EyeColorTheme {
        didSet { UserDefaults.standard.set(eyeTheme.rawValue, forKey: "KuroCat.EyeTheme") }
    }

    private init() {
        let d = UserDefaults.standard
        showKeyBadges = d.object(forKey: "KuroCat.ShowKeyBadges") as? Bool ?? true
        showMouseBadges = d.object(forKey: "KuroCat.ShowMouseBadges") as? Bool ?? true
        showKeyLegends = d.object(forKey: "KuroCat.ShowKeyLegends") as? Bool ?? true
        footEasterEgg = d.object(forKey: "KuroCat.FootEasterEgg") as? Bool ?? true
        petInteraction = d.object(forKey: "KuroCat.PetInteraction") as? Bool ?? true
        earbudAnimation = d.object(forKey: "KuroCat.EarbudAnimation") as? Bool ?? true
        fullClickThrough = d.object(forKey: "KuroCat.FullClickThrough") as? Bool ?? false
        fullScreenOverlay = d.object(forKey: "KuroCat.FullScreenOverlay") as? Bool ?? true
        let savedScale = d.double(forKey: "KuroCat.Scale")
        scale = savedScale > 0.3 ? CGFloat(savedScale) : 1.0
        coatTheme = CatCoatTheme(rawValue: d.string(forKey: "KuroCat.CoatTheme") ?? "") ?? .classicBlack
        keyboardTheme = KeyboardTheme(rawValue: d.string(forKey: "KuroCat.KeyboardTheme") ?? "") ?? .classicMilk
        let savedEye = d.string(forKey: "KuroCat.EyeTheme") ?? ""
        eyeTheme = EyeColorTheme.saved(savedEye)
    }
}

// MARK: - Spring Primitives

struct Spring1D {
    var value: CGFloat
    var velocity: CGFloat = 0
    var target: CGFloat

    init(_ initial: CGFloat) {
        self.value = initial
        self.target = initial
    }

    mutating func step(dt: CGFloat, stiffness: CGFloat = 260, damping: CGFloat = 22) {
        let force = (target - value) * stiffness
        let damp = velocity * damping
        velocity += (force - damp) * dt
        value += velocity * dt
    }
}

struct Spring2D {
    var x: Spring1D
    var y: Spring1D

    init(_ pt: CGPoint) {
        self.x = Spring1D(pt.x)
        self.y = Spring1D(pt.y)
    }

    var point: CGPoint { CGPoint(x: x.value, y: y.value) }
    var target: CGPoint {
        get { CGPoint(x: x.target, y: y.target) }
        set { x.target = newValue.x; y.target = newValue.y }
    }

    mutating func step(dt: CGFloat, stiffness: CGFloat = 340, damping: CGFloat = 25) {
        x.step(dt: dt, stiffness: stiffness, damping: damping)
        y.step(dt: dt, stiffness: stiffness, damping: damping)
    }
}

// MARK: - Particles

struct FloatingKeycap {
    let id: Int
    let label: String
    var position: CGPoint
    var velocity: CGPoint
    var age: CGFloat = 0
    let lifetime: CGFloat = 1.25
    var rotation: CGFloat
}

struct PurrSparkle {
    var position: CGPoint
    var velocity: CGPoint
    var age: CGFloat = 0
    var lifetime: CGFloat = 0.9
    let isSleepZ: Bool
    var note: String? = nil   // music note drawn as text while the cat wears its AirPods
}

struct ScriptedFrameInput {
    var now: CFTimeInterval
    var activeKeys: Set<PhysicalKey>
    var orderedKeys: [PhysicalKey]
    var mouseScreenPos: CGPoint
    var mouseVelocity: CGVector
    var leftMouseDown: Bool
    var rightMouseDown: Bool
    var lastMouseMoveTime: CFTimeInterval
    var lastMouseClickTime: CFTimeInterval
    var lastKeyActivityTime: CFTimeInterval
}

// MARK: - Cat Physics & Procedural Animation State

final class CatPhysics {
    // Logical canvas dimensions
    static let canvasSize = CGSize(width: 360, height: 320)

    // Rest positions
    static let seatAnchor = CGPoint(x: 184, y: 226)
    static let leftPawRest = CGPoint(x: 164, y: 235)
    static let rightPawRest = CGPoint(x: 204, y: 235)
    static let hindFootRest = CGPoint(x: 214, y: 234)
    static let mouseBaseCenter = CGPoint(x: 78, y: 258)

    // Springs
    var leftPaw = Spring2D(leftPawRest)
    var rightPaw = Spring2D(rightPawRest)
    var hindFoot = Spring2D(hindFootRest)
    var hindFootActive = Spring1D(0) // 0 = tucked, 1 = kicking 3rd key

    var bodyShiftX = Spring1D(0)
    var bodyTilt = Spring1D(0)       // radians
    var bodySquash = Spring1D(1.0)
    var headTilt = Spring1D(0)       // radians (roll)
    var headYaw = Spring1D(0)        // -1 (turn left) ... +1 (turn right)
    var headPitch = Spring1D(0)      // -1 (look up)   ... +1 (look down)
    var headOffsetY = Spring1D(0)
    var earFold = Spring1D(0)        // 0 = alert, 1 = relaxed petting ears
    var earTwitch = Spring1D(0)

    var eyeGaze = Spring2D(.zero)    // normalized [-1..1, -1..1]
    var pupilScale = Spring1D(1.0)
    var huntIntensity = Spring1D(0)  // 0 = normal slit pupils, 1 = full hunting crouch + big round boba pupils!
    var mouseOffset = Spring2D(.zero)
    var mousePressLeft = Spring1D(0)
    var mousePressRight = Spring1D(0)

    // Paw mode flags (for crisp outline & posture rendering)
    private(set) var leftPawOnMouse = false
    /// 0 = paw resting on the mouse's palm, 1 = toes on the scroll wheel.
    var scrollReach = Spring1D(0)
    /// Accumulated wheel roll in canvas points, drives the wheel stripes.
    private(set) var wheelPhase: CGFloat = 0
    private var lastScrollTime: CFTimeInterval = 0
    /// Phase (radians) of the paw rubbing back and forth along the wheel while scrolling.
    private var scrollStrokePhase: CGFloat = 0
    /// How long the paw stays on the wheel after the last scroll event.
    static let scrollHoldDuration: CFTimeInterval = 0.35

    // Cat Link: a second cat driven by a friend's events instead of this Mac's input.
    struct Appearance { var coat: CatCoatTheme; var keyboard: KeyboardTheme; var eye: EyeColorTheme }
    /// Draw with these themes instead of this Mac's settings (the friend's own choices).
    var appearance: Appearance?
    /// Keys the friend is holding (anonymized); used instead of this Mac's keyboard.
    var externalActiveKeys: Set<PhysicalKey>?
    /// Typing speed for the tail wag; nil = this Mac's own KPM.
    var kpmOverride: Int?
    /// No key badges: a remote cat's keys are random stand-ins, so their letters mean nothing.
    var showsKeyBadges = true

    // AirPods: when the sound output switches to headphones, both paws lift two AirPods into the
    // ears; when it switches back to speakers, the paws take them out and tuck them away.
    enum EarbudPhase { case off, puttingOn, on, takingOff }
    struct EarbudPose {
        var inEar: CGFloat   // 0 = held in the paw, 1 = sitting in the ear
        var scale: CGFloat
        var alpha: CGFloat
    }
    private(set) var earbudPhase: EarbudPhase = .off
    private(set) var earbudProgress: CGFloat = 0   // 0...1 through the current gesture
    private var earbudPhaseStart: CFTimeInterval = 0
    private var headphonesConnected = false
    var headphonesPlaying = false
    private var musicNoteCooldown: CGFloat = 0.6
    static let earbudPutOnDuration: CFTimeInterval = 1.3
    static let earbudTakeOffDuration: CFTimeInterval = 1.15
    private(set) var leftPawTyping = false
    private(set) var rightPawTyping = false

    // Keycap glow & press depths
    var keyGlow: [PhysicalKey: CGFloat] = [:]
    var floatingKeycaps: [FloatingKeycap] = []
    var sparkles: [PurrSparkle] = []
    private var nextBadgeID = 1
    private var badgeCascadeIndex = 0
    private var lastBadgeTime: CFTimeInterval = 0

    // Time & blink
    private(set) var time: CGFloat = 0
    private(set) var tailPhase: CGFloat = 0
    private var nextBlinkTime: CGFloat = 3.2
    private var blinkProgress: CGFloat = 0 // 0 = open, 1 = closed
    private var isBlinking = false
    private var blinkTimer: CGFloat = 0

    // Petting state
    private(set) var petHeadIntensity: CGFloat = 0  // 0..1 -> happy closed lime lines
    private(set) var petBodyIntensity: CGFloat = 0  // 0..1 -> blissful half-squint
    private(set) var totalPetSessions: Int = UserDefaults.standard.integer(forKey: "KuroCat.PetCount")
    private var petStrokeAccumulator: CGFloat = 0
    private var lastPetTime: CFTimeInterval = 0
    private var lastStrokeTime: CFTimeInterval = 0
    private var petStrokeStartTime: CFTimeInterval = 0
    /// The cat only starts reacting after this long of petting...
    static let petEngageDuration: CFTimeInterval = 1.0
    /// ...with at least this much cursor travel (canvas points; the head is ~96 wide),
    /// so a cursor that merely drifts onto the head doesn't count.
    static let petEngageDistance: CGFloat = 60
    /// Pauses up to this long (changing direction, lifting a finger off the trackpad)
    /// keep the petting progress; a longer pause starts over.
    static let petStrokeGap: CFTimeInterval = 0.6
    private var fastMoveStartTime: CFTimeInterval = 0
    private var lastFastMoveTime: CFTimeInterval = 0
    /// Seconds of continuous fast mouse movement before the pupils start turning round.
    static let huntArmDuration: CFTimeInterval = 0.8
    /// Slowdowns shorter than this (e.g. reversing direction while waving) keep the timer going.
    static let huntGap: CFTimeInterval = 0.25
    private var isCurrentlyPetting = false
    private var petCursorCanvasPos: CGPoint = .zero
    private var petSparkleCooldown: CGFloat = 0

    // Sleepy idle state
    private(set) var sleepiness: CGFloat = 0 // 0 = awake, 1 = napping
    private var sleepZCooldown: CGFloat = 0

    // Recent key memory so fast single taps linger visibly (~140ms)
    private var recentLeftKey: (key: PhysicalKey, time: CFTimeInterval)?
    private var recentRightKey: (key: PhysicalKey, time: CFTimeInterval)?
    private var lastTypedPoint: CGPoint?

    // Computed eye closure (0 = wide open, 0.55 = half squint, 1 = fully closed)
    var effectiveEyeClosure: CGFloat {
        let petClose = max(petHeadIntensity * 1.0, petBodyIntensity * 0.62)
        let sleepClose = sleepiness * 0.92
        return min(1.0, max(blinkProgress, max(petClose, sleepClose)))
    }

    /// True when eyes should draw the signature glowing lime horizontal closed-eye lines
    var isHappyClosedEyes: Bool {
        petHeadIntensity > 0.45
    }

    var isBlissfulSquint: Bool {
        petBodyIntensity > 0.35 && !isHappyClosedEyes
    }

    // MARK: - Input Hooks

    func triggerKeyDown(_ key: PhysicalKey, at timestamp: CFTimeInterval? = nil) {
        let now = timestamp ?? CACurrentMediaTime()
        let vk = key.visualKey
        keyGlow[vk] = 1.0

        let pt = KeyboardLayout.canvasPoint(for: vk)
        lastTypedPoint = pt

        // Snap memory to the appropriate paw with lively bounce velocity
        let active = externalActiveKeys ?? InputEngine.shared.activeKeys
        let leftHeld = active.contains { $0.prefersLeftPaw }
        let rightHeld = active.contains { !$0.prefersLeftPaw }

        if key.prefersLeftPaw {
            recentLeftKey = (vk, now)
            leftPaw.y.velocity -= 180
        } else if !rightHeld || !leftHeld {
            recentRightKey = (vk, now)
            rightPaw.y.velocity -= 180
        } else {
            recentRightKey = (vk, now)
            rightPaw.y.velocity -= 180
        }

        // Expressive micro-reactions
        bodySquash.velocity -= 1.3
        earTwitch.velocity += (key.prefersLeftPaw ? -3.8 : 3.8)
        headTilt.velocity += (key.prefersLeftPaw ? -1.4 : 1.4)
        pupilScale.target = 1.16

        // Wake up immediately
        sleepiness = 0

        // Spawn floating keycap badge
        if showsKeyBadges && CatSettings.shared.showKeyBadges {
            spawnFloatingKeycap(label: key.badgeLabel, near: pt, now: now)
        }
    }

    func triggerMouseClick(button: Int, isDown: Bool) {
        sleepiness = 0
        if isDown {
            let now = CACurrentMediaTime()
            if button == 0 {
                mousePressLeft.target = 1.0
                leftPaw.y.velocity += 130
            } else {
                mousePressRight.target = 1.0
                leftPaw.y.velocity += 110
            }
            pupilScale.target = 1.12
            earTwitch.velocity -= 2.2
            if CatSettings.shared.showMouseBadges {
                let label = button == 0 ? "左键" : (button == 1 ? "右键" : "中键")
                let pt = CGPoint(
                    x: CatPhysics.mouseBaseCenter.x + mouseOffset.point.x,
                    y: CatPhysics.mouseBaseCenter.y + mouseOffset.point.y - 6
                )
                spawnFloatingKeycap(label: label, near: pt, now: now)
            }
        } else {
            if button == 0 { mousePressLeft.target = 0 }
            else { mousePressRight.target = 0 }
        }
    }

    /// Scrolling: the paw on the mini mouse reaches onto the wheel and flicks it a little.
    func triggerScroll(delta: CGFloat, at timestamp: CFTimeInterval? = nil) {
        guard delta != 0 else { return }
        sleepiness = 0
        let now = timestamp ?? CACurrentMediaTime()
        // A new scroll gesture pops a "滚轮" badge, like clicks pop "左键"/"右键".
        if now - lastScrollTime > 0.6 && CatSettings.shared.showMouseBadges {
            let pt = CGPoint(x: CatPhysics.mouseBaseCenter.x + mouseOffset.point.x,
                             y: CatPhysics.mouseBaseCenter.y + mouseOffset.point.y - 6)
            spawnFloatingKeycap(label: "滚轮", near: pt, now: now)
        }
        lastScrollTime = now
        // Roll the wheel's stripes. Each step stays under half the stripe spacing (2.8),
        // otherwise fast scrolling makes the stripes appear to crawl backward.
        wheelPhase += max(-1.1, min(1.1, delta * 0.25))
        // Advance the paw's rubbing stroke along the wheel (direction follows the scroll).
        let step = min(0.55, 0.12 + abs(delta) * 0.05)
        scrollStrokePhase += delta > 0 ? step : -step
    }

    /// Called by the overlay window when cursor strokes across the cat's head or body.
    /// A small floating symbol above the head (e.g. ♥ when a friend pets this cat from afar).
    func spawnFloatingNote(_ note: String) {
        let hc = headCenter
        sparkles.append(PurrSparkle(position: CGPoint(x: hc.x + CGFloat.random(in: -30...22), y: hc.y - 58),
                                    velocity: CGPoint(x: CGFloat.random(in: -8...8), y: -22),
                                    lifetime: 1.6, isSleepZ: false, note: note))
    }

    /// The sound output switched to headphones (true) or back to speakers (false).
    /// `animated: false` (at launch) just shows the final state without the paw gesture.
    func setHeadphonesConnected(_ connected: Bool, animated: Bool = true) {
        headphonesConnected = connected
        if !animated {
            earbudPhase = connected && CatSettings.shared.earbudAnimation ? .on : .off
            earbudProgress = 0
        }
    }

    var isEarbudGesture: Bool { earbudPhase == .puttingOn || earbudPhase == .takingOff }

    /// Where each AirPod should be drawn, or nil when the cat has none.
    var earbudPose: EarbudPose? {
        let u = earbudProgress
        switch earbudPhase {
        case .off:
            return nil
        case .on:
            return EarbudPose(inEar: 1, scale: 1, alpha: 1)
        case .puttingOn:
            // Pop into the paws, carried up, pressed into the ears around u = 0.40...0.52.
            let pop = u < 0.16 ? CatPhysics.easeOutBack(u / 0.16) : 1
            return EarbudPose(inEar: CatPhysics.smoothstep(0.40, 0.52, u), scale: pop, alpha: min(1, u / 0.06))
        case .takingOff:
            // Grabbed around u = 0.34...0.46, pulled out, then shrink away as the paws go back down.
            let away = 1 - CatPhysics.smoothstep(0.62, 0.88, u)
            return EarbudPose(inEar: 1 - CatPhysics.smoothstep(0.34, 0.46, u), scale: 0.35 + away * 0.65, alpha: away)
        }
    }

    /// Head center in canvas space (shared with the renderer so paws and AirPods track the head).
    var headCenter: CGPoint {
        let breath = sin(time * 2.1) * 1.6
        return CGPoint(
            x: CatPhysics.seatAnchor.x + bodyShiftX.value + bodyTilt.value * 36 + headYaw.value * 7.5,
            y: 126 + headOffsetY.value + headPitch.value * 4.8 + breath * 1.1
        )
    }

    /// Canvas point where an AirPod sits: in the ear opening (the lower middle of the ear triangle,
    /// just above where it meets the head), following the 2.5D head turn.
    func earbudAnchor(isLeft: Bool) -> CGPoint { earbudAnchor3D(isLeft: isLeft).pt }

    /// Same, plus depth: z < 0 means the bud has turned to the far side of the head.
    /// The bud is a fixed blend of the ear's corners (inner base, outer base, tip), so it stays inside
    /// the ear as it folds back during petting or twitches.
    func earbudAnchor3D(isLeft: Bool) -> (pt: CGPoint, z: CGFloat) {
        let e = earCornersNow(isLeft: isLeft)
        let (wi, wo, wt): (CGFloat, CGFloat, CGFloat) = (0.30, 0.216, 0.484)   // = (±23, -31) at rest
        let p = CatRenderer.projectHead3D(x: wi * e.inner.x + wo * e.outer.x + wt * e.tip.x,
                                          y: wi * e.inner.y + wo * e.outer.y + wt * e.tip.y,
                                          z: wi * e.inner.z + wo * e.outer.z + wt * e.tip.z,
                                          yawRad: headYaw.value * 0.50, pitchRad: headPitch.value * 0.36)
        let c = cos(headTilt.value), s = sin(headTilt.value)
        let hc = headCenter
        return (CGPoint(x: hc.x + p.pt.x * c - p.pt.y * s, y: hc.y + p.pt.x * s + p.pt.y * c), p.z)
    }

    /// How far the ear leans away from its resting angle; the in-ear bud tilts with it.
    func earbudLean(isLeft: Bool) -> CGFloat {
        earCornersNow(isLeft: isLeft).angle - (isLeft ? -0.06 : 0.06)
    }

    /// Same inputs as the renderer's ear drawing (see CatRenderer.drawEarsLayer).
    private func earCornersNow(isLeft: Bool) -> (inner: CatRenderer.EarPoint, outer: CatRenderer.EarPoint, tip: CatRenderer.EarPoint, angle: CGFloat) {
        let twitch = earTwitch.value * 0.045
        return CatRenderer.earCorners(isLeft: isLeft, fold: earFold.value, twitch: isLeft ? twitch : -twitch)
    }

    /// An AirPod held in a paw sits at the paw's inner top, so the paw stays outside the bud
    /// (and the raised arm stays clear of the eyes).
    static func earbudHoldOffset(isLeft: Bool) -> CGPoint { CGPoint(x: isLeft ? 8 : -8, y: -5) }

    static func smoothstep(_ a: CGFloat, _ b: CGFloat, _ x: CGFloat) -> CGFloat {
        let t = max(0, min(1, (x - a) / (b - a)))
        return t * t * (3 - 2 * t)
    }

    static func easeOutBack(_ x: CGFloat) -> CGFloat {
        let t = max(0, min(1, x)) - 1
        return 1 + 2.4 * t * t * t + 1.4 * t * t
    }

    private func updateEarbudPhase(now: CFTimeInterval, dt: CGFloat) {
        let wants = headphonesConnected && CatSettings.shared.earbudAnimation
        switch earbudPhase {
        case .off where wants, .on where !wants:
            earbudPhase = wants ? .puttingOn : .takingOff
            earbudPhaseStart = now
            sleepiness = 0
        case .puttingOn, .takingOff:
            let duration = earbudPhase == .puttingOn ? CatPhysics.earbudPutOnDuration : CatPhysics.earbudTakeOffDuration
            let previous = earbudProgress
            earbudProgress = CGFloat(min(1, (now - earbudPhaseStart) / duration))
            if earbudPhase == .puttingOn && previous < 0.5 && earbudProgress >= 0.5 {
                earTwitch.velocity += 34   // ears flick as the buds go in
            }
            if earbudPhase == .takingOff && previous < 0.4 && earbudProgress >= 0.4 {
                earTwitch.velocity -= 26
            }
            if earbudProgress >= 1 {
                earbudPhase = earbudPhase == .puttingOn ? .on : .off
                earbudProgress = 0
                if earbudPhase == .off {
                    for paw in [leftPaw.point, rightPaw.point] {   // little poof where they were tucked away
                        sparkles.append(PurrSparkle(position: CGPoint(x: paw.x, y: paw.y - 8),
                                                    velocity: CGPoint(x: 0, y: -20), isSleepZ: false))
                    }
                }
            }
        default:
            break
        }

        // Music notes drift out of the ears while something is playing through the headphones.
        if earbudPhase == .on && headphonesPlaying && sleepiness < 0.5 {
            musicNoteCooldown -= dt
            if musicNoteCooldown <= 0 {
                musicNoteCooldown = CGFloat.random(in: 1.5...2.6)
                let isLeft = Bool.random()
                let sign: CGFloat = isLeft ? -1 : 1
                let ear = earbudAnchor(isLeft: isLeft)
                sparkles.append(PurrSparkle(position: CGPoint(x: ear.x + sign * 8 - 4, y: ear.y - 16),
                                            velocity: CGPoint(x: sign * 13, y: -20),
                                            lifetime: 1.7, isSleepZ: false, note: Bool.random() ? "♪" : "♫"))
            }
        }
    }

    func registerPetStroke(canvasPoint: CGPoint, delta: CGFloat, isHead: Bool, at timestamp: CFTimeInterval? = nil) {
        guard CatSettings.shared.petInteraction else { return }
        let now = timestamp ?? CACurrentMediaTime()

        // Petting must be deliberate: a cursor just passing over the cat shouldn't count.
        // A pause longer than the stroke gap starts a new stroke.
        if now - lastStrokeTime > CatPhysics.petStrokeGap {
            petStrokeStartTime = now
            if !isCurrentlyPetting { petStrokeAccumulator = 0 }
        }
        lastStrokeTime = now
        petStrokeAccumulator += delta
        if !isCurrentlyPetting {
            guard now - petStrokeStartTime >= CatPhysics.petEngageDuration,
                  petStrokeAccumulator >= CatPhysics.petEngageDistance else { return }
            isCurrentlyPetting = true
            totalPetSessions += 1
            UserDefaults.standard.set(totalPetSessions, forKey: "KuroCat.PetCount")
        }

        lastPetTime = now
        petCursorCanvasPos = canvasPoint
        sleepiness = 0

        let energy = min(delta / 26.0, 0.35)
        if isHead {
            petHeadIntensity = min(1.0, petHeadIntensity + energy + 0.08)
            petBodyIntensity = max(petBodyIntensity, petHeadIntensity * 0.6)
        } else {
            petBodyIntensity = min(1.0, petBodyIntensity + energy + 0.07)
        }

        // Gentle nuzzle toward cursor
        let dx = (canvasPoint.x - CatPhysics.seatAnchor.x) / 60.0
        headTilt.target = max(-0.18, min(0.18, dx * 0.16))
        headOffsetY.velocity += 18
    }

    private func spawnFloatingKeycap(label: String, near pt: CGPoint, now: CFTimeInterval) {
        if now - lastBadgeTime < 0.38 {
            badgeCascadeIndex = (badgeCascadeIndex + 1) % 6
        } else {
            badgeCascadeIndex = 0
        }
        lastBadgeTime = now

        // Spread fast bursts gracefully so they stay readable like "F O J F E A"
        let spreadOffsets: [CGFloat] = [-10, 14, -22, 26, 2, 36]
        let dx = spreadOffsets[badgeCascadeIndex] + CGFloat.random(in: -3...3)
        let startX = max(55, min(315, pt.x + dx * 0.45))
        let startY = pt.y - 26 - CGFloat(badgeCascadeIndex % 2) * 8

        let badge = FloatingKeycap(
            id: nextBadgeID,
            label: label,
            position: CGPoint(x: startX, y: startY),
            velocity: CGPoint(x: dx * 0.65, y: -36 - CGFloat.random(in: 0...14)),
            rotation: CGFloat.random(in: -0.07...0.07)
        )
        nextBadgeID += 1
        floatingKeycaps.append(badge)
        if floatingKeycaps.count > 16 {
            floatingKeycaps.removeFirst(floatingKeycaps.count - 16)
        }
    }

    // MARK: - 60 FPS Simulation Step

    func step(dt rawDt: CGFloat, windowFrameInScreen: CGRect, scripted: ScriptedFrameInput? = nil) {
        let dt = min(rawDt, 1.0 / 25.0)
        time += dt

        let engine = InputEngine.shared
        if scripted == nil {
            engine.pollMouse(dt: dt)
        }

        let now = scripted?.now ?? CACurrentMediaTime()
        let activeKeys = scripted?.activeKeys ?? engine.activeKeys
        let orderedKeys = scripted?.orderedKeys ?? engine.pressedOrder
        let curMouseVel = scripted?.mouseVelocity ?? engine.mouseVelocity
        let curMouseLoc = scripted?.mouseScreenPos ?? engine.lastMouseLocation
        let curLeftMouse = scripted?.leftMouseDown ?? engine.leftMouseDown
        let curRightMouse = scripted?.rightMouseDown ?? engine.rightMouseDown
        let curLastMouseMove = scripted?.lastMouseMoveTime ?? engine.lastMouseMoveTime
        let curLastMouseClick = scripted?.lastMouseClickTime ?? engine.lastMouseClickTime
        let curLastKeyTime = scripted?.lastKeyActivityTime ?? engine.lastKeyActivityTime

        // 1. Decay petting intensity gracefully when hand stops moving
        if now - lastPetTime > 0.28 {
            petHeadIntensity = max(0, petHeadIntensity - dt * 1.4)
            petBodyIntensity = max(0, petBodyIntensity - dt * 1.2)
            if petHeadIntensity < 0.05 && petBodyIntensity < 0.05 {
                // Progress toward the next petting session is reset in registerPetStroke
                // (after a pause), not here: this runs every frame before petting engages.
                isCurrentlyPetting = false
            }
        } else if (petHeadIntensity > 0.55 || petBodyIntensity > 0.55) {
            petSparkleCooldown -= dt
            if petSparkleCooldown <= 0 {
                petSparkleCooldown = 0.42
                let sx = CatPhysics.seatAnchor.x + CGFloat.random(in: -44...44)
                let sy = 88 + CGFloat.random(in: -14...18)
                sparkles.append(PurrSparkle(
                    position: CGPoint(x: sx, y: sy),
                    velocity: CGPoint(x: CGFloat.random(in: -10...10), y: -24),
                    isSleepZ: false
                ))
            }
        }

        // 2. Sleepy idle detection
        let lastAnyActivity = max(curLastKeyTime, max(curLastMouseMove, lastPetTime))
        let idleSeconds = lastAnyActivity > 0 ? (now - lastAnyActivity) : 0
        if activeKeys.isEmpty && idleSeconds > 45 {
            let targetSleep = min(1.0, CGFloat((idleSeconds - 45) / 35.0))
            sleepiness += (targetSleep - sleepiness) * dt * 1.5
            if sleepiness > 0.8 {
                sleepZCooldown -= dt
                if sleepZCooldown <= 0 {
                    sleepZCooldown = 2.6
                    sparkles.append(PurrSparkle(
                        position: CGPoint(x: CatPhysics.seatAnchor.x + 42, y: 100),
                        velocity: CGPoint(x: 12, y: -18),
                        isSleepZ: true
                    ))
                }
            }
        } else {
            sleepiness = max(0, sleepiness - dt * 5.0)
        }

        updateEarbudPhase(now: now, dt: dt)

        // 2b. Fast Mouse Movement -> Hunting / Stalking Mode (dilated round boba pupils + low crouch!)
        let mouseSpeed = hypot(curMouseVel.dx, curMouseVel.dy)
        let isFastMove = mouseSpeed > 460 && petHeadIntensity < 0.25 && petBodyIntensity < 0.25
        // Ordinary cursor moves across the screen easily exceed 460 pt/s, so only sustained
        // fast waving counts: brief slowdowns (direction reversals) don't reset the timer.
        if isFastMove {
            if now - lastFastMoveTime > CatPhysics.huntGap { fastMoveStartTime = now }
            lastFastMoveTime = now
        }
        if isFastMove && now - fastMoveStartTime >= CatPhysics.huntArmDuration {
            // Build up smoothly & responsively with fast mouse waving (laser-pointer effect!)
            let boost = min(1.0, (mouseSpeed - 460) / 1100.0)
            huntIntensity.target = min(1.0, huntIntensity.target + (0.45 + boost * 1.15) * dt)
            sleepiness = 0
        } else {
            // Calm down gently once cursor stops darting
            huntIntensity.target = max(0.0, huntIntensity.target - dt * 0.65)
        }
        huntIntensity.step(dt: dt, stiffness: 85, damping: 16)
        let hunt = max(0, min(1, huntIntensity.value))

        // 3. Determine limb targets (Left Paw, Right Paw, 3rd-Key Hind Foot!)
        let tapHoldWindow: CFTimeInterval = 0.14
        let mouseRecent = (now - curLastMouseMove < 2.2) || curLeftMouse || curRightMouse
        let mouseActivelyUsed = (now - curLastMouseMove < 0.75) || curLeftMouse || curRightMouse

        // Partition active keys
        var leftCandidates: [PhysicalKey] = []
        var rightCandidates: [PhysicalKey] = []
        for k in orderedKeys {
            if k.prefersLeftPaw { leftCandidates.append(k) }
            else { rightCandidates.append(k) }
        }

        var leftKey: PhysicalKey? = leftCandidates.last
        var rightKey: PhysicalKey? = rightCandidates.last
        var footKey: PhysicalKey? = nil

        // If 2+ keys are pressed on the same side, let the free paw help —
        // UNLESS viewer-left paw (cat's right hand) is actively driving the mouse!
        if leftKey == nil && rightCandidates.count >= 2 && !mouseActivelyUsed {
            let sorted = rightCandidates.sorted {
                KeyboardLayout.canvasPoint(for: $0).x < KeyboardLayout.canvasPoint(for: $1).x
            }
            leftKey = sorted.first
            rightKey = sorted.last
        } else if rightKey == nil && leftCandidates.count >= 2 {
            let sorted = leftCandidates.sorted {
                KeyboardLayout.canvasPoint(for: $0).x < KeyboardLayout.canvasPoint(for: $1).x
            }
            leftKey = sorted.first
            rightKey = sorted.last
        }

        // 3+ simultaneous keys (or 2+ keys while other hand is busy on the mouse!) -> HIND FOOT EASTER EGG!!
        if CatSettings.shared.footEasterEgg {
            if orderedKeys.count >= 3 {
                footKey = orderedKeys.first { $0 != leftKey && $0 != rightKey }
            } else if mouseActivelyUsed && leftKey == nil && rightCandidates.count >= 2 {
                footKey = rightCandidates.first { $0 != rightKey }
            }
        }

        // Keep quick taps visible for a tiny snappy window
        if leftKey == nil, let rec = recentLeftKey, now - rec.time < tapHoldWindow {
            leftKey = rec.key
        }
        if rightKey == nil, let rec = recentRightKey, now - rec.time < tapHoldWindow {
            rightKey = rec.key
        }

        // Subtle mini-mouse movement driven by global cursor velocity
        let targetMouseDx = max(-5.5, min(5.5, curMouseVel.dx * 0.0045))
        let targetMouseDy = max(-4.5, min(4.5, -curMouseVel.dy * 0.0045))
        mouseOffset.target = CGPoint(x: targetMouseDx, y: targetMouseDy)
        mouseOffset.step(dt: dt, stiffness: 120, damping: 16)
        mousePressLeft.step(dt: dt, stiffness: 400, damping: 24)
        mousePressRight.step(dt: dt, stiffness: 400, damping: 24)
        scrollReach.target = (now - lastScrollTime < CatPhysics.scrollHoldDuration) ? 1 : 0
        scrollReach.step(dt: dt, stiffness: 220, damping: 22)

        let liveMouseCenter = CGPoint(
            x: CatPhysics.mouseBaseCenter.x + mouseOffset.point.x,
            y: CatPhysics.mouseBaseCenter.y + mouseOffset.point.y
        )

        // Left paw target
        if let lk = leftKey {
            leftPaw.target = KeyboardLayout.canvasPoint(for: lk)
            leftPawTyping = true
            leftPawOnMouse = false
        } else if mouseRecent && sleepiness < 0.3 {
            // While scrolling, slide from the palm rest (+2, -3) forward onto the wheel and rub
            // back and forth along it. The wheel axis is the mouse's 0.22 rad tilt: (-0.22, 0.98).
            let reach = max(0, min(1, scrollReach.value))
            let stroke = sin(scrollStrokePhase) * 3.2 * reach
            leftPaw.target = CGPoint(x: liveMouseCenter.x + 2 - reach * 3.0 - stroke * 0.22,
                                     y: liveMouseCenter.y - 3 + reach * 5.0 + stroke * 0.98)
            leftPawTyping = false
            leftPawOnMouse = true
        } else {
            leftPaw.target = CatPhysics.leftPawRest
            leftPawTyping = false
            leftPawOnMouse = false
        }

        // Right paw target
        if let rk = rightKey {
            rightPaw.target = KeyboardLayout.canvasPoint(for: rk)
            rightPawTyping = true
        } else {
            rightPaw.target = CatPhysics.rightPawRest
            rightPawTyping = false
        }

        // AirPods gesture: both paws leave the keyboard/mouse, rise beside the head, press the buds
        // into the ears (or pull them out), then return. Paw targets keep the held bud on the ear.
        if isEarbudGesture {
            let u = earbudProgress
            let putting = earbudPhase == .puttingOn
            let (up, atEar) = putting ? (u < 0.56, u >= 0.36) : (u < 0.56, u < 0.34)
            for isLeft in [true, false] {
                let sign: CGFloat = isLeft ? -1 : 1
                let ear = earbudAnchor(isLeft: isLeft)
                let offset = CatPhysics.earbudHoldOffset(isLeft: isLeft)
                let hold = CGPoint(x: ear.x - offset.x, y: ear.y - offset.y)
                let target: CGPoint
                if !up {
                    target = isLeft ? CatPhysics.leftPawRest : CatPhysics.rightPawRest
                } else if atEar {
                    target = hold
                } else {
                    target = CGPoint(x: hold.x + sign * 13, y: hold.y + 9)   // just outside the ear
                }
                if isLeft { leftPaw.target = target } else { rightPaw.target = target }
            }
            leftPawTyping = false
            rightPawTyping = false
            leftPawOnMouse = false
        }

        // Hind foot target (3rd simultaneous key!)
        if let fk = footKey {
            hindFoot.target = KeyboardLayout.canvasPoint(for: fk)
            hindFootActive.target = 1.0
        } else {
            hindFoot.target = CatPhysics.hindFootRest
            hindFootActive.target = 0.0
        }

        leftPaw.step(dt: dt, stiffness: 360, damping: 24)
        rightPaw.step(dt: dt, stiffness: 360, damping: 24)
        hindFoot.step(dt: dt, stiffness: 320, damping: 22)
        hindFootActive.step(dt: dt, stiffness: 260, damping: 22)

        // 4. Expressive body lean, squash, head & ears
        var targetShiftX: CGFloat = 0
        var targetBodyTilt: CGFloat = 0
        if leftPawOnMouse && !rightPawTyping {
            targetShiftX = -10
            targetBodyTilt = -0.075
        } else if leftPawOnMouse && rightPawTyping {
            targetShiftX = -3
            targetBodyTilt = (rightPaw.point.x - 196) * 0.0008
        } else if leftPawTyping || rightPawTyping {
            let avgX = ((leftPawTyping ? leftPaw.point.x : 184) + (rightPawTyping ? rightPaw.point.x : 184)) * 0.5
            targetShiftX = (avgX - 184) * 0.14
            targetBodyTilt = (avgX - 184) * 0.0011
        }
        bodyShiftX.target = max(-16, min(16, targetShiftX))
        bodyTilt.target = max(-0.12, min(0.12, targetBodyTilt))
        // Coiled low hunting crouch when chasing a fast-moving mouse!
        bodySquash.target = 1.0 - hunt * 0.11

        headOffsetY.target = (leftPawTyping || rightPawTyping) ? 3.5 : (sleepiness * 5.0 + hunt * 10.5)
        earFold.target = max(petHeadIntensity * 0.7, petBodyIntensity * 0.45) + sleepiness * 0.25 - hunt * 0.18
        earTwitch.target = 0

        bodyShiftX.step(dt: dt, stiffness: 140, damping: 18)
        bodyTilt.step(dt: dt, stiffness: 140, damping: 18)
        bodySquash.step(dt: dt, stiffness: 280, damping: 18)
        headOffsetY.step(dt: dt, stiffness: 180, damping: 19)
        earFold.step(dt: dt, stiffness: 120, damping: 16)
        earTwitch.step(dt: dt, stiffness: 320, damping: 16)

        // 5. 2.5D Head Turning (Yaw + Pitch) + Eye Tracking!
        // Head smoothly turns in pseudo-3D toward the mouse cursor (or subtly nods down toward keys while typing).
        let typingRecently = !activeKeys.isEmpty || (now - curLastKeyTime < 0.65)
        let mouseMovedRecently = (now - curLastMouseMove < 1.2)
        let eyeOriginCanvas = CGPoint(x: CatPhysics.seatAnchor.x + bodyShiftX.value, y: 124 + headOffsetY.value)

        // Compute normalized vector from cat head to real macOS screen cursor
        let scale = max(0.2, windowFrameInScreen.width / CatPhysics.canvasSize.width)
        let headScreenX = windowFrameInScreen.minX + eyeOriginCanvas.x * scale
        let headScreenY = windowFrameInScreen.maxY - eyeOriginCanvas.y * scale
        let cursor = curMouseLoc
        let curDx = (cursor.x - headScreenX) / 290.0
        let curDy = -(cursor.y - headScreenY) / 260.0
        let curMag = hypot(curDx, curDy)
        let clampedCur = min(1.0, curMag)
        let cursorNX = curMag > 0.001 ? (curDx / curMag) * clampedCur : 0
        let cursorNY = curMag > 0.001 ? (curDy / curMag) * clampedCur : 0

        let lookTargetX: CGFloat
        let lookTargetY: CGFloat
        if typingRecently && !mouseMovedRecently, let keyPt = lastTypedPoint {
            let kdx = max(-0.85, min(0.85, (keyPt.x - eyeOriginCanvas.x) / 95.0))
            let kdy = max(-0.3, min(0.92, (keyPt.y - eyeOriginCanvas.y) / 125.0))
            lookTargetX = kdx
            lookTargetY = kdy
        } else if typingRecently && mouseMovedRecently, let keyPt = lastTypedPoint {
            // Blend mouse & typing naturally when gaming with mouse + keyboard!
            let kdx = max(-0.85, min(0.85, (keyPt.x - eyeOriginCanvas.x) / 95.0))
            lookTargetX = cursorNX * 0.72 + kdx * 0.28
            lookTargetY = cursorNY * 0.72 + 0.25
        } else {
            lookTargetX = cursorNX
            lookTargetY = cursorNY
        }

        // Head turns smoothly in 2.5D (yaw & pitch) — locks on faster during hunting mode!
        if sleepiness > 0.45 {
            headYaw.target = 0
            headPitch.target = 0.45 * sleepiness
        } else {
            headYaw.target = max(-1.0, min(1.0, lookTargetX))
            headPitch.target = max(-1.0, min(1.0, lookTargetY))
        }
        let headStiff: CGFloat = 88 + hunt * 75
        let headDamp: CGFloat = 14.5 + hunt * 4.5
        headYaw.step(dt: dt, stiffness: headStiff, damping: headDamp)
        headPitch.step(dt: dt, stiffness: headStiff, damping: headDamp)

        // Subtle natural head roll coupled to head turning (curious tilt / locked-on hunt!)
        if now - lastPetTime > 0.28 {
            headTilt.target = bodyTilt.target * 0.45 + headYaw.value * (0.11 + hunt * 0.05) - headYaw.value * headPitch.value * 0.04
        }
        headTilt.step(dt: dt, stiffness: 130, damping: 16)

        // Eyes lead slightly ahead of the 2.5D head turn
        eyeGaze.target = CGPoint(x: lookTargetX * 0.88, y: lookTargetY * 0.85)
        eyeGaze.step(dt: dt, stiffness: 210 + hunt * 80, damping: 21)

        // Pupil dilation
        if hindFootActive.value > 0.3 {
            pupilScale.target = 1.24
        } else if now - curLastKeyTime > 0.45 && now - curLastMouseClick > 0.45 {
            pupilScale.target = 1.0
        }
        pupilScale.step(dt: dt, stiffness: 150, damping: 18)

        // 6. Blinking (cats lock eyes without blinking during hunting mode!)
        if isBlinking {
            blinkTimer += dt
            let blinkDuration: CGFloat = 0.14
            if blinkTimer >= blinkDuration {
                isBlinking = false
                blinkProgress = 0
                nextBlinkTime = time + CGFloat.random(in: 2.6...5.6)
            } else {
                let p = blinkTimer / blinkDuration
                blinkProgress = sin(p * .pi)
            }
        } else if time >= nextBlinkTime && sleepiness < 0.5 && petHeadIntensity < 0.3 && hunt < 0.35 {
            isBlinking = true
            blinkTimer = 0
        }

        // 7. Tail wag speed reacts organically to energy / petting / hunting tension!
        let kpmBoost = min(2.4, CGFloat(kpmOverride ?? engine.currentKPM) / 160.0)
        let petBoost = max(petHeadIntensity, petBodyIntensity) * 1.4
        let huntBoost = hunt * 2.8
        let tailSpeed: CGFloat = 1.85 + kpmBoost + petBoost + huntBoost - sleepiness * 1.1
        tailPhase += dt * tailSpeed

        // 8. Update per-keycap glows
        let visualActive = Set(activeKeys.map { $0.visualKey })
        for (k, v) in keyGlow {
            if visualActive.contains(k) {
                keyGlow[k] = 1.0
            } else {
                let next = v - dt * 4.5
                if next <= 0.01 { keyGlow.removeValue(forKey: k) }
                else { keyGlow[k] = next }
            }
        }
        for vk in visualActive {
            keyGlow[vk] = 1.0
        }

        // 9. Update floating keycaps
        for i in floatingKeycaps.indices.reversed() {
            floatingKeycaps[i].age += dt
            floatingKeycaps[i].position.x += floatingKeycaps[i].velocity.x * dt
            floatingKeycaps[i].position.y += floatingKeycaps[i].velocity.y * dt
            floatingKeycaps[i].velocity.x *= (1.0 - dt * 2.4)
            floatingKeycaps[i].velocity.y *= (1.0 - dt * 1.6)
            if floatingKeycaps[i].age >= floatingKeycaps[i].lifetime {
                floatingKeycaps.remove(at: i)
            }
        }

        // 10. Update purr / sleep particles
        for i in sparkles.indices.reversed() {
            sparkles[i].age += dt
            sparkles[i].position.x += sparkles[i].velocity.x * dt
            sparkles[i].position.y += sparkles[i].velocity.y * dt
            if sparkles[i].age >= sparkles[i].lifetime {
                sparkles.remove(at: i)
            }
        }
    }
}
