import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A readable, non-interactive status chip derived from the existing C++ flag.
Rectangle {
    id: pill
    property bool available: false
    readonly property string statusText: available ? "Available" : "Rented"
    implicitWidth: statusLabel.implicitWidth + 20
    implicitHeight: 24
    Layout.preferredWidth: implicitWidth
    Layout.preferredHeight: implicitHeight
    radius: 9
    color: available ? tokens.mint : "#EAF1FF"
    border.color: available ? "#C3E8DC" : "#D7E5FC"
    border.width: 1
    Accessible.role: Accessible.StaticText
    Accessible.name: statusText + " vehicle"
    Label {
        id: statusLabel
        anchors.centerIn: parent
        text: pill.statusText
        color: pill.available ? tokens.teal : tokens.rented
        font.pixelSize: 11
        font.weight: Font.DemiBold
    }
    DesignTokens { id: tokens }
}
