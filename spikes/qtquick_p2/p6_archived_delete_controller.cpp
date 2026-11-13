#include "p6_archived_delete_controller.h"
#include "src/application/rental_read_service.h"
#include "src/application/rental_list_model.h"
#include "rentalsystem.h"

P6ArchivedDeleteController::P6ArchivedDeleteController(
    RentalSystem &system,const RentalReadService &reads,
    RentalListModel &rentals,bool disposable,QObject *parent)
    : QObject(parent),m_service(system),m_reads(reads),m_rentals(rentals),
      m_disposable(disposable) {
    connect(&m_rentals,&RentalListModel::selectionChanged,
            this,&P6ArchivedDeleteController::stateChanged);
    connect(&m_rentals,&QAbstractItemModel::modelReset,
            this,&P6ArchivedDeleteController::stateChanged);
    // A read outage can retain the selected ID without a model reset.
    // Rebind the irreversible action's enabled state on availability changes.
    connect(&m_rentals,&RentalListModel::snapshotAvailabilityChanged,
            this,&P6ArchivedDeleteController::stateChanged);
}

bool P6ArchivedDeleteController::canDeleteSelected() const {
    if(!m_disposable || !m_rentals.snapshotAvailable() ||
       m_rentals.selectedRow()<0)return false;
    const auto record=m_reads.recordById(m_rentals.selectedRentalId());
    return record && record->status==RentalRecordStatus::Archived;
}

void P6ArchivedDeleteController::clearMessage() {
    if(m_message.isEmpty())return;
    m_message.clear();
    emit stateChanged();
}

bool P6ArchivedDeleteController::permanentlyDelete(
    const QString &rentalId,const QString &confirmation) {
    if(!m_disposable) {
        m_message=QStringLiteral("Permanent deletion is disabled in the persistent fictional-only session.");
        emit stateChanged();
        return false;
    }
    if(!m_rentals.snapshotAvailable()) {
        m_message=QStringLiteral(
            "Rental data unavailable. Retry the read before permanently deleting.");
        emit stateChanged();
        return false;
    }
    // The modal takes a snapshot of the selected ID. Reject stale or hidden
    // records instead of deleting a different selection by a retained row.
    if(rentalId.isEmpty() || rentalId!=m_rentals.selectedRentalId() ||
       m_rentals.selectedRow()<0) {
        m_message=QStringLiteral("Selection changed. Select an archived rental again.");
        emit stateChanged();
        return false;
    }
    const auto record=m_reads.recordById(rentalId);
    if(!record || record->status!=RentalRecordStatus::Archived) {
        m_message=QStringLiteral("Only already archived rentals may be permanently deleted.");
        emit stateChanged();
        return false;
    }
    const ArchivedDeleteResult result=m_service.permanentlyDelete(
        record->rentalId,confirmation);
    if(!result.ok()) {
        if(result.error==ArchivedDeleteError::ExplicitConfirmationRequired)
            m_message=QStringLiteral("Type the exact DELETE phrase to confirm.");
        else if(result.error==ArchivedDeleteError::CommitRejected)
            m_message=QStringLiteral("Deletion was not saved. The archived record remains.");
        else
            m_message=QStringLiteral("Deletion refused. Check the archived record.");
        emit stateChanged();
        return false;
    }
    // The C++ domain and its atomic writer have committed. Only now refresh.
    // This clears the removed selection, updates Overview totals and history.
    m_rentals.refresh();
    m_message=QStringLiteral("Archived rental %1 permanently deleted from this temporary demo.").arg(result.rentalId);
    emit stateChanged();
    return true;
}
