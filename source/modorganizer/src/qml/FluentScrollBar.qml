import QtQuick
import MO2Revamped

// a Windows 11 scroll bar for a Flickable: a thin line that widens under the mouse
Item {
    id: bar

    property Flickable flickable
    readonly property bool active: area.containsMouse || area.pressed
    readonly property real ratio: flickable ? flickable.visibleArea.heightRatio : 1

    width: 12
    visible: flickable !== null && flickable.contentHeight > flickable.height + 1

    Rectangle {
        id: thumb
        readonly property real travel: bar.height - height
        width: bar.active ? 6 : 2
        x: bar.width - width - 3
        radius: width / 2
        height: Math.max(28, bar.height * bar.ratio)
        y: bar.ratio >= 1 ? 0
           : Math.max(0, Math.min(travel, travel * bar.flickable.visibleArea.yPosition / (1 - bar.ratio)))
        color: Fluent.muted
        opacity: bar.active ? 0.8 : 0.5
        Behavior on width { NumberAnimation { duration: Fluent.fast; easing.type: Easing.OutCubic } }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        property real grab: 0

        function scrollTo(y) {
            const f = bar.flickable
            const span = f.contentHeight - f.height
            if (thumb.travel <= 0 || span <= 0)
                return
            f.contentY = Math.max(0, Math.min(span, (y - grab) / thumb.travel * span))
        }
        onPressed: mouse => {
            grab = (mouse.y >= thumb.y && mouse.y <= thumb.y + thumb.height) ? mouse.y - thumb.y : thumb.height / 2
            scrollTo(mouse.y)
        }
        onPositionChanged: mouse => { if (pressed) scrollTo(mouse.y) }
    }
}
