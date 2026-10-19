#pragma once
#include <windows.h>
#include <dwrite.h>
#include <dwrite_3.h>
#include <vector>
#include <string>
#include <unordered_map>

namespace AffinityArabic {

struct ShapedGlyphResult {
    uint16_t glyphId;
    float advance;
    float hmtxAdvance;
    float offsetX;
    float offsetY;
    uint16_t clusterIndex;
    bool isZeroWidth;
};

struct ShapedGlyphComponent {
    uint16_t glyphId;
    float advance;
    float hmtxAdvance;
    float offsetX;
    float offsetY;
    bool isDiacritic;
};

struct ShapedGlyphCluster {
    uint16_t firstChar;
    uint16_t charCount;
    uint16_t firstGlyph;
    uint16_t glyphCount;
};

struct ShapedTextRun {
    std::vector<ShapedGlyphResult> glyphsByChar;
    std::vector<ShapedGlyphComponent> glyphs;
    std::vector<ShapedGlyphCluster> clusters;
};

class DWriteShaper {
public:
    static DWriteShaper& Instance();

    bool Initialize();

    bool ShapeText(
        const std::wstring& text,
        const std::wstring& fontName,
        float fontSize,
        std::vector<ShapedGlyphResult>& outGlyphs,
        const std::wstring& postscriptName = L"",
        uint32_t sampleChar = 0,
        uint16_t sampleGid = 0,
        const std::vector<DWRITE_FONT_AXIS_VALUE>& axisValues = {},
        const std::vector<DWRITE_FONT_FEATURE>& fontFeatures = {}
    );

    bool ShapeTextDetailed(
        const std::wstring& text,
        const std::wstring& fontName,
        float fontSize,
        ShapedTextRun& outRun,
        const std::wstring& postscriptName = L"",
        uint32_t sampleChar = 0,
        uint16_t sampleGid = 0,
        const std::vector<DWRITE_FONT_AXIS_VALUE>& axisValues = {},
        const std::vector<DWRITE_FONT_FEATURE>& fontFeatures = {}
    );

    bool ShapeTextWithFallback(
        const std::wstring& text,
        const std::wstring& fontName,
        float fontSize,
        std::vector<ShapedGlyphResult>& outGlyphs,
        const std::wstring& postscriptName = L"",
        uint32_t sampleChar = 0,
        uint16_t sampleGid = 0,
        const std::vector<DWRITE_FONT_AXIS_VALUE>& axisValues = {},
        const std::vector<DWRITE_FONT_FEATURE>& fontFeatures = {}
    );

    bool ShapeTextWithFallbackDetailed(
        const std::wstring& text,
        const std::wstring& fontName,
        float fontSize,
        ShapedTextRun& outRun,
        const std::wstring& postscriptName = L"",
        uint32_t sampleChar = 0,
        uint16_t sampleGid = 0,
        const std::vector<DWRITE_FONT_AXIS_VALUE>& axisValues = {},
        const std::vector<DWRITE_FONT_FEATURE>& fontFeatures = {}
    );

private:
    DWriteShaper();
    ~DWriteShaper();

    void EnsureFontMapsIndexed();
    bool FindFontFamilySmart(const std::wstring& fontName, UINT32* outFamilyIndex);

    IDWriteFactory* m_pFactory = nullptr;
    IDWriteTextAnalyzer* m_pAnalyzer = nullptr;
    IDWriteFontCollection* m_pFontCollection = nullptr;
    bool m_initialized = false;
    bool m_mapsIndexed = false;
    std::unordered_map<std::wstring, std::vector<std::pair<UINT32, UINT32>>> m_psFontMap;
    std::unordered_map<std::wstring, std::vector<std::pair<UINT32, UINT32>>> m_canonicalPsFontMap;
    std::unordered_map<std::wstring, std::vector<std::pair<UINT32, UINT32>>> m_fullNameFontMap;
    std::unordered_map<std::wstring, std::vector<std::pair<UINT32, UINT32>>> m_typoFamilyFontMap;
    std::unordered_map<std::wstring, std::vector<std::pair<UINT32, UINT32>>> m_wwsFamilyFontMap;
    std::unordered_map<std::wstring, UINT32> m_familyNameMap;
    std::unordered_map<std::wstring, UINT32> m_asciiFoldedFamilyMap;
    std::vector<std::pair<std::wstring, UINT32>> m_asciiFoldedFamilyList;
    std::unordered_map<std::wstring, UINT32> m_familyIndexCache;
    std::unordered_map<std::wstring, UINT32> m_fontIndexCache;
};

}
