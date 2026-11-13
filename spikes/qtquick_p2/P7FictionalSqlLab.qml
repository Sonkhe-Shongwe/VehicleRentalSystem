import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The lab uses its own temporary SQLite database with generated data.
Flickable {
    id: lab
    objectName: "p7AppSqlLab"
    clip: true
    contentWidth: width
    contentHeight: contents.implicitHeight
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    ColumnLayout {
    id: contents
    width: lab.width
    spacing: 16

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 120
        radius: 12
        color: "#E8F5F0"
        border.color: "#0F766E"
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 7
            Label {
                text: "SQLITE LAB · ISOLATED TEST DATA"
                color: "#0F766E"
                font.pixelSize: 15
                font.bold: true
                Accessible.name: text
            }
            Label {
                Layout.fillWidth: true
                text: "The lab uses a separate sample database that resets when you close the app."
                wrapMode: Text.WordWrap
                color: "#17324D"
                font.pixelSize: 12
                Accessible.name: text
            }
            // Presentation guidance is driven exclusively by C++-owned
            // lifecycle state, never by a QML simulation of a booking.
            Label {
                objectName: "p7AppNextStep"
                Layout.fillWidth: true
                text: !p7SqlController.ready ?
                      "LAB UNAVAILABLE · Could not open the sample database" :
                      p7SqlController.stage === "READY" ?
                      "STEP 1/5 · Preview a 3-day quote" :
                      p7SqlController.stage === "QUOTED" ?
                      "STEP 2/5 · Confirm booking (no payment collected)" :
                      p7SqlController.stage === "ACTIVE" ?
                      "STEP 3/5 · Return contract R001" :
                      p7SqlController.stage === "RETURNED" ?
                      "STEP 4/5 · Archive the returned contract" :
                      p7SqlController.stage === "ARCHIVED" ?
                      "STEP 5/5 · Retire C001 while preserving history" :
                      p7SqlController.stage === "RETIRED" ?
                      "COMPLETE · Archived history retained; test data resets on exit" :
                      "LAB UNAVAILABLE · No further action available"
                color: "#0F766E"
                font.pixelSize: 12
                font.bold: true
                elide: Text.ElideRight
                Accessible.name: text
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 14
        P7LabSnapshotCard {
            objectName: "p7AppFleetSnapshotCard"
            heading: "FLEET"
            snapshotAvailable: p7SqlFleet.snapshotAvailable
            summary: p7SqlFleet.snapshotAvailable ?
                     p7SqlFleet.totalCount + " vehicle  |  " +
                     p7SqlFleet.availableCount + " available" :
                     "Fleet read unavailable. Counts hidden."
            metricObjectName: "p7AppFleetMetrics"
            retryObjectName: "p7AppRetryFleet"
            retryLabel: "Retry Fleet read"
            onRetryRequested: p7SqlFleet.refresh()
        }
        P7LabSnapshotCard {
            objectName: "p7AppRentalsSnapshotCard"
            heading: "RENTALS"
            snapshotAvailable: p7SqlRentals.snapshotAvailable
            summary: p7SqlRentals.snapshotAvailable ?
                     "Active: " + p7SqlRentals.activeCount +
                     "  |  History: " + p7SqlRentals.historyCount :
                     "Rentals read unavailable. Counts hidden."
            metricObjectName: "p7AppRentalsMetrics"
            retryObjectName: "p7AppRetryRentals"
            retryLabel: "Retry Rentals read"
            onRetryRequested: p7SqlRentals.refresh()
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 175
        radius: 12
        color: "white"
        border.color: "#D6E0E7"
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10
            Label {
                text: "C001 · TOYOTA COROLLA"
                color: "#17324D"
                font.pixelSize: 18
                font.bold: true
            }
            Label {
                objectName: "p7AppAsset"
                Layout.fillWidth: true
                text: p7SqlFleet.snapshotAvailable && p7SqlFleet.vehicles.length > 0 ?
                      p7SqlFleet.vehicles[0].displayName +
                      "  ·  R " + Number(p7SqlFleet.vehicles[0].dailyRate).toFixed(2) + "/day" +
                      "  ·  " + (p7SqlFleet.vehicles[0].available ? "Available" : "Not available") :
                      "Verified vehicle details unavailable"
                color: "#17324D"
                Accessible.name: text
            }
            Label {
                objectName: "p7AppReceipt"
                text: p7SqlController.rentalId === "" ?
                      "Rental ID: not issued" :
                      "Rental ID: " + p7SqlController.rentalId
                color: "#17324D"
                font.bold: true
                Accessible.name: text
            }
            Label {
                objectName: "p7AppQuote"
                text: p7SqlController.quoteLabel + " · NOT A PAYMENT"
                color: "#0F766E"
                font.bold: true
                Accessible.name: text
            }
        }
    }

    Flow {
        Layout.fillWidth: true
        spacing: 10
        Button {
            objectName: "p7AppPreview"
            text: "Preview 3 days"
            enabled: p7SqlController.canPreview
            onClicked: p7SqlController.previewQuote()
        }
        Button {
            objectName: "p7AppConfirm"
            text: "Confirm booking"
            enabled: p7SqlController.canConfirm
            onClicked: p7SqlController.confirmBooking()
        }
        Button {
            objectName: "p7AppReturn"
            text: "Return rental"
            enabled: p7SqlController.canReturn
            onClicked: p7SqlController.returnBooking()
        }
        Button {
            objectName: "p7AppArchive"
            text: "Archive rental"
            enabled: p7SqlController.canArchive
            onClicked: p7SqlController.archiveBooking()
        }
        Button {
            objectName: "p7AppRetire"
            text: "Retire vehicle"
            enabled: p7SqlController.canRetire
            onClicked: p7SqlController.retireAsset()
        }
    }

    // Keep compact-height booking and return actions above the optional
    // read-only Overview panel so operators do not lose the action row.
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 145
        radius: 12
        color: "white"
        border.color: "#D6E0E7"
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 15
            spacing: 5
            Label {
                text: "SUMMARY"
                color: "#0F766E"
                font.bold: true
                Accessible.name: text
            }
            Label {
                objectName: "p7AppOverviewFleet"
                Layout.fillWidth: true
                text: p7SqlOverview.snapshotAvailable ?
                      "Fleet: " + p7SqlOverview.totalVehicles + " total · " +
                      p7SqlOverview.availableVehicles + " available" :
                      "Overview unavailable. Fleet counts hidden."
                color: "#17324D"
                Accessible.name: text
            }
            Label {
                objectName: "p7AppOverviewRentals"
                Layout.fillWidth: true
                text: p7SqlOverview.snapshotAvailable ?
                      "Contracts: " + p7SqlOverview.activeRentals + " active · " +
                      p7SqlOverview.historyRentals + " returned or archived" :
                      "Overview unavailable. Contract counts hidden."
                color: "#17324D"
                Accessible.name: text
            }
            Label {
                objectName: "p7AppOverviewValue"
                Layout.fillWidth: true
                text: p7SqlOverview.snapshotAvailable ?
                      "Active booked value: " + p7SqlOverview.activeBookedLabel +
                      " · NOT collected revenue or payment" :
                      "Overview unavailable. Booked value hidden."
                color: "#17324D"
                Accessible.name: text
            }
            Button {
                objectName: "p7AppRetryOverview"
                text: "Retry Overview read"
                visible: !p7SqlOverview.snapshotAvailable
                onClicked: p7SqlOverview.refresh()
                Accessible.name: text
            }
        }
    }

    // This explorer is deliberately READ ONLY: status and selection are
    // computed by typed C++ DTO models; no SQL, PII or lifecycle writes here.
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 338
        radius: 12
        color: "white"
        border.color: "#D6E0E7"
        RowLayout {
            anchors.fill: parent
            anchors.margins: 15
            spacing: 14

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 7
                Label {
                    text: "FLEET RECORD EXPLORER · READ ONLY"
                    color: "#0F766E"
                    font.bold: true
                    Accessible.name: text
                }
                ComboBox {
                    objectName: "p7AppFleetFilter"
                    Layout.fillWidth: true
                    model: ["All", "Available", "Unavailable"]
                    enabled: p7SqlFleet.snapshotAvailable
                    onActivated: p7SqlFleet.setStatusFilter(currentText)
                    Accessible.name: "Filter SQLite Lab fleet by availability"
                }
                TextField {
                    objectName: "p7AppFleetSearch"
                    Layout.fillWidth: true
                    enabled: p7SqlFleet.snapshotAvailable
                    placeholderText: "Search vehicle ID, model or details"
                    maximumLength: 80
                    onTextChanged: p7SqlFleet.setSearchText(text)
                    Accessible.name: "Search SQLite Lab vehicle records"
                }
                ListView {
                    objectName: "p7AppFleetList"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 96
                    clip: true
                    visible: p7SqlFleet.snapshotAvailable
                    model: p7SqlFleet.visibleVehicles
                    delegate: Button {
                        required property var modelData
                        objectName: "p7AppFleetRow" + modelData.id
                        width: ListView.view.width
                        height: 38
                        text: modelData.id + " · " + modelData.displayName +
                              (modelData.available ? " · Available" : " · Unavailable")
                        onClicked: p7SqlFleet.selectById(modelData.id)
                        Accessible.name: "Inspect test vehicle " + modelData.id
                    }
                }
                Label {
                    objectName: "p7AppFleetEmpty"
                    Layout.fillWidth: true
                    visible: p7SqlFleet.snapshotAvailable && p7SqlFleet.visibleCount === 0
                    text: "No matching vehicles. Change the search or availability filter."
                    color: "#556576"
                    wrapMode: Text.WordWrap
                    Accessible.name: text
                }
                Label {
                    objectName: "p7AppFleetSelected"
                    Layout.fillWidth: true
                    text: !p7SqlFleet.snapshotAvailable ?
                          "Fleet snapshot unavailable, details hidden" :
                          p7SqlFleet.selectedVehicleId === "" ?
                          "Select a vehicle to inspect" :
                          p7SqlFleet.selectedVehicle.id + " · " +
                          p7SqlFleet.selectedVehicle.details +
                          " · R " + Number(p7SqlFleet.selectedVehicle.dailyRate).toFixed(2) + "/day"
                    color: "#17324D"
                    wrapMode: Text.WordWrap
                    Accessible.name: text
                }
                Label {
                    objectName: "p7AppFleetVisibleCount"
                    text: p7SqlFleet.snapshotAvailable ?
                          p7SqlFleet.visibleCount + " matching vehicle(s)" :
                          "Unverified"
                    color: "#556576"
                    Accessible.name: text
                }
            }
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                color: "#D6E0E7"
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 7
                Label {
                    text: "RENTAL CONTRACTS · READ ONLY"
                    color: "#0F766E"
                    font.bold: true
                    Accessible.name: text
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 7
                    ComboBox {
                        objectName: "p7AppRentalFilter"
                        Layout.fillWidth: true
                        model: ["All", "Active", "History"]
                        enabled: p7SqlRentals.snapshotAvailable
                        onActivated: p7SqlRentals.statusFilter = currentText
                        Accessible.name: "Filter SQLite Lab rental contracts by status"
                    }
                    TextField {
                        objectName: "p7AppRentalSearch"
                        Layout.fillWidth: true
                        enabled: p7SqlRentals.snapshotAvailable
                        placeholderText: "Find rental or vehicle ID"
                        maximumLength: 80
                        onTextChanged: p7SqlRentals.searchText = text
                        Accessible.name: "Search SQLite Lab rental or vehicle IDs"
                    }
                }
                ListView {
                    objectName: "p7AppRentalList"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 96
                    clip: true
                    visible: p7SqlRentals.snapshotAvailable
                    model: p7SqlRentals
                    delegate: Button {
                        objectName: "p7AppRentalRow" + rentalId
                        width: ListView.view.width
                        height: 38
                        text: rentalId + " · " + vehicleId + " · " + status
                        onClicked: p7SqlRentals.selectVisibleRow(index)
                        Accessible.name: "Inspect test contract " + rentalId
                    }
                }
                Label {
                    objectName: "p7AppRentalSelected"
                    Layout.fillWidth: true
                    text: !p7SqlRentals.snapshotAvailable ?
                          "Rental snapshot unavailable, details hidden" :
                          p7SqlRentals.selectedRentalId === "" ?
                          "Select a contract to inspect" :
                          p7SqlRentals.selectedContract.rentalId + " · " +
                          p7SqlRentals.selectedContract.status +
                          " · booked R " +
                          Number(p7SqlRentals.selectedContract.total).toFixed(2) +
                          " (NOT collected revenue)"
                    color: "#17324D"
                    wrapMode: Text.WordWrap
                    Accessible.name: text
                }
                Label {
                    objectName: "p7AppRentalVisibleCount"
                    text: p7SqlRentals.snapshotAvailable ?
                          p7SqlRentals.count + " visible contract(s)" : "Unverified"
                    color: "#556576"
                    Accessible.name: text
                }
            }
        }
    }
    Label {
        objectName: "p7AppMessage"
        Layout.fillWidth: true
        text: p7SqlController.message
        color: "#17324D"
        wrapMode: Text.WordWrap
        Accessible.name: text
    }
    Label {
        Layout.fillWidth: true
        text: "EXPERIMENTAL AND ISOLATED · This is not production persistence or a commercial booking workflow."
        color: "#556576"
        font.pixelSize: 12
        wrapMode: Text.WordWrap
        Accessible.name: text
    }
}
}
