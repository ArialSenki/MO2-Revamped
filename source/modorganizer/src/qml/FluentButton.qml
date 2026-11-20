import QtQuick
import MO2Revamped

// a Windows 11 button: standard, primary (accent) or flat, with an optional
// glyph before its text
Rectangle {
    id: button

    property string text
    property string glyph
    // after the text, such as the chevron of a button with a menu
    property string trailingGlyph
    property color glyphColor: primary ? Fluent.onAccent : Fluent.text
    property string tip
    property bool primary: false
    property bool flat: false
    property int minWidth: 0
    property int horizontalPadding: 14
    signal clicked()

    readonly property bool hovered: area.containsMouse
    readonly property bool down: area.pressed

    implicitWidth: Math.max(minWidth, content.implicitWidth + 2 * horizontalPadding)
    implicitHeight: 32
    radius: 4
    activeFocusOnTab: enabled

    color: {
        if (!enabled)
            return flat ? "transparent" : Fluent.controlDisabled
        if (primary)
            return down ? Fluent.accentFillPressed : hovered ? Fluent.accentFillHover : Fluent.accentFill
        if (flat)
            return down ? Fluent.subtlePressed : hovered ? Fluent.subtleHover : "transparent"
        return down ? Fluent.controlPressed : hovered ? Fluent.controlHover : Fluent.control
    }
    border.width: flat ? 0 : 1
    border.color: primary && enabled ? color : Fluent.controlBorder
    Behavior on color { ColorAnimation { duration: Fluent.fast } }

    // the darker lower edge of a raised control
    Rectangle {
        visible: !button.flat && !button.primary && button.enabled && !button.down
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 3; rightMargin: 3 }
        height: 1
        color: Fluent.controlBorderBottom
    }

    // the keyboard focus ring
    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: button.radius + 3
        color: "transparent"
        border.width: 2
        border.color: Fluent.text
        visible: button.activeFocus && (button.focusReason === Qt.TabFocusReason ||
                                        button.focusReason === Qt.BacktabFocusReason)
    }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 8
        FluentGlyph {
            visible: button.glyph !== ""
            text: button.glyph
            size: 14
            color: button.enabled ? button.glyphColor : Fluent.disabledText
            anchors.verticalCenter: parent.verticalCenter
        }
        FluentText {
            visible: button.text !== ""
            text: button.text
            color: !button.enabled ? Fluent.disabledText
                 : button.primary ? Fluent.onAccent
                 : (button.down ? Fluent.muted : Fluent.text)
            anchors.verticalCenter: parent.verticalCenter
        }
        FluentGlyph {
            visible: button.trailingGlyph !== ""
            text: button.trailingGlyph
            size: 10
            color: button.enabled ? Fluent.muted : Fluent.disabledText
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    FluentToolTipArea {
        id: area
        anchors.fill: parent
        tip: button.tip
        cursorShape: button.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: if (button.enabled) button.clicked()
    }

    Keys.onSpacePressed: if (enabled) clicked()
    Keys.onReturnPressed: if (enabled) clicked()
    Keys.onEnterPressed: if (enabled) clicked()

    Accessible.role: Accessible.Button
    Accessible.name: text !== "" ? text : tip
    Accessible.description: tip
    Accessible.onPressAction: if (enabled) clicked()
}
