#pragma once
#include <windows.h>
#include <vector>
#include <cstdint>

namespace AffinityArabic {

struct HookEntry {
    void* target;
    void* detour;
    void** original;
    uint8_t originalBytes[32];
    size_t patchSize;
    void* trampoline;
};

class HookEngine {
public:
    static HookEngine& Instance();

    bool CreateHook(void* target, void* detour, void** original, size_t patchSize = 0);
    bool EnableHook(void* target);
    bool DisableHook(void* target);
    void UnhookAll();

private:
    HookEngine() = default;
    ~HookEngine();

    std::vector<HookEntry> m_hooks;
};

}
