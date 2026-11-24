import QtQuick
import MO2Revamped

// a Segoe Fluent Icons glyph, given as its one-character string. Without the
// icon fonts (a Wine prefix has neither) it is the glyph's SVG, in its color
Item {
    id: glyph
    property string text
    property int size: 16
    // the size in points instead, when set (as the summary tiles' badges)
    property real pointSize: 0
    property color color: Fluent.muted
    // the color the picture is asked for in: the one the color is going to, not
    // each step of an animation on it (a new picture per frame, every one kept
    // in the image cache). Set by who animates the color; the picture then
    // changes once, at the start
    property color pictureColor: color

    readonly property int pixels: pointSize > 0 ? Math.round(pointSize * 4 / 3) : size

    implicitWidth: Fluent.glyphFont ? label.implicitWidth : pixels
    implicitHeight: Fluent.glyphFont ? label.implicitHeight : pixels
    Accessible.ignored: true

    Text {
        id: label
        visible: Fluent.glyphFont
        anchors.fill: parent
        text: Fluent.glyphFont ? glyph.text : ""
        color: glyph.color
        font: glyph.pointSize > 0 ? Qt.font({ family: Fluent.iconFamily, pointSize: glyph.pointSize })
                                  : Qt.font({ family: Fluent.iconFamily, pixelSize: glyph.size })
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        textFormat: Text.PlainText
        Accessible.ignored: true
    }

    Image {
        visible: !Fluent.glyphFont && glyph.text !== ""
        anchors.centerIn: parent
        width: glyph.pixels
        height: glyph.pixels
        sourceSize: Qt.size(glyph.pixels, glyph.pixels)
        smooth: true
        cache: true
        source: Fluent.glyphFont || glyph.text === "" ? ""
              : "image://fluentglyph/" + glyph.text.charCodeAt(0).toString(16).toUpperCase()
                + "/" + String(glyph.pictureColor).substring(1)
        Accessible.ignored: true
    }
}
