import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Shared presentation only. Each caller supplies its C++ verified read state;
// this component never calculates occupancy, performs SQL or issues a write.
Rectangle {
    id: card
    property string heading: ""
    property string summary: ""
    property bool snapshotAvailable: false
    property string metricObjectName: ""
    property string retryObjectName: ""
    property string retryLabel: "Retry read"
    signal retryRequested()

    DesignTokens { id: palette }
    Layout.fillWidth: true
    Layout.preferredHeight: 115
    radius: 12
    color: palette.surface
    border.color: palette.border

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 15
        spacing: 8
        Label {
            Layout.fillWidth: true
            text: card.heading
            color: palette.teal
            font.bold: true
            Accessible.name: text
        }
        Label {
            objectName: card.metricObjectName
            Layout.fillWidth: true
            text: card.summary
            color: palette.navy
            font.pixelSize: 18
            font.bold: true
            wrapMode: Text.WordWrap
            Accessible.name: text
        }
        Button {
            objectName: card.retryObjectName
            text: card.retryLabel
            visible: !card.snapshotAvailable
            onClicked: card.retryRequested()
            Accessible.name: text
        }
    }
}
