
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <sstream>
#include <memory>
#include <filesystem>

static constexpr wchar_t AppName[]    = L"Outpost2.exe";
static constexpr wchar_t AppRelPath[] = L"..";
static constexpr wchar_t LoaderFlag[] = L"/OPU";

// =====================================================================================================================
bool LoadApp(
  const std::filesystem::path&  programPath,
  const wchar_t*                pCmdLine,
  PROCESS_INFORMATION*          pProcessInfo)
{
  STARTUPINFO startupInfo = { };
  startupInfo.cb = sizeof(startupInfo);

  bool result = true;

  // CreateProcess can modify the cmd line argument, so we need a mutable string.
  std::unique_ptr<wchar_t[]> pCmd(new wchar_t[_MAX_ENV]);
  {
    std::wstringstream cmdBuilder;
    cmdBuilder << std::filesystem::absolute(programPath) << " " << LoaderFlag;
    if ((pCmdLine != nullptr) && (pCmdLine[0] != L'\0')) {
      cmdBuilder << " " << pCmdLine;
    }
    cmdBuilder.get(pCmd.get(), _MAX_ENV);
  }

  // Create the process.
  if (result) {
    result = CreateProcessW(nullptr,
                            pCmd.get(),
                            nullptr,
                            nullptr,
                            false,
                            0,
                            nullptr,
                            std::filesystem::current_path().wstring().data(),
                            &startupInfo,
                            pProcessInfo);
  }

  if (result && (pProcessInfo->hThread != NULL)) {
    // Inherit process CPU affinity from the loader.
    DWORD_PTR processAffinity = 0;
    DWORD_PTR systemAffinity  = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &processAffinity, &systemAffinity)) {
      SetProcessAffinityMask(pProcessInfo->hProcess, processAffinity);
    }

    // Inherit process priority class and main thread priority from the loader.
    SetPriorityClass(pProcessInfo->hProcess, GetPriorityClass(GetCurrentProcess()));
    SetThreadPriority(pProcessInfo->hThread, GetThreadPriority(GetCurrentThread()));
  }

  return result;
}

// =====================================================================================================================
int WINAPI wWinMain(
  HINSTANCE  hInstance,
  HINSTANCE  hPrevInstance,
  wchar_t*   pCmdLine,
  int        nShowCmd)
{
  const std::filesystem::path appPath = AppRelPath;

  PROCESS_INFORMATION processInfo = { };
  const bool result = LoadApp(appPath/AppName, pCmdLine, &processInfo);

  if (processInfo.hThread != NULL) {
    CloseHandle(processInfo.hThread);
  }

  return result ? 0 : -1;
}
