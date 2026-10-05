//TL Overview#5

var tooltips = []

function tooltipWidget(widgetName, explanation, maxheight, clickable) {
  var component = Qt.createComponent("TooltipArea.qml")
  if (component.status === Component.Ready)
    finishCreation(component, widgetName, explanation, maxheight, clickable)
  else
    component.statusChanged.connect(function() {
      finishCreation(component, widgetName, explanation, maxheight, clickable)
    })
}

function finishCreation(component, widgetName, explanation, maxheight, clickable) {
  if (component.status !== Component.Ready)
    return

  var rect = tutorialControl.getRect(widgetName)
  if (typeof clickable === "undefined") clickable = false
  if (typeof maxheight === "undefined" || maxheight === 0) maxheight = rect.height
  var obj = component.createObject(tutToplevel, {
    "x": rect.x, "y": rect.y, "width": rect.width, "height": maxheight
  })
  obj.tooltipText = explanation
  obj.clickable = clickable
  obj.visible = true
  tooltips.push(obj)
}

function tooltipAction(actionName, explanation, maxheight) {
  var rect = tutorialControl.getActionRect(actionName)
  var menuRect = tutorialControl.getMenuRect(actionName)
  var component = Qt.createComponent("TooltipArea.qml")
  if (typeof maxheight === "undefined" || maxheight === 0) maxheight = rect.height
  var obj = component.createObject(tutToplevel, {
    "x": rect.x, "y": rect.y + menuRect.height,
    "width": rect.width, "height": maxheight
  })
  obj.tooltipText = explanation
  obj.visible = true
  tooltips.push(obj)
}

function setupTooltips() {
  for (var tip in tooltips) tooltips[tip].destroy()
  tooltips = []

  tooltipWidget("modList", qsTr("Installed Elden Ring mods are listed here. Checked mods are active in this profile; their files are combined in the virtual game directory."))
  tooltipWidget("profileBox", qsTr("Profiles keep separate enabled mod lists. Elden Ring save isolation is configured from the Profiles window."))
  tooltipWidget("listOptionsBtn", qsTr("Refresh the mod list or manage mod-list backups and updates."))
  tooltipWidget("openFolderMenu", qsTr("Open the folders used by this MO2 instance, including its mods, profiles, downloads, and the Elden Ring game folder."))
  tooltipWidget("restoreModsButton", qsTr("Restore a saved backup of this profile's mod list."))
  tooltipWidget("saveModsButton", qsTr("Create a backup of this profile's mod list."))
  tooltipWidget("activeModsCounter", qsTr("Shows how many mods are enabled in the current Elden Ring profile."))
  tooltipWidget("groupCombo", qsTr("Group the installed mods by category or another available grouping."))
  tooltipWidget("displayCategoriesBtn", qsTr("Show or hide the category and filter panel."))
  tooltipWidget("modFilterEdit", qsTr("Filter installed mods by name."))
  tooltipWidget("qt_tabwidget_tabbar", qsTr("Switch between Data, Saves, and Downloads. Other tabs appear only when supported by the game."), 0, true)
  tooltipWidget("categoriesGroup", qsTr("Select a category or built-in filter to narrow the visible mod list."))
  tooltipWidget("executablesListBox", qsTr("Choose Elden Ring or another executable configured for this instance."))
  tooltipWidget("startButton", qsTr("Launch the selected program through MO2 with this profile's virtual files. The arrow on the right opens shortcut options."))
  tooltipWidget("logList", qsTr("Read messages from MO2 and its active plugins. Error messages can help diagnose an installation or launch issue."))
  tooltipWidget("apistats", qsTr("Shows the remaining Nexus Mods API request allowance for this account."))

  tooltipAction("actionChange_Game", qsTr("Open Manage Instances to switch between independent MO2 setups, such as Elden Ring and Skyrim."))
  tooltipAction("actionInstallMod", qsTr("Install a mod archive stored on your computer."))
  tooltipAction("actionNexus", qsTr("Open the Nexus Mods page for the currently managed game."))
  tooltipAction("actionModPage", qsTr("Open the Nexus Mods page for the selected mod when it has a Nexus link."))
  tooltipAction("actionAdd_Profile", qsTr("Create, copy, rename, or manage profiles and their Elden Ring save settings."))
  tooltipAction("action_Refresh", qsTr("Refresh the active profile and virtual game file list."))
  tooltipAction("actionModify_Executables", qsTr("Add or edit programs that can be launched through this MO2 instance."))
  tooltipAction("actionTool", qsTr("Open the tools enabled for this game instance."))
  tooltipAction("actionSettings", qsTr("Change settings for this MO2 installation."))
  tooltipAction("actionHelp", qsTr("Open interface help, this Elden Ring overview, MO2 documentation, and support links."))

  switch (tutorialControl.getTabName("tabWidget")) {
    case "dataTab":
      tooltipWidget("dataTree", qsTr("Browse the virtual Elden Ring game directory. The Mod column shows which mod supplies each file; select a row to inspect its source."))
      break
    case "savesTab":
      tooltipWidget("savegameList", qsTr("Browse saves visible to the current profile. Hover a save briefly to see its details."))
      break
    case "downloadTab":
      tooltipWidget("downloadView", qsTr("Downloaded archives appear here. Double-click one to install it. The Elden Ring installer offers standard MO2 layout or the original archive paths."))
      break
  }
}

function getTutorialSteps() {
  tutorialCanceller.visible = false
  return [function() {
    tutorial.text = qsTr("Click to quit")
    setupTooltips()
    onTabChanged(setupTooltips)
    waitForClick()
  }]
}
