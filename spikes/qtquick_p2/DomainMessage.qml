import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Displays C++ domain results without recomputing business values.
Label {
    id: message
    property bool emphasized: false
    DesignTokens { id: tokens }
    Layout.fillWidth: true
    wrapMode: Text.WordWrap
    color: emphasized ? tokens.navy : tokens.ink
    font.bold: emphasized
    Accessible.name: text
}
