# Mod Organizer 2 translations

The Mod Organizer 2 2.5.2 translations, one `.ts` file per `.qm` file the
2.5.2 release shipped (the organizer, uibase, the plugins, the Python plugins
and the NXM handler). MO2 translates them on Transifex and released only the
compiled `.qm` files; these sources were written from those files with Qt's
`lconvert`, and `lrelease` turns each one back into the identical `.qm`.

`Build-Release.ps1` compiles them into `translations/`. Only Qt's own `qt_`
and `qtbase_` files still come from the runtime; the Revamped strings are in
`source/revamped/translations/`.
