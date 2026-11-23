# Themes of your own

MO2 Revamped has two themes, Light and Dark. You can add your own: a `.qss`
style sheet in the `stylesheets` folder of the MO2 Revamped installation. It
shows in **Settings > Theme** as "*name* (on Dark)" or "*name* (on Light)".

## The quick way: the theme editor

In **Settings > Theme**, **New theme...** opens a small editor: a name, the theme
it goes on top of, and six colors to pick. MO2 shows the colors as you change
them, Save writes the theme to the `stylesheets` folder and puts it in use, and
Cancel puts back the theme you had. **Edit theme...** opens the selected theme
of your own in the same editor (rules you wrote by hand are kept), and **Open
themes folder** shows the files, to share a theme or add rules to it.

## How a theme works

A theme of your own always loads **on top of** Light or Dark. It doesn't have to
style every window, only what you want to change. Two things shape it:

1. **Its colors**, in a comment at the top. They recolor the whole interface,
   including the parts MO2 paints itself (switches, cards, the navigation pane,
   icons), which a style sheet can't reach.
2. **Its own rules**, below. They come after Light's or Dark's, so they win at
   the same [specificity](https://doc.qt.io/qt-6/stylesheet-syntax.html#conflict-resolution).

MO2 reloads the theme each time you save the file, so you can edit it with MO2
open.

## The header

```css
/* mo2r-theme
   name: Ember
   base: dark
   accent: #E0A84F
   window: #1C1814
   surface: #26211B
   border: #4A3F30
   text: #F1E6D2
   muted: #B8A88C
*/
```

| Key | What it is | In Dark | In Light |
|---|---|---|---|
| `name` | The name in Settings. Without it, the file's name. | | |
| `base` | `dark` or `light`: the theme it loads on top of. Without it, MO2 goes by `window`, and then by the file's name. | | |
| `accent` | Selected items, switches, links, focus rings | #4CC2FF | #005FB8 |
| `window` | Window background | #202226 | #F3F3F3 |
| `surface` | Cards, lists, text boxes | #2B2B2B | #FBFBFB |
| `border` | Lines and frames | #3D3D3D | #E5E5E5 |
| `text` | Text and icons | #FFFFFF | #1A1A1A |
| `muted` | Secondary text and icons | #C5C5C5 | #5D5D5D |

Every key is optional. A color you leave out keeps the base theme's.

How the colors apply: each grey of Light or Dark is placed between your
`window`, `surface`, `border`, `muted` and `text` by how light it is, and each
shade of the base accent becomes the same shade of yours. Status colors (red,
green, yellow) and the colors you give mods and separators don't change.

## Your own rules

Anything Qt style sheets can do. Some starting points:

```css
/* the mod list's rows */
QMainWindow#MainWindow ModListView#modList::item { padding: 2px 0; }

/* the switches: these take precedence over the header's accent */
FluentSwitchPainter { qproperty-onColor: #E0A84F; }
QMainWindow#MainWindow ModListView#modList { qproperty-switchOnColor: #E0A84F; }

/* the toasts */
MessageDialog { qproperty-cardColor: #26211B; qproperty-lineColor: #4A3F30; }
```

To find a widget's name, open `stylesheets/dark.qss` (or `Light.qss`) and look
for the rule that styles it: copy its selector into your theme. Images can use
paths relative to your theme's file, as in `url(mytheme/background.png)`.

## Themes made for Mod Organizer 2

A theme made for the classic MO2 interface loads too: drop its `.qss` file (and
its image folder) into `stylesheets` and it appears in Settings > Appearance as
"Name (on Dark)", or on Light if its name says so. Its name comes from the
first line of its opening comment, when that is a short title.

MO2 Revamped draws some parts itself (the cards, switches, navigation, summary
tiles and the main button), and a style sheet's rules don't reach them. Without
a header, their colors are read from the theme's rules: the window and text
from its `QWidget` rule, the surface from its item views or the fill it uses
most, the border from the borders it draws most, the accent from its hover,
focus and selection colors (or any color with a hue), and the muted text from
`:disabled`, made lighter if it would be hard to read. The log says what it
found (at the debug level). When the guess is off, add the header above: its
colors always win.

Rules the old theme has for whole widget types can still clash with Revamped's
layout, for instance `QWidget { background-color: ... }` behind the options in
a card, or margins between buttons. To port such a theme, add the header with
its colors and adjust or remove the rules that clash, marking each change in the
file. Only share a ported theme if its license allows modified copies.

## Keep a copy

Keep a copy of your theme outside the installation folder. Don't edit
`Light.qss` or `dark.qss`: updates replace them.

An example is in [`docs/themes/Ember.qss`](themes/Ember.qss).
