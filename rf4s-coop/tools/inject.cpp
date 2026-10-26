// rf4s-coop-inject: loads rf4s-coop.dll into a running copy of the game.
//
//   rf4s-coop-inject <game.exe name | process id> [path\to\rf4s-coop.dll]
//
// Injecting into the running game (instead of launching it) keeps Steam's own
// launch flow intact.

#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

std::wstring widen(const char* s) {
  int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
  std::wstring out(n > 0 ? n - 1 : 0, L'\0');
  if (n > 1) MultiByteToWideChar(CP_ACP, 0, s, -1, out.data(), n);
  return out;
}

DWORD findProcess(const std::wstring& exeName) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return 0;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof entry;
  DWORD pid = 0;
  for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry)) {
    if (_wcsicmp(entry.szExeFile, exeName.c_str()) == 0) {
      pid = entry.th32ProcessID;
      break;
    }
  }
  CloseHandle(snapshot);
  return pid;
}

std::wstring defaultDllPath() {
  wchar_t self[MAX_PATH];
  DWORD len = GetModuleFileNameW(nullptr, self, MAX_PATH);
  std::wstring path(self, len);
  return path.substr(0, path.find_last_of(L"\\/") + 1) + L"rf4s-coop.dll";
}

int fail(const char* what) {
  std::fprintf(stderr, "%s (error %lu)\n", what, GetLastError());
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: rf4s-coop-inject <game.exe name | process id> [path\\to\\rf4s-coop.dll]\n");
    return 2;
  }

  char* end = nullptr;
  unsigned long asNumber = std::strtoul(argv[1], &end, 10);
  DWORD pid = (*end == '\0') ? static_cast<DWORD>(asNumber) : findProcess(widen(argv[1]));
  if (pid == 0) {
    std::fprintf(stderr, "No running process called %s. Start the game first.\n", argv[1]);
    return 1;
  }

  std::wstring dll = argc >= 3 ? widen(argv[2]) : defaultDllPath();
  wchar_t full[MAX_PATH];
  if (!GetFullPathNameW(dll.c_str(), MAX_PATH, full, nullptr)) return fail("Bad DLL path");
  if (GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES) return fail("DLL not found");

  HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                   PROCESS_VM_WRITE | PROCESS_VM_READ,
                               FALSE, pid);
  if (!process) return fail("Couldn't open the game process");

  BOOL wow64 = FALSE;
  if (IsWow64Process(process, &wow64) && wow64) {
    CloseHandle(process);
    std::fprintf(stderr, "The game is a 32-bit process; this injector and DLL are 64-bit.\n");
    return 1;
  }

  size_t bytes = (wcslen(full) + 1) * sizeof(wchar_t);
  void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!remote || !WriteProcessMemory(process, remote, full, bytes, nullptr)) {
    CloseHandle(process);
    return fail("Couldn't write into the game process");
  }

  // kernel32 loads at the same address in every process of a session.
  auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
      reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW")));
  HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr);
  if (!thread) {
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);
    return fail("Couldn't start the loader thread");
  }

  WaitForSingleObject(thread, 10000);
  DWORD loaded = 0;
  GetExitCodeThread(thread, &loaded);  // low 32 bits of the module handle; 0 = failed
  CloseHandle(thread);
  VirtualFreeEx(process, remote, 0, MEM_RELEASE);
  CloseHandle(process);

  if (!loaded) {
    std::fprintf(stderr, "LoadLibraryW failed inside the game. Check that the DLL is 64-bit.\n");
    return 1;
  }
  std::printf("Injected into process %lu. See rf4s-coop.log next to the DLL.\n", pid);
  return 0;
}
