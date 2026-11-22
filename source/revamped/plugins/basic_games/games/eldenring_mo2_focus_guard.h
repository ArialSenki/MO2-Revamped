// When the bridge's "start minimized" guard ends by itself.
//
// On Windows the guard holds until the user picks the game: a click on it,
// Alt+Tab or Win+Tab, or its taskbar button. Under Wine or Proton none of
// those reach the game's hooks (Gamescope has no taskbar and no Shell_TrayWnd,
// and its focus changes aren't user activations), so the guard never ended
// and the game stayed minimized for good. There it ends a while after the
// game first showed its window minimized, and the window is restored.
//
// Kept free of Windows headers so tests/eldenring/focus_guard builds it alone.
#pragma once

namespace mo2_focus_guard {

// how long the game stays minimized under Wine, from its first window (the
// logos and the first load), or from the guard's start if it shows none
constexpr unsigned long long kWineReleaseAfterMs = 15000;

// Whether the guard ends by itself at `now` (GetTickCount64 ticks).
// `guard_start` is when the guard began, `first_show` when the game first
// showed a window minimized (0: not yet).
inline bool release_by_timeout(bool wine, unsigned long long guard_start,
                               unsigned long long first_show,
                               unsigned long long now) {
  if (!wine) {
    return false;
  }
  const unsigned long long from = first_show != 0 ? first_show : guard_start;
  if (from == 0 || now < from) {
    return false;
  }
  return now - from >= kWineReleaseAfterMs;
}

}  // namespace mo2_focus_guard
