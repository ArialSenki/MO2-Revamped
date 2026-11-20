import QtQuick
import MO2Revamped

// a Windows 11 search box: the accent line below while it has the focus, and a
// button that clears it
Rectangle {
    id: field

    property alias text: input.text
    property string placeholder
    signal edited(string text)

    function focusAndSelect() {
        input.forceActiveFocus()
        input.selectAll()
    }

    implicitHeight: 32
    radius: 4
    color: Fluent.input
    border.width: 1
    border.color: Fluent.controlBorder

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 1; rightMargin: 1 }
        height: input.activeFocus ? 2 : 1
        radius: 1
        color: input.activeFocus ? Fluent.accent : Fluent.controlBorderBottom
        Behavior on color { ColorAnimation { duration: Fluent.fast } }
    }

    FluentGlyph {
        id: icon
        text: "\uE721"
        size: 13
        anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
    }

    TextInput {
        id: input
        anchors { left: icon.right; leftMargin: 8; right: clear.left; rightMargin: 4; verticalCenter: parent.verticalCenter }
        color: Fluent.text
        selectionColor: Fluent.accentFill
        selectedTextColor: Fluent.onAccent
        font.family: Fluent.fontFamily
        font.pointSize: Fluent.fontSize
        clip: true
        selectByMouse: true
        onTextEdited: field.edited(text)
        Keys.onEscapePressed: event => {
            if (text !== "") {
                text = ""
                field.edited("")
                event.accepted = true
            } else {
                event.accepted = false
            }
        }
        Accessible.role: Accessible.EditableText
        Accessible.name: field.placeholder

        FluentText {
            anchors.fill: parent
            text: field.placeholder
            color: Fluent.disabledText
            visible: input.text === ""
        }
    }

    FluentIconButton {
        id: clear
        visible: input.text !== ""
        width: visible ? 26 : 0
        height: 24
        glyph: "\uE711"
        anchors { right: parent.right; rightMargin: 4; verticalCenter: parent.verticalCenter }
        activeFocusOnTab: false
        onClicked: {
            input.text = ""
            field.edited("")
            input.forceActiveFocus()
        }
    }
}
