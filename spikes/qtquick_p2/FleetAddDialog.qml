import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: addDialog
    property var command
    property var focusReturn
    title: "Add vehicle"
    modal: true
    focus: true
    width: Math.min(560, parent ? parent.width - 64 : 520)
    standardButtons: Dialog.NoButton
    // The native file picker is explicit user intent. Decoder/copy validation
    // lives in C++ and never exposes an arbitrary input to RentalSystem.
    FileDialog {
        id: vehiclePhotoPicker
        objectName: "fleetPhotoPicker"
        title: "Choose a vehicle photo"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp)"]
        onAccepted: fleetPhotos.chooseFile(selectedFile)
    }
    closePolicy: Popup.CloseOnEscape
    onOpened: Qt.callLater(function() {
        if(addDialog.visible) {
            // Set after the type-dependent SpinBox bounds have settled.
            // Without this, its first open may clamp the initial 4 doors to 8.
            addExtra.value = addType.currentIndex === 0 ? 4 : 450
            addBrand.forceActiveFocus(Qt.TabFocusReason)
        }
    })
    onClosed: {
        fleetPhotos.discardStaged()
        addBrand.clear()
        addModel.clear()
        addRate.clear()
        addType.currentIndex = 0
        addExtra.value = 4
        Qt.callLater(function() {
            if(focusReturn && focusReturn.visible && focusReturn.enabled)
                focusReturn.forceActiveFocus(Qt.TabFocusReason)
        })
    }
    contentItem: ColumnLayout {
        spacing: 9
        Label {
            text: "Vehicle additions are kept in this session and do not change saved records."
            color: "#5F6F82"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        ComboBox {
            id: addType
            objectName: "fleetAddType"
            model: ["Car", "Motorcycle"]
            activeFocusOnTab: true
            Accessible.name: "Vehicle type"
            Layout.fillWidth: true
            onCurrentIndexChanged: addExtra.value = currentIndex===0 ? 4 : 450
        }
        AccessibleTextField {
            id: addBrand
            objectName: "fleetAddBrand"
            fieldPurpose: "Vehicle brand"
            placeholderText: "Brand, for example Toyota"
            Layout.fillWidth: true
        }
        AccessibleTextField {
            id: addModel
            objectName: "fleetAddModel"
            fieldPurpose: "Vehicle model"
            placeholderText: "Model, for example Orion"
            Layout.fillWidth: true
        }
        AccessibleTextField {
            id: addRate
            objectName: "fleetAddRate"
            fieldPurpose: "Daily rental rate in rand"
            placeholderText: "Daily rate (R), for example 420"
            Layout.fillWidth: true
        }
        RowLayout {
            Label {
                text: addType.currentIndex===0 ? "Doors" : "Engine capacity (cc)"
                color: "#17324D"
            }
            SpinBox {
                id: addExtra
                objectName: "fleetAddExtra"
                from: addType.currentIndex===0 ? 2 : 50
                to: addType.currentIndex===0 ? 8 : 3000
                value: 4
                editable: true
                activeFocusOnTab: true
                Accessible.name: addType.currentIndex===0
                    ? "Number of car doors" : "Motorcycle engine capacity in cc"
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 5
            RowLayout {
                Layout.fillWidth: true
                Button {
                    objectName: "fleetChoosePhoto"
                    text: fleetPhotos.hasStaged ? "Change photo..." : "Choose photo (optional)..."
                    activeFocusOnTab: true
                    Accessible.name: "Choose an optional photo from this computer"
                    onClicked: vehiclePhotoPicker.open()
                }
                Button {
                    objectName: "fleetDiscardPhoto"
                    text: "Clear photo"
                    visible: fleetPhotos.hasStaged
                    onClicked: fleetPhotos.discardStaged()
                }
                Item { Layout.fillWidth: true }
                Image {
                    objectName: "fleetPhotoPreview"
                    Layout.preferredWidth: 90
                    Layout.preferredHeight: 56
                    // Source must not depend on visible: failed decode hides preview.
                    source: fleetPhotos.hasStaged
                        ? "image://fleetphotos/__staged?v=" + fleetPhotos.revision : ""
                    visible: fleetPhotos.hasStaged && status === Image.Ready
                    fillMode: Image.PreserveAspectCrop
                    cache: false
                    Accessible.name: "Chosen preview image"
                }
            }
            Label {
                objectName: "fleetPhotoFeedback"
                Layout.fillWidth: true
                text: fleetPhotos.feedback !== "" ? fleetPhotos.feedback :
                      "No image needed. A neutral vehicle illustration is the fallback."
                color: "#5F6F82"
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: "Selected photos are stored locally for this session."
                color: "#5F6F82"
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
        DomainMessage {
            objectName: "fleetAddFeedback"
            text: command ? command.lastMessage : ""
        }
        RowLayout {
            Button {
                objectName: "fleetAddCancel"
                text: "Cancel"
                activeFocusOnTab: true
                onClicked: addDialog.close()
            }
            Item { Layout.fillWidth: true }
            Button {
                objectName: "fleetAddConfirm"
                text: "Add vehicle"
                activeFocusOnTab: true
                enabled: command && command.canAdd
                onClicked: {
                    if(command.addVehicle(addType.currentText,addBrand.text,
                                             addModel.text,Number(addRate.text),
                                             addExtra.value))
                        addDialog.close()
                }
            }
        }
    }
}
