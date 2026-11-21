# Nexus collections and Wabbajack lists

Both install a whole modded setup in one go: they download, install and order
everything for you. A Nexus account is needed for files from Nexus; with
Premium, MO2 downloads them on its own.

## Nexus collections

### Find a collection

- Tools (or the **Nexus** button) > **Nexus collections** > **Browse
  collections…** searches Nexus' collections, with a game filter, sorting
  (**Most endorsed**, **Most downloaded**, **Recently updated**) and **Adult
  content**. **Open the collection** opens the one you chose.
- **Download a Nexus collection…** takes a collection link.
- A collection link from the browser opens it too.

### The collection window

It shows **What it needs**, the author's notes and the mods.

- **Install into:** **Current profile**, **New profile** or **New instance**.
- **Separators by category** puts the mods under category separators.
- **Automatic (Premium)** or **Page by page on Nexus** chooses how files are
  downloaded.

Then three steps: download, install, and order and finish.

**Checks before installing.** On the first **Start**, MO2 looks for things
that may get in the way: another game version, a missing script extender or
DLC, mods removed from Nexus, mods already active in the profile, or many files
to click with a free account. You can start anyway and fix them later.

### Free and Premium

- **Premium** downloads on its own, up to three files at a time.
- **Free:** MO2 opens each file's page on Nexus (**Open next on Nexus**) and
  you press **Slow download** there. **Open the next page automatically** saves
  you a click per file.

### What is applied

- The FOMOD choices the author made. Mods where they couldn't all be applied
  are listed at the end under "Installer options chosen by hand".
- Binary patches, checked against the original file first.
- Exact copies of the author's install, Root folders and file overrides.
- INI tweaks, after asking you.
- The mod order and plugin order rules. When the collection sets no plugin
  order, **Sort plugins with LOOT** is offered at the end.
- Files from other sites, or from another game's page on Nexus, are left for
  you to install by hand; the window says which.

### Pause, continue and update

- **Pause**, **Continue** and **Retry failed** work at any time. Per file,
  **Mark as done** and **Skip** help when a file can't be downloaded.
- Progress is saved in the instance's `collections` folder. An unfinished
  collection is offered again on the next start: **Continue**, **Not now** or
  **Don't ask again**.
- Notices wait while a collection installs.
- When there is a new revision, **Update to revision <N>** updates it. Mods
  that are no longer in the collection go to the Recycle Bin.

## Wabbajack lists

[Wabbajack](https://www.wabbajack.org) lists are full modded setups. MO2
Revamped installs them itself, without the Wabbajack app, each in an instance
of its own.

### Find a list

- Tools > **Wabbajack lists** > **Browse Wabbajack lists…** shows Wabbajack's
  gallery by game. **Open the list** downloads the `.wabbajack` file and opens
  it.
- **Open a Wabbajack list…** takes a `.wabbajack` file you already have.

### Download the archives

The list window shows **What it needs** and each archive with its status.

- Choose the **Downloads folder**. An existing Wabbajack downloads folder can
  be reused: archives are recognized by their hash.
- **Download** fetches the archives; stopped downloads continue where they
  left off.
- **Check by hash** verifies the archives already in the folder.
- MO2 downloads from Nexus, direct links and Wabbajack's servers. Without
  Premium, each Nexus file is one click on its page (**Open next on Nexus**).
- Files from Google Drive, MEGA, MediaFire, ModDB, forums or Bethesda.net are
  marked **By hand**: double-click to open them in the browser. Finished
  browser downloads are picked up from the downloads folder. Right-click a file
  > **Choose the downloaded file…** if it was saved somewhere else.
- Game files the list needs are taken from your game folder.

### Install the list

**Install** puts the list in a **new instance**: give it an **Instance name**
and an **Install folder**. The folder must be empty and can't be the game's
folder, the downloads folder or a drive's root. At the end, **Restart in the
new instance** switches to it.

- The list's own profile, load order and programs come with it.
- MO2 plugins that a list brings are set aside and offered, not installed on
  their own.
- An install that stopped can be continued later.

### Manual steps

Some lists leave files for the game folder. **Manual steps of this list…**, in
the same menu, copies them, backs up what they replace and runs their patchers
when you ask.

### Common problems

- A list needs its game installed and a lot of free space; MO2 says how much
  before it starts.
- When Nexus' request limit is reached, MO2 waits or you continue later:
  nothing is lost.
- A file gone from its server has to be found by hand.
- A step MO2 Revamped can't do yet is reported as "Not done yet by MO2
  Revamped: …". Report the list and the step in an
  [issue](https://github.com/ArialSenki/MO2-Revamped/issues).
