import Foundation
import AppKit

// Break reminder: after you have worked for a while (menu: 30 min – 3 h), the cat waves and suggests
// a break. Local only: nothing goes over Cat Link.
//
//   work ──(worked longer than the setting)──▶ remind ──(a break)──▶ rest ──(any input)──▶ work
//
// A pause counts as a break when it is long enough for what the cat knows:
//   - 15 min without input, in any state (thinking can easily take a few minutes)
//   - 2 min without input after you click OK (input in the first 30 s is ignored: saving your work)
//   - 2 min with the screen locked or the computer asleep
// In remind, a reminder shows right away and then every 30 min: the cat waves for 7 s, and the bubble
// stays for 30 s with [OK] [Later], plus [I've rested] from the second reminder on.
// Tiny mouse movements are not input. Times come from a clock that keeps running while the computer sleeps.

struct BreakReminderLogic {
    enum Phase { case work, remind, rest }
    enum Button { case ok, later, rested }

    static let naturalBreak: Double = 15 * 60
    static let okBreak: Double = 2 * 60
    static let okPreparation: Double = 30
    static let awayBreak: Double = 2 * 60
    static let repeatEvery: Double = 30 * 60
    static let bubbleLife: Double = 30
    static let waveDuration: Double = 7

    /// Seconds of work before the first reminder; 0 = off.
    private(set) var interval: Double
    private(set) var phase: Phase = .work
    private(set) var workStart: Double
    private(set) var lastInput: Double
    /// Reminders shown since this remind period began (the first one has no "I've rested").
    private(set) var remindersShown = 0
    private(set) var lastReminderAt: Double = 0
    /// When OK was clicked, while waiting to see whether a break follows.
    private(set) var okAt: Double?
    private(set) var lockedSince: Double?
    private(set) var asleepSince: Double?
    /// The reminder bubble on screen.
    private(set) var bubbleShownAt: Double?
    private(set) var bubbleOffersRested = false
    private(set) var bubbleWorked: Double = 0

    init(interval: Double, now: Double) {
        self.interval = interval
        workStart = now
        lastInput = now
    }

    /// A new setting applies to the time already worked; reminding stops if that is now too short.
    mutating func setInterval(_ seconds: Double, now: Double) {
        interval = seconds
        if phase == .remind && (seconds <= 0 || now - workStart < seconds) {
            phase = .work
            remindersShown = 0
            bubbleShownAt = nil
        }
    }

    func isWaving(now: Double) -> Bool {
        guard let shown = bubbleShownAt else { return false }
        return now - shown < Self.waveDuration
    }

    func workedSeconds(now: Double) -> Double { phase == .rest ? 0 : max(0, now - workStart) }

    mutating func noteInput(now: Double) {
        if let ok = okAt, now - ok < Self.okPreparation { lastInput = now; return }
        let tookBreak = phase == .rest || hasTakenBreak(now: now)
        lastInput = now
        if tookBreak { startWork(now: now); return }
        // Busy again within 2 min of OK: not a break, keep reminding.
        okAt = nil
    }

    mutating func screenLocked(now: Double) { if lockedSince == nil { lockedSince = now } }
    mutating func screenUnlocked(now: Double) { tick(now: now); lockedSince = nil }
    mutating func willSleep(now: Double) { if asleepSince == nil { asleepSince = now } }
    mutating func didWake(now: Double) { tick(now: now); asleepSince = nil }

    mutating func tick(now: Double) {
        if phase != .rest && hasTakenBreak(now: now) {
            phase = .rest
            okAt = nil
            bubbleShownAt = nil
            remindersShown = 0
            return
        }
        if let shown = bubbleShownAt, now - shown >= Self.bubbleLife { bubbleShownAt = nil }
        guard interval > 0, lockedSince == nil, asleepSince == nil else { return }
        switch phase {
        case .work:
            if now - workStart >= interval { phase = .remind; showReminder(now: now) }
        case .remind:
            if now - lastReminderAt >= Self.repeatEvery { showReminder(now: now) }
        case .rest:
            break
        }
    }

    mutating func tap(_ button: Button, now: Double) {
        bubbleShownAt = nil
        switch button {
        case .ok:
            okAt = now
            lastInput = now
        case .later:
            break
        case .rested:
            startWork(now: now)
        }
    }

    private func hasTakenBreak(now: Double) -> Bool {
        let idle = now - lastInput
        if idle >= Self.naturalBreak { return true }
        if okAt != nil && idle >= Self.okBreak { return true }
        if let away = [lockedSince, asleepSince].compactMap({ $0 }).min(), now - away >= Self.awayBreak { return true }
        return false
    }

    private mutating func startWork(now: Double) {
        phase = .work
        workStart = now
        okAt = nil
        remindersShown = 0
        bubbleShownAt = nil
    }

    private mutating func showReminder(now: Double) {
        remindersShown += 1
        lastReminderAt = now
        bubbleShownAt = now
        bubbleOffersRested = remindersShown > 1
        bubbleWorked = now - workStart
    }
}

final class BreakReminderCenter {
    static let shared = BreakReminderCenter()

    /// Keeps running while the Mac sleeps (CACurrentMediaTime and systemUptime stop).
    static func clock() -> Double { Double(clock_gettime_nsec_np(CLOCK_MONOTONIC)) / 1_000_000_000 }

    static let choices = [0, 30, 60, 120, 180]
    private let minutesKey = "Whisku.BreakReminderMinutes"
    var minutes: Int {
        get { UserDefaults.standard.object(forKey: minutesKey) as? Int ?? 60 }
        set { UserDefaults.standard.set(newValue, forKey: minutesKey); logic.setInterval(Double(newValue) * 60, now: Self.clock()) }
    }

    private(set) var logic: BreakReminderLogic
    /// Input counts only once the pointer is this far from where it last counted (points).
    static let tinyMove: CGFloat = 12
    private var mouseAnchor = NSEvent.mouseLocation
    private var observers: [(NotificationCenter, NSObjectProtocol)] = []

    private struct Bubble {
        let shownAt: Double
        let worked: Double
        let buttons: [BreakReminderLogic.Button]
        let isPreview: Bool
    }
    private var preview: Bubble?
    /// "Enjoy your break ☕" after OK.
    private var ackShownAt: Double?
    static let ackLife: Double = 4

    private init() {
        let stored = UserDefaults.standard.object(forKey: "Whisku.BreakReminderMinutes") as? Int ?? 60
        logic = BreakReminderLogic(interval: Double(stored) * 60, now: Self.clock())
    }

    func start() {
        let distributed = DistributedNotificationCenter.default()
        let workspace = NSWorkspace.shared.notificationCenter
        let watch: [(NotificationCenter, Notification.Name, (inout BreakReminderLogic, Double) -> Void)] = [
            (distributed, Notification.Name("com.apple.screenIsLocked"), { $0.screenLocked(now: $1) }),
            (distributed, Notification.Name("com.apple.screenIsUnlocked"), { $0.screenUnlocked(now: $1) }),
            // Switching to another user's session is like locking.
            (workspace, NSWorkspace.sessionDidResignActiveNotification, { $0.screenLocked(now: $1) }),
            (workspace, NSWorkspace.sessionDidBecomeActiveNotification, { $0.screenUnlocked(now: $1) }),
            (workspace, NSWorkspace.willSleepNotification, { $0.willSleep(now: $1) }),
            (workspace, NSWorkspace.didWakeNotification, { $0.didWake(now: $1) }),
        ]
        for (center, name, apply) in watch {
            let token = center.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in
                guard let self else { return }
                apply(&self.logic, Self.clock())
            }
            observers.append((center, token))
        }
    }

    /// A key press, click or scroll on this computer.
    func noteInput() { logic.noteInput(now: Self.clock()) }

    /// Every frame: sample the pointer (ignoring tiny movements) and advance the state.
    func step() {
        let now = Self.clock()
        let loc = NSEvent.mouseLocation
        if hypot(loc.x - mouseAnchor.x, loc.y - mouseAnchor.y) >= Self.tinyMove {
            mouseAnchor = loc
            logic.noteInput(now: now)
        }
        logic.tick(now: now)
        if let p = preview, now - p.shownAt >= BreakReminderLogic.bubbleLife { preview = nil }
        if let a = ackShownAt, now - a >= Self.ackLife { ackShownAt = nil }
    }

    var isWaving: Bool {
        let now = Self.clock()
        if let p = preview, now - p.shownAt < BreakReminderLogic.waveDuration { return true }
        return logic.isWaving(now: now)
    }

    /// While the reminder (or its "enjoy your break") is up, it takes the space above the cat.
    var isShowingBubble: Bool { currentBubble != nil || ackShownAt != nil }

    private var currentBubble: Bubble? {
        if let p = preview { return p }
        guard let shown = logic.bubbleShownAt else { return nil }
        return Bubble(shownAt: shown, worked: logic.bubbleWorked,
                      buttons: logic.bubbleOffersRested ? [.ok, .later, .rested] : [.ok, .later], isPreview: false)
    }

    func showPreview() {
        let worked = minutes > 0 ? Double(minutes) * 60 : 3600
        preview = Bubble(shownAt: Self.clock(), worked: worked, buttons: [.ok, .later, .rested], isPreview: true)
        ackShownAt = nil
    }

    func tap(_ button: BreakReminderLogic.Button) {
        let now = Self.clock()
        if preview != nil {
            preview = nil
        } else {
            logic.tap(button, now: now)
        }
        ackShownAt = button == .ok ? now : nil
    }

    // MARK: Text

    static func duration(_ seconds: Double) -> String {
        let total = max(0, Int(seconds / 60))
        let h = total / 60, m = total % 60
        if h == 0 { return tr("\(m) 分钟", "\(m) min") }
        if m == 0 { return tr("\(h) 小时", "\(h) h") }
        return tr("\(h) 小时 \(m) 分钟", "\(h) h \(m) min")
    }

    /// The same, spelled out for a sentence: "1 hour 30 min".
    static func spokenDuration(_ seconds: Double) -> String {
        let total = max(0, Int(seconds / 60))
        let h = total / 60, m = total % 60
        if h == 0 { return tr("\(m) 分钟", m == 1 ? "1 minute" : "\(m) minutes") }
        let hours = tr("\(h) 小时", h == 1 ? "1 hour" : "\(h) hours")
        return m == 0 ? hours : tr("\(hours) \(m) 分钟", "\(hours) \(m) min")
    }

    private static func title(worked: Double) -> String {
        tr("已经连续工作 \(spokenDuration(worked))了，休息一下吧 ☕", "You've worked for \(spokenDuration(worked)). Time for a break ☕")
    }

    /// The title font, smaller if a long text would not fit on the canvas.
    private static func titleFont(for title: String) -> NSFont {
        let width = (title as NSString).size(withAttributes: [.font: titleFont]).width
        let room = CatPhysics.canvasSize.width - 8 - 24
        return width <= room ? titleFont : NSFont.systemFont(ofSize: 12 * room / width, weight: .semibold)
    }

    private static func label(_ button: BreakReminderLogic.Button) -> String {
        switch button {
        case .ok: return tr("好的", "OK")
        case .later: return tr("等会儿", "Later")
        case .rested: return tr("我休息过了", "I've rested")
        }
    }

    // MARK: Drawing (canvas coordinates, flipped, 360 x 320)

    static let titleFont = NSFont.systemFont(ofSize: 12, weight: .semibold)
    static let buttonFont = NSFont.systemFont(ofSize: 11, weight: .semibold)
    /// The bubble's bottom edge sits where the newest Claude Code bubble's does, just above the ears.
    static let bubbleBottom: CGFloat = ClaudeAlertCenter.newestBubbleY + 21
    static let buttonHeight: CGFloat = 19

    private struct Layout {
        let rect: CGRect
        let titleOrigin: CGPoint
        let buttons: [(button: BreakReminderLogic.Button, rect: CGRect)]
    }

    private func layout(_ bubble: Bubble) -> Layout {
        let title = Self.title(worked: bubble.worked)
        let titleW = (title as NSString).size(withAttributes: [.font: Self.titleFont(for: title)]).width
        let widths = bubble.buttons.map { (Self.label($0) as NSString).size(withAttributes: [.font: Self.buttonFont]).width + 20 }
        let gap: CGFloat = 6
        let buttonsW = widths.reduce(0, +) + gap * CGFloat(widths.count - 1)
        let w = min(CatPhysics.canvasSize.width - 8, max(titleW, buttonsW) + 24)
        let h: CGFloat = 8 + 16 + 6 + Self.buttonHeight + 7
        let rect = CGRect(x: CatPhysics.seatAnchor.x - w / 2, y: Self.bubbleBottom - h, width: w, height: h)
        let clamped = rect.offsetBy(dx: max(4 - rect.minX, 0) + min(CatPhysics.canvasSize.width - 4 - rect.maxX, 0), dy: 0)
        var x = clamped.midX - buttonsW / 2
        var buttons: [(BreakReminderLogic.Button, CGRect)] = []
        for (i, b) in bubble.buttons.enumerated() {
            buttons.append((b, CGRect(x: x, y: clamped.maxY - 7 - Self.buttonHeight, width: widths[i], height: Self.buttonHeight)))
            x += widths[i] + gap
        }
        return Layout(rect: clamped, titleOrigin: CGPoint(x: clamped.midX - titleW / 2, y: clamped.minY + 8), buttons: buttons)
    }

    private func ackRect() -> CGRect {
        let text = tr("好好休息一下 ☕", "Enjoy your break ☕")
        let w = (text as NSString).size(withAttributes: [.font: ClaudeAlertCenter.font]).width + 22
        return CGRect(x: CatPhysics.seatAnchor.x - w / 2, y: ClaudeAlertCenter.newestBubbleY, width: w, height: 21)
    }

    func draw(in ctx: CGContext) {
        let now = Self.clock()
        if let bubble = currentBubble {
            drawReminder(bubble, in: ctx, now: now)
        } else if let shown = ackShownAt {
            let age = now - shown
            let alpha = max(0, min(1, CGFloat(age / 0.2), CGFloat((Self.ackLife - age) / 0.8)))
            drawPill(ackRect(), text: tr("好好休息一下 ☕", "Enjoy your break ☕"), alpha: alpha,
                     pop: ClaudeAlertCenter.popScale(age: age), in: ctx)
        }
    }

    private static let bubbleFill = NSColor(calibratedWhite: 0.11, alpha: 0.92)
    private static let titleInk = NSColor(calibratedWhite: 0.97, alpha: 1)
    private static let okFill = NSColor(calibratedRed: 0.62, green: 0.95, blue: 0.45, alpha: 1)
    private static let okInk = NSColor(calibratedRed: 0.08, green: 0.22, blue: 0.04, alpha: 1)
    private static let otherFill = NSColor(calibratedWhite: 1, alpha: 0.16)
    private static let otherInk = NSColor(calibratedWhite: 0.95, alpha: 1)

    private func drawReminder(_ bubble: Bubble, in ctx: CGContext, now: Double) {
        let l = layout(bubble)
        let r = l.rect
        let age = now - bubble.shownAt
        let alpha = max(0, min(1, CGFloat(age / 0.25), CGFloat((BreakReminderLogic.bubbleLife - age) / 1.5)))
        let pop = ClaudeAlertCenter.popScale(age: age)
        ctx.saveGState()
        ctx.setAlpha(alpha)
        ctx.translateBy(x: r.midX, y: r.maxY)
        ctx.scaleBy(x: pop, y: pop)
        ctx.translateBy(x: -r.midX, y: -r.maxY)

        ctx.setShadow(offset: CGSize(width: 0, height: 1.5), blur: 4, color: NSColor.black.withAlphaComponent(0.3).cgColor)
        ctx.setFillColor(Self.bubbleFill.cgColor)
        ctx.addPath(CGPath(roundedRect: r, cornerWidth: 11, cornerHeight: 11, transform: nil))
        ctx.fillPath()
        ctx.move(to: CGPoint(x: r.midX - 5, y: r.maxY - 0.5))
        ctx.addLine(to: CGPoint(x: r.midX + 5, y: r.maxY - 0.5))
        ctx.addLine(to: CGPoint(x: r.midX + 1, y: r.maxY + 6))
        ctx.closePath()
        ctx.fillPath()
        ctx.setShadow(offset: .zero, blur: 0, color: nil)

        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(cgContext: ctx, flipped: true)
        let title = Self.title(worked: bubble.worked)
        NSAttributedString(string: title,
                           attributes: [.font: Self.titleFont(for: title), .foregroundColor: Self.titleInk]).draw(at: l.titleOrigin)
        for (button, rect) in l.buttons {
            let isOK = button == .ok
            ctx.setFillColor((isOK ? Self.okFill : Self.otherFill).cgColor)
            ctx.addPath(CGPath(roundedRect: rect, cornerWidth: rect.height / 2, cornerHeight: rect.height / 2, transform: nil))
            ctx.fillPath()
            let text = NSAttributedString(string: Self.label(button),
                                          attributes: [.font: Self.buttonFont, .foregroundColor: isOK ? Self.okInk : Self.otherInk])
            let size = text.size()
            text.draw(at: CGPoint(x: rect.midX - size.width / 2, y: rect.midY - size.height / 2))
        }
        NSGraphicsContext.restoreGraphicsState()
        ctx.restoreGState()
    }

    private func drawPill(_ r: CGRect, text: String, alpha: CGFloat, pop: CGFloat, in ctx: CGContext) {
        ctx.saveGState()
        ctx.setAlpha(alpha)
        ctx.translateBy(x: r.midX, y: r.midY)
        ctx.scaleBy(x: pop, y: pop)
        ctx.translateBy(x: -r.midX, y: -r.midY)
        ctx.setShadow(offset: CGSize(width: 0, height: 1.5), blur: 4, color: NSColor.black.withAlphaComponent(0.3).cgColor)
        ctx.setFillColor(Self.bubbleFill.cgColor)
        ctx.addPath(CGPath(roundedRect: r, cornerWidth: r.height / 2, cornerHeight: r.height / 2, transform: nil))
        ctx.fillPath()
        ctx.move(to: CGPoint(x: r.midX - 5, y: r.maxY - 0.5))
        ctx.addLine(to: CGPoint(x: r.midX + 5, y: r.maxY - 0.5))
        ctx.addLine(to: CGPoint(x: r.midX + 1, y: r.maxY + 6))
        ctx.closePath()
        ctx.fillPath()
        ctx.setShadow(offset: .zero, blur: 0, color: nil)
        let s = NSAttributedString(string: text, attributes: [.font: ClaudeAlertCenter.font, .foregroundColor: Self.titleInk])
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(cgContext: ctx, flipped: true)
        s.draw(at: CGPoint(x: r.minX + 11, y: r.minY + (r.height - s.size().height) / 2))
        NSGraphicsContext.restoreGraphicsState()
        ctx.restoreGState()
    }

    enum Hit { case bubble, button(BreakReminderLogic.Button) }

    /// What is under a canvas point: a button, the rest of the bubble, or nothing.
    func hit(at pt: CGPoint) -> Hit? {
        guard let bubble = currentBubble else { return nil }
        let l = layout(bubble)
        if let b = l.buttons.first(where: { $0.rect.insetBy(dx: -2, dy: -3).contains(pt) }) { return .button(b.button) }
        return l.rect.insetBy(dx: -2, dy: -2).contains(pt) ? .bubble : nil
    }

    // MARK: Menu

    func menuItem() -> NSMenuItem {
        let parent = NSMenuItem(title: tr("休息提醒 ☕", "Break reminder ☕"), action: nil, keyEquivalent: "")
        let menu = NSMenu()
        if minutes > 0 {
            let now = Self.clock()
            let status = logic.phase == .rest ? tr("正在休息", "On a break")
                : tr("已连续工作 \(Self.duration(logic.workedSeconds(now: now)))", "Working for \(Self.duration(logic.workedSeconds(now: now)))")
            let statusItem = NSMenuItem(title: status, action: nil, keyEquivalent: "")
            statusItem.isEnabled = false
            menu.addItem(statusItem)
            menu.addItem(.separator())
        }
        let header = NSMenuItem(title: tr("工作多久后提醒：", "Remind me after working:"), action: nil, keyEquivalent: "")
        header.isEnabled = false
        menu.addItem(header)
        for m in Self.choices {
            let title = m == 0 ? tr("关闭", "Off") : Self.duration(Double(m) * 60)
            let item = NSMenuItem(title: title, action: #selector(BreakReminderMenuActions.select(_:)), keyEquivalent: "")
            item.target = BreakReminderMenuActions.shared
            item.representedObject = m
            item.state = m == minutes ? .on : .off
            item.indentationLevel = 1
            menu.addItem(item)
        }
        menu.addItem(.separator())
        let previewItem = NSMenuItem(title: tr("预览提醒", "Preview the reminder"), action: #selector(BreakReminderMenuActions.preview), keyEquivalent: "")
        previewItem.target = BreakReminderMenuActions.shared
        menu.addItem(previewItem)
        let note = NSMenuItem(title: tr("离开 15 分钟、点「好的」后离开 2 分钟，或锁屏 2 分钟，都算休息",
                                        "A break: 15 min away, 2 min after OK, or 2 min locked"), action: nil, keyEquivalent: "")
        note.isEnabled = false
        menu.addItem(note)
        parent.submenu = menu
        return parent
    }
}

@objc final class BreakReminderMenuActions: NSObject {
    static let shared = BreakReminderMenuActions()
    @objc func select(_ sender: NSMenuItem) {
        guard let m = sender.representedObject as? Int else { return }
        BreakReminderCenter.shared.minutes = m
    }
    @objc func preview() { BreakReminderCenter.shared.showPreview() }
}
