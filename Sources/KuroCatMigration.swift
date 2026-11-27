import Foundation
import AppKit

// Whiskerkey 1.0 replaces KuroCat (this project's earlier name). It has its own app ID, so macOS keeps
// its settings, keyboard permission and login item apart from KuroCat's. On the first launch Whiskerkey:
//   1. copies KuroCat's settings (cats, Cat Link pairing code, nickname, positions, options),
//   2. quits a KuroCat that is still running (two cats would both react to the keyboard),
//   3. moves "start at login" over,
//   4. offers to move the old KuroCat app to the Trash and to point Claude Code's hooks at Whiskerkey.
enum KuroCatMigration {
    static let bundleID = "com.chanke.kurocat"
    private static let importedKey = "Whiskerkey.ImportedKuroCatSettings"
    private static let offeredKey = "Whiskerkey.OfferedKuroCatCleanup"

    /// Copies KuroCat's settings once: "KuroCat.X" becomes "Whiskerkey.X", and nothing already set is
    /// overwritten. Runs before anything reads the settings. Returns how many were copied.
    @discardableResult
    static func importSettings(from domain: [String: Any]? = UserDefaults.standard.persistentDomain(forName: bundleID),
                               into defaults: UserDefaults = .standard) -> Int {
        guard !defaults.bool(forKey: importedKey) else { return 0 }
        defaults.set(true, forKey: importedKey)
        var copied = 0
        for (key, value) in domain ?? [:] where key.hasPrefix("KuroCat.") {
            let newKey = "Whiskerkey." + key.dropFirst("KuroCat.".count)
            guard defaults.object(forKey: newKey) == nil else { continue }
            defaults.set(value, forKey: newKey)
            copied += 1
        }
        return copied
    }

    static func quitRunningKuroCat() {
        for app in NSRunningApplication.runningApplications(withBundleIdentifier: bundleID) {
            if !app.terminate() { app.forceTerminate() }
            DispatchQueue.main.asyncAfter(deadline: .now() + 3) { if !app.isTerminated { app.forceTerminate() } }
        }
    }

    /// Removes KuroCat's login item (~/Library/LaunchAgents/com.chanke.kurocat.plist).
    /// Returns true if it was there, so Whiskerkey turns its own on instead.
    static func removeKuroCatLoginItem(home: URL = FileManager.default.homeDirectoryForCurrentUser) -> Bool {
        let url = home.appendingPathComponent("Library/LaunchAgents/\(bundleID).plist")
        guard FileManager.default.fileExists(atPath: url.path) else { return false }
        return (try? FileManager.default.removeItem(at: url)) != nil
    }

    /// Copies of KuroCat.app on this Mac (not counting the Trash).
    static func kuroCatApps() -> [URL] {
        NSWorkspace.shared.urlsForApplications(withBundleIdentifier: bundleID).filter { !$0.path.contains("/.Trash/") }
    }

    static var claudeSettingsURL: URL {
        FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".claude/settings.json")
    }

    /// True if a Claude Code hook in `settings` still starts KuroCat.
    static func claudeHooksStartKuroCat(_ settings: String) -> Bool {
        settings.contains("KuroCat.app/Contents/MacOS/KuroCat")
    }

    /// `settings` (Claude Code's settings.json text) with every KuroCat hook program replaced by `exe`.
    /// Only the program path changes; ` --claude-hook` and everything else stay as they are.
    static func pointClaudeHooks(_ settings: String, at exe: String) -> String {
        // KuroCat wrote '…/KuroCat.app/Contents/MacOS/KuroCat' (shell-quoted) into a JSON string.
        let quoted = "'" + exe.replacingOccurrences(of: "'", with: "'\\''") + "'"
        let json = quoted.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"")
        let pattern = #"'[^'"]*/KuroCat\.app/Contents/MacOS/KuroCat'|[^\s"']*/KuroCat\.app/Contents/MacOS/KuroCat"#
        guard let regex = try? NSRegularExpression(pattern: pattern) else { return settings }
        let range = NSRange(settings.startIndex..., in: settings)
        return regex.stringByReplacingMatches(in: settings, range: range, withTemplate: NSRegularExpression.escapedTemplate(for: json))
    }

    /// Points Claude Code's hooks at this app, keeping the old file as settings.json.before-whiskerkey.
    static func updateClaudeHooks(settings: String, exe: String) throws {
        let url = claudeSettingsURL
        let backup = url.deletingLastPathComponent().appendingPathComponent("settings.json.before-whiskerkey")
        let fm = FileManager.default
        if fm.fileExists(atPath: backup.path) { try fm.removeItem(at: backup) }
        try fm.copyItem(at: url, to: backup)
        try pointClaudeHooks(settings, at: exe).write(to: url, atomically: true, encoding: .utf8)
    }

    /// Once, shortly after launch: if KuroCat is still on this Mac, or Claude Code still starts it, offer to tidy up.
    static func offerCleanup() {
        let defaults = UserDefaults.standard
        guard !defaults.bool(forKey: offeredKey) else { return }
        let apps = kuroCatApps()
        let settings = try? String(contentsOf: claudeSettingsURL, encoding: .utf8)
        let hooksStartKuroCat = settings.map(claudeHooksStartKuroCat) ?? false
        guard !apps.isEmpty || hooksStartKuroCat else { return }
        defaults.set(true, forKey: offeredKey)

        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        alert.alertStyle = .informational
        alert.messageText = tr("KuroCat 现在叫 Whiskerkey 了", "KuroCat is now Whiskerkey")
        var parts = [tr("你的小猫、猫咪连线和设置都已经带过来了。", "Your cats, Cat Link and settings came along.")]
        if !apps.isEmpty {
            parts.append(tr("把旧的 KuroCat 移到废纸篓：", "Move the old KuroCat to the Trash:") + "\n" + apps.map(\.path).joined(separator: "\n"))
        }
        if hooksStartKuroCat {
            parts.append(tr("Claude Code 的提醒钩子改为启动 Whiskerkey（原文件保留为 ~/.claude/settings.json.before-whiskerkey）。",
                            "Point Claude Code's hooks at Whiskerkey (the old file is kept as ~/.claude/settings.json.before-whiskerkey)."))
        }
        parts.append(tr("macOS 会请你再允许一次 Whiskerkey 的「输入监控」，小猫才能看到键盘。",
                        "macOS will ask you once to allow Input Monitoring for Whiskerkey, so the cat can see the keyboard."))
        alert.informativeText = parts.joined(separator: "\n\n")
        alert.addButton(withTitle: tr("好的，整理一下", "Tidy up"))
        alert.addButton(withTitle: tr("先不要", "Not now"))
        guard alert.runModal() == .alertFirstButtonReturn else { return }
        if !apps.isEmpty { NSWorkspace.shared.recycle(apps) { _, _ in } }
        if hooksStartKuroCat, let settings, let exe = Bundle.main.executableURL?.path {
            try? updateClaudeHooks(settings: settings, exe: exe)
        }
    }
}
