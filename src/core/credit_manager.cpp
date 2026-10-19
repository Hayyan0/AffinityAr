#include "credit_manager.h"

extern "C" {
    __declspec(dllexport) const char* AffinityArabic_Author = "Hayyan (@HayyanCodes)";
    __declspec(dllexport) const char* AffinityArabic_Twitter = "https://x.com/HayyanCodes";
    __declspec(dllexport) const char* AffinityArabic_Copyright = "Affinity Arabic Support Copyright (C) 2026 Hayyan (@HayyanCodes). All rights reserved.";

    __declspec(dllexport) const char* GetAffinityArabicAuthor() {
        return AffinityArabic_Author;
    }
    __declspec(dllexport) const char* GetAffinityArabicTwitter() {
        return AffinityArabic_Twitter;
    }
    __declspec(dllexport) const char* GetAffinityArabicCopyright() {
        return AffinityArabic_Copyright;
    }
}

namespace AffinityArabic {

CreditManager& CreditManager::Instance() {
    static CreditManager s_Instance;
    return s_Instance;
}

}
