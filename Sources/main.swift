import Foundation
import AppKit

// `Whisku --verify-update <Whisku.app>`: exit 0 if that app is signed like this one, so the
// in-app updater would install it (used by scripts/release.sh before publishing).
if let idx = CommandLine.arguments.firstIndex(of: "--verify-update"), idx + 1 < CommandLine.arguments.count {
    let ok = AppDelegate.isSignedLikeThisApp(URL(fileURLWithPath: CommandLine.arguments[idx + 1]))
    print(ok ? "OK: signed with the same certificate" : "REFUSED: not signed like this app")
    exit(ok ? 0 : 1)
}

// Claude Code hook: tell the running Whisku that a session finished or needs you, then exit.
if CommandLine.arguments.contains("--claude-hook") {
    ClaudeHook.runFromCommandLine(arguments: CommandLine.arguments)
}

// Support headless multi-pose preview rendering for visual verification
if let idx = CommandLine.arguments.firstIndex(of: "--render-preview"),
   idx + 1 < CommandLine.arguments.count {
    let outPath = CommandLine.arguments[idx + 1]
    renderPreviewSheet(to: outPath, forceSiamese: false)
    exit(0)
}

if let idx = CommandLine.arguments.firstIndex(of: "--render-siamese-preview"),
   idx + 1 < CommandLine.arguments.count {
    let outPath = CommandLine.arguments[idx + 1]
    renderPreviewSheet(to: outPath, forceSiamese: true)
    exit(0)
}

// Support headless Live-style animated frame sequence export (for GIF & MP4 generation)
if let idx = CommandLine.arguments.firstIndex(of: "--render-demos"),
   idx + 1 < CommandLine.arguments.count {
    let outDir = CommandLine.arguments[idx + 1]
    renderAllLiveDemos(to: outDir)
    exit(0)
}

// Support custom-background full feature showcase video (skin switch, typing, hunting boba eyes, petting, combo!)
if let idx = CommandLine.arguments.firstIndex(of: "--render-showcase-video"),
   idx + 2 < CommandLine.arguments.count {
    let bgPath = CommandLine.arguments[idx + 1]
    let outDir = CommandLine.arguments[idx + 2]
    renderCustomShowcaseVideo(bgPath: bgPath, outDir: outDir)
    exit(0)
}

func renderPreviewSheet(to path: String, forceSiamese: Bool = false) {
    let prevCoat = CatSettings.shared.coatTheme
    let prevEye = CatSettings.shared.eyeTheme
    let prevKb = CatSettings.shared.keyboardTheme
    if forceSiamese {
        CatSettings.shared.coatTheme = .siamese
        CatSettings.shared.eyeTheme = .siameseBlue
        CatSettings.shared.keyboardTheme = .classicMilk
    } else {
        CatSettings.shared.coatTheme = .classicBlack
        CatSettings.shared.eyeTheme = .neonChartreuse
        CatSettings.shared.keyboardTheme = .classicMilk
    }
    defer {
        CatSettings.shared.coatTheme = prevCoat
        CatSettings.shared.eyeTheme = prevEye
        CatSettings.shared.keyboardTheme = prevKb
    }
    let cellW = 360
    let cellH = 320
    let scale = 2
    let cols = 3
    let rows = 2
    let totalW = cellW * cols * scale
    let totalH = cellH * rows * scale

    let cs = CGColorSpaceCreateDeviceRGB()
    guard let ctx = CGContext(
        data: nil,
        width: totalW,
        height: totalH,
        bitsPerComponent: 8,
        bytesPerRow: 0,
        space: cs,
        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
    ) else { return }

    // Flip context to match top-down NSView coordinates
    ctx.translateBy(x: 0, y: CGFloat(totalH))
    ctx.scaleBy(x: CGFloat(scale), y: -CGFloat(scale))

    // Soft desktop wallpaper backdrop matching reference aesthetic
    let bgColors = [
        NSColor(calibratedRed: 0.72, green: 0.74, blue: 0.71, alpha: 1.0).cgColor,
        NSColor(calibratedRed: 0.54, green: 0.56, blue: 0.55, alpha: 1.0).cgColor
    ] as CFArray
    if let grad = CGGradient(colorsSpace: cs, colors: bgColors, locations: [0, 1]) {
        ctx.drawLinearGradient(grad, start: .zero, end: CGPoint(x: cellW * cols, y: cellH * rows), options: [])
    }

    let scenarios: [(String, (CatPhysics) -> Void)] = [
        ("1. Idle (Seamless Thick Neck)", { p in
            p.leftPaw.x.value = CatPhysics.leftPawRest.x
            p.leftPaw.y.value = CatPhysics.leftPawRest.y
            p.rightPaw.x.value = CatPhysics.rightPawRest.x
            p.rightPaw.y.value = CatPhysics.rightPawRest.y
            p.headYaw.value = 0
            p.headPitch.value = 0
            p.eyeGaze.x.value = 0
            p.eyeGaze.y.value = 0
        }),
        ("2. 2.5D Head Turn Left-Up", { p in
            p.leftPaw.x.value = CatPhysics.mouseBaseCenter.x + 2
            p.leftPaw.y.value = CatPhysics.mouseBaseCenter.y - 3
            p.bodyShiftX.value = -9
            p.bodyTilt.value = -0.07
            p.headYaw.value = -0.88
            p.headPitch.value = -0.62
            p.headTilt.value = -0.11
            p.eyeGaze.x.value = -0.78
            p.eyeGaze.y.value = -0.58
        }),
        ("3. 2.5D Head Turn Right-Down", { p in
            let burst: [PhysicalKey] = [.j, .o, .e, .a]
            for (i, k) in burst.enumerated() {
                p.triggerKeyDown(k)
                if i < p.floatingKeycaps.count {
                    p.floatingKeycaps[i].age = 0.22 + CGFloat(burst.count - 1 - i) * 0.12
                }
            }
            let ptL = KeyboardLayout.canvasPoint(for: .o) // viewer-left = cat's right hand (O)
            let ptR = KeyboardLayout.canvasPoint(for: .f) // viewer-right = cat's left hand (F)
            p.leftPaw.x.value = ptL.x; p.leftPaw.y.value = ptL.y
            p.rightPaw.x.value = ptR.x; p.rightPaw.y.value = ptR.y
            p.headYaw.value = 0.72
            p.headPitch.value = 0.78
            p.headTilt.value = 0.09
            p.eyeGaze.x.value = 0.65
            p.eyeGaze.y.value = 0.72
        }),
        ("4. Hunting Mode! (Round Boba Eyes)", { p in
            p.leftPaw.x.value = CatPhysics.mouseBaseCenter.x + 2
            p.leftPaw.y.value = CatPhysics.mouseBaseCenter.y - 3
            p.huntIntensity.value = 1.0
            p.bodySquash.value = 0.89
            p.headOffsetY.value = 10.5
            p.earFold.value = -0.18
            p.headYaw.value = -0.55
            p.headPitch.value = -0.25
            p.headTilt.value = -0.09
            p.eyeGaze.x.value = -0.50
            p.eyeGaze.y.value = -0.22
        }),
        ("5. Gaming 3-Key + Hind Foot!", { p in
            p.triggerKeyDown(.w)
            p.triggerKeyDown(.leftShift)
            p.triggerKeyDown(.space)
            for i in p.floatingKeycaps.indices { p.floatingKeycaps[i].age = 0.20 }
            let ptL = KeyboardLayout.canvasPoint(for: .space)
            let ptR = KeyboardLayout.canvasPoint(for: .w)
            let ptF = KeyboardLayout.canvasPoint(for: .leftShift)
            p.leftPaw.x.value = ptL.x; p.leftPaw.y.value = ptL.y
            p.rightPaw.x.value = ptR.x; p.rightPaw.y.value = ptR.y
            p.hindFoot.x.value = ptF.x; p.hindFoot.y.value = ptF.y
            p.hindFootActive.value = 1.0
            p.pupilScale.value = 1.24
            p.headPitch.value = 0.55
            p.eyeGaze.x.value = 0.15; p.eyeGaze.y.value = 0.60
        }),
        ("6. Petting (Happy Closed Eyes)", { p in
            p.leftPaw.x.value = CatPhysics.mouseBaseCenter.x + 2
            p.leftPaw.y.value = CatPhysics.mouseBaseCenter.y - 3
            p.bodyShiftX.value = -7
            p.bodyTilt.value = -0.05
            p.headYaw.value = -0.25
            p.headTilt.value = -0.08
            p.earFold.value = 0.65
            // Simulate 1.5s of stroking so the deliberate-petting delay is passed.
            let strokeStart = CACurrentMediaTime() - 1.5
            for i in 0...30 {
                p.registerPetStroke(canvasPoint: CGPoint(x: 184, y: 120), delta: 16, isHead: true,
                                    at: strokeStart + Double(i) * 0.05)
            }
            p.triggerKeyDown(.s)
            p.floatingKeycaps[0].age = 0.22
        })
    ]

    for (index, (label, configure)) in scenarios.enumerated() {
        let col = index % cols
        let row = index / cols
        let ox = CGFloat(col * cellW)
        let oy = CGFloat(row * cellH)

        let physics = CatPhysics()
        configure(physics)

        ctx.saveGState()
        ctx.translateBy(x: ox, y: oy)

        // Subtle panel divider
        ctx.setStrokeColor(NSColor(calibratedWhite: 1.0, alpha: 0.25).cgColor)
        ctx.setLineWidth(1.0)
        ctx.stroke(CGRect(x: 0.5, y: 0.5, width: CGFloat(cellW) - 1, height: CGFloat(cellH) - 1))

        CatRenderer.draw(in: ctx, physics: physics, showPermissionPill: false)

        // Label badge top-left
        let attrs: [NSAttributedString.Key: Any] = [
            .font: NSFont.systemFont(ofSize: 12, weight: .semibold),
            .foregroundColor: NSColor.white
        ]
        ctx.setFillColor(NSColor(calibratedWhite: 0.08, alpha: 0.72).cgColor)
        ctx.fill(CGRect(x: 8, y: 8, width: 215, height: 22))
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(cgContext: ctx, flipped: true)
        NSAttributedString(string: label, attributes: attrs).draw(at: CGPoint(x: 14, y: 11))
        NSGraphicsContext.restoreGraphicsState()

        ctx.restoreGState()
    }

    guard let cgImg = ctx.makeImage() else { return }
    let rep = NSBitmapImageRep(cgImage: cgImg)
    if let png = rep.representation(using: .png, properties: [:]) {
        try? png.write(to: URL(fileURLWithPath: path))
    }
}

// MARK: - Live-Style Animated Demo Exporter (30fps frames -> GIF / MP4)

private func drawDemoBackdrop(in ctx: CGContext, width: CGFloat, height: CGFloat) {
    let cs = CGColorSpaceCreateDeviceRGB()
    let bgColors = [
        NSColor(calibratedRed: 0.78, green: 0.80, blue: 0.77, alpha: 1.0).cgColor,
        NSColor(calibratedRed: 0.62, green: 0.65, blue: 0.63, alpha: 1.0).cgColor
    ] as CFArray
    if let grad = CGGradient(colorsSpace: cs, colors: bgColors, locations: [0, 1]) {
        ctx.drawLinearGradient(grad, start: CGPoint(x: 0, y: 0), end: CGPoint(x: width, y: height), options: [])
    }
    // Soft subtle vignette / studio light glow behind cat
    let glowColors = [
        NSColor(calibratedWhite: 1.0, alpha: 0.22).cgColor,
        NSColor(calibratedWhite: 1.0, alpha: 0.0).cgColor
    ] as CFArray
    if let rad = CGGradient(colorsSpace: cs, colors: glowColors, locations: [0, 1]) {
        let c = CGPoint(x: width * 0.5, y: height * 0.46)
        ctx.drawRadialGradient(rad, startCenter: c, startRadius: 10, endCenter: c, endRadius: width * 0.58, options: [])
    }
}

private func drawMacCursor(in ctx: CGContext, at pt: CGPoint, clicking: Bool) {
    ctx.saveGState()
    ctx.translateBy(x: pt.x, y: pt.y)
    if clicking {
        ctx.setFillColor(NSColor(calibratedRed: 0.79, green: 0.95, blue: 0.25, alpha: 0.45).cgColor)
        ctx.fillEllipse(in: CGRect(x: -11, y: -11, width: 22, height: 22))
    }
    let scale: CGFloat = clicking ? 0.90 : 1.0
    ctx.scaleBy(x: scale, y: scale)

    let arrow = CGMutablePath()
    arrow.move(to: CGPoint(x: 0, y: 0))
    arrow.addLine(to: CGPoint(x: 0, y: 18))
    arrow.addLine(to: CGPoint(x: 4.6, y: 14.2))
    arrow.addLine(to: CGPoint(x: 7.8, y: 20.5))
    arrow.addLine(to: CGPoint(x: 10.6, y: 19.2))
    arrow.addLine(to: CGPoint(x: 7.4, y: 12.8))
    arrow.addLine(to: CGPoint(x: 13.0, y: 12.8))
    arrow.closeSubpath()

    ctx.setFillColor(NSColor(calibratedWhite: 0.08, alpha: 1.0).cgColor)
    ctx.addPath(arrow)
    ctx.fillPath()
    ctx.setStrokeColor(NSColor.white.cgColor)
    ctx.setLineWidth(1.5)
    ctx.setLineJoin(.round)
    ctx.addPath(arrow)
    ctx.strokePath()
    ctx.restoreGState()
}

private func renderSequence(
    name: String,
    outRoot: String,
    durationSec: Double,
    fps: Int = 30,
    driver: (_ t: Double, _ dt: Double, _ physics: CatPhysics, _ state: inout ScriptedFrameInput, _ showCursor: inout Bool) -> Void
) {
    let dir = (outRoot as NSString).appendingPathComponent(name)
    try? FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true)

    let outW = 540
    let outH = 480
    let canvasW = CatPhysics.canvasSize.width   // 360
    let canvasH = CatPhysics.canvasSize.height  // 320
    let scaleX = CGFloat(outW) / canvasW
    let scaleY = CGFloat(outH) / canvasH

    // Virtual window frame in screen coordinates (origin at 0,0, width=360, height=320)
    let winFrame = CGRect(x: 0, y: 0, width: canvasW, height: canvasH)
    let physics = CatPhysics()

    var state = ScriptedFrameInput(
        now: 100.0,
        activeKeys: [],
        orderedKeys: [],
        mouseScreenPos: CGPoint(x: 184, y: 196),
        mouseVelocity: .zero,
        leftMouseDown: false,
        rightMouseDown: false,
        lastMouseMoveTime: 0,
        lastMouseClickTime: 0,
        lastKeyActivityTime: 0
    )

    let totalFrames = Int(durationSec * Double(fps))
    let subSteps = 2
    let subDt = 1.0 / Double(fps * subSteps)
    let cs = CGColorSpaceCreateDeviceRGB()

    for frameIdx in 0..<totalFrames {
        var showCursor = false
        for s in 0..<subSteps {
            let t = (Double(frameIdx) + Double(s) / Double(subSteps)) / Double(fps)
            state.now = 100.0 + t
            driver(t, subDt, physics, &state, &showCursor)
            physics.step(dt: CGFloat(subDt), windowFrameInScreen: winFrame, scripted: state)
        }

        guard let ctx = CGContext(
            data: nil,
            width: outW,
            height: outH,
            bitsPerComponent: 8,
            bytesPerRow: 0,
            space: cs,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        ) else { continue }

        ctx.translateBy(x: 0, y: CGFloat(outH))
        ctx.scaleBy(x: scaleX, y: -scaleY)

        drawDemoBackdrop(in: ctx, width: canvasW, height: canvasH)
        CatRenderer.draw(in: ctx, physics: physics, showPermissionPill: false)

        if showCursor {
            // Convert bottom-up virtual screen coords to top-down canvas coords
            let cPt = CGPoint(x: state.mouseScreenPos.x, y: canvasH - state.mouseScreenPos.y)
            drawMacCursor(in: ctx, at: cPt, clicking: state.leftMouseDown || state.rightMouseDown)
        }

        if let cgImg = ctx.makeImage() {
            let rep = NSBitmapImageRep(cgImage: cgImg)
            if let png = rep.representation(using: .png, properties: [:]) {
                let fn = String(format: "frame_%04d.png", frameIdx)
                try? png.write(to: URL(fileURLWithPath: (dir as NSString).appendingPathComponent(fn)))
            }
        }
    }
}

func renderAllLiveDemos(to outDir: String) {
    // Preserve user's saved themes; use classic iconic Xiaohongshu look for the demos
    let prevEye = CatSettings.shared.eyeTheme
    let prevKb = CatSettings.shared.keyboardTheme
    let prevBadges = CatSettings.shared.showKeyBadges
    let prevFoot = CatSettings.shared.footEasterEgg

    CatSettings.shared.eyeTheme = .neonChartreuse
    CatSettings.shared.keyboardTheme = .classicMilk
    CatSettings.shared.showKeyBadges = true
    CatSettings.shared.footEasterEgg = true

    defer {
        CatSettings.shared.eyeTheme = prevEye
        CatSettings.shared.keyboardTheme = prevKb
        CatSettings.shared.showKeyBadges = prevBadges
        CatSettings.shared.footEasterEgg = prevFoot
    }

    // 1. Typing animation (3.4s)
    let typingEvents: [(Double, Double, PhysicalKey)] = [
        (0.20, 0.36, .h), (0.38, 0.54, .e), (0.56, 0.72, .l), (0.74, 0.90, .l), (0.92, 1.08, .o),
        (1.14, 1.30, .space),
        (1.34, 1.50, .k), (1.52, 1.68, .u), (1.70, 1.86, .r), (1.88, 2.04, .o),
        (2.12, 2.28, .c), (2.30, 2.46, .a), (2.48, 2.64, .t), (2.70, 2.92, .enter)
    ]
    var firedTyping = Set<Int>()
    renderSequence(name: "01_typing", outRoot: outDir, durationSec: 3.4) { t, _, physics, state, showCursor in
        showCursor = false
        var active = Set<PhysicalKey>()
        var ordered: [PhysicalKey] = []
        for (idx, (t0, t1, key)) in typingEvents.enumerated() {
            if t >= t0 && t < t1 {
                active.insert(key)
                ordered.append(key)
                if !firedTyping.contains(idx) {
                    firedTyping.insert(idx)
                    state.lastKeyActivityTime = state.now
                    physics.triggerKeyDown(key, at: state.now)
                }
            }
        }
        state.activeKeys = active
        state.orderedKeys = ordered
    }

    // 2. Fast mouse waving -> 2.5D head turn + gradually dilating HUGE round boba hunting eyes! (4.4s)
    var prevMouse2 = CGPoint(x: 184, y: 270)
    renderSequence(name: "02_hunting", outRoot: outDir, durationSec: 4.4) { t, dt, _, state, showCursor in
        showCursor = true
        let mx: CGFloat
        let my: CGFloat
        let speedBoostMultiplier: CGFloat
        if t < 1.15 {
            // Smooth curious sweep left -> right above cat's head to showcase 2.5D head turning
            let p = t / 1.15
            mx = 184 + CGFloat(sin(p * .pi * 2.0)) * 138
            my = 278 + CGFloat(cos(p * .pi * 2.0)) * 18 // top of frame (canvas y ~24..60, never blocks face!)
            speedBoostMultiplier = 0.4 // gentle gaze first, slits stay narrow!
        } else if t < 3.25 {
            // Rapid laser-pointer figure-8 darting! Smoothly dilates pupils into HUGE round boba hunting eyes!
            let p = t - 1.15
            mx = 184 + CGFloat(sin(p * 11.5)) * 145
            my = 276 + CGFloat(sin(p * 23.0)) * 22
            speedBoostMultiplier = 2.6 // maps crisp virtual canvas motion to real-screen brisk mouse waving
        } else {
            // Cursor slows & stops -> pupils smoothly shrink back from big boba -> sleek slits!
            let p = min(1.0, (t - 3.25) / 1.15)
            let ease = CGFloat(1.0 - p)
            mx = 184 + CGFloat(sin(2.1 * 11.5)) * 145 * ease
            my = 276
            speedBoostMultiplier = 0.2
        }
        let cur = CGPoint(x: mx, y: my)
        let vx = (cur.x - prevMouse2.x) / CGFloat(dt) * speedBoostMultiplier
        let vy = (cur.y - prevMouse2.y) / CGFloat(dt) * speedBoostMultiplier
        prevMouse2 = cur
        state.mouseScreenPos = cur
        state.mouseVelocity = CGVector(dx: vx, dy: vy)
        if hypot(vx, vy) > 15 {
            state.lastMouseMoveTime = state.now
        }
        state.activeKeys = []
        state.orderedKeys = []
    }

    // 3. Simultaneous typing/gaming + dragging & clicking mouse! (4.0s)
    let comboEvents: [(Double, Double, PhysicalKey)] = [
        (0.25, 0.85, .w), (0.55, 1.15, .d),
        (1.25, 1.85, .a), (1.45, 1.90, .leftShift),
        (2.05, 2.70, .w), (2.15, 2.65, .leftShift), (2.25, 2.75, .space), // 3-key hind foot!
        (2.90, 3.25, .s), (3.15, 3.45, .f)
    ]
    var firedCombo = Set<Int>()
    var prevMouse3 = CGPoint(x: 85, y: 278)
    var prevClick3 = false
    renderSequence(name: "03_typing_and_mouse", outRoot: outDir, durationSec: 4.0) { t, dt, physics, state, showCursor in
        showCursor = true
        // Smooth active gaming mouse aiming & dragging cleanly in the upper sky (never overlapping face!)
        let mx = 180 + CGFloat(sin(t * 2.6)) * 135
        let my = 278 + CGFloat(sin(t * 4.2)) * 22
        let cur = CGPoint(x: mx, y: my)
        let vx = (cur.x - prevMouse3.x) / CGFloat(dt)
        let vy = (cur.y - prevMouse3.y) / CGFloat(dt)
        prevMouse3 = cur
        state.mouseScreenPos = cur
        state.mouseVelocity = CGVector(dx: vx * 0.45, dy: vy * 0.45) // purposeful aim, not frantic hunt
        state.lastMouseMoveTime = state.now

        let clicking = (t > 0.45 && t < 0.65) || (t > 1.55 && t < 1.85) || (t > 2.40 && t < 2.65)
        state.leftMouseDown = clicking
        if clicking != prevClick3 {
            prevClick3 = clicking
            if clicking { state.lastMouseClickTime = state.now }
            physics.triggerMouseClick(button: 0, isDown: clicking)
        }

        var active = Set<PhysicalKey>()
        var ordered: [PhysicalKey] = []
        for (idx, (t0, t1, key)) in comboEvents.enumerated() {
            if t >= t0 && t < t1 {
                active.insert(key)
                ordered.append(key)
                if !firedCombo.contains(idx) {
                    firedCombo.insert(idx)
                    state.lastKeyActivityTime = state.now
                    physics.triggerKeyDown(key, at: state.now)
                }
            }
        }
        state.activeKeys = active
        state.orderedKeys = ordered
    }
}

// MARK: - Custom Background Full Showcase Video (Black Cat -> Typing -> Hunting -> Petting -> Skin Switch -> Siamese!)

private func drawOpenHandCursor(in ctx: CGContext, at pt: CGPoint) {
    ctx.saveGState()
    ctx.translateBy(x: pt.x, y: pt.y)

    // Crisp minimal macOS-style open petting hand silhouette (white fill, dark charcoal stroke)
    let hand = CGMutablePath()
    // Palm base
    hand.addRoundedRect(in: CGRect(x: -7.5, y: -1.5, width: 15, height: 12), cornerWidth: 5.5, cornerHeight: 5.5)
    // Four fingers + thumb
    hand.addRoundedRect(in: CGRect(x: -7.2, y: -10.5, width: 3.3, height: 10.5), cornerWidth: 1.6, cornerHeight: 1.6)
    hand.addRoundedRect(in: CGRect(x: -3.4, y: -12.5, width: 3.4, height: 12.5), cornerWidth: 1.7, cornerHeight: 1.7)
    hand.addRoundedRect(in: CGRect(x: 0.5,  y: -12.0, width: 3.4, height: 12.0), cornerWidth: 1.7, cornerHeight: 1.7)
    hand.addRoundedRect(in: CGRect(x: 4.3,  y: -9.5,  width: 3.1, height: 9.8),  cornerWidth: 1.5, cornerHeight: 1.5)
    hand.addRoundedRect(in: CGRect(x: -11.0, y: -1.0, width: 4.8, height: 3.4), cornerWidth: 1.6, cornerHeight: 1.6)

    ctx.setFillColor(NSColor.white.cgColor)
    ctx.setStrokeColor(NSColor(calibratedWhite: 0.12, alpha: 1.0).cgColor)
    ctx.setLineWidth(1.5)
    ctx.addPath(hand)
    ctx.drawPath(using: .fillStroke)
    ctx.restoreGState()
}

private func drawToastPill(in ctx: CGContext, text: String, center: CGPoint, alpha: CGFloat, accent: NSColor) {
    guard alpha > 0.01 else { return }
    ctx.saveGState()
    ctx.setAlpha(alpha)

    let attrs: [NSAttributedString.Key: Any] = [
        .font: NSFont.systemFont(ofSize: 15, weight: .semibold),
        .foregroundColor: NSColor.white
    ]
    let attrStr = NSAttributedString(string: text, attributes: attrs)
    let sz = attrStr.size()
    let padX: CGFloat = 18
    let padY: CGFloat = 9
    let rect = CGRect(
        x: center.x - (sz.width * 0.5 + padX),
        y: center.y - (sz.height * 0.5 + padY),
        width: sz.width + padX * 2,
        height: sz.height + padY * 2
    )
    let path = CGPath(roundedRect: rect, cornerWidth: rect.height * 0.5, cornerHeight: rect.height * 0.5, transform: nil)

    ctx.setFillColor(NSColor(calibratedRed: 0.10, green: 0.10, blue: 0.12, alpha: 0.88).cgColor)
    ctx.addPath(path)
    ctx.fillPath()

    ctx.setStrokeColor(accent.withAlphaComponent(0.9).cgColor)
    ctx.setLineWidth(1.6)
    ctx.addPath(path)
    ctx.strokePath()

    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(cgContext: ctx, flipped: true)
    attrStr.draw(at: CGPoint(x: center.x - sz.width * 0.5, y: center.y - sz.height * 0.5 - 0.5))
    NSGraphicsContext.restoreGraphicsState()

    ctx.restoreGState()
}

func renderCustomShowcaseVideo(bgPath: String, outDir: String) {
    try? FileManager.default.createDirectory(atPath: outDir, withIntermediateDirectories: true)

    guard let bgNSImage = NSImage(contentsOfFile: bgPath),
          let bgCGImage = bgNSImage.cgImage(forProposedRect: nil, context: nil, hints: nil) else {
        fputs("Failed to load background image: \(bgPath)\n", stderr)
        return
    }

    let prevCoat = CatSettings.shared.coatTheme
    let prevEye = CatSettings.shared.eyeTheme
    let prevKb = CatSettings.shared.keyboardTheme
    let prevBadges = CatSettings.shared.showKeyBadges
    let prevFoot = CatSettings.shared.footEasterEgg

    CatSettings.shared.coatTheme = .classicBlack
    CatSettings.shared.eyeTheme = .neonChartreuse
    CatSettings.shared.keyboardTheme = .classicMilk
    CatSettings.shared.showKeyBadges = true
    CatSettings.shared.footEasterEgg = true

    defer {
        CatSettings.shared.coatTheme = prevCoat
        CatSettings.shared.eyeTheme = prevEye
        CatSettings.shared.keyboardTheme = prevKb
        CatSettings.shared.showKeyBadges = prevBadges
        CatSettings.shared.footEasterEgg = prevFoot
    }

    // Output resolution matches uploaded background cleanly (even dims for H.264!)
    let outW = 1024
    let outH = 780
    let fps = 30
    let durationSec = 16.5
    let totalFrames = Int(durationSec * Double(fps))
    let subSteps = 2
    let subDt = 1.0 / Double(fps * subSteps)

    // Place the cat cleanly in the lower-right desktop region (scale 1.18 -> 424.8 x 377.6)
    let catScale: CGFloat = 1.18
    let catW = CatPhysics.canvasSize.width * catScale
    let catH = CatPhysics.canvasSize.height * catScale
    let catOriginX: CGFloat = CGFloat(outW) - catW - 18.0    // ~581
    let catOriginYTop: CGFloat = CGFloat(outH) - catH - 16.0 // ~386 (top-down canvas Y)

    // Virtual macOS bottom-up screen frame for eye/head tracking math
    let winFrameBottomUp = CGRect(
        x: catOriginX,
        y: CGFloat(outH) - (catOriginYTop + catH),
        width: catW,
        height: catH
    )

    let physics = CatPhysics()
    var state = ScriptedFrameInput(
        now: 100.0,
        activeKeys: [],
        orderedKeys: [],
        mouseScreenPos: CGPoint(x: 420, y: 460),
        mouseVelocity: .zero,
        leftMouseDown: false,
        rightMouseDown: false,
        lastMouseMoveTime: 0,
        lastMouseClickTime: 0,
        lastKeyActivityTime: 0
    )

    // Act 1 (0.2s - 3.2s): Black Cat typing burst
    let act1Keys: [(Double, Double, PhysicalKey)] = [
        (0.25, 0.43, .l), (0.45, 0.63, .i), (0.65, 0.83, .f), (0.85, 1.03, .e),
        (1.08, 1.26, .space),
        (1.30, 1.48, .k), (1.50, 1.68, .l), (1.70, 1.88, .i), (1.90, 2.08, .n), (2.10, 2.28, .e),
        (2.36, 2.58, .enter), (2.68, 2.90, .a), (2.82, 3.05, .s)
    ]
    // Act 5a (10.6s - 12.8s): Siamese multitasking (mouse drag + WASD + 3-key hind foot!)
    let act5Keys: [(Double, Double, PhysicalKey)] = [
        (10.65, 11.15, .w), (10.90, 11.40, .d),
        (11.45, 11.95, .a), (11.60, 12.00, .leftShift),
        (12.05, 12.65, .w), (12.12, 12.60, .leftShift), (12.20, 12.70, .space)
    ]
    var firedAct1 = Set<Int>()
    var firedAct5 = Set<Int>()
    var switchedToSiamese = false

    // Track screen-space cursor (top-down for easy choreography, converted to bottom-up for physics)
    var prevCursorTop = CGPoint(x: 360, y: 310)
    var prevPetCanvasPt: CGPoint? = nil
    var prevClick = false

    let cs = CGColorSpaceCreateDeviceRGB()

    for frameIdx in 0..<totalFrames {
        var cursorTop = prevCursorTop
        var cursorVisible = false
        var cursorIsHand = false
        var toastText: String? = nil
        var toastAlpha: CGFloat = 0
        var skinBurstProgress: CGFloat = -1 // 0..1 right at 9.5s

        for s in 0..<subSteps {
            let t = (Double(frameIdx) + Double(s) / Double(subSteps)) / Double(fps)
            state.now = 100.0 + t

            var active = Set<PhysicalKey>()
            var ordered: [PhysicalKey] = []
            var clicking = false
            var speedMult: CGFloat = 1.0

            // --- ACT 1 (0.0 - 3.3s): Black Cat Typing ---
            if t < 3.3 {
                cursorVisible = false
                cursorTop = CGPoint(x: 420, y: 340)
                for (idx, (t0, t1, k)) in act1Keys.enumerated() {
                    if t >= t0 && t < t1 {
                        active.insert(k)
                        ordered.append(k)
                        if !firedAct1.contains(idx) {
                            firedAct1.insert(idx)
                            state.lastKeyActivityTime = state.now
                            physics.triggerKeyDown(k, at: state.now)
                        }
                    }
                }
            }
            // --- ACT 2 (3.3 - 6.9s): Mouse Follow (2.5D Head Turn) + Rapid Waving (Hunting Boba Eyes!) ---
            else if t < 6.9 {
                cursorVisible = true
                if t < 4.5 {
                    // Graceful sweep over K-line chart to showcase 2.5D head tracking
                    let p = (t - 3.3) / 1.2
                    cursorTop = CGPoint(
                        x: 420 + CGFloat(sin(p * .pi * 1.6)) * 280,
                        y: 260 - CGFloat(cos(p * .pi * 1.6)) * 120
                    )
                    speedMult = 0.35
                } else if t < 6.2 {
                    // Rapid laser-pointer darting across upper-left/mid chart -> FULL BOBA HUNTING EYES!
                    let p = t - 4.5
                    cursorTop = CGPoint(
                        x: 430 + CGFloat(sin(p * 12.5)) * 290,
                        y: 210 + CGFloat(sin(p * 25.0)) * 75
                    )
                    speedMult = 2.0
                } else {
                    // Ease gently toward the cat's head to prepare for petting
                    let p = CGFloat((t - 6.2) / 0.7)
                    let targetX = catOriginX + 184 * catScale
                    let targetY = catOriginYTop + 115 * catScale
                    cursorTop = CGPoint(
                        x: 430 + (targetX - 430) * p,
                        y: 210 + (targetY - 210) * p
                    )
                    speedMult = 0.25
                }
            }
            // --- ACT 3 (6.9 - 9.3s): Petting Black Cat's Head -> Happy Closed Lime Eyes! ---
            else if t < 9.3 {
                cursorVisible = true
                cursorIsHand = true
                let p = t - 6.9
                let headCanvasX: CGFloat = 184 + CGFloat(sin(p * 7.2)) * 26
                let headCanvasY: CGFloat = 114 + CGFloat(cos(p * 14.4)) * 7
                cursorTop = CGPoint(
                    x: catOriginX + headCanvasX * catScale,
                    y: catOriginYTop + headCanvasY * catScale
                )
                let curPetPt = CGPoint(x: headCanvasX, y: headCanvasY)
                if let prev = prevPetCanvasPt {
                    let d = hypot(curPetPt.x - prev.x, curPetPt.y - prev.y)
                    if d > 0.4 {
                        physics.registerPetStroke(canvasPoint: curPetPt, delta: d * 1.35, isHead: true, at: state.now)
                    }
                }
                prevPetCanvasPt = curPetPt
                speedMult = 0.15
            }
            // --- ACT 4 (9.3 - 10.6s): Skin Switch -> 挖煤暹罗猫! ---
            else if t < 10.6 {
                prevPetCanvasPt = nil
                cursorVisible = true
                let p = t - 9.3
                cursorTop = CGPoint(x: 470, y: 300)
                speedMult = 0.1
                if t >= 9.6 && !switchedToSiamese {
                    switchedToSiamese = true
                    CatSettings.shared.coatTheme = .siamese
                    CatSettings.shared.eyeTheme = .siameseBlue
                    physics.bodySquash.velocity -= 2.2
                    physics.earTwitch.velocity += 5.0
                }
                if t >= 9.45 && t <= 10.55 {
                    skinBurstProgress = CGFloat(max(0, min(1, (t - 9.6) / 0.6)))
                    toastText = "🐱 切换皮肤：挖煤暹罗猫"
                    let fadeIn = min(1.0, (t - 9.45) / 0.18)
                    let fadeOut = min(1.0, (10.55 - t) / 0.22)
                    toastAlpha = CGFloat(min(fadeIn, fadeOut))
                }
                _ = p
            }
            // --- ACT 5a (10.6 - 12.8s): Siamese Multitasking (Dragging Mouse + WASD + 3-Key Foot!) ---
            else if t < 12.8 {
                cursorVisible = true
                let p = t - 10.6
                cursorTop = CGPoint(
                    x: 340 + CGFloat(sin(p * 3.1)) * 220,
                    y: 250 + CGFloat(cos(p * 4.4)) * 95
                )
                speedMult = 0.32
                clicking = (p > 0.3 && p < 0.65) || (p > 1.25 && p < 1.65)

                for (idx, (t0, t1, k)) in act5Keys.enumerated() {
                    if t >= t0 && t < t1 {
                        active.insert(k)
                        ordered.append(k)
                        if !firedAct5.contains(idx) {
                            firedAct5.insert(idx)
                            state.lastKeyActivityTime = state.now
                            physics.triggerKeyDown(k, at: state.now)
                        }
                    }
                }
            }
            // --- ACT 5b (12.8 - 14.7s): Siamese Hunting Mode (Huge Sapphire-Ring Boba Eyes!) ---
            else if t < 14.7 {
                cursorVisible = true
                let p = t - 12.8
                if p < 1.35 {
                    cursorTop = CGPoint(
                        x: 420 + CGFloat(sin(p * 13.0)) * 295,
                        y: 200 + CGFloat(sin(p * 26.0)) * 70
                    )
                    speedMult = 2.1
                } else {
                    let q = CGFloat((p - 1.35) / 0.55)
                    let targetX = catOriginX + 184 * catScale
                    let targetY = catOriginYTop + 114 * catScale
                    cursorTop = CGPoint(
                        x: 420 + (targetX - 420) * q,
                        y: 200 + (targetY - 200) * q
                    )
                    speedMult = 0.2
                }
            }
            // --- ACT 5c (14.7 - 16.5s): Petting Siamese Head -> Happy Closed Blue Eyes! ---
            else {
                cursorVisible = true
                cursorIsHand = true
                let p = t - 14.7
                let headCanvasX: CGFloat = 184 + CGFloat(sin(p * 7.0)) * 25
                let headCanvasY: CGFloat = 114 + CGFloat(cos(p * 14.0)) * 6
                cursorTop = CGPoint(
                    x: catOriginX + headCanvasX * catScale,
                    y: catOriginYTop + headCanvasY * catScale
                )
                let curPetPt = CGPoint(x: headCanvasX, y: headCanvasY)
                if let prev = prevPetCanvasPt {
                    let d = hypot(curPetPt.x - prev.x, curPetPt.y - prev.y)
                    if d > 0.4 {
                        physics.registerPetStroke(canvasPoint: curPetPt, delta: d * 1.35, isHead: true, at: state.now)
                    }
                }
                prevPetCanvasPt = curPetPt
                speedMult = 0.12
            }

            // Convert top-down cursor -> bottom-up macOS screen coords & velocity
            let vx = (cursorTop.x - prevCursorTop.x) / CGFloat(subDt) * speedMult
            let vy = -(cursorTop.y - prevCursorTop.y) / CGFloat(subDt) * speedMult
            prevCursorTop = cursorTop

            state.mouseScreenPos = CGPoint(x: cursorTop.x, y: CGFloat(outH) - cursorTop.y)
            state.mouseVelocity = CGVector(dx: vx, dy: vy)
            if cursorVisible && hypot(vx, vy) > 12 {
                state.lastMouseMoveTime = state.now
            }

            state.leftMouseDown = clicking
            if clicking != prevClick {
                prevClick = clicking
                if clicking { state.lastMouseClickTime = state.now }
                physics.triggerMouseClick(button: 0, isDown: clicking)
            }

            state.activeKeys = active
            state.orderedKeys = ordered

            physics.step(dt: CGFloat(subDt), windowFrameInScreen: winFrameBottomUp, scripted: state)
        }

        // Render composited frame (User's K-Line Screenshot + Live Cat + Cursor + Skin Toast)
        guard let ctx = CGContext(
            data: nil,
            width: outW,
            height: outH,
            bitsPerComponent: 8,
            bytesPerRow: 0,
            space: cs,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        ) else { continue }

        // 1. Draw background image cleanly right-side-up (CGContext default is bottom-up)
        ctx.draw(bgCGImage, in: CGRect(x: 0, y: 0, width: outW, height: outH))

        // 2. Flip to top-down coordinates for CatRenderer & overlays
        ctx.saveGState()
        ctx.translateBy(x: 0, y: CGFloat(outH))
        ctx.scaleBy(x: 1.0, y: -1.0)

        // Draw Cat in lower-right desktop area
        ctx.saveGState()
        ctx.translateBy(x: catOriginX, y: catOriginYTop)
        ctx.scaleBy(x: catScale, y: catScale)
        CatRenderer.draw(in: ctx, physics: physics, showPermissionPill: false)
        ctx.restoreGState()

        // Magical ring pop right at skin switch moment
        if skinBurstProgress > 0 && skinBurstProgress < 1 {
            let center = CGPoint(x: catOriginX + 184 * catScale, y: catOriginYTop + 175 * catScale)
            let radius = 40 + skinBurstProgress * 130
            let alpha = (1.0 - skinBurstProgress) * 0.65
            ctx.saveGState()
            ctx.setStrokeColor(EyeColorTheme.siameseBlue.primaryColor.withAlphaComponent(alpha).cgColor)
            ctx.setLineWidth(4.0 * (1.0 - skinBurstProgress))
            ctx.strokeEllipse(in: CGRect(x: center.x - radius, y: center.y - radius, width: radius * 2, height: radius * 2))
            ctx.restoreGState()
        }

        // Skin-switch translucent macOS toast pill above the cat
        if let text = toastText, toastAlpha > 0.01 {
            drawToastPill(
                in: ctx,
                text: text,
                center: CGPoint(x: catOriginX + 184 * catScale, y: catOriginYTop + 26),
                alpha: toastAlpha,
                accent: EyeColorTheme.siameseBlue.primaryColor
            )
        }

        // Realistic cursor (arrow or open petting hand!)
        if cursorVisible {
            if cursorIsHand {
                drawOpenHandCursor(in: ctx, at: cursorTop)
            } else {
                drawMacCursor(in: ctx, at: cursorTop, clicking: state.leftMouseDown)
            }
        }

        ctx.restoreGState()

        if let cgImg = ctx.makeImage() {
            let rep = NSBitmapImageRep(cgImage: cgImg)
            if let png = rep.representation(using: .png, properties: [:]) {
                let fn = String(format: "frame_%04d.png", frameIdx)
                try? png.write(to: URL(fileURLWithPath: (outDir as NSString).appendingPathComponent(fn)))
            }
        }
    }
}

// Bring earlier settings over once, before anything reads them: Whiskerkey's (Whisku's name and
// app ID up to 1.1.0) first, then KuroCat's (its name before that).
KuroCatMigration.importWhiskerkeySettings()
KuroCatMigration.importSettings()
KuroCatMigration.removeWhiskerkeySettings()

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.run()
