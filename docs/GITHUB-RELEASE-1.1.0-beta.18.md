MO2 Revamped 1.1.0 beta 18 is the **newest version**, still a beta. It is
beta 17 plus a **safe Merge** into an installed mod (a cancelled, failed or
interrupted merge no longer leaves the mod half changed), **zEdit merges** for
older Wabbajack lists, a **seamless theme change**, and small fixes. The
**Nexus sign-in** in the browser is built in but stays hidden until Nexus
issues MO2 Revamped's client id.

## Download

- `MO2-Revamped-Setup-1.1.0-beta.18.exe`: run it and choose **Update MO2
  Revamped** to keep an existing installation and its data (beta 17 or
  earlier), or **Install MO2 Revamped** for a new one.
- `SHA256SUMS.txt`: checksum of the installer.

The installer is not digitally signed, so Windows SmartScreen may warn about it.

## Installing mods

- **Merge into an installed mod** no longer leaves it half changed. The archive
  is extracted to a folder of its own first (`.mo2-merging`, beside the mods
  folder), and its files go into the mod only once all of them are there.
  Before, cancelling a merge halfway left the mod with part of the new files
  and some of its own already replaced. If a file can't be moved (a program
  has it open, say), the files already moved go back and the replaced ones
  return, and the message says so.
- **A merge interrupted** by closing or a crash is offered at the next start:
  **Finish** adds the rest of the files, **Undo** puts the mod back as it was,
  **Later** asks again next time.

## Wabbajack lists

- **zEdit merges (MergedPatch)** are built now, as Wabbajack does: the merge's
  source plugins are joined in the list's order and the list's patch is
  applied, then checked by its hash and size. Until now they were left out and
  listed under "Not done yet". Only **older lists** have these merges; newer
  lists don't use them and install as before.

## Fixes

- **Changing the theme** is seamless, as when Windows changes its own: the
  open windows keep the old theme until the new one is in place everywhere,
  then it fades in. With animations off (in MO2 or in Windows) it changes in
  one step.
- **Root copy:** a game at a drive root (`D:\`) no longer gives paths like
  `D://file` in the log and the messages.
- **Elden Ring bridge:** a leftover Wine registry key on a real Windows no
  longer makes the bridge act as under Wine, where it showed the game too
  early at startup.
- **Without the Windows icon fonts:** the Python tools' row glyphs use the
  theme's muted tone like MO2's own, and the summary tiles no longer make a
  new glyph picture for every frame of their color animation.
- **Baldur's Gate 3:** "Convert JSONS to YAML" reads folded (`>`) and kept
  (`|+`) blocks as YAML does.

## Coming: Nexus sign-in

- **Sign in with Nexus Mods** in the browser, without copying an API key
  (OAuth 2.0 with PKCE), is in this build but **not visible yet**. It appears
  in Settings, Nexus once Nexus registers MO2 Revamped; until then the API key
  works exactly as before. Signing in will keep the stored API key, and the
  key is used again after signing out.

## From beta 17

Everything else is the same as in beta 17 and beta 16, including what was
tested and the known issues; see the
[beta 16 notes](https://github.com/ArialSenki/MO2-Revamped/releases/tag/v1.1.0-beta.16).

The full list is in the [changelog](https://github.com/ArialSenki/MO2-Revamped/blob/v1.1.0-beta.18/CHANGELOG.md).

MO2 Revamped is an independent community edition of Mod Organizer 2. It is not
affiliated with the Mod Organizer 2 team, Wabbajack, Nexus Mods, Nolvus, CD PROJEKT RED,
Larian Studios, FromSoftware or Bandai Namco Entertainment.
