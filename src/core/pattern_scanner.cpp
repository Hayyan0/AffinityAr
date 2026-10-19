#include "pattern_scanner.h"
#include <cstring>
#include <cstdlib>

extern "C" {
#include "hde/hde64.h"
}

namespace AffinityArabic {

struct PatternByte {
    uint8_t byte;
    uint8_t mask;
};

struct RTTICompleteObjectLocator {
    DWORD signature;
    DWORD offset;
    DWORD cdOffset;
    DWORD pTypeDescriptor;
    DWORD pClassDescriptor;
    DWORD pSelf;
};

static bool GetSectionRange(HMODULE hMod, const char* sectionName, uintptr_t* pStart, size_t* pSize) {
    if (!hMod || !pStart || !pSize) return false;
    uint8_t* base = (uint8_t*)hMod;

    __try {
        PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)base;
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return false;

        PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)(base + dosHeader->e_lfanew);
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return false;

        if (!sectionName) {
            *pStart = (uintptr_t)base;
            *pSize = ntHeaders->OptionalHeader.SizeOfImage;
            return true;
        }

        PIMAGE_SECTION_HEADER sec = IMAGE_FIRST_SECTION(ntHeaders);
        for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i, ++sec) {
            char name[9] = {0};
            memcpy(name, sec->Name, 8);
            if (_stricmp(name, sectionName) == 0) {
                *pStart = (uintptr_t)(base + sec->VirtualAddress);
                *pSize = sec->Misc.VirtualSize ? sec->Misc.VirtualSize : sec->SizeOfRawData;
                return true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    return false;
}

void* PatternScanner::FollowJumps(void* p, int maxDepth) {
    if (!p) return nullptr;
    uint8_t* cur = (uint8_t*)p;
    for (int depth = 0; depth < maxDepth; ++depth) {
        __try {
            if (cur[0] == 0xE9) {
                int32_t rel = *(const int32_t*)(cur + 1);
                cur = cur + 5 + rel;
                continue;
            }
            if (cur[0] == 0xEB) {
                int8_t rel = *(const int8_t*)(cur + 1);
                cur = cur + 2 + rel;
                continue;
            }
            if (cur[0] == 0xFF && cur[1] == 0x25) {
                int32_t rel = *(const int32_t*)(cur + 2);
                void** targetPtr = (void**)(cur + 6 + rel);
                cur = (uint8_t*)(*targetPtr);
                continue;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
        break;
    }
    return (void*)cur;
}

void* PatternScanner::FindExport(HMODULE hMod, const char* exportName) {
    if (!hMod || !exportName) return nullptr;
    FARPROC proc = GetProcAddress(hMod, exportName);
    if (!proc) return nullptr;
    return FollowJumps((void*)proc);
}

static bool MatchPatternSafe(const uint8_t* scanStart, size_t scanLen, const PatternByte* tokens, size_t tokCount, void** outMatch) {
    uint8_t firstByte = tokens[0].byte;
    uint8_t firstMask = tokens[0].mask;

    __try {
        for (size_t i = 0; i <= scanLen; ++i) {
            if (firstMask && scanStart[i] != firstByte) continue;

            bool match = true;
            for (size_t j = 1; j < tokCount; ++j) {
                if (tokens[j].mask && scanStart[i + j] != tokens[j].byte) {
                    match = false;
                    break;
                }
            }
            if (match) {
                *outMatch = (void*)(scanStart + i);
                return true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return false;
}

void* PatternScanner::FindPattern(HMODULE hMod, const char* pattern, const char* sectionName) {
    if (!hMod || !pattern) return nullptr;
    uintptr_t start = 0;
    size_t size = 0;
    if (!GetSectionRange(hMod, sectionName, &start, &size)) {
        if (!GetSectionRange(hMod, nullptr, &start, &size)) return nullptr;
    }

    std::vector<PatternByte> tokens;
    const char* p = pattern;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (*p == '?') {
            tokens.push_back({0, 0});
            p++;
            if (*p == '?') p++;
        } else {
            char* end = nullptr;
            uint8_t val = (uint8_t)strtoul(p, &end, 16);
            if (end == p) break;
            tokens.push_back({val, 0xFF});
            p = end;
        }
    }

    if (tokens.empty() || size < tokens.size()) return nullptr;

    const uint8_t* scanStart = (const uint8_t*)start;
    const size_t scanLen = size - tokens.size();
    void* matchedPtr = nullptr;

    if (MatchPatternSafe(scanStart, scanLen, tokens.data(), tokens.size(), &matchedPtr)) {
        return matchedPtr;
    }
    return nullptr;
}

size_t PatternScanner::DetermineSafePatchSize(const void* target, size_t minSize) {
    if (!target) return 0;
    size_t size = 0;
    const uint8_t* p = (const uint8_t*)target;
    while (size < minSize && size < 32) {
        hde64s hs;
        unsigned int len = 0;
        __try {
            len = hde64_disasm(p + size, &hs);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return (size >= minSize) ? size : minSize;
        }
        if (len == 0 || (hs.flags & F_ERROR)) {
            return (size >= minSize) ? size : minSize;
        }
        size += len;
    }
    return size;
}

void* PatternScanner::ResolveFunction(HMODULE hMod,
                                     const char* exportName,
                                     const char* aobSignature,
                                     uintptr_t fallbackRva) {
    if (!hMod) return nullptr;

    if (exportName && exportName[0] != '\0') {
        void* addr = FindExport(hMod, exportName);
        if (addr) return addr;
    }

    if (aobSignature && aobSignature[0] != '\0') {
        void* addr = FindPattern(hMod, aobSignature, ".text");
        if (addr) return FollowJumps(addr);
    }

    if (fallbackRva != 0) {
        void* addr = (void*)((uintptr_t)hMod + fallbackRva);
        return FollowJumps(addr);
    }

    return nullptr;
}

static uintptr_t FindStringSafe(const char* start, size_t len, const char* str, size_t strLen) {
    __try {
        const char* cur = start;
        const char* end = cur + len - strLen;
        for (; cur <= end; ++cur) {
            if (memcmp(cur, str, strLen + 1) == 0) {
                return (uintptr_t)cur;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

static size_t ScanColsSafe(const uint8_t* rdataPtr, size_t colScanLimit, DWORD rvaTypeDesc, uintptr_t base, uintptr_t* outLocators, size_t maxLocators) {
    size_t count = 0;
    __try {
        for (size_t i = 0; i <= colScanLimit; i += 4) {
            const RTTICompleteObjectLocator* col = (const RTTICompleteObjectLocator*)(rdataPtr + i);
            if (col->pTypeDescriptor == rvaTypeDesc) {
                DWORD expectedSelf = (DWORD)((uintptr_t)col - base);
                if (col->pSelf == expectedSelf) {
                    if (count < maxLocators) {
                        outLocators[count++] = (uintptr_t)col;
                    }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return count;
}

static size_t ScanVtablesSafe(const uint8_t* rdataPtr, size_t ptrScanLimit, const uintptr_t* locators, size_t locCount, uintptr_t* outVtables, size_t maxVtables) {
    size_t count = 0;
    __try {
        for (size_t i = 0; i <= ptrScanLimit; i += sizeof(void*)) {
            uintptr_t ptrVal = *(const uintptr_t*)(rdataPtr + i);
            for (size_t k = 0; k < locCount; ++k) {
                if (ptrVal == locators[k]) {
                    if (count < maxVtables) {
                        outVtables[count++] = (uintptr_t)(rdataPtr + i + sizeof(void*));
                    }
                    break;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return count;
}

std::vector<uintptr_t> PatternScanner::FindClassVtables(HMODULE hMod, const char* mangledTypeName) {
    std::vector<uintptr_t> vtables;
    if (!hMod || !mangledTypeName) return vtables;

    uintptr_t base = (uintptr_t)hMod;
    size_t strLen = strlen(mangledTypeName);

    const char* sections[] = { ".data", ".rdata", nullptr };
    uintptr_t strAddr = 0;
    for (int s = 0; sections[s] && !strAddr; ++s) {
        uintptr_t secStart = 0;
        size_t secSize = 0;
        if (GetSectionRange(hMod, sections[s], &secStart, &secSize)) {
            strAddr = FindStringSafe((const char*)secStart, secSize, mangledTypeName, strLen);
        }
    }
    if (!strAddr) return vtables;

    uintptr_t typeDescriptor = strAddr - 2 * sizeof(void*);
    DWORD rvaTypeDesc = (DWORD)(typeDescriptor - base);

    uintptr_t rdataStart = 0;
    size_t rdataSize = 0;
    if (!GetSectionRange(hMod, ".rdata", &rdataStart, &rdataSize)) {
        return vtables;
    }

    const uint8_t* rdataPtr = (const uint8_t*)rdataStart;
    const size_t colScanLimit = (rdataSize >= sizeof(RTTICompleteObjectLocator))
                               ? (rdataSize - sizeof(RTTICompleteObjectLocator)) : 0;

    uintptr_t locators[64];
    size_t locCount = ScanColsSafe(rdataPtr, colScanLimit, rvaTypeDesc, base, locators, 64);
    if (locCount == 0) return vtables;


    const size_t ptrScanLimit = (rdataSize >= sizeof(void*)) ? (rdataSize - sizeof(void*)) : 0;
    uintptr_t foundVtables[64];
    size_t vtblCount = ScanVtablesSafe(rdataPtr, ptrScanLimit, locators, locCount, foundVtables, 64);

    for (size_t i = 0; i < vtblCount; ++i) {
        vtables.push_back(foundVtables[i]);
    }

    return vtables;
}

}
