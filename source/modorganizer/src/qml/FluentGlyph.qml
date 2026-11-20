import QtQuick
import MO2Revamped

// a Segoe Fluent Icons glyph, given as its one-character string
Text {
    property int size: 16
    color: Fluent.muted
    font.family: Fluent.iconFamily
    font.pixelSize: size
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    textFormat: Text.PlainText
    Accessible.ignored: true
}
