import QtQuick 2.7

// a Windows 11 tooltip
Rectangle {
  SystemPalette { id: palette; colorGroup: SystemPalette.Active }
  readonly property bool dark: palette.window.hslLightness < 0.5

  color: dark ? "#2C2C2C" : "#F9F9F9"
  width: childrenRect.width + 16
  height: childrenRect.height + 12
  z: 20000
  clip: false
  radius: 4
  border.color: dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.10)
  border.width: 1
  property alias text: tooltipText.text
  Text {
    id: tooltipText
    font.family: "Segoe UI Variable Text"
    font.pointSize: 9
    wrapMode: Text.WordWrap
    anchors.left: parent.left
    anchors.top: parent.top
    anchors.leftMargin: 8
    anchors.topMargin: 6
    z: parent.z
    color: palette.windowText
    onTextChanged: {
      if (width > 240)
        width = 240
    }
  }
  visible: false
}
