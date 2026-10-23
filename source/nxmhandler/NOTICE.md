# NXM handler: provenance and license

This directory contains the source of `nxmhandler.exe`, the program Windows
starts for `nxm://` links. It is based on
[ModOrganizer2/modorganizer-nxmhandler](https://github.com/ModOrganizer2/modorganizer-nxmhandler)
at commit `9876f57cd508c56028ec16ba0967ef1ead6fc47d` (2022-04-28), the
revision used for Mod Organizer 2 2.5.2 (file version 1.2.0.0).

The upstream repository has no license file or per-file license headers. It is
a Mod Organizer 2 component (version resource company: Tannin) and is
distributed with Mod Organizer 2 under the GNU General Public License version 3
or later; see [`../modorganizer/LICENSE`](../modorganizer/LICENSE). Copyright
remains with its original contributors.

## MO2 Revamped changes

- The handler list window (`handlerwindow.ui`) and the "add binary" dialog
  (`addbinarydialog.ui`, `addbinarydialog.*`) are replaced by Fluent windows
  built in code: `handlerwindow.*`, `handlereditdialog.*`, `linktestdialog.*`,
  `installinfo.*` and `fluentstyle.*` (light/dark from Windows, Windows accent).
- `main.cpp`: Fluent look and translations (`qtbase_`, `nxmhandler_`,
  `revamped_<language>.qm`); a link for a game no program handles offers to
  choose one and then forwards the link; when Mod Organizer registers at start
  and the user answered "No", the question is not repeated until the Windows
  registration changes (`declinedcommand` in `nxmhandler.ini`).
- `handlerstorage.*`: the link routing is also available as a static function
  for the window's link preview; the routing itself is unchanged.
- `version.rc`: file version 1.2.0.1, product name "NXM Handler (MO2 Revamped)".
- `src/CMakeLists.txt`: links `dwmapi` and `version`; Release builds map local
  paths with `/pathmap`.
