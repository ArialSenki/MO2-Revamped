import QtQuick 2.7

// rectangle for description texts
Rectangle {
    id: descriptionPanel
    property alias text: textBox.text
    property alias continueVisible: continueIcon.visible
    property int innerWidth;
    signal clicked

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 100
    width: textBox.width + 30
    height: textBox.height + 8
    border.color: "black"
    border.width: 3
    smooth: true
    //opacity: 0.9
    z: 10000
    color: "#FF707070"

    Image {
        id: continueIcon
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 5
        anchors.right: parent.right
        anchors.rightMargin: 5
        source: "qrc:/MO/gui/next"

        SequentialAnimation on opacity {
            loops: 2
            running: continueIcon.visible

            PauseAnimation { duration: 350 }
            NumberAnimation { easing.type: Easing.InOutSine; duration: 450; to: 0.84 }
            NumberAnimation { easing.type: Easing.InOutSine; duration: 550; to: 1.0 }
        }
    }

    Text {
        id: textBox
        text: ""
        font.pointSize: 12
        font.bold: false
        width: descriptionPanel.innerWidth
        font.family: "Courier"
        wrapMode: Text.WordWrap
        anchors.centerIn: parent
    }

    MouseArea {
        id: clickArea
        anchors.fill: parent
        onClicked: parent.clicked()
    }
}
