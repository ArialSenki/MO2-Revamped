import QtQuick
import MO2Revamped

// MO2 Revamped: the Instances window in Qt Quick. Everything it shows comes from
// the window's controls through the bridge, and everything it does goes back to
// them (InstancesQuickBridge), so it works as the Widgets version does
Rectangle {
    id: root

    required property InstancesQuickBridge bridge
    readonly property var s: bridge.state

    color: Fluent.window
    focus: true

    Keys.onEscapePressed: bridge.press("close")

    // the selected instance in view, as the Widgets list scrolls to it
    function showCurrent() {
        if (bridge.currentRow >= 0 && bridge.currentRow < list.count)
            list.positionViewAtIndex(bridge.currentRow, ListView.Contain)
    }

    Connections {
        target: root.bridge
        function onSearchRequested() { search.focusAndSelect() }
        function onStateChanged() { root.showCurrent() }
        function onRowsChanged() { Qt.callLater(root.showCurrent) }
    }

    // ---- the instances --------------------------------------------------

    Rectangle {
        id: listCard
        anchors { left: parent.left; top: parent.top; bottom: footer.top; leftMargin: 16; topMargin: 16 }
        width: 280
        radius: 8
        color: Fluent.surface
        border.color: Fluent.surfaceBorder

        Item {
            id: listHeader
            anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: 14; rightMargin: 8; topMargin: 10 }
            height: 34

            FluentText {
                id: listTitle
                text: root.s.title
                font.pointSize: 12
                font.weight: Font.DemiBold
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
            Rectangle {
                anchors { left: listTitle.right; leftMargin: 8; verticalCenter: parent.verticalCenter }
                height: 20
                width: Math.max(20, countText.implicitWidth + 12)
                radius: 10
                color: Fluent.subtleSelected
                FluentText {
                    id: countText
                    anchors.centerIn: parent
                    text: root.s.count
                    font.pointSize: Fluent.fontSize * 0.9
                    color: Fluent.muted
                }
            }
            FluentButton {
                id: grouping
                flat: true
                horizontalPadding: 10
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                text: root.s.grouping
                trailingGlyph: "\uE70D"
                tip: root.s.groupingTip
                Accessible.name: root.s.groupingName
                onClicked: {
                    const p = grouping.mapToGlobal(0, grouping.height + 2)
                    root.bridge.viewMenu(p.x, p.y)
                }
            }
        }

        FluentSearchField {
            id: search
            anchors { left: parent.left; right: parent.right; top: listHeader.bottom; leftMargin: 8; rightMargin: 8; topMargin: visible ? 6 : 0 }
            visible: root.s.filterVisible
            height: visible ? 32 : 0
            placeholder: root.s.filterPlaceholder
            text: root.s.filterText
            onEdited: t => root.bridge.setFilter(t)
            onFocusMoved: f => root.bridge.setSearchFocus(f)
        }

        ListView {
            id: list
            anchors { left: parent.left; right: parent.right; top: search.bottom; bottom: nolvus.top; margins: 6; topMargin: 6 }
            clip: true
            model: root.bridge.rows
            activeFocusOnTab: true
            focus: true
            keyNavigationEnabled: false
            boundsBehavior: Flickable.StopAtBounds
            Accessible.role: Accessible.List
            Accessible.name: root.s.title

            // the next instance row from the current one, skipping headings
            function step(delta) {
                const rows = root.bridge.rows
                let i = root.bridge.currentRow
                for (let k = 0; k < rows.length; ++k) {
                    i += delta
                    if (i < 0 || i >= rows.length)
                        return
                    if (!rows[i].header) {
                        root.bridge.selectRow(rows[i].row)
                        return
                    }
                }
            }
            function edge(last) {
                const rows = root.bridge.rows
                for (let k = 0; k < rows.length; ++k) {
                    const r = rows[last ? rows.length - 1 - k : k]
                    if (!r.header) {
                        root.bridge.selectRow(r.row)
                        return
                    }
                }
            }
            function rowY(index) {
                const rows = root.bridge.rows
                let y = 0
                for (let k = 0; k < index && k < rows.length; ++k)
                    y += rows[k].header ? 30 : 60
                return y
            }
            function menuAt(index, x, y) {
                if (index < 0)
                    return
                const p = list.contentItem.mapToGlobal(x, y)
                root.bridge.rowMenu(root.bridge.rows[index].row, p.x, p.y)
            }

            Keys.onUpPressed: step(-1)
            Keys.onDownPressed: step(1)
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Home) { edge(false); event.accepted = true }
                else if (event.key === Qt.Key_End) { edge(true); event.accepted = true }
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    if (root.bridge.currentRow >= 0)
                        root.bridge.activateRow(root.bridge.rows[root.bridge.currentRow].row)
                    event.accepted = true
                } else if (event.key === Qt.Key_Menu ||
                           (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                    const i = root.bridge.currentRow
                    menuAt(i, 40, rowY(i) + 40)
                    event.accepted = true
                }
            }

            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool current: index === root.bridge.currentRow
                width: ListView.view.width
                height: modelData.header ? 30 : 60

                Accessible.role: modelData.header ? Accessible.StaticText : Accessible.ListItem
                Accessible.name: modelData.text
                Accessible.description: modelData.caption
                Accessible.selected: current
                Accessible.onPressAction: root.bridge.selectRow(modelData.row)

                Rectangle {
                    visible: !row.modelData.header
                    anchors { fill: parent; margins: 2 }
                    radius: 4
                    color: row.current ? Fluent.subtleSelected : Fluent.subtleHover
                    opacity: row.current || area.containsMouse ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Fluent.fast } }

                    Rectangle {
                        x: 0
                        width: 3
                        radius: 1.5
                        height: row.current ? 16 : 0
                        anchors.verticalCenter: parent.verticalCenter
                        color: Fluent.accent
                        Behavior on height { NumberAnimation { duration: Fluent.normal; easing.type: Easing.OutCubic } }
                    }
                }

                FluentText {
                    visible: row.modelData.header
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 10; rightMargin: 8; bottomMargin: 5 }
                    text: row.modelData.text
                    color: Fluent.muted
                    font.weight: Font.DemiBold
                    font.pointSize: Fluent.fontSize * 0.92
                }

                Image {
                    id: icon
                    visible: !row.modelData.header && row.modelData.icon !== ""
                    source: row.modelData.icon
                    width: 32
                    height: 32
                    sourceSize: Qt.size(64, 64)
                    smooth: true
                    anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                }
                Column {
                    visible: !row.modelData.header
                    anchors { left: icon.visible ? icon.right : parent.left; leftMargin: icon.visible ? 12 : 16; right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                    spacing: 1
                    FluentText {
                        width: parent.width
                        text: row.modelData.text
                        font.weight: row.current ? Font.DemiBold : Font.Normal
                    }
                    FluentText {
                        width: parent.width
                        visible: text !== ""
                        text: row.modelData.caption
                        color: Fluent.muted
                        font.pointSize: Fluent.fontSize * 0.9
                    }
                }

                FluentToolTipArea {
                    id: area
                    anchors.fill: parent
                    tip: row.modelData.tip
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: mouse => {
                        list.forceActiveFocus()
                        if (!row.modelData.header)
                            root.bridge.selectRow(row.modelData.row)
                    }
                    onClicked: mouse => {
                        if (mouse.button === Qt.RightButton) {
                            const p = row.mapToGlobal(mouse.x, mouse.y)
                            root.bridge.rowMenu(row.modelData.row, p.x, p.y)
                        }
                    }
                    onDoubleClicked: mouse => {
                        if (mouse.button === Qt.LeftButton && !row.modelData.header)
                            root.bridge.activateRow(row.modelData.row)
                    }
                }
            }
        }

        FluentScrollBar {
            flickable: list
            anchors { right: list.right; top: list.top; bottom: list.bottom; rightMargin: -4 }
        }

        // the Nolvus instances found that have no instance here yet
        Column {
            id: nolvus
            anchors { left: parent.left; right: parent.right; bottom: newButton.top; leftMargin: 12; rightMargin: 12; bottomMargin: visible ? 8 : 0 }
            visible: root.s.nolvusVisible
            height: visible ? implicitHeight : 0
            spacing: 6

            Rectangle { width: parent.width; height: 1; color: Fluent.divider }
            FluentText {
                text: root.s.nolvusTitle
                color: Fluent.muted
                font.weight: Font.DemiBold
                font.pointSize: Fluent.fontSize * 0.92
            }
            Repeater {
                model: root.s.nolvus
                delegate: Loader {
                    id: entry
                    required property var modelData
                    width: nolvus.width
                    sourceComponent: entry.modelData.kind === "button" ? nolvusButton : nolvusLabel
                    Component {
                        id: nolvusLabel
                        FluentText {
                            width: nolvus.width
                            text: entry.modelData.text
                            textFormat: Text.StyledText
                            wrapMode: Text.Wrap
                            elide: Text.ElideNone
                            FluentToolTipArea { anchors.fill: parent; tip: entry.modelData.tip; acceptedButtons: Qt.NoButton }
                        }
                    }
                    Component {
                        id: nolvusButton
                        FluentButton {
                            width: nolvus.width
                            text: entry.modelData.text
                            tip: entry.modelData.tip
                            enabled: entry.modelData.enabled
                            onClicked: root.bridge.nolvusPress(entry.modelData.id)
                        }
                    }
                }
            }
        }

        FluentButton {
            id: newButton
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 8 }
            visible: root.s["new"].visible
            height: visible ? 32 : 0
            glyph: "\uE710"
            text: root.s["new"].text
            tip: root.s["new"].tip
            enabled: root.s["new"].enabled
            onClicked: root.bridge.press("new")
        }
    }

    // ---- the selected instance -------------------------------------------

    Rectangle {
        id: detailsCard
        anchors { left: listCard.right; right: parent.right; top: parent.top; bottom: footer.top; leftMargin: 8; rightMargin: 16; topMargin: 16 }
        radius: 8
        color: Fluent.surface
        border.color: Fluent.surfaceBorder
        clip: true

        // nothing selected
        Column {
            visible: !root.s.hasSelection
            anchors.centerIn: parent
            width: parent.width - 80
            spacing: 12
            FluentGlyph {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "\uE8B7"
                size: 40
                color: Fluent.disabledText
            }
            FluentText {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                elide: Text.ElideNone
                text: root.s.emptyText
                color: Fluent.muted
            }
        }

        Item {
            id: details
            visible: root.s.hasSelection
            anchors.fill: parent

            // a newly selected instance rises into place
            property int shownSelection: -2
            transform: Translate { id: rise }
            Connections {
                target: root.bridge
                function onStateChanged() {
                    if (root.s.selection !== details.shownSelection) {
                        details.shownSelection = root.s.selection
                        entrance.restart()
                    }
                }
            }
            ParallelAnimation {
                id: entrance
                NumberAnimation { target: details; property: "opacity"; from: 0.0; to: 1.0; duration: Fluent.normal; easing.type: Easing.OutCubic }
                NumberAnimation { target: rise; property: "y"; from: 10; to: 0; duration: Fluent.normal; easing.type: Easing.OutCubic }
            }

            Item {
                id: head
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: 84

                Image {
                    id: headIcon
                    visible: root.s.icon !== ""
                    source: root.s.icon
                    width: 44
                    height: 44
                    sourceSize: Qt.size(88, 88)
                    smooth: true
                    anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
                }
                Column {
                    anchors { left: headIcon.visible ? headIcon.right : parent.left; leftMargin: headIcon.visible ? 14 : 20; right: renameButton.left; rightMargin: 10; verticalCenter: parent.verticalCenter }
                    spacing: 2
                    FluentText {
                        width: parent.width
                        text: root.s.name
                        font.pointSize: 15
                        font.weight: Font.DemiBold
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }
                    FluentText {
                        width: parent.width
                        text: root.s.caption
                        color: Fluent.muted
                    }
                }
                FluentIconButton {
                    id: renameButton
                    anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                    glyph: "\uE8AC"
                    tip: root.s.rename.tip
                    enabled: root.s.rename.enabled
                    visible: root.s.rename.visible
                    Accessible.name: root.s.rename.text !== "" ? root.s.rename.text : tip
                    onClicked: root.bridge.press("rename")
                }
            }

            Flickable {
                id: rows
                anchors { left: parent.left; right: parent.right; top: head.bottom; bottom: actions.top }
                contentHeight: rowColumn.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: rowColumn
                    width: rows.width
                    Repeater {
                        model: root.s.details
                        delegate: Item {
                            id: detail
                            required property var modelData
                            width: rowColumn.width
                            height: 62

                            Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top } height: 1; color: Fluent.divider }

                            FluentGlyph {
                                id: detailGlyph
                                width: 24
                                text: detail.modelData.glyph
                                anchors { left: parent.left; leftMargin: 18; verticalCenter: parent.verticalCenter }
                            }
                            Column {
                                anchors { left: detailGlyph.right; leftMargin: 14; right: detailAction.left; rightMargin: 10; verticalCenter: parent.verticalCenter }
                                spacing: 2
                                FluentText {
                                    width: parent.width
                                    text: detail.modelData.title
                                }
                                FluentText {
                                    width: parent.width
                                    text: detail.modelData.value
                                    elide: Text.ElideMiddle
                                    color: detail.modelData.critical ? Fluent.critical : Fluent.muted
                                    FluentToolTipArea { anchors.fill: parent; tip: detail.modelData.tip; acceptedButtons: Qt.NoButton }
                                }
                            }
                            FluentIconButton {
                                id: detailAction
                                visible: detail.modelData.action
                                width: visible ? 32 : 0
                                anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                                glyph: detail.modelData.actionGlyph
                                tip: detail.modelData.actionTip
                                enabled: detail.modelData.actionEnabled
                                onClicked: root.bridge.detailAction(detail.modelData.key)
                            }
                            Accessible.role: Accessible.StaticText
                            Accessible.name: detail.modelData.title + ": " + detail.modelData.value
                        }
                    }
                }
            }
            FluentScrollBar {
                flickable: rows
                anchors { right: rows.right; top: rows.top; bottom: rows.bottom }
            }

            Rectangle {
                id: actions
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 1 }
                height: 58
                color: "transparent"
                Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top } height: 1; color: Fluent.divider }

                Row {
                    anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                    spacing: 8
                    FluentButton {
                        visible: root.s.duplicate.visible
                        glyph: "\uE8C8"
                        text: root.s.duplicate.text
                        tip: root.s.duplicate.tip
                        enabled: root.s.duplicate.enabled
                        onClicked: root.bridge.press("duplicate")
                    }
                    FluentButton {
                        visible: root.s.shortcut.visible
                        glyph: "\uE71B"
                        text: root.s.shortcut.text
                        tip: root.s.shortcut.tip
                        enabled: root.s.shortcut.enabled
                        onClicked: root.bridge.press("shortcut")
                    }
                }
                FluentButton {
                    anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                    visible: root.s["delete"].visible
                    glyph: "\uE74D"
                    glyphColor: Fluent.critical
                    text: root.s["delete"].text
                    tip: root.s["delete"].tip
                    enabled: root.s["delete"].enabled
                    onClicked: root.bridge.press("delete")
                }
            }
        }
    }

    // ---- help, open and close --------------------------------------------

    Item {
        id: footer
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 60

        FluentButton {
            anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
            flat: true
            horizontalPadding: 10
            glyph: "\uE897"
            glyphColor: Fluent.muted
            text: root.s.help.text
            tip: root.s.help.tip
            onClicked: root.bridge.press("help")
        }
        Row {
            anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
            spacing: 8
            FluentButton {
                primary: true
                minWidth: 120
                text: root.s.open.text
                tip: root.s.open.tip
                enabled: root.s.open.enabled
                onClicked: root.bridge.press("open")
            }
            FluentButton {
                minWidth: 102
                text: root.s.close.text
                tip: root.s.close.tip
                onClicked: root.bridge.press("close")
            }
        }
    }
}
