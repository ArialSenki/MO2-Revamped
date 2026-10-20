import QtQuick 2.7

// "Exit tutorial" as a small subtle button below the teaching tip
Rectangle {
    id: cancellerPanel
    property alias text: textBox.text
    property alias cancelVisible: cancelIcon.visible
    property int innerWidth;
    signal clicked

    SystemPalette { id: palette; colorGroup: SystemPalette.Active }
    readonly property bool dark: palette.window.hslLightness < 0.5

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 70
    width: textBox.implicitWidth + 44
    height: 28
    radius: 4
    border.width: 1
    border.color: dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.10)
    color: clickArea.containsMouse ? (dark ? "#323232" : "#F0F0F0")
                                   : (dark ? "#2C2C2C" : "#F9F9F9")
    z: 10000

    Behavior on color { ColorAnimation { duration: 83 } }

    Text {
        id: cancelIcon
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 10
        text: ""
        font.family: "Segoe Fluent Icons"
        font.pixelSize: 10
        color: palette.windowText
    }

    Text {
        id: textBox
        text: qsTr("Exit Tutorial")
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: cancelIcon.right
        anchors.leftMargin: 8
        font.family: "Segoe UI Variable Text"
        font.pointSize: 9
        color: palette.windowText
    }

    MouseArea {
        id: clickArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: parent.clicked()
    }
}
