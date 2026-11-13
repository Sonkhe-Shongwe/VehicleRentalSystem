import QtQuick
import QtQuick.Controls

// Shared styling for permanent deletion actions.
Button {
    id: control
    hoverEnabled: true
    activeFocusOnTab: true
    implicitHeight: 44
    // Keep the press state distinct from hover, including with reduced motion.
    readonly property int hoverFeedbackMs: motionPolicy.reducedMotion ? 0 : colors.hoverFeedbackMs
    readonly property int pressFeedbackMs: motionPolicy.reducedMotion ? 0 : colors.pressFeedbackMs
    readonly property color feedbackSurface: !control.enabled ? colors.disabledSurface :
        control.down ? colors.dangerPressed :
        control.hovered ? colors.dangerHover : colors.danger
    background: Rectangle {
        objectName: "destructiveActionFeedbackSurface"
        radius: 8
        color: control.feedbackSurface
        border.width: control.activeFocus ? colors.focusWidth : 0
        border.color: colors.navy
        // Reduced motion and disabled commands snap without a cosmetic delay.
        Behavior on color {
            enabled: !motionPolicy.reducedMotion && control.enabled
            ColorAnimation {
                duration: control.down ? control.pressFeedbackMs : control.hoverFeedbackMs
                easing.type: Easing.OutCubic
            }
        }
    }
    contentItem: Text {
        text: control.text
        color: control.enabled ? "#FFFFFF" : colors.disabledInk
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    DesignTokens { id: colors }
}
