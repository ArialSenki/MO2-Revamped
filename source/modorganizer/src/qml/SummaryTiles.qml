import QtQuick
import MO2Revamped

// the summary row of the main window: active mods, conflicts, Overwrite and
// updates, each with its button (Show, Manage) when it has a use. What the
// tiles say and do comes from the Widgets tiles (SummaryQuickBridge)
Item {
    id: root

    required property SummaryQuickBridge bridge

    readonly property int spacing: 12
    readonly property int count: Math.max(1, bridge.tiles.length)
    readonly property real tileWidth: (width - (count - 1) * spacing) / count

    Item {
        anchors.fill: parent

        Repeater {
            // a count, not the list: the tiles stay when their values change,
            // so the changes can move
            model: root.bridge.tiles.length

            Rectangle {
                id: tile

                required property int index
                readonly property var modelData: root.bridge.tiles[index] ?? ({})

                // whole pixels, as a Widgets layout gives them: a tile at a
                // fraction of a pixel draws soft borders
                x: Math.round(index * (root.tileWidth + root.spacing))
                width: Math.round((index + 1) * (root.tileWidth + root.spacing) - root.spacing) - x
                height: root.height
                radius: 8
                color: Fluent.surface
                border.width: 1
                border.color: Fluent.surfaceBorder

                readonly property string tone: modelData.tone ?? ""

                // a tile's first value and tone appear as they are, once it has
                // one; later changes move a little
                property bool settled: false
                onToneChanged: if (tone !== "") Qt.callLater(() => tile.settled = true)

                Accessible.role: Accessible.StaticText
                Accessible.name: modelData.caption + ": " + modelData.value
                Accessible.description: modelData.help

                Rectangle {
                    id: badge
                    x: 14
                    anchors.verticalCenter: parent.verticalCenter
                    width: 34
                    height: 34
                    radius: 8
                    color: tile.tone === "accent" ? Fluent.tintAccent
                         : tile.tone === "caution" ? Fluent.tintCaution
                         : tile.tone === "critical" ? Fluent.tintCritical
                         : Fluent.tintNeutral
                    Behavior on color { enabled: tile.settled; ColorAnimation { duration: Fluent.normal } }

                    Text {
                        anchors.centerIn: parent
                        text: tile.modelData.glyph
                        font.family: Fluent.iconFamily
                        font.pointSize: 13
                        textFormat: Text.PlainText
                        color: tile.tone === "accent" ? Fluent.accent
                             : tile.tone === "caution" ? Fluent.onTintCaution
                             : tile.tone === "critical" ? Fluent.critical
                             : Fluent.muted
                        Behavior on color { enabled: tile.settled; ColorAnimation { duration: Fluent.normal } }
                        Accessible.ignored: true
                    }
                }

                Column {
                    anchors.left: badge.right
                    anchors.leftMargin: 14
                    anchors.right: chip.visible ? chip.left : parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter

                    FluentText {
                        width: parent.width
                        text: tile.modelData.caption
                        color: Fluent.caption
                        Accessible.ignored: true
                    }
                    FluentText {
                        id: value
                        width: parent.width
                        text: tile.modelData.value
                        font.pointSize: 12
                        font.weight: Font.DemiBold
                        Accessible.ignored: true

                        // a new value rises into place, as Windows 11 counters do
                        transform: Translate { id: rise }
                        onTextChanged: if (tile.settled && Fluent.animated) arrive.restart()
                        ParallelAnimation {
                            id: arrive
                            NumberAnimation {
                                target: value; property: "opacity"
                                from: 0.35; to: 1; duration: Fluent.normal
                                easing.type: Easing.OutCubic
                            }
                            NumberAnimation {
                                target: rise; property: "y"
                                from: 5; to: 0; duration: Fluent.normal
                                easing.type: Easing.OutCubic
                            }
                        }
                    }
                }

                // Show or Manage: a round chip, as QPushButton#fluentStatAction
                Rectangle {
                    id: chip
                    visible: tile.modelData.actionVisible
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    width: chipText.implicitWidth + 28
                    height: 26
                    radius: 13
                    color: chipArea.containsMouse ? Fluent.chipHover : Fluent.chip
                    border.width: 1
                    border.color: Fluent.controlBorder
                    activeFocusOnTab: visible
                    Behavior on color { ColorAnimation { duration: Fluent.fast } }

                    FluentText {
                        id: chipText
                        anchors.centerIn: parent
                        text: tile.modelData.action
                        color: chipArea.containsMouse ? Fluent.text : Fluent.muted
                        Accessible.ignored: true
                    }

                    // the keyboard focus ring
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -3
                        radius: chip.radius + 3
                        color: "transparent"
                        border.width: 2
                        border.color: Fluent.text
                        visible: chip.activeFocus && (chip.focusReason === Qt.TabFocusReason ||
                                                      chip.focusReason === Qt.BacktabFocusReason)
                    }

                    MouseArea {
                        id: chipArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.bridge.act(tile.index)
                    }

                    // once per press, as a button: a held key doesn't repeat it
                    Keys.onSpacePressed: event => { if (!event.isAutoRepeat) root.bridge.act(tile.index) }
                    Keys.onReturnPressed: event => { if (!event.isAutoRepeat) root.bridge.act(tile.index) }
                    Keys.onEnterPressed: event => { if (!event.isAutoRepeat) root.bridge.act(tile.index) }

                    Accessible.role: Accessible.Button
                    Accessible.name: tile.modelData.actionName
                    Accessible.onPressAction: root.bridge.act(tile.index)
                }
            }
        }
    }
}
