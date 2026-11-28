import Foundation
import AppKit

// Claude Code alerts: Claude Code hooks run `Whisku --claude-hook`, which reads the hook's JSON
// from stdin and tells the running Whisku through a local distributed notification. The cat then
// shows a bubble: "done" when a session finishes (Stop), "needs you" when Claude waits for an
// approval (Notification / PermissionRequest). Nothing leaves this computer and nothing goes over
// Cat Link; only the project folder name, the event kind and the app to bring back are passed.

enum ClaudeAlertKind: String { case done, needsYou }

enum ClaudeHook {
    static let notificationName = Notification.Name("com.whiskerkey.ClaudeCodeEvent")
    /// Sent by KuroCat's hook, in case Claude Code still starts the old app.
    static let kuroCatNotificationName = Notification.Name("com.kurocat.ClaudeCodeEvent")

    /// The command Claude Code runs (shell form, works with every Claude Code version).
    static var hookCommand: String {
        let exe = Bundle.main.executableURL?.path ?? "/Applications/Whisku.app/Contents/MacOS/Whisku"
        return "'\(exe.replacingOccurrences(of: "'", with: "'\\''"))' --claude-hook"
    }

    /// The `hooks` block to merge into ~/.claude/settings.json.
    static var settingsSnippet: String {
        let cmd = hookCommand.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"")
        let entry = "[{ \"hooks\": [{ \"type\": \"command\", \"command\": \"\(cmd)\", \"async\": true, \"timeout\": 5 }] }]"
        return """
        {
          "hooks": {
            "Stop": \(entry),
            "Notification": \(entry),
            "PermissionRequest": \(entry)
          }
        }
        """
    }

    /// Hook mode: read the hook input, post it to the running Whisku, exit 0 (never block Claude Code).
    static func runFromCommandLine(arguments: [String]) -> Never {
        var input = Data()
        if isatty(STDIN_FILENO) == 0 {
            input = FileHandle.standardInput.readDataToEndOfFile()
        } else if let i = arguments.firstIndex(of: "--claude-hook"), i + 1 < arguments.count {
            // Manual test from a terminal: `Whisku --claude-hook done` or `--claude-hook needs`
            let event = arguments[i + 1] == "needs" ? "PermissionRequest" : "Stop"
            input = (try? JSONSerialization.data(withJSONObject: ["hook_event_name": event,
                                                                 "cwd": FileManager.default.currentDirectoryPath,
                                                                 "session_id": "manual-test"])) ?? Data()
        }
        let env = ProcessInfo.processInfo.environment
        if var info = parse(input) {
            if let app = env["__CFBundleIdentifier"] ?? terminalBundleID(env["TERM_PROGRAM"]) { info["app"] = app }
            DistributedNotificationCenter.default().postNotificationName(notificationName, object: nil,
                                                                         userInfo: info, deliverImmediately: true)
            RunLoop.current.run(until: Date().addingTimeInterval(0.05))
        }
        exit(0)
    }

    /// Hook JSON -> ["kind", "project", "session"], or nil for events the cat ignores.
    static func parse(_ data: Data) -> [String: String]? {
        guard let obj = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any],
              let event = obj["hook_event_name"] as? String else { return nil }
        let kind: ClaudeAlertKind
        switch event {
        case "Stop":
            kind = .done
        case "PermissionRequest":
            kind = .needsYou
        case "Notification":
            let type = obj["notification_type"] as? String ?? ""
            let message = (obj["message"] as? String ?? "").lowercased()
            // "Waiting for your input" repeats a finished turn; auth notices aren't requests.
            if type == "idle_prompt" || type == "auth_success" || message.contains("waiting for your input") { return nil }
            kind = .needsYou
        default:
            return nil
        }
        let cwd = obj["cwd"] as? String ?? ""
        let folder = URL(fileURLWithPath: cwd).lastPathComponent
        return ["kind": kind.rawValue,
                "project": folder.isEmpty || folder == "/" ? "Claude Code" : folder,
                "session": obj["session_id"] as? String ?? ""]
    }

    private static func terminalBundleID(_ termProgram: String?) -> String? {
        switch termProgram {
        case "vscode": return "com.microsoft.VSCode"
        case "Apple_Terminal": return "com.apple.Terminal"
        case "iTerm.app": return "com.googlecode.iterm2"
        case "WarpTerminal": return "dev.warp.Warp-Stable"
        case "ghostty": return "com.mitchellh.ghostty"
        default: return nil
        }
    }
}

struct ClaudeAlert {
    let id = UUID()
    let kind: ClaudeAlertKind
    let project: String
    let session: String
    let app: String?
    let start: CFTimeInterval
    /// Shown top edge (canvas y). It slides toward its slot when bubbles are added or closed.
    var y: CGFloat = ClaudeAlertCenter.newestBubbleY

    var lifetime: CFTimeInterval { kind == .done ? 45 : 600 }

    var text: String {
        let name = project.count > 18 ? String(project.prefix(17)) + "…" : project
        return kind == .done ? tr("✓ \(name) 完成了", "✓ \(name) done") : tr("\(name) 等你确认", "\(name) needs you")
    }
}

final class ClaudeAlertCenter {
    static let shared = ClaudeAlertCenter()

    private(set) var alerts: [ClaudeAlert] = []
    /// A new alert arrived (the app makes the cat react).
    var onAlert: ((ClaudeAlert) -> Void)?
    /// Whether our cat is on screen (sounds play only then).
    var isCatVisible: () -> Bool = { true }

    private let enabledKey = "Whiskerkey.ClaudeAlerts"
    private let soundKey = "Whiskerkey.ClaudeAlertSound"
    var enabled: Bool {
        get { UserDefaults.standard.object(forKey: enabledKey) as? Bool ?? true }
        set { UserDefaults.standard.set(newValue, forKey: enabledKey); if !newValue { alerts.removeAll() } }
    }
    var soundEnabled: Bool {
        get { UserDefaults.standard.object(forKey: soundKey) as? Bool ?? true }
        set { UserDefaults.standard.set(newValue, forKey: soundKey) }
    }

    var needsAttention: Bool { alerts.contains { $0.kind == .needsYou } }

    func start() {
        for name in [ClaudeHook.notificationName, ClaudeHook.kuroCatNotificationName] {
            DistributedNotificationCenter.default().addObserver(forName: name, object: nil, queue: .main) { [weak self] note in
                guard let info = note.userInfo as? [String: String] else { return }
                self?.receive(info)
            }
        }
    }

    func receive(_ info: [String: String], now: CFTimeInterval = CACurrentMediaTime()) {
        guard enabled, let kind = ClaudeAlertKind(rawValue: info["kind"] ?? "") else { return }
        let alert = ClaudeAlert(kind: kind, project: info["project"] ?? "Claude Code", session: info["session"] ?? "",
                                app: info["app"], start: now)
        // Notification and PermissionRequest often both fire for one approval.
        if kind == .needsYou, let last = alerts.last(where: { $0.session == alert.session && $0.kind == .needsYou }),
           alert.start - last.start < 3 { return }
        // A newer event for a session replaces its older bubble.
        alerts.removeAll { !alert.session.isEmpty && $0.session == alert.session }
        alerts.append(alert)
        if alerts.count > 3 { alerts.removeFirst(alerts.count - 3) }
        if soundEnabled && isCatVisible() {
            NSSound(named: kind == .done ? "Glass" : "Purr")?.play()
        }
        onAlert?(alert)
    }

    private var lastStep: CFTimeInterval = 0

    func step(now: CFTimeInterval) {
        alerts.removeAll { now - $0.start > $0.lifetime }
        let dt = CGFloat(min(0.1, max(0, now - lastStep)))
        lastStep = now
        let follow = min(1, dt * 11)
        for i in alerts.indices {
            alerts[i].y += (slotY(i) - alerts[i].y) * follow
        }
    }

    /// The newest bubble sits just above the ears; older ones stack upward.
    static let newestBubbleY: CGFloat = 46
    static let bubbleSpacing: CGFloat = 23

    private func slotY(_ index: Int) -> CGFloat {
        ClaudeAlertCenter.newestBubbleY - CGFloat(alerts.count - 1 - index) * ClaudeAlertCenter.bubbleSpacing
    }

    /// Clicking a bubble closes it and brings back the app the session runs in.
    func activate(_ id: UUID) {
        guard let alert = alerts.first(where: { $0.id == id }) else { return }
        alerts.removeAll { $0.id == id }
        if let bundle = alert.app,
           let app = NSRunningApplication.runningApplications(withBundleIdentifier: bundle).first {
            app.activate(options: [])
        }
    }

    func dismissAll() { alerts.removeAll() }

    // MARK: Drawing (canvas coordinates, flipped, 360 x 320)

    /// Bubbles stack above the cat's head; the newest is nearest the cat (at the bottom).
    func layout(now: CFTimeInterval) -> [(alert: ClaudeAlert, rect: CGRect, alpha: CGFloat)] {
        let font = Self.font
        return alerts.map { alert in
            let w = (alert.text as NSString).size(withAttributes: [.font: font]).width + 22 + (alert.kind == .needsYou ? 14 : 0)
            let rect = CGRect(x: CatPhysics.seatAnchor.x - w / 2, y: alert.y, width: w, height: 21)
            let age = now - alert.start
            let fadeIn = min(1, CGFloat(age / 0.25))
            let fadeOut = min(1, CGFloat((alert.lifetime - age) / 4))
            return (alert, rect, max(0, min(fadeIn, fadeOut)))
        }
    }

    static let font = NSFont.systemFont(ofSize: 11.5, weight: .semibold)

    func draw(in ctx: CGContext, now: CFTimeInterval) {
        let items = layout(now: now)
        for (i, item) in items.enumerated() {
            let r = item.rect
            let isNeeds = item.alert.kind == .needsYou
            let pop = Self.popScale(age: now - item.alert.start)
            ctx.saveGState()
            ctx.setAlpha(item.alpha)
            ctx.translateBy(x: r.midX, y: r.midY)
            ctx.scaleBy(x: pop, y: pop)
            ctx.translateBy(x: -r.midX, y: -r.midY)

            let fill = isNeeds ? NSColor(calibratedRed: 1.0, green: 0.66, blue: 0.16, alpha: 0.96)
                               : NSColor(calibratedWhite: 0.11, alpha: 0.9)
            ctx.setShadow(offset: CGSize(width: 0, height: 1.5), blur: 4, color: NSColor.black.withAlphaComponent(0.3).cgColor)
            ctx.setFillColor(fill.cgColor)
            ctx.addPath(CGPath(roundedRect: r, cornerWidth: r.height / 2, cornerHeight: r.height / 2, transform: nil))
            ctx.fillPath()
            // Speech-bubble tail toward the cat under the bubble nearest the head.
            if i == items.count - 1 {
                ctx.move(to: CGPoint(x: r.midX - 5, y: r.maxY - 0.5))
                ctx.addLine(to: CGPoint(x: r.midX + 5, y: r.maxY - 0.5))
                ctx.addLine(to: CGPoint(x: r.midX + 1, y: r.maxY + 6))
                ctx.closePath()
                ctx.fillPath()
            }
            ctx.setShadow(offset: .zero, blur: 0, color: nil)

            var textX = r.minX + 11
            if isNeeds {
                // Pulsing dot: "waiting for you"
                let pulse = 0.55 + 0.45 * CGFloat(sin((now - item.alert.start) * 5.0) * 0.5 + 0.5)
                ctx.setFillColor(NSColor(calibratedRed: 0.55, green: 0.12, blue: 0.02, alpha: pulse).cgColor)
                ctx.fillEllipse(in: CGRect(x: textX, y: r.midY - 3.5, width: 7, height: 7))
                textX += 13
            }
            let color = isNeeds ? NSColor(calibratedRed: 0.25, green: 0.1, blue: 0.0, alpha: 1)
                                : NSColor(calibratedRed: 0.62, green: 0.95, blue: 0.45, alpha: 1)
            let text = NSAttributedString(string: item.alert.text, attributes: [.font: Self.font, .foregroundColor: color])
            NSGraphicsContext.saveGraphicsState()
            NSGraphicsContext.current = NSGraphicsContext(cgContext: ctx, flipped: true)
            text.draw(at: CGPoint(x: textX, y: r.minY + (r.height - text.size().height) / 2))
            NSGraphicsContext.restoreGraphicsState()
            ctx.restoreGState()
        }
    }

    /// Bubbles pop in with a small overshoot.
    static func popScale(age: CFTimeInterval) -> CGFloat {
        let t = CGFloat(min(1, age / 0.35))
        return 0.6 + 0.4 * CatPhysics.easeOutBack(t)
    }

    func alertID(at canvasPoint: CGPoint, now: CFTimeInterval) -> UUID? {
        layout(now: now).last { $0.rect.insetBy(dx: -3, dy: -3).contains(canvasPoint) }?.alert.id
    }

    // MARK: Menu

    func menuItem() -> NSMenuItem {
        let parent = NSMenuItem(title: tr("Claude Code 提醒 🤖", "Claude Code alerts 🤖"), action: nil, keyEquivalent: "")
        let menu = NSMenu()
        let items: [(String, Selector, Bool?)] = [
            (tr("完成或等你确认时提醒", "Alert when a session is done or needs you"), #selector(ClaudeAlertMenuActions.toggleEnabled), enabled),
            (tr("提示音", "Sound"), #selector(ClaudeAlertMenuActions.toggleSound), soundEnabled),
            (tr("试一下「完成了」", "Try “done”"), #selector(ClaudeAlertMenuActions.testDone), nil),
            (tr("试一下「等你确认」", "Try “needs you”"), #selector(ClaudeAlertMenuActions.testNeeds), nil),
            (tr("复制 Claude Code 钩子设置", "Copy Claude Code hook settings"), #selector(ClaudeAlertMenuActions.copySettings), nil),
        ]
        for (i, (title, action, state)) in items.enumerated() {
            if i == 2 || i == 4 { menu.addItem(.separator()) }
            let item = NSMenuItem(title: title, action: action, keyEquivalent: "")
            item.target = ClaudeAlertMenuActions.shared
            if let state { item.state = state ? .on : .off }
            if i >= 2 && i <= 3 { item.isEnabled = enabled }
            menu.addItem(item)
        }
        parent.submenu = menu
        return parent
    }
}

@objc final class ClaudeAlertMenuActions: NSObject {
    static let shared = ClaudeAlertMenuActions()
    private var center: ClaudeAlertCenter { .shared }

    @objc func toggleEnabled() { center.enabled.toggle() }
    @objc func toggleSound() { center.soundEnabled.toggle() }
    @objc func testDone() { center.receive(["kind": "done", "project": "Whisku", "session": "menu-test-done"]) }
    @objc func testNeeds() { center.receive(["kind": "needsYou", "project": "Whisku", "session": "menu-test-needs"]) }
    @objc func copySettings() {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(ClaudeHook.settingsSnippet, forType: .string)
        let alert = NSAlert()
        alert.messageText = tr("已复制 Claude Code 钩子设置", "Claude Code hook settings copied")
        alert.informativeText = tr("把它合并到 ~/.claude/settings.json 的 \"hooks\" 里，然后重新打开 Claude Code。之后每当一个会话完成，或者 Claude 等你确认时，小猫都会提醒你。只在本机，不会通过猫咪连线发给朋友。", "Merge it into the \"hooks\" section of ~/.claude/settings.json, then reopen Claude Code. From then on, the cat tells you whenever a session finishes or Claude needs your OK. It stays on this computer and never goes over Cat Link.")
        NSApp.activate(ignoringOtherApps: true)
        alert.runModal()
    }
}
