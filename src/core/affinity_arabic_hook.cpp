#include "affinity_arabic_hook.h"
#include "hook_engine.h"
#include "pattern_scanner.h"
#include "credit_manager.h"
#include "../shaping/arabic_shaper.h"
#include "../shaping/dwrite_shaper.h"
#include "../bidi/bidi_engine.h"
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <atomic>
#include <cwctype>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <intrin.h>

namespace AffinityArabic {

static std::ofstream g_LogFile;
static std::atomic<bool> g_LoggingChecked{false};
static bool g_LoggingEnabled = false;

static void Log(const std::string& msg) {
#ifdef _DEBUG
    OutputDebugStringA(("[AffinityArabic] " + msg + "\n").c_str());
#endif

    if (!g_LoggingChecked.load(std::memory_order_relaxed)) {
        char envBuf[16] = {0};
        if (GetEnvironmentVariableA("AFFINITY_ARABIC_LOG", envBuf, sizeof(envBuf)) > 0 && envBuf[0] == '1') {
            g_LoggingEnabled = true;
        }
        g_LoggingChecked.store(true, std::memory_order_relaxed);
    }

    if (!g_LoggingEnabled) {
        return;
    }

    if (!g_LogFile.is_open()) {
        char dllPath[MAX_PATH] = {0};
        HMODULE hThis = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)Log, &hThis);
        if (hThis && GetModuleFileNameA(hThis, dllPath, MAX_PATH)) {
            char* lastSlash = strrchr(dllPath, '\\');
            if (lastSlash) *(lastSlash + 1) = '\0';
            strcat_s(dllPath, "hook.log");
            g_LogFile.open(dllPath, std::ios::app);
        }
    }
    if (g_LogFile.is_open()) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char timeBuf[32];
        snprintf(timeBuf, sizeof(timeBuf), "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        g_LogFile << timeBuf << "[AffinityArabic] " << msg << std::endl;
        g_LogFile.flush();
    }
}

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep) {
    if (ep && ep->ExceptionRecord) {
        DWORD code = ep->ExceptionRecord->ExceptionCode;
        if (code == 0xC0000005 || code == 0xC00000FD || code == 0xC000001D) {
            std::stringstream ss;
            ss << "CRASH DETECTED! Exception Code: 0x" << std::hex << code
               << " at Address: 0x" << (uintptr_t)ep->ExceptionRecord->ExceptionAddress;

            if (code == 0xC0000005 && ep->ExceptionRecord->NumberParameters >= 2) {
                ss << (ep->ExceptionRecord->ExceptionInformation[0] == 0 ? " (READ from 0x" : " (WRITE to 0x")
                   << (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1] << ")";
            }

            HMODULE hMod = NULL;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCWSTR)ep->ExceptionRecord->ExceptionAddress, &hMod);
            if (hMod) {
                wchar_t modPath[MAX_PATH];
                GetModuleFileNameW(hMod, modPath, MAX_PATH);
                std::wstring wp(modPath);
                size_t slash = wp.find_last_of(L"\\/");
                std::wstring base = (slash != std::wstring::npos) ? wp.substr(slash + 1) : wp;
                uintptr_t rva = (uintptr_t)ep->ExceptionRecord->ExceptionAddress - (uintptr_t)hMod;
                char baseMb[MAX_PATH] = {0};
                WideCharToMultiByte(CP_UTF8, 0, base.c_str(), -1, baseMb, (int)sizeof(baseMb), NULL, NULL);
                ss << " in [" << baseMb << "] + 0x" << std::hex << rva;
            }

            Log(ss.str());
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static bool SafeReadUInt32(const void* ptr, uint32_t* outVal) {
    __try {
        *outVal = *(const uint32_t*)ptr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadPointer(const void* ptr, void** outVal) {
    __try {
        *outVal = *(void* const*)ptr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadDouble(const void* ptr, double* outVal) {
    __try {
        *outVal = *(const double*)ptr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadUInt8(const void* ptr, uint8_t* outVal) {
    __try {
        *outVal = *(const uint8_t*)ptr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadInt32(const void* ptr, int32_t* outVal) {
    __try {
        *outVal = *(const int32_t*)ptr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

#pragma pack(push, 1)
struct GlyphPosition {
    double advance; // 0x00
    double dx;      // 0x08
    double dy;      // 0x10
};
#pragma pack(pop)
static_assert(sizeof(GlyphPosition) == 24, "GlyphPosition must be 24 bytes");

struct PointDbl {
    double x;
    double y;
};

struct RectDbl {
    double xMin; // 0x00
    double yMin; // 0x08
    double xMax; // 0x10
    double yMax; // 0x18
};

typedef void (*FnLinePartFillGlyphMetrics)(void* pThis, int startIndex, int endIndex, bool b);
typedef void (*FnLinePartCalculateWidths)(void* pThis, int startIndex, int endIndex);
typedef PointDbl* (*FnLinePartGetBasePosition)(void* pThis, PointDbl* outPoint, int index, bool b);
typedef RectDbl* (*FnLinePartGetHighlightBox)(void* pThis, RectDbl* outRect, int startIndex, int endIndex, bool b);
typedef void (*FnSelectionMoveCaret)(void* pThis, int moveCaret, bool isSelect, const void* param_4);
typedef void* (*FnDeleteTextCommandCtor)(void* pThis, int moveCaret, void* pCountedSelection, int flag);
typedef void (*FnTextRendererDrawTextRunCurves)(
    void* pThis,
    const PointDbl* origin,
    const void* pGlyphMetricsVector,
    const GlyphPosition* positions,
    void* glyphAtts,
    void* renderExtras
);

typedef void (*FnGsrApplyCore)(void* pNonCountedLinePart, int startIndex, bool isLate);
typedef void (*FnGprApply)(void* pNonCountedLinePart, int startIndex);

static FnLinePartFillGlyphMetrics g_OriginalLinePartFillGlyphMetrics = nullptr;
static FnLinePartCalculateWidths g_OriginalLinePartCalculateWidths = nullptr;
static FnLinePartGetBasePosition g_OriginalLinePartGetBasePosition = nullptr;
static FnLinePartGetHighlightBox g_OriginalLinePartGetHighlightBox = nullptr;
static FnSelectionMoveCaret g_OriginalSelectionMoveCaret = nullptr;
static FnDeleteTextCommandCtor g_OriginalDeleteTextCommandCtor = nullptr;
static FnTextRendererDrawTextRunCurves g_OriginalTextRendererDrawTextRunCurves = nullptr;
static FnGsrApplyCore g_OriginalGsrApplyCore = nullptr;
static FnGprApply g_OriginalGprApplyFalse = nullptr;
static FnGprApply g_OriginalGprApplyTrue = nullptr;

struct ComposedPos {
    void* pLinePart;
    int32_t characterIndex;
    int32_t padding;
};
typedef ComposedPos* (*FnHitTestWholeLine)(void* pVisitor, void* pNonCountedLinePart);
static FnHitTestWholeLine g_OriginalHitTestWholeLine = nullptr;

typedef void (*FnHitTestVisitorGetPoint)(void* pVisitor, PointDbl* outPoint, void** ppColumn);
static FnHitTestVisitorGetPoint g_HitTestVisitorGetPoint = nullptr;

static bool SafeGetPoint(FnHitTestVisitorGetPoint fn, void* pVisitor, PointDbl* outPoint, void** ppColumn) {
    if (!fn || !pVisitor || !outPoint || !ppColumn) return false;
    __try {
        fn(pVisitor, outPoint, ppColumn);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

typedef void* (*FnOpenTypeRulesGetAvailableFeatures)(
    void* pThis,
    void* pOutFeatureTagSet,
    const void* pScriptLangPair,
    bool isDefaultAvailable
);
typedef void (*FnFeatureTagSetAdd)(void* pThis, const uint32_t* pTag);

static FnOpenTypeRulesGetAvailableFeatures g_OriginalOpenTypeRulesGetAvailableFeatures = nullptr;
static FnFeatureTagSetAdd g_FeatureTagSetAdd = nullptr;

static uintptr_t g_GlyphIndexGlyphVtable1 = 0;
static uintptr_t g_GlyphIndexGlyphVtable2 = 0;
static void* g_DeleteTextCommandGetEffectiveRanges = nullptr;

typedef void* (*FnGetGlyphIndexMetrics)(void* pFontMetrics, void* outNonCounted, int glyphIndex);
typedef int (*FnGetGlyphIndex)(void* pFontMetrics, unsigned int char32);
typedef void (*FnAcquireStrong)(void* pControlBlock);
typedef int64_t (*FnReleaseStrong)(void* pControlBlock);
typedef void (*FnDeleteObject)(void* pObj, int flags);

static FnGetGlyphIndexMetrics g_GetGlyphIndexMetrics = nullptr;
static FnGetGlyphIndex g_GetGlyphIndex = nullptr;
static FnAcquireStrong g_AcquireStrong = nullptr;
static FnReleaseStrong g_ReleaseStrong = nullptr;

static inline bool IsValidUserPointer(const void* ptr) {
    uintptr_t val = (uintptr_t)ptr;
    return (val >= 0x10000 && val < 0x00007FFFFFFFFFFFULL);
}

static void ReleaseCounted(void* pObj) {
    if (!IsValidUserPointer(pObj) || !g_ReleaseStrong) return;
    __try {
        void* pControlBlock = nullptr;
        if (!SafeReadPointer((char*)pObj + 8, &pControlBlock) || !IsValidUserPointer(pControlBlock)) return;
        int64_t remaining = g_ReleaseStrong(pControlBlock);
        if (remaining == 0) {
            void** vtable = *(void***)pObj;
            if (IsValidUserPointer(vtable) && IsValidUserPointer((void*)vtable[0])) {
                FnDeleteObject fnDel = (FnDeleteObject)vtable[0];
                fnDel(pObj, 1);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void* MakeGlyphMetricsForGID(void* pFontMetrics, int gid) {
    if (!IsValidUserPointer(pFontMetrics) || !g_GetGlyphIndexMetrics || !g_AcquireStrong) {
        return nullptr;
    }
    __try {
        void* nonCounted[2] = { nullptr, nullptr };
        g_GetGlyphIndexMetrics(pFontMetrics, nonCounted, gid);
        void* pMetrics = nonCounted[0];
        if (IsValidUserPointer(pMetrics)) {
            void* pControlBlock = nullptr;
            if (SafeReadPointer((char*)pMetrics + 8, &pControlBlock) && IsValidUserPointer(pControlBlock)) {
                g_AcquireStrong(pControlBlock);
                return pMetrics;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}

static int GetSpaceGID(void* pFontMetrics) {
    if (!IsValidUserPointer(pFontMetrics) || !g_GetGlyphIndex) return 0;
    __try {
        return g_GetGlyphIndex(pFontMetrics, 0x20);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}


struct FullGlyphRenderPlan {
    void* linePartAddress = nullptr;
    void* glyphMetricsBegin = nullptr;
    size_t logicalGlyphCount = 0;
    size_t metricsVectorCount = 0;
    double dwriteToDocScale = 1.0;
    ShapedTextRun shapedRun;
    std::vector<void*> componentMetrics;

    ~FullGlyphRenderPlan() {
        for (void* metrics : componentMetrics) {
            ReleaseCounted(metrics);
        }
    }
};

static std::mutex g_RenderPlansMutex;
static std::unordered_map<void*, std::shared_ptr<FullGlyphRenderPlan>> g_RenderPlans;
static thread_local bool g_InExpandedTextRender = false;

static void RemoveRenderPlan(void* linePartAddress) {
    std::lock_guard<std::mutex> lock(g_RenderPlansMutex);
    g_RenderPlans.erase(linePartAddress);
}

static void StoreRenderPlan(const std::shared_ptr<FullGlyphRenderPlan>& plan) {
    std::lock_guard<std::mutex> lock(g_RenderPlansMutex);
    if (g_RenderPlans.size() >= 2048 &&
        g_RenderPlans.find(plan->linePartAddress) == g_RenderPlans.end()) {
        g_RenderPlans.erase(g_RenderPlans.begin());
    }
    g_RenderPlans[plan->linePartAddress] = plan;
}

static std::shared_ptr<FullGlyphRenderPlan> FindRenderPlan(const void* linePartAddress) {
    if (!IsValidUserPointer(linePartAddress)) return nullptr;

    std::lock_guard<std::mutex> lock(g_RenderPlansMutex);
    auto it = g_RenderPlans.find(const_cast<void*>(linePartAddress));
    if (it == g_RenderPlans.end()) return nullptr;
    void* begin = nullptr;
    void* end = nullptr;
    if (!SafeReadPointer((const char*)linePartAddress + 0x88, &begin) ||
        !SafeReadPointer((const char*)linePartAddress + 0x90, &end)) {
        return nullptr;
    }
    const auto& plan = it->second;
    if (begin != plan->glyphMetricsBegin || !begin || !end || end < begin) {
        return nullptr;
    }
    size_t currentCount = (static_cast<const char*>(end) - static_cast<const char*>(begin)) / sizeof(void*);
    if (currentCount != plan->metricsVectorCount && currentCount != plan->logicalGlyphCount) {
        return nullptr;
    }
    return plan;
}

static std::shared_ptr<FullGlyphRenderPlan> MakeRenderPlan(
    void* linePartAddress,
    void* glyphMetricsBegin,
    size_t logicalGlyphCount,
    size_t metricsVectorCount,
    double dwriteToDocScale,
    const ShapedTextRun& shapedRun,
    void* pFontMetrics
) {
    if (!linePartAddress || !glyphMetricsBegin || !pFontMetrics ||
        shapedRun.glyphs.empty() || shapedRun.clusters.empty()) {
        return nullptr;
    }

    auto plan = std::make_shared<FullGlyphRenderPlan>();
    plan->linePartAddress = linePartAddress;
    plan->glyphMetricsBegin = glyphMetricsBegin;
    plan->logicalGlyphCount = logicalGlyphCount;
    plan->metricsVectorCount = metricsVectorCount;
    plan->dwriteToDocScale = dwriteToDocScale > 0.0001 ? dwriteToDocScale : 1.0;
    plan->shapedRun = shapedRun;
    plan->componentMetrics.reserve(shapedRun.glyphs.size());

    int spaceGid = GetSpaceGID(pFontMetrics);
    for (const auto& component : shapedRun.glyphs) {
        int gid = (component.glyphId != 0 && component.glyphId != 0xFFFF)
            ? (int)component.glyphId
            : spaceGid;
        void* metrics = MakeGlyphMetricsForGID(pFontMetrics, gid);
        if (!metrics) {
            return nullptr;
        }
        plan->componentMetrics.push_back(metrics);
    }
    return plan;
}

static bool BuildExpandedRenderRun(
    const FullGlyphRenderPlan& plan,
    const GlyphPosition* nativePositions,
    size_t visStart,
    size_t visEnd,
    std::vector<void*>& outMetrics,
    std::vector<GlyphPosition>& outPositions
) {
    if (!nativePositions || visStart >= visEnd || visEnd > plan.logicalGlyphCount ||
        plan.componentMetrics.size() != plan.shapedRun.glyphs.size()) {
        return false;
    }

    outMetrics.clear();
    outPositions.clear();
    outMetrics.reserve(plan.componentMetrics.size());
    outPositions.reserve(plan.componentMetrics.size());

    for (auto clusterIt = plan.shapedRun.clusters.rbegin(); clusterIt != plan.shapedRun.clusters.rend(); ++clusterIt) {
        const ShapedGlyphCluster& cluster = *clusterIt;
        size_t firstChar = cluster.firstChar;
        size_t charEnd = firstChar + cluster.charCount;
        size_t firstGlyph = cluster.firstGlyph;
        size_t glyphEnd = firstGlyph + cluster.glyphCount;
        if (charEnd > plan.logicalGlyphCount || glyphEnd > plan.shapedRun.glyphs.size()) return false;

        if (charEnd <= visStart || firstChar >= visEnd) {
            continue;
        }

        double nativeAdvance = 0.0;
        for (size_t logicalIndex = firstChar; logicalIndex < charEnd; ++logicalIndex) {
            if (logicalIndex >= visStart && logicalIndex < visEnd) {
                nativeAdvance += nativePositions[logicalIndex - visStart].advance;
            }
        }

        double shapedAdvance = 0.0;
        for (size_t glyphIndex = firstGlyph; glyphIndex < glyphEnd; ++glyphIndex) {
            shapedAdvance += plan.shapedRun.glyphs[glyphIndex].advance;
        }
        const double advScale = (shapedAdvance > 0.00001 && nativeAdvance > 0.0)
            ? (nativeAdvance / shapedAdvance)
            : ((nativeAdvance > 0.0) ? plan.dwriteToDocScale : 0.0);
        const double offsetScale = plan.dwriteToDocScale;
        size_t baseGlyphIdx = firstGlyph;
        bool foundBase = false;
        for (size_t g = firstGlyph; g < glyphEnd; ++g) {
            if (!plan.shapedRun.glyphs[g].isDiacritic) {
                if (!foundBase || plan.shapedRun.glyphs[g].advance > 0.0f) {
                    baseGlyphIdx = g;
                    foundBase = true;
                }
            }
        }
        const auto& baseComponent = plan.shapedRun.glyphs[baseGlyphIdx];
        const double basePurePlacementX =
            -((double)baseComponent.offsetX - ((double)baseComponent.advance - (double)baseComponent.hmtxAdvance)) * offsetScale;
        const double baseDy = -(double)baseComponent.offsetY * offsetScale;

        double ltrAdvBefore = 0.0;
        for (size_t glyphIndex = firstGlyph; glyphIndex < glyphEnd; ++glyphIndex) {
            const ShapedGlyphComponent& component = plan.shapedRun.glyphs[glyphIndex];
            const double adv = component.advance * advScale;
            GlyphPosition position{};
            position.advance = adv;

            bool isMark = component.isDiacritic;
            if (!isMark) {
                const double purePlacementX =
                    -((double)component.offsetX - ((double)component.advance - (double)component.hmtxAdvance)) * offsetScale;
                position.dx = purePlacementX;
                position.dy = -(double)component.offsetY * offsetScale;
            } else {
                double markOffsetScale = advScale;
                position.dx = -ltrAdvBefore + basePurePlacementX - ((double)component.offsetX * markOffsetScale);
                position.dy = -((double)component.offsetY * offsetScale);
            }

            ltrAdvBefore += adv;
            outMetrics.push_back(plan.componentMetrics[glyphIndex]);
            outPositions.push_back(position);
        }
    }

    return !outMetrics.empty() && outMetrics.size() == outPositions.size();
}

static bool ReadFontStringAtOffset(void* pFontMetrics, size_t offset, char* outBuffer, uint32_t maxLen) {
    if (!IsValidUserPointer(pFontMetrics) || !outBuffer || maxLen == 0) return false;
    __try {
        void* pStringHeader = nullptr;
        if (!SafeReadPointer((char*)pFontMetrics + offset, &pStringHeader) || !IsValidUserPointer(pStringHeader)) {
            return false;
        }
        uint32_t len = 0;
        if (!SafeReadUInt32((char*)pStringHeader + 4, &len) || len == 0 || len >= maxLen) {
            return false;
        }
        const char* chars = (const char*)((char*)pStringHeader + 12);
        memcpy(outBuffer, chars, len);
        outBuffer[len] = '\0';
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static std::wstring ReadFontWStringAtOffset(void* pFontMetrics, size_t offset) {
    char nameBuf[256] = {0};
    if (!ReadFontStringAtOffset(pFontMetrics, offset, nameBuf, sizeof(nameBuf) - 1)) {
        return L"";
    }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, nameBuf, -1, NULL, 0);
    if (wlen <= 1) return L"";
    std::wstring fn(wlen - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, nameBuf, -1, &fn[0], wlen);
    return fn;
}

static inline DWRITE_FONT_FEATURE_TAG NormalizeToDWriteTag(uint32_t tag) {
    uint32_t swapped = _byteswap_ulong(tag);
    if (tag == DWRITE_FONT_FEATURE_TAG_STANDARD_LIGATURES ||
        tag == DWRITE_FONT_FEATURE_TAG_CONTEXTUAL_ALTERNATES ||
        tag == DWRITE_FONT_FEATURE_TAG_DISCRETIONARY_LIGATURES ||
        tag == DWRITE_FONT_FEATURE_TAG_REQUIRED_LIGATURES ||
        tag == DWRITE_FONT_FEATURE_TAG_CURSIVE_POSITIONING) {
        return (DWRITE_FONT_FEATURE_TAG)tag;
    }
    return (DWRITE_FONT_FEATURE_TAG)swapped;
}

static bool GetFontMetricsAndName(
    void* pThis,
    void** outFontMetrics,
    std::wstring* outFontName,
    double* outFontSize = nullptr,
    std::wstring* outPostscriptName = nullptr,
    std::vector<DWRITE_FONT_AXIS_VALUE>* outVarAxes = nullptr,
    std::vector<DWRITE_FONT_FEATURE>* outFontFeatures = nullptr
) {
    if (!IsValidUserPointer(pThis)) return false;

    void* pFonts = nullptr;
    if (!SafeReadPointer((char*)pThis + 0x40, &pFonts) || !IsValidUserPointer(pFonts)) return false;

    void* pGlyphAtts = nullptr;
    if (!SafeReadPointer((char*)pFonts + 8, &pGlyphAtts) || !IsValidUserPointer(pGlyphAtts)) return false;

    void* actualGlyphAtts = pGlyphAtts;
    void* pFontMetrics = nullptr;
    if (SafeReadPointer((char*)pGlyphAtts + 0x1F0, &pFontMetrics) && IsValidUserPointer(pFontMetrics)) {
    } else {
        void* pIndirect = nullptr;
        if (SafeReadPointer(pGlyphAtts, &pIndirect) && IsValidUserPointer(pIndirect)) {
            if (SafeReadPointer((char*)pIndirect + 0x1F0, &pFontMetrics) && IsValidUserPointer(pFontMetrics)) {
                actualGlyphAtts = pIndirect;
            }
        }
    }

    if (!IsValidUserPointer(pFontMetrics)) {
        return false;
    }

    if (outFontMetrics) *outFontMetrics = pFontMetrics;

    if (outFontSize) {
        double fontSize = 0.0;
        if (!SafeReadDouble((char*)actualGlyphAtts + 0x228, &fontSize) || fontSize <= 0.0001) {
            SafeReadDouble((char*)actualGlyphAtts + 0x40, &fontSize);
        }
        double scaleX = 1.0;
        if (SafeReadDouble((char*)actualGlyphAtts + 0x90, &scaleX) && scaleX > 0.0001) {
            fontSize *= scaleX;
        }
        *outFontSize = fontSize;
    }

    std::wstring psName = ReadFontWStringAtOffset(pFontMetrics, 0x10);
    if (outPostscriptName) {
        *outPostscriptName = psName;
    }

    if (outFontName) {
        std::wstring familyName = ReadFontWStringAtOffset(pFontMetrics, 0x18);
        std::wstring ribbiName = ReadFontWStringAtOffset(pFontMetrics, 0x20);
        std::wstring chosen = !ribbiName.empty() ? ribbiName : familyName;
        if (!familyName.empty() && !ribbiName.empty() && ribbiName.find(familyName) == std::wstring::npos) {
            chosen = familyName;
        }

        auto hasKeyword = [](const std::wstring& s, const wchar_t* kw) {
            std::wstring low = s;
            for (wchar_t& c : low) c = (wchar_t)towlower(c);
            std::wstring kwLow = kw;
            for (wchar_t& c : kwLow) c = (wchar_t)towlower(c);
            return low.find(kwLow) != std::wstring::npos;
        };

        if (!psName.empty()) {
            if (hasKeyword(psName, L"slanted") && !hasKeyword(chosen, L"slanted")) {
                chosen += L" Slanted";
            } else if (hasKeyword(psName, L"italic") && !hasKeyword(chosen, L"italic")) {
                if (hasKeyword(psName, L"bold") && !hasKeyword(chosen, L"bold")) {
                    chosen += L" Bold Italic";
                } else {
                    chosen += L" Italic";
                }
            } else if (hasKeyword(psName, L"oblique") && !hasKeyword(chosen, L"oblique")) {
                chosen += L" Oblique";
            }
        }

        uint8_t isItalic = 0;
        if (SafeReadUInt8((char*)pFontMetrics + 0x34, &isItalic) && isItalic != 0) {
            if (!hasKeyword(chosen, L"slanted") && !hasKeyword(chosen, L"italic") && !hasKeyword(chosen, L"oblique")) {
                chosen += L" Slanted";
            }
        }

        if (!psName.empty()) {
            if (hasKeyword(psName, L"extralight") && !hasKeyword(chosen, L"extralight") && !hasKeyword(chosen, L"extra light")) {
                chosen += L" ExtraLight";
            } else if (hasKeyword(psName, L"ultralight") && !hasKeyword(chosen, L"ultralight") && !hasKeyword(chosen, L"ultra light")) {
                chosen += L" UltraLight";
            } else if (hasKeyword(psName, L"thin") && !hasKeyword(chosen, L"thin")) {
                chosen += L" Thin";
            } else if (hasKeyword(psName, L"hairline") && !hasKeyword(chosen, L"hairline")) {
                chosen += L" Hairline";
            } else if (hasKeyword(psName, L"extrabold") && !hasKeyword(chosen, L"extrabold") && !hasKeyword(chosen, L"extra bold")) {
                chosen += L" ExtraBold";
            } else if (hasKeyword(psName, L"ultrabold") && !hasKeyword(chosen, L"ultrabold") && !hasKeyword(chosen, L"ultra bold")) {
                chosen += L" UltraBold";
            } else if (hasKeyword(psName, L"semibold") && !hasKeyword(chosen, L"semibold") && !hasKeyword(chosen, L"semi bold")) {
                chosen += L" SemiBold";
            } else if (hasKeyword(psName, L"black") && !hasKeyword(chosen, L"black")) {
                chosen += L" Black";
            } else if (hasKeyword(psName, L"bold") && !hasKeyword(chosen, L"bold")) {
                chosen += L" Bold";
            }
        }

        uint32_t weight = 0;
        if (SafeReadUInt32((char*)pFontMetrics + 0x30, &weight)) {
            if (weight == 700 && !hasKeyword(chosen, L"bold") && !hasKeyword(chosen, L"variable")) {
                chosen += L" Bold";
            }
        }

        std::vector<DWRITE_FONT_AXIS_VALUE> varAxes;
        auto extractVarAxes = [](void* basePtr, std::vector<DWRITE_FONT_AXIS_VALUE>& outAxes) -> bool {
            if (!IsValidUserPointer(basePtr)) return false;
            void* varBegin = nullptr;
            void* varEnd = nullptr;
            if (SafeReadPointer((char*)basePtr + 0x40, &varBegin) &&
                SafeReadPointer((char*)basePtr + 0x48, &varEnd) &&
                IsValidUserPointer(varBegin) && IsValidUserPointer(varEnd) && varEnd > varBegin) {
                uintptr_t byteLen = (uintptr_t)varEnd - (uintptr_t)varBegin;
                if (byteLen % 56 == 0 && byteLen <= 56 * 64) {
                    for (char* p = (char*)varBegin; p < (char*)varEnd; p += 56) {
                        uint32_t rawTag = 0;
                        SafeReadUInt32(p, &rawTag);

                        char axisNameBuf[64] = {0};
                        ReadFontStringAtOffset(p, 8, axisNameBuf, sizeof(axisNameBuf) - 1);
                        std::string aName = axisNameBuf;
                        for (char& c : aName) c = (char)tolower((unsigned char)c);

                        DWRITE_FONT_AXIS_TAG axisTag = (DWRITE_FONT_AXIS_TAG)0;
                        if (aName.find("weight") != std::string::npos || aName == "wght" ||
                            rawTag == DWRITE_MAKE_FONT_AXIS_TAG('w','g','h','t') || rawTag == 0x77676874U) {
                            axisTag = DWRITE_FONT_AXIS_TAG_WEIGHT;
                        } else if (aName.find("contrast") != std::string::npos || aName == "cont" ||
                                   rawTag == DWRITE_MAKE_FONT_AXIS_TAG('C','O','N','T') || rawTag == 0x434F4E54U ||
                                   rawTag == DWRITE_MAKE_FONT_AXIS_TAG('c','o','n','t') || rawTag == 0x636F6E74U) {
                            axisTag = DWRITE_MAKE_FONT_AXIS_TAG('C','O','N','T');
                        } else if (aName.find("extend") != std::string::npos || aName == "extn" ||
                                   rawTag == DWRITE_MAKE_FONT_AXIS_TAG('E','X','T','N') || rawTag == 0x4558544EU ||
                                   rawTag == DWRITE_MAKE_FONT_AXIS_TAG('e','x','t','n') || rawTag == 0x6578746EU) {
                            axisTag = DWRITE_MAKE_FONT_AXIS_TAG('E','X','T','N');
                        } else if (aName.find("width") != std::string::npos || aName == "wdth" ||
                                   rawTag == DWRITE_MAKE_FONT_AXIS_TAG('w','d','t','h') || rawTag == 0x77647468U) {
                            axisTag = DWRITE_FONT_AXIS_TAG_WIDTH;
                        } else if (aName.find("slant") != std::string::npos || aName == "slnt" ||
                                   rawTag == DWRITE_MAKE_FONT_AXIS_TAG('s','l','n','t') || rawTag == 0x736C6E74U) {
                            axisTag = DWRITE_FONT_AXIS_TAG_SLANT;
                        } else if (aName.find("italic") != std::string::npos || aName == "ital" ||
                                   rawTag == DWRITE_MAKE_FONT_AXIS_TAG('i','t','a','l') || rawTag == 0x6974616CU) {
                            axisTag = DWRITE_FONT_AXIS_TAG_ITALIC;
                        } else if (aName.find("optical") != std::string::npos || aName == "opsz" ||
                                   rawTag == DWRITE_MAKE_FONT_AXIS_TAG('o','p','s','z') || rawTag == 0x6F70737AU) {
                            axisTag = DWRITE_FONT_AXIS_TAG_OPTICAL_SIZE;
                        } else {
                            char c0 = (char)(rawTag & 0xFF);
                            char c1 = (char)((rawTag >> 8) & 0xFF);
                            char c2 = (char)((rawTag >> 16) & 0xFF);
                            char c3 = (char)((rawTag >> 24) & 0xFF);
                            if (isprint((unsigned char)c0) && isprint((unsigned char)c1) &&
                                isprint((unsigned char)c2) && isprint((unsigned char)c3)) {
                                axisTag = (DWRITE_FONT_AXIS_TAG)rawTag;
                            }
                        }

                        if (axisTag != 0) {
                            double dVal = 0.0;
                            SafeReadDouble(p + 0x28, &dVal);
                            DWRITE_FONT_AXIS_VALUE ax{};
                            ax.axisTag = axisTag;
                            ax.value = (float)dVal;
                            outAxes.push_back(ax);
                        }
                    }
                    return !outAxes.empty();
                }
            }
            return false;
        };

        if (!extractVarAxes(pFontMetrics, varAxes)) {
            extractVarAxes(actualGlyphAtts, varAxes);
        }

        if (weight >= 100 && weight <= 1000) {
            bool hasWeightAxis = false;
            for (const auto& ax : varAxes) {
                if (ax.axisTag == DWRITE_FONT_AXIS_TAG_WEIGHT) {
                    hasWeightAxis = true;
                    break;
                }
            }
            if (!hasWeightAxis && !varAxes.empty()) {
                DWRITE_FONT_AXIS_VALUE wAxis{};
                wAxis.axisTag = DWRITE_FONT_AXIS_TAG_WEIGHT;
                wAxis.value = (float)weight;
                varAxes.push_back(wAxis);
            }
        }

        if (outVarAxes) {
            *outVarAxes = varAxes;
        }

        if (!varAxes.empty()) {
            std::stringstream ssVar;
            ssVar << "GetFontMetricsAndName: Detected " << varAxes.size() << " variable axes: ";
            for (const auto& ax : varAxes) {
                char t0 = (char)(ax.axisTag & 0xFF);
                char t1 = (char)((ax.axisTag >> 8) & 0xFF);
                char t2 = (char)((ax.axisTag >> 16) & 0xFF);
                char t3 = (char)((ax.axisTag >> 24) & 0xFF);
                ssVar << "[" << t0 << t1 << t2 << t3 << "=" << ax.value << "] ";
            }
            Log(ssVar.str());
        }

        double extnValue = 0.0;
        for (const auto& ax : varAxes) {
            if (ax.axisTag == DWRITE_MAKE_FONT_AXIS_TAG('E','X','T','N') ||
                ax.axisTag == DWRITE_MAKE_FONT_AXIS_TAG('e','x','t','n')) {
                extnValue = (double)ax.value;
                break;
            }
        }

        if (varAxes.empty() && extnValue > 0.0) {
            if (extnValue > 100.5) {
                if (extnValue >= 350.0) {
                    if (!hasKeyword(chosen, L"extraextended")) chosen += L" ExtraExtended";
                    if (!psName.empty() && !hasKeyword(psName, L"extraextended")) psName += L"-ExtraExtended";
                } else if (extnValue >= 250.0) {
                    if (!hasKeyword(chosen, L"extended")) chosen += L" Extended";
                    if (!psName.empty() && !hasKeyword(psName, L"extended")) psName += L"-Extended";
                } else {
                    if (!hasKeyword(chosen, L"semiextended")) chosen += L" SemiExtended";
                    if (!psName.empty() && !hasKeyword(psName, L"semiextended")) psName += L"-SemiExtended";
                }
            } else {
                auto removeKeyword = [](std::wstring& s, const std::wstring& kw) {
                    size_t pos = 0;
                    std::wstring sLow = s;
                    for (wchar_t& c : sLow) c = (wchar_t)towlower(c);
                    std::wstring kwLow = kw;
                    for (wchar_t& c : kwLow) c = (wchar_t)towlower(c);
                    while ((pos = sLow.find(kwLow, pos)) != std::wstring::npos) {
                        s.erase(pos, kw.length());
                        sLow.erase(pos, kwLow.length());
                    }
                };
                removeKeyword(chosen, L"ExtraExtended");
                removeKeyword(chosen, L"Extra Extended");
                removeKeyword(chosen, L"SemiExtended");
                removeKeyword(chosen, L"Semi Extended");
                removeKeyword(chosen, L"Extended");
                if (!psName.empty()) {
                    removeKeyword(psName, L"-ExtraExtended");
                    removeKeyword(psName, L"ExtraExtended");
                    removeKeyword(psName, L"-SemiExtended");
                    removeKeyword(psName, L"SemiExtended");
                    removeKeyword(psName, L"-Extended");
                    removeKeyword(psName, L"Extended");
                }
            }
            if (outPostscriptName) *outPostscriptName = psName;
        }

        if (!psName.empty()) {
            auto hasWidthKeyword = [](const std::wstring& s) {
                std::wstring low = s;
                for (wchar_t& c : low) c = (wchar_t)towlower(c);
                return low.find(L"cond") != std::wstring::npos ||
                       low.find(L"wide") != std::wstring::npos ||
                       low.find(L"narrow") != std::wstring::npos ||
                       low.find(L"expanded") != std::wstring::npos ||
                       low.find(L"extend") != std::wstring::npos;
            };
            if (hasWidthKeyword(psName) && !hasWidthKeyword(chosen)) {
                chosen = psName;
            }
        }
        *outFontName = chosen;
    }

    if (outFontFeatures) {
        outFontFeatures->clear();
        auto extractFeatures = [](void* glyphAttsPtr, std::vector<DWRITE_FONT_FEATURE>& outFeats) -> bool {
            if (!IsValidUserPointer(glyphAttsPtr)) return false;
            void* pOpenTypeAtt = nullptr;
            if (!SafeReadPointer((char*)glyphAttsPtr + 0x108, &pOpenTypeAtt) || !IsValidUserPointer(pOpenTypeAtt)) {
                return false;
            }
            void* featBegin = nullptr;
            void* featEnd = nullptr;
            if (SafeReadPointer((char*)pOpenTypeAtt + 0x10, &featBegin) &&
                SafeReadPointer((char*)pOpenTypeAtt + 0x18, &featEnd) &&
                IsValidUserPointer(featBegin) && IsValidUserPointer(featEnd) && featEnd >= featBegin) {
                uintptr_t byteLen = (uintptr_t)featEnd - (uintptr_t)featBegin;
                if (byteLen % 8 == 0 && byteLen <= 8 * 256) {
                    for (char* p = (char*)featBegin; p < (char*)featEnd; p += 8) {
                        uint32_t tag = 0;
                        int32_t val = 0;
                        SafeReadUInt32(p, &tag);
                        SafeReadInt32(p + 4, &val);
                        if (tag != 0) {
                            DWRITE_FONT_FEATURE feat{};
                            feat.nameTag = NormalizeToDWriteTag(tag);
                            feat.parameter = val < 0 ? 0 : (UINT32)val;
                            outFeats.push_back(feat);
                        }
                    }
                    return !outFeats.empty();
                }
            }
            return false;
        };

        if (!extractFeatures(actualGlyphAtts, *outFontFeatures)) {
            extractFeatures(pGlyphAtts, *outFontFeatures);
        }

        if (!outFontFeatures->empty()) {
            std::stringstream ssFeat;
            ssFeat << "GetFontMetricsAndName: Detected " << outFontFeatures->size() << " OpenType features: ";
            for (const auto& f : *outFontFeatures) {
                char t0 = (char)(f.nameTag & 0xFF);
                char t1 = (char)((f.nameTag >> 8) & 0xFF);
                char t2 = (char)((f.nameTag >> 16) & 0xFF);
                char t3 = (char)((f.nameTag >> 24) & 0xFF);
                ssFeat << "[" << t0 << t1 << t2 << t3 << "=" << f.parameter << "] ";
            }
            Log(ssFeat.str());
        }
    }

    return true;
}

static bool IsOpenTypeAddedGlyph(const void* glyph, const uint16_t* pFlags, size_t index) {
    if (pFlags) {
        __try {
            if ((pFlags[index] & 0x08) != 0) return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (IsValidUserPointer(glyph) && (g_GlyphIndexGlyphVtable1 != 0 || g_GlyphIndexGlyphVtable2 != 0)) {
        void* vtbl = nullptr;
        if (SafeReadPointer(glyph, &vtbl)) {
            uintptr_t v = (uintptr_t)vtbl;
            if (v == g_GlyphIndexGlyphVtable1 || v == g_GlyphIndexGlyphVtable2) {
                return true;
            }
        }
    }
    return false;
}

static bool InspectLinePart(void* pThis, size_t* outPrintableCount, std::vector<uint32_t>* outCodes, size_t* outTotalCount = nullptr) {
    if (!IsValidUserPointer(pThis)) return false;

    void** pBegin = nullptr;
    void** pEnd = nullptr;
    if (!SafeReadPointer((char*)pThis + 0x18, (void**)&pBegin) ||
        !SafeReadPointer((char*)pThis + 0x20, (void**)&pEnd) ||
        !pBegin || !pEnd || pEnd <= pBegin) {
        return false;
    }

    size_t count = pEnd - pBegin;
    if (count == 0 || count > 20000) return false;
    if (outTotalCount) *outTotalCount = count;

    uint16_t* pFlags = nullptr;
    SafeReadPointer((char*)pThis + 0x70, (void**)&pFlags);

    bool hasArabic = false;
    std::vector<uint32_t> codes;
    codes.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        void* glyph = nullptr;
        if (!SafeReadPointer(pBegin + i, &glyph) || !glyph) {
            codes.push_back(0);
            continue;
        }
        if (IsOpenTypeAddedGlyph(glyph, pFlags, i)) {
            codes.push_back(0);
            continue;
        }
        uint32_t code = 0;
        if (SafeReadUInt32((char*)glyph + 0x38, &code)) {
            if (code >= 0xFB50 && code <= 0xFEFF) {
                code = (uint32_t)ArabicShaper::ToNominalChar((wchar_t)code);
            }
            codes.push_back(code);
            if (ArabicShaper::IsArabicChar((wchar_t)code)) {
                hasArabic = true;
            }
        } else {
            codes.push_back(0);
        }
    }

    if (!hasArabic) return false;

    size_t printableCount = count;
    while (printableCount > 0 && (codes[printableCount - 1] == 0 ||
                                  codes[printableCount - 1] == L'\r' ||
                                  codes[printableCount - 1] == L'\n' ||
                                  codes[printableCount - 1] == 0x2028 ||
                                  codes[printableCount - 1] == 0x2029)) {
        printableCount--;
    }

    bool hasArabicInPrintable = false;
    for (size_t i = 0; i < printableCount; ++i) {
        if (ArabicShaper::IsArabicChar((wchar_t)codes[i])) {
            hasArabicInPrintable = true;
            break;
        }
    }
    if (!hasArabicInPrintable) return false;

    if (outPrintableCount) *outPrintableCount = printableCount;
    if (outCodes) *outCodes = std::move(codes);
    return true;
}

static std::atomic<bool> g_CaretInArabic{false};
static std::atomic<uint64_t> g_LastCaretUpdate{0};
static std::atomic<int> g_LastCaretIndex{-1};
static std::atomic<int> g_LastPrintableCount{0};

static inline bool IsWhitespaceCode(uint32_t c) {
    return (c == 0x20 || c == 0xA0 || (c >= 0x2000 && c <= 0x200B) || c == 0x3000);
}

static bool IsLinePartBroken(void* pLinePart, size_t printableCount) {
    if (!IsValidUserPointer(pLinePart)) return false;

    void* pColumn = nullptr;
    if (SafeReadPointer((char*)pLinePart + 0xC0, &pColumn) && IsValidUserPointer(pColumn)) {
        void* linesBegin = nullptr;
        void* linesEnd = nullptr;
        int lineIdx = 0;
        if (SafeReadPointer((char*)pColumn + 0x20, &linesBegin) &&
            SafeReadPointer((char*)pColumn + 0x28, &linesEnd) &&
            SafeReadInt32((char*)pLinePart + 0x10, &lineIdx) &&
            linesBegin && linesEnd && linesEnd > linesBegin) {
            size_t lineCount = ((char*)linesEnd - (char*)linesBegin) / sizeof(void*);
            if ((size_t)lineIdx + 1 < lineCount) {
                return true;
            }
        }
    }

    uint8_t isBroken = 0;
    if (SafeReadUInt8((char*)pLinePart + 0x11A, &isBroken) && isBroken != 0) {
        if (g_CaretInArabic.load() && g_LastCaretIndex.load() >= (int)printableCount) {
            return false;
        }
        return true;
    }

    return false;
}

static void CollapseTrailingWhitespaceAdvances(
    void* pLinePart,
    size_t printableCount,
    const std::vector<uint32_t>& codes,
    GlyphPosition* pPosBegin
) {
    if (!pPosBegin || printableCount == 0) return;
    size_t k = printableCount;
    while (k > 0 && IsWhitespaceCode(codes[k - 1])) {
        pPosBegin[k - 1].advance = 0.0;
        pPosBegin[k - 1].dx = 0.0;
        pPosBegin[k - 1].dy = 0.0;
        k--;
    }
}

static void DetourGsrApplyCore(void* pNonCountedLinePart, int startIndex, bool isLate) {
    void* pLinePart = nullptr;
    if (IsValidUserPointer(pNonCountedLinePart) &&
        SafeReadPointer(pNonCountedLinePart, &pLinePart) &&
        InspectLinePart(pLinePart, nullptr, nullptr)) {
        return;
    }
    if (g_OriginalGsrApplyCore) {
        g_OriginalGsrApplyCore(pNonCountedLinePart, startIndex, isLate);
    }
}

static void DetourGprApplyFalse(void* pNonCountedLinePart, int startIndex) {
    void* pLinePart = nullptr;
    if (IsValidUserPointer(pNonCountedLinePart) &&
        SafeReadPointer(pNonCountedLinePart, &pLinePart) &&
        InspectLinePart(pLinePart, nullptr, nullptr)) {
        return;
    }
    if (g_OriginalGprApplyFalse) {
        g_OriginalGprApplyFalse(pNonCountedLinePart, startIndex);
    }
}

static void DetourGprApplyTrue(void* pNonCountedLinePart, int startIndex) {
    void* pLinePart = nullptr;
    if (IsValidUserPointer(pNonCountedLinePart) &&
        SafeReadPointer(pNonCountedLinePart, &pLinePart) &&
        InspectLinePart(pLinePart, nullptr, nullptr)) {
        return;
    }
    if (g_OriginalGprApplyTrue) {
        g_OriginalGprApplyTrue(pNonCountedLinePart, startIndex);
    }
}

static std::wstring g_PrevNominalText;
static void* g_PrevLinePart = nullptr;
static std::atomic<int> g_LamDeletedBeforeAlefIndex{-1};
static std::atomic<uint64_t> g_LamDeletedTick{0};
static void DetourLinePartFillGlyphMetrics(void* pThis, int startIndex, int endIndex, bool b) {
    if (g_OriginalLinePartFillGlyphMetrics) {
        g_OriginalLinePartFillGlyphMetrics(pThis, startIndex, endIndex, b);
    }
    if (pThis) {
        RemoveRenderPlan(pThis);
    }

    try {
        size_t printableCount = 0;
        std::vector<uint32_t> codes;

        if (pThis && InspectLinePart(pThis, &printableCount, &codes) && printableCount > 0) {
            void** pBegin = nullptr;
            SafeReadPointer((char*)pThis + 0x18, (void**)&pBegin);

            void** pMetricsBegin = nullptr;
            void** pMetricsEnd = nullptr;
            SafeReadPointer((char*)pThis + 0x88, (void**)&pMetricsBegin);
            SafeReadPointer((char*)pThis + 0x90, (void**)&pMetricsEnd);

            size_t metricsCount = (pMetricsBegin && pMetricsEnd && pMetricsEnd >= pMetricsBegin) 
                ? (pMetricsEnd - pMetricsBegin) : 0;

            if (pBegin && metricsCount >= printableCount) {
                void* pFontMetrics = nullptr;
                std::wstring fontName;
                std::wstring postscriptName;
                double fontSize = 0.0;
                std::vector<DWRITE_FONT_AXIS_VALUE> varAxes;
                std::vector<DWRITE_FONT_FEATURE> fontFeatures;
                GetFontMetricsAndName(pThis, &pFontMetrics, &fontName, &fontSize, &postscriptName, &varAxes, &fontFeatures);
                const double dwriteToDocScale = (fontSize > 0.0001) ? (fontSize / 16.0) : 1.0;

                std::wstring nominalText;
                nominalText.reserve(printableCount);
                for (size_t i = 0; i < printableCount; ++i) {
                    nominalText.push_back((wchar_t)codes[i]);
                }

                if (g_PrevLinePart == pThis && g_PrevNominalText.size() > nominalText.size()) {
                    size_t delIdx = 0;
                    while (delIdx < nominalText.size() && g_PrevNominalText[delIdx] == nominalText[delIdx]) {
                        delIdx++;
                    }
                    if (delIdx < g_PrevNominalText.size()) {
                        wchar_t deletedChar = g_PrevNominalText[delIdx];
                        wchar_t currentChar = (delIdx < nominalText.size()) ? nominalText[delIdx] : 0;
                        if (deletedChar == 0x0644 && (currentChar == 0x0627 || currentChar == 0x0622 || currentChar == 0x0623 || currentChar == 0x0625)) {
                            g_LamDeletedBeforeAlefIndex.store((int)delIdx);
                            g_LamDeletedTick.store(GetTickCount64());
                            std::stringstream ssDel;
                            ssDel << "FillGlyphMetrics: Detected Lam deletion before Alef at index " << delIdx;
                            Log(ssDel.str());
                        }
                    }
                }
                g_PrevNominalText = nominalText;
                g_PrevLinePart = pThis;

                uint32_t sampleChar = 0;
                uint16_t sampleGid = 0;
                if (IsValidUserPointer(pFontMetrics) && g_GetGlyphIndex) {
                    for (size_t i = 0; i < printableCount; ++i) {
                        if (codes[i] >= 0x0600 && codes[i] <= 0x06FF && codes[i] != 0x20) {
                            int gid = g_GetGlyphIndex(pFontMetrics, codes[i]);
                            if (gid > 0 && gid < 0xFFFF) {
                                sampleChar = codes[i];
                                sampleGid = (uint16_t)gid;
                                break;
                            }
                        }
                    }
                }

                ShapedTextRun shapedRun;
                bool shapedOk = DWriteShaper::Instance().ShapeTextWithFallbackDetailed(
                    nominalText, fontName, 16.0f, shapedRun, postscriptName, sampleChar, sampleGid, varAxes, fontFeatures);
                const std::vector<ShapedGlyphResult>& dwriteGlyphs = shapedRun.glyphsByChar;

                if (shapedOk && dwriteGlyphs.size() == printableCount && IsValidUserPointer(pFontMetrics) && g_GetGlyphIndexMetrics) {
                    int spaceGid = GetSpaceGID(pFontMetrics);
                    std::string fontNameMb;
                    if (!fontName.empty()) {
                        int len = WideCharToMultiByte(CP_UTF8, 0, fontName.c_str(), -1, NULL, 0, NULL, NULL);
                        if (len > 1) {
                            fontNameMb.resize(len - 1);
                            WideCharToMultiByte(CP_UTF8, 0, fontName.c_str(), -1, &fontNameMb[0], len, NULL, NULL);
                        }
                    }

                    std::string psNameMb;
                    if (!postscriptName.empty()) {
                        int len = WideCharToMultiByte(CP_UTF8, 0, postscriptName.c_str(), -1, NULL, 0, NULL, NULL);
                        if (len > 1) {
                            psNameMb.resize(len - 1);
                            WideCharToMultiByte(CP_UTF8, 0, postscriptName.c_str(), -1, &psNameMb[0], len, NULL, NULL);
                        }
                    }

                    std::stringstream ss;
                    ss << "FillGlyphMetrics: GSUB native layout for [" << (fontNameMb.empty() ? "Default" : fontNameMb.c_str())
                       << "] ps=[" << (psNameMb.empty() ? "-" : psNameMb.c_str())
                       << "] sampleGid=" << sampleGid
                       << " (" << printableCount << " chars):";

                    for (size_t i = 0; i < printableCount; ++i) {
                        size_t logIdx = i;
                        uint16_t gid = dwriteGlyphs[logIdx].glyphId;
                        bool isZero = dwriteGlyphs[logIdx].isZeroWidth;

                        void* newMetrics = nullptr;
                        if (!isZero && gid != 0 && gid != 0xFFFF) {
                            newMetrics = MakeGlyphMetricsForGID(pFontMetrics, (int)gid);
                        } else {
                            newMetrics = MakeGlyphMetricsForGID(pFontMetrics, spaceGid);
                        }

                        if (newMetrics && pMetricsBegin) {
                            void* oldMetrics = *(void**)(pMetricsBegin + i);
                            *(void**)(pMetricsBegin + i) = newMetrics;
                            if (oldMetrics) {
                                ReleaseCounted(oldMetrics);
                            }
                        }
                        ss << " [log " << logIdx << " GID=" << gid << "]";
                    }
                    Log(ss.str());

                    auto renderPlan = MakeRenderPlan(
                        pThis,
                        pMetricsBegin,
                        printableCount,
                        metricsCount,
                        dwriteToDocScale,
                        shapedRun,
                        pFontMetrics
                    );
                    if (renderPlan) {
                        StoreRenderPlan(renderPlan);
                    }
                } else if (IsValidUserPointer(pFontMetrics) && g_GetGlyphIndex && g_GetGlyphIndexMetrics) {
                    int spaceGid = GetSpaceGID(pFontMetrics);
                    std::vector<uint32_t> printableCodes(codes.begin(), codes.begin() + printableCount);
                    auto shaped = ArabicShaper::ShapeForLine(printableCodes);

                    for (size_t i = 0; i < printableCount; ++i) {
                        size_t logIdx = i;
                        bool isZero = (logIdx < shaped.size()) ? shaped[logIdx].isZeroWidth : false;
                        int gid = spaceGid;
                        if (!isZero && logIdx < shaped.size()) {
                            gid = g_GetGlyphIndex(pFontMetrics, shaped[logIdx].code);
                            if (gid <= 0) {
                                gid = g_GetGlyphIndex(pFontMetrics, codes[logIdx]);
                            }
                        }
                        void* newMetrics = MakeGlyphMetricsForGID(pFontMetrics, gid > 0 ? gid : spaceGid);
                        if (newMetrics && pMetricsBegin) {
                            void* oldMetrics = *(void**)(pMetricsBegin + i);
                            *(void**)(pMetricsBegin + i) = newMetrics;
                            if (oldMetrics) {
                                ReleaseCounted(oldMetrics);
                            }
                        }
                    }
                }
            }
        }
    } catch (...) {
        Log("Exception caught inside DetourLinePartFillGlyphMetrics!");
    }
}

static std::shared_ptr<FullGlyphRenderPlan> GetOrCreateRenderPlan(void* pLinePart) {
    if (!IsValidUserPointer(pLinePart)) return nullptr;

    auto existing = FindRenderPlan(pLinePart);
    if (existing) return existing;

    size_t printableCount = 0;
    size_t totalCount = 0;
    std::vector<uint32_t> codes;
    if (!InspectLinePart(pLinePart, &printableCount, &codes, &totalCount) || printableCount == 0) {
        return nullptr;
    }

    void** pMetricsBegin = nullptr;
    void** pMetricsEnd = nullptr;
    SafeReadPointer((char*)pLinePart + 0x88, (void**)&pMetricsBegin);
    SafeReadPointer((char*)pLinePart + 0x90, (void**)&pMetricsEnd);
    size_t metricsCount = (pMetricsBegin && pMetricsEnd && pMetricsEnd >= pMetricsBegin)
        ? (size_t)(pMetricsEnd - pMetricsBegin) : 0;
    if (!pMetricsBegin || metricsCount < printableCount) {
        return nullptr;
    }

    void* pFontMetrics = nullptr;
    std::wstring fontName;
    std::wstring postscriptName;
    double fontSize = 0.0;
    std::vector<DWRITE_FONT_AXIS_VALUE> varAxes;
    std::vector<DWRITE_FONT_FEATURE> fontFeatures;
    if (!GetFontMetricsAndName(pLinePart, &pFontMetrics, &fontName, &fontSize, &postscriptName, &varAxes, &fontFeatures) ||
        !IsValidUserPointer(pFontMetrics)) {
        return nullptr;
    }

    std::wstring nominalText;
    nominalText.reserve(printableCount);
    for (size_t i = 0; i < printableCount; ++i) {
        nominalText.push_back((wchar_t)codes[i]);
    }

    uint32_t sampleChar = 0;
    uint16_t sampleGid = 0;
    if (IsValidUserPointer(pFontMetrics) && g_GetGlyphIndex) {
        for (size_t i = 0; i < printableCount; ++i) {
            if (codes[i] >= 0x0600 && codes[i] <= 0x06FF && codes[i] != 0x20) {
                int gid = g_GetGlyphIndex(pFontMetrics, codes[i]);
                if (gid > 0 && gid < 0xFFFF) {
                    sampleChar = codes[i];
                    sampleGid = (uint16_t)gid;
                    break;
                }
            }
        }
    }

    ShapedTextRun shapedRun;
    if (!DWriteShaper::Instance().ShapeTextWithFallbackDetailed(nominalText, fontName, 16.0f, shapedRun, postscriptName, sampleChar, sampleGid, varAxes, fontFeatures) ||
        shapedRun.glyphsByChar.size() != printableCount) {
        return nullptr;
    }

    double dwriteToDocScale = (fontSize > 0.0001) ? (fontSize / 16.0) : 1.0;
    GlyphPosition* pPosBegin = nullptr;
    GlyphPosition* pPosLast = nullptr;
    if (SafeReadPointer((char*)pLinePart + 0x58, (void**)&pPosBegin) &&
        SafeReadPointer((char*)pLinePart + 0x60, (void**)&pPosLast) &&
        pPosBegin && pPosLast && (size_t)(pPosLast - pPosBegin) >= printableCount) {
        if (IsLinePartBroken(pLinePart, printableCount)) {
            CollapseTrailingWhitespaceAdvances(pLinePart, printableCount, codes, pPosBegin);
        }
        double totalUnkernedPosAdv = 0.0;
        double totalUnkernedHmtxEm = 0.0;
        for (size_t i = 0; i < printableCount; ++i) {
            size_t logIdx = i;
            if (!shapedRun.glyphsByChar[logIdx].isZeroWidth) {
                double gposDiff = (double)shapedRun.glyphsByChar[logIdx].advance -
                                  (double)shapedRun.glyphsByChar[logIdx].hmtxAdvance;
                if (gposDiff > -0.0001 && gposDiff < 0.0001) {
                    void* gm = nullptr;
                    double hmtxEm = 0.0;
                    if (SafeReadPointer(pMetricsBegin + i, &gm) && IsValidUserPointer(gm) &&
                        SafeReadDouble((const char*)gm + 0x18, &hmtxEm) && hmtxEm > 0.0) {
                        totalUnkernedPosAdv += pPosBegin[i].advance;
                        totalUnkernedHmtxEm += hmtxEm;
                    }
                }
            }
        }
        if (totalUnkernedHmtxEm > 0.0001 && totalUnkernedPosAdv > 0.0001) {
            dwriteToDocScale = (totalUnkernedPosAdv / totalUnkernedHmtxEm) / 16.0;
        }
    }

    auto plan = MakeRenderPlan(
        pLinePart,
        pMetricsBegin,
        printableCount,
        metricsCount,
        dwriteToDocScale,
        shapedRun,
        pFontMetrics
    );
    if (plan) {
        StoreRenderPlan(plan);
        std::stringstream ss;
        ss << "GetOrCreateRenderPlan: expanded " << printableCount << " chars -> "
           << shapedRun.glyphs.size() << " glyphs (scale=" << dwriteToDocScale << ")";
        Log(ss.str());
    }
    return plan;
}

static void DetourTextRendererDrawTextRunCurves(
    void* pThis,
    const PointDbl* origin,
    const void* pGlyphMetricsVector,
    const GlyphPosition* positions,
    void* glyphAtts,
    void* renderExtras
) {
    if (!g_OriginalTextRendererDrawTextRunCurves) return;

    if (g_InExpandedTextRender) {
        g_OriginalTextRendererDrawTextRunCurves(
            pThis, origin, pGlyphMetricsVector, positions, glyphAtts, renderExtras);
        return;
    }

    try {
        void* pLinePart = nullptr;
        size_t visStart = 0;
        size_t visEnd = 0;
        bool hasSubrange = false;

        if (IsValidUserPointer(renderExtras)) {
            void* pExtras = nullptr;
            if (SafeReadPointer(renderExtras, &pExtras) && IsValidUserPointer(pExtras)) {
                SafeReadPointer((char*)pExtras + 0x18, &pLinePart);
                uint32_t s = 0, e = 0;
                if (SafeReadUInt32((char*)pExtras + 0x20, &s) &&
                    SafeReadUInt32((char*)pExtras + 0x24, &e) && e > s) {
                    visStart = (size_t)s;
                    visEnd = (size_t)e;
                    hasSubrange = true;
                }
            }
        }
        if (!IsValidUserPointer(pLinePart) && IsValidUserPointer(pThis)) {
            SafeReadPointer((char*)pThis + 0x128, &pLinePart);
        }

        std::shared_ptr<FullGlyphRenderPlan> plan = GetOrCreateRenderPlan(pLinePart);
        if (plan && IsValidUserPointer(pGlyphMetricsVector)) {
            size_t rawRangeCount = hasSubrange ? (visEnd - visStart) : 0;
            if (!hasSubrange) {
                visStart = 0;
                visEnd = plan->logicalGlyphCount;
            } else if (visEnd > plan->logicalGlyphCount) {
                visEnd = plan->logicalGlyphCount;
            }
            void* vecBegin = nullptr;
            void* vecEnd = nullptr;
            if (visStart < visEnd &&
                SafeReadPointer((const char*)pGlyphMetricsVector, &vecBegin) &&
                SafeReadPointer((const char*)pGlyphMetricsVector + 8, &vecEnd) &&
                vecBegin && vecEnd && vecEnd >= vecBegin) {
                size_t inputCount = ((const char*)vecEnd - (const char*)vecBegin) / sizeof(void*);
                bool countMatches = hasSubrange
                    ? (inputCount == rawRangeCount || inputCount == (visEnd - visStart))
                    : (inputCount >= (visEnd - visStart));
                if (countMatches) {
                    bool matchesLinePartMetrics = false;
                    size_t checkCount = visEnd - visStart;
                    for (size_t k = 0; k < checkCount && k < inputCount; ++k) {
                        void* inGm = nullptr;
                        void* lpGm = nullptr;
                        if (SafeReadPointer((const char*)vecBegin + k * sizeof(void*), &inGm) &&
                            SafeReadPointer((const char*)plan->glyphMetricsBegin + (visStart + k) * sizeof(void*), &lpGm) &&
                            inGm && inGm == lpGm) {
                            matchesLinePartMetrics = true;
                            break;
                        }
                    }

                    if (matchesLinePartMetrics) {
                        std::vector<void*> expandedMetrics;
                        std::vector<GlyphPosition> expandedPositions;
                        if (BuildExpandedRenderRun(*plan, positions, visStart, visEnd, expandedMetrics, expandedPositions)) {
                            struct RenderGuard {
                                RenderGuard() { g_InExpandedTextRender = true; }
                                ~RenderGuard() { g_InExpandedTextRender = false; }
                            } guard;

                            g_OriginalTextRendererDrawTextRunCurves(
                                pThis,
                                origin,
                                &expandedMetrics,
                                expandedPositions.data(),
                                glyphAtts,
                                renderExtras
                            );
                            return;
                        }
                    }
                }
            }
        }
    } catch (...) {
        Log("Exception caught inside DetourTextRendererDrawTextRunCurves!");
    }

    g_OriginalTextRendererDrawTextRunCurves(
        pThis, origin, pGlyphMetricsVector, positions, glyphAtts, renderExtras);
}
static void DetourLinePartCalculateWidths(void* pThis, int startIndex, int endIndex) {
    size_t printableCount = 0;
    size_t totalCount = 0;
    std::vector<uint32_t> codes;
    bool hasArabic = InspectLinePart(pThis, &printableCount, &codes, &totalCount) && (printableCount > 0);
    if (g_OriginalLinePartCalculateWidths) {
        if (hasArabic) {
            g_OriginalLinePartCalculateWidths(pThis, 0, (int)printableCount);
        } else {
            g_OriginalLinePartCalculateWidths(pThis, startIndex, endIndex);
        }
    }

    if (hasArabic) {
        try {
            GlyphPosition* pPosBegin = nullptr;
            GlyphPosition* pPosLast = nullptr;
            SafeReadPointer((char*)pThis + 0x58, (void**)&pPosBegin);
            SafeReadPointer((char*)pThis + 0x60, (void**)&pPosLast);

            if (pPosBegin && pPosLast && pPosLast > pPosBegin) {
                size_t posCount = pPosLast - pPosBegin;
                if (printableCount > posCount) printableCount = posCount;

                void* pFontMetrics = nullptr;
                std::wstring fontName;
                std::wstring postscriptName;
                double fontSize = 0.0;
                std::vector<DWRITE_FONT_AXIS_VALUE> varAxes;
                std::vector<DWRITE_FONT_FEATURE> fontFeatures;
                GetFontMetricsAndName(pThis, &pFontMetrics, &fontName, &fontSize, &postscriptName, &varAxes, &fontFeatures);

                std::wstring nominalText;
                nominalText.reserve(printableCount);
                for (size_t i = 0; i < printableCount; ++i) {
                    nominalText.push_back((wchar_t)codes[i]);
                }

                uint32_t sampleChar = 0;
                uint16_t sampleGid = 0;
                if (IsValidUserPointer(pFontMetrics) && g_GetGlyphIndex) {
                    for (size_t i = 0; i < printableCount; ++i) {
                        if (codes[i] >= 0x0600 && codes[i] <= 0x06FF && codes[i] != 0x20) {
                            int gid = g_GetGlyphIndex(pFontMetrics, codes[i]);
                            if (gid > 0 && gid < 0xFFFF) {
                                sampleChar = codes[i];
                                sampleGid = (uint16_t)gid;
                                break;
                            }
                        }
                    }
                }

                std::vector<ShapedGlyphResult> dwriteGlyphs;
                bool shapedOk = DWriteShaper::Instance().ShapeTextWithFallback(
                    nominalText, fontName, 16.0f, dwriteGlyphs, postscriptName, sampleChar, sampleGid, varAxes, fontFeatures);

                if (shapedOk && dwriteGlyphs.size() == printableCount) {
                    void** pMetricsBegin = nullptr;
                    SafeReadPointer((char*)pThis + 0x88, (void**)&pMetricsBegin);

                    double totalNativeAdvance = 0.0;
                    double totalHmtxEm = 0.0;
                    double totalShaped = 0.0;
                    for (size_t i = 0; i < printableCount; ++i) {
                        size_t logIdx = printableCount - 1 - i;
                        if (!dwriteGlyphs[logIdx].isZeroWidth) {
                            totalNativeAdvance += pPosBegin[i].advance;
                            totalShaped += dwriteGlyphs[logIdx].advance;
                            if (pMetricsBegin) {
                                void* gm = nullptr;
                                double hmtxEm = 0.0;
                                if (SafeReadPointer(pMetricsBegin + i, &gm) && IsValidUserPointer(gm) &&
                                    SafeReadDouble((const char*)gm + 0x18, &hmtxEm) && hmtxEm > 0.0) {
                                    totalHmtxEm += hmtxEm;
                                }
                            }
                        }
                    }

                    double dwriteToDocScale = 0.0;
                    if (totalHmtxEm > 0.0001 && totalNativeAdvance > 0.0001) {
                        dwriteToDocScale = (totalNativeAdvance / totalHmtxEm) / 16.0;
                    } else if (fontSize > 0.0001) {
                        dwriteToDocScale = fontSize / 16.0;
                    } else if (totalShaped > 0.0001 && totalNativeAdvance > 0.0001) {
                        dwriteToDocScale = totalNativeAdvance / totalShaped;
                    } else {
                        dwriteToDocScale = 1.0;
                    }

                    auto plan = FindRenderPlan(pThis);
                    if (plan) {
                        plan->dwriteToDocScale = dwriteToDocScale;
                    }

                    for (size_t i = 0; i < printableCount; ++i) {
                        size_t logIdx = i;
                        if (dwriteGlyphs[logIdx].isZeroWidth) {
                            pPosBegin[i].advance = 0.0;
                            pPosBegin[i].dx = 0.0;
                            pPosBegin[i].dy = 0.0;
                        } else {
                            const double gposAdvanceDelta =
                                ((double)dwriteGlyphs[logIdx].advance - (double)dwriteGlyphs[logIdx].hmtxAdvance) * dwriteToDocScale;
                            pPosBegin[i].advance = (std::max)(0.0, pPosBegin[i].advance + gposAdvanceDelta);
                            const double purePlacementX =
                                -((double)dwriteGlyphs[logIdx].offsetX - ((double)dwriteGlyphs[logIdx].advance - (double)dwriteGlyphs[logIdx].hmtxAdvance)) * dwriteToDocScale;
                            pPosBegin[i].dx = purePlacementX;
                            pPosBegin[i].dy = -dwriteGlyphs[logIdx].offsetY * dwriteToDocScale;
                        }
                    }

                    if (plan) {
                        for (const auto& cluster : plan->shapedRun.clusters) {
                            if (cluster.charCount >= 2) {
                                size_t baseCount = 0;
                                for (size_t k = cluster.firstChar; k < cluster.firstChar + cluster.charCount && k < printableCount; ++k) {
                                    if (!ArabicShaper::IsTashkeel((wchar_t)codes[k])) {
                                        baseCount++;
                                    }
                                }
                                if (baseCount >= 2) {
                                    double clusterNativeAdv = pPosBegin[cluster.firstChar].advance;
                                    double advPerChar = clusterNativeAdv / (double)baseCount;

                                    for (size_t k = cluster.firstChar; k < cluster.firstChar + cluster.charCount && k < printableCount; ++k) {
                                        if (!ArabicShaper::IsTashkeel((wchar_t)codes[k])) {
                                            pPosBegin[k].advance = advPerChar;
                                            if (k != cluster.firstChar) {
                                                pPosBegin[k].dx = 0.0;
                                                pPosBegin[k].dy = 0.0;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                } else {
                    std::vector<uint32_t> printableCodes(codes.begin(), codes.begin() + printableCount);
                    auto shaped = ArabicShaper::ShapeForLine(printableCodes);
                    for (size_t i = 0; i < printableCount; ++i) {
                        size_t logIdx = i;
                        if (logIdx < shaped.size() && shaped[logIdx].isZeroWidth) {
                            if (shaped[logIdx].code == 0x200D && logIdx > 0 && i > 0) {
                                size_t baseIdx = i - 1; 
                                double totalAdv = pPosBegin[baseIdx].advance;
                                pPosBegin[baseIdx].advance = totalAdv / 2.0;
                                pPosBegin[i].advance = totalAdv / 2.0;
                                pPosBegin[i].dx = 0.0;
                                pPosBegin[i].dy = 0.0;
                            } else {
                                pPosBegin[i].advance = 0.0;
                                pPosBegin[i].dx = 0.0;
                                pPosBegin[i].dy = 0.0;
                            }
                        }
                    }
                }

                if (IsLinePartBroken(pThis, printableCount)) {
                    CollapseTrailingWhitespaceAdvances(pThis, printableCount, codes, pPosBegin);
                }
            }
        } catch (...) {
            Log("Exception caught inside DetourLinePartCalculateWidths!");
        }
    }
}

static PointDbl* DetourLinePartGetBasePosition(void* pThis, PointDbl* outPoint, int index, bool b) {
    try {
        if (!pThis || !outPoint) {
            if (g_OriginalLinePartGetBasePosition) return g_OriginalLinePartGetBasePosition(pThis, outPoint, index, b);
            return outPoint;
        }

        size_t printableCount = 0;
        size_t totalCount = 0;
        std::vector<uint32_t> codes;

        if (InspectLinePart(pThis, &printableCount, &codes, &totalCount) && printableCount > 0) {
            g_CaretInArabic.store(true);
            g_LastCaretIndex.store(index);
            g_LastPrintableCount.store((int)printableCount);
            g_LastCaretUpdate.store(GetTickCount64());

            GlyphPosition* pPosBegin = nullptr;
            GlyphPosition* pPosLast = nullptr;
            SafeReadPointer((char*)pThis + 0x58, (void**)&pPosBegin);
            SafeReadPointer((char*)pThis + 0x60, (void**)&pPosLast);

            if (pPosBegin && pPosLast && pPosLast > pPosBegin) {
                size_t posCount = pPosLast - pPosBegin;
                if (printableCount > posCount) printableCount = posCount;

                if (IsLinePartBroken(pThis, printableCount)) {
                    CollapseTrailingWhitespaceAdvances(pThis, printableCount, codes, pPosBegin);
                }

                double originX = 0.0;
                double originY = 0.0;
                SafeReadDouble((char*)pThis + 0xC8, &originX);
                SafeReadDouble((char*)pThis + 0xD0, &originY);

                double totalW = 0.0;
                for (size_t i = 0; i < printableCount; ++i) {
                    totalW += pPosBegin[i].advance;
                }

                int k = index;
                if (GetTickCount64() - g_LamDeletedTick.load() < 1500 && index == g_LamDeletedBeforeAlefIndex.load()) {
                    k = index + 1;
                    std::stringstream ssDel;
                    ssDel << "GetBasePosition: adjusting caret for Lam-Alef deletion from k=" << index << " to k=" << k;
                    Log(ssDel.str());
                }
                if (k < 0) k = 0;
                if ((size_t)k > printableCount) k = (int)printableCount;

                double rightW = 0.0;
                for (int j = 0; j < k; ++j) {
                    rightW += pPosBegin[j].advance;
                }

                outPoint->x = originX + totalW - rightW;
                outPoint->y = originY;

                std::stringstream ss;
                ss << "GetBasePosition: k=" << index << "/" << printableCount 
                   << " -> x=" << outPoint->x << " (origX=" << originX << ", totalW=" << totalW << ", rightW=" << rightW << ")";
                Log(ss.str());

                return outPoint;
            }
        } else {
            g_CaretInArabic.store(false);
            g_LastCaretIndex.store(-1);
            g_LastPrintableCount.store(0);
        }
    } catch (...) {
        Log("Exception caught inside DetourLinePartGetBasePosition!");
    }

    if (g_OriginalLinePartGetBasePosition) {
        return g_OriginalLinePartGetBasePosition(pThis, outPoint, index, b);
    }
    return outPoint;
}

static RectDbl* DetourLinePartGetHighlightBox(void* pThis, RectDbl* outRect, int startIndex, int endIndex, bool b) {
    if (!g_OriginalLinePartGetHighlightBox) return outRect;

    RectDbl* res = g_OriginalLinePartGetHighlightBox(pThis, outRect, startIndex, endIndex, b);
    if (!res || !pThis) return res;

    try {
        size_t printableCount = 0;
        size_t totalCount = 0;
        std::vector<uint32_t> codes;
        bool hasArabic = InspectLinePart(pThis, &printableCount, &codes, &totalCount) && (printableCount > 0);

        if (!hasArabic) {
            return res;
        }

        g_CaretInArabic.store(true);
        g_LastCaretIndex.store(startIndex);
        g_LastPrintableCount.store((int)printableCount);
        g_LastCaretUpdate.store(GetTickCount64());

        int s = (std::max)(0, (std::min)(startIndex, (int)printableCount));
        int e = (std::max)(0, (std::min)(endIndex, (int)printableCount));
        if (s > e) std::swap(s, e);
        if (s == e) {
            return res;
        }

        GlyphPosition* pPosBegin = nullptr;
        GlyphPosition* pPosLast = nullptr;
        SafeReadPointer((char*)pThis + 0x58, (void**)&pPosBegin);
        SafeReadPointer((char*)pThis + 0x60, (void**)&pPosLast);

        if (!pPosBegin || !pPosLast || pPosLast <= pPosBegin) {
            return res;
        }

        size_t posCount = pPosLast - pPosBegin;
        if (printableCount > posCount) printableCount = posCount;

        if (IsLinePartBroken(pThis, printableCount)) {
            CollapseTrailingWhitespaceAdvances(pThis, printableCount, codes, pPosBegin);
        }

        double originX = 0.0;
        SafeReadDouble((char*)pThis + 0xC8, &originX);

        double totalW = 0.0;
        for (size_t i = 0; i < printableCount; ++i) {
            totalW += pPosBegin[i].advance;
        }

        double minSelectedX = 1e30;
        double maxSelectedX = -1e30;
        bool anySelected = false;

        double cumW = 0.0;
        for (size_t j = 0; j < printableCount; ++j) {
            double adv = pPosBegin[j].advance;
            if (j >= (size_t)s && j < (size_t)e) {
                double charLeft = originX + totalW - cumW - adv;
                double charRight = originX + totalW - cumW;
                if (charLeft < minSelectedX) minSelectedX = charLeft;
                if (charRight > maxSelectedX) maxSelectedX = charRight;
                anySelected = true;
            }
            cumW += adv;
        }

        if (anySelected && maxSelectedX >= minSelectedX) {
            res->xMin = minSelectedX;
            res->xMax = maxSelectedX;
        }
    } catch (...) {
        Log("Exception caught inside DetourLinePartGetHighlightBox!");
    }

    return res;
}

static void DetourSelectionMoveCaret(void* pThis, int moveCaret, bool isSelect, const void* param_4) {
    if (!g_OriginalSelectionMoveCaret) return;

    void* retAddr = _ReturnAddress();
    if (g_DeleteTextCommandGetEffectiveRanges) {
        uintptr_t funcStart = (uintptr_t)g_DeleteTextCommandGetEffectiveRanges;
        if ((uintptr_t)retAddr >= funcStart && (uintptr_t)retAddr <= funcStart + 0x400) {
            g_OriginalSelectionMoveCaret(pThis, moveCaret, isSelect, param_4);
            return;
        }
    } else {
        HMODULE hPersona = GetModuleHandleW(L"libpersona.dll");
        if (hPersona) {
            uintptr_t personaBase = (uintptr_t)hPersona;
            if ((uintptr_t)retAddr >= personaBase + 0x042A0270 && (uintptr_t)retAddr <= personaBase + 0x042A0660) {
                g_OriginalSelectionMoveCaret(pThis, moveCaret, isSelect, param_4);
                return;
            }
        }
    }

    g_LamDeletedBeforeAlefIndex.store(-1);

    int effectiveMove = moveCaret;
    if (g_CaretInArabic.load()) {
        switch (moveCaret) {
        case 1: effectiveMove = 2; break;
        case 2: effectiveMove = 1; break; 
        case 3: effectiveMove = 4; break; 
        case 4: effectiveMove = 3; break;
        case 5: effectiveMove = 6; break;
        case 6: effectiveMove = 5; break;
        case 0xe: effectiveMove = 0xf; break;
        case 0xf: effectiveMove = 0xe; break;
        default: break;
        }
    }

    g_OriginalSelectionMoveCaret(pThis, effectiveMove, isSelect, param_4);
}
static int MapVisualHitToLogicalIndex(const std::wstring& nominalText, int visualHitIndex) {
    int N = (int)nominalText.size();
    if (N <= 0) return 0;

    int v = visualHitIndex;
    if (v < 0) v = 0;
    if (v > N) v = N;

    bool isPureRtl = true;
    for (wchar_t c : nominalText) {
        BidiDirection dir = BidiEngine::GetCharDirection(c);
        if (dir == BidiDirection::LTR || dir == BidiDirection::Number) {
            isPureRtl = false;
            break;
        }
    }

    if (isPureRtl) {
        return N - v;
    }

    auto visToLogMap = BidiEngine::GetVisualToLogicalMap(nominalText);
    if (visToLogMap.size() != (size_t)N) {
        return N - v;
    }

    if (v == 0) {
        size_t log0 = visToLogMap[0];
        if (ArabicShaper::IsArabicChar(nominalText[log0])) {
            return (int)log0 + 1;
        } else {
            return (int)log0;
        }
    }
    if (v == N) {
        size_t logLast = visToLogMap[N - 1];
        if (ArabicShaper::IsArabicChar(nominalText[logLast])) {
            return (int)logLast;
        } else {
            return (int)logLast + 1;
        }
    }

    size_t leftLog = visToLogMap[v - 1];
    size_t rightLog = visToLogMap[v];

    bool leftIsRtl = (leftLog < nominalText.size()) && ArabicShaper::IsArabicChar(nominalText[leftLog]);
    bool rightIsRtl = (rightLog < nominalText.size()) && ArabicShaper::IsArabicChar(nominalText[rightLog]);

    if (leftIsRtl && rightIsRtl) {
        return (int)leftLog;
    } else if (!leftIsRtl && !rightIsRtl) {
        return (int)rightLog;
    } else if (leftIsRtl && !rightIsRtl) {
        return (int)rightLog;
    } else {
        return (int)leftLog + 1;
    }
}

static ComposedPos* DetourHitTestWholeLine(void* pVisitor, void* pNonCountedLinePart) {
    if (!g_OriginalHitTestWholeLine) return nullptr;

    ComposedPos* res = g_OriginalHitTestWholeLine(pVisitor, pNonCountedLinePart);
    if (!res || !IsValidUserPointer(res->pLinePart)) return res;

    try {
        size_t printableCount = 0;
        size_t totalCount = 0;
        std::vector<uint32_t> codes;
        if (InspectLinePart(res->pLinePart, &printableCount, &codes, &totalCount) && printableCount > 0) {
            std::wstring nominalText;
            nominalText.reserve(printableCount);
            for (size_t i = 0; i < printableCount; ++i) {
                nominalText.push_back((wchar_t)codes[i]);
            }

            GlyphPosition* pPosBegin = nullptr;
            GlyphPosition* pPosEnd = nullptr;
            SafeReadPointer((char*)res->pLinePart + 0x58, (void**)&pPosBegin);
            SafeReadPointer((char*)res->pLinePart + 0x60, (void**)&pPosEnd);

            void* pColumn = nullptr;
            SafeReadPointer((char*)res->pLinePart + 0xC0, &pColumn);

            PointDbl clickPt{0.0, 0.0};
            bool gotPoint = SafeGetPoint(g_HitTestVisitorGetPoint, pVisitor, &clickPt, &pColumn);

            int logicalIdx = res->characterIndex;
            if (gotPoint && pPosBegin && pPosEnd && (size_t)(pPosEnd - pPosBegin) >= printableCount) {
                if (IsLinePartBroken(res->pLinePart, printableCount)) {
                    CollapseTrailingWhitespaceAdvances(res->pLinePart, printableCount, codes, pPosBegin);
                }

                double originX = 0.0;
                SafeReadDouble((char*)res->pLinePart + 0xC8, &originX);

                double totalW = 0.0;
                for (size_t i = 0; i < printableCount; ++i) {
                    totalW += pPosBegin[i].advance;
                }

                double clickX = clickPt.x;
                if (clickX >= originX + totalW) {
                    logicalIdx = 0;
                } else if (clickX <= originX) {
                    logicalIdx = (int)printableCount;
                } else {
                    double cumW = 0.0;
                    logicalIdx = (int)printableCount;
                    for (size_t j = 0; j < printableCount; ++j) {
                        double adv = pPosBegin[j].advance;
                        double charRight = originX + totalW - cumW;
                        double charLeft = charRight - adv;
                        if (clickX >= charLeft && clickX <= charRight) {
                            double mid = (charLeft + charRight) * 0.5;
                            if (clickX >= mid) {
                                logicalIdx = (int)j;
                            } else {
                                logicalIdx = (int)j + 1;
                            }
                            break;
                        }
                        cumW += adv;
                    }
                }
            } else {
                logicalIdx = MapVisualHitToLogicalIndex(nominalText, res->characterIndex);
            }

            std::stringstream ss;
            ss << "HitTestWholeLine: origCharIdx=" << res->characterIndex << "/" << printableCount
               << " (gotPoint=" << gotPoint << ", clickX=" << clickPt.x << ") -> logical=" << logicalIdx;
            Log(ss.str());

            res->characterIndex = logicalIdx;

            g_LamDeletedBeforeAlefIndex.store(-1);
            g_CaretInArabic.store(true);
            g_LastCaretIndex.store(logicalIdx);
            g_LastPrintableCount.store((int)printableCount);
            g_LastCaretUpdate.store(GetTickCount64());
        }
    } catch (...) {
        Log("Exception caught inside DetourHitTestWholeLine!");
    }

    return res;
}


static void* DetourOpenTypeRulesGetAvailableFeatures(
    void* pThis,
    void* pOutFeatureTagSet,
    const void* pScriptLangPair,
    bool isDefaultAvailable
) {
    void* ret = g_OriginalOpenTypeRulesGetAvailableFeatures(
        pThis, pOutFeatureTagSet, pScriptLangPair, isDefaultAvailable);

    try {
        if (pThis && pOutFeatureTagSet && g_FeatureTagSetAdd) {
            void* rulesBegin = nullptr;
            void* rulesEnd = nullptr;
            if (SafeReadPointer((char*)pThis + 0x10, &rulesBegin) &&
                SafeReadPointer((char*)pThis + 0x18, &rulesEnd) &&
                IsValidUserPointer(rulesBegin) && IsValidUserPointer(rulesEnd) && rulesEnd >= rulesBegin) {
                uintptr_t byteLen = (uintptr_t)rulesEnd - (uintptr_t)rulesBegin;
                if (byteLen % 0x78 == 0 && byteLen <= 0x78 * 10000) {
                    for (char* rule = (char*)rulesBegin; rule < (char*)rulesEnd; rule += 0x78) {
                        uint32_t scriptTag = 0;
                        uint32_t featureTag = 0;
                        if (SafeReadUInt32(rule, &scriptTag) && SafeReadUInt32(rule + 8, &featureTag)) {
                            if (scriptTag == 0x61726162U || scriptTag == 0x61706361U) {
                                uint32_t be = featureTag;
                                uint32_t le = _byteswap_ulong(featureTag);
                                bool isInternal = (be == 0x696e6974U || le == 0x696e6974U ||  // init
                                                   be == 0x6d656469U || le == 0x6d656469U ||  // medi
                                                   be == 0x66696e61U || le == 0x66696e61U ||  // fina
                                                   be == 0x69736f6cU || le == 0x69736f6cU ||  // isol
                                                   be == 0x726c6967U || le == 0x726c6967U ||  // rlig
                                                   be == 0x63757273U || le == 0x63757273U ||  // curs
                                                   be == 0x6d61726bU || le == 0x6d61726bU ||  // mark
                                                   be == 0x6d6b6d6bU || le == 0x6d6b6d6bU ||  // mkmk
                                                   be == 0x6c6f636cU || le == 0x6c6f636cU);   // locl
                                if (!isInternal) {
                                    g_FeatureTagSetAdd(pOutFeatureTagSet, &featureTag);
                                }
                            }
                        }
                    }
                }
            }
        }
    } catch (...) {
        Log("Exception caught inside DetourOpenTypeRulesGetAvailableFeatures!");
    }

    return ret;
}
extern "C" {

__declspec(dllexport) const wchar_t* ProcessArabicText(const wchar_t* input) {
    static std::wstring s_Buffer;
    if (!input || !*input) return L"";
    s_Buffer = AffinityArabic::BidiEngine::ProcessText(input);
    return s_Buffer.c_str();
}

__declspec(dllexport) const wchar_t* UnprocessArabicText(const wchar_t* input) {
    static std::wstring s_Buffer;
    if (!input || !*input) return L"";
    std::wstring rev(input);
    std::reverse(rev.begin(), rev.end());
    s_Buffer = AffinityArabic::ArabicShaper::NormalizeToNominal(rev);
    return s_Buffer.c_str();
}

}

bool AffinityHookManager::InstallHooks() {
    Log("Starting native layout hook installation...");
    SetUnhandledExceptionFilter(CrashHandler);

    HMODULE hStory = GetModuleHandleW(L"libStory.dll");
    if (!hStory) {
        Log("Error: libStory.dll not loaded!");
        return false;
    }

    uintptr_t baseStory = (uintptr_t)hStory;
    Log("libStory.dll base: 0x" + std::to_string(baseStory));

    std::vector<uintptr_t> vtables = PatternScanner::FindClassVtables(hStory, ".?AVGlyphIndexGlyph@Story@@");
    if (vtables.size() >= 2) {
        g_GlyphIndexGlyphVtable1 = vtables[0];
        g_GlyphIndexGlyphVtable2 = vtables[1];
        Log("Dynamic RTTI discovered GlyphIndexGlyph vtables: 0x" + std::to_string(g_GlyphIndexGlyphVtable1) + ", 0x" + std::to_string(g_GlyphIndexGlyphVtable2));
    } else if (vtables.size() == 1) {
        g_GlyphIndexGlyphVtable1 = vtables[0];
        Log("Dynamic RTTI discovered GlyphIndexGlyph primary vtable: 0x" + std::to_string(g_GlyphIndexGlyphVtable1));
    } else {
        g_GlyphIndexGlyphVtable1 = baseStory + 0xF17458;
        g_GlyphIndexGlyphVtable2 = baseStory + 0xF17518;
        Log("Warning: Dynamic RTTI vtable lookup failed, using fallback vtables");
    }

    HMODULE hRenderer = GetModuleHandleW(L"librenderer.dll");
    if (hRenderer) {
        g_GetGlyphIndexMetrics = (FnGetGlyphIndexMetrics)PatternScanner::FindExport(
            hRenderer, "?GetGlyphIndexMetrics@FontMetrics@Renderer@@QEBA?AV?$NonCounted@$$CBVGlyphMetrics@Renderer@@@Kernel@@H@Z");
        g_GetGlyphIndex = (FnGetGlyphIndex)PatternScanner::FindExport(
            hRenderer, "?GetGlyphIndex@FontMetrics@Renderer@@QEBAHI@Z");
        Log(std::string("Resolved GetGlyphIndexMetrics: ") + (g_GetGlyphIndexMetrics ? "SUCCESS" : "FAILED"));
        Log(std::string("Resolved GetGlyphIndex: ") + (g_GetGlyphIndex ? "SUCCESS" : "FAILED"));
    } else {
        Log("Warning: librenderer.dll not loaded, GSUB glyph metrics disabled");
    }

    HMODULE hKernel = GetModuleHandleW(L"libkernel.dll");
    if (hKernel) {
        g_AcquireStrong = (FnAcquireStrong)PatternScanner::FindExport(hKernel, "?AcquireStrong@CountedControlBlock@Kernel@@QEAAXXZ");
        g_ReleaseStrong = (FnReleaseStrong)PatternScanner::FindExport(hKernel, "?ReleaseStrong@CountedControlBlock@Kernel@@QEAA_JXZ");
        Log(std::string("Resolved AcquireStrong: ") + (g_AcquireStrong ? "SUCCESS" : "FAILED"));
        Log(std::string("Resolved ReleaseStrong: ") + (g_ReleaseStrong ? "SUCCESS" : "FAILED"));
    }

    void* targetFillGlyphMetrics = PatternScanner::ResolveFunction(
        hStory,
        "?FillGlyphMetrics@LinePart@Story@@AEAAXHH_N@Z",
        nullptr,
        0xCCFC30
    );
    bool b1 = HookEngine::Instance().CreateHook(
        targetFillGlyphMetrics,
        (void*)DetourLinePartFillGlyphMetrics,
        (void**)&g_OriginalLinePartFillGlyphMetrics,
        0
    );
    Log(std::string("Hook LinePart::FillGlyphMetrics: ") + (b1 ? "SUCCESS" : "FAILED"));

    void* targetCalculateWidths = PatternScanner::ResolveFunction(
        hStory,
        "?CalculateWidths@LinePart@Story@@QEAAXHH@Z",
        nullptr,
        0xCCF2B0
    );
    bool b2 = HookEngine::Instance().CreateHook(
        targetCalculateWidths,
        (void*)DetourLinePartCalculateWidths,
        (void**)&g_OriginalLinePartCalculateWidths,
        0
    );
    Log(std::string("Hook LinePart::CalculateWidths: ") + (b2 ? "SUCCESS" : "FAILED"));

    void* targetGetBasePosition = PatternScanner::ResolveFunction(
        hStory,
        "?GetBasePosition@LinePart@Story@@QEBA?AV?$PointT@N@Kernel@@H_N@Z",
        nullptr,
        0xCD0100
    );
    bool b3 = HookEngine::Instance().CreateHook(
        targetGetBasePosition,
        (void*)DetourLinePartGetBasePosition,
        (void**)&g_OriginalLinePartGetBasePosition,
        0
    );
    Log(std::string("Hook LinePart::GetBasePosition: ") + (b3 ? "SUCCESS" : "FAILED"));

    void* targetDrawTextRunCurves = PatternScanner::ResolveFunction(
        hStory,
        "?DrawTextRunCurves@TextRenderer@Story@@AEAAXV?$PointT@N@Kernel@@AEBV?$vector@V?$Counted@$$CBVGlyphMetrics@Renderer@@@Kernel@@V?$allocator@V?$Counted@$$CBVGlyphMetrics@Renderer@@@Kernel@@@std@@@std@@PEBVGlyphPosition@Renderer@@V?$NonCounted@$$CBVGlyphAtts@Story@@@4@V?$Counted@$$CBVStoryTextRenderExtras@Story@@@4@@Z",
        nullptr,
        0xDD6B30
    );
    bool b4 = HookEngine::Instance().CreateHook(
        targetDrawTextRunCurves,
        (void*)DetourTextRendererDrawTextRunCurves,
        (void**)&g_OriginalTextRendererDrawTextRunCurves,
        0
    );
    Log(std::string("Hook TextRenderer::DrawTextRunCurves: ") + (b4 ? "SUCCESS" : "FAILED"));

    void* targetGsrApplyCore = PatternScanner::ResolveFunction(
        hStory,
        nullptr,
        "4C 8B DC 55 41 55 48 83 EC 68 8B EA 4C 8B E9 48 8B 11 48 8B 42 20",
        0xCA3000
    );
    bool b5 = HookEngine::Instance().CreateHook(
        targetGsrApplyCore,
        (void*)DetourGsrApplyCore,
        (void**)&g_OriginalGsrApplyCore,
        0
    );
    Log(std::string("Hook Story::GsrApplyCore: ") + (b5 ? "SUCCESS" : "FAILED"));

    void* targetGprApplyFalse = PatternScanner::ResolveFunction(
        hStory,
        nullptr,
        "89 54 24 10 48 89 4C 24 08 55 41 55 41 57 48 83 EC 70 4C 8B 01 44 8B FA",
        0xC93B50
    );
    bool b6 = HookEngine::Instance().CreateHook(
        targetGprApplyFalse,
        (void*)DetourGprApplyFalse,
        (void**)&g_OriginalGprApplyFalse,
        0
    );
    Log(std::string("Hook Story::GprApplyFalse: ") + (b6 ? "SUCCESS" : "FAILED"));

    void* targetGprApplyTrue = PatternScanner::ResolveFunction(
        hStory,
        nullptr,
        "48 89 5C 24 18 48 89 6C 24 20 56 57 41 56 48 83 EC 30 48 8B 19 8B FA 48 8B F1",
        0xC938D0
    );
    bool b7 = HookEngine::Instance().CreateHook(
        targetGprApplyTrue,
        (void*)DetourGprApplyTrue,
        (void**)&g_OriginalGprApplyTrue,
        0
    );
    Log(std::string("Hook Story::GprApplyTrue: ") + (b7 ? "SUCCESS" : "FAILED"));

    void* targetGetHighlightBox = PatternScanner::ResolveFunction(
        hStory,
        "?GetHighlightBox@LinePart@Story@@QEBA?AV?$RectT@N@Kernel@@HH_N@Z",
        nullptr,
        0xCD0A60
    );
    bool b8 = HookEngine::Instance().CreateHook(
        targetGetHighlightBox,
        (void*)DetourLinePartGetHighlightBox,
        (void**)&g_OriginalLinePartGetHighlightBox,
        0
    );
    Log(std::string("Hook Story::LinePart::GetHighlightBox: ") + (b8 ? "SUCCESS" : "FAILED"));

    void* targetSelectionMoveCaret = PatternScanner::ResolveFunction(
        hStory,
        "?MoveCaret@Selection@Story@@QEAAXW402@_NAEBV?$Counted@$$CBVComposedStory@Story@@@Kernel@@@Z",
        nullptr,
        0xD2F110
    );
    bool b9 = HookEngine::Instance().CreateHook(
        targetSelectionMoveCaret,
        (void*)DetourSelectionMoveCaret,
        (void**)&g_OriginalSelectionMoveCaret,
        0
    );
    Log(std::string("Hook Story::Selection::MoveCaret: ") + (b9 ? "SUCCESS" : "FAILED"));

    HMODULE hPersona = GetModuleHandleW(L"libpersona.dll");
    if (hPersona) {
        g_DeleteTextCommandGetEffectiveRanges = PatternScanner::ResolveFunction(
            hPersona,
            "?GetEffectiveRanges@DeleteTextCommand@@UEAA?AVStoryRanges@Story@@V?$NonCounted@$$CBVStory@1@@Kernel@@V?$NonCounted@$$CBVTextSelection@@@5@@Z",
            nullptr,
            0x042A0270
        );
    }

    g_HitTestVisitorGetPoint = (FnHitTestVisitorGetPoint)PatternScanner::ResolveFunction(
        hStory,
        "?GetPoint@HitTestVisitor@Story@@AEBA?AV?$PointT@N@Kernel@@V?$NonCounted@$$CBVComposedColumn@Story@@@4@@Z",
        nullptr,
        0xCAC590
    );
    Log(std::string("Resolve HitTestVisitor::GetPoint: ") + (g_HitTestVisitorGetPoint ? "SUCCESS" : "FAILED"));

    void* targetHitTestWholeLine = PatternScanner::ResolveFunction(
        hStory,
        "?HitTestWholeLine@HitTestVisitor@Story@@QEAAAEBVComposedPos@2@V?$NonCounted@$$CBVLinePart@Story@@@Kernel@@@Z",
        nullptr,
        0xCAD120
    );
    bool b11 = HookEngine::Instance().CreateHook(
        targetHitTestWholeLine,
        (void*)DetourHitTestWholeLine,
        (void**)&g_OriginalHitTestWholeLine,
        0
    );
    Log(std::string("Hook Story::HitTestVisitor::HitTestWholeLine: ") + (b11 ? "SUCCESS" : "FAILED"));

    void* targetGetAvailableFeatures = PatternScanner::ResolveFunction(
        hStory,
        "?GetAvailableFeatures@OpenTypeRules@Story@@QEBA?AVFeatureTagSet@2@AEBVOpenTypeScriptLanguagePair@2@_N@Z",
        nullptr,
        0xCF67D0
    );
    g_FeatureTagSetAdd = (FnFeatureTagSetAdd)PatternScanner::ResolveFunction(
        hStory,
        "?Add@FeatureTagSet@Story@@QEAAXVFeatureTag@2@@Z",
        nullptr,
        0x61AE0
    );
    bool b12 = HookEngine::Instance().CreateHook(
        targetGetAvailableFeatures,
        (void*)DetourOpenTypeRulesGetAvailableFeatures,
        (void**)&g_OriginalOpenTypeRulesGetAvailableFeatures,
        0
    );
    Log(std::string("Hook Story::OpenTypeRules::GetAvailableFeatures: ") + (b12 ? "SUCCESS" : "FAILED"));

    bool allSuccess = b1 && b2 && b3 && b4 && b5 && b6 && b7 && b8 && b9 && b11 && b12;
    Log(std::string("All native Arabic hooks installed: ") + (allSuccess ? "SUCCESS" : "PARTIAL/FAILED"));

    return allSuccess;
}

static bool IsTargetProcess() {
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exePath, MAX_PATH)) return false;
    std::wstring p(exePath);
    size_t slash = p.find_last_of(L"\\/");
    std::wstring name = (slash != std::wstring::npos) ? p.substr(slash + 1) : p;
    std::transform(name.begin(), name.end(), name.begin(), ::towlower);
    return (name == L"affinity.exe");
}

static std::atomic<bool> s_Initialized{false};
static std::atomic<bool> s_HooksInstalled{false};

DWORD WINAPI AffinityHookManager::HookWorkerThread(LPVOID lpParam) {
    Log("HookWorkerThread started in Affinity.exe, waiting for libStory.dll and libpersona.dll...");
    int attempts = 0;
    while (!GetModuleHandleW(L"libStory.dll") || !GetModuleHandleW(L"libpersona.dll")) {
        Sleep(50);
        attempts++;
        if (attempts % 40 == 0) {
            Log("Waiting for target modules... (" + std::to_string(attempts * 50) + " ms elapsed)");
        }
        if (attempts > 600) {
            Log("Timeout waiting for target modules! Aborting hook installation.");
            return 1;
        }
    }

    Log("Target modules loaded! Waiting 200ms for imports to bind...");
    Sleep(200);

    bool expected = false;
    if (s_HooksInstalled.compare_exchange_strong(expected, true)) {
        InstallHooks();
    }
    return 0;
}

static HANDLE s_hProcessMutex = NULL;

bool AffinityHookManager::Initialize() {
    if (!IsTargetProcess()) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::wstring p(exePath);
        size_t slash = p.find_last_of(L"\\/");
        std::wstring name = (slash != std::wstring::npos) ? p.substr(slash + 1) : p;
        char nameMb[MAX_PATH] = {0};
        WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, nameMb, (int)sizeof(nameMb), NULL, NULL);
        Log(std::string("AffinityHookManager::Initialize called in helper process [") + nameMb + "] - skipping hooks.");
        return false;
    }

    Log("===============================================================================");
    Log("Affinity Arabic Native Support — Developed by Hayyan (@HayyanCodes)");
    Log("Twitter / X: https://x.com/HayyanCodes");
    Log("===============================================================================");
    Log("AffinityHookManager::Initialize called in Affinity.exe");

    DWORD pid = GetCurrentProcessId();
    std::wstring mutexName = L"Local\\AffinityArabic_Process_" + std::to_wstring(pid);
    s_hProcessMutex = CreateMutexW(NULL, TRUE, mutexName.c_str());
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        Log("AffinityArabic hooks already active in this process (cross-module mutex detected). Skipping duplicate initialization.");
        if (s_hProcessMutex) {
            CloseHandle(s_hProcessMutex);
            s_hProcessMutex = NULL;
        }
        return true;
    }

    bool expected = false;
    if (!s_Initialized.compare_exchange_strong(expected, true)) {
        Log("AffinityArabic hooks already initializing in Affinity.exe. Skipping duplicate.");
        return true;
    }

    CreateThread(NULL, 0, HookWorkerThread, NULL, 0, NULL);
    return true;
}

void AffinityHookManager::Shutdown() {
    Log("AffinityHookManager::Shutdown called");
    HookEngine::Instance().UnhookAll();
    {
        std::lock_guard<std::mutex> lock(g_RenderPlansMutex);
        g_RenderPlans.clear();
    }
    if (s_hProcessMutex) {
        CloseHandle(s_hProcessMutex);
        s_hProcessMutex = NULL;
    }
    if (g_LogFile.is_open()) {
        g_LogFile.close();
    }
}

}
