import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Native Button keeps accessible workspace names even in compact icon mode.
// Arrows change focus only. Enter/Space activate a page deliberately.
Button {
    id: nav
    required property string modelData
    required property int index
    property bool currentWorkspace: false
    property bool sidebarCollapsed: false
    // Root animation is shared by all delegates; no independent per-row clocks.
    property real collapseProgress: sidebarCollapsed ? 1 : 0
    property var workspaceItems: null
    signal enterContent()
    Layout.fillWidth: true
    Layout.minimumHeight: 48
    implicitHeight: 48
    objectName: "workspaceNav" + modelData
    // Keep stable Fleet object names and icon mapping for source-backed navigation.
    text: modelData === "Fleet" ? "Vehicles" : modelData
    activeFocusOnTab: true
    hoverEnabled: true
    readonly property int hoverFeedbackMs: motionPolicy.reducedMotion ? 0 : colors.hoverFeedbackMs
    readonly property int pressFeedbackMs: motionPolicy.reducedMotion ? 0 : colors.pressFeedbackMs
    readonly property color feedbackSurface:
        nav.currentWorkspace ? (nav.down ? colors.shellSelectedPressed : colors.shellSelected) :
        nav.down ? colors.shellNavPressed :
        nav.hovered ? colors.shellHover : "transparent"
    ToolTip.visible: nav.sidebarCollapsed && nav.hovered
    ToolTip.text: nav.text
    Accessible.role: Accessible.RadioButton
    Accessible.name: nav.text + " workspace"
    Accessible.checkable: true
    Accessible.checked: nav.currentWorkspace
    Accessible.description: "Up and Down focus workspaces. Enter activates; Right enters the active page."
    Keys.onPressed: (event) => {
        if (event.modifiers !== Qt.NoModifier || !nav.activeFocus)
            return
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            nav.clicked()
            event.accepted = true
            return
        }
        let target = -1
        if (event.key === Qt.Key_Up && nav.index > 0)
            target = nav.index - 1
        else if (event.key === Qt.Key_Down &&
                 nav.index < (nav.workspaceItems ? nav.workspaceItems.count - 1 : 2))
            target = nav.index + 1
        else if (event.key === Qt.Key_Right && nav.currentWorkspace) {
            nav.enterContent()
            event.accepted = true
            return
        } else
            return
        if (nav.workspaceItems && target >= 0) {
            const sibling = nav.workspaceItems.itemAt(target)
            if (sibling && sibling.visible && sibling.enabled)
                sibling.forceActiveFocus(Qt.TabFocusReason)
        }
        event.accepted = true
    }
    background: Rectangle {
        objectName: "workspaceNavFeedbackSurface"
        radius: colors.componentRadius
        // Active selection/accessible state remains instant; only nonactive
        // hover and press feedback is gently blended on the dark shell.
        color: nav.feedbackSurface
        Behavior on color {
            enabled: !motionPolicy.reducedMotion && !nav.currentWorkspace
            ColorAnimation {
                duration: nav.down ? nav.pressFeedbackMs : nav.hoverFeedbackMs
                easing.type: Easing.OutCubic
            }
        }
        border.width: nav.activeFocus ? colors.focusWidth :
                      (nav.currentWorkspace ? 1 : 0)
        border.color: nav.activeFocus ? colors.shellFocus :
                      colors.shellSelectedBorder
        Rectangle {
            visible: nav.currentWorkspace
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: 24
            radius: 2
            color: colors.shellAction
        }
    }
    contentItem: Item {
        R2LucideIcon {
            objectName: "workspaceIcon" + nav.modelData
            visible: nav.modelData !== "SQLite Lab"
            iconName: nav.modelData === "Fleet" ? "car-front" :
                      nav.modelData === "Rentals" ? "clipboard-list" : "layout-grid"
            tone: nav.currentWorkspace ? "active" : "inverse"
            width: 22
            height: 22
            x: 12 + ((parent.width - width) / 2 - 12) * nav.collapseProgress
            y: (parent.height - height) / 2
        }
        SidebarGlyph {
            objectName: "workspaceLabGlyph"
            visible: nav.modelData === "SQLite Lab"
            kind: nav.modelData
            glyphColor: nav.currentWorkspace ? colors.shellSelectedInk :
                        colors.shellText
            width: 22
            height: 22
            x: 12 + ((parent.width - width) / 2 - 12) * nav.collapseProgress
            y: (parent.height - height) / 2
        }
        Text {
            visible: nav.collapseProgress < 0.70
            opacity: Math.max(0, 1 - 1.6 * nav.collapseProgress)
            x: 46
            y: 0
            width: Math.max(0,parent.width - x - 7)
            height: parent.height
            text: nav.text
            color: nav.currentWorkspace ? colors.shellSelectedInk :
                   colors.shellText
            font.family: colors.shellFontFamily
            font.pixelSize: 14
            font.bold: nav.currentWorkspace
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }
    DesignTokens { id: colors }
}
