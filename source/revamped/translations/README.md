# Revamped translations

MO2's translation files (`organizer_<language>.qm`) predate MO2 Revamped, so
they do not translate text added by Revamped or the Revamped plugins.
`revamped_<language>.ts` supplies those strings for Spanish, German, French,
Italian and Brazilian Portuguese. Other MO2 languages show the English text for
Revamped additions.

- Application strings use their usual Qt context and are only listed when the
  original MO2 file for that language does not translate them.
- Plugin strings use one context per plugin (`_TR_CONTEXT` in the plugin, or
  `EldenRingArchiveInstaller` for the archive installer). The source text is
  English and is passed to `QCoreApplication.translate()`.

Edit the files with Qt Linguist or any text editor. `Build-Release.ps1`
compiles them with `lrelease` into `translations/revamped_<language>.qm`, and
MO2 installs that translator together with its own when the interface language
is set or changed. Keep every `%1`, `%n` and `{name}` placeholder from the
source text.
