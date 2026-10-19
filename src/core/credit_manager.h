#pragma once
#include <windows.h>
#include <string>

namespace AffinityArabic {

inline constexpr const wchar_t* AUTHOR_NAME = L"Hayyan";
inline constexpr const wchar_t* AUTHOR_HANDLE = L"@HayyanCodes";
inline constexpr const wchar_t* TWITTER_URL = L"https://x.com/HayyanCodes";
inline constexpr const char* COPYRIGHT_STRING = "Affinity Arabic Support Copyright (C) 2026 Hayyan (@HayyanCodes). All rights reserved.";

}

extern "C" {
    __declspec(dllexport) const char* GetAffinityArabicAuthor();
    __declspec(dllexport) const char* GetAffinityArabicTwitter();
    __declspec(dllexport) const char* GetAffinityArabicCopyright();
}

namespace AffinityArabic {

class CreditManager {
public:
    static CreditManager& Instance();

    const wchar_t* GetAuthorName() const { return AUTHOR_NAME; }
    const wchar_t* GetAuthorHandle() const { return AUTHOR_HANDLE; }
    const wchar_t* GetTwitterUrl() const { return TWITTER_URL; }
    const char* GetCopyright() const { return COPYRIGHT_STRING; }
};

}
