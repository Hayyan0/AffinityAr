#include "dwrite_shaper.h"
#include "arabic_shaper.h"
#include <iostream>
#include <algorithm>

#pragma comment(lib, "dwrite.lib")

namespace AffinityArabic {

class TextAnalysisSource : public IDWriteTextAnalysisSource {
    std::wstring m_text;
    LONG m_refCount;
public:
    TextAnalysisSource(const std::wstring& text) : m_text(text), m_refCount(1) {}

    IFACEMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IDWriteTextAnalysisSource) || riid == __uuidof(IUnknown)) {
            *ppv = this;
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHOD_(ULONG, AddRef)() override { return InterlockedIncrement(&m_refCount); }
    IFACEMETHOD_(ULONG, Release)() override {
        ULONG c = InterlockedDecrement(&m_refCount);
        if (c == 0) delete this;
        return c;
    }

    IFACEMETHOD(GetTextAtPosition)(UINT32 textPosition, const WCHAR** textString, UINT32* textLength) override {
        if (textPosition >= m_text.size()) {
            *textString = nullptr;
            *textLength = 0;
            return S_OK;
        }
        *textString = m_text.c_str() + textPosition;
        *textLength = (UINT32)(m_text.size() - textPosition);
        return S_OK;
    }

    IFACEMETHOD(GetTextBeforePosition)(UINT32 textPosition, const WCHAR** textString, UINT32* textLength) override {
        *textString = nullptr;
        *textLength = 0;
        return S_OK;
    }

    IFACEMETHOD_(DWRITE_READING_DIRECTION, GetParagraphReadingDirection)() override {
        return DWRITE_READING_DIRECTION_RIGHT_TO_LEFT;
    }

    IFACEMETHOD(GetLocaleName)(UINT32 textPosition, UINT32* textLength, const WCHAR** localeName) override {
        *localeName = L"ar-SA";
        *textLength = (UINT32)(m_text.size() - textPosition);
        return S_OK;
    }

    IFACEMETHOD(GetNumberSubstitution)(UINT32 textPosition, UINT32* textLength, IDWriteNumberSubstitution** numberSubstitution) override {
        *numberSubstitution = nullptr;
        *textLength = (UINT32)(m_text.size() - textPosition);
        return S_OK;
    }
};

struct ScriptRunItem {
    UINT32 textPosition;
    UINT32 textLength;
    DWRITE_SCRIPT_ANALYSIS scriptAnalysis;
};

struct BidiRunItem {
    UINT32 textPosition;
    UINT32 textLength;
    UINT8 explicitLevel;
    UINT8 resolvedLevel;
};

class TextAnalysisSink : public IDWriteTextAnalysisSink {
public:
    std::vector<ScriptRunItem> scriptRuns;
    std::vector<BidiRunItem> bidiRuns;

    IFACEMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IDWriteTextAnalysisSink) || riid == __uuidof(IUnknown)) {
            *ppv = this; return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    IFACEMETHOD_(ULONG, AddRef)() override { return 1; }
    IFACEMETHOD_(ULONG, Release)() override { return 1; }

    IFACEMETHOD(SetScriptAnalysis)(UINT32 textPosition, UINT32 textLength, const DWRITE_SCRIPT_ANALYSIS* sa) override {
        if (sa && textLength > 0) scriptRuns.push_back({ textPosition, textLength, *sa });
        return S_OK;
    }
    IFACEMETHOD(SetLineBreakpoints)(UINT32, UINT32, const DWRITE_LINE_BREAKPOINT*) override { return S_OK; }
    IFACEMETHOD(SetBidiLevel)(UINT32 textPosition, UINT32 textLength, UINT8 explicitLevel, UINT8 resolvedLevel) override {
        if (textLength > 0) bidiRuns.push_back({ textPosition, textLength, explicitLevel, resolvedLevel });
        return S_OK;
    }
    IFACEMETHOD(SetNumberSubstitution)(UINT32, UINT32, IDWriteNumberSubstitution*) override { return S_OK; }
};

struct UnifiedRun {
    UINT32 textPosition;
    UINT32 textLength;
    DWRITE_SCRIPT_ANALYSIS scriptAnalysis;
    UINT8 bidiLevel;
    bool isRTL;
};

static std::vector<UnifiedRun> BuildUnifiedRuns(
    UINT32 totalLen,
    const std::vector<ScriptRunItem>& scriptRuns,
    const std::vector<BidiRunItem>& bidiRuns
) {
    if (totalLen == 0) return {};
    std::vector<DWRITE_SCRIPT_ANALYSIS> charScript(totalLen);
    std::vector<UINT8> charBidi(totalLen, 1); 

    for (const auto& sr : scriptRuns) {
        for (UINT32 i = 0; i < sr.textLength && (sr.textPosition + i) < totalLen; ++i) {
            charScript[sr.textPosition + i] = sr.scriptAnalysis;
        }
    }
    for (const auto& br : bidiRuns) {
        for (UINT32 i = 0; i < br.textLength && (br.textPosition + i) < totalLen; ++i) {
            charBidi[br.textPosition + i] = br.resolvedLevel;
        }
    }

    std::vector<UnifiedRun> runs;
    UINT32 curStart = 0;
    for (UINT32 i = 1; i <= totalLen; ++i) {
        bool boundary = (i == totalLen);
        if (!boundary) {
            if (charScript[i].script != charScript[curStart].script ||
                charScript[i].shapes != charScript[curStart].shapes ||
                charBidi[i] != charBidi[curStart]) {
                boundary = true;
            }
        }
        if (boundary) {
            UnifiedRun r;
            r.textPosition = curStart;
            r.textLength = i - curStart;
            r.scriptAnalysis = charScript[curStart];
            r.bidiLevel = charBidi[curStart];
            r.isRTL = (charBidi[curStart] & 1) != 0;
            runs.push_back(r);
            curStart = i;
        }
    }
    return runs;
}

static std::wstring ToLower(const std::wstring& s) {
    std::wstring res = s;
    std::transform(res.begin(), res.end(), res.begin(), ::towlower);
    return res;
}

static std::wstring TrimWString(const std::wstring& s) {
    size_t start = 0;
    while (start < s.size() && iswspace(s[start])) start++;
    size_t end = s.size();
    while (end > start && iswspace(s[end - 1])) end--;
    return s.substr(start, end - start);
}

static std::wstring NormalizeFontKey(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        if (iswalnum(c)) {
            out.push_back((wchar_t)towlower(c));
        }
    }
    return out;
}

static wchar_t FoldCharToAscii(wchar_t c) {
    switch (c) {
    case L'ü': case L'Ü':
    case L'ú': case L'Ú':
    case L'ù': case L'Ù':
    case L'û': case L'Û':
        return L'u';
    case 0x0131: // Turkish i: ı
    case 0x0130: // Turkish I: İ
    case L'i': case L'I':
    case L'í': case L'Í':
    case L'ì': case L'Ì':
    case L'î': case L'Î':
    case L'ï': case L'Ï':
        return L'i';
    case L'ç': case L'Ç':
        return L'c';
    case 0x015F: case 0x015E: // Turkish ş, Ş
        return L's';
    case 0x011F: case 0x011E: // Turkish ğ, Ğ
        return L'g';
    case L'ö': case L'Ö':
    case L'ó': case L'Ó':
    case L'ò': case L'Ò':
    case L'ô': case L'Ô':
    case L'õ': case L'Õ':
    case L'ø': case L'Ø':
        return L'o';
    case L'ä': case L'Ä':
    case L'á': case L'Á':
    case L'à': case L'À':
    case L'â': case L'Â':
    case L'ã': case L'Ã':
    case L'å': case L'Å':
    case L'æ': case L'Æ':
        return L'a';
    case L'é': case L'É':
    case L'è': case L'È':
    case L'ê': case L'Ê':
    case L'ë': case L'Ë':
        return L'e';
    case L'ñ': case L'Ñ':
        return L'n';
    case L'ß':
        return L's';
    case L'ý': case L'Ý': case L'ÿ':
        return L'y';
    case L'?':
    case L'_':
    case 0xFFFD: // Unicode replacement char
        return L'?';
    default:
        if (c >= L'A' && c <= L'Z') return (wchar_t)(c + (L'a' - L'A'));
        if (c >= L'a' && c <= L'z') return c;
        if (c >= L'0' && c <= L'9') return c;
        return 0;
    }
}

static std::wstring FoldToAsciiKey(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        wchar_t folded = FoldCharToAscii(c);
        if (folded != 0) {
            out.push_back(folded);
        }
    }
    return out;
}

static bool MatchFoldedKey(const std::wstring& pattern, const std::wstring& candidate) {
    if (pattern.empty() || candidate.empty()) return false;
    if (pattern == candidate) return true;
    if (pattern.size() == candidate.size()) {
        bool match = true;
        for (size_t i = 0; i < pattern.size(); ++i) {
            if (pattern[i] != candidate[i] && pattern[i] != L'?' && candidate[i] != L'?') {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    size_t minLen = (std::min)(pattern.size(), candidate.size());
    if (minLen >= 4) {
        bool prefixMatch = true;
        for (size_t i = 0; i < minLen; ++i) {
            if (pattern[i] != candidate[i] && pattern[i] != L'?' && candidate[i] != L'?') {
                prefixMatch = false;
                break;
            }
        }
        if (prefixMatch) {
            size_t diff = (pattern.size() > candidate.size()) ? (pattern.size() - candidate.size()) : (candidate.size() - pattern.size());
            if (diff <= 2) return true;
        }
    }
    return false;
}

static bool MatchFoldedTokens(const std::wstring& pattern, const std::wstring& candidate) {
    std::vector<std::wstring> tokens;
    std::wstring cur;
    for (wchar_t c : pattern) {
        if (c != L'?' && iswalnum(c)) {
            cur.push_back(c);
        } else {
            if (cur.size() >= 3) tokens.push_back(cur);
            cur.clear();
        }
    }
    if (cur.size() >= 3) tokens.push_back(cur);

    if (tokens.empty()) return false;
    int matchedCount = 0;
    for (const auto& tok : tokens) {
        if (candidate.find(tok) != std::wstring::npos) {
            matchedCount++;
        }
    }
    return (matchedCount >= 2 || (tokens.size() == 1 && matchedCount == 1));
}

static std::wstring GetInfoString(IDWriteFont* pFont, DWRITE_INFORMATIONAL_STRING_ID infoId) {
    if (!pFont) return L"";
    IDWriteLocalizedStrings* pStrings = nullptr;
    BOOL exists = FALSE;
    pFont->GetInformationalStrings(infoId, &pStrings, &exists);
    if (!exists || !pStrings) return L"";
    UINT32 len = 0;
    pStrings->GetStringLength(0, &len);
    std::wstring s(len + 1, L'\0');
    pStrings->GetString(0, &s[0], len + 1);
    s.resize(len);
    pStrings->Release();
    return s;
}

static std::wstring GetFaceName(IDWriteFont* pFont) {
    if (!pFont) return L"";
    IDWriteLocalizedStrings* pNames = nullptr;
    if (FAILED(pFont->GetFaceNames(&pNames)) || !pNames) return L"";
    UINT32 len = 0;
    pNames->GetStringLength(0, &len);
    std::wstring s(len + 1, L'\0');
    pNames->GetString(0, &s[0], len + 1);
    s.resize(len);
    pNames->Release();
    return s;
}

static void ParseFontAttributes(
    const std::wstring& fontName,
    DWRITE_FONT_WEIGHT& outWeight,
    DWRITE_FONT_STRETCH& outStretch,
    DWRITE_FONT_STYLE& outStyle
) {
    outWeight = DWRITE_FONT_WEIGHT_NORMAL;
    outStretch = DWRITE_FONT_STRETCH_NORMAL;
    outStyle = DWRITE_FONT_STYLE_NORMAL;

    std::wstring lower = ToLower(fontName);

    if (lower.find(L"italic") != std::wstring::npos) {
        outStyle = DWRITE_FONT_STYLE_ITALIC;
    } else if (lower.find(L"oblique") != std::wstring::npos || lower.find(L"slanted") != std::wstring::npos) {
        outStyle = DWRITE_FONT_STYLE_OBLIQUE;
    }
    if (lower.find(L"ultra condensed") != std::wstring::npos || lower.find(L"ultra-condensed") != std::wstring::npos || lower.find(L"ultracondensed") != std::wstring::npos) {
        outStretch = DWRITE_FONT_STRETCH_ULTRA_CONDENSED;
    } else if (lower.find(L"extra condensed") != std::wstring::npos || lower.find(L"extra-condensed") != std::wstring::npos || lower.find(L"extracondensed") != std::wstring::npos) {
        outStretch = DWRITE_FONT_STRETCH_EXTRA_CONDENSED;
    } else if (lower.find(L"semi condensed") != std::wstring::npos || lower.find(L"semi-condensed") != std::wstring::npos || lower.find(L"semicondensed") != std::wstring::npos ||
               lower.find(L"semi cond") != std::wstring::npos || lower.find(L"semi-cond") != std::wstring::npos) {
        outStretch = DWRITE_FONT_STRETCH_SEMI_CONDENSED;
    } else if (lower.find(L"condensed") != std::wstring::npos || lower.find(L"cond") != std::wstring::npos || lower.find(L"narrow") != std::wstring::npos || lower.find(L"compressed") != std::wstring::npos) {
        outStretch = DWRITE_FONT_STRETCH_CONDENSED;
    } else if (lower.find(L"ultra wide") != std::wstring::npos || lower.find(L"ultra-wide") != std::wstring::npos || lower.find(L"ultrawide") != std::wstring::npos ||
               lower.find(L"ultra expanded") != std::wstring::npos || lower.find(L"ultra-expanded") != std::wstring::npos) {
        outStretch = DWRITE_FONT_STRETCH_ULTRA_EXPANDED;
    } else if (lower.find(L"extra wide") != std::wstring::npos || lower.find(L"extra-wide") != std::wstring::npos || lower.find(L"extrawide") != std::wstring::npos ||
               lower.find(L"extra expanded") != std::wstring::npos || lower.find(L"extra-expanded") != std::wstring::npos) {
        outStretch = DWRITE_FONT_STRETCH_EXTRA_EXPANDED;
    } else if (lower.find(L"semi wide") != std::wstring::npos || lower.find(L"semi-wide") != std::wstring::npos || lower.find(L"semiwide") != std::wstring::npos ||
               lower.find(L"semi expanded") != std::wstring::npos || lower.find(L"semi-expanded") != std::wstring::npos) {
        outStretch = DWRITE_FONT_STRETCH_SEMI_EXPANDED;
    } else if (lower.find(L"wide") != std::wstring::npos || lower.find(L"expanded") != std::wstring::npos || lower.find(L"extended") != std::wstring::npos) {
        outStretch = DWRITE_FONT_STRETCH_EXPANDED;
    }

    // Weight
    if (lower.find(L"thin") != std::wstring::npos || lower.find(L"hairline") != std::wstring::npos) {
        outWeight = DWRITE_FONT_WEIGHT_THIN;
    } else if (lower.find(L"extralight") != std::wstring::npos || lower.find(L"extra light") != std::wstring::npos ||
               lower.find(L"ultralight") != std::wstring::npos || lower.find(L"ultra light") != std::wstring::npos) {
        outWeight = DWRITE_FONT_WEIGHT_EXTRA_LIGHT;
    } else if (lower.find(L"light") != std::wstring::npos) {
        outWeight = DWRITE_FONT_WEIGHT_LIGHT;
    } else if (lower.find(L"semilight") != std::wstring::npos || lower.find(L"semi light") != std::wstring::npos) {
        outWeight = DWRITE_FONT_WEIGHT_SEMI_LIGHT;
    } else if (lower.find(L"extrabold") != std::wstring::npos || lower.find(L"extra bold") != std::wstring::npos ||
               lower.find(L"ultrabold") != std::wstring::npos || lower.find(L"ultra bold") != std::wstring::npos) {
        outWeight = DWRITE_FONT_WEIGHT_EXTRA_BOLD;
    } else if (lower.find(L"semibold") != std::wstring::npos || lower.find(L"semi bold") != std::wstring::npos ||
               lower.find(L"demibold") != std::wstring::npos || lower.find(L"demi bold") != std::wstring::npos) {
        outWeight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    } else if (lower.find(L"bold") != std::wstring::npos) {
        outWeight = DWRITE_FONT_WEIGHT_BOLD;
    } else if (lower.find(L"black") != std::wstring::npos || lower.find(L"heavy") != std::wstring::npos) {
        outWeight = DWRITE_FONT_WEIGHT_BLACK;
    } else if (lower.find(L"medium") != std::wstring::npos) {
        outWeight = DWRITE_FONT_WEIGHT_MEDIUM;
    }
}

static void ParseFontWeightAndStyle(
    const std::wstring& fontName,
    DWRITE_FONT_WEIGHT& outWeight,
    DWRITE_FONT_STYLE& outStyle
) {
    DWRITE_FONT_STRETCH dummy;
    ParseFontAttributes(fontName, outWeight, dummy, outStyle);
}

static IDWriteFont* FindFontByPostScriptName(
    IDWriteFontFamily* pFamily,
    const std::wstring& psName,
    UINT32* outFontIndex = nullptr
) {
    if (!pFamily || psName.empty()) return nullptr;
    std::wstring reqKey = NormalizeFontKey(psName);
    UINT32 count = pFamily->GetFontCount();
    for (UINT32 f = 0; f < count; ++f) {
        IDWriteFont* pF = nullptr;
        if (SUCCEEDED(pFamily->GetFont(f, &pF)) && pF) {
            std::wstring ps = GetInfoString(pF, DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME);
            if (!ps.empty() && NormalizeFontKey(ps) == reqKey) {
                if (outFontIndex) *outFontIndex = f;
                return pF;
            }
            pF->Release();
        }
    }
    return nullptr;
}

static IDWriteFont* FindBestFontInFamily(
    IDWriteFontFamily* pFamily,
    const std::wstring& requestedFontName,
    DWRITE_FONT_WEIGHT targetWeight,
    DWRITE_FONT_STRETCH targetStretch,
    DWRITE_FONT_STYLE targetStyle,
    UINT32* outFontIndex = nullptr,
    const std::wstring& requestedPostscriptName = L""
) {
    if (!pFamily) return nullptr;
    UINT32 fontCount = pFamily->GetFontCount();
    if (fontCount == 0) return nullptr;

    if (!requestedPostscriptName.empty()) {
        IDWriteFont* exactFont = FindFontByPostScriptName(pFamily, requestedPostscriptName, outFontIndex);
        if (exactFont) return exactFont;
    }

    std::wstring reqLower = ToLower(requestedFontName);
    std::wstring reqKey = NormalizeFontKey(requestedFontName);

    IDWriteFont* bestFont = nullptr;
    UINT32 bestIndex = (UINT32)-1;
    int bestScore = -1;

    for (UINT32 f = 0; f < fontCount; ++f) {
        IDWriteFont* pFont = nullptr;
        if (FAILED(pFamily->GetFont(f, &pFont)) || !pFont) continue;

        std::wstring fullName = GetInfoString(pFont, DWRITE_INFORMATIONAL_STRING_FULL_NAME);
        std::wstring psName = GetInfoString(pFont, DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME);
        std::wstring win32Fam = GetInfoString(pFont, DWRITE_INFORMATIONAL_STRING_WIN32_FAMILY_NAMES);
        std::wstring win32Sub = GetInfoString(pFont, DWRITE_INFORMATIONAL_STRING_WIN32_SUBFAMILY_NAMES);
        std::wstring faceName = GetFaceName(pFont);

        std::wstring fullKey = NormalizeFontKey(fullName);
        std::wstring psKey = NormalizeFontKey(psName);
        std::wstring win32FamKey = NormalizeFontKey(win32Fam);
        std::wstring faceKey = NormalizeFontKey(faceName);

        std::wstring reqFolded = FoldToAsciiKey(requestedFontName);
        std::wstring fullFolded = FoldToAsciiKey(fullName);
        std::wstring psFolded = FoldToAsciiKey(psName);
        std::wstring faceFolded = FoldToAsciiKey(faceName);

        int score = 0;

        if (!psKey.empty() && psKey == reqKey) {
            score += 100000;
        } else if (!fullKey.empty() && fullKey == reqKey) {
            score += 90000;
        } else if (!faceKey.empty() && faceKey == reqKey) {
            score += 85000;
        } else if (!win32FamKey.empty() && win32FamKey == reqKey) {
            score += 1000;
        } else if (!reqFolded.empty()) {
            if (!psFolded.empty() && MatchFoldedKey(reqFolded, psFolded)) {
                score += 95000;
            } else if (!fullFolded.empty() && MatchFoldedKey(reqFolded, fullFolded)) {
                score += 88000;
            } else if (!faceFolded.empty() && MatchFoldedKey(reqFolded, faceFolded)) {
                score += 84000;
            }
        }

        if (pFont->GetStretch() == targetStretch) {
            score += 25000;
        } else {
            int stretchDiff = abs((int)pFont->GetStretch() - (int)targetStretch);
            score += (5000 - stretchDiff * 1000);
        }

        if (pFont->GetStyle() == targetStyle) {
            score += 10000;
        } else {
            score -= 10000;
        }

        int weightDiff = abs((int)pFont->GetWeight() - (int)targetWeight);
        score += (5000 - weightDiff * 5);

        if (targetWeight == DWRITE_FONT_WEIGHT_NORMAL &&
            (win32Sub == L"Regular" || faceName == L"Regular" || fullName.find(L"Regular") != std::wstring::npos)) {
            score += 2000;
        }

        if (score > bestScore) {
            if (bestFont) bestFont->Release();
            bestScore = score;
            bestFont = pFont;
            bestIndex = f;
        } else {
            pFont->Release();
        }
    }

    if (bestFont && outFontIndex) {
        *outFontIndex = bestIndex;
    }
    return bestFont;
}

static bool CheckFontHasGPOS(IDWriteFontFace* pFontFace) {
    if (!pFontFace) return false;
    const void* tableData = nullptr;
    UINT32 tableSize = 0;
    void* tableContext = nullptr;
    BOOL exists = FALSE;
    HRESULT hr = pFontFace->TryGetFontTable(
        DWRITE_MAKE_OPENTYPE_TAG('G', 'P', 'O', 'S'),
        &tableData,
        &tableSize,
        &tableContext,
        &exists
    );
    if (FAILED(hr) || !exists || !tableData || tableSize < 10) {
        if (exists && tableContext) pFontFace->ReleaseFontTable(tableContext);
        return false;
    }
    const uint8_t* p = reinterpret_cast<const uint8_t*>(tableData);
    uint16_t featOff = (static_cast<uint16_t>(p[6]) << 8) | static_cast<uint16_t>(p[7]);
    uint16_t lookOff = (static_cast<uint16_t>(p[8]) << 8) | static_cast<uint16_t>(p[9]);
    bool hasFeaturesAndLookups =
        (featOff > 0 && featOff + 2 <= tableSize && ((static_cast<uint16_t>(p[featOff]) << 8) | static_cast<uint16_t>(p[featOff + 1])) > 0) &&
        (lookOff > 0 && lookOff + 2 <= tableSize && ((static_cast<uint16_t>(p[lookOff]) << 8) | static_cast<uint16_t>(p[lookOff + 1])) > 0);
    pFontFace->ReleaseFontTable(tableContext);
    return hasFeaturesAndLookups;
}

DWriteShaper& DWriteShaper::Instance() {
    static DWriteShaper s_Instance;
    return s_Instance;
}

DWriteShaper::DWriteShaper() {
    Initialize();
}

DWriteShaper::~DWriteShaper() {
    if (m_pFontCollection) { m_pFontCollection->Release(); m_pFontCollection = nullptr; }
    if (m_pAnalyzer) { m_pAnalyzer->Release(); m_pAnalyzer = nullptr; }
    if (m_pFactory) { m_pFactory->Release(); m_pFactory = nullptr; }
}

bool DWriteShaper::Initialize() {
    if (m_initialized) return true;

    HRESULT hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&m_pFactory)
    );
    if (FAILED(hr) || !m_pFactory) return false;

    hr = m_pFactory->CreateTextAnalyzer(&m_pAnalyzer);
    if (FAILED(hr) || !m_pAnalyzer) return false;

    hr = m_pFactory->GetSystemFontCollection(&m_pFontCollection);
    if (FAILED(hr) || !m_pFontCollection) return false;

    m_initialized = true;
    return true;
}

static std::wstring GetCanonicalTokenKey(const std::wstring& name) {
    std::vector<std::wstring> tokens;
    std::wstring cur;
    for (wchar_t ch : name) {
        if (iswalnum(ch)) {
            if (iswupper(ch) && !cur.empty() && iswlower(cur.back())) {
                tokens.push_back(ToLower(cur));
                cur.clear();
            }
            cur += (wchar_t)towlower(ch);
        } else {
            if (!cur.empty()) {
                tokens.push_back(cur);
                cur.clear();
            }
        }
    }
    if (!cur.empty()) {
        tokens.push_back(cur);
    }

    std::vector<std::wstring> splitTokens;
    for (const auto& t : tokens) {
        if (t == L"semiextended") {
            splitTokens.push_back(L"semi");
            splitTokens.push_back(L"extended");
        } else if (t == L"extraextended") {
            splitTokens.push_back(L"extra");
            splitTokens.push_back(L"extended");
        } else if (t == L"extrabold") {
            splitTokens.push_back(L"extra");
            splitTokens.push_back(L"bold");
        } else if (t == L"extralight") {
            splitTokens.push_back(L"extra");
            splitTokens.push_back(L"light");
        } else if (t == L"semibold") {
            splitTokens.push_back(L"semi");
            splitTokens.push_back(L"bold");
        } else if (t == L"ultrabold") {
            splitTokens.push_back(L"ultra");
            splitTokens.push_back(L"bold");
        } else if (t == L"ultralight") {
            splitTokens.push_back(L"ultra");
            splitTokens.push_back(L"light");
        } else if (t != L"regular" && t != L"normal") {
            splitTokens.push_back(t);
        }
    }

    std::sort(splitTokens.begin(), splitTokens.end());
    std::wstring key;
    for (const auto& st : splitTokens) {
        if (!key.empty()) key += L"|";
        key += st;
    }
    return key;
}

void DWriteShaper::EnsureFontMapsIndexed() {
    if (m_mapsIndexed || !m_pFontCollection) return;
    m_mapsIndexed = true;

    UINT32 count = m_pFontCollection->GetFontFamilyCount();
    for (UINT32 i = 0; i < count; ++i) {
        IDWriteFontFamily* pFam = nullptr;
        if (FAILED(m_pFontCollection->GetFontFamily(i, &pFam)) || !pFam) continue;

        IDWriteLocalizedStrings* pFamNames = nullptr;
        if (SUCCEEDED(pFam->GetFamilyNames(&pFamNames)) && pFamNames) {
            UINT32 nc = pFamNames->GetCount();
            for (UINT32 n = 0; n < nc; ++n) {
                wchar_t nameBuf[256];
                if (SUCCEEDED(pFamNames->GetString(n, nameBuf, 256))) {
                    m_familyNameMap[NormalizeFontKey(nameBuf)] = i;
                    std::wstring foldedKey = FoldToAsciiKey(nameBuf);
                    if (!foldedKey.empty()) {
                        if (m_asciiFoldedFamilyMap.find(foldedKey) == m_asciiFoldedFamilyMap.end()) {
                            m_asciiFoldedFamilyMap[foldedKey] = i;
                        }
                        m_asciiFoldedFamilyList.push_back({ foldedKey, i });
                    }
                }
            }
            pFamNames->Release();
        }

        UINT32 fc = pFam->GetFontCount();
        for (UINT32 j = 0; j < fc; ++j) {
            IDWriteFont* pF = nullptr;
            if (FAILED(pFam->GetFont(j, &pF)) || !pF) continue;

            std::wstring ps = GetInfoString(pF, DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME);
            if (!ps.empty()) {
                m_psFontMap[NormalizeFontKey(ps)].push_back({ i, j });
                std::wstring canKey = GetCanonicalTokenKey(ps);
                if (!canKey.empty()) {
                    m_canonicalPsFontMap[canKey].push_back({ i, j });
                }
                std::wstring psFolded = FoldToAsciiKey(ps);
                if (!psFolded.empty() && m_asciiFoldedFamilyMap.find(psFolded) == m_asciiFoldedFamilyMap.end()) {
                    m_asciiFoldedFamilyMap[psFolded] = i;
                    m_asciiFoldedFamilyList.push_back({ psFolded, i });
                }
            }

            std::wstring fn = GetInfoString(pF, DWRITE_INFORMATIONAL_STRING_FULL_NAME);
            if (!fn.empty()) {
                m_fullNameFontMap[NormalizeFontKey(fn)].push_back({ i, j });
                std::wstring fnFolded = FoldToAsciiKey(fn);
                if (!fnFolded.empty() && m_asciiFoldedFamilyMap.find(fnFolded) == m_asciiFoldedFamilyMap.end()) {
                    m_asciiFoldedFamilyMap[fnFolded] = i;
                    m_asciiFoldedFamilyList.push_back({ fnFolded, i });
                }
            }

            std::wstring typo = GetInfoString(pF, DWRITE_INFORMATIONAL_STRING_TYPOGRAPHIC_FAMILY_NAMES);
            if (!typo.empty()) {
                std::wstring typoKey = NormalizeFontKey(typo);
                m_typoFamilyFontMap[typoKey].push_back({ i, j });
                if (m_familyNameMap.find(typoKey) == m_familyNameMap.end()) {
                    m_familyNameMap[typoKey] = i;
                }
                std::wstring typoFolded = FoldToAsciiKey(typo);
                if (!typoFolded.empty() && m_asciiFoldedFamilyMap.find(typoFolded) == m_asciiFoldedFamilyMap.end()) {
                    m_asciiFoldedFamilyMap[typoFolded] = i;
                    m_asciiFoldedFamilyList.push_back({ typoFolded, i });
                }
            }

            std::wstring wws = GetInfoString(pF, DWRITE_INFORMATIONAL_STRING_WEIGHT_STRETCH_STYLE_FAMILY_NAME);
            if (!wws.empty()) {
                std::wstring wwsKey = NormalizeFontKey(wws);
                m_wwsFamilyFontMap[wwsKey].push_back({ i, j });
                if (m_familyNameMap.find(wwsKey) == m_familyNameMap.end()) {
                    m_familyNameMap[wwsKey] = i;
                }
                std::wstring wwsFolded = FoldToAsciiKey(wws);
                if (!wwsFolded.empty() && m_asciiFoldedFamilyMap.find(wwsFolded) == m_asciiFoldedFamilyMap.end()) {
                    m_asciiFoldedFamilyMap[wwsFolded] = i;
                    m_asciiFoldedFamilyList.push_back({ wwsFolded, i });
                }
            }

            pF->Release();
        }
        pFam->Release();
    }
}

static std::wstring StripFontPrefixesAndSuffixes(const std::wstring& rawName) {
    std::wstring s = TrimWString(rawName);
    const wchar_t* prefixes[] = {
        L"ArbFONTS-", L"ArbFONTS_", L"ArbFONTS ", L"alfont_com_", L"alfont_com-"
    };
    std::wstring lower = ToLower(s);
    for (const wchar_t* pre : prefixes) {
        size_t preLen = wcslen(pre);
        std::wstring lowerPre = ToLower(pre);
        if (lower.rfind(lowerPre, 0) == 0 && s.size() > preLen) {
            s = TrimWString(s.substr(preLen));
            lower = ToLower(s);
            break;
        }
    }

    const wchar_t* suffixes[] = {
        L" Regular", L"-Regular", L" Bold", L"-Bold", L" Italic", L" Bold Italic", L" Slanted",
        L" Light", L" Light Slanted", L" Medium", L" Medium Slanted",
        L" Black", L" Black Slanted", L" Thin", L" Thin Slanted",
        L" SemiBold", L" SemiBold Slanted", L" ExtraBold", L" ExtraLight",
        L" Hairline", L" Semi Hairline", L" Heavy", L" UltraBold", L" UltraLight",
        L" SemiBold Italic", L" ExtraBold Italic", L" Light Italic", L" Medium Italic",
        L" Flat Regular", L" Flat Bold", L" Flat Light", L" Flat Medium", L" Flat",
        L" Sans Regular", L" Sans Bold", L" Sans Light", L" Sans Medium",
        L" Variable",
        L" ExtraExtended", L" Extra Extended", L"-ExtraExtended",
        L" SemiExtended", L" Semi Extended", L"-SemiExtended",
        L" Extended", L"-Extended"
    };

    for (const wchar_t* suf : suffixes) {
        size_t sufLen = wcslen(suf);
        if (s.size() > sufLen) {
            size_t pos = lower.rfind(ToLower(suf));
            if (pos != std::wstring::npos && pos + sufLen == s.size()) {
                s = TrimWString(s.substr(0, pos));
                break;
            }
        }
    }
    return s;
}

static bool FamilyHasMatchingStretch(IDWriteFontFamily* pFamily, DWRITE_FONT_STRETCH targetStretch) {
    if (!pFamily || targetStretch == DWRITE_FONT_STRETCH_NORMAL) return true;
    UINT32 count = pFamily->GetFontCount();
    for (UINT32 f = 0; f < count; ++f) {
        IDWriteFont* pF = nullptr;
        if (SUCCEEDED(pFamily->GetFont(f, &pF)) && pF) {
            DWRITE_FONT_STRETCH s = pF->GetStretch();
            pF->Release();
            if (targetStretch < DWRITE_FONT_STRETCH_NORMAL && s < DWRITE_FONT_STRETCH_NORMAL) {
                return true;
            }
            if (targetStretch > DWRITE_FONT_STRETCH_NORMAL && s > DWRITE_FONT_STRETCH_NORMAL) {
                return true;
            }
        }
    }
    return false;
}

bool DWriteShaper::FindFontFamilySmart(const std::wstring& fontName, UINT32* outFamilyIndex) {
    if (!m_pFontCollection || fontName.empty() || !outFamilyIndex) return false;

    auto it = m_familyIndexCache.find(fontName);
    if (it != m_familyIndexCache.end()) {
        *outFamilyIndex = it->second;
        return true;
    }

    DWRITE_FONT_WEIGHT targetWeight;
    DWRITE_FONT_STRETCH targetStretch;
    DWRITE_FONT_STYLE targetStyle;
    ParseFontAttributes(fontName, targetWeight, targetStretch, targetStyle);

    std::wstring trimmed = TrimWString(fontName);
    std::wstring stripped = StripFontPrefixesAndSuffixes(trimmed);

    const std::wstring candidates[] = {
        fontName,
        trimmed,
        stripped,
        trimmed.empty() ? L"" : (trimmed + L" Variable"),
        stripped.empty() ? L"" : (stripped + L" Variable")
    };

    auto testFamily = [&](const std::wstring& nameStr, UINT32* outIdx, bool requireStretch) -> bool {
        if (nameStr.empty()) return false;
        UINT32 idx = 0;
        BOOL exists = FALSE;
        m_pFontCollection->FindFamilyName(nameStr.c_str(), &idx, &exists);
        if (exists) {
            if (requireStretch && targetStretch != DWRITE_FONT_STRETCH_NORMAL) {
                IDWriteFontFamily* pCandFam = nullptr;
                if (SUCCEEDED(m_pFontCollection->GetFontFamily(idx, &pCandFam)) && pCandFam) {
                    bool ok = FamilyHasMatchingStretch(pCandFam, targetStretch);
                    pCandFam->Release();
                    if (!ok) return false;
                }
            }
            *outIdx = idx;
            return true;
        }
        return false;
    };

    UINT32 familyIndex = 0;
    for (int pass = 0; pass < (targetStretch != DWRITE_FONT_STRETCH_NORMAL ? 2 : 1); ++pass) {
        bool requireStretch = (pass == 0 && targetStretch != DWRITE_FONT_STRETCH_NORMAL);

        for (const auto& cand : candidates) {
            if (testFamily(cand, &familyIndex, requireStretch)) {
                m_familyIndexCache[fontName] = familyIndex;
                *outFamilyIndex = familyIndex;
                return true;
            }
        }

        std::wstring cur = stripped.empty() ? trimmed : stripped;
        while (true) {
            size_t pos = cur.find_last_of(L" -_");
            if (pos == std::wstring::npos || pos == 0) break;
            cur = TrimWString(cur.substr(0, pos));
            if (cur.empty()) break;

            if (testFamily(cur, &familyIndex, requireStretch) || testFamily(cur + L" Variable", &familyIndex, requireStretch)) {
                m_familyIndexCache[fontName] = familyIndex;
                *outFamilyIndex = familyIndex;
                return true;
            }
        }
    }

    std::wstring foldedTrimmed = FoldToAsciiKey(trimmed);
    std::wstring foldedStripped = FoldToAsciiKey(stripped);
    const std::wstring foldedCands[] = { foldedTrimmed, foldedStripped };

    for (const auto& fc : foldedCands) {
        if (fc.empty()) continue;
        auto fit = m_asciiFoldedFamilyMap.find(fc);
        if (fit != m_asciiFoldedFamilyMap.end()) {
            if (targetStretch != DWRITE_FONT_STRETCH_NORMAL) {
                IDWriteFontFamily* pCandFam = nullptr;
                if (SUCCEEDED(m_pFontCollection->GetFontFamily(fit->second, &pCandFam)) && pCandFam) {
                    bool ok = FamilyHasMatchingStretch(pCandFam, targetStretch);
                    pCandFam->Release();
                    if (!ok) goto skipFoldedExact;
                }
            }
            m_familyIndexCache[fontName] = fit->second;
            *outFamilyIndex = fit->second;
            return true;
        }
    skipFoldedExact:;
    }

    for (const auto& fc : foldedCands) {
        if (fc.empty()) continue;
        if (fc.find(L'?') != std::wstring::npos) {
            for (const auto& item : m_asciiFoldedFamilyList) {
                if (MatchFoldedKey(fc, item.first)) {
                    m_familyIndexCache[fontName] = item.second;
                    *outFamilyIndex = item.second;
                    return true;
                }
            }
        }
    }

    for (const auto& fc : foldedCands) {
        if (fc.empty()) continue;
        for (const auto& item : m_asciiFoldedFamilyList) {
            if (MatchFoldedTokens(fc, item.first)) {
                m_familyIndexCache[fontName] = item.second;
                *outFamilyIndex = item.second;
                return true;
            }
        }
    }

    std::wstring lowerTrimmed = ToLower(trimmed);
    std::wstring lowerStripped = ToLower(stripped);
    std::wstring keyTrimmed = NormalizeFontKey(trimmed);
    std::wstring keyStripped = NormalizeFontKey(stripped);

    UINT32 count = m_pFontCollection->GetFontFamilyCount();
    UINT32 bestIndex = (UINT32)-1;
    size_t bestScore = 0;

    auto scoreCandidate = [&](const wchar_t* candidateStr, UINT32 famIdx) {
        std::wstring candTrim = TrimWString(candidateStr);
        if (candTrim.empty()) return;
        std::wstring lowerFam = ToLower(candTrim);
        std::wstring keyFam = NormalizeFontKey(candTrim);
        if (keyFam.empty()) return;

        size_t targetLen = keyStripped.empty() ? keyTrimmed.size() : keyStripped.size();
        size_t minLen = (std::min)(keyFam.size(), targetLen);
        size_t diffLen = (keyFam.size() > targetLen) ? (keyFam.size() - targetLen) : (targetLen - keyFam.size());
        if (diffLen > 99) diffLen = 99;

        size_t score = 0;
        if (lowerFam == lowerTrimmed || lowerFam == lowerStripped ||
            keyFam == keyTrimmed || (!keyStripped.empty() && keyFam == keyStripped)) {
            score = 100000 + keyFam.size();
        } else if (lowerTrimmed.find(lowerFam) == 0 || lowerFam.find(lowerTrimmed) == 0 ||
                   (!lowerStripped.empty() && (lowerStripped.find(lowerFam) == 0 || lowerFam.find(lowerStripped) == 0)) ||
                   keyTrimmed.find(keyFam) == 0 || keyFam.find(keyTrimmed) == 0 ||
                   (!keyStripped.empty() && (keyStripped.find(keyFam) == 0 || keyFam.find(keyStripped) == 0))) {
            score = 50000 + minLen * 100 + (100 - diffLen);
        } else if (minLen >= 4 &&
                   (keyTrimmed.find(keyFam) != std::wstring::npos ||
                    keyFam.find(keyTrimmed) != std::wstring::npos ||
                    (!keyStripped.empty() && (keyStripped.find(keyFam) != std::wstring::npos || keyFam.find(keyStripped) != std::wstring::npos)))) {
            score = 10000 + minLen * 100 + (100 - diffLen);
        }

        std::wstring foldedFam = FoldToAsciiKey(candTrim);
        if (!foldedFam.empty()) {
            if (foldedFam == foldedTrimmed || (!foldedStripped.empty() && foldedFam == foldedStripped)) {
                score = (std::max)(score, (size_t)98000);
            } else if (MatchFoldedKey(foldedTrimmed, foldedFam) || (!foldedStripped.empty() && MatchFoldedKey(foldedStripped, foldedFam))) {
                score = (std::max)(score, (size_t)95000);
            } else if (MatchFoldedTokens(foldedTrimmed, foldedFam) || (!foldedStripped.empty() && MatchFoldedTokens(foldedStripped, foldedFam))) {
                score = (std::max)(score, (size_t)90000);
            }
        }

        if (score > bestScore) {
            bestScore = score;
            bestIndex = famIdx;
        }
    };

    for (UINT32 i = 0; i < count; ++i) {
        IDWriteFontFamily* pFam = nullptr;
        if (SUCCEEDED(m_pFontCollection->GetFontFamily(i, &pFam))) {
            IDWriteLocalizedStrings* pNames = nullptr;
            if (SUCCEEDED(pFam->GetFamilyNames(&pNames))) {
                UINT32 nameCount = pNames->GetCount();
                for (UINT32 j = 0; j < nameCount; ++j) {
                    wchar_t nameBuf[256];
                    if (SUCCEEDED(pNames->GetString(j, nameBuf, 256))) {
                        scoreCandidate(nameBuf, i);
                    }
                }
                pNames->Release();
            }
            pFam->Release();
        }
    }

    if (bestIndex != (UINT32)-1) {
        m_familyIndexCache[fontName] = bestIndex;
        *outFamilyIndex = bestIndex;
        return true;
    }

    return false;
}

bool DWriteShaper::ShapeTextDetailed(
    const std::wstring& text,
    const std::wstring& fontName,
    float fontSize,
    ShapedTextRun& outRun,
    const std::wstring& postscriptName,
    uint32_t sampleChar,
    uint16_t sampleGid,
    const std::vector<DWRITE_FONT_AXIS_VALUE>& axisValues,
    const std::vector<DWRITE_FONT_FEATURE>& fontFeatures
) {
    outRun = {};
    if (!Initialize() || text.empty()) return false;
    EnsureFontMapsIndexed();

    std::wstring cacheKey = postscriptName.empty() ? fontName : (fontName + L"|" + postscriptName);
    if (sampleGid > 0) {
        cacheKey += L"|" + std::to_wstring(sampleChar) + L"|" + std::to_wstring(sampleGid);
    }
    for (const auto& ax : axisValues) {
        cacheKey += L"|" + std::to_wstring(ax.axisTag) + L"=" + std::to_wstring((int)(ax.value * 100.0f));
    }

    IDWriteFontFamily* pFamily = nullptr;
    IDWriteFont* pFont = nullptr;

    auto fit = m_fontIndexCache.find(cacheKey);
    auto famIt = m_familyIndexCache.find(cacheKey);
    if (fit != m_fontIndexCache.end() && famIt != m_familyIndexCache.end()) {
        if (SUCCEEDED(m_pFontCollection->GetFontFamily(famIt->second, &pFamily)) && pFamily) {
            if (fit->second < pFamily->GetFontCount()) {
                pFamily->GetFont(fit->second, &pFont);
            }
        }
    }

    if (!pFont) {
        if (pFamily) { pFamily->Release(); pFamily = nullptr; }

        UINT32 familyIndex = 0;
        UINT32 chosenFontIndex = 0;

        DWRITE_FONT_WEIGHT targetWeight;
        DWRITE_FONT_STRETCH targetStretch;
        DWRITE_FONT_STYLE targetStyle;
        ParseFontAttributes(fontName, targetWeight, targetStretch, targetStyle);

        auto evaluateCandidates = [&](const std::vector<std::pair<UINT32, UINT32>>& cands) -> IDWriteFont* {
            IDWriteFont* bestCandidate = nullptr;
            int bestScore = -1;
            bool foundGidMatch = false;

            for (const auto& c : cands) {
                IDWriteFontFamily* pCandFam = nullptr;
                if (FAILED(m_pFontCollection->GetFontFamily(c.first, &pCandFam)) || !pCandFam) continue;
                IDWriteFont* pF = nullptr;
                if (SUCCEEDED(pCandFam->GetFont(c.second, &pF)) && pF) {
                    bool matchesGid = false;
                    if (sampleGid > 0 && sampleChar > 0) {
                        IDWriteFontFace* pFace = nullptr;
                        if (SUCCEEDED(pF->CreateFontFace(&pFace)) && pFace) {
                            UINT16 gid = 0;
                            pFace->GetGlyphIndices(&sampleChar, 1, &gid);
                            pFace->Release();
                            matchesGid = (gid == sampleGid);
                        }
                    }

                    if (foundGidMatch && !matchesGid) {
                        pF->Release();
                        pCandFam->Release();
                        continue;
                    }

                    int score = 0;
                    if (matchesGid) score += 10000;
                    if (pF->GetStyle() == targetStyle) score += 1000;
                    if (pF->GetStretch() == targetStretch) score += 500;
                    int weightDiff = abs((int)pF->GetWeight() - (int)targetWeight);
                    score += (500 - weightDiff);

                    if (matchesGid && !foundGidMatch) {
                        if (bestCandidate) bestCandidate->Release();
                        bestCandidate = pF;
                        bestScore = score;
                        familyIndex = c.first;
                        chosenFontIndex = c.second;
                        if (pFamily) pFamily->Release();
                        pFamily = pCandFam;
                        pCandFam = nullptr;
                        foundGidMatch = true;
                    } else if (score > bestScore) {
                        if (bestCandidate) bestCandidate->Release();
                        bestCandidate = pF;
                        bestScore = score;
                        familyIndex = c.first;
                        chosenFontIndex = c.second;
                        if (pFamily) pFamily->Release();
                        pFamily = pCandFam;
                        pCandFam = nullptr;
                    } else {
                        pF->Release();
                    }
                }
                if (pCandFam) pCandFam->Release();
            }
            return bestCandidate;
        };

        if (!postscriptName.empty()) {
            auto it = m_psFontMap.find(NormalizeFontKey(postscriptName));
            if (it != m_psFontMap.end()) {
                pFont = evaluateCandidates(it->second);
            }
            if (!pFont) {
                auto canIt = m_canonicalPsFontMap.find(GetCanonicalTokenKey(postscriptName));
                if (canIt != m_canonicalPsFontMap.end()) {
                    pFont = evaluateCandidates(canIt->second);
                }
            }
        }

        if (!pFont && !fontName.empty()) {
            auto it = m_fullNameFontMap.find(NormalizeFontKey(fontName));
            if (it != m_fullNameFontMap.end()) {
                pFont = evaluateCandidates(it->second);
            }
            if (!pFont) {
                auto canIt = m_canonicalPsFontMap.find(GetCanonicalTokenKey(fontName));
                if (canIt != m_canonicalPsFontMap.end()) {
                    pFont = evaluateCandidates(canIt->second);
                }
            }
        }

        if (!pFont && !fontName.empty()) {
            auto it = m_typoFamilyFontMap.find(NormalizeFontKey(fontName));
            if (it != m_typoFamilyFontMap.end()) {
                pFont = evaluateCandidates(it->second);
            }
            if (!pFont) {
                std::wstring stripped = StripFontPrefixesAndSuffixes(fontName);
                if (!stripped.empty()) {
                    auto sit = m_typoFamilyFontMap.find(NormalizeFontKey(stripped));
                    if (sit != m_typoFamilyFontMap.end()) {
                        pFont = evaluateCandidates(sit->second);
                    }
                }
            }
        }

        if (!pFont && !fontName.empty()) {
            auto it = m_wwsFamilyFontMap.find(NormalizeFontKey(fontName));
            if (it != m_wwsFamilyFontMap.end()) {
                pFont = evaluateCandidates(it->second);
            }
        }

        if (!pFont && !postscriptName.empty()) {
            std::vector<std::wstring> candidates;
            std::wstring trimmed = TrimWString(fontName);
            if (!trimmed.empty()) {
                candidates.push_back(trimmed);
                std::wstring cur = trimmed;
                while (true) {
                    size_t pos = cur.find_last_of(L" -_");
                    if (pos == std::wstring::npos || pos == 0) break;
                    cur = TrimWString(cur.substr(0, pos));
                    if (cur.empty()) break;
                    candidates.push_back(cur);
                    candidates.push_back(cur + L" Variable");
                }
            }
            size_t hyphen = postscriptName.find(L'-');
            if (hyphen != std::wstring::npos && hyphen > 0) {
                std::wstring psPrefix = postscriptName.substr(0, hyphen);
                candidates.push_back(psPrefix);
            }

            for (const auto& cand : candidates) {
                BOOL exists = FALSE;
                UINT32 famIdx = 0;
                m_pFontCollection->FindFamilyName(cand.c_str(), &famIdx, &exists);
                if (exists) {
                    IDWriteFontFamily* pCandFam = nullptr;
                    if (SUCCEEDED(m_pFontCollection->GetFontFamily(famIdx, &pCandFam)) && pCandFam) {
                        IDWriteFont* pCandFont = FindFontByPostScriptName(pCandFam, postscriptName, &chosenFontIndex);
                        if (pCandFont) {
                            if (pFamily) pFamily->Release();
                            pFamily = pCandFam;
                            pFont = pCandFont;
                            familyIndex = famIdx;
                            break;
                        }
                        pCandFam->Release();
                    }
                }
            }
        }

        if (!pFont && !fontName.empty()) {
            auto it = m_familyNameMap.find(NormalizeFontKey(fontName));
            if (it != m_familyNameMap.end()) {
                if (SUCCEEDED(m_pFontCollection->GetFontFamily(it->second, &pFamily)) && pFamily) {
                    familyIndex = it->second;
                    pFont = FindBestFontInFamily(pFamily, fontName, targetWeight, targetStretch, targetStyle, &chosenFontIndex, postscriptName);
                }
            }
        }

        if (!pFont && !fontName.empty()) {
            std::wstring folded = FoldToAsciiKey(fontName);
            auto it = m_asciiFoldedFamilyMap.find(folded);
            if (it != m_asciiFoldedFamilyMap.end()) {
                if (SUCCEEDED(m_pFontCollection->GetFontFamily(it->second, &pFamily)) && pFamily) {
                    familyIndex = it->second;
                    pFont = FindBestFontInFamily(pFamily, fontName, targetWeight, targetStretch, targetStyle, &chosenFontIndex, postscriptName);
                }
            }
        }

        if (!pFont) {
            if (FindFontFamilySmart(fontName, &familyIndex)) {
                if (SUCCEEDED(m_pFontCollection->GetFontFamily(familyIndex, &pFamily)) && pFamily) {
                    pFont = FindBestFontInFamily(pFamily, fontName, targetWeight, targetStretch, targetStyle, &chosenFontIndex, postscriptName);
                    if (!pFont) {
                        pFamily->GetFirstMatchingFont(targetWeight, targetStretch, targetStyle, &pFont);
                    }
                }
            }
        }

        if (pFont && sampleGid > 0 && sampleChar > 0 && pFamily) {
            IDWriteFontFace* pFace = nullptr;
            if (SUCCEEDED(pFont->CreateFontFace(&pFace)) && pFace) {
                UINT16 gid = 0;
                pFace->GetGlyphIndices(&sampleChar, 1, &gid);
                pFace->Release();
                if (gid != sampleGid) {
                    bool matched = false;
                    UINT32 fc = pFamily->GetFontCount();
                    for (UINT32 j = 0; j < fc; ++j) {
                        IDWriteFont* pF = nullptr;
                        if (SUCCEEDED(pFamily->GetFont(j, &pF)) && pF) {
                            IDWriteFontFace* pCandFace = nullptr;
                            if (SUCCEEDED(pF->CreateFontFace(&pCandFace)) && pCandFace) {
                                UINT16 candGid = 0;
                                pCandFace->GetGlyphIndices(&sampleChar, 1, &candGid);
                                pCandFace->Release();
                                if (candGid == sampleGid) {
                                    pFont->Release();
                                    pFont = pF;
                                    chosenFontIndex = j;
                                    matched = true;
                                    break;
                                }
                            }
                            pF->Release();
                        }
                    }

                    if (!matched) {
                        std::vector<std::pair<UINT32, UINT32>> crossCandidates;
                        std::wstring stripped = StripFontPrefixesAndSuffixes(fontName);
                        std::wstring keys[] = {
                            NormalizeFontKey(fontName),
                            NormalizeFontKey(stripped),
                            NormalizeFontKey(postscriptName)
                        };
                        for (const auto& k : keys) {
                            if (k.empty()) continue;
                            auto it1 = m_typoFamilyFontMap.find(k);
                            if (it1 != m_typoFamilyFontMap.end()) {
                                crossCandidates.insert(crossCandidates.end(), it1->second.begin(), it1->second.end());
                            }
                            auto it2 = m_wwsFamilyFontMap.find(k);
                            if (it2 != m_wwsFamilyFontMap.end()) {
                                crossCandidates.insert(crossCandidates.end(), it2->second.begin(), it2->second.end());
                            }
                        }

                        std::vector<std::wstring> tokens;
                        std::wstring curTok;
                        for (wchar_t ch : fontName) {
                            if (iswalnum(ch)) curTok += (wchar_t)towlower(ch);
                            else if (!curTok.empty()) { tokens.push_back(curTok); curTok.clear(); }
                        }
                        if (!curTok.empty()) tokens.push_back(curTok);

                        UINT32 famCount = m_pFontCollection->GetFontFamilyCount();
                        for (UINT32 fi = 0; fi < famCount; ++fi) {
                            if (fi == familyIndex) continue;
                            IDWriteFontFamily* pOtherFam = nullptr;
                            if (FAILED(m_pFontCollection->GetFontFamily(fi, &pOtherFam)) || !pOtherFam) continue;

                            IDWriteLocalizedStrings* pOtherNames = nullptr;
                            std::wstring otherFamName;
                            if (SUCCEEDED(pOtherFam->GetFamilyNames(&pOtherNames)) && pOtherNames) {
                                wchar_t b[256];
                                if (SUCCEEDED(pOtherNames->GetString(0, b, 256))) otherFamName = b;
                                pOtherNames->Release();
                            }
                            std::wstring lowerOther = ToLower(otherFamName);

                            bool tokenMatch = false;
                            for (const auto& tok : tokens) {
                                if (tok.size() >= 3 && tok != L"font" && tok != L"sans" && tok != L"serif" && tok != L"arabic") {
                                    if (lowerOther.find(tok) != std::wstring::npos) {
                                        tokenMatch = true;
                                        break;
                                    }
                                }
                            }

                            if (tokenMatch) {
                                UINT32 ofc = pOtherFam->GetFontCount();
                                for (UINT32 oj = 0; oj < ofc; ++oj) {
                                    crossCandidates.push_back({ fi, oj });
                                }
                            }
                            pOtherFam->Release();
                        }

                        if (!crossCandidates.empty()) {
                            IDWriteFont* matchedCrossFont = evaluateCandidates(crossCandidates);
                            if (matchedCrossFont) {
                                pFont->Release();
                                pFont = matchedCrossFont;
                                matched = true;
                            }
                        }

                        if (!matched) {
                            for (UINT32 fi = 0; fi < famCount && !matched; ++fi) {
                                IDWriteFontFamily* pOtherFam = nullptr;
                                if (FAILED(m_pFontCollection->GetFontFamily(fi, &pOtherFam)) || !pOtherFam) continue;
                                UINT32 ofc = pOtherFam->GetFontCount();
                                for (UINT32 oj = 0; oj < ofc; ++oj) {
                                    IDWriteFont* pF = nullptr;
                                    if (SUCCEEDED(pOtherFam->GetFont(oj, &pF)) && pF) {
                                        IDWriteFontFace* pFace = nullptr;
                                        if (SUCCEEDED(pF->CreateFontFace(&pFace)) && pFace) {
                                            UINT16 gid = 0;
                                            pFace->GetGlyphIndices(&sampleChar, 1, &gid);
                                            pFace->Release();
                                            if (gid == sampleGid) {
                                                pFont->Release();
                                                pFont = pF;
                                                familyIndex = fi;
                                                chosenFontIndex = oj;
                                                if (pFamily) pFamily->Release();
                                                pFamily = pOtherFam;
                                                pOtherFam = nullptr;
                                                matched = true;
                                                break;
                                            }
                                        }
                                        pF->Release();
                                    }
                                }
                                if (pOtherFam) pOtherFam->Release();
                            }
                        }
                    }
                }
            }
        }

        if (!pFont && sampleGid > 0 && sampleChar > 0) {
            std::vector<std::pair<UINT32, UINT32>> crossCandidates;
            std::vector<std::wstring> tokens;
            std::wstring curTok;
            for (wchar_t ch : fontName) {
                if (iswalnum(ch)) curTok += (wchar_t)towlower(ch);
                else if (!curTok.empty()) { tokens.push_back(curTok); curTok.clear(); }
            }
            if (!curTok.empty()) tokens.push_back(curTok);

            UINT32 famCount = m_pFontCollection->GetFontFamilyCount();
            for (UINT32 fi = 0; fi < famCount; ++fi) {
                IDWriteFontFamily* pOtherFam = nullptr;
                if (FAILED(m_pFontCollection->GetFontFamily(fi, &pOtherFam)) || !pOtherFam) continue;

                IDWriteLocalizedStrings* pOtherNames = nullptr;
                std::wstring otherFamName;
                if (SUCCEEDED(pOtherFam->GetFamilyNames(&pOtherNames)) && pOtherNames) {
                    wchar_t b[256];
                    if (SUCCEEDED(pOtherNames->GetString(0, b, 256))) otherFamName = b;
                    pOtherNames->Release();
                }
                std::wstring lowerOther = ToLower(otherFamName);

                bool tokenMatch = false;
                for (const auto& tok : tokens) {
                    if (tok.size() >= 3 && tok != L"font" && tok != L"sans" && tok != L"serif" && tok != L"arabic") {
                        if (lowerOther.find(tok) != std::wstring::npos) {
                            tokenMatch = true;
                            break;
                        }
                    }
                }

                if (tokenMatch) {
                    UINT32 ofc = pOtherFam->GetFontCount();
                    for (UINT32 oj = 0; oj < ofc; ++oj) {
                        crossCandidates.push_back({ fi, oj });
                    }
                }
                pOtherFam->Release();
            }

            if (!crossCandidates.empty()) {
                pFont = evaluateCandidates(crossCandidates);
            }

            if (!pFont) {
                for (UINT32 fi = 0; fi < famCount && !pFont; ++fi) {
                    IDWriteFontFamily* pOtherFam = nullptr;
                    if (FAILED(m_pFontCollection->GetFontFamily(fi, &pOtherFam)) || !pOtherFam) continue;
                    UINT32 ofc = pOtherFam->GetFontCount();
                    for (UINT32 oj = 0; oj < ofc; ++oj) {
                        IDWriteFont* pF = nullptr;
                        if (SUCCEEDED(pOtherFam->GetFont(oj, &pF)) && pF) {
                            IDWriteFontFace* pFace = nullptr;
                            if (SUCCEEDED(pF->CreateFontFace(&pFace)) && pFace) {
                                UINT16 gid = 0;
                                pFace->GetGlyphIndices(&sampleChar, 1, &gid);
                                pFace->Release();
                                if (gid == sampleGid) {
                                    pFont = pF;
                                    familyIndex = fi;
                                    chosenFontIndex = oj;
                                    pFamily = pOtherFam;
                                    pOtherFam = nullptr;
                                    break;
                                }
                            }
                            pF->Release();
                        }
                    }
                    if (pOtherFam) pOtherFam->Release();
                }
            }
        }

        if (pFont) {
            m_familyIndexCache[cacheKey] = familyIndex;
            m_fontIndexCache[cacheKey] = chosenFontIndex;
        } else {
            if (pFamily) pFamily->Release();
            return false;
        }
    }

    if (pFamily) pFamily->Release();
    if (!pFont) return false;

    IDWriteFontFace* pFontFace = nullptr;
    HRESULT hr = pFont->CreateFontFace(&pFontFace);
    pFont->Release();
    if (FAILED(hr) || !pFontFace) return false;

    if (!axisValues.empty()) {
        IDWriteFontFace5* pFace5 = nullptr;
        if (SUCCEEDED(pFontFace->QueryInterface(__uuidof(IDWriteFontFace5), (void**)&pFace5)) && pFace5) {
            IDWriteFontResource* pResource = nullptr;
            if (SUCCEEDED(pFace5->GetFontResource(&pResource)) && pResource) {
                if (pResource->HasVariations()) {
                    IDWriteFontFace5* pVarFace = nullptr;
                    if (SUCCEEDED(pResource->CreateFontFace(
                        DWRITE_FONT_SIMULATIONS_NONE,
                        axisValues.data(),
                        (UINT32)axisValues.size(),
                        &pVarFace)) && pVarFace) {
                        pFontFace->Release();
                        pFontFace = pVarFace;
                    }
                }
                pResource->Release();
            }
            pFace5->Release();
        }
    }

    std::wstring normalizedText;
    normalizedText.reserve(text.size());
    for (wchar_t c : text) {
        if (c >= 0xFB50 && c <= 0xFEFF) {
            normalizedText.push_back(ArabicShaper::ToNominalChar(c));
        } else {
            normalizedText.push_back(c);
        }
    }

    TextAnalysisSource source(normalizedText);
    TextAnalysisSink sink;
    m_pAnalyzer->AnalyzeScript(&source, 0, (UINT32)normalizedText.size(), &sink);
    m_pAnalyzer->AnalyzeBidi(&source, 0, (UINT32)normalizedText.size(), &sink);

    auto unifiedRuns = BuildUnifiedRuns((UINT32)normalizedText.size(), sink.scriptRuns, sink.bidiRuns);
    if (unifiedRuns.empty()) {
        pFontFace->Release();
        return false;
    }

    const float effectiveFontSize = fontSize > 0.0f ? fontSize : 16.0f;
    DWRITE_FONT_METRICS dwriteFontMetrics{};
    pFontFace->GetMetrics(&dwriteFontMetrics);
    const float designToDip = (dwriteFontMetrics.designUnitsPerEm > 0)
        ? (effectiveFontSize / (float)dwriteFontMetrics.designUnitsPerEm)
        : (effectiveFontSize / 1000.0f);
    bool fontHasGPOS = CheckFontHasGPOS(pFontFace);

    outRun.glyphsByChar.reserve(normalizedText.size());
    outRun.glyphs.reserve(normalizedText.size() * 2);
    outRun.clusters.reserve(normalizedText.size());

    size_t nonZeroArabicCount = 0;
    size_t missingGlyphCount = 0;

    for (const auto& run : unifiedRuns) {
        const UINT32 runLen = run.textLength;
        const UINT32 maxGlyphs = runLen * 3 + 16;
        const wchar_t* runText = normalizedText.c_str() + run.textPosition;
        const wchar_t* runLocale = (run.scriptAnalysis.script == 3 || run.isRTL) ? L"ar-SA" : L"en-US";

        std::vector<UINT16> runClusterMap(runLen);
        std::vector<DWRITE_SHAPING_TEXT_PROPERTIES> runTextProps(runLen);
        std::vector<UINT16> runGlyphIndices(maxGlyphs);
        std::vector<DWRITE_SHAPING_GLYPH_PROPERTIES> runGlyphProps(maxGlyphs);
        UINT32 runActualGlyphCount = 0;

        std::vector<DWRITE_FONT_FEATURE> effectiveFeatures = fontFeatures;

        bool hasRlig = false;
        for (auto& f : effectiveFeatures) {
            if (f.nameTag == DWRITE_FONT_FEATURE_TAG_REQUIRED_LIGATURES || f.nameTag == 0x67696c72U) {
                f.parameter = 1; 
                hasRlig = true;
            }
        }
        if (!hasRlig && !effectiveFeatures.empty()) {
            DWRITE_FONT_FEATURE rligFeat{};
            rligFeat.nameTag = DWRITE_FONT_FEATURE_TAG_REQUIRED_LIGATURES;
            rligFeat.parameter = 1;
            effectiveFeatures.push_back(rligFeat);
        }

        for (auto& f : effectiveFeatures) {
            if (f.nameTag == DWRITE_FONT_FEATURE_TAG_CURSIVE_POSITIONING || f.nameTag == 0x73727563U) {
                f.parameter = 1; 
            }
        }

        DWRITE_TYPOGRAPHIC_FEATURES typoFeatures{};
        const DWRITE_TYPOGRAPHIC_FEATURES* pTypoFeatures = nullptr;
        const DWRITE_TYPOGRAPHIC_FEATURES** ppTypoFeatures = nullptr;
        UINT32 featureRangeLength = (UINT32)runLen;
        const UINT32* pFeatureRangeLengths = nullptr;
        UINT32 featureRangeCount = 0;

        if (!effectiveFeatures.empty()) {
            typoFeatures.features = effectiveFeatures.data();
            typoFeatures.featureCount = (UINT32)effectiveFeatures.size();
            pTypoFeatures = &typoFeatures;
            ppTypoFeatures = &pTypoFeatures;
            pFeatureRangeLengths = &featureRangeLength;
            featureRangeCount = 1;
        }

        hr = m_pAnalyzer->GetGlyphs(
            runText,
            runLen,
            pFontFace,
            FALSE,
            run.isRTL ? TRUE : FALSE,
            &run.scriptAnalysis,
            runLocale,
            nullptr,
            ppTypoFeatures,
            pFeatureRangeLengths,
            featureRangeCount,
            maxGlyphs,
            runClusterMap.data(),
            runTextProps.data(),
            runGlyphIndices.data(),
            runGlyphProps.data(),
            &runActualGlyphCount
        );

        if (FAILED(hr) || runActualGlyphCount == 0) {
            for (size_t c = 0; c < runLen; ++c) {
                ShapedGlyphResult r{};
                r.clusterIndex = (uint16_t)outRun.glyphs.size();
                r.glyphId = 0;
                r.isZeroWidth = true;
                wchar_t ch = normalizedText[run.textPosition + c];
                if (ch >= 0x0600 && ch <= 0x06FF) {
                    nonZeroArabicCount++;
                    missingGlyphCount++;
                }
                outRun.glyphsByChar.push_back(r);
            }
            continue;
        }

        // Prefix mark adjustment
        std::vector<bool> isSyntheticPrefixMark(runActualGlyphCount, false);
        if (run.scriptAnalysis.script == 3 || run.isRTL) {
            for (size_t c = 0; c < runLen; ++c) {
                UINT16 gStart = runClusterMap[c];
                if (c > 0 && gStart == runClusterMap[c - 1]) continue;
                UINT16 gEnd = (UINT16)runActualGlyphCount;
                for (size_t nextC = c + 1; nextC < runLen; ++nextC) {
                    if (runClusterMap[nextC] != gStart) {
                        gEnd = runClusterMap[nextC];
                        break;
                    }
                }
                if (gEnd > runActualGlyphCount) gEnd = (UINT16)runActualGlyphCount;

                UINT16 firstBase = gEnd;
                for (UINT16 g = gStart; g < gEnd; ++g) {
                    if (!runGlyphProps[g].isDiacritic) {
                        firstBase = g;
                        break;
                    }
                }
                if (firstBase > gStart && firstBase < gEnd) {
                    uint16_t* rawProps = reinterpret_cast<uint16_t*>(runGlyphProps.data());
                    rawProps[firstBase] = (rawProps[gStart] & 0xFF90) | (rawProps[firstBase] & 0x006F) | 0x0010;
                    for (UINT16 g = gStart; g < firstBase; ++g) {
                        isSyntheticPrefixMark[g] = true;
                    }
                }
            }
        }

        std::vector<FLOAT> runGlyphAdvances(runActualGlyphCount);
        std::vector<DWRITE_GLYPH_OFFSET> runGlyphOffsets(runActualGlyphCount);
        std::vector<DWRITE_GLYPH_METRICS> runDesignGlyphMetrics(runActualGlyphCount);
        HRESULT hrDesignMetrics = pFontFace->GetDesignGlyphMetrics(
            runGlyphIndices.data(),
            runActualGlyphCount,
            runDesignGlyphMetrics.data(),
            FALSE
        );

        hr = m_pAnalyzer->GetGlyphPlacements(
            runText,
            runClusterMap.data(),
            runTextProps.data(),
            runLen,
            runGlyphIndices.data(),
            runGlyphProps.data(),
            runActualGlyphCount,
            pFontFace,
            effectiveFontSize,
            FALSE,
            run.isRTL ? TRUE : FALSE,
            &run.scriptAnalysis,
            runLocale,
            ppTypoFeatures,
            pFeatureRangeLengths,
            featureRangeCount,
            runGlyphAdvances.data(),
            runGlyphOffsets.data()
        );

        std::vector<bool> glyphIsDiacritic(runActualGlyphCount, false);
        for (UINT32 g = 0; g < runActualGlyphCount; ++g) {
            if (runGlyphProps[g].isDiacritic != 0) {
                glyphIsDiacritic[g] = true;
            }
        }
        for (size_t c = 0; c < runLen; ++c) {
            wchar_t ch = runText[c];
            if (ArabicShaper::IsTashkeel(ch) || (ch >= 0x06D6 && ch <= 0x06ED)) {
                UINT16 gStart = (c < runClusterMap.size()) ? runClusterMap[c] : (UINT16)c;
                UINT16 gEnd = (UINT16)runActualGlyphCount;
                for (size_t nextC = c + 1; nextC < runLen; ++nextC) {
                    if (runClusterMap[nextC] != gStart) {
                        gEnd = runClusterMap[nextC];
                        break;
                    }
                }
                if (gEnd > runActualGlyphCount) gEnd = (UINT16)runActualGlyphCount;
                for (UINT16 g = gStart; g < gEnd; ++g) {
                    if (runGlyphAdvances[g] == 0.0f) {
                        glyphIsDiacritic[g] = true;
                    }
                }
            }
        }

        if (SUCCEEDED(hrDesignMetrics) && dwriteFontMetrics.designUnitsPerEm > 0) {
            const int gap = (int)(dwriteFontMetrics.designUnitsPerEm * 0.04f);
            size_t lastBaseIdx = 0;
            bool hasBase = false;
            int currentAboveTop = 0;
            int currentBelowBottom = 0;

            for (UINT32 g = 0; g < runActualGlyphCount; ++g) {
                bool isMark = glyphIsDiacritic[g];
                if (!isMark) {
                    lastBaseIdx = g;
                    hasBase = true;
                    const auto& baseGm = runDesignGlyphMetrics[lastBaseIdx];
                    int baseInkH = (int)baseGm.advanceHeight - baseGm.topSideBearing - baseGm.bottomSideBearing;
                    int baseYMax = (int)dwriteFontMetrics.ascent - baseGm.topSideBearing;
                    int baseYMin = baseYMax - baseInkH;
                    currentAboveTop = baseYMax;
                    currentBelowBottom = baseYMin;
                } else if (hasBase) {
                    const float compHmtxAdv = (float)runDesignGlyphMetrics[g].advanceWidth * designToDip;
                    const bool isUnpositioned = !fontHasGPOS ||
                        (runGlyphOffsets[g].ascenderOffset == 0.0f &&
                         (runGlyphOffsets[g].advanceOffset == 0.0f ||
                          fabs(runGlyphOffsets[g].advanceOffset - (-compHmtxAdv)) < 0.001f));

                    if (isUnpositioned) {
                        const auto& baseGm = runDesignGlyphMetrics[lastBaseIdx];
                        const auto& markGm = runDesignGlyphMetrics[g];

                        double baseXCenter = (double)baseGm.advanceWidth / 2.0;
                        int markInkW = (int)markGm.advanceWidth - markGm.leftSideBearing - markGm.rightSideBearing;
                        double markXCenter = (markInkW > 0)
                            ? (markGm.leftSideBearing + markInkW / 2.0)
                            : ((markGm.advanceWidth > 0) ? (markGm.advanceWidth / 2.0) : 0.0);

                        double deltaX = (baseXCenter - markXCenter) * designToDip;
                        runGlyphOffsets[g].advanceOffset = (FLOAT)(-deltaX);

                        int markInkH = (int)markGm.advanceHeight - markGm.topSideBearing - markGm.bottomSideBearing;
                        int markYMax = (int)dwriteFontMetrics.ascent - markGm.topSideBearing;
                        int markYMin = markYMax - markInkH;

                        if (markYMin > 0) {
                            int targetYMin = currentAboveTop + gap;
                            if (targetYMin > markYMin) {
                                double deltaY = (targetYMin - markYMin) * designToDip;
                                runGlyphOffsets[g].ascenderOffset = (FLOAT)(deltaY);
                                currentAboveTop = markYMax + (targetYMin - markYMin);
                            } else {
                                currentAboveTop = (std::max)(currentAboveTop, markYMax);
                            }
                        } else {
                            int targetYMax = currentBelowBottom - gap;
                            if (targetYMax < markYMax) {
                                double deltaY = (targetYMax - markYMax) * designToDip;
                                runGlyphOffsets[g].ascenderOffset = (FLOAT)(deltaY);
                                currentBelowBottom = markYMin + (targetYMax - markYMax);
                            } else {
                                currentBelowBottom = (std::min)(currentBelowBottom, markYMin);
                            }
                        }
                    }
                }
            }
        }

        UINT16 runGlyphStartInGlobal = (UINT16)outRun.glyphs.size();
        std::vector<UINT16> oldToNewGlyph(runActualGlyphCount + 1, 0);

        for (UINT16 g = 0; g < runActualGlyphCount; ++g) {
            oldToNewGlyph[g] = (UINT16)(outRun.glyphs.size() - runGlyphStartInGlobal);
            bool isMark = glyphIsDiacritic[g];
            bool isEmptyControlMark = false;
            if (SUCCEEDED(hrDesignMetrics) && isMark && runGlyphAdvances[g] == 0.0f) {
                int32_t inkW = (int32_t)runDesignGlyphMetrics[g].advanceWidth -
                               runDesignGlyphMetrics[g].leftSideBearing -
                               runDesignGlyphMetrics[g].rightSideBearing;
                int32_t inkH = (int32_t)runDesignGlyphMetrics[g].advanceHeight -
                               runDesignGlyphMetrics[g].topSideBearing -
                               runDesignGlyphMetrics[g].bottomSideBearing;
                if (inkW <= 0 && inkH <= 0) {
                    isEmptyControlMark = true;
                }
            }
            if ((isSyntheticPrefixMark[g] || isEmptyControlMark) && runGlyphAdvances[g] == 0.0f) {
                continue;
            }
            float compHmtxAdv = SUCCEEDED(hrDesignMetrics)
                ? ((float)runDesignGlyphMetrics[g].advanceWidth * designToDip)
                : runGlyphAdvances[g];
            outRun.glyphs.push_back({
                runGlyphIndices[g],
                runGlyphAdvances[g],
                compHmtxAdv,
                runGlyphOffsets[g].advanceOffset,
                runGlyphOffsets[g].ascenderOffset,
                isMark
            });
        }
        oldToNewGlyph[runActualGlyphCount] = (UINT16)(outRun.glyphs.size() - runGlyphStartInGlobal);

        for (size_t c = 0; c < runLen; ++c) {
            UINT16 gStart = (c < runClusterMap.size()) ? runClusterMap[c] : (UINT16)c;
            bool isContinuation = (c > 0 && gStart == runClusterMap[c - 1]);

            UINT16 gEnd = (UINT16)runActualGlyphCount;
            size_t charEnd = runLen;
            if (!isContinuation) {
                for (size_t nextC = c + 1; nextC < runLen; ++nextC) {
                    if (runClusterMap[nextC] != gStart) {
                        gEnd = runClusterMap[nextC];
                        charEnd = nextC;
                        break;
                    }
                }
                if (gEnd > runActualGlyphCount) gEnd = (UINT16)runActualGlyphCount;
                UINT16 mappedStart = runGlyphStartInGlobal + oldToNewGlyph[gStart];
                UINT16 mappedEnd = runGlyphStartInGlobal + oldToNewGlyph[gEnd];
                if (mappedStart < mappedEnd) {
                    outRun.clusters.push_back({
                        (uint16_t)(run.textPosition + c),
                        (uint16_t)(charEnd - c),
                        mappedStart,
                        (uint16_t)(mappedEnd - mappedStart)
                    });
                }
            }

            ShapedGlyphResult r{};
            r.clusterIndex = (gStart <= runActualGlyphCount)
                ? (runGlyphStartInGlobal + oldToNewGlyph[gStart])
                : (runGlyphStartInGlobal + gStart);

            wchar_t ch = normalizedText[run.textPosition + c];

            if (ch == L'\r' || ch == L'\n') {
                r.glyphId = 0;
                r.advance = 0.0f;
                r.hmtxAdvance = 0.0f;
                r.offsetX = 0.0f;
                r.offsetY = 0.0f;
                r.isZeroWidth = true;
            } else if (isContinuation) {
                r.glyphId = 0xFFFF;
                r.advance = 0.0f;
                r.hmtxAdvance = 0.0f;
                r.offsetX = 0.0f;
                r.offsetY = 0.0f;
                r.isZeroWidth = true;
            } else {
                UINT16 chosenGlyph = gStart;
                float totalAdv = 0.0f;
                bool foundBase = false;
                for (UINT16 g = gStart; g < gEnd && g < runActualGlyphCount; ++g) {
                    totalAdv += runGlyphAdvances[g];
                    if (!glyphIsDiacritic[g]) {
                        if (!foundBase || runGlyphAdvances[g] > 0.0f) {
                            chosenGlyph = g;
                            foundBase = true;
                        }
                    }
                }

                if (chosenGlyph < runActualGlyphCount) {
                    r.glyphId = runGlyphIndices[chosenGlyph];
                    r.advance = totalAdv;
                    r.hmtxAdvance = SUCCEEDED(hrDesignMetrics)
                        ? ((float)runDesignGlyphMetrics[chosenGlyph].advanceWidth * designToDip)
                        : totalAdv;
                    r.offsetX = runGlyphOffsets[chosenGlyph].advanceOffset;
                    r.offsetY = runGlyphOffsets[chosenGlyph].ascenderOffset;
                    r.isZeroWidth = isContinuation || (foundBase ? false : (totalAdv <= 0.0001f));
                } else {
                    r.glyphId = 0;
                    r.advance = 0.0f;
                    r.hmtxAdvance = 0.0f;
                    r.offsetX = 0.0f;
                    r.offsetY = 0.0f;
                    r.isZeroWidth = true;
                }
            }

            if (ch >= 0x0600 && ch <= 0x06FF) {
                nonZeroArabicCount++;
                if (r.glyphId == 0) {
                    missingGlyphCount++;
                }
            }

            outRun.glyphsByChar.push_back(r);
        }
    }

    auto hasExtendedKeyword = [](const std::wstring& s) {
        std::wstring low = s;
        for (wchar_t& c : low) c = (wchar_t)towlower(c);
        return low.find(L"extend") != std::wstring::npos;
    };
    auto isIdrisFont = [](const std::wstring& s) {
        std::wstring low = s;
        for (wchar_t& c : low) c = (wchar_t)towlower(c);
        return low.find(L"idris") != std::wstring::npos;
    };

    if (axisValues.empty() && (isIdrisFont(fontName) || isIdrisFont(postscriptName))) {
        if (hasExtendedKeyword(postscriptName) || hasExtendedKeyword(fontName)) {
            auto getElongatedGid = [](uint16_t gid) -> uint16_t {
                switch (gid) {
                case 619: return 1659; // uni0633 -> uni0633.elongated
                case 621: return 1661; // uni0633.fina -> uni0633.fina.elongated
                case 622: return 1660; // uni0633.fina.PostBehInit -> uni0633.fina.PostBehInit.elongated
                case 653: return 1662; // uni0635 -> uni0635.elongated
                case 655: return 1663; // uni0635.fina -> uni0635.fina.elongated
                case 735: return 1671; // uni0643.fina -> uni0643.fina.elongated
                case 736: return 1669; // uni0643.fina.PostLam -> uni0643.fina.PostLam.elongated
                case 737: return 1670; // uni0643.fina.PostMeem -> uni0643.fina.PostMeem.elongated
                case 747: return 1673; // uni0643.init -> uni0643.init.elongated
                case 738: return 1672; // uni0643.medi -> uni0643.medi.elongated
                case 764: return 1676; // uni0644 -> uni0644.elongated
                case 765: return 1680; // uni0644.fina -> uni0644.fina.elongated
                case 766: return 1677; // uni0644.fina.Cup -> uni0644.fina.Cup.elongated
                case 767: return 1678; // uni0644.fina.PostLam -> uni0644.fina.PostLam.elongated
                case 768: return 1679; // uni0644.fina.PostMeem -> uni0644.fina.PostMeem.elongated
                case 885: return 1684; // uni0649 -> uni0649.elongated
                case 887: return 1687; // uni0649.fina -> uni0649.fina.elongated
                case 888: return 1685; // uni0649.fina.PostBehOrSeenOrSad -> uni0649.fina.PostBehOrSeenOrSad.elongated
                case 889: return 1686; // uni0649.fina.PostLamOrKafOrFeh -> uni0649.fina.PostLamOrKafOrFeh.elongated
                case 484: return 1656; // uni066E -> uni066E.elongated
                case 485: return 1658; // uni066E.fina -> uni066E.fina.elongated
                case 486: return 1657; // uni066E.fina.PostBeh -> uni066E.fina.PostBeh.elongated
                case 729: return 1666; // uni066F -> uni066F.elongated
                case 730: return 1667; // uni066F.fina -> uni066F.fina.elongated
                case 714: return 1664; // uni06A1 -> uni06A1.elongated
                case 715: return 1665; // uni06A1.fina -> uni06A1.fina.elongated
                case 757: return 1674; // uni06A9 -> uni06A9.elongated
                case 758: return 1675; // uni06A9.fina -> uni06A9.fina.elongated
                case 846: return 1681; // uni06BA -> uni06BA.elongated
                case 847: return 1683; // uni06BA.fina -> uni06BA.fina.elongated
                case 848: return 1682; // uni06BA.fina.Post -> uni06BA.fina.Post.elongated
                default: return 0;
                }
            };

            for (auto& g : outRun.glyphs) {
                uint16_t elongGid = getElongatedGid(g.glyphId);
                if (elongGid != 0) {
                    g.glyphId = elongGid;
                    DWRITE_GLYPH_METRICS gm{};
                    if (SUCCEEDED(pFontFace->GetDesignGlyphMetrics(&elongGid, 1, &gm))) {
                        float advDip = (float)gm.advanceWidth * designToDip;
                        g.advance = advDip;
                        g.hmtxAdvance = advDip;
                    }
                }
            }

            for (auto& r : outRun.glyphsByChar) {
                uint16_t elongGid = getElongatedGid(r.glyphId);
                if (elongGid != 0) {
                    r.glyphId = elongGid;
                    DWRITE_GLYPH_METRICS gm{};
                    if (SUCCEEDED(pFontFace->GetDesignGlyphMetrics(&elongGid, 1, &gm))) {
                        float advDip = (float)gm.advanceWidth * designToDip;
                        r.advance = advDip;
                        r.hmtxAdvance = advDip;
                    }
                }
            }
        }
    }

    pFontFace->Release();

    if (nonZeroArabicCount > 0 && missingGlyphCount == nonZeroArabicCount) {
        outRun = {};
        return false;
    }

    return true;
}

bool DWriteShaper::ShapeText(
    const std::wstring& text,
    const std::wstring& fontName,
    float fontSize,
    std::vector<ShapedGlyphResult>& outGlyphs,
    const std::wstring& postscriptName,
    uint32_t sampleChar,
    uint16_t sampleGid,
    const std::vector<DWRITE_FONT_AXIS_VALUE>& axisValues,
    const std::vector<DWRITE_FONT_FEATURE>& fontFeatures
) {
    ShapedTextRun run;
    if (!ShapeTextDetailed(text, fontName, fontSize, run, postscriptName, sampleChar, sampleGid, axisValues, fontFeatures)) {
        outGlyphs.clear();
        return false;
    }
    outGlyphs = std::move(run.glyphsByChar);
    return true;
}

bool DWriteShaper::ShapeTextWithFallback(
    const std::wstring& text,
    const std::wstring& fontName,
    float fontSize,
    std::vector<ShapedGlyphResult>& outGlyphs,
    const std::wstring& postscriptName,
    uint32_t sampleChar,
    uint16_t sampleGid,
    const std::vector<DWRITE_FONT_AXIS_VALUE>& axisValues,
    const std::vector<DWRITE_FONT_FEATURE>& fontFeatures
) {
    return ShapeText(text, fontName, fontSize, outGlyphs, postscriptName, sampleChar, sampleGid, axisValues, fontFeatures);
}

bool DWriteShaper::ShapeTextWithFallbackDetailed(
    const std::wstring& text,
    const std::wstring& fontName,
    float fontSize,
    ShapedTextRun& outRun,
    const std::wstring& postscriptName,
    uint32_t sampleChar,
    uint16_t sampleGid,
    const std::vector<DWRITE_FONT_AXIS_VALUE>& axisValues,
    const std::vector<DWRITE_FONT_FEATURE>& fontFeatures
) {
    return ShapeTextDetailed(text, fontName, fontSize, outRun, postscriptName, sampleChar, sampleGid, axisValues, fontFeatures);
}

}