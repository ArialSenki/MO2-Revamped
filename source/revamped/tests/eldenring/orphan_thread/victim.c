#include <windows.h>
static DWORD WINAPI worker(void* p) { (void)p; for (;;) Sleep(1000); }
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
  (void)h; (void)r;
  if (reason == DLL_PROCESS_ATTACH) {
    HANDLE t = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (t) CloseHandle(t);
    return FALSE;  /* the loader unloads the DLL; the thread runs later */
  }
  return TRUE;
}
