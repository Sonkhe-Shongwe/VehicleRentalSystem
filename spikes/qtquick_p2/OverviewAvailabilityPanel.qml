import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Direction A: compact readiness readout. The authoritative C++ snapshot
// supplies the numerator, denominator and all due/contract summaries.
Rectangle {
    id: panel
    objectName: "overviewAvailabilityPanel"
    property int total: 0
    property int available: 0
    property int rented: 0
    property int carCount: 0
    property int motorcycleCount: 0
    property int dueToday: 0
    property int history: 0
    property string oldestOverdueId: ""
    property bool rentalSnapshotAvailable: true
    property real bookedValue: 0
    property bool compact: false
    readonly property real availableShare: total > 0
        ? Math.max(0, Math.min(1, available / total)) : 0
    readonly property int availablePercent: Math.round(100 * availableShare)
    Layout.fillWidth: true
    Layout.minimumWidth: 290
    radius: 15
    color: colors.surface
    border.width: 1
    border.color: colors.border
    Accessible.role: Accessible.StaticText
    Accessible.name: "Fleet readiness: " + availablePercent + "%; " +
        available + " available, " + rented + " rented of " + total
    DesignTokens { id: colors }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: panel.compact ? 11 : 15
        spacing: panel.compact ? 3 : 5
        Label {
            text: "FLEET READINESS"
            color: colors.ink
            font.pixelSize: 11
            font.letterSpacing: 1
            font.weight: Font.DemiBold
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 9
            Item {
                Layout.preferredHeight: panel.compact ? 60 : 73
                Layout.preferredWidth: panel.compact ? 70 : 85
                Canvas {
                    id: availabilityRing
                    objectName: "overviewAvailabilityRing"
                    anchors.fill: parent
                    readonly property real share: panel.availableShare
                    onShareChanged: requestPaint()
                    Component.onCompleted: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        const radius = Math.min(width, height) / 2 - 6
                        ctx.lineWidth = 6
                        ctx.strokeStyle = "#DCE8E8"
                        ctx.beginPath()
                        ctx.arc(width/2, height/2, radius, 0, 2 * Math.PI)
                        ctx.stroke()
                        if (share > 0) {
                            ctx.strokeStyle = "#0F766E"
                            ctx.beginPath()
                            ctx.arc(width/2, height/2, radius,
                                -Math.PI / 2, -Math.PI / 2 + share * 2 * Math.PI)
                            ctx.stroke()
                        }
                    }
                }
                Label {
                    objectName: "overviewAvailablePercent"
                    anchors.centerIn: parent
                    text: panel.availablePercent + "%"
                    font.pixelSize: panel.compact ? 20 : 24
                    font.weight: Font.DemiBold
                    color: colors.teal
                    Accessible.name: text + " of the fleet available"
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: "of the fleet"
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: colors.navy
                }
                Label {
                    text: "is ready to rent"
                    font.pixelSize: 12
                    color: colors.ink
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 7
            radius: 4
            color: colors.quietSurface
            Rectangle {
                width: parent.width * panel.availableShare
                height: parent.height
                radius: 4
                color: colors.teal
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: panel.available + " available"
                font.pixelSize: 11
                font.weight: Font.DemiBold
                color: colors.teal
                Layout.fillWidth: true
            }
            Label {
                text: panel.rented + " rented"
                font.pixelSize: 11
                font.weight: Font.DemiBold
                color: colors.rented
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: colors.border
        }
        Label {
            text: panel.carCount + " cars · " +
                  panel.motorcycleCount + " motorcycles · " + panel.total + " total"
            font.pixelSize: 11
            color: colors.navy
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                objectName: "overviewDueTodayText"
                text: panel.rentalSnapshotAvailable ? "Due today: " + panel.dueToday : "Due data unavailable"
                font.pixelSize: 10
                color: colors.ink
            }
            Label {
                objectName: "overviewHistoryText"
                text: panel.rentalSnapshotAvailable ? "Returned or archived: " + panel.history : ""
                font.pixelSize: 10
                color: colors.ink
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }
        Label {
            objectName: "overviewOldestOverdueText"
            text: !panel.rentalSnapshotAvailable
                ? "Rental snapshot is unavailable"
                : panel.oldestOverdueId !== ""
                ? "Oldest overdue contract: " + panel.oldestOverdueId
                : "No overdue returns need attention"
            font.pixelSize: 10
            font.weight: Font.DemiBold
            color: panel.oldestOverdueId !== "" ? colors.warningInk : colors.teal
            Layout.fillWidth: true
            elide: Text.ElideRight
            Accessible.name: text
        }
    }
}
