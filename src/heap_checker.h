// heap_checker.h
#pragma once

#include <string>
#include <sstream>
#include <iostream>

#if defined(_WIN32)
# include <windows.h>
#endif

// Only include CRT debug helpers on MSVC debug builds
#if defined(_DEBUG) && defined(_MSC_VER)
# include <crtdbg.h>
#endif

namespace HeapChecker
{

inline bool CheckHeapIntegrity(std::string* outMessage = nullptr)
{
    bool ok = true;
    std::ostringstream ss;

#if defined(_DEBUG) && defined(_MSC_VER)
    // CRT debug heap check (portable across MSVC debug CRT)
    if (_CrtCheckMemory() == 0) {
        ok = false;
        ss << "CRT: _CrtCheckMemory failed. ";
    }
#endif

#if defined(_WIN32)
    HANDLE hHeap = ::GetProcessHeap();
    if (hHeap == nullptr) {
        ok = false;
        ss << "WinAPI: GetProcessHeap returned NULL. ";
    } else {
        if (!::HeapValidate(hHeap, 0, nullptr)) {
            ok = false;
            ss << "WinAPI: HeapValidate(GetProcessHeap()) failed. ";
        }
    }
#endif

    if (!ok) {
        std::string info = ss.str();
        if (outMessage) *outMessage = info;
#if defined(_WIN32)
        ::OutputDebugStringA(("HeapChecker: " + info + "\n").c_str());
#endif
        std::cerr << "HeapChecker: " << info << std::endl;
    }

    return ok;
}

inline void ReportHeapCorruptionAndBreak(const char* location)
{
    std::ostringstream ss;
    ss << "Heap corruption detected at: " << (location ? location : "(unknown)") << "\n";
    std::string msg = ss.str();
#if defined(_WIN32)
    ::OutputDebugStringA(msg.c_str());
#endif
    std::cerr << msg << std::endl;

#if defined(_WIN32)
    if (::IsDebuggerPresent()) {
#  if defined(_MSC_VER)
        __debugbreak();
#  else
        abort();
#  endif
    } else {
        ::RaiseException(EXCEPTION_NONCONTINUABLE, 0, 0, nullptr);
    }
#else
    abort();
#endif
}

} // namespace HeapChecker
