// Loads the bridge, then a mod DLL from its real path, the way MO2's game
// start does. With "virtual", GetModuleFileNameW answers with a path in the
// game folder for every module, as usvfs does; the bridge must still match
// the initializer MO2 gave by the real path.
//   host.exe <bridge.dll> <mod dll path> [virtual]
#include <windows.h>
#include <stdio.h>
#include <string.h>

static DWORD WINAPI virtual_module_file_name(HMODULE module, LPWSTR buffer, DWORD size) {
  const wchar_t fake[] = L"C:\\FakeGame\\Game\\DLLs\\target.dll";
  DWORD length = (DWORD)(sizeof(fake) / sizeof(wchar_t)) - 1;
  if (size <= length) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return size;
  }
  memcpy(buffer, fake, (length + 1) * sizeof(wchar_t));
  return length;
}

static void redirect(const char* dll) {
  unsigned char* target = (unsigned char*)GetProcAddress(GetModuleHandleA(dll), "GetModuleFileNameW");
  if (target == NULL) {
    return;
  }
  unsigned char jump[14] = {0xFF, 0x25, 0, 0, 0, 0};
  void* destination = (void*)&virtual_module_file_name;
  memcpy(jump + 6, &destination, sizeof(destination));
  DWORD old;
  VirtualProtect(target, sizeof(jump), PAGE_EXECUTE_READWRITE, &old);
  memcpy(target, jump, sizeof(jump));
  VirtualProtect(target, sizeof(jump), old, &old);
  FlushInstructionCache(GetCurrentProcess(), target, sizeof(jump));
}

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) {
    return 2;
  }
  if (argc > 3 && wcscmp(argv[3], L"virtual") == 0) {
    redirect("kernelbase.dll");
    redirect("kernel32.dll");
  }
  HMODULE bridge = LoadLibraryW(argv[1]);
  HMODULE mod = LoadLibraryW(argv[2]);
  printf("bridge %s, mod %s\n", bridge ? "loaded" : "missing", mod ? "loaded" : "missing");
  fflush(stdout);
  if (argc > 3 && wcscmp(argv[3], L"window") == 0) {
    // the game's first window: the bridge holds it until the initializer
    // has returned (the game makes its DXGI factory right after)
    HWND window = CreateWindowExW(0, L"STATIC", L"game", WS_OVERLAPPED, 0, 0, 10, 10,
                                  NULL, NULL, NULL, NULL);
    const int ready = GetFileAttributesA("initialized.txt") != INVALID_FILE_ATTRIBUTES;
    printf("initializer %s the first window\n", ready ? "returned before" : "NOT done before");
    if (window) {
      DestroyWindow(window);
    }
    return ready ? 0 : 1;
  }
  // the bridge waits up to 10 s for game code, then scans for 10 s
  for (int waited = 0; waited < 25000; waited += 250) {
    if (GetFileAttributesA("initialized.txt") != INVALID_FILE_ATTRIBUTES) {
      printf("initializer called\n");
      return 0;
    }
    Sleep(250);
  }
  printf("initializer not called\n");
  return 1;
}
