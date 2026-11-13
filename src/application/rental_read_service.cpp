#include "src/application/rental_read_service.h"
#include "rentalsystem.h"
#include "rental.h"

#include <utility>

namespace {
RentalRecordSummary makeSummary(const Rental &rental, const QDate &today) {
    RentalRecordSummary summary;
    summary.rentalId = rental.getRentalId();
    summary.vehicleId = rental.getVehicleId();
    summary.startDate = rental.getStartDate();
    summary.expectedReturnDate = rental.getExpectedReturnDate();
    summary.actualReturnDate = rental.getActualReturnDate();
    summary.rentalDays = rental.getRentalDays();
    summary.legacyDailyRate = rental.getPricePerDay();
    summary.legacyTotal = rental.getTotalCost();

    // The legacy domain owns the status flags and their precedence.
    summary.status = rental.getIsArchived()
        ? RentalRecordStatus::Archived
        : rental.getIsReturned()
            ? RentalRecordStatus::Returned
            : RentalRecordStatus::Active;

    if (summary.status != RentalRecordStatus::Active) {
        summary.dueState = RentalDueState::NotApplicable;
    } else if (!today.isValid() || !summary.expectedReturnDate.isValid()) {
        // An invalid injected clock cannot silently imply "not overdue".
        summary.dueState = RentalDueState::Unknown;
    } else if (summary.expectedReturnDate < today) {
        summary.dueState = RentalDueState::Overdue;
    } else if (summary.expectedReturnDate == today) {
        summary.dueState = RentalDueState::DueToday;
    } else {
        summary.dueState = RentalDueState::NotDue;
    }
    return summary;
}
}

RentalReadService::RentalReadService(const RentalSystem &system, Today today)
    : m_system(system), m_today(std::move(today)) {}

QList<RentalRecordSummary> RentalReadService::records() const {
    const QDate today = m_today ? m_today() : m_system.today();
    const QList<Rental*> source = m_system.getRentals();
    QList<RentalRecordSummary> result;
    result.reserve(source.size());
    for (const Rental *rental : source) {
        if (rental != nullptr)
            result.append(makeSummary(*rental, today));
    }
    return result;
}

QList<RentalRecordSummary> RentalReadService::recordsByStatus(
        RentalRecordStatus status) const {
    QList<RentalRecordSummary> result;
    for (const RentalRecordSummary &item : records()) {
        if (item.status == status)
            result.append(item);
    }
    return result;
}

std::optional<RentalRecordSummary> RentalReadService::recordById(
        const QString &id) const {
    // Delegate trimmed, case-insensitive IDs to the authoritative domain.
    const Rental *rental = m_system.findRentalById(id);
    if (rental == nullptr)
        return std::nullopt;
    const QDate today = m_today ? m_today() : m_system.today();
    return makeSummary(*rental, today);
}
