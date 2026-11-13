import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: removeDialog
    property var command
    property var fleetController
    property var focusReturn
    property var fallbackFocusReturn
    property string targetId: ""
    property string targetName: ""
    property bool reviewInvalidated: false
    property string reviewInvalidationReason: ""
    function invalidateReview(reason) {
        // The selection reviewed for permanent removal must remain
        // continuous. A -> B -> A cannot resurrect an older approval.
        if (!removeDialog.visible || removeDialog.reviewInvalidated)
            return
        removeDialog.reviewInvalidationReason = reason
        removeDialog.reviewInvalidated = true
    }
    Connections {
        target: removeDialog.fleetController
        function onSelectionChanged() {
            if (removeDialog.visible &&
                    removeDialog.targetId !== removeDialog.fleetController.selectedId)
                removeDialog.invalidateReview("Selection changed.")
        }
        function onFleetChanged() {
            // A C++ refresh may update the selected vehicle without its ID
            // changing, including its availability or rental references.
            removeDialog.invalidateReview("Fleet data refreshed.")
        }
    }
    title: "Remove vehicle"
    modal: true
    focus: true
    width: Math.min(520, parent ? parent.width-60 : 500)
    standardButtons: Dialog.NoButton
    closePolicy: Popup.CloseOnEscape
    onOpened: {
        reviewInvalidated = false
        reviewInvalidationReason = ""
        Qt.callLater(function() {
            if(removeDialog.visible)
                confirmationField.forceActiveFocus(Qt.TabFocusReason)
        })
    }
    onClosed: {
        confirmationField.clear()
        targetId = ""
        targetName = ""
        reviewInvalidated = false
        reviewInvalidationReason = ""
        Qt.callLater(function() {
            if(focusReturn && focusReturn.visible && focusReturn.enabled)
                focusReturn.forceActiveFocus(Qt.TabFocusReason)
            else if(fallbackFocusReturn && fallbackFocusReturn.visible && fallbackFocusReturn.enabled)
                fallbackFocusReturn.forceActiveFocus(Qt.TabFocusReason)
        })
    }
    contentItem: ColumnLayout {
        spacing: 12
        Label {
            text: "This removes the vehicle from the current session. This action cannot be undone here."
            color: "#17324D"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            objectName: "fleetRemoveTarget"
            text: targetId === "" ? "No vehicle selected" : targetName + " (" + targetId + ")"
            font.bold: true
            color: "#17324D"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Label {
            text: "Active rental vehicles cannot be removed. Type REMOVE " + targetId + " to confirm."
            wrapMode: Text.WordWrap
            color: "#5F6F82"
            Layout.fillWidth: true
        }
        Label {
            objectName: "fleetRemoveReviewWarning"
            visible: removeDialog.reviewInvalidated
            Layout.fillWidth: true
            color: "#B42318"
            text: removeDialog.reviewInvalidationReason +
                  " Close and reopen to review this removal."
            wrapMode: Text.WordWrap
            Accessible.name: text
        }
        AccessibleTextField {
            id: confirmationField
            objectName: "fleetRemoveConfirmation"
            fieldPurpose: "Exact vehicle removal confirmation"
            placeholderText: "REMOVE " + targetId
            Layout.fillWidth: true
        }
        DomainMessage {
            objectName: "fleetRemoveFeedback"
            text: command ? command.lastMessage : ""
        }
        RowLayout {
            Button {
                objectName: "fleetRemoveCancel"
                text: "Cancel"
                activeFocusOnTab: true
                onClicked: removeDialog.close()
            }
            Item { Layout.fillWidth: true }
            DangerButton {
                objectName: "fleetRemoveConfirm"
                text: "Remove vehicle"
                activeFocusOnTab: true
                enabled: command && command.canRemove && targetId !== "" &&
                         !removeDialog.reviewInvalidated &&
                         removeDialog.fleetController &&
                         targetId === removeDialog.fleetController.selectedId &&
                         command.canRemoveVehicle(targetId)
                onClicked: {
                    // Re-check the selected ID and domain eligibility at the
                    // click boundary; do not rely only on a bound button state.
                    if(removeDialog.reviewInvalidated ||
                            !removeDialog.fleetController ||
                            targetId !== removeDialog.fleetController.selectedId ||
                            !command.canRemoveVehicle(targetId))
                        return
                    if(command.removeVehicle(targetId, confirmationField.text))
                        removeDialog.close()
                }
            }
        }
    }
}
