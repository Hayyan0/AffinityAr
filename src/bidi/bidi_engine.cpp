#include "bidi_engine.h"
#include "../shaping/arabic_shaper.h"
#include <algorithm>

namespace AffinityArabic {

BidiDirection BidiEngine::GetCharDirection(wchar_t c) {
    if (ArabicShaper::IsArabicChar(c)) return BidiDirection::RTL;

    if ((c >= L'0' && c <= L'9') || (c >= 0x0660 && c <= 0x0669)) {
        return BidiDirection::Number;
    }

    if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
        (c >= 0x00C0 && c <= 0x024F)) {
        return BidiDirection::LTR;
    }

    return BidiDirection::Neutral;
}

wchar_t BidiEngine::GetMirroredChar(wchar_t c) {
    switch (c) {
    case L'(': return L')';
    case L')': return L'(';
    case L'[': return L']';
    case L']': return L'[';
    case L'{': return L'}';
    case L'}': return L'{';
    case L'<': return L'>';
    case L'>': return L'<';
    case 0x00AB: return 0x00BB;
    case 0x00BB: return 0x00AB;
    default: return c;
    }
}

std::vector<BidiRun> BidiEngine::SegmentRuns(const std::wstring& text) {
    std::vector<BidiRun> runs;
    if (text.empty()) return runs;

    BidiDirection currentDir = BidiDirection::Neutral;
    size_t runStart = 0;

    for (size_t i = 0; i < text.size(); ++i) {
        BidiDirection dir = GetCharDirection(text[i]);
        if (dir == BidiDirection::Neutral) {
            continue;
        }

        if (currentDir == BidiDirection::Neutral) {
            currentDir = dir;
        } else if (currentDir != dir) {
            runs.push_back({ runStart, i - runStart, currentDir });
            runStart = i;
            currentDir = dir;
        }
    }

    if (runStart < text.size()) {
        runs.push_back({ runStart, text.size() - runStart, currentDir == BidiDirection::Neutral ? BidiDirection::LTR : currentDir });
    }

    return runs;
}

std::wstring BidiEngine::ReorderVisual(const std::wstring& text) {
    if (text.empty()) return text;

    BidiDirection paraDir = BidiDirection::LTR;
    for (wchar_t c : text) {
        BidiDirection d = GetCharDirection(c);
        if (d == BidiDirection::RTL) {
            paraDir = BidiDirection::RTL;
            break;
        } else if (d == BidiDirection::LTR) {
            paraDir = BidiDirection::LTR;
            break;
        }
    }

    std::wstring mirrored;
    mirrored.reserve(text.size());
    for (wchar_t c : text) {
        mirrored.push_back(GetMirroredChar(c));
    }

    if (paraDir == BidiDirection::RTL) {
        std::wstring rev = mirrored;
        std::reverse(rev.begin(), rev.end());

        size_t n = rev.size();
        size_t i = 0;
        while (i < n) {
            BidiDirection dir = GetCharDirection(rev[i]);
            if (dir == BidiDirection::LTR || dir == BidiDirection::Number) {
                size_t start = i;
                while (i < n) {
                    BidiDirection curDir = GetCharDirection(rev[i]);
                    if (curDir == BidiDirection::LTR || curDir == BidiDirection::Number ||
                        (curDir == BidiDirection::Neutral && rev[i] != L' ' && rev[i] != L'\t' && rev[i] != L'\n' && rev[i] != L'\r')) {
                        i++;
                    } else {
                        break;
                    }
                }
                std::reverse(rev.begin() + start, rev.begin() + i);
            } else {
                i++;
            }
        }
        return rev;
    } else {
        std::wstring res = mirrored;
        size_t n = res.size();
        size_t i = 0;
        while (i < n) {
            if (GetCharDirection(res[i]) == BidiDirection::RTL) {
                size_t start = i;
                while (i < n && (GetCharDirection(res[i]) == BidiDirection::RTL || ArabicShaper::IsTashkeel(res[i]))) {
                    i++;
                }
                std::reverse(res.begin() + start, res.begin() + i);
            } else {
                i++;
            }
        }
        return res;
    }
}

std::vector<size_t> BidiEngine::GetVisualToLogicalMap(const std::wstring& text) {
    size_t n = text.size();
    std::vector<size_t> map(n);
    for (size_t i = 0; i < n; ++i) map[i] = i;
    if (n == 0) return map;

    BidiDirection paraDir = BidiDirection::LTR;
    for (wchar_t c : text) {
        BidiDirection d = GetCharDirection(c);
        if (d == BidiDirection::RTL) {
            paraDir = BidiDirection::RTL;
            break;
        } else if (d == BidiDirection::LTR) {
            paraDir = BidiDirection::LTR;
            break;
        }
    }

    if (paraDir == BidiDirection::RTL) {
        std::reverse(map.begin(), map.end());
        size_t i = 0;
        while (i < n) {
            BidiDirection dir = GetCharDirection(text[map[i]]);
            if (dir == BidiDirection::LTR || dir == BidiDirection::Number) {
                size_t start = i;
                while (i < n) {
                    wchar_t curC = text[map[i]];
                    BidiDirection curDir = GetCharDirection(curC);
                    if (curDir == BidiDirection::LTR || curDir == BidiDirection::Number ||
                        (curDir == BidiDirection::Neutral && curC != L' ' && curC != L'\t' && curC != L'\n' && curC != L'\r')) {
                        i++;
                    } else {
                        break;
                    }
                }
                std::reverse(map.begin() + start, map.begin() + i);
            } else {
                i++;
            }
        }
    } else {
        size_t i = 0;
        while (i < n) {
            if (GetCharDirection(text[map[i]]) == BidiDirection::RTL) {
                size_t start = i;
                while (i < n && (GetCharDirection(text[map[i]]) == BidiDirection::RTL || ArabicShaper::IsTashkeel(text[map[i]]))) {
                    i++;
                }
                std::reverse(map.begin() + start, map.begin() + i);
            } else {
                i++;
            }
        }
    }
    return map;
}

std::wstring BidiEngine::ProcessText(const std::wstring& text) {
    if (!ArabicShaper::ContainsArabic(text)) {
        return text;
    }


    std::wstring normalized = ArabicShaper::NormalizeToNominal(text);
    std::wstring shaped = ArabicShaper::Reshape(normalized);
    std::wstring visual = ReorderVisual(shaped);

    return visual;
}

}
