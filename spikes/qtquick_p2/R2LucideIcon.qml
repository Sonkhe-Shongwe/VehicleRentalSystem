import QtQuick

// R2 pinned, offline Lucide SVG, decorative inside fully accessible controls.
Item {
    id: icon
    property string iconName: "layout-grid"
    property string tone: "emerald"
    readonly property url assetUrl: "qrc:/r2-icons/" + tone + "/" + iconName + ".svg"
    readonly property bool assetReady: glyph.status === Image.Ready
    implicitWidth: 22
    implicitHeight: 22
    Image {
        id: glyph
        objectName: "r2LucideSvgImage"
        anchors.fill: parent
        source: icon.assetUrl
        sourceSize.width: Math.max(24, Math.round(icon.width * 2))
        sourceSize.height: Math.max(24, Math.round(icon.height * 2))
        fillMode: Image.PreserveAspectFit
        asynchronous: false
        smooth: true
        Accessible.ignored: true
    }
}
