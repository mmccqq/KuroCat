import Foundation
import AppKit

// Whiskerkey 1.0 replaces KuroCat (this project's earlier name). It has its own app ID, so macOS keeps
// its settings, keyboard permission and login item apart from KuroCat's. On the first launch Whiskerkey:
//   1. copies KuroCat's settings (cats, Cat Link pairing code, nickname, positions, options),
//   2. quits a KuroCat that is still running (two cats would both react to the keyboard),
//   3. moves "start at login" over,
//   4. offers to move the old KuroCat app to the Trash and to point Claude Code's hooks at Whiskerkey.
//
// Whisku 1.2.0 also gets its own app ID (app.whisku; it was com.whiskerkey.app while it was called
// Whiskerkey). On its first launch it copies the Whiskerkey settings ("Whiskerkey.X" -> "Whisku.X"),
// removes the old ones, and moves "start at login" over. macOS asks once more for Input Monitoring.
enum KuroCatMigration {
    static let bundleID = "com.chanke.kurocat"
    /// Whisku's app ID up to 1.1.0.
    static let whiskerkeyBundleID = "com.whiskerkey.app"
    private static let importedWhiskerkeyKey = "Whisku.ImportedWhiskerkeySettings"
    private static let importedKey = "Whisku.ImportedKuroCatSettings"
    private static let offeredKey = "Whisku.OfferedKuroCatCleanup"

    /// Copies "<prefix>X" settings from an earlier app's `domain` to "Whisku.X"; nothing already set is
    /// overwritten. Returns how many were copied.
    static func copySettings(from domain: [String: Any]?, prefix: String, into defaults: UserDefaults) -> Int {
        var copied = 0
        for (key, value) in domain ?? [:] where key.hasPrefix(prefix) {
            let newKey = "Whisku." + key.dropFirst(prefix.count)
            guard defaults.object(forKey: newKey) == nil else { continue }
            defaults.set(value, forKey: newKey)
            copied += 1
        }
        return copied
    }

    /// Copies KuroCat's settings once ("KuroCat.X" -> "Whisku.X"). Runs before anything reads the
    /// settings, after importWhiskerkeySettings. Returns how many were copied.
    @discardableResult
    static func importSettings(from domain: [String: Any]? = UserDefaults.standard.persistentDomain(forName: bundleID),
                               into defaults: UserDefaults = .standard) -> Int {
        guard !defaults.bool(forKey: importedKey) else { return 0 }
        defaults.set(true, forKey: importedKey)
        return copySettings(from: domain, prefix: "KuroCat.", into: defaults)
    }

    /// Copies Whiskerkey's settings (Whisku up to 1.1.0, app ID com.whiskerkey.app) once:
    /// "Whiskerkey.X" -> "Whisku.X". Their copy of "ImportedKuroCatSettings" comes along, so KuroCat
    /// isn't imported a second time. Returns how many were copied.
    @discardableResult
    static func importWhiskerkeySettings(from domain: [String: Any]? = UserDefaults.standard.persistentDomain(forName: whiskerkeyBundleID),
                                         into defaults: UserDefaults = .standard) -> Int {
        guard !defaults.bool(forKey: importedWhiskerkeyKey) else { return 0 }
        defaults.set(true, forKey: importedWhiskerkeyKey)
        return copySettings(from: domain, prefix: "Whiskerkey.", into: defaults)
    }

    /// Removes the old app ID's settings once they're copied (like `defaults delete com.whiskerkey.app`).
    static func removeWhiskerkeySettings() {
        guard UserDefaults.standard.bool(forKey: importedWhiskerkeyKey),
              UserDefaults.standard.persistentDomain(forName: whiskerkeyBundleID) != nil else { return }
        UserDefaults.standard.removePersistentDomain(forName: whiskerkeyBundleID)
    }

    /// Quits a running KuroCat or Whiskerkey (an earlier app ID): two cats would both react to the keyboard.
    static func quitRunningKuroCat() {
        for id in [bundleID, whiskerkeyBundleID] {
            for app in NSRunningApplication.runningApplications(withBundleIdentifier: id) {
                if !app.terminate() { app.forceTerminate() }
                DispatchQueue.main.asyncAfter(deadline: .now() + 3) { if !app.isTerminated { app.forceTerminate() } }
            }
        }
    }

    /// Removes the login items of earlier app IDs (~/Library/LaunchAgents/com.chanke.kurocat.plist and
    /// com.whiskerkey.app.plist). Returns true if one was there, so Whisku turns its own on instead.
    static func removeKuroCatLoginItem(home: URL = FileManager.default.homeDirectoryForCurrentUser) -> Bool {
        var removed = false
        for id in [bundleID, whiskerkeyBundleID] {
            let url = home.appendingPathComponent("Library/LaunchAgents/\(id).plist")
            guard FileManager.default.fileExists(atPath: url.path) else { continue }
            removed = (try? FileManager.default.removeItem(at: url)) != nil || removed
        }
        return removed
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

    /// Matches a hook program `…/<app>.app/Contents/MacOS/<app>`, shell-quoted ('…') or not.
    private static func hookProgramRegex(_ app: String) -> NSRegularExpression? {
        let tail = "/\(app)\\.app/Contents/MacOS/\(app)"
        return try? NSRegularExpression(pattern: "'[^'\"]*\(tail)'|[^\\s\"']*\(tail)")
    }

    /// `settings` (Claude Code's settings.json text) with every hook program of the app called `app`
    /// (KuroCat, or Whiskerkey before it became Whisku) replaced by `exe`. Only the program path
    /// changes; ` --claude-hook` and everything else stay as they are.
    static func pointClaudeHooks(_ settings: String, at exe: String, from app: String = "KuroCat") -> String {
        // The hooks hold '…/App.app/Contents/MacOS/App' (shell-quoted) inside a JSON string.
        let quoted = "'" + exe.replacingOccurrences(of: "'", with: "'\\''") + "'"
        let json = quoted.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"")
        guard let regex = hookProgramRegex(app) else { return settings }
        let range = NSRange(settings.startIndex..., in: settings)
        return regex.stringByReplacingMatches(in: settings, range: range, withTemplate: NSRegularExpression.escapedTemplate(for: json))
    }

    /// The programs that hooks of the app called `app` start, without shell quotes.
    static func claudeHookPrograms(_ settings: String, from app: String) -> [String] {
        guard let regex = hookProgramRegex(app) else { return [] }
        return regex.matches(in: settings, range: NSRange(settings.startIndex..., in: settings)).compactMap {
            Range($0.range, in: settings).map { String(settings[$0]).trimmingCharacters(in: CharacterSet(charactersIn: "'")) }
        }
    }

    /// Whiskerkey became Whisku in 1.1.0, so Claude Code's hooks may still start
    /// …/Whiskerkey.app/Contents/MacOS/Whiskerkey. Once no such app is left, they're pointed at this app
    /// (the old file is kept as settings.json.before-whisku). Returns true if the hooks were changed.
    @discardableResult
    static func repointRenamedClaudeHooks(settingsURL: URL = claudeSettingsURL,
                                          exe: String? = Bundle.main.executableURL?.path) -> Bool {
        guard let exe, !exe.hasSuffix("/Whiskerkey"),
              let settings = try? String(contentsOf: settingsURL, encoding: .utf8) else { return false }
        let old = claudeHookPrograms(settings, from: "Whiskerkey")
        guard !old.isEmpty, !old.contains(where: { FileManager.default.fileExists(atPath: $0) }) else { return false }
        let updated = pointClaudeHooks(settings, at: exe, from: "Whiskerkey")
        let backup = settingsURL.deletingLastPathComponent().appendingPathComponent("settings.json.before-whisku")
        let fm = FileManager.default
        do {
            if fm.fileExists(atPath: backup.path) { try fm.removeItem(at: backup) }
            try fm.copyItem(at: settingsURL, to: backup)
            try updated.write(to: settingsURL, atomically: true, encoding: .utf8)
            return true
        } catch { return false }
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
        alert.messageText = tr("KuroCat 现在叫 Whisku 了", "KuroCat is now Whisku")
        var parts = [tr("你的小猫、猫咪连线和设置都已经带过来了。", "Your cats, Cat Link and settings came along.")]
        if !apps.isEmpty {
            parts.append(tr("把旧的 KuroCat 移到废纸篓：", "Move the old KuroCat to the Trash:") + "\n" + apps.map(\.path).joined(separator: "\n"))
        }
        if hooksStartKuroCat {
            parts.append(tr("Claude Code 的提醒钩子改为启动 Whisku（原文件保留为 ~/.claude/settings.json.before-whiskerkey）。",
                            "Point Claude Code's hooks at Whisku (the old file is kept as ~/.claude/settings.json.before-whiskerkey)."))
        }
        parts.append(tr("macOS 会请你再允许一次 Whisku 的「输入监控」，小猫才能看到键盘。",
                        "macOS will ask you once to allow Input Monitoring for Whisku, so the cat can see the keyboard."))
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
