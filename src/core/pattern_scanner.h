#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>

namespace AffinityArabic {

class PatternScanner {
public:
    static void* FollowJumps(void* p, int maxDepth = 10);

    static void* FindExport(HMODULE hMod, const char* exportName);

    static void* FindPattern(HMODULE hMod, const char* pattern, const char* sectionName = nullptr);

    static size_t DetermineSafePatchSize(const void* target, size_t minSize = 14);

    static void* ResolveFunction(HMODULE hMod,
                                const char* exportName,
                                const char* aobSignature = nullptr,
                                uintptr_t fallbackRva = 0);

    static std::vector<uintptr_t> FindClassVtables(HMODULE hMod, const char* mangledTypeName);
};
}
