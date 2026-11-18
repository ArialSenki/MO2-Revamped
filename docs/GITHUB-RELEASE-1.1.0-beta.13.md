MO2 Revamped 1.1.0 beta 13 is the **newest version**, still a beta. It comes from
a **full audit of every window and option** and two rounds of fixes from
hands-on testing. It adds **Nolvus support**, and is **much faster with very
large lists**. It also handles **Nexus' request limit** without failing
downloads, and has quieter notices that never take the focus.

## Beta status: what works and in which games

This is a **beta**. It works for daily use in the games below, but expect bugs.
Back up your saves and your mods folder first, and please report what fails.

**Tested by hand with this version, with the real game and mods**

- **Skyrim Special Edition**: SKSE64 2.3.1 started from MO2.
- **Nolvus Awakening 6.0.21** (3,597 mods), as a linked instance. The game
  started from MO2 with SKSE and its 200 plugins loaded.
- **Cyberpunk 2077** 2.5: the Welcome to Night City collection (364 mods)
  installed. The game started with RED4ext and its plugins loaded.
- **Baldur's Gate 3** (DX11): started from MO2 with a 278-mod list.

**Tested with earlier betas, not played again with this one:** Elden Ring and
Elden Ring Nightreign, Fallout 4, Fallout 3 and Fallout: New Vegas.

**Not in this beta:**
- installing Wabbajack lists natively (a separate experimental build);
- installing or updating Nolvus itself (use the Nolvus Dashboard);
- the Microsoft Store / Game Pass editions.

## Download

- `MO2-Revamped-Setup-1.1.0-beta.13.exe`: run it and choose **Install MO2
  Revamped**, or **Update MO2 Revamped** to keep an existing installation and
  its data.
- `SHA256SUMS.txt`: checksum of the installer.

The installer is not digitally signed, so Windows SmartScreen may warn about it.

## Nolvus

- **Find Nolvus:** Instances finds your Nolvus install, in `C:\Nolvus` or a
  folder you pick.
- **Open in MO2 Revamped:** makes a linked instance. It uses Nolvus' mods and
  game but its own copy of the profiles, so the Nolvus Dashboard keeps updating
  the list.
- **Transfer to MO2 Revamped:** turns a Nolvus install into a normal instance.
  You can move it (seconds on the same drive), copy it (sizes and free space
  are shown first) or keep it in place. Every step can be undone.
- **Warnings before running:** plain warnings for Nolvus requirements, which
  never block.
- **Downloads:** the Nolvus archives show as a read-only group.

## Faster with very large lists

Measured with Nolvus Awakening (3,597 mods, ~3,480 plugins, 546,000 files):

- **Turning all mods back on:** 24.6 s → 4.2 s.
- **Run:** the window no longer freezes while the virtual folder is prepared
  (14.5 s → about 1 s). Cancel works.
- **Data tab, first search letter:** 7.0 s → 1.0 s, with ~250 MB less memory.
- **Mod information for a mod of 64,000 files:** 0.85 s → 0.29 s.

Compared with the original Mod Organizer 2.5.2 that Nolvus installs, on the same
list (medians of three alternating runs each, warm disk):

- **Start until the list shows:** 6.1 s → 2.9 s.
- **Turning one mod on or off:** 1.2–2.1 s → 0.23–0.35 s.
- **Turning all mods back on:** 29.6 s → 8.7 s.
- **Refresh (F5):** the window is blocked for 2.6 s → 1.0 s.
- **Switching profiles:** the window is blocked for 3.6 s → 1.3 s.
- **Run:** the window is blocked for 9.1 s → 0.8 s. The whole start takes
  about 2 s longer (14 s instead of 12 s).
- **Memory after using everything:** 2.0 GB → 1.7 GB.

## Nexus

- **Request limit:** when Nexus answers "too many requests" (429), MO2 pauses
  and goes on by itself, with one notice, and no mod is marked as failed.
  Collections now make fewer requests.
- **Identification:** MO2 Revamped names itself to Nexus as MO2 Revamped.
- **Connect to Nexus** (website sign-in) is hidden until Nexus registers MO2
  Revamped's own sign-in.
  - Until then, enter your personal API key in **Settings > Nexus**.
  - A key you already saved keeps working.
- **Endorse:**
  - explains each refusal in one sentence;
  - is hidden for the mod's author;
  - never sends twice.
- **Adult collections** are hidden in the collection browser unless you ask
  for them.

## Also in this beta

- **Collections:**
  - a Wabbajack-style window that stays open with live progress;
  - one notice to start and one to end, none per mod;
  - a free-space check before starting.
- **Notices:** they never take the focus or cover a dialog's buttons. The
  Notifications window explains its checks for your game.
- **Downloads:** grouped by origin or into your own categories, and large
  folders open about four times faster.
- **Baldur's Gate 3:** LSLib updates, and JSON to YAML works. Reparsing pak
  metadata is eight times faster.
- **Interface:** the floating log, windows that open off screen, the plugin
  counter and the plugin Name column were fixed. The mouse wheel no longer
  switches tabs.
- **Help:** a help guide, and a bug report that hides your personal data.

## Known issues

- Dragging downloads between groups with the mouse may not drop. Use
  **Move to category** in the menu instead.

The full list is in the [changelog](https://github.com/ArialSenki/MO2-Revamped/blob/v1.1.0-beta.13/CHANGELOG.md).

MO2 Revamped is an independent community edition of Mod Organizer 2. It is not
affiliated with the Mod Organizer 2 team, Nexus Mods, Nolvus, FromSoftware or
Bandai Namco Entertainment.
