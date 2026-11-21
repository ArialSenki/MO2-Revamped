#include <windows.h>
#include <stdio.h>
int main(int argc, char** argv) {
  if (argc > 1) {
    HMODULE bridge = LoadLibraryA(argv[1]);
    printf("bridge %s\n", bridge ? "loaded" : "missing");
  }
  HMODULE victim = LoadLibraryA("victim.dll");
  printf("victim %s (error %lu)\n", victim ? "loaded" : "not loaded", GetLastError());
  fflush(stdout);
  /* the bridge loads it once more a second later */
  Sleep(3000);
  printf("survived; second load %s\n",
         GetFileAttributesA("victim-loaded.txt") != INVALID_FILE_ATTRIBUTES ? "worked" : "did not happen");
  return 0;
}
