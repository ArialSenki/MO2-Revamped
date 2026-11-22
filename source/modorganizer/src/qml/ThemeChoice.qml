import QtQuick
import MO2Revamped

// MO2 Revamped: the first-start theme question in Qt Quick. A tile for each
// theme with a little window drawn in its colors; the chosen one has the accent
// stroke and the check mark. As the Widgets version: the arrow keys move the
// choice, Enter or a double click takes it, Escape closes without one
Rectangle {
    id: root

    required property ThemeChoiceBridge bridge

    // FluentContentDialog's measures: 20 above the heading, 16 between the
    // text and the tiles, 24 below them, and the button band; the height
    // follows the text, so a translation that wraps one more line still fits
    implicitWidth: 600
    implicitHeight: 20 + texts.implicitHeight + 16 + 196 + 24 + band.height
    color: Fluent.surface
    focus: true

    Keys.onLeftPressed: bridge.chosen = Math.max(0, bridge.chosen - 1)
    Keys.onRightPressed: bridge.chosen = Math.min(bridge.themes.length - 1, bridge.chosen + 1)
    Keys.onReturnPressed: bridge.accept()
    Keys.onEnterPressed: bridge.accept()
    Keys.onEscapePressed: bridge.reject()

    // the content dialog's text, as FluentContentDialog sets it
    Column {
        id: texts
        anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: 24; rightMargin: 24; topMargin: 20 }
        spacing: 8
        FluentText {
            width: parent.width
            text: root.bridge.heading
            font.pointSize: 14
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
            elide: Text.ElideNone
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }
        FluentText {
            width: parent.width
            text: root.bridge.description
            color: Fluent.muted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
    }

    Row {
        id: tiles
        anchors { left: parent.left; right: parent.right; top: texts.bottom; leftMargin: 24; rightMargin: 24; topMargin: 16 }
        spacing: 6

        Repeater {
            model: root.bridge.themes
            delegate: Item {
                id: tile
                required property var modelData
                required property int index
                readonly property bool chosen: index === root.bridge.chosen
                readonly property var c: modelData

                width: (tiles.width - tiles.spacing * (root.bridge.themes.length - 1)) / root.bridge.themes.length
                height: 196

                Accessible.role: Accessible.RadioButton
                Accessible.name: c.name
                Accessible.checkable: true
                Accessible.checked: chosen
                Accessible.onPressAction: root.bridge.chosen = index

                // the card: a faint fill of the text color, deeper under the mouse
                Rectangle {
                    id: card
                    width: parent.width
                    height: parent.height
                    y: area.containsMouse && !area.pressed ? -2 : 0
                    radius: 8
                    color: area.pressed ? Fluent.subtlePressed : area.containsMouse ? Fluent.subtleSelected : Fluent.subtleHover
                    border.width: tile.chosen ? 2 : 1
                    border.color: tile.chosen ? Fluent.accent : Fluent.controlBorder
                    Behavior on y { NumberAnimation { duration: Fluent.fast; easing.type: Easing.OutCubic } }
                    Behavior on color { ColorAnimation { duration: Fluent.fast } }
                    Behavior on border.color { ColorAnimation { duration: Fluent.fast } }

                    // the little window of the theme
                    Rectangle {
                        id: preview
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        height: 140
                        radius: 5
                        color: tile.c.background
                        border.color: tile.c.line
                        clip: true

                        // title bar dots
                        Row {
                            x: 10; y: 8; spacing: 4
                            Repeater {
                                model: 3
                                Rectangle { width: 6; height: 6; radius: 3; color: Qt.rgba(tile.c.muted.r, tile.c.muted.g, tile.c.muted.b, 0.6) }
                            }
                        }

                        // navigation: the selected item filled and marked by the accent
                        Item {
                            id: nav
                            x: 8; y: 24
                            width: parent.width * 0.28
                            height: parent.height - 32
                            Rectangle {
                                width: parent.width; height: 14; radius: 3
                                color: Qt.rgba(tile.c.text.r, tile.c.text.g, tile.c.text.b, 0.09)
                                Rectangle { x: 1; y: 4; width: 3; height: 6; radius: 1.5; color: tile.c.accent }
                                Rectangle { x: 8; y: 5.5; width: parent.width * 0.6; height: 3; radius: 1.5; color: tile.c.text }
                            }
                            Repeater {
                                model: 3
                                Rectangle {
                                    required property int index
                                    x: 8; y: 21 + index * 13
                                    width: nav.width * (0.66 - index * 0.1); height: 3; radius: 1.5
                                    color: Qt.rgba(tile.c.muted.r, tile.c.muted.g, tile.c.muted.b, 0.65)
                                }
                            }
                        }

                        // the content card: a heading, two lines, a switch and a button
                        Rectangle {
                            anchors { left: nav.right; right: parent.right; top: nav.top; bottom: parent.bottom; leftMargin: 8; rightMargin: 8; bottomMargin: 8 }
                            radius: 4
                            color: tile.c.card
                            border.color: tile.c.line
                            Rectangle { x: 9; y: 10; width: parent.width * 0.5; height: 5; radius: 2.5; color: tile.c.text }
                            Rectangle { x: 9; y: 22; width: parent.width * 0.72; height: 3; radius: 1.5; color: Qt.rgba(tile.c.muted.r, tile.c.muted.g, tile.c.muted.b, 0.75) }
                            Rectangle { x: 9; y: 30; width: parent.width * 0.56; height: 3; radius: 1.5; color: Qt.rgba(tile.c.muted.r, tile.c.muted.g, tile.c.muted.b, 0.75) }
                            // a switch, on
                            Rectangle {
                                x: 9; y: 44; width: 22; height: 11; radius: 5.5
                                color: tile.c.accent
                                Rectangle { x: 13; y: 2.5; width: 6; height: 6; radius: 3; color: tile.c.onAccent }
                            }
                            Rectangle {
                                anchors { right: parent.right; bottom: parent.bottom; margins: 8 }
                                width: 34; height: 12; radius: 6
                                color: tile.c.accent
                                Rectangle { anchors.centerIn: parent; width: 16; height: 2; radius: 1; color: tile.c.onAccent }
                            }
                        }
                    }

                    // the name and the check mark of the chosen theme
                    FluentText {
                        anchors { left: parent.left; right: badge.left; top: preview.bottom; bottom: parent.bottom; leftMargin: 14; rightMargin: 6 }
                        text: tile.c.name
                        font.weight: tile.chosen ? Font.DemiBold : Font.Normal
                    }
                    Rectangle {
                        id: badge
                        anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.bottom; verticalCenterOffset: -23 }
                        width: 20; height: 20; radius: 10
                        color: Fluent.accent
                        scale: tile.chosen ? 1 : 0
                        opacity: tile.chosen ? 1 : 0
                        Behavior on scale { NumberAnimation { duration: Fluent.normal; easing.type: Easing.OutBack } }
                        Behavior on opacity { NumberAnimation { duration: Fluent.fast } }
                        FluentGlyph {
                            anchors.centerIn: parent
                            text: "\uE73E"
                            size: 11
                            color: Fluent.onAccent
                        }
                    }
                }

                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.bridge.chosen = tile.index
                        root.forceActiveFocus()
                    }
                    onDoubleClicked: {
                        root.bridge.chosen = tile.index
                        root.bridge.accept()
                    }
                }
            }
        }
    }

    // the button in the darker band below, as in a content dialog: 24 around
    // it, and as wide as the band, as FluentContentDialog's buttons are
    Rectangle {
        id: band
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 80
        color: Fluent.window
        Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top } height: 1; color: Fluent.divider }
        FluentButton {
            anchors { left: parent.left; right: parent.right; leftMargin: 24; rightMargin: 24; verticalCenter: parent.verticalCenter }
            primary: true
            minWidth: 110
            text: root.bridge.acceptLabel
            onClicked: root.bridge.accept()
        }
    }
}
