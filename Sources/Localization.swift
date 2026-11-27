import Foundation

/// UI language: follow the system (Chinese on a Chinese system, English otherwise), or a fixed choice
/// from the 「语言 / Language」 menu. Every visible text is written as `tr("中文", "English")`, so both
/// languages sit side by side in the code (the Windows version does the same in Localization.hpp).
enum AppLanguage: String, CaseIterable {
    case system, chinese, english
}

enum L10n {
    private static let key = "Whiskerkey.Language"

    static var setting: AppLanguage {
        get { AppLanguage(rawValue: UserDefaults.standard.string(forKey: key) ?? "") ?? .system }
        set {
            UserDefaults.standard.set(newValue.rawValue, forKey: key)
            cachedIsChinese = resolve(newValue)
        }
    }

    /// True when the UI should be Chinese (cached: text is looked up every frame).
    static var isChinese: Bool {
        if let cached = cachedIsChinese { return cached }
        let value = resolve(setting)
        cachedIsChinese = value
        return value
    }

    private static var cachedIsChinese: Bool?

    private static func resolve(_ language: AppLanguage) -> Bool {
        switch language {
        case .chinese: return true
        case .english: return false
        case .system: return (Locale.preferredLanguages.first ?? "").lowercased().hasPrefix("zh")
        }
    }
}

/// The text in the current UI language.
@inline(__always) func tr(_ chinese: String, _ english: String) -> String {
    L10n.isChinese ? chinese : english
}
