import QtQuick

// Shared visual focus/selection state; no keyboard event interception.
// Keyboard focus (Navy) must remain separate from selected record (Mint/Teal).
Rectangle {
    id: frame
    objectName: "sharedFocusFrame"
    property bool keyboardFocused: false
    property bool selected: false
    property bool hovered: false
    property color idleSurface: colors.surface
    property int cornerRadius: 12
    readonly property int selectionFadeMs: motionPolicy.selectionFadeMs

    DesignTokens { id: colors }
    radius: cornerRadius
    color: selected ? colors.mint : hovered ? colors.neutralHover : idleSurface
    border.width: keyboardFocused ? colors.focusWidth :
                  selected ? colors.selectedWidth : 1
    border.color: keyboardFocused ? colors.navy :
                  selected ? colors.teal : colors.border
    // Only the selected surface tint may fade for 90ms in ordinary mode.
    // Navy keyboard focus border and ID changes are always instantaneous.
    // Reduced motion (or unknown OS preference) sets this duration to zero.
    Behavior on color {
        ColorAnimation { duration: frame.selectionFadeMs }
    }
}
