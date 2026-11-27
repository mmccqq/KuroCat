import Foundation
import AppKit
import Security
import CryptoKit

final class AppDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate {
    private var statusItem: NSStatusItem!
    private var overlayWindow: OverlayWindow!
    private var renderTimer: Timer?
    private var lastFrameTime: CFTimeInterval = CACurrentMediaTime()
    private var isPetVisible = true
    private var availableUpdateVersion: String?
    private var availableUpdateURL: URL?
    private var availableAssetURL: URL?
    private var isUpdating = false
    private var isHandingOverToNewerCopy = false
    private let catLink = CatLinkController()

    /// This fork's version, e.g. "1.3.3-mmccqq.5" (Info.plist KuroCatForkVersion); release tags are "v" + it.
    private var currentAppVersion: String {
        Bundle.main.infoDictionary?["KuroCatForkVersion"] as? String
            ?? Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "1.3.3"
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.accessory)
        if resolveDuplicateRunningCopies() { return }

        overlayWindow = OverlayWindow()
        overlayWindow.orderFrontRegardless()

        let catView = overlayWindow.catView
        catView.onRequestPermission = { [weak self] in
            self?.openInputMonitoringSettings()
        }
        catView.onRightClickMenu = { [weak self] event in
            guard let self else { return }
            let menu = self.buildMenu()
            NSMenu.popUpContextMenu(menu, with: event, for: self.overlayWindow.catView)
        }

        // Wire dual-layer InputEngine -> CatPhysics
        let engine = InputEngine.shared
        engine.onKeyDown = { [weak self] key in
            DispatchQueue.main.async {
                self?.overlayWindow.catView.physics.triggerKeyDown(key)
                self?.catLink.noteKeyDown(key)
                BreakReminderCenter.shared.noteInput()
            }
        }
        engine.onKeyUp = { [weak self] key in
            DispatchQueue.main.async {
                self?.catLink.noteKeyUp(key)
            }
        }
        engine.onMouseClick = { [weak self] button, isDown in
            DispatchQueue.main.async {
                self?.overlayWindow.catView.physics.triggerMouseClick(button: button, isDown: isDown)
                self?.catLink.noteClick(button: button, isDown: isDown)
                if isDown { BreakReminderCenter.shared.noteInput() }
            }
        }
        engine.onScroll = { [weak self] delta in
            DispatchQueue.main.async {
                self?.overlayWindow.catView.physics.triggerScroll(delta: delta)
                self?.catLink.noteScroll(delta)
                BreakReminderCenter.shared.noteInput()
            }
        }
        engine.start()

        // Sound output -> AirPods on (headphones) / off (speakers)
        let audio = AudioOutputMonitor.shared
        audio.onHeadphonesChange = { [weak self] isHeadphones, animated in
            self?.overlayWindow.catView.physics.setHeadphonesConnected(isHeadphones, animated: animated)
            self?.catLink.profileChanged()
        }
        audio.onPlayingChange = { [weak self] playing in
            self?.overlayWindow.catView.physics.headphonesPlaying = playing
            self?.catLink.profileChanged()
        }
        audio.start()

        // Claude Code: bubbles when a session finishes or waits for an approval (local only)
        catView.showsClaudeAlerts = true
        let claude = ClaudeAlertCenter.shared
        claude.isCatVisible = { [weak self] in self?.isPetVisible ?? false }
        claude.onAlert = { [weak self] alert in
            if alert.kind == .done { self?.overlayWindow.catView.physics.cheer() }
        }
        claude.start()

        // Break reminder: the cat suggests a break after a long stretch of work (local only)
        catView.showsBreakReminder = true
        BreakReminderCenter.shared.start()

        // Cat Link: show a friend's cat, and ours on their screen
        catLink.menuProvider = { [weak self] in self?.buildMenu() ?? NSMenu() }
        catLink.onIndicatorChange = { [weak self] linked in self?.updateLinkIndicator(linked) }
        catLink.setup(mainWindow: overlayWindow)

        setupStatusItem()
        if isAutoStartEnabled() {
            // Keep LaunchAgent path updated if user moved KuroCat.app into /Applications
            _ = setAutoStartEnabled(true)
        }
        startRenderLoop()

        // Check this fork's GitHub Releases shortly after launch, then every few hours
        startUpdateChecks()
    }

    func applicationWillTerminate(_ notification: Notification) {
        // Quitting during launch because a newer copy is running: leave its stats alone.
        if isHandingOverToNewerCopy { return }
        renderTimer?.invalidate()
        catLink.shutdown()
        InputEngine.shared.flushStats()
        InputEngine.shared.stop()
    }

    // MARK: - One KuroCat at a time (newest copy wins)

    /// Users often download a new KuroCat.app while an older copy is still running (or is started
    /// by the login item from another folder). That old process kept running and kept announcing
    /// "new version available" even though they had already updated. Keep only the newest copy:
    /// quit older ones, or hand over to an already-running newer one. Returns true if THIS process quits.
    private func resolveDuplicateRunningCopies() -> Bool {
        guard let bundleID = Bundle.main.bundleIdentifier else { return false }
        let myPID = ProcessInfo.processInfo.processIdentifier
        let myURL = Bundle.main.bundleURL.standardizedFileURL
        let others = NSRunningApplication.runningApplications(withBundleIdentifier: bundleID)
            .filter { $0.processIdentifier != myPID }
        if others.isEmpty { return false }

        for other in others {
            guard let otherURL = other.bundleURL?.standardizedFileURL, otherURL != myURL,
                  let otherVersion = Self.shortVersion(ofAppAt: otherURL) else { continue }
            if isVersion(otherVersion, newerThan: currentAppVersion) {
                isHandingOverToNewerCopy = true
                _ = other.activate(options: [])
                NSApp.terminate(nil)
                return true
            }
        }
        for other in others {
            // Same path (replaced in place by an update) or an older/equal copy elsewhere.
            if !other.terminate() { _ = other.forceTerminate() }
            DispatchQueue.main.asyncAfter(deadline: .now() + 3.0) {
                if !other.isTerminated { _ = other.forceTerminate() }
            }
        }
        return false
    }

    /// Reads CFBundleShortVersionString straight from disk (Bundle(url:) may return cached info).
    private static func shortVersion(ofAppAt url: URL) -> String? {
        let plistURL = url.appendingPathComponent("Contents/Info.plist")
        guard let dict = NSDictionary(contentsOf: plistURL) as? [String: Any] else { return nil }
        return dict["KuroCatForkVersion"] as? String ?? dict["CFBundleShortVersionString"] as? String
    }

    /// A newer KuroCat.app that is already on disk (e.g. downloaded to ~/Downloads but never
    /// launched because LaunchServices just re-activated this running copy).
    private func newestDownloadedCopy() -> (url: URL, version: String)? {
        guard let bundleID = Bundle.main.bundleIdentifier else { return nil }
        let myURL = Bundle.main.bundleURL.standardizedFileURL
        var best: (url: URL, version: String)?
        for url in NSWorkspace.shared.urlsForApplications(withBundleIdentifier: bundleID) {
            let std = url.standardizedFileURL
            guard std != myURL, !std.path.contains("/.Trash/"),
                  let v = Self.shortVersion(ofAppAt: std),
                  isVersion(v, newerThan: best?.version ?? currentAppVersion) else { continue }
            best = (std, v)
        }
        return best
    }

    private func switchToDownloadedCopy(_ url: URL) {
        let config = NSWorkspace.OpenConfiguration()
        config.createsNewApplicationInstance = true
        NSWorkspace.shared.openApplication(at: url, configuration: config) { _, error in
            DispatchQueue.main.async {
                if error == nil { NSApp.terminate(nil) }
            }
        }
    }

    // MARK: - 60 FPS Render Loop

    private func startRenderLoop() {
        lastFrameTime = CACurrentMediaTime()
        let timer = Timer(timeInterval: 1.0 / 60.0, repeats: true) { [weak self] _ in
            guard let self else { return }
            let now = CACurrentMediaTime()
            let dt = CGFloat(now - self.lastFrameTime)
            self.lastFrameTime = now

            let claude = ClaudeAlertCenter.shared
            claude.step(now: now)
            let breaks = BreakReminderCenter.shared
            breaks.step()
            self.overlayWindow.catView.physics.wantsAttention = claude.needsAttention || breaks.isWaving
            if self.isPetVisible {
                let view = self.overlayWindow.catView
                view.physics.step(dt: dt, windowFrameInScreen: self.overlayWindow.frame)
                view.updateHoverAndClickThrough()
                view.needsDisplay = true
            } else {
                // Our cat is hidden, but Cat Link still needs live mouse speed for the friend.
                InputEngine.shared.pollMouse(dt: min(dt, 1.0 / 25.0))
            }
            self.catLink.step(dt: dt)
        }
        RunLoop.main.add(timer, forMode: .common)
        renderTimer = timer
    }

    // MARK: - Menu Bar Icon & Menu

    private func setupStatusItem() {
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
        if let button = statusItem.button {
            button.image = makeMenuBarCatIcon()
            button.toolTip = tr("KuroCat 黑猫键盘桌宠", "KuroCat keyboard cat")
        }
        let menu = NSMenu()
        menu.delegate = self
        statusItem.menu = menu
    }

    /// A small 🔗 next to the menu bar cat while linked with an online friend.
    private func updateLinkIndicator(_ linked: Bool) {
        guard let statusItem, let button = statusItem.button else { return }
        statusItem.length = linked ? NSStatusItem.variableLength : NSStatusItem.squareLength
        button.title = linked ? "🔗" : ""
        button.imagePosition = linked ? .imageLeft : .imageOnly
        button.toolTip = !linked ? tr("KuroCat 黑猫键盘桌宠", "KuroCat keyboard cat")
            : catLink.isSharingPaused ? tr("KuroCat 黑猫键盘桌宠 · 猫咪连线中（暂停分享）", "KuroCat keyboard cat · Cat Link on (sharing paused)")
            : tr("KuroCat 黑猫键盘桌宠 · 猫咪连线中", "KuroCat keyboard cat · Cat Link on")
    }

    func menuNeedsUpdate(_ menu: NSMenu) {
        menu.removeAllItems()
        for item in buildMenu().items {
            item.menu?.removeItem(item)
            menu.addItem(item)
        }
    }

    private func buildMenu() -> NSMenu {
        let menu = NSMenu()
        let engine = InputEngine.shared
        let settings = CatSettings.shared
        let physics = overlayWindow.catView.physics

        // Status header
        let statusPrefix = engine.status == .needsPermission ? "⚠️" : "🟢"
        let statusItem = NSMenuItem(title: "\(statusPrefix) \(engine.status.displayName)", action: engine.status == .needsPermission ? #selector(openInputMonitoringSettings) : nil, keyEquivalent: "")
        statusItem.target = self
        menu.addItem(statusItem)

        if engine.status == .needsPermission {
            let grantItem = NSMenuItem(title: tr("🔑 前往系统设置开启「输入监控」...", "🔑 Open System Settings to allow Input Monitoring..."), action: #selector(openInputMonitoringSettings), keyEquivalent: "")
            grantItem.target = self
            menu.addItem(grantItem)
        }

        // Live stats
        let statsTitle = tr("⌨️ 今日击键 \(engine.totalKeystrokesToday)  ·  KPM \(engine.currentKPM)  ·  摸猫 \(physics.totalPetSessions) 次", "⌨️ Keys today \(engine.totalKeystrokesToday)  ·  KPM \(engine.currentKPM)  ·  Pets \(physics.totalPetSessions)")
        let statsItem = NSMenuItem(title: statsTitle, action: nil, keyEquivalent: "")
        statsItem.isEnabled = false
        menu.addItem(statsItem)

        menu.addItem(.separator())

        // Visibility & Interactive modes
        // Which cats to show: ours and the friend's (Cat Link) independently, or all at once.
        let showParent = NSMenuItem(title: tr("显示的猫咪", "Cats to show"), action: nil, keyEquivalent: "")
        let showMenu = NSMenu()
        let mineItem = NSMenuItem(title: tr("我的猫", "My cat"), action: #selector(toggleVisibility), keyEquivalent: "")
        mineItem.target = self
        mineItem.state = isPetVisible ? .on : .off
        showMenu.addItem(mineItem)
        let friendItem = NSMenuItem(title: tr("朋友的猫", "Friend's cat"), action: #selector(toggleFriendVisibility), keyEquivalent: "")
        friendItem.target = self
        friendItem.state = catLink.showFriendCat ? .on : .off
        showMenu.addItem(friendItem)
        showMenu.addItem(.separator())
        let showAll = NSMenuItem(title: tr("全部显示", "Show all"), action: #selector(showAllCats), keyEquivalent: "")
        showAll.target = self
        showMenu.addItem(showAll)
        let hideAll = NSMenuItem(title: tr("全部隐藏", "Hide all"), action: #selector(hideAllCats), keyEquivalent: "")
        hideAll.target = self
        showMenu.addItem(hideAll)
        showParent.submenu = showMenu
        menu.addItem(showParent)

        let clickThroughItem = NSMenuItem(title: tr("完全鼠标穿透 (按住 ⌥Option 可临时右键/拖动)", "Full click-through (hold ⌥ Option to right-click or drag)"), action: #selector(toggleClickThrough), keyEquivalent: "")
        clickThroughItem.target = self
        clickThroughItem.state = settings.fullClickThrough ? .on : .off
        menu.addItem(clickThroughItem)

        let fullscreenItem = NSMenuItem(title: tr("全屏游戏/应用置顶显示", "Show over full-screen games and apps"), action: #selector(toggleFullScreenOverlay), keyEquivalent: "")
        fullscreenItem.target = self
        fullscreenItem.state = settings.fullScreenOverlay ? .on : .off
        menu.addItem(fullscreenItem)

        let autoStartItem = NSMenuItem(title: tr("开机自动启动", "Start at login"), action: #selector(toggleAutoStart), keyEquivalent: "")
        autoStartItem.target = self
        autoStartItem.state = isAutoStartEnabled() ? .on : .off
        menu.addItem(autoStartItem)

        menu.addItem(.separator())

        // Features
        let badgesItem = NSMenuItem(title: tr("显示按键气泡 ([S] [W] [⇧])", "Show key bubbles ([S] [W] [⇧])"), action: #selector(toggleKeyBadges), keyEquivalent: "")
        badgesItem.target = self
        badgesItem.state = settings.showKeyBadges ? .on : .off
        menu.addItem(badgesItem)

        let mouseBadgesItem = NSMenuItem(title: tr("显示鼠标点击气泡 ([左键] [右键])", "Show mouse click bubbles ([Left] [Right])"), action: #selector(toggleMouseBadges), keyEquivalent: "")
        mouseBadgesItem.target = self
        mouseBadgesItem.state = settings.showMouseBadges ? .on : .off
        menu.addItem(mouseBadgesItem)

        let legendsItem = NSMenuItem(title: tr("显示键盘字符", "Show key labels"), action: #selector(toggleKeyLegends), keyEquivalent: "")
        legendsItem.target = self
        legendsItem.state = settings.showKeyLegends ? .on : .off
        menu.addItem(legendsItem)

        let footItem = NSMenuItem(title: tr("3键同时按「用脚按」彩蛋 🐾", "3 keys at once: press one with a foot 🐾"), action: #selector(toggleFootEgg), keyEquivalent: "")
        footItem.target = self
        footItem.state = settings.footEasterEgg ? .on : .off
        menu.addItem(footItem)

        let petItem = NSMenuItem(title: tr("摸头/摸背闭眼互动", "Close eyes when petted"), action: #selector(togglePetInteraction), keyEquivalent: "")
        petItem.target = self
        petItem.state = settings.petInteraction ? .on : .off
        menu.addItem(petItem)

        let earbudItem = NSMenuItem(title: tr("连接耳机时戴上 AirPods 🎧", "Wear AirPods with headphones 🎧"), action: #selector(toggleEarbudAnimation), keyEquivalent: "")
        earbudItem.target = self
        earbudItem.state = settings.earbudAnimation ? .on : .off
        menu.addItem(earbudItem)

        let mosquitoItem = NSMenuItem(title: tr("睡着时会有蚊子来捣乱 🦟", "Mosquito visits while asleep 🦟"), action: #selector(toggleMosquito), keyEquivalent: "")
        mosquitoItem.target = self
        mosquitoItem.state = settings.mosquitoVisits ? .on : .off
        menu.addItem(mosquitoItem)
        let mosquitoDemo = NSMenuItem(title: tr("🎬 演示：蚊子来了", "🎬 Demo: here comes a mosquito"), action: #selector(runMosquitoDemo), keyEquivalent: "")
        mosquitoDemo.target = self
        menu.addItem(mosquitoDemo)

        menu.addItem(ClaudeAlertCenter.shared.menuItem())
        menu.addItem(BreakReminderCenter.shared.menuItem())

        menu.addItem(.separator())
        menu.addItem(catLink.menuItem())
        menu.addItem(.separator())

        // Scale submenu
        let scaleMenu = NSMenu()
        let scales: [(String, CGFloat)] = [
            (tr("迷你 (0.65x)", "Mini (0.65x)"), 0.65),
            (tr("偏小 (0.85x)", "Small (0.85x)"), 0.85),
            (tr("标准 (1.0x)", "Standard (1.0x)"), 1.0),
            (tr("偏大 (1.25x)", "Large (1.25x)"), 1.25),
            (tr("超大 (1.5x)", "Extra large (1.5x)"), 1.5)
        ]
        for (title, val) in scales {
            let item = NSMenuItem(title: title, action: #selector(selectScale(_:)), keyEquivalent: "")
            item.target = self
            item.representedObject = NSNumber(value: Double(val))
            item.state = abs(settings.scale - val) < 0.05 ? .on : .off
            scaleMenu.addItem(item)
        }
        let scaleParent = NSMenuItem(title: tr("尺寸大小", "Size"), action: nil, keyEquivalent: "")
        scaleParent.submenu = scaleMenu
        menu.addItem(scaleParent)

        // Cat coat submenu: original skins plus approved shorthair and orange-white.
        let coatMenu = NSMenu()
        for coat in CatCoatTheme.allCases {
            let item = NSMenuItem(title: coat.displayName, action: #selector(selectCoatTheme(_:)), keyEquivalent: "")
            item.target = self
            item.representedObject = coat.rawValue
            item.state = settings.coatTheme == coat ? .on : .off
            coatMenu.addItem(item)
        }
        let coatParent = NSMenuItem(title: tr("猫咪皮肤 🐱", "Cat coat 🐱"), action: nil, keyEquivalent: "")
        coatParent.submenu = coatMenu
        menu.addItem(coatParent)

        // Keyboard Theme submenu
        let kbMenu = NSMenu()
        for theme in KeyboardTheme.allCases {
            let item = NSMenuItem(title: theme.displayName, action: #selector(selectKeyboardTheme(_:)), keyEquivalent: "")
            item.target = self
            item.representedObject = theme.rawValue
            item.state = settings.keyboardTheme == theme ? .on : .off
            kbMenu.addItem(item)
        }
        let kbParent = NSMenuItem(title: tr("键盘配色", "Keyboard colors"), action: nil, keyEquivalent: "")
        kbParent.submenu = kbMenu
        menu.addItem(kbParent)

        // Eye Color submenu
        let eyeMenu = NSMenu()
        for eye in EyeColorTheme.allCases {
            let item = NSMenuItem(title: eye.displayName, action: #selector(selectEyeTheme(_:)), keyEquivalent: "")
            item.target = self
            item.representedObject = eye.rawValue
            item.state = settings.eyeTheme == eye ? .on : .off
            eyeMenu.addItem(item)
        }
        let eyeParent = NSMenuItem(title: tr("瞳色切换", "Eye color"), action: nil, keyEquivalent: "")
        eyeParent.submenu = eyeMenu
        menu.addItem(eyeParent)

        menu.addItem(.separator())

        let demoItem = NSMenuItem(title: tr("🎬 演示打字 + 摸猫动画", "🎬 Demo: typing + petting"), action: #selector(runDemoBurst), keyEquivalent: "")
        demoItem.target = self
        menu.addItem(demoItem)

        let resetPosItem = NSMenuItem(title: tr("重置位置到右下角", "Move back to the bottom-right corner"), action: #selector(resetPosition), keyEquivalent: "")
        resetPosItem.target = self
        menu.addItem(resetPosItem)

        let relaunchItem = NSMenuItem(title: tr("重启 KuroCat (授权后刷新)", "Restart KuroCat (after granting permission)"), action: #selector(relaunchApp), keyEquivalent: "")
        relaunchItem.target = self
        menu.addItem(relaunchItem)

        // Language: the title is in both languages so it can be found whichever one is showing.
        let languageMenu = NSMenu()
        for (language, title) in [(AppLanguage.system, "跟随系统 / System"), (.chinese, "中文"), (.english, "English")] {
            let item = NSMenuItem(title: title, action: #selector(selectLanguage(_:)), keyEquivalent: "")
            item.target = self
            item.representedObject = language.rawValue
            item.state = L10n.setting == language ? .on : .off
            languageMenu.addItem(item)
        }
        let languageParent = NSMenuItem(title: "语言 / Language", action: nil, keyEquivalent: "")
        languageParent.submenu = languageMenu
        menu.addItem(languageParent)

        if !Self.updateChecksEnabled {
            let versionItem = NSMenuItem(title: tr("版本 v\(currentAppVersion) (分支版，已关闭更新检查)", "Version v\(currentAppVersion) (fork, update check off)"), action: nil, keyEquivalent: "")
            versionItem.isEnabled = false
            menu.addItem(versionItem)
        } else if let ver = availableUpdateVersion {
            let title = isUpdating ? tr("⏳ 正在下载更新 \(ver)...", "⏳ Downloading update \(ver)...") : tr("🆕 发现新版本 \(ver) — 点击自动更新", "🆕 New version \(ver) — click to update")
            let updateItem = NSMenuItem(title: title, action: #selector(openUpdateDownload), keyEquivalent: "")
            updateItem.target = self
            updateItem.isEnabled = !isUpdating
            menu.addItem(updateItem)
        } else {
            let checkItem = NSMenuItem(title: tr("检查更新 (当前 v\(currentAppVersion))", "Check for updates (now v\(currentAppVersion))"), action: #selector(manualCheckForUpdates), keyEquivalent: "")
            checkItem.target = self
            menu.addItem(checkItem)
        }

        menu.addItem(.separator())

        let quitItem = NSMenuItem(title: tr("退出 KuroCat", "Quit KuroCat"), action: #selector(quitApp), keyEquivalent: "q")
        quitItem.target = self
        menu.addItem(quitItem)

        return menu
    }

    // MARK: - Actions

    @objc private func openInputMonitoringSettings() {
        InputEngine.shared.requestPermissionsAndOpenSettings()
    }

    @objc private func toggleVisibility() {
        isPetVisible.toggle()
        if isPetVisible {
            overlayWindow.orderFrontRegardless()
        } else {
            overlayWindow.orderOut(nil)
        }
    }

    @objc private func toggleFriendVisibility() {
        catLink.showFriendCat.toggle()
    }

    @objc private func showAllCats() {
        if !isPetVisible { toggleVisibility() }
        catLink.showFriendCat = true
    }

    @objc private func hideAllCats() {
        if isPetVisible { toggleVisibility() }
        catLink.showFriendCat = false
    }

    @objc private func toggleClickThrough() {
        CatSettings.shared.fullClickThrough.toggle()
    }

    @objc private func toggleFullScreenOverlay() {
        CatSettings.shared.fullScreenOverlay.toggle()
        overlayWindow.applyWindowLevelAndBehavior()
        catLink.applyWindowLevel()
    }

    // MARK: - Launch at Login (macOS LaunchAgent — works reliably for signed & ad-hoc builds)

    private var launchAgentURL: URL {
        let dir = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/LaunchAgents", isDirectory: true)
        return dir.appendingPathComponent("com.chanke.kurocat.plist")
    }

    private func isAutoStartEnabled() -> Bool {
        FileManager.default.fileExists(atPath: launchAgentURL.path)
    }

    @discardableResult
    private func setAutoStartEnabled(_ enable: Bool) -> Bool {
        let fm = FileManager.default
        let url = launchAgentURL
        if enable {
            do {
                try fm.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
                let appPath = Bundle.main.bundlePath
                let plist: [String: Any] = [
                    "Label": "com.chanke.kurocat",
                    "ProgramArguments": ["/usr/bin/open", "-a", appPath],
                    "RunAtLoad": true,
                    "KeepAlive": false
                ]
                let data = try PropertyListSerialization.data(fromPropertyList: plist, format: .xml, options: 0)
                try data.write(to: url, options: .atomic)
                return true
            } catch {
                return false
            }
        } else {
            do {
                if fm.fileExists(atPath: url.path) {
                    try fm.removeItem(at: url)
                }
                return true
            } catch {
                return false
            }
        }
    }

    @objc private func toggleAutoStart() {
        _ = setAutoStartEnabled(!isAutoStartEnabled())
    }

    @objc private func toggleKeyBadges() {
        CatSettings.shared.showKeyBadges.toggle()
    }

    @objc private func toggleMouseBadges() {
        CatSettings.shared.showMouseBadges.toggle()
    }

    @objc private func toggleKeyLegends() {
        CatSettings.shared.showKeyLegends.toggle()
    }

    @objc private func toggleFootEgg() {
        CatSettings.shared.footEasterEgg.toggle()
    }

    @objc private func togglePetInteraction() {
        CatSettings.shared.petInteraction.toggle()
    }

    @objc private func selectLanguage(_ sender: NSMenuItem) {
        guard let raw = sender.representedObject as? String, let language = AppLanguage(rawValue: raw) else { return }
        L10n.setting = language
        // Menus are rebuilt each time they open; refresh what is already on screen.
        updateLinkIndicator(catLink.isLinkIndicatorOn)
        catLink.languageChanged()
        overlayWindow.catView.needsDisplay = true
    }

    @objc private func toggleMosquito() {
        CatSettings.shared.mosquitoVisits.toggle()
    }

    @objc private func runMosquitoDemo() {
        overlayWindow.catView.physics.summonMosquito()
    }

    @objc private func toggleEarbudAnimation() {
        CatSettings.shared.earbudAnimation.toggle()
    }

    @objc private func selectScale(_ sender: NSMenuItem) {
        guard let num = sender.representedObject as? NSNumber else { return }
        overlayWindow.applyScale(CGFloat(num.doubleValue))
        catLink.applyScale(CGFloat(num.doubleValue))
    }

    @objc private func selectCoatTheme(_ sender: NSMenuItem) {
        guard let raw = sender.representedObject as? String,
              let coat = CatCoatTheme(rawValue: raw) else { return }
        CatSettings.shared.coatTheme = coat
        // Auto-pair signature eye color (still freely overridable via 瞳色切换!)
        switch coat {
        case .classicBlack:
            CatSettings.shared.eyeTheme = .neonChartreuse
        case .siamese:
            CatSettings.shared.eyeTheme = .siameseBlue
        case .shorthair, .orangeWhite:
            CatSettings.shared.eyeTheme = .softAmber
        }
        catLink.profileChanged()
    }

    @objc private func selectKeyboardTheme(_ sender: NSMenuItem) {
        guard let raw = sender.representedObject as? String,
              let theme = KeyboardTheme(rawValue: raw) else { return }
        CatSettings.shared.keyboardTheme = theme
        catLink.profileChanged()
    }

    @objc private func selectEyeTheme(_ sender: NSMenuItem) {
        guard let raw = sender.representedObject as? String,
              let theme = EyeColorTheme(rawValue: raw) else { return }
        CatSettings.shared.eyeTheme = theme
        catLink.profileChanged()
    }

    @objc private func resetPosition() {
        overlayWindow.resetPosition()
        catLink.resetPosition()
    }

    @objc private func runDemoBurst() {
        let keys: [PhysicalKey] = [.w, .a, .s, .d, .f, .j, .o, .e, .space, .leftShift]
        for (idx, k) in keys.enumerated() {
            DispatchQueue.main.asyncAfter(deadline: .now() + Double(idx) * 0.13) { [weak self] in
                self?.overlayWindow.catView.physics.triggerKeyDown(k)
            }
        }
    }

    @objc private func relaunchApp() {
        let bundleURL = Bundle.main.bundleURL
        let config = NSWorkspace.OpenConfiguration()
        config.createsNewApplicationInstance = true
        NSWorkspace.shared.openApplication(at: bundleURL, configuration: config) { _, _ in
            DispatchQueue.main.async {
                NSApp.terminate(nil)
            }
        }
    }

    @objc private func quitApp() {
        NSApp.terminate(nil)
    }

    // MARK: - Auto Update Checker & In-App Self-Updater

    /// Off in this fork: the endpoints below serve the upstream app, and "updating" would
    /// replace this fork with upstream's build. Kept as a switch so upstream merges stay simple.
    // MARK: - Updates (only from this fork's own GitHub Releases, never from upstream)

    private static let updateChecksEnabled = true
    private static let updateRepo = "whiskerkey/whiskerkey"
    private static let latestReleaseAPI = "https://api.github.com/repos/\(updateRepo)/releases/latest"
    private static let updatePageURLString = "https://github.com/\(updateRepo)/releases/latest"
    /// KuroCat is becoming Whiskerkey (with its own app ID). While it moves, updates may use either name;
    /// the new name is preferred. The download must still be signed with this app's own certificate.
    static let macAssetNames = ["Whiskerkey-macOS-universal.zip", "KuroCat-macOS-universal.zip"]
    static let updateAppNames = ["Whiskerkey", "KuroCat"]
    static let updateIdentifiers = ["com.whiskerkey.app", "com.chanke.kurocat"]
    private static let updateCheckInterval: TimeInterval = 6 * 3600
    /// The newest version we already showed the "new version" prompt for (later checks only update the menu).
    private static let promptedTagKey = "KuroCat.UpdatePromptedTag"
    private var availableAssetSize: Int?
    private var updateTimer: Timer?

    struct UpdateInfo {
        let tag: String
        let pageURL: URL?
        let assetURL: URL?
        let assetSize: Int?
    }

    @objc private func manualCheckForUpdates() {
        checkForUpdates(manual: true)
    }

    @objc private func openUpdateDownload() {
        performInAppUpdate()
    }

    private func startUpdateChecks() {
        guard Self.updateChecksEnabled else { return }
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.5) { [weak self] in
            self?.checkForUpdates(manual: false)
        }
        updateTimer = Timer.scheduledTimer(withTimeInterval: Self.updateCheckInterval, repeats: true) { [weak self] _ in
            self?.checkForUpdates(manual: false)
        }
    }

    /// GitHub's "latest release" JSON -> tag, release page, and the macOS zip (URL + exact size).
    static func parseUpdateInfo(from data: Data) -> UpdateInfo? {
        guard let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let tag = json["tag_name"] as? String, !tag.isEmpty else { return nil }
        let page = (json["html_url"] as? String).flatMap(URL.init(string:))
        let assets = json["assets"] as? [[String: Any]] ?? []
        let asset = Self.macAssetNames.lazy.compactMap { name in assets.first { ($0["name"] as? String) == name } }.first
        let assetURL = (asset?["browser_download_url"] as? String).flatMap(URL.init(string:))
        return UpdateInfo(tag: tag, pageURL: page, assetURL: assetURL, assetSize: asset?["size"] as? Int)
    }

    private enum UpdateCheckOutcome { case found(UpdateInfo), noRelease, failed }

    private func fetchLatestRelease(completion: @escaping (UpdateCheckOutcome) -> Void) {
        var req = URLRequest(url: URL(string: Self.latestReleaseAPI)!, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 15)
        req.setValue("KuroCat/\(currentAppVersion)", forHTTPHeaderField: "User-Agent")
        req.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
        URLSession.shared.dataTask(with: req) { data, response, error in
            let status = (response as? HTTPURLResponse)?.statusCode ?? 0
            let outcome: UpdateCheckOutcome
            if status == 404 {
                outcome = .noRelease
            } else if error == nil, status == 200, let data, let info = Self.parseUpdateInfo(from: data) {
                outcome = .found(info)
            } else {
                outcome = .failed
            }
            DispatchQueue.main.async { completion(outcome) }
        }.resume()
    }

    private func checkForUpdates(manual: Bool) {
        guard Self.updateChecksEnabled, !isUpdating else { return }
        fetchLatestRelease { [weak self] outcome in
            guard let self else { return }
            switch outcome {
            case .found(let info):
                self.handleUpdateResult(info, manual: manual)
            case .noRelease:
                if manual { self.showInfoAlert(title: tr("已是最新版本", "Up to date"), message: tr("当前版本 v\(self.currentAppVersion)，还没有发布更新的版本。", "You have v\(self.currentAppVersion). No newer version has been published yet.")) }
            case .failed:
                if manual { self.showInfoAlert(title: tr("检查更新失败", "Couldn't check for updates"), message: tr("无法连接到 GitHub，请稍后重试或检查网络连接。", "Couldn't reach GitHub. Please try again later or check your network connection.")) }
            }
        }
    }

    private func handleUpdateResult(_ info: UpdateInfo, manual: Bool) {
        if isVersion(info.tag, newerThan: currentAppVersion),
           let local = newestDownloadedCopy(), !isVersion(info.tag, newerThan: local.version) {
            // The user already downloaded the latest version; they are just still running the old copy.
            NSApp.activate(ignoringOtherApps: true)
            let alert = NSAlert()
            alert.alertStyle = .informational
            alert.messageText = tr("你已经下载了新版本 v\(local.version)", "You've already downloaded v\(local.version)")
            alert.informativeText = tr("但现在运行的还是旧版 v\(currentAppVersion)：\n\(Bundle.main.bundlePath)\n\n新版本位置：\n\(local.url.path)\n\n点击「切换到新版本」会关闭旧版并打开新版。建议把新版拖进「应用程序」文件夹并删除旧版。", "But the old v\(currentAppVersion) is still running:\n\(Bundle.main.bundlePath)\n\nThe new version is at:\n\(local.url.path)\n\n“Switch to the new version” quits the old one and opens the new one. It's best to drag the new one into Applications and delete the old one.")
            alert.addButton(withTitle: tr("切换到新版本", "Switch to the new version"))
            alert.addButton(withTitle: tr("稍后再说", "Later"))
            if alert.runModal() == .alertFirstButtonReturn {
                switchToDownloadedCopy(local.url)
            }
            return
        }
        if isVersion(info.tag, newerThan: currentAppVersion) {
            availableUpdateVersion = info.tag
            availableUpdateURL = info.pageURL
            availableAssetURL = info.assetURL
            availableAssetSize = info.assetSize
            // Ask once per new version; after "later" the menu keeps offering it.
            let defaults = UserDefaults.standard
            if manual || defaults.string(forKey: Self.promptedTagKey) != info.tag {
                defaults.set(info.tag, forKey: Self.promptedTagKey)
                promptUpdateAvailable(latestTag: info.tag)
            }
        } else {
            availableUpdateVersion = nil
            if manual {
                showInfoAlert(title: tr("已是最新版本", "Up to date"), message: tr("当前版本 v\(currentAppVersion) 已是最新版本。", "v\(currentAppVersion) is the latest version."))
            }
        }
    }

    /// True if the app at `url` is signed with the same certificate as this one (it satisfies this
    /// app's designated requirement). Only such builds may replace this app: a tampered download is
    /// refused, and macOS keeps the Input Monitoring permission across the update.
    static func isSignedLikeThisApp(_ url: URL) -> Bool {
        var me: SecCode?
        guard SecCodeCopySelf([], &me) == errSecSuccess, let me else { return false }
        var myStatic: SecStaticCode?
        guard SecCodeCopyStaticCode(me, [], &myStatic) == errSecSuccess, let myStatic else { return false }
        return isSigned(url, likeApp: myStatic)
    }

    /// True if `url` is a valid app signed with the same certificate as `reference`, under one of
    /// `updateIdentifiers` (KuroCat or Whiskerkey). An ad-hoc signed reference accepts nothing.
    static func isSigned(_ url: URL, likeApp reference: SecStaticCode) -> Bool {
        guard let requirement = updateRequirement(likeApp: reference) else { return false }
        var newCode: SecStaticCode?
        guard SecStaticCodeCreateWithPath(url as CFURL, [], &newCode) == errSecSuccess, let newCode else { return false }
        let flags = SecCSFlags(rawValue: SecCSFlags.RawValue(kSecCSCheckAllArchitectures | kSecCSStrictValidate | kSecCSCheckNestedCode))
        return SecStaticCodeCheckValidity(newCode, flags, requirement) == errSecSuccess
    }

    /// `certificate leaf = H"<the reference's leaf certificate>" and (identifier "…" or identifier "…")`.
    static func updateRequirement(likeApp reference: SecStaticCode) -> SecRequirement? {
        var info: CFDictionary?
        guard SecCodeCopySigningInformation(reference, SecCSFlags(rawValue: kSecCSSigningInformation), &info) == errSecSuccess,
              let dict = info as? [String: Any],
              let certificates = dict[kSecCodeInfoCertificates as String] as? [SecCertificate],
              let leaf = certificates.first else { return nil }
        let hash = Insecure.SHA1.hash(data: SecCertificateCopyData(leaf) as Data).map { String(format: "%02x", $0) }.joined()
        let identifiers = updateIdentifiers.map { "identifier \"\($0)\"" }.joined(separator: " or ")
        var requirement: SecRequirement?
        guard SecRequirementCreateWithString("certificate leaf = H\"\(hash)\" and (\(identifiers))" as CFString, [], &requirement) == errSecSuccess else { return nil }
        return requirement
    }

    /// The app inside an extracted update: Whiskerkey.app, or KuroCat.app from before the rename.
    static func updatedApp(in folder: URL) -> URL? {
        for name in updateAppNames {
            let app = folder.appendingPathComponent("\(name).app")
            if FileManager.default.isExecutableFile(atPath: app.appendingPathComponent("Contents/MacOS/\(name)").path) { return app }
        }
        return nil
    }

    /// The shell script that replaces the running app once it has quit. The update keeps its own name
    /// (Whiskerkey.app replaces KuroCat.app in the same folder). On failure the old app is put back.
    /// `open` is the command that starts an app (tests pass `true`).
    static func installScript(pid: Int32, currentApp: URL, newApp: URL, workDir: URL, open: String = "open -n") -> String {
        func q(_ s: String) -> String { "'" + s.replacingOccurrences(of: "'", with: "'\\''") + "'" }
        let old = q(currentApp.path)
        let dstURL = currentApp.deletingLastPathComponent().appendingPathComponent(newApp.lastPathComponent)
        let dst = q(dstURL.path)
        let renamed = dstURL.standardizedFileURL != currentApp.standardizedFileURL
        return """
        while kill -0 \(pid) 2>/dev/null; do sleep 0.2; done
        rm -rf \(old).bak \(dst).bak
        if mv \(old) \(old).bak && { \(renamed ? "[ ! -e \(dst) ] || mv \(dst) \(dst).bak; " : "")/usr/bin/ditto \(q(newApp.path)) \(dst); }; then
            rm -rf \(old).bak \(dst).bak
            \(open) \(dst)
        else
            \(renamed ? "rm -rf \(dst); mv \(dst).bak \(dst) 2>/dev/null; " : "")mv \(old).bak \(old) 2>/dev/null
            \(open) \(old)
        fi
        rm -rf \(q(workDir.path))
        """
    }

    /// Compares the numbers in two versions: "v1.3.3-mmccqq.5" is [1, 3, 3, 5], newer than "1.3.3".
    static func isVersion(_ lhs: String, newerThan rhs: String) -> Bool {
        func parse(_ v: String) -> [Int] {
            v.split(whereSeparator: { !$0.isNumber }).map { Int($0) ?? 0 }
        }
        let a = parse(lhs)
        let b = parse(rhs)
        let count = max(a.count, b.count)
        for i in 0..<count {
            let va = i < a.count ? a[i] : 0
            let vb = i < b.count ? b[i] : 0
            if va != vb { return va > vb }
        }
        return false
    }

    private func isVersion(_ lhs: String, newerThan rhs: String) -> Bool { Self.isVersion(lhs, newerThan: rhs) }

    private func promptUpdateAvailable(latestTag: String) {
        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        alert.alertStyle = .informational
        alert.messageText = tr("发现 KuroCat 新版本 \(latestTag)", "KuroCat \(latestTag) is available")
        alert.informativeText = tr("当前版本：v\(currentAppVersion)\n最新版本：\(latestTag)\n当前运行位置：\(Bundle.main.bundlePath)\n\n点击「立即更新」将在后台自动下载安装包并重启应用。", "Current version: v\(currentAppVersion)\nLatest version: \(latestTag)\nRunning from: \(Bundle.main.bundlePath)\n\n“Update now” downloads it in the background and restarts KuroCat.")
        alert.addButton(withTitle: tr("立即更新", "Update now"))
        alert.addButton(withTitle: tr("打开下载页", "Open download page"))
        alert.addButton(withTitle: tr("稍后再说", "Later"))
        let resp = alert.runModal()
        if resp == .alertFirstButtonReturn {
            performInAppUpdate()
        } else if resp == .alertSecondButtonReturn {
            let fallback = URL(string: Self.updatePageURLString)!
            NSWorkspace.shared.open(availableUpdateURL ?? fallback)
        }
    }

    private func showSignatureMismatchAlert() {
        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = tr("没有安装这个更新", "This update wasn't installed")
        alert.informativeText = tr("下载的新版本和当前 KuroCat 的签名证书不一致，为了安全没有替换。可以打开下载页手动下载。", "The downloaded version isn't signed with the same certificate as this KuroCat, so for safety it wasn't installed. You can download it from the download page instead.")
        alert.addButton(withTitle: tr("打开下载页", "Open download page"))
        alert.addButton(withTitle: tr("取消", "Cancel"))
        if alert.runModal() == .alertFirstButtonReturn {
            NSWorkspace.shared.open(availableUpdateURL ?? URL(string: Self.updatePageURLString)!)
        }
    }

    private func performInAppUpdate() {
        guard !isUpdating else { return }
        guard let assetURL = availableAssetURL else {
            // This release has no macOS download (yet): show the release page instead.
            NSWorkspace.shared.open(availableUpdateURL ?? URL(string: Self.updatePageURLString)!)
            return
        }
        let expectedSize = availableAssetSize
        isUpdating = true

        var req = URLRequest(url: assetURL, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 60)
        req.setValue("KuroCat/\(currentAppVersion)", forHTTPHeaderField: "User-Agent")

        URLSession.shared.downloadTask(with: req) { [weak self] tempURL, response, error in
            guard let self else { return }
            let httpStatus = (response as? HTTPURLResponse)?.statusCode ?? 0
            guard error == nil, let tempURL, httpStatus == 200 || httpStatus == 0 else {
                DispatchQueue.main.async {
                    self.isUpdating = false
                    self.handleUpdateDownloadFailure()
                }
                return
            }

            let fm = FileManager.default
            let workDir = fm.temporaryDirectory.appendingPathComponent("KuroCatUpdate-\(UUID().uuidString)")
            let zipURL = workDir.appendingPathComponent("update.zip")
            let extractDir = workDir.appendingPathComponent("extracted")
            do {
                try fm.createDirectory(at: extractDir, withIntermediateDirectories: true)
                try fm.copyItem(at: tempURL, to: zipURL)
                let attrs = try fm.attributesOfItem(atPath: zipURL.path)
                let fileSize = (attrs[.size] as? NSNumber)?.intValue ?? 0
                // Must be complete: exactly the size GitHub lists for this file.
                guard fileSize > 50_000, expectedSize == nil || fileSize == expectedSize else {
                    throw NSError(domain: "KuroCatUpdate", code: 1)
                }

                let unzip = Process()
                unzip.executableURL = URL(fileURLWithPath: "/usr/bin/ditto")
                unzip.arguments = ["-x", "-k", zipURL.path, extractDir.path]
                try unzip.run()
                unzip.waitUntilExit()
                guard unzip.terminationStatus == 0 else {
                    throw NSError(domain: "KuroCatUpdate", code: 2)
                }

                guard let newAppURL = Self.updatedApp(in: extractDir) else {
                    throw NSError(domain: "KuroCatUpdate", code: 3)
                }
                guard Self.isSignedLikeThisApp(newAppURL) else {
                    throw NSError(domain: "KuroCatUpdate", code: 4)
                }

                let currentAppURL = Bundle.main.bundleURL
                let parentWritable = fm.isWritableFile(atPath: currentAppURL.deletingLastPathComponent().path)

                DispatchQueue.main.async {
                    self.installDownloadedApp(newAppURL: newAppURL, currentAppURL: currentAppURL, workDir: workDir, needsAdmin: !parentWritable)
                }
            } catch {
                try? fm.removeItem(at: workDir)
                let signatureMismatch = (error as NSError).code == 4
                DispatchQueue.main.async {
                    self.isUpdating = false
                    if signatureMismatch { self.showSignatureMismatchAlert() } else { self.handleUpdateDownloadFailure() }
                }
            }
        }.resume()
    }

    private func installDownloadedApp(newAppURL: URL, currentAppURL: URL, workDir: URL, needsAdmin: Bool) {
        func shQuote(_ s: String) -> String {
            "'" + s.replacingOccurrences(of: "'", with: "'\\''") + "'"
        }
        let pid = ProcessInfo.processInfo.processIdentifier
        let dst = shQuote(currentAppURL.path)
        let src = shQuote(newAppURL.path)
        let tmp = shQuote(workDir.path)

        let installed = currentAppURL.deletingLastPathComponent().appendingPathComponent(newAppURL.lastPathComponent)
        if needsAdmin {
            // Whiskerkey.app replaces KuroCat.app: the old one is moved aside, then removed.
            let newDst = shQuote(installed.path)
            let clearNew = installed.standardizedFileURL == currentAppURL.standardizedFileURL ? "" : "rm -rf \(newDst) && "
            let cmd = "rm -rf \(dst).bak && mv \(dst) \(dst).bak && \(clearNew)/usr/bin/ditto \(src) \(newDst) && rm -rf \(dst).bak \(tmp)"
            let escaped = cmd.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"")
            let osa = Process()
            osa.executableURL = URL(fileURLWithPath: "/usr/bin/osascript")
            osa.arguments = ["-e", "do shell script \"\(escaped)\" with administrator privileges"]
            do {
                try osa.run()
                osa.waitUntilExit()
                if osa.terminationStatus == 0 {
                    let relaunch = Process()
                    relaunch.executableURL = URL(fileURLWithPath: "/usr/bin/open")
                    relaunch.arguments = ["-n", installed.path]
                    try? relaunch.run()
                    NSApp.terminate(nil)
                    return
                }
            } catch {}
            try? FileManager.default.removeItem(at: workDir)
            isUpdating = false
            handleUpdateDownloadFailure()
            return
        }

        let script = Self.installScript(pid: pid, currentApp: currentAppURL, newApp: newAppURL, workDir: workDir)
        let proc = Process()
        proc.executableURL = URL(fileURLWithPath: "/bin/sh")
        proc.arguments = ["-c", script]
        do {
            try proc.run()
            NSApp.terminate(nil)
        } catch {
            try? FileManager.default.removeItem(at: workDir)
            isUpdating = false
            handleUpdateDownloadFailure()
        }
    }

    private func handleUpdateDownloadFailure() {
        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = tr("自动更新失败", "Update failed")
        alert.informativeText = tr("下载或替换安装包时出错，是否打开下载页手动下载？", "Something went wrong while downloading or installing the update. Open the download page to download it manually?")
        alert.addButton(withTitle: tr("打开下载页", "Open download page"))
        alert.addButton(withTitle: tr("取消", "Cancel"))
        if alert.runModal() == .alertFirstButtonReturn {
            let fallback = URL(string: Self.updatePageURLString)!
            NSWorkspace.shared.open(availableUpdateURL ?? fallback)
        }
    }

    private func showInfoAlert(title: String, message: String) {
        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        alert.alertStyle = .informational
        alert.messageText = title
        alert.informativeText = message
        alert.addButton(withTitle: tr("好", "OK"))
        alert.runModal()
    }

    // MARK: - Crisp Vector Menu Bar Cat Silhouette Icon

    private func makeMenuBarCatIcon() -> NSImage {
        let size = NSSize(width: 18, height: 18)
        let img = NSImage(size: size, flipped: true) { _ in
            guard let ctx = NSGraphicsContext.current?.cgContext else { return false }
            ctx.setFillColor(NSColor.black.cgColor)

            // Cute cat head silhouette with ears
            let path = CGMutablePath()
            path.move(to: CGPoint(x: 3.0, y: 8.5))
            path.addLine(to: CGPoint(x: 3.5, y: 2.5))
            path.addLine(to: CGPoint(x: 7.2, y: 6.0))
            path.addQuadCurve(to: CGPoint(x: 10.8, y: 6.0), control: CGPoint(x: 9.0, y: 5.5))
            path.addLine(to: CGPoint(x: 14.5, y: 2.5))
            path.addLine(to: CGPoint(x: 15.0, y: 8.5))
            path.addCurve(to: CGPoint(x: 9.0, y: 15.5), control1: CGPoint(x: 16.0, y: 12.8), control2: CGPoint(x: 13.0, y: 15.5))
            path.addCurve(to: CGPoint(x: 3.0, y: 8.5), control1: CGPoint(x: 5.0, y: 15.5), control2: CGPoint(x: 2.0, y: 12.8))
            path.closeSubpath()

            // Punch out cute cat eyes
            path.addEllipse(in: CGRect(x: 5.4, y: 8.8, width: 2.4, height: 2.2))
            path.addEllipse(in: CGRect(x: 10.2, y: 8.8, width: 2.4, height: 2.2))

            ctx.addPath(path)
            ctx.fillPath(using: .evenOdd)
            return true
        }
        img.isTemplate = true
        return img
    }
}
