import QtQuick

// Original, model-agnostic vector artwork. Decorative only: never suggests a
// specific make, licence, condition or commercially approved photograph.
Item {
    id: illustration
    property string vehicleType: "Car"
    implicitWidth: 164
    implicitHeight: 74
    Accessible.ignored: true

    Canvas {
        id: drawing
        anchors.centerIn: parent
        width: 164
        height: 74
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.lineJoin = "round"
            ctx.lineCap = "round"
            ctx.lineWidth = 3
            ctx.strokeStyle = "#17324D"
            ctx.fillStyle = "#FFFFFF"

            if (illustration.vehicleType.toLowerCase() === "motorcycle") {
                // The offline fallback must read as a road motorcycle rather
                // than a bicycle. This is original, make-neutral vector art.
                const navy = "#17324D"
                const teal = "#0F766E"
                const ice = "#EAF0F4"

                // Substantial tyres, hubs and frame.
                for (const cx of [37, 129]) {
                    ctx.fillStyle = navy
                    ctx.beginPath()
                    ctx.arc(cx, 56, 16, 0, 2 * Math.PI)
                    ctx.fill()
                    ctx.fillStyle = ice
                    ctx.beginPath()
                    ctx.arc(cx, 56, 9, 0, 2 * Math.PI)
                    ctx.fill()
                    ctx.strokeStyle = teal
                    ctx.lineWidth = 2
                    ctx.beginPath()
                    ctx.arc(cx, 56, 5, 0, 2 * Math.PI)
                    ctx.stroke()
                }
                ctx.strokeStyle = navy
                ctx.lineWidth = 5
                ctx.beginPath()
                ctx.moveTo(37, 56)
                ctx.lineTo(69, 44)
                ctx.lineTo(102, 50)
                ctx.lineTo(129, 56)
                ctx.stroke()

                // Lower engine block and distinctive casing.
                ctx.fillStyle = navy
                ctx.beginPath()
                ctx.moveTo(68, 33)
                ctx.lineTo(101, 30)
                ctx.lineTo(112, 44)
                ctx.lineTo(91, 53)
                ctx.lineTo(66, 49)
                ctx.closePath()
                ctx.fill()
                ctx.fillStyle = ice
                ctx.beginPath()
                ctx.arc(82, 43, 11, 0, 2 * Math.PI)
                ctx.fill()
                ctx.strokeStyle = teal
                ctx.lineWidth = 2.5
                ctx.stroke()
                ctx.beginPath()
                ctx.moveTo(76, 41)
                ctx.lineTo(88, 41)
                ctx.moveTo(82, 36)
                ctx.lineTo(82, 49)
                ctx.stroke()

                // Tank, flat saddle and short rear tail.
                ctx.fillStyle = teal
                ctx.beginPath()
                ctx.moveTo(70, 23)
                ctx.bezierCurveTo(82, 16, 103, 22, 103, 32)
                ctx.lineTo(92, 37)
                ctx.lineTo(74, 35)
                ctx.closePath()
                ctx.fill()
                ctx.fillStyle = navy
                ctx.beginPath()
                ctx.moveTo(48, 25)
                ctx.lineTo(73, 26)
                ctx.lineTo(79, 32)
                ctx.lineTo(53, 32)
                ctx.closePath()
                ctx.fill()
                ctx.strokeStyle = navy
                ctx.lineWidth = 4
                ctx.beginPath()
                ctx.moveTo(48, 29)
                ctx.lineTo(37, 30)
                ctx.lineTo(29, 36)
                ctx.stroke()

                // Fork, handlebar, headlamp and a raised front fender.
                ctx.lineWidth = 4
                ctx.beginPath()
                ctx.moveTo(107, 24)
                ctx.lineTo(129, 56)
                ctx.moveTo(113, 23)
                ctx.lineTo(135, 52)
                ctx.stroke()
                ctx.lineWidth = 3
                ctx.beginPath()
                ctx.moveTo(105, 22)
                ctx.lineTo(108, 16)
                ctx.lineTo(124, 16)
                ctx.stroke()
                ctx.fillStyle = "#E8F5F0"
                ctx.beginPath()
                ctx.ellipse(110, 25, 9, 7, 0, 0, 2 * Math.PI)
                ctx.fill()
                ctx.strokeStyle = teal
                ctx.lineWidth = 2
                ctx.stroke()
                ctx.beginPath()
                ctx.arc(129, 56, 20, 3.82, 5.62)
                ctx.stroke()

                // Exhaust below the engine makes the type legible at 112 px.
                ctx.strokeStyle = teal
                ctx.lineWidth = 5
                ctx.beginPath()
                ctx.moveTo(88, 55)
                ctx.lineTo(108, 61)
                ctx.lineTo(118, 58)
                ctx.stroke()
            } else {
                // Generic passenger-vehicle outline, not a particular car model.
                ctx.beginPath()
                ctx.moveTo(17, 54)
                ctx.lineTo(17, 43)
                ctx.quadraticCurveTo(17, 38, 26, 36)
                ctx.lineTo(44, 32)
                ctx.lineTo(58, 16)
                ctx.quadraticCurveTo(61, 13, 67, 13)
                ctx.lineTo(108, 13)
                ctx.quadraticCurveTo(113, 13, 118, 19)
                ctx.lineTo(132, 35)
                ctx.lineTo(143, 39)
                ctx.quadraticCurveTo(149, 42, 149, 49)
                ctx.lineTo(149, 55)
                ctx.lineTo(17, 55)
                ctx.closePath()
                ctx.fill()
                ctx.stroke()
                ctx.beginPath()
                ctx.moveTo(54, 33)
                ctx.lineTo(66, 19)
                ctx.lineTo(108, 19)
                ctx.lineTo(122, 34)
                ctx.closePath()
                ctx.strokeStyle = "#0F766E"
                ctx.stroke()
                ctx.strokeStyle = "#17324D"
                ctx.beginPath()
                ctx.arc(48, 56, 11, 0, 2 * Math.PI)
                ctx.arc(119, 56, 11, 0, 2 * Math.PI)
                ctx.fill()
                ctx.stroke()
            }
        }
    }
    onVehicleTypeChanged: drawing.requestPaint()
}
