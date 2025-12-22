// heap_checker.cpp
#include "heap_checker.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#include <sstream>
#include <iostream>

#ifdef _DEBUG
#include <crtdbg.h>
#endif

namespace HeapChecker {

bool CheckHeapIntegrity(std::string* outMessage) {
  bool ok = true;
  std::ostringstream ss;

#ifdef _DEBUG
  // CRT debug heap checks (only valid when using debug CRT)
  if (_CrtCheckMemory() == 0) {
    ok = false;
    ss << "CRT: _CrtCheckMemory failed. ";
  }
  if (_CrtHeapCheck() == 0) {
    ok = false;
    ss << "CRT: _CrtHeapCheck failed. ";
  }
#endif

#if defined(_WIN32)
  // OS-level heap validate against default process heap
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

void ReportHeapCorruptionAndBreak(const char* location) {
  std::ostringstream ss;
  ss << "Heap corruption detected at: " << (location ? location : "(unknown)")
     << "\n";
  std::string msg = ss.str();
#if defined(_WIN32)
  ::OutputDebugStringA(msg.c_str());
#endif
  std::cerr << msg << std::endl;

  if (::IsDebuggerPresent()) {
#if defined(_MSC_VER)
    __debugbreak();
#else
    // fallback
    abort();
#endif
  } else {
    // Force an immediate crash so a dump can be captured
#if defined(_WIN32)
    // Raise a non-continuable exception to generate a crash
    ::RaiseException(EXCEPTION_NONCONTINUABLE, 0, 0, nullptr);
#else
    abort();
#endif
  }
}

}  // namespace HeapChecker
