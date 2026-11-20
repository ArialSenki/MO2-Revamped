import QtQuick
import MO2Revamped

// a mouse area that shows the Widgets tool tip (in the theme's style) after a
// moment of hovering, as the controls of the Widgets windows do
MouseArea {
    id: area

    property string tip
    property bool tipShown: false

    hoverEnabled: true

    function hideTip() {
        tipTimer.stop()
        if (tipShown) {
            Fluent.hideToolTip()
            tipShown = false
        }
    }

    function showTip() {
        if (tip === "")
            return
        const p = area.mapToGlobal(area.mouseX, area.mouseY)
        Fluent.showToolTip(tip, p.x, p.y)
        tipShown = true
    }

    onContainsMouseChanged: {
        if (containsMouse && tip !== "")
            tipTimer.restart()
        else
            hideTip()
    }
    onPressed: hideTip()
    onTipChanged: if (tipShown) showTip()
    Component.onDestruction: hideTip()

    Timer {
        id: tipTimer
        interval: 650
        onTriggered: area.showTip()
    }
}
