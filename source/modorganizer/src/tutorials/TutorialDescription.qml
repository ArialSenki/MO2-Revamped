import QtQuick 2.7

// Windows 11 teaching tip: the step text on a rounded flyout surface, with a
// chevron when a click on it moves to the next step
Rectangle {
    id: descriptionPanel
    property alias text: textBox.text
    property alias continueVisible: continueIcon.visible
    property int innerWidth;
    signal clicked

    SystemPalette { id: palette; colorGroup: SystemPalette.Active }
    readonly property bool dark: palette.window.hslLightness < 0.5

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 100
    width: textBox.width + 32 + (continueIcon.visible ? 24 : 0)
    height: textBox.height + 24
    radius: 8
    border.width: 1
    border.color: dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.10)
    color: dark ? "#2C2C2C" : "#F9F9F9"
    z: 10000

    // a new step slides up a little while it fades in
    onTextChanged: entrance.restart()
    ParallelAnimation {
        id: entrance
        NumberAnimation { target: descriptionPanel; property: "opacity"; from: 0; to: 1; duration: 167; easing.type: Easing.OutCubic }
        NumberAnimation { target: slide; property: "y"; from: 8; to: 0; duration: 250; easing.type: Easing.OutCubic }
    }
    transform: Translate { id: slide; y: 0 }

    Text {
        id: continueIcon
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 12
        text: ""
        font.family: "Segoe Fluent Icons"
        font.pixelSize: 14
        color: palette.windowText
    }

    Text {
        id: textBox
        text: ""
        width: descriptionPanel.innerWidth
        x: 16
        anchors.verticalCenter: parent.verticalCenter
        font.family: "Segoe UI Variable Text"
        font.pointSize: 10
        color: palette.windowText
        wrapMode: Text.WordWrap
        lineHeight: 1.15
    }

    MouseArea {
        id: clickArea
        anchors.fill: parent
        cursorShape: continueIcon.visible ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: parent.clicked()
    }
}
