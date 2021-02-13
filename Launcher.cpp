
#define WIN32_LEAN_AND_MEAN
#define BUILD_WINDOWS
#include <windows.h>
#include <wininet.h>
#include <shellapi.h>

#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <sstream>
#include <memory>
#include <filesystem>

static constexpr wchar_t  AppName[]    = L"Outpost2.exe";
static constexpr wchar_t  AppRelPath[] = L"..";
static constexpr uint32_t GameVersion  = 1401;
static constexpr wchar_t  IniKey[]     = L"CheckForUpdates";
static constexpr wchar_t  IniName[]    = L".\\outpost2.ini";
static constexpr wchar_t  IniSection[] = L"Game";
static constexpr wchar_t  LoaderFlag[] = L"/OPU";
static constexpr wchar_t  UpdateURL[]  = L"https://www.outpost2.net/updatecheck/";
static constexpr wchar_t  UserAgent[]  = L"OPULauncher/1.4.1";

// =====================================================================================================================
bool CheckForUpdates() {
  bool result = false;

  // Check that updates are configured in the INI and ask the user if not.
  int32_t updateCheckEnabled = GetPrivateProfileIntW(IniSection, IniKey, -1, IniName);
  if (updateCheckEnabled == -1) {
    if (MessageBoxW(
      nullptr,
      L"Would you like to check for updates when starting Outpost 2? Note that this requires an internet connection, "
      L"and will send your game and operating system version to OPU. No other information is collected.",
      L"OPU Update",
      MB_YESNO | MB_ICONINFORMATION) == IDYES)
    {
      WritePrivateProfileStringW(IniSection, IniKey, L"1", IniName);
      updateCheckEnabled = 1;
    }
    else {
      WritePrivateProfileStringW(IniSection, IniKey, L"0", IniName);
      updateCheckEnabled = 0;
    }
  }

  if (updateCheckEnabled > 0) {
    HINTERNET hInternet = InternetOpenW(UserAgent, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);

    if (hInternet != NULL) {
      // Configure the timeout (2.5s default) so we don't wait forever if internet is bad/unstable
      uint32_t connectTimeoutMs = GetPrivateProfileIntW(IniSection, L"UpdateCheckTimeoutMs", 2500, IniName);
      InternetSetOptionW(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &connectTimeoutMs, sizeof(connectTimeoutMs));

      // Game update version
      std::wstringstream headersBuilder;
      headersBuilder << "X-Update-Version: " << GameVersion << "\n";

      // Windows version/build (and service pack if available)
      OSVERSIONINFOW versionInfo = { };
      versionInfo.dwOSVersionInfoSize = sizeof(versionInfo);
      if (GetVersionExW(&versionInfo)) {
        headersBuilder << "X-Windows-Version: "
                       << versionInfo.dwMajorVersion << "." << versionInfo.dwMinorVersion
                       << " build " << versionInfo.dwBuildNumber;
        if (versionInfo.szCSDVersion[0]) {
          headersBuilder << " " << versionInfo.szCSDVersion;
        }
        headersBuilder << "\n";
      }

      // Wine version (and host OS info) if present
      HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
      if (hNtdll != NULL) {
        auto*const pfnWineGetVersion = reinterpret_cast<const char*(*)()>(GetProcAddress(hNtdll, "wine_get_version"));

        if (pfnWineGetVersion != nullptr) {
          const char*const pWineVersion = pfnWineGetVersion();

          if (pWineVersion != nullptr) {
            headersBuilder << "X-Wine-Version: " << pWineVersion << "\n";
            auto*const pfnWineGetHostVersion = reinterpret_cast<void(*)(const char**, const char**)>(
              GetProcAddress(hNtdll, "wine_get_host_version"));

            if (pfnWineGetHostVersion != nullptr) {
              const char* sysname = nullptr;
              const char* release = nullptr;
              pfnWineGetHostVersion(&sysname, &release);
              if ((sysname != nullptr) && (release != nullptr)) {
                headersBuilder << "X-Wine-Host-Version: " << sysname << " " << release << "\n";
              }
            }
          }
        }
      }

      // Send the HTTP request to the server and fetch its contents.
      const auto& headers = headersBuilder.str();
      HINTERNET hUrl = InternetOpenUrlW(
        hInternet, UpdateURL, headers.c_str(), headers.size(), INTERNET_FLAG_RELOAD, 0);

      if (hUrl != NULL) {
        std::stringstream dataBuilder;
        char buffer[1024];
        DWORD numBytesRead = ULONG_MAX;
        while (InternetReadFile(hUrl, buffer, sizeof(buffer), &numBytesRead) && (numBytesRead > 0)) {
          dataBuilder.write(buffer, numBytesRead);
        }
        InternetCloseHandle(hUrl);

        // First line of the response should contain a URL pointing to the update page,
        // or empty if the client is already up to date.
        const auto& data = dataBuilder.str();
        if (data.empty() == false) {
          if (MessageBoxW(
            nullptr,
            L"There is a newer version of OPU Update available. Would you like to download the update now?",
            L"Update Available",
            MB_YESNO | MB_ICONINFORMATION) == IDYES)
          {
            // Check that the URL is a valid HTTPS URL before we pass it to ShellExecute for security reasons.
            URL_COMPONENTSA urlComponents = { };
            memset(&urlComponents, 0, sizeof(urlComponents));
            urlComponents.dwStructSize = sizeof(urlComponents);

            if (InternetCrackUrlA(data.c_str(), data.size(), 0, &urlComponents) &&
                (urlComponents.nScheme == INTERNET_SCHEME_HTTPS))
            {
              // Open the URL in the default browser.
              result = true;
              ShellExecuteA(nullptr, "open", data.c_str(), nullptr, nullptr, SW_SHOW);
            }
            else {
              MessageBoxW(nullptr,
                          L"The server returned an invalid response. Please visit the update page manually.",
                          L"Update Available",
                          MB_ICONEXCLAMATION | MB_OK);
            }
          }
        }
      }
      InternetCloseHandle(hInternet);
    }
  }

  return result;
}

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
  if (CheckForUpdates()) {
    // A true return value means the user chose to update, so no need to launch the game.
    return 0;
  }

  const std::filesystem::path appPath = AppRelPath;

  PROCESS_INFORMATION processInfo = { };
  const bool result = LoadApp(appPath/AppName, pCmdLine, &processInfo);

  if (processInfo.hThread != NULL) {
    CloseHandle(processInfo.hThread);
  }

  return result ? 0 : -1;
}
