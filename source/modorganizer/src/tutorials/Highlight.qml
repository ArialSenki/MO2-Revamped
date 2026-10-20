import QtQuick 2.7

// marks the control a step talks about: an accent outline, green when the
// step waits for a click on it; it breathes twice when it appears
Rectangle {
    id: highlightFrame
    property bool clickTarget: false

    SystemPalette { id: palette; colorGroup: SystemPalette.Active }
    readonly property bool dark: palette.window.hslLightness < 0.5

    radius: 6
    color: "transparent"
    height: 100
    border.color: clickTarget ? (dark ? "#6CCB5F" : "#0F7B0F")
                              : (dark ? "#60CDFF" : "#005FB8")
    border.width: 2
    opacity: 1.0

    SequentialAnimation {
        loops: 2
        running: highlightFrame.visible

        PauseAnimation { duration: 250 }
        NumberAnimation {
            target: highlightFrame
            property: "opacity"
            easing.type: Easing.InOutSine
            duration: 450
            to: 0.6
        }
        NumberAnimation {
            target: highlightFrame
            property: "opacity"
            easing.type: Easing.InOutSine
            duration: 550
            to: 1.0
        }
    }
}
