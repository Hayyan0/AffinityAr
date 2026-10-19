#include "hook_engine.h"
#include "pattern_scanner.h"
#include <cstring>

namespace AffinityArabic {

HookEngine& HookEngine::Instance() {
    static HookEngine s_instance;
    return s_instance;
}

HookEngine::~HookEngine() {
    UnhookAll();
}

bool HookEngine::Initialize() {
    return true;
}

bool HookEngine::CreateHook(void* target, void* detour, void** original, size_t patchSize) {
    if (!target || !detour) return false;

    if (patchSize == 0) {
        patchSize = PatternScanner::DetermineSafePatchSize(target, 14);
    }

    if (patchSize < 14 || patchSize > 32) return false;

    // Guard: Prevent double-hooking if function already has a 14-byte indirect jump
    const uint8_t* pTarget = (const uint8_t*)target;
    if (pTarget[0] == 0xFF && pTarget[1] == 0x25 && pTarget[2] == 0x00 && pTarget[3] == 0x00 && pTarget[4] == 0x00 && pTarget[5] == 0x00) {
        return false;
    }

    for (const auto& h : m_hooks) {
        if (h.target == target) return false;
    }

    void* trampoline = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!trampoline) return false;

    HookEntry entry{};
    entry.target = target;
    entry.detour = detour;
    entry.original = original;
    entry.patchSize = patchSize;
    entry.trampoline = trampoline;

    // Save original bytes
    memcpy(entry.originalBytes, target, patchSize);

    // Build trampoline:
    // 1. Copy original instructions
    memcpy(trampoline, target, patchSize);

    // 2. Jump back to target + patchSize
    uint8_t* pTramp = (uint8_t*)trampoline + patchSize;
    pTramp[0] = 0xFF;
    pTramp[1] = 0x25;
    pTramp[2] = 0x00;
    pTramp[3] = 0x00;
    pTramp[4] = 0x00;
    pTramp[5] = 0x00;
    uintptr_t returnAddress = (uintptr_t)target + patchSize;
    memcpy(pTramp + 6, &returnAddress, sizeof(uintptr_t));

    FlushInstructionCache(GetCurrentProcess(), trampoline, 64);

    if (original) {
        *original = trampoline;
    }

    m_hooks.push_back(entry);
    return EnableHook(target);
}

bool HookEngine::EnableHook(void* target) {
    for (auto& h : m_hooks) {
        if (h.target == target) {
            DWORD oldProtect;
            if (!VirtualProtect(target, h.patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                return false;
            }

            // Write 14-byte indirect jump: FF 25 00 00 00 00 <64-bit address>
            uint8_t* p = (uint8_t*)target;
            p[0] = 0xFF;
            p[1] = 0x25;
            p[2] = 0x00;
            p[3] = 0x00;
            p[4] = 0x00;
            p[5] = 0x00;
            uintptr_t detourAddr = (uintptr_t)h.detour;
            memcpy(p + 6, &detourAddr, sizeof(uintptr_t));

            // Pad remaining bytes with NOP
            for (size_t i = 14; i < h.patchSize; ++i) {
                p[i] = 0x90;
            }

            VirtualProtect(target, h.patchSize, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), target, h.patchSize);
            return true;
        }
    }
    return false;
}

bool HookEngine::DisableHook(void* target) {
    for (auto& h : m_hooks) {
        if (h.target == target) {
            DWORD oldProtect;
            if (VirtualProtect(target, h.patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                memcpy(target, h.originalBytes, h.patchSize);
                VirtualProtect(target, h.patchSize, oldProtect, &oldProtect);
                FlushInstructionCache(GetCurrentProcess(), target, h.patchSize);
                return true;
            }
        }
    }
    return false;
}

void HookEngine::UnhookAll() {
    for (auto& h : m_hooks) {
        DisableHook(h.target);
        if (h.trampoline) {
            VirtualFree(h.trampoline, 0, MEM_RELEASE);
            h.trampoline = nullptr;
        }
    }
    m_hooks.clear();
}

}
