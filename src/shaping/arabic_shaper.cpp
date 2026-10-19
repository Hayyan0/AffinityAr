#include "arabic_shaper.h"
#include <unordered_map>

namespace AffinityArabic {

static const std::unordered_map<wchar_t, ArabicGlyphForms> s_ArabicTable = {
    { 0x0621, { 0xFE80, 0xFE80, 0xFE80, 0xFE80 } }, // ء
    { 0x0622, { 0xFE81, 0xFE81, 0xFE82, 0xFE82 } }, // آ
    { 0x0623, { 0xFE83, 0xFE83, 0xFE84, 0xFE84 } }, // أ
    { 0x0624, { 0xFE85, 0xFE85, 0xFE86, 0xFE86 } }, // ؤ
    { 0x0625, { 0xFE87, 0xFE87, 0xFE88, 0xFE88 } }, // إ
    { 0x0626, { 0xFE89, 0xFE8B, 0xFE8C, 0xFE8A } }, // ئ
    { 0x0627, { 0xFE8D, 0xFE8D, 0xFE8E, 0xFE8E } }, // ا
    { 0x0628, { 0xFE8F, 0xFE91, 0xFE92, 0xFE90 } }, // ب
    { 0x0629, { 0xFE93, 0xFE93, 0xFE94, 0xFE94 } }, // ة
    { 0x062A, { 0xFE95, 0xFE97, 0xFE98, 0xFE96 } }, // ت
    { 0x062B, { 0xFE99, 0xFE9B, 0xFE9C, 0xFE9A } }, // ث
    { 0x062C, { 0xFE9D, 0xFE9F, 0xFEA0, 0xFE9E } }, // ج
    { 0x062D, { 0xFEA1, 0xFEA3, 0xFEA4, 0xFEA2 } }, // ح
    { 0x062E, { 0xFEA5, 0xFEA7, 0xFEA8, 0xFEA6 } }, // خ
    { 0x062F, { 0xFEA9, 0xFEA9, 0xFEAA, 0xFEAA } }, // د
    { 0x0630, { 0xFEAB, 0xFEAB, 0xFEAC, 0xFEAC } }, // ذ
    { 0x0631, { 0xFEAD, 0xFEAD, 0xFEAE, 0xFEAE } }, // ر
    { 0x0632, { 0xFEAF, 0xFEAF, 0xFEB0, 0xFEB0 } }, // ز
    { 0x0633, { 0xFEB1, 0xFEB3, 0xFEB4, 0xFEB2 } }, // س
    { 0x0634, { 0xFEB5, 0xFEB7, 0xFEB8, 0xFEB6 } }, // ش
    { 0x0635, { 0xFEB9, 0xFEBB, 0xFEBC, 0xFEBA } }, // ص
    { 0x0636, { 0xFEBD, 0xFEBF, 0xFEC0, 0xFEBE } }, // ض
    { 0x0637, { 0xFEC1, 0xFEC3, 0xFEC4, 0xFEC2 } }, // ط
    { 0x0638, { 0xFEC5, 0xFEC7, 0xFEC8, 0xFEC6 } }, // ظ
    { 0x0639, { 0xFEC9, 0xFECB, 0xFECC, 0xFECA } }, // ع
    { 0x063A, { 0xFECD, 0xFECF, 0xFED0, 0xFECE } }, // غ
    { 0x0641, { 0xFED1, 0xFED3, 0xFED4, 0xFED2 } }, // ف
    { 0x0642, { 0xFED5, 0xFED7, 0xFED8, 0xFED6 } }, // ق
    { 0x0643, { 0xFED9, 0xFEDB, 0xFEDC, 0xFEDA } }, // ك
    { 0x0644, { 0xFEDD, 0xFEDF, 0xFEE0, 0xFEDE } }, // ل
    { 0x0645, { 0xFEE1, 0xFEE3, 0xFEE4, 0xFEE2 } }, // م
    { 0x0646, { 0xFEE5, 0xFEE7, 0xFEE8, 0xFEE6 } }, // ن
    { 0x0647, { 0xFEE9, 0xFEEB, 0xFEEC, 0xFEEA } }, // ه
    { 0x0648, { 0xFEED, 0xFEED, 0xFEEE, 0xFEEE } }, // و
    { 0x0649, { 0xFEEF, 0xFEEF, 0xFEF0, 0xFEF0 } }, // ى
    { 0x064A, { 0xFEF1, 0xFEF3, 0xFEF4, 0xFEF2 } }, // ي

    // Extended Arabic letters (Persian / Urdu / Kurdish)
    { 0x067E, { 0xFB56, 0xFB58, 0xFB59, 0xFB57 } }, // پ
    { 0x0686, { 0xFB7A, 0xFB7C, 0xFB7D, 0xFB7B } }, // چ
    { 0x0698, { 0xFB8A, 0xFB8A, 0xFB8B, 0xFB8B } }, // ژ
    { 0x06AF, { 0xFB92, 0xFB94, 0xFB95, 0xFB93 } }, // گ
    { 0x06A9, { 0xFB8E, 0xFB90, 0xFB91, 0xFB8F } }, // ک
    { 0x06D2, { 0xFBAE, 0xFBAE, 0xFBAF, 0xFBAF } }, // ے
    { 0x06CC, { 0xFBFC, 0xFBFE, 0xFBFF, 0xFBFD } }  // ی
};

bool ArabicShaper::IsArabicChar(wchar_t c) {
    return (c >= 0x0600 && c <= 0x06FF) ||
           (c >= 0x0750 && c <= 0x077F) ||
           (c >= 0x08A0 && c <= 0x08FF) ||
           (c >= 0xFB50 && c <= 0xFDFF) ||
           (c >= 0xFE70 && c <= 0xFEFF) ||
           (c == 0x200C || c == 0x200D);
}

bool ArabicShaper::NeedsShaping(wchar_t c) {
    return (c >= 0x0600 && c <= 0x06FF) ||
           (c >= 0x0750 && c <= 0x077F) ||
           (c >= 0x08A0 && c <= 0x08FF);
}

bool ArabicShaper::NeedsShaping(const std::wstring& text) {
    for (wchar_t c : text) {
        if (NeedsShaping(c)) return true;
    }
    return false;
}

bool ArabicShaper::IsTashkeel(wchar_t c) {
    return (c >= 0x064B && c <= 0x065F) || (c == 0x0670);
}

bool ArabicShaper::ContainsArabic(const std::wstring& text) {
    for (wchar_t c : text) {
        if (IsArabicChar(c)) return true;
    }
    return false;
}

bool ArabicShaper::IsPresentationForm(wchar_t c) {
    return (c >= 0xFB50 && c <= 0xFDFF) ||
           (c >= 0xFE70 && c <= 0xFEFF);
}

std::wstring ArabicShaper::NormalizeToNominal(const std::wstring& text) {
    static std::unordered_map<wchar_t, std::wstring> s_PresToNom;
    static bool s_Initialized = false;
    if (!s_Initialized) {
        for (const auto& pair : s_ArabicTable) {
            wchar_t nominal = pair.first;
            const ArabicGlyphForms& f = pair.second;
            s_PresToNom[f.isolated] = std::wstring(1, nominal);
            s_PresToNom[f.initial] = std::wstring(1, nominal);
            s_PresToNom[f.medial] = std::wstring(1, nominal);
            s_PresToNom[f.final_form] = std::wstring(1, nominal);
        }
        s_PresToNom[0xFEFB] = L"\u0644\u0627";
        s_PresToNom[0xFEFC] = L"\u0644\u0627";
        s_PresToNom[0xFEF5] = L"\u0644\u0622";
        s_PresToNom[0xFEF6] = L"\u0644\u0622";
        s_PresToNom[0xFEF7] = L"\u0644\u0623";
        s_PresToNom[0xFEF8] = L"\u0644\u0623";
        s_PresToNom[0xFEF9] = L"\u0644\u0625";
        s_PresToNom[0xFEFA] = L"\u0644\u0625";
        s_Initialized = true;
    }

    std::wstring result;
    result.reserve(text.size());
    for (wchar_t c : text) {
        auto it = s_PresToNom.find(c);
        if (it != s_PresToNom.end()) {
            result += it->second;
        } else {
            result.push_back(c);
        }
    }
    return result;
}

bool ArabicShaper::IsLamAlefPresentation(wchar_t c) {
    return (c >= 0xFEF5 && c <= 0xFEFC);
}

wchar_t ArabicShaper::ToNominalChar(wchar_t c) {
    static std::unordered_map<wchar_t, wchar_t> s_PresToSingleNom;
    static bool s_Initialized = false;
    if (!s_Initialized) {
        for (const auto& pair : s_ArabicTable) {
            wchar_t nominal = pair.first;
            const ArabicGlyphForms& f = pair.second;
            s_PresToSingleNom[f.isolated] = nominal;
            s_PresToSingleNom[f.initial] = nominal;
            s_PresToSingleNom[f.medial] = nominal;
            s_PresToSingleNom[f.final_form] = nominal;
        }
        s_Initialized = true;
    }
    auto it = s_PresToSingleNom.find(c);
    if (it != s_PresToSingleNom.end()) {
        return it->second;
    }
    return c;
}

JoiningType ArabicShaper::GetJoiningType(wchar_t c) {
    if (IsTashkeel(c)) return JoiningType::Transparent;
    if (c == 0x0640 || c == 0x200D) return JoiningType::JoinCausing; 
    if (c == 0x200C) return JoiningType::NonJoining; 
    if (IsLamAlefPresentation(c)) return JoiningType::RightJoining;

    wchar_t nom = ToNominalChar(c);

    switch (nom) {
    case 0x0622: // آ
    case 0x0623: // أ
    case 0x0624: // ؤ
    case 0x0625: // إ
    case 0x0627: // ا
    case 0x062F: // د
    case 0x0630: // ذ
    case 0x0631: // ر
    case 0x0632: // ز
    case 0x0648: // و
    case 0x0649: // ى
    case 0x0629: // ة
    case 0x0698: // ژ
    case 0x06D2: // ے
        return JoiningType::RightJoining;
    default:
        break;
    }

    if (s_ArabicTable.find(nom) != s_ArabicTable.end()) {
        if (nom == 0x0621) return JoiningType::NonJoining;
        return JoiningType::DualJoining;
    }

    return JoiningType::NonJoining;
}

const ArabicGlyphForms* ArabicShaper::GetGlyphForms(wchar_t c) {
    auto it = s_ArabicTable.find(c);
    if (it != s_ArabicTable.end()) {
        return &it->second;
    }
    return nullptr;
}

bool ArabicShaper::IsAlef(wchar_t c) {
    return (c == 0x0622 || c == 0x0623 || c == 0x0625 || c == 0x0627);
}

wchar_t ArabicShaper::GetLamAlef(wchar_t alef, bool isFinal) {
    switch (alef) {
    case 0x0622: return isFinal ? 0xFEF6 : 0xFEF5; 
    case 0x0623: return isFinal ? 0xFEF8 : 0xFEF7; 
    case 0x0625: return isFinal ? 0xFEFA : 0xFEF9; 
    case 0x0627: return isFinal ? 0xFEFC : 0xFEFB; 
    default: return 0;
    }
}

std::wstring ArabicShaper::Reshape(const std::wstring& input) {
    if (input.empty()) return input;

    std::wstring result;
    result.reserve(input.size());

    size_t len = input.size();
    for (size_t i = 0; i < len; ++i) {
        wchar_t current = input[i];

        const ArabicGlyphForms* forms = GetGlyphForms(current);
        if (!forms) {
            result.push_back(current);
            continue;
        }

        if (current == 0x0644 && (i + 1 < len)) {
            size_t nextIdx = i + 1;
            std::vector<wchar_t> interveningTashkeel;
            while (nextIdx < len && IsTashkeel(input[nextIdx])) {
                interveningTashkeel.push_back(input[nextIdx]);
                nextIdx++;
            }

            if (nextIdx < len && IsAlef(input[nextIdx])) {
                wchar_t alef = input[nextIdx];

                bool prevConnects = false;
                if (i > 0) {
                    size_t prevIdx = i - 1;
                    while (prevIdx > 0 && IsTashkeel(input[prevIdx])) {
                        prevIdx--;
                    }
                    if (!IsTashkeel(input[prevIdx])) {
                        JoiningType prevType = GetJoiningType(input[prevIdx]);
                        prevConnects = (prevType == JoiningType::DualJoining || prevType == JoiningType::JoinCausing);
                    }
                }

                wchar_t lamAlef = GetLamAlef(alef, prevConnects);
                if (lamAlef != 0) {
                    result.push_back(lamAlef);
                    for (wchar_t t : interveningTashkeel) {
                        result.push_back(t);
                    }
                    i = nextIdx; 
                    continue;
                }
            }
        }

        bool prevConnects = false;
        if (i > 0) {
            size_t prevIdx = i - 1;
            while (prevIdx > 0 && IsTashkeel(input[prevIdx])) {
                prevIdx--;
            }
            if (!IsTashkeel(input[prevIdx])) {
                JoiningType prevType = GetJoiningType(input[prevIdx]);
                prevConnects = (prevType == JoiningType::DualJoining || prevType == JoiningType::JoinCausing);
            }
        }

        bool nextConnects = false;
        if (i + 1 < len) {
            size_t nextIdx = i + 1;
            while (nextIdx + 1 < len && IsTashkeel(input[nextIdx])) {
                nextIdx++;
            }
            if (!IsTashkeel(input[nextIdx])) {
                JoiningType nextType = GetJoiningType(input[nextIdx]);
                nextConnects = (nextType == JoiningType::DualJoining || 
                                nextType == JoiningType::RightJoining || 
                                nextType == JoiningType::JoinCausing);
            }
        }

        JoiningType curType = GetJoiningType(current);

        wchar_t shapedChar = current;
        if (curType == JoiningType::DualJoining) {
            if (prevConnects && nextConnects) {
                shapedChar = forms->medial;
            } else if (prevConnects) {
                shapedChar = forms->final_form;
            } else if (nextConnects) {
                shapedChar = forms->initial;
            } else {
                shapedChar = forms->isolated;
            }
        } else if (curType == JoiningType::RightJoining) {
            if (prevConnects) {
                shapedChar = forms->final_form;
            } else {
                shapedChar = forms->isolated;
            }
        } else {
            shapedChar = forms->isolated;
        }

        result.push_back(shapedChar);
    }

    return result;
}

std::vector<ArabicShaper::ShapedGlyph> ArabicShaper::ShapeForLine(const std::vector<uint32_t>& nominalCodes) {
    std::vector<ShapedGlyph> result;
    if (nominalCodes.empty()) return result;

    size_t len = nominalCodes.size();
    result.reserve(len);

    for (size_t i = 0; i < len; ++i) {
        uint32_t rawCurrent = nominalCodes[i];

        if (rawCurrent == 0 || !IsArabicChar((wchar_t)rawCurrent)) {
            result.push_back({ rawCurrent, false });
            continue;
        }

        if (rawCurrent == 0x200D || rawCurrent == 0x200C) {
            result.push_back({ rawCurrent, true });
            continue;
        }

        if (IsLamAlefPresentation((wchar_t)rawCurrent)) {
            bool prevConnects = false;
            if (i > 0) {
                size_t prevIdx = i - 1;
                while (prevIdx > 0 && IsTashkeel((wchar_t)nominalCodes[prevIdx])) {
                    prevIdx--;
                }
                if (!IsTashkeel((wchar_t)nominalCodes[prevIdx])) {
                    JoiningType prevType = GetJoiningType((wchar_t)nominalCodes[prevIdx]);
                    prevConnects = (prevType == JoiningType::DualJoining || prevType == JoiningType::JoinCausing);
                }
            }
            wchar_t shapedLA = (wchar_t)rawCurrent;
            if (rawCurrent == 0xFEFB || rawCurrent == 0xFEFC) {
                shapedLA = prevConnects ? 0xFEFC : 0xFEFB;
            } else if (rawCurrent == 0xFEF5 || rawCurrent == 0xFEF6) {
                shapedLA = prevConnects ? 0xFEF6 : 0xFEF5;
            } else if (rawCurrent == 0xFEF7 || rawCurrent == 0xFEF8) {
                shapedLA = prevConnects ? 0xFEF8 : 0xFEF7;
            } else if (rawCurrent == 0xFEF9 || rawCurrent == 0xFEFA) {
                shapedLA = prevConnects ? 0xFEFA : 0xFEF9;
            }
            result.push_back({ (uint32_t)shapedLA, false });
            continue;
        }

        uint32_t current = ToNominalChar((wchar_t)rawCurrent);

        const ArabicGlyphForms* forms = GetGlyphForms((wchar_t)current);
        if (!forms) {
            result.push_back({ rawCurrent, false });
            continue;
        }

        if (current == 0x0644 && (i + 1 < len)) {
            size_t nextIdx = i + 1;
            while (nextIdx < len && IsTashkeel((wchar_t)nominalCodes[nextIdx])) {
                nextIdx++;
            }

            if (nextIdx < len) {
                wchar_t nextNom = ToNominalChar((wchar_t)nominalCodes[nextIdx]);
                if (IsAlef(nextNom)) {
                    wchar_t alef = nextNom;

                    bool prevConnects = false;
                    if (i > 0) {
                        size_t prevIdx = i - 1;
                        while (prevIdx > 0 && IsTashkeel((wchar_t)nominalCodes[prevIdx])) {
                            prevIdx--;
                        }
                        if (!IsTashkeel((wchar_t)nominalCodes[prevIdx])) {
                            JoiningType prevType = GetJoiningType((wchar_t)nominalCodes[prevIdx]);
                            prevConnects = (prevType == JoiningType::DualJoining || prevType == JoiningType::JoinCausing);
                        }
                    }

                    wchar_t lamAlef = GetLamAlef(alef, prevConnects);
                    if (lamAlef != 0) {
                        result.push_back({ (uint32_t)lamAlef, false });
                        for (size_t t = i + 1; t < nextIdx; ++t) {
                            result.push_back({ nominalCodes[t], false });
                        }
                        result.push_back({ 0x200D, true });
                        i = nextIdx;
                        continue;
                    }
                }
            }
        }

        bool prevConnects = false;
        if (i > 0) {
            size_t prevIdx = i - 1;
            while (prevIdx > 0 && IsTashkeel((wchar_t)nominalCodes[prevIdx])) {
                prevIdx--;
            }
            if (!IsTashkeel((wchar_t)nominalCodes[prevIdx])) {
                JoiningType prevType = GetJoiningType((wchar_t)nominalCodes[prevIdx]);
                prevConnects = (prevType == JoiningType::DualJoining || prevType == JoiningType::JoinCausing);
            }
        }

        bool nextConnects = false;
        if (i + 1 < len) {
            size_t nextIdx = i + 1;
            while (nextIdx + 1 < len && IsTashkeel((wchar_t)nominalCodes[nextIdx])) {
                nextIdx++;
            }
            if (!IsTashkeel((wchar_t)nominalCodes[nextIdx])) {
                JoiningType nextType = GetJoiningType((wchar_t)nominalCodes[nextIdx]);
                nextConnects = (nextType == JoiningType::DualJoining || 
                                nextType == JoiningType::RightJoining || 
                                nextType == JoiningType::JoinCausing);
            }
        }

        JoiningType curType = GetJoiningType((wchar_t)current);

        wchar_t shapedChar = (wchar_t)current;
        if (curType == JoiningType::DualJoining) {
            if (prevConnects && nextConnects) {
                shapedChar = forms->medial;
            } else if (prevConnects) {
                shapedChar = forms->final_form;
            } else if (nextConnects) {
                shapedChar = forms->initial;
            } else {
                shapedChar = forms->isolated;
            }
        } else if (curType == JoiningType::RightJoining) {
            if (prevConnects) {
                shapedChar = forms->final_form;
            } else {
                shapedChar = forms->isolated;
            }
        } else {
            shapedChar = forms->isolated;
        }

        result.push_back({ (uint32_t)shapedChar, false });
    }

    return result;
}

}
