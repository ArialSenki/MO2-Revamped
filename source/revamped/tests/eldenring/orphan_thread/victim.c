#include <windows.h>
/* Fails its first load the way Damage Competition sometimes does: it starts
   a thread in DllMain and returns FALSE, so the loader unloads it and the
   thread runs in freed memory. A second load works (victim-loaded.txt),
   unless VICTIM_ALWAYS_FAILS is set. */
static DWORD WINAPI worker(void* p) { (void)p; for (;;) Sleep(1000); }
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
  (void)h; (void)r;
  if (reason == DLL_PROCESS_ATTACH) {
    if (GetEnvironmentVariableA("VICTIM_ALWAYS_FAILS", NULL, 0) == 0 &&
        GetFileAttributesA("victim-failed-once.txt") != INVALID_FILE_ATTRIBUTES) {
      CloseHandle(CreateFileA("victim-loaded.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL));
      return TRUE;
    }
    CloseHandle(CreateFileA("victim-failed-once.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL));
    HANDLE t = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (t) CloseHandle(t);
    return FALSE;  /* the loader unloads the DLL; the thread runs later */
  }
  return TRUE;
}
