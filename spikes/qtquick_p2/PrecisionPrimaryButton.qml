import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Precision Automotive Light: one persistent primary command, not a page.
// Reuses the existing native booking flow; this component has no domain logic.
Button {
    id: action
    property bool compact: false
    // Visual progress is separate from the immediate accessible button state.
    property real compactProgress: compact ? 1 : 0
    text: "New rental"
    implicitHeight: 48
    Layout.fillWidth: true
    Layout.minimumHeight: 48
    Layout.preferredHeight: 48
    activeFocusOnTab: true
    hoverEnabled: true
    readonly property int hoverFeedbackMs: motionPolicy.reducedMotion ? 0 : colors.hoverFeedbackMs
    readonly property int pressFeedbackMs: motionPolicy.reducedMotion ? 0 : colors.pressFeedbackMs
    readonly property color feedbackSurface: !action.enabled ? colors.disabledSurface :
        action.down ? colors.shellActionPressed :
        action.hovered ? colors.shellActionHover : colors.shellAction
    Accessible.role: Accessible.Button
    Accessible.name: "New rental"
    Accessible.description: "Opens vehicle selection, customer details and booking review"
    ToolTip.visible: compact && hovered
    ToolTip.text: "New rental"
    background: Rectangle {
        objectName: "primaryActionFeedbackSurface"
        radius: colors.componentRadius
        color: action.feedbackSurface
        // Cosmetic color only. The command, focus border and accessibility
        // state remain immediate. Reduced motion has no animation frame.
        Behavior on color {
            enabled: !motionPolicy.reducedMotion
            ColorAnimation {
                duration: action.down ? action.pressFeedbackMs : action.hoverFeedbackMs
                easing.type: Easing.OutCubic
            }
        }
        border.width: action.activeFocus ? colors.focusWidth : 1
        border.color: action.activeFocus ? colors.shellFocus : "#159473"
    }
    contentItem: Item {
        R2LucideIcon {
            objectName: "sidebarNewRentalIcon"
            iconName: "plus"
            tone: "inverse"
            opacity: action.enabled ? 1 : 0.45
            width: 19
            height: 19
            x: 12 + ((parent.width - width) / 2 - 12) * action.compactProgress
            y: (parent.height - height) / 2
        }
        Text {
            visible: action.compactProgress < 0.70
            opacity: Math.max(0, 1 - 1.6 * action.compactProgress)
            text: action.text
            x: 42
            width: Math.max(0, parent.width - x - 6)
            height: parent.height
            color: action.enabled ? "#FFFFFF" : colors.disabledInk
            font.pixelSize: 14
            font.family: colors.shellFontFamily
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }
    DesignTokens { id: colors }
}
