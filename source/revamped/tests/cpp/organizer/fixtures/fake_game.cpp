// MO2 Revamped: a stand-in game exe for rootlaunch_test (built as game.exe by run.ps1,
// linked with fake_proxy's import library so its import table names d3d11.dll)
extern "C" __declspec(dllimport) int ProxyExport();

int main()
{
  return ProxyExport();
}
