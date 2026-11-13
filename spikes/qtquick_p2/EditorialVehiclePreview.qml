import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Read-only available-vehicle teaser. Vehicle IDs, names and rates arrive
// from the unfiltered C++ fleet snapshot, not Fleet's transient search proxy.
Button {
    id: card
    property string vehicleId: ""
    property string vehicleName: ""
    property string vehicleType: "Car"
    property real dailyRate: 0
    property int photoRevision: 0
    property bool hasPhoto: false
    property bool compact: false
    signal selected(string id)
    objectName: "editorialReadyVehicle"
    Layout.fillWidth: true
    Layout.minimumHeight: compact ? 77 : 93
    padding: 0
    hoverEnabled: true
    activeFocusOnTab: true
    Accessible.name: "View available " + vehicleName + ", " + vehicleId +
                     ", rate R " + Number(dailyRate).toFixed(2) + " per day"
    Accessible.description: "Open the real Fleet view and select this vehicle"
    onClicked: selected(vehicleId)
    background: Rectangle {
        color: card.activeFocus ? "#E8F5F0" :
               card.hovered ? "#F1F5F8" : "#FFFFFF"
        radius: 9
        border.color: card.activeFocus ? "#17324D" : "transparent"
        border.width: card.activeFocus ? 3 : 1
    }
    contentItem: RowLayout {
        spacing: 12
        Rectangle {
            Layout.preferredWidth: card.compact ? 77 : 101
            Layout.preferredHeight: card.compact ? 61 : 75
            radius: 9
            color: "#EDF3F6"
            clip: true
            Image {
                id: photo
                anchors.fill: parent
                anchors.margins: 3
                source: card.hasPhoto
                    ? "image://fleetphotos/" + card.vehicleId +
                      "?v=" + card.photoRevision : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true
                visible: card.hasPhoto && status === Image.Ready
                Accessible.ignored: true
            }
            VehicleSilhouette {
                anchors.centerIn: parent
                scale: card.compact ? 0.43 : 0.54
                vehicleType: card.vehicleType
                visible: !photo.visible
            }
            Label {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: 5
                visible: photo.visible
                text: "REFERENCE"
                font.pixelSize: 8
                font.bold: true
                color: "#17324D"
                padding: 2
                background: Rectangle { radius: 3; color: "#F6F8FA" }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3
            Label {
                text: card.vehicleName
                color: "#17324D"
                font.pixelSize: 14
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                text: card.vehicleId + "  ·  Available"
                color: "#0F766E"
                font.pixelSize: 11
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Label {
                text: "R " + Number(card.dailyRate).toFixed(2) + " / day"
                color: "#17324D"
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
        }
        R2LucideIcon {
            objectName: "overviewReadyChevron"
            iconName: "chevron-right"
            tone: "emerald"
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            Layout.alignment: Qt.AlignVCenter
            Accessible.ignored: true
        }
    }
}
