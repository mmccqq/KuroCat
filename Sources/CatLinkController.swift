import AppKit

/// The Cat Link UI: the friend's cat window, the 猫咪连线 menu, pairing dialogs and a local demo.
final class CatLinkController: NSObject {
    private let link = CatLinkSession.shared
    private weak var mainWindow: OverlayWindow?
    private var remoteWindow: OverlayWindow?
    private var driver: RemoteCatDriver?
    private var lastHeart: CFTimeInterval = 0
    private var demoUntil: CFTimeInterval = 0

    /// The app's context menu, shown on right-click over the friend's cat too.
    var menuProvider: (() -> NSMenu)?
    /// True while linked with an online friend (for the menu bar indicator).
    var onIndicatorChange: ((Bool) -> Void)?

    private let defaults = UserDefaults.standard
    private var savedCode: CatLinkCode? { defaults.string(forKey: "KuroCat.LinkCode").flatMap { CatLinkCode(parsing: $0) } }
    private var nickname: String { defaults.string(forKey: "KuroCat.LinkNickname") ?? "" }
    /// Whether the friend's cat is shown (independent of our own cat).
    var showFriendCat: Bool {
        get { defaults.object(forKey: "KuroCat.LinkShowFriend") as? Bool ?? true }
        set {
            defaults.set(newValue, forKey: "KuroCat.LinkShowFriend")
            if shouldShowRemote { remoteWindow?.orderFrontRegardless() } else { remoteWindow?.orderOut(nil) }
        }
    }
    private var newCodeServer: CatLinkCode.Server {
        CatLinkCode.Server(rawValue: defaults.string(forKey: "KuroCat.LinkServer") ?? "") ?? .china
    }
    private var isDemo: Bool { CACurrentMediaTime() < demoUntil }

    static let whatIsShared = """
    对方会看到：你在打字（只知道是左手还是右手在敲，不知道按了哪个键）、鼠标的移动、点击和滚动（包括光标在屏幕上的大致位置）、是否戴着耳机、你的猫咪皮肤和昵称。你们也能隔空摸对方的猫。
    输入密码时会自动暂停发送键盘动作。所有内容都用配对码加密，中转服务器也看不到内容。
    """

    // MARK: Setup & per-frame

    func setup(mainWindow: OverlayWindow) {
        self.mainWindow = mainWindow
        link.profile = { [weak self] in self?.profile() ?? CatLinkEvent(k: "hello") }
        link.onStatusChange = { [weak self] in self?.statusChanged() }
        link.onFriendEvent = { [weak self] event, at in
            DispatchQueue.main.asyncAfter(deadline: .now() + max(0, at - CACurrentMediaTime())) {
                self?.applyFriend(event)
            }
        }
        if defaults.bool(forKey: "KuroCat.LinkEnabled"), let code = savedCode {
            link.start(code: code)
        }
    }

    func step(dt: CGFloat) {
        guard let window = remoteWindow, window.isVisible, let driver else { return }
        let now = CACurrentMediaTime()
        let screen = NSScreen.main?.frame ?? window.frame
        window.catView.physics.step(dt: dt, windowFrameInScreen: window.frame,
                                    scripted: driver.frameInput(now: now, screen: screen))
        window.catView.updateHoverAndClickThrough()
        window.catView.needsDisplay = true
    }

    func applyScale(_ scale: CGFloat) { remoteWindow?.applyScale(scale) }
    func applyWindowLevel() { remoteWindow?.applyWindowLevelAndBehavior() }
    func resetPosition() { remoteWindow?.resetPosition() }
    func shutdown() { link.stop() }

    // MARK: Our side → friend

    func noteKeyDown(_ key: PhysicalKey) { link.noteKeyDown(key) }
    func noteKeyUp(_ key: PhysicalKey) { link.noteKeyUp(key) }
    func noteClick(button: Int, isDown: Bool) { link.noteClick(button: button, isDown: isDown) }
    func noteScroll(_ delta: CGFloat) { link.noteScroll(delta) }
    func profileChanged() { link.profileChanged() }

    private func profile() -> CatLinkEvent {
        let s = CatSettings.shared
        let audio = AudioOutputMonitor.shared
        return CatLinkEvent(k: "hello", n: nickname.isEmpty ? nil : nickname,
                            coat: CatLinkEvent.themeName(s.coatTheme), kb: CatLinkEvent.themeName(s.keyboardTheme),
                            eye: CatLinkEvent.themeName(s.eyeTheme), hp: audio.isHeadphones, pl: audio.isPlaying)
    }

    // MARK: Friend → our screen

    private func applyFriend(_ event: CatLinkEvent) {
        if event.k == "pet" {
            // The friend is petting OUR cat from their screen.
            guard let physics = mainWindow?.catView.physics else { return }
            physics.registerPetStroke(canvasPoint: CGPoint(x: event.x ?? 184, y: event.y ?? 100),
                                      delta: CGFloat(event.d ?? 6), isHead: event.h ?? true)
            let now = CACurrentMediaTime()
            if now - lastHeart > 1.1 { lastHeart = now; physics.spawnFloatingNote("♥") }
            return
        }
        ensureRemoteWindow()
        driver?.apply(event, now: CACurrentMediaTime())
        if event.k == "hello" { updateLabel() }
    }

    private var shouldShowRemote: Bool {
        showFriendCat && (isDemo || link.status != .off) && driver?.hasProfile == true
    }

    private func ensureRemoteWindow() {
        if remoteWindow == nil, let main = mainWindow {
            let window = OverlayWindow(positionKey: "KuroCat.FriendPos", defaultShiftX: -(main.frame.width + 12))
            window.catView.isRemote = true
            window.catView.onPetStroke = { [weak self] pt, delta, isHead in
                guard let self, !self.isDemo else { return }
                self.link.notePetFriendCat(canvasPoint: pt, delta: delta, isHead: isHead)
            }
            window.catView.onRightClickMenu = { [weak self, weak window] event in
                guard let self, let window, let menu = self.menuProvider?() else { return }
                NSMenu.popUpContextMenu(menu, with: event, for: window.catView)
            }
            remoteWindow = window
            driver = RemoteCatDriver(physics: window.catView.physics)
        }
        if let window = remoteWindow, !window.isVisible, showFriendCat {
            window.orderFrontRegardless()
        }
    }

    private func updateLabel() {
        guard let view = remoteWindow?.catView else { return }
        if isDemo {
            view.nameLabel = "演示 · 模拟的朋友"
            view.nameLabelDimmed = false
            return
        }
        let name = link.friendName.map { "\($0) 的猫" } ?? "朋友的猫"
        switch link.status {
        case .linked: view.nameLabel = name + " · 在线"; view.nameLabelDimmed = false
        case .friendOffline: view.nameLabel = name + " · 离线"; view.nameLabelDimmed = true
        case .connecting, .reconnecting: view.nameLabel = name + " · 正在重新连接…"; view.nameLabelDimmed = true
        case .waitingForFriend, .off: view.nameLabel = name; view.nameLabelDimmed = true
        }
        view.needsDisplay = true
    }

    private func statusChanged() {
        if link.status == .friendOffline || link.status == .off { driver?.releaseAll() }
        if link.status == .off && !isDemo { remoteWindow?.orderOut(nil) }
        updateLabel()
        onIndicatorChange?(link.status == .linked)
    }

    // MARK: Menu

    func menuItem() -> NSMenuItem {
        let parent = NSMenuItem(title: link.status == .linked ? "🔗 猫咪连线（已连线）" : "🔗 猫咪连线", action: nil, keyEquivalent: "")
        let sub = NSMenu()
        let friend = link.friendName ?? "朋友"
        let status: String
        switch link.status {
        case .off: status = "未连线"
        case .connecting: status = "正在连接服务器…"
        case .waitingForFriend: status = "已连接，等待朋友输入配对码"
        case .linked: status = "已和 \(friend) 连线 🟢"
        case .friendOffline: status = "\(friend) 已离线，等待对方回来"
        case .reconnecting: status = "网络中断，正在重新连接…"
        }
        sub.addItem(disabled("状态：\(status)"))
        if link.status == .reconnecting, let error = link.lastError { sub.addItem(disabled("原因：\(error)")) }
        sub.addItem(.separator())

        if link.status == .off {
            sub.addItem(item("创建配对码，邀请朋友…", #selector(createCode)))
            sub.addItem(item("输入朋友的配对码…", #selector(joinCode)))
            if let code = savedCode { sub.addItem(item("重新连线（\(code.display)）", #selector(reconnect))) }
        } else if let code = link.code {
            sub.addItem(item("复制配对码 \(code.display)", #selector(copyCode)))
            sub.addItem(item("断开连线", #selector(disconnect)))
        }
        sub.addItem(.separator())

        let serverParent = NSMenuItem(title: "新配对码使用的服务器", action: nil, keyEquivalent: "")
        let serverMenu = NSMenu()
        for server in CatLinkCode.Server.allCases {
            let it = item(server.title, #selector(selectServer(_:)))
            it.representedObject = server.rawValue
            it.state = server == newCodeServer ? .on : .off
            serverMenu.addItem(it)
        }
        serverParent.submenu = serverMenu
        sub.addItem(serverParent)
        sub.addItem(item("我的昵称：\(nickname.isEmpty ? "未设置" : nickname)…", #selector(setNickname)))
        sub.addItem(.separator())
        sub.addItem(item("🎬 演示：模拟一位朋友", #selector(runDemo)))
        sub.addItem(item("对方能看到什么？", #selector(explain)))
        parent.submenu = sub
        return parent
    }

    private func item(_ title: String, _ action: Selector) -> NSMenuItem {
        let it = NSMenuItem(title: title, action: action, keyEquivalent: "")
        it.target = self
        return it
    }

    private func disabled(_ title: String) -> NSMenuItem {
        NSMenuItem(title: title, action: nil, keyEquivalent: "")
    }

    // MARK: Actions

    private func start(_ code: CatLinkCode) {
        defaults.set(code.display, forKey: "KuroCat.LinkCode")
        defaults.set(true, forKey: "KuroCat.LinkEnabled")
        link.start(code: code)
    }

    @objc private func createCode() {
        let code = CatLinkCode.generate(server: newCodeServer)
        start(code)
        let alert = NSAlert()
        alert.messageText = "把这个配对码发给你的朋友"
        alert.informativeText = "朋友在 KuroCat 菜单「🔗 猫咪连线 → 输入朋友的配对码」里粘贴它，你们的猫就会出现在对方的屏幕上。\n配对码就是你们的密钥，请只发给这位朋友。\n\n" + Self.whatIsShared
        alert.accessoryView = selectableField(code.display)
        alert.addButton(withTitle: "复制配对码")
        alert.addButton(withTitle: "完成")
        NSApp.activate(ignoringOtherApps: true)
        if alert.runModal() == .alertFirstButtonReturn { copy(code.display) }
    }

    @objc private func joinCode() {
        let alert = NSAlert()
        alert.messageText = "输入朋友的配对码"
        alert.informativeText = "形如 KC-C-XXXX-XXXX-XXXX-XXXX-XXXX。\n\n" + Self.whatIsShared
        let field = NSTextField(frame: NSRect(x: 0, y: 0, width: 300, height: 24))
        field.placeholderString = "KC-C-XXXX-XXXX-XXXX-XXXX-XXXX"
        if let clip = NSPasteboard.general.string(forType: .string), let code = CatLinkCode(parsing: clip) {
            field.stringValue = code.display
        }
        alert.accessoryView = field
        alert.addButton(withTitle: "连线")
        alert.addButton(withTitle: "取消")
        NSApp.activate(ignoringOtherApps: true)
        alert.window.initialFirstResponder = field
        guard alert.runModal() == .alertFirstButtonReturn else { return }
        guard let code = CatLinkCode(parsing: field.stringValue) else {
            let error = NSAlert()
            error.messageText = "配对码不对"
            error.informativeText = "请检查是否完整复制了配对码（KC- 开头，共 5 组 4 个字符）。"
            error.runModal()
            return
        }
        start(code)
    }

    @objc private func reconnect() {
        guard let code = savedCode else { return }
        start(code)
    }

    @objc private func copyCode() {
        if let code = link.code { copy(code.display) }
    }

    @objc private func disconnect() {
        defaults.set(false, forKey: "KuroCat.LinkEnabled")
        link.stop()
    }

    @objc private func selectServer(_ sender: NSMenuItem) {
        defaults.set(sender.representedObject as? String, forKey: "KuroCat.LinkServer")
    }

    @objc private func setNickname() {
        let alert = NSAlert()
        alert.messageText = "你的昵称"
        alert.informativeText = "会显示在朋友屏幕上你的猫的上方。"
        let field = NSTextField(frame: NSRect(x: 0, y: 0, width: 240, height: 24))
        field.stringValue = nickname
        alert.accessoryView = field
        alert.addButton(withTitle: "保存")
        alert.addButton(withTitle: "取消")
        NSApp.activate(ignoringOtherApps: true)
        alert.window.initialFirstResponder = field
        guard alert.runModal() == .alertFirstButtonReturn else { return }
        defaults.set(String(field.stringValue.trimmingCharacters(in: .whitespaces).prefix(16)), forKey: "KuroCat.LinkNickname")
        link.profileChanged()
    }


    @objc private func explain() {
        let alert = NSAlert()
        alert.messageText = "猫咪连线会分享什么？"
        alert.informativeText = Self.whatIsShared + "\n\n断开连线后就不再发送任何内容。"
        NSApp.activate(ignoringOtherApps: true)
        alert.runModal()
    }

    private func copy(_ text: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }

    private func selectableField(_ text: String) -> NSTextField {
        let field = NSTextField(labelWithString: text)
        field.isSelectable = true
        field.font = .monospacedSystemFont(ofSize: 15, weight: .semibold)
        field.sizeToFit()
        return field
    }

    // MARK: Demo — a pretend friend, entirely local (nothing is sent)

    @objc private func runDemo() {
        let duration: CFTimeInterval = 24
        demoUntil = CACurrentMediaTime() + duration
        var script: [(CFTimeInterval, CatLinkEvent)] = []
        let others = CatCoatTheme.allCases.filter { $0 != CatSettings.shared.coatTheme }
        let coat = others.randomElement() ?? .siamese
        script.append((0, CatLinkEvent(k: "hello", coat: CatLinkEvent.themeName(coat), kb: "classicMilk",
                                       eye: coat == .siamese ? "siameseBlue" : "softAmber", hp: false, pl: false)))
        // 0.5–5 s: typing bursts with both paws
        var t: CFTimeInterval = 0.5, id = 1
        while t < 5 {
            let side = Bool.random() ? "l" : "r"
            script.append((t, CatLinkEvent(k: "kd", id: id, s: side)))
            script.append((t + 0.07, CatLinkEvent(k: "ku", id: id)))
            id += 1
            t += Double.random(in: 0.1...0.22)
            if Int.random(in: 0..<9) == 0 { t += 0.6 }
        }
        // 6–8.5 s: waving the mouse fast (hunting eyes), 9–10.5 s: scrolling, 11 s: a click
        for i in 0..<25 {
            let phase = Double(i) * 0.6
            script.append((6 + Double(i) * 0.1, CatLinkEvent(k: "m", x: 0.5 + 0.3 * sin(phase), y: 0.5,
                                                             vx: 1.6 * cos(phase), vy: 0)))
        }
        script.append((8.6, CatLinkEvent(k: "m", x: 0.6, y: 0.5, vx: 0, vy: 0)))
        for i in 0..<15 { script.append((9 + Double(i) * 0.1, CatLinkEvent(k: "w", d: 5))) }
        script.append((11, CatLinkEvent(k: "c", b: 0, d: 1)))
        script.append((11.12, CatLinkEvent(k: "c", b: 0, d: 0)))
        // 12.5 s: puts on headphones and plays music, 21 s: takes them off
        script.append((12.5, CatLinkEvent(k: "hello", coat: CatLinkEvent.themeName(coat), kb: "classicMilk",
                                          eye: coat == .siamese ? "siameseBlue" : "softAmber", hp: true, pl: true)))
        script.append((21, CatLinkEvent(k: "hello", coat: CatLinkEvent.themeName(coat), kb: "classicMilk",
                                        eye: coat == .siamese ? "siameseBlue" : "softAmber", hp: false, pl: false)))
        for (at, event) in script {
            DispatchQueue.main.asyncAfter(deadline: .now() + at) { [weak self] in self?.applyFriend(event) }
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + duration + 0.1) { [weak self] in
            guard let self else { return }
            if self.link.status == .off { self.remoteWindow?.orderOut(nil) }
            self.updateLabel()
        }
    }
}
