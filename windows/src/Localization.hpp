#pragma once
// UI language (same as macOS Sources/Localization.swift): follow the Windows display language
// (Chinese on a Chinese system, English otherwise), or a fixed choice from the 「语言 / Language」
// menu, saved as HKCU\Software\KuroCat\Language (0 = system, 1 = 中文, 2 = English).
// Every visible text is written as tr(L"中文", L"English"), both languages side by side.

#include <windows.h>
#include <string>

namespace l10n {

enum class Language : DWORD { System = 0, Chinese = 1, English = 2 };

inline Language readSetting() {
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\KuroCat", L"Language", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
        return Language::System;
    }
    return value <= 2 ? static_cast<Language>(value) : Language::System;
}

inline bool resolve(Language language) {
    if (language == Language::Chinese) return true;
    if (language == Language::English) return false;
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
}

inline Language& current() {
    static Language language = readSetting();
    return language;
}

/// Cached: text is looked up every frame (bubbles, badges).
inline int& cachedChinese() {
    static int value = -1;
    return value;
}

inline bool isChinese() {
    int& cached = cachedChinese();
    if (cached < 0) cached = resolve(current()) ? 1 : 0;
    return cached == 1;
}

inline void set(Language language) {
    current() = language;
    cachedChinese() = resolve(language) ? 1 : 0;
    DWORD value = static_cast<DWORD>(language);
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\KuroCat", L"Language", REG_DWORD, &value, sizeof(value));
}

}  // namespace l10n

/// The text in the current UI language.
inline const wchar_t* tr(const wchar_t* chinese, const wchar_t* english) {
    return l10n::isChinese() ? chinese : english;
}
inline std::wstring tr(const std::wstring& chinese, const std::wstring& english) {
    return l10n::isChinese() ? chinese : english;
}
