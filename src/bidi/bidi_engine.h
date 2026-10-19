#pragma once
#include <string>
#include <vector>

namespace AffinityArabic {

enum class BidiDirection {
    LTR,
    RTL,
    Neutral,
    Number
};

struct BidiRun {
    size_t start;
    size_t length;
    BidiDirection dir;
};

class BidiEngine {
public:
    static BidiDirection GetCharDirection(wchar_t c);
    static wchar_t GetMirroredChar(wchar_t c);
    

    static std::vector<BidiRun> SegmentRuns(const std::wstring& text);
    static std::wstring ReorderVisual(const std::wstring& text);
    static std::vector<size_t> GetVisualToLogicalMap(const std::wstring& text);
    static std::wstring ProcessText(const std::wstring& text);
};

}
