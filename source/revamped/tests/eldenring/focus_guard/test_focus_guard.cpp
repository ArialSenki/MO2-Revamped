// When the bridge's "start minimized" guard ends by itself
// (plugins/basic_games/games/eldenring_mo2_focus_guard.h).
//   powershell -File run.ps1 -ZigPath <zig 0.13.0>
#include <cstdio>

#include "../../../plugins/basic_games/games/eldenring_mo2_focus_guard.h"

namespace {
int failures = 0;

void check(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}
}  // namespace

int main() {
  using mo2_focus_guard::kWineReleaseAfterMs;
  using mo2_focus_guard::release_by_timeout;
  const unsigned long long start = 1000;
  const unsigned long long shown = 4000;

  // Windows: only the user ends it, however long it takes
  check(!release_by_timeout(false, start, shown, shown + 10 * kWineReleaseAfterMs),
        "never by time on Windows");
  check(!release_by_timeout(false, start, 0, start + 10 * kWineReleaseAfterMs),
        "never by time on Windows without a window");

  // Wine: counted from the first minimized window
  check(!release_by_timeout(true, start, shown, shown + kWineReleaseAfterMs - 1),
        "Wine: not before its time from the first window");
  check(release_by_timeout(true, start, shown, shown + kWineReleaseAfterMs),
        "Wine: at its time from the first window");
  check(!release_by_timeout(true, start, shown, start + kWineReleaseAfterMs),
        "Wine: the first window, not the guard's start, counts");

  // Wine, no window shown yet: from the guard's start
  check(!release_by_timeout(true, start, 0, start + kWineReleaseAfterMs - 1),
        "Wine: not before its time from the guard's start");
  check(release_by_timeout(true, start, 0, start + kWineReleaseAfterMs),
        "Wine: at its time from the guard's start");

  // odd clocks never end it early
  check(!release_by_timeout(true, start, shown, shown - 1), "a tick before the window");
  check(!release_by_timeout(true, 0, 0, kWineReleaseAfterMs * 2), "no start at all");

  // long enough for the logos and the first load, short enough to notice
  check(kWineReleaseAfterMs >= 5000 && kWineReleaseAfterMs <= 60000, "a sensible time");

  if (failures == 0) {
    std::printf("focus guard: all checks passed\n");
  }
  return failures == 0 ? 0 : 1;
}
