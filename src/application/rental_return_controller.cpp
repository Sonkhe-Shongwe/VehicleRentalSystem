#include "src/application/rental_return_controller.h"

#include "rentalsystem.h"
#include "src/application/rental_list_model.h"

#include <utility>

RentalReturnService::RentalReturnService(
    RentalSystem &system, const RentalReadService &reads, Today today)
    : m_system(system), m_reads(reads), m_today(std::move(today)) {}

RentalReturnResult RentalReturnService::returnById(const QString &id) {
    const auto snapshot = m_reads.recordById(id);
    if (!snapshot)
        return {RentalReturnError::MissingRental, {}};
    if (snapshot->status != RentalRecordStatus::Active)
        return {RentalReturnError::NotActive, {}};

    const QDate today = m_today ? m_today() : m_system.today();
    // Do not call legacy mutation with invalid, pre-start or future dates.
    if (!today.isValid() || today < snapshot->startDate ||
        today > m_system.today())
        return {RentalReturnError::InvalidReturnDate, {}};

    // Legacy RentalSystem owns persistence AND rollback after QSaveFile
    // failure. A false result cannot distinguish I/O from final rejection.
    if (!m_system.completeRental(snapshot->rentalId, today))
        return {RentalReturnError::CommitFailed, {}};
    return {RentalReturnError::None, snapshot->rentalId};
}

RentalReturnController::RentalReturnController(
    RentalSystem &system, const RentalReadService &reads,
    RentalListModel &rentals, std::function<void()> refreshFleet,
    QObject *parent, RentalReturnService::Today today)
    : QObject(parent), m_service(system, reads, std::move(today)),
      m_reads(reads), m_rentals(rentals),
      m_refreshFleet(std::move(refreshFleet)) {
    connect(&m_rentals, &RentalListModel::selectionChanged, this,
            &RentalReturnController::stateChanged);
    connect(&m_rentals, &QAbstractItemModel::modelReset, this,
            &RentalReturnController::stateChanged);
    connect(&m_rentals, &RentalListModel::snapshotAvailabilityChanged, this,
            &RentalReturnController::stateChanged);
}

bool RentalReturnController::canReturnSelected() const {
    if (!m_rentals.snapshotAvailable())
        return false;
    const QString id = m_rentals.selectedRentalId();
    if (id.isEmpty() || m_rentals.selectedRow() < 0)
        return false;
    const auto item = m_reads.recordById(id);
    return item && item->status == RentalRecordStatus::Active;
}

bool RentalReturnController::returnSelectedFor(const QString &expectedRentalId) {
    if (expectedRentalId.isEmpty() ||
        expectedRentalId != m_rentals.selectedRentalId() ||
        m_rentals.selectedRow() < 0) {
        m_lastMessage = QStringLiteral(
            "Selection changed. Open the return confirmation again.");
        emit stateChanged();
        emit returnRejected(RentalReturnError::MissingRental);
        return false;
    }
    return returnSelected();
}

bool RentalReturnController::returnSelected() {
    if (!m_rentals.snapshotAvailable()) {
        m_lastMessage = QStringLiteral(
            "Rental data unavailable. Retry the read before returning.");
        emit stateChanged();
        emit returnRejected(RentalReturnError::MissingRental);
        return false;
    }
    // The controller never acts on a stale or invisible selection.
    const QString id = m_rentals.selectedRentalId();
    const RentalReturnResult result =
        (id.isEmpty() || m_rentals.selectedRow() < 0)
            ? RentalReturnResult{RentalReturnError::MissingRental, {}}
            : m_service.returnById(id);

    if (!result.ok()) {
        switch (result.error) {
        case RentalReturnError::MissingRental:
            m_lastMessage = QStringLiteral("Select a visible rental to return.");
            break;
        case RentalReturnError::NotActive:
            m_lastMessage = QStringLiteral("Only active rentals can be returned.");
            break;
        case RentalReturnError::InvalidReturnDate:
            m_lastMessage = QStringLiteral("Return date is not valid.");
            break;
        case RentalReturnError::CommitFailed:
            m_lastMessage = QStringLiteral("Return was not saved. No change confirmed.");
            break;
        case RentalReturnError::None:
            m_lastMessage = QStringLiteral("Return was not completed.");
            break;
        }
        emit stateChanged();
        emit returnRejected(result.error);
        return false;
    }

    // No optimistic receipt or refresh: this point is reached only after
    // RentalSystem successfully saves the return and updates occupancy.
    m_rentals.refresh();
    if (m_refreshFleet)
        m_refreshFleet();
    // The committed contract may have disappeared from Active, Overdue or
    // DueToday. Reveal its actual saved Returned state without another write.
    // As in booking, a visible record keeps the current filter.
    m_rentals.locateById(result.rentalId);
    m_lastMessage = QStringLiteral("Returned rental %1.").arg(result.rentalId);
    emit stateChanged();
    emit returnSucceeded(result.rentalId);
    return true;
}
