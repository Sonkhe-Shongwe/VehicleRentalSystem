import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// R2 presentation only. C++ remains the source of fleet/rental values.
// Pinned, locally bundled Lucide SVG icons are used.
Rectangle {
    id: card
    property string metricLabel: ""
    property string value: ""
    // Formatting is a view-only concern. Preserve the underlying card.value
    // and its existing spoken name for native accessibility/API tests.
    readonly property string displayValue: card.value.startsWith("R ")
        ? "R " + card.value.slice(2).replace(/\B(?=(\d{3})+(?!\d))/g, ",")
        : card.value
    property string note: ""
    property string metricIcon: ""
    property color accent: colors.teal
    property bool compact: false
    DesignTokens { id: colors }
    Layout.fillWidth: true
    Layout.minimumWidth: 170
    radius: 14
    color: colors.surface
    border.color: colors.border
    border.width: 1
    Accessible.role: Accessible.StaticText
    Accessible.name: metricLabel + ": " + value + ". " + note
    Rectangle {
        x: 13
        y: card.compact ? 10 : 13
        width: card.compact ? 29 : 36
        height: width
        radius: 10
        color: colors.mint
        R2LucideIcon {
            anchors.centerIn: parent
            width: card.compact ? 19 : 22
            height: width
            iconName: card.metricIcon
            tone: "emerald"
            Accessible.ignored: true
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: card.compact ? 51 : 59
        anchors.rightMargin: 10
        anchors.topMargin: card.compact ? 7 : 10
        anchors.bottomMargin: card.compact ? 6 : 9
        spacing: 2
        Label {
            text: card.metricLabel.toUpperCase()
            color: colors.secondaryText
            font.family: colors.shellFontFamily
            font.pixelSize: card.compact ? 11 : 13
            font.weight: Font.DemiBold
            font.letterSpacing: 0.2
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        Label {
            objectName: "overviewMetricValue"
            text: card.displayValue
            color: colors.pageHeading
            font.family: colors.shellFontFamily
            font.pixelSize: card.compact ? 25 : 34
            font.weight: Font.DemiBold
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        Label {
            text: card.note
            color: colors.secondaryText
            font.family: colors.shellFontFamily
            font.pixelSize: card.compact ? 10 : 12
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
    }
}
