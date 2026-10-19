#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace AffinityArabic {

enum class JoiningType {
    NonJoining,
    RightJoining,
    LeftJoining,
    DualJoining,
    JoinCausing,
    Transparent
};

struct ArabicGlyphForms {
    wchar_t isolated;
    wchar_t initial;
    wchar_t medial;
    wchar_t final_form;
};

class ArabicShaper {
public:
    static bool IsArabicChar(wchar_t c);
    static bool NeedsShaping(wchar_t c);
    static bool NeedsShaping(const std::wstring& text);
    static bool IsTashkeel(wchar_t c);
    static bool ContainsArabic(const std::wstring& text);
    static bool IsPresentationForm(wchar_t c);
    static wchar_t ToNominalChar(wchar_t c);
    static bool IsLamAlefPresentation(wchar_t c);
    static std::wstring NormalizeToNominal(const std::wstring& text);

    static std::wstring Reshape(const std::wstring& input);

    struct ShapedGlyph {
        uint32_t code;
        bool isZeroWidth;
    };

    static std::vector<ShapedGlyph> ShapeForLine(const std::vector<uint32_t>& nominalCodes);

private:
    static JoiningType GetJoiningType(wchar_t c);
    static const ArabicGlyphForms* GetGlyphForms(wchar_t c);
    static wchar_t GetLamAlef(wchar_t alef, bool isFinal);
    static bool IsAlef(wchar_t c);
};

}
