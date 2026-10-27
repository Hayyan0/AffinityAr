#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <unordered_set>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")

typedef const wchar_t* (*FnProcessArabicText)(const wchar_t* input);
typedef const wchar_t* (*FnUnprocessArabicText)(const wchar_t* input);

static FnProcessArabicText g_ProcessArabic = nullptr;
static FnUnprocessArabicText g_UnprocessArabic = nullptr;

static bool HasArabic(const std::wstring& s) {
    for (wchar_t c : s) {
        if ((c >= 0x0600 && c <= 0x06FF) ||
            (c >= 0x0750 && c <= 0x077F) ||
            (c >= 0x08A0 && c <= 0x08FF) ||
            (c >= 0xFB50 && c <= 0xFDFF) ||
            (c >= 0xFE70 && c <= 0xFEFF)) {
            return true;
        }
    }
    return false;
}

static bool HasPresentationForms(const std::wstring& s) {
    for (wchar_t c : s) {
        if ((c >= 0xFB50 && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF)) {
            return true;
        }
    }
    return false;
}

static std::wstring GetClipboardText() {
    if (!OpenClipboard(NULL)) return L"";
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    std::wstring result;
    if (hData) {
        const wchar_t* pText = (const wchar_t*)GlobalLock(hData);
        if (pText) {
            result = pText;
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    return result;
}

static bool SetClipboardText(const std::wstring& text) {
    if (!OpenClipboard(NULL)) return false;
    EmptyClipboard();
    size_t bytes = (text.length() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* pMem = GlobalLock(hMem);
        if (pMem) {
            memcpy(pMem, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    CloseClipboard();
    return true;
}

static void HandleHotkeyTransform() {
    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
    Sleep(20);

    keybd_event(VK_CONTROL, 0, 0, 0);
    keybd_event('C', 0, 0, 0);
    keybd_event('C', 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    Sleep(60);

    std::wstring selected = GetClipboardText();
    if (selected.empty() || !HasArabic(selected)) {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    std::wstring transformed;
    if (HasPresentationForms(selected) && g_UnprocessArabic) {
        transformed = g_UnprocessArabic(selected.c_str());
    } else if (g_ProcessArabic) {
        transformed = g_ProcessArabic(selected.c_str());
    }

    if (!transformed.empty()) {
        SetClipboardText(transformed);
        Sleep(20);
        keybd_event(VK_CONTROL, 0, 0, 0);
        keybd_event('V', 0, 0, 0);
        keybd_event('V', 0, KEYEVENTF_KEYUP, 0);
        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
        MessageBeep(MB_OK);
    }
}

static std::wstring FindExistingPath(const std::vector<std::wstring>& candidates) {
    for (const auto& path : candidates) {
        if (!path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return path;
        }
    }
    return L"";
}

static std::wstring ReadConfigFile(const std::wstring& cfgPath) {
    if (GetFileAttributesW(cfgPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return L"";
    }

    HANDLE hFile = CreateFileW(cfgPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return L"";

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize > 8192) {
        CloseHandle(hFile);
        return L"";
    }

    std::vector<char> buffer(fileSize + 2, 0);
    DWORD bytesRead = 0;
    ReadFile(hFile, buffer.data(), fileSize, &bytesRead, NULL);
    CloseHandle(hFile);

    const char* pData = buffer.data();
    size_t dataLen = bytesRead;

    if (dataLen >= 3 && (unsigned char)pData[0] == 0xEF && (unsigned char)pData[1] == 0xBB && (unsigned char)pData[2] == 0xBF) {
        pData += 3;
        dataLen -= 3;
    }

    int wlen = MultiByteToWideChar(CP_UTF8, 0, pData, (int)dataLen, NULL, 0);
    if (wlen > 0) {
        std::wstring line(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, pData, (int)dataLen, &line[0], wlen);
        while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n' || line.back() == L' ' || line.back() == L'\t')) {
            line.pop_back();
        }
        if (GetFileAttributesW(line.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return line;
        }
    }
    return L"";
}

static std::wstring FindMsixAffinityFromRegistry() {
    HKEY hKey = NULL;
    const wchar_t* subKey = L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository\\Packages";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t keyName[512] = {0};
        DWORD index = 0;
        DWORD keyNameSize = 512;
        while (RegEnumKeyExW(hKey, index++, keyName, &keyNameSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
            keyNameSize = 512;
            if (wcsstr(keyName, L"Canva.Affinity_") == keyName) {
                HKEY hPkg = NULL;
                if (RegOpenKeyExW(hKey, keyName, 0, KEY_READ, &hPkg) == ERROR_SUCCESS) {
                    wchar_t rootFolder[MAX_PATH * 2] = {0};
                    DWORD dataSize = sizeof(rootFolder);
                    DWORD type = 0;
                    if (RegQueryValueExW(hPkg, L"PackageRootFolder", NULL, &type, (LPBYTE)rootFolder, &dataSize) == ERROR_SUCCESS) {
                        std::wstring exeCandidate = std::wstring(rootFolder) + L"\\App\\Affinity.exe";
                        if (GetFileAttributesW(exeCandidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
                            RegCloseKey(hPkg);
                            RegCloseKey(hKey);
                            return exeCandidate;
                        }
                    }
                    RegCloseKey(hPkg);
                }
            }
        }
        RegCloseKey(hKey);
    }
    return L"";
}

static bool InjectDllIntoProcess(DWORD pid, const std::wstring& dllPath) {
    if (dllPath.empty()) return false;

    HANDLE hProcess = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE,
        pid
    );
    if (!hProcess) return false;

    HMODULE hMods[1024];
    DWORD cbNeeded = 0;
    if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
        for (unsigned int i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
            wchar_t szModName[MAX_PATH] = {0};
            if (GetModuleBaseNameW(hProcess, hMods[i], szModName, sizeof(szModName) / sizeof(wchar_t))) {
                if (_wcsicmp(szModName, L"affinity_arabic.dll") == 0) {
                    CloseHandle(hProcess);
                    return true;
                }
            }
        }
    }

    size_t dllPathSize = (dllPath.length() + 1) * sizeof(wchar_t);
    void* remoteMem = VirtualAllocEx(hProcess, NULL, dllPathSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) {
        CloseHandle(hProcess);
        return false;
    }

    if (!WriteProcessMemory(hProcess, remoteMem, dllPath.c_str(), dllPathSize, NULL)) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HMODULE hK32 = GetModuleHandleW(L"kernel32.dll");
    void* pLoadLib = (void*)GetProcAddress(hK32, "LoadLibraryW");
    if (!pLoadLib) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    bool success = false;
    for (int attempt = 0; attempt < 3 && !success; ++attempt) {
        HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pLoadLib, remoteMem, 0, NULL);
        if (hThread) {
            WaitForSingleObject(hThread, 8000);
            DWORD exitCode = 0;
            if (GetExitCodeThread(hThread, &exitCode) && exitCode != 0) {
                success = true;
            }
            CloseHandle(hThread);
        }
        if (!success) {
            Sleep(500);
        }
    }

    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return success;
}

static void RunWatcherLoop(const std::wstring& dllPath) {
    HANDLE hWatcherMutex = CreateMutexW(NULL, TRUE, L"Local\\AffinityArabic_Watcher_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hWatcherMutex) CloseHandle(hWatcherMutex);
        return;
    }

    RegisterHotKey(NULL, 1, MOD_NOREPEAT, VK_F8);
    RegisterHotKey(NULL, 2, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'A');

    std::unordered_set<DWORD> hookedPids;
    MSG msg;

    while (true) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                goto cleanup;
            }
            if (msg.message == WM_HOTKEY) {
                HandleHotkeyTransform();
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe{};
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    if (_wcsicmp(pe.szExeFile, L"Affinity.exe") == 0) {
                        if (hookedPids.find(pe.th32ProcessID) == hookedPids.end()) {
                            Sleep(800); // Allow CLR / imports to initialize
                            if (InjectDllIntoProcess(pe.th32ProcessID, dllPath)) {
                                hookedPids.insert(pe.th32ProcessID);
                            }
                        }
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }

        for (auto it = hookedPids.begin(); it != hookedPids.end(); ) {
            HANDLE hTest = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, *it);
            if (!hTest) {
                it = hookedPids.erase(it);
            } else {
                DWORD code = 0;
                if (GetExitCodeProcess(hTest, &code) && code != STILL_ACTIVE) {
                    it = hookedPids.erase(it);
                } else {
                    ++it;
                }
                CloseHandle(hTest);
            }
        }

        Sleep(500);
    }

cleanup:
    UnregisterHotKey(NULL, 1);
    UnregisterHotKey(NULL, 2);
    if (hWatcherMutex) CloseHandle(hWatcherMutex);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    wchar_t selfPath[MAX_PATH];
    GetModuleFileNameW(NULL, selfPath, MAX_PATH);

    std::wstring selfDir = selfPath;
    size_t lastSlash = selfDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        selfDir = selfDir.substr(0, lastSlash);
    }

    std::wstring localAppData;
    wchar_t localEnv[MAX_PATH] = {0};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localEnv, MAX_PATH) > 0) {
        localAppData = localEnv;
    }

    std::vector<std::wstring> dllCandidates = {
        selfDir + L"\\arabic_support\\bin\\affinity_arabic.dll",
        localAppData + L"\\AffinityArabic\\affinity_arabic.dll",
        selfDir + L"\\affinity_arabic.dll",
        selfDir + L"\\bin\\affinity_arabic.dll",
        L"C:\\Program Files\\Affinity\\Affinity\\arabic_support\\bin\\affinity_arabic.dll",
        L"C:\\Program Files\\Affinity\\Affinity\\affinity_arabic.dll"
    };

    std::wstring dllPath = FindExistingPath(dllCandidates);

    int nArgs = 0;
    LPWSTR* szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);

    bool isWatcherMode = false;
    if (szArglist && nArgs > 1) {
        for (int i = 1; i < nArgs; ++i) {
            if (szArglist[i] && (_wcsicmp(szArglist[i], L"--watch") == 0 || _wcsicmp(szArglist[i], L"/watch") == 0 || _wcsicmp(szArglist[i], L"-watch") == 0)) {
                isWatcherMode = true;
                break;
            }
        }
    }

    HMODULE hArabicDll = nullptr;
    if (!dllPath.empty()) {
        hArabicDll = LoadLibraryW(dllPath.c_str());
        if (hArabicDll) {
            g_ProcessArabic = (FnProcessArabicText)GetProcAddress(hArabicDll, "ProcessArabicText");
            g_UnprocessArabic = (FnUnprocessArabicText)GetProcAddress(hArabicDll, "UnprocessArabicText");
        }
    }

    if (isWatcherMode) {
        if (szArglist) LocalFree(szArglist);
        RunWatcherLoop(dllPath);
        if (hArabicDll) FreeLibrary(hArabicDll);
        return 0;
    }

    std::wstring affinityExe;
    std::wstring cmdLine;
    bool isIfeoLaunch = false;

    if (szArglist && nArgs > 1 && szArglist[1] != nullptr) {
        std::wstring firstArg = szArglist[1];
        std::wstring lowerArg = firstArg;
        std::transform(lowerArg.begin(), lowerArg.end(), lowerArg.begin(), ::towlower);
        if (lowerArg.find(L"affinity.exe") != std::wstring::npos &&
            GetFileAttributesW(firstArg.c_str()) != INVALID_FILE_ATTRIBUTES) {
            affinityExe = firstArg;
            isIfeoLaunch = true;

            cmdLine = L"\"" + affinityExe + L"\"";
            for (int i = 2; i < nArgs; ++i) {
                if (szArglist[i]) {
                    cmdLine += L" \"";
                    cmdLine += szArglist[i];
                    cmdLine += L"\"";
                }
            }
        }
    }

    if (szArglist) {
        LocalFree(szArglist);
        szArglist = nullptr;
    }

    if (affinityExe.empty()) {
        affinityExe = ReadConfigFile(selfDir + L"\\affinity_msix.cfg");
        if (affinityExe.empty() && !localAppData.empty()) {
            affinityExe = ReadConfigFile(localAppData + L"\\AffinityArabic\\affinity_msix.cfg");
        }
    }

    if (affinityExe.empty()) {
        std::vector<std::wstring> exeCandidates = {
            selfDir + L"\\Affinity.exe",
            selfDir + L"\\..\\Affinity.exe",
            selfDir + L"\\..\\..\\Affinity.exe",
            L"C:\\Program Files\\Affinity\\Affinity\\Affinity.exe",
            L"D:\\Program Files\\Affinity\\Affinity\\Affinity.exe",
            L"E:\\Program Files\\Affinity\\Affinity\\Affinity.exe"
        };
        affinityExe = FindExistingPath(exeCandidates);
    }

    if (affinityExe.empty()) {
        affinityExe = FindMsixAffinityFromRegistry();
    }

    if (affinityExe.empty()) {
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(L"C:\\Program Files\\WindowsApps\\Canva.Affinity_*", &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    std::wstring candidate = L"C:\\Program Files\\WindowsApps\\" + std::wstring(fd.cFileName) + L"\\App\\Affinity.exe";
                    if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        affinityExe = candidate;
                        break;
                    }
                }
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    }

    if (affinityExe.empty()) {
        MessageBoxW(NULL, L"Affinity.exe not found! Please check installation directory or reinstall Arabic support.", L"Affinity Arabic Launcher", MB_ICONERROR);
        return 1;
    }

    std::wstring affinityDir = affinityExe;
    size_t exeSlash = affinityDir.find_last_of(L"\\/");
    if (exeSlash != std::wstring::npos) {
        affinityDir = affinityDir.substr(0, exeSlash);
    }

    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    if (cmdLine.empty()) {
        cmdLine = L"\"" + affinityExe + L"\"";
        LPCWSTR rawCmd = GetCommandLineW();
        if (rawCmd) {
            bool inQuote = false;
            const wchar_t* p = rawCmd;
            while (*p) {
                if (*p == L'"') inQuote = !inQuote;
                else if (*p == L' ' && !inQuote) {
                    while (*p == L' ') p++;
                    if (*p) {
                        cmdLine += L" ";
                        cmdLine += p;
                    }
                    break;
                }
                p++;
            }
        }
    }

    DWORD creationFlags = 0;
    if (isIfeoLaunch) {
        creationFlags |= DEBUG_PROCESS;
    }

    BOOL created = CreateProcessW(
        affinityExe.c_str(),
        &cmdLine[0],
        NULL,
        NULL,
        FALSE,
        creationFlags,
        NULL,
        affinityDir.c_str(),
        &si,
        &pi
    );

    if (!created && isIfeoLaunch) {
        created = CreateProcessW(
            affinityExe.c_str(),
            &cmdLine[0],
            NULL,
            NULL,
            FALSE,
            0,
            NULL,
            affinityDir.c_str(),
            &si,
            &pi
        );
    }

    if (!created) {
        MessageBoxW(NULL, L"Failed to start Affinity process!", L"Affinity Arabic Launcher", MB_ICONERROR);
        return 1;
    }

    if (isIfeoLaunch) {
        DebugActiveProcessStop(pi.dwProcessId);
    }

    bool proxyAlreadyInstalled = (GetFileAttributesW((affinityDir + L"\\libstacktrace_orig.dll").c_str()) != INVALID_FILE_ATTRIBUTES);
    if (!proxyAlreadyInstalled && !dllPath.empty()) {
        WaitForInputIdle(pi.hProcess, 3000);
        Sleep(1000);
        InjectDllIntoProcess(pi.dwProcessId, dllPath);
    }

    RegisterHotKey(NULL, 1, MOD_NOREPEAT, VK_F8);
    RegisterHotKey(NULL, 2, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'A');

    MSG msg;
    while (true) {
        DWORD exitCode = 0;
        if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
            break;
        }

        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                goto cleanup;
            }
            if (msg.message == WM_HOTKEY) {
                HandleHotkeyTransform();
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(50);
    }

cleanup:
    UnregisterHotKey(NULL, 1);
    UnregisterHotKey(NULL, 2);
    if (hArabicDll) {
        FreeLibrary(hArabicDll);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
