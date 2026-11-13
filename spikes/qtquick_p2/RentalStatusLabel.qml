import QtQuick
import QtQuick.Controls

// Display status only; C++ remains responsible for the rental lifecycle.
Rectangle {
    id: pill
    property string statusValue: ""
    readonly property bool activeRental: statusValue === "Active"
    readonly property bool returnedRental: statusValue === "Returned"
    DesignTokens { id: tokens }
    implicitWidth: Math.max(84, caption.implicitWidth + 22)
    implicitHeight: 28
    radius: 14
    color: activeRental ? tokens.mint : returnedRental ? tokens.ice :
           statusValue === "Archived" ? tokens.quietSurface : tokens.surface
    border.color: activeRental ? tokens.teal : tokens.border
    border.width: 1
    Accessible.role: Accessible.StaticText
    Accessible.name: statusValue + " rental status"
    Label {
        id: caption
        anchors.centerIn: parent
        text: pill.statusValue
        color: pill.activeRental ? tokens.teal :
               pill.returnedRental ? tokens.rented : tokens.navy
        font.pixelSize: 12
        font.weight: Font.DemiBold
    }
}
