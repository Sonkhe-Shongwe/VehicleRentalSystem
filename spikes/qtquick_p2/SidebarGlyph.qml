import QtQuick

// Native QML Canvas icons: no external glyph fonts, PNGs, API or licensing.
// Simple 22px line artwork remains consistent on Windows and Linux.
Canvas {
    id: glyph
    property string kind: "Overview"
    property color glyphColor: "#17324D"
    implicitWidth: 22
    implicitHeight: 22
    onKindChanged: requestPaint()
    onGlyphColorChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d")
        ctx.clearRect(0, 0, width, height)
        ctx.save()
        ctx.translate((width - 22) / 2, (height - 22) / 2)
        // Original native-drawn icons share a deliberate two-pixel stroke.
        // A separately licensed full icon asset pass remains outstanding.
        ctx.lineWidth = 2
        ctx.lineCap = "round"
        ctx.lineJoin = "round"
        ctx.strokeStyle = glyph.glyphColor
        if (kind === "Overview") {
            ctx.strokeRect(3, 3, 7, 7)
            ctx.strokeRect(13, 3, 6, 7)
            ctx.strokeRect(3, 13, 7, 6)
            ctx.strokeRect(13, 13, 6, 6)
        } else if (kind === "Fleet") {
            ctx.beginPath()
            ctx.moveTo(3, 13); ctx.lineTo(5.5, 8)
            ctx.lineTo(16.5, 8); ctx.lineTo(19, 13)
            ctx.lineTo(19, 17); ctx.lineTo(3, 17); ctx.closePath()
            ctx.moveTo(5.5, 8); ctx.lineTo(7, 5); ctx.lineTo(15, 5)
            ctx.lineTo(16.5, 8); ctx.stroke()
            ctx.beginPath(); ctx.arc(6.5, 17, 1.5, 0, Math.PI * 2); ctx.stroke()
            ctx.beginPath(); ctx.arc(15.5, 17, 1.5, 0, Math.PI * 2); ctx.stroke()
        } else if (kind === "Rentals") {
            ctx.strokeRect(5, 4, 12, 16)
            ctx.beginPath()
            ctx.moveTo(8, 3);ctx.lineTo(14, 3)
            ctx.moveTo(8, 9);ctx.lineTo(14, 9)
            ctx.moveTo(8, 13);ctx.lineTo(14, 13)
            ctx.moveTo(8, 17);ctx.lineTo(12, 17)
            ctx.stroke()
        } else if (kind === "check") {
            ctx.beginPath(); ctx.arc(11, 11, 8, 0, Math.PI * 2)
            ctx.moveTo(7, 11); ctx.lineTo(10, 14); ctx.lineTo(16, 8)
            ctx.stroke()
        } else if (kind === "clock") {
            ctx.beginPath(); ctx.arc(11, 11, 8, 0, Math.PI * 2)
            ctx.moveTo(11, 6); ctx.lineTo(11, 11); ctx.lineTo(15, 13)
            ctx.stroke()
        } else if (kind === "search") {
            ctx.beginPath()
            ctx.arc(9.5, 9.5, 5.2, 0, Math.PI * 2)
            ctx.moveTo(13.3, 13.3); ctx.lineTo(19, 19)
            ctx.stroke()
        } else if (kind === "add") {
            ctx.beginPath()
            ctx.moveTo(11, 4); ctx.lineTo(11, 18)
            ctx.moveTo(4, 11); ctx.lineTo(18, 11)
            ctx.stroke()
        } else if (kind === "SQLite Lab") {
            ctx.beginPath()
            ctx.moveTo(3, 6);ctx.bezierCurveTo(3, 2, 19, 2, 19, 6)
            ctx.bezierCurveTo(19, 10, 3, 10, 3, 6)
            ctx.moveTo(3, 6);ctx.lineTo(3, 17)
            ctx.bezierCurveTo(3, 21, 19, 21, 19, 17)
            ctx.lineTo(19, 6)
            ctx.moveTo(3, 12);ctx.bezierCurveTo(3, 16, 19, 16, 19, 12)
            ctx.stroke()
        } else if (kind === "collapse" || kind === "expand") {
            ctx.beginPath()
            ctx.moveTo(4, 5);ctx.lineTo(4, 17)
            ctx.moveTo(9, 6);ctx.lineTo(19, 6)
            ctx.moveTo(9, 16);ctx.lineTo(19, 16)
            if (kind === "collapse") {
                ctx.moveTo(14, 9);ctx.lineTo(11, 11);ctx.lineTo(14, 13)
            } else {
                ctx.moveTo(13, 9);ctx.lineTo(16, 11);ctx.lineTo(13, 13)
            }
            ctx.stroke()
        }
        ctx.restore()
    }
}
