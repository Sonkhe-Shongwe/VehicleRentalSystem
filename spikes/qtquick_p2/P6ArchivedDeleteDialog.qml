import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: deleteDialog
    property var command
    property var rentals
    property var focusReturn
    property var fallbackFocusReturn
    property string targetId: ""
    property bool reviewInvalidated: false
    property string reviewInvalidationReason: ""
    function invalidateReview(reason) {
        // A reviewed target is valid only while its verified context remains
        // continuous. Never re-arm the same modal after A -> B -> A selection.
        if (!deleteDialog.visible || deleteDialog.reviewInvalidated)
            return
        deleteDialog.reviewInvalidationReason = reason
        deleteDialog.reviewInvalidated = true
    }
    Connections {
        target: deleteDialog.rentals
        function onSelectionChanged() {
            if (deleteDialog.visible &&
                    deleteDialog.targetId !== deleteDialog.rentals.selectedRentalId)
                deleteDialog.invalidateReview("Selection changed.")
        }
        function onModelReset() {
            deleteDialog.invalidateReview("Rental data refreshed.")
        }
        function onSnapshotAvailabilityChanged() {
            if (deleteDialog.rentals && !deleteDialog.rentals.snapshotAvailable)
                deleteDialog.invalidateReview("Rental data unavailable.")
        }
    }
    title: "Permanently delete archived rental"
    modal: true
    focus: true
    width: Math.min(520, parent ? parent.width - 64 : 500)
    standardButtons: Dialog.NoButton
    closePolicy: Popup.CloseOnEscape
    onOpened: {
        reviewInvalidated = false
        reviewInvalidationReason = ""
        Qt.callLater(function() {
            if(deleteDialog.visible)
                deletePhrase.forceActiveFocus(Qt.TabFocusReason)
        })
    }
    onClosed: {
        deletePhrase.clear()
        targetId = ""
        reviewInvalidated = false
        reviewInvalidationReason = ""
        Qt.callLater(function() {
            if(focusReturn && focusReturn.visible && focusReturn.enabled)
                focusReturn.forceActiveFocus(Qt.TabFocusReason)
            else if(fallbackFocusReturn && fallbackFocusReturn.visible)
                fallbackFocusReturn.forceActiveFocus(Qt.TabFocusReason)
        })
    }
    contentItem: ColumnLayout {
        spacing: 12
        Label {
            text: "This permanently removes the archived rental from the current session. It cannot be undone."
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            color: "#17324D"
        }
        Label {
            objectName: "rentalDeleteTarget"
            text: targetId === "" ? "No archived rental selected" : "Archived rental: " + targetId
            font.bold: true
            color: "#17324D"
            Layout.fillWidth: true
        }
        Label {
            text: "Type DELETE " + targetId + " exactly to confirm."
            wrapMode: Text.WordWrap
            color: "#5F6F82"
            Layout.fillWidth: true
        }
        Label {
            objectName: "rentalDeleteReviewWarning"
            visible: deleteDialog.reviewInvalidated
            Layout.fillWidth: true
            text: deleteDialog.reviewInvalidationReason +
                  " Close and reopen to review this deletion."
            color: "#B42318"
            wrapMode: Text.WordWrap
            Accessible.name: text
        }
        AccessibleTextField {
            id: deletePhrase
            objectName: "rentalDeletePhrase"
            fieldPurpose: "Exact archived rental deletion phrase"
            placeholderText: "DELETE " + targetId
            Layout.fillWidth: true
        }
        DomainMessage {
            objectName: "rentalDeleteFeedback"
            text: command ? command.lastMessage : ""
        }
        RowLayout {
            Button {
                objectName: "rentalDeleteCancel"
                text: "Cancel"
                activeFocusOnTab: true
                onClicked: deleteDialog.close()
            }
            Item { Layout.fillWidth: true }
            DangerButton {
                objectName: "rentalDeleteConfirm"
                text: "Delete archived record"
                activeFocusOnTab: true
                enabled: command && command.canDeleteSelected &&
                         targetId !== "" && !deleteDialog.reviewInvalidated &&
                         deleteDialog.rentals &&
                         targetId === deleteDialog.rentals.selectedRentalId
                Accessible.name: "Permanently delete selected archived rental"
                onClicked: {
                    if(command.permanentlyDelete(targetId,deletePhrase.text))
                        deleteDialog.close()
                }
            }
        }
    }
}
