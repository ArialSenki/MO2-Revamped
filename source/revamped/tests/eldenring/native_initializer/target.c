// A mod DLL with a Revamped native initializer: it takes a while (as
// RenderBridge's patching does), then leaves initialized.txt
#include <windows.h>
__declspec(dllexport) void mo2_test_init(void) {
  Sleep(1500);
  HANDLE file = CreateFileA("initialized.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
  if (file != INVALID_HANDLE_VALUE) {
    CloseHandle(file);
  }
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
  return TRUE;
}
