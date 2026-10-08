import QtQuick 2.7

Rectangle {
    id: highlightFrame
    radius: 10
    color: "transparent"
    height: 100
    border.color: "black"
    border.width: 3
    opacity: 0.84
    smooth: true

    SequentialAnimation {
        loops: 2
        running: highlightFrame.visible

        PauseAnimation { duration: 250 }
        NumberAnimation {
            target: highlightFrame
            property: "opacity"
            easing.type: Easing.InOutSine
            duration: 450
            to: 0.74
        }
        NumberAnimation {
            target: highlightFrame
            property: "opacity"
            easing.type: Easing.InOutSine
            duration: 550
            to: 0.84
        }
    }
}
