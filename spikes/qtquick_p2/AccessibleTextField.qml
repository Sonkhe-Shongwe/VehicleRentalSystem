import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// No custom key handlers: cursor, Tab and selection remain Qt-native.
TextField {
    property string fieldPurpose: ""
    activeFocusOnTab: true
    Layout.minimumHeight: 44
    leftPadding: 12
    rightPadding: 12
    DesignTokens { id: fieldTokens }
    background: Rectangle {
        radius: 8
        color: fieldTokens.surface
        border.color: parent.activeFocus ? fieldTokens.navy : fieldTokens.border
        border.width: parent.activeFocus ? 2 : 1
    }
    Accessible.name: fieldPurpose
}
