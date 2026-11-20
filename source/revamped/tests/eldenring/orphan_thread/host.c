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
  Sleep(1500);
  printf("survived\n");
  return 0;
}
