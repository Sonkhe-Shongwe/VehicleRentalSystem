#include "src/application/rental_confirm_service.h"
#include "rentalsystem.h"
#include "rental.h"
#include <cmath>
#include <memory>

namespace {
bool validText(const QString &value) {
    return !value.trimmed().isEmpty() && !value.contains('|') &&
           !value.contains('\n') && !value.contains('\r');
}
bool sameQuote(const RentalQuotePreview &reviewed, const RentalQuotePreview &fresh) {
    // Displayed values are never accepted as the current business truth.
    if (!std::isfinite(reviewed.legacyDailyRate) ||
        !std::isfinite(reviewed.legacyTotal))
        return false;
    return reviewed.vehicleId.trimmed().compare(fresh.vehicleId, Qt::CaseInsensitive) == 0 &&
           reviewed.vehicleName == fresh.vehicleName &&
           reviewed.startDate == fresh.startDate &&
           reviewed.expectedReturnDate == fresh.expectedReturnDate &&
           reviewed.days == fresh.days &&
           std::abs(reviewed.legacyDailyRate - fresh.legacyDailyRate) <= 0.005 &&
           std::abs(reviewed.legacyTotal - fresh.legacyTotal) <= 0.005;
}
}

RentalConfirmService::RentalConfirmService(RentalSystem &system)
    : m_system(system) {}

RentalConfirmationResult RentalConfirmService::confirm(const RentalConfirmationRequest &request) {
    if (!validText(request.customerName))
        return {RentalConfirmationError::InvalidCustomerName, std::nullopt};
    if (!validText(request.phoneNumber))
        return {RentalConfirmationError::InvalidPhoneNumber, std::nullopt};

    if (request.reviewedQuote.vehicleId.trimmed().isEmpty() ||
        request.reviewedQuote.days < 1 || request.reviewedQuote.days > 365 ||
        !request.reviewedQuote.startDate.isValid() ||
        !request.reviewedQuote.expectedReturnDate.isValid())
        return {RentalConfirmationError::InvalidQuote, std::nullopt};

    // A quote from the review screen is only a snapshot. Recheck today's
    // availability, cost and dates, even if the user confirmed it immediately.
    const RentalQuoteService quoteService(m_system);
    const auto fresh = quoteService.preview(request.reviewedQuote.vehicleId,
                                            request.reviewedQuote.days);
    if (!fresh.ok()) {
        switch (fresh.error) {
        case RentalQuoteError::VehicleNotFound:
            return {RentalConfirmationError::VehicleNotFound, std::nullopt};
        case RentalQuoteError::VehicleUnavailable:
            return {RentalConfirmationError::VehicleUnavailable, std::nullopt};
        case RentalQuoteError::InvalidDuration:
        case RentalQuoteError::InvalidDate:
        case RentalQuoteError::InvalidPrice:
        case RentalQuoteError::None:
            return {RentalConfirmationError::InvalidQuote, std::nullopt};
        }
    }
    if (!sameQuote(request.reviewedQuote, *fresh.quote))
        return {RentalConfirmationError::QuoteStale, std::nullopt};

    const QString rentalId = m_system.generateNextRentalId();
    if (rentalId.isEmpty() || m_system.findRentalById(rentalId) != nullptr)
        return {RentalConfirmationError::CommitFailed, std::nullopt};

    // Keep ownership until the authoritative domain accepts the rental.
    // Existing createRental() verifies today's dates/rate/availability and
    // rolls back the occupancy flag + ledger list when QSaveFile fails.
    auto rental = std::make_unique<Rental>(
        rentalId, request.customerName.trimmed(), request.phoneNumber.trimmed(),
        fresh.quote->vehicleId, fresh.quote->startDate, fresh.quote->expectedReturnDate,
        fresh.quote->days, fresh.quote->legacyDailyRate, fresh.quote->legacyTotal);
    if (!m_system.createRental(rental.get()))
        return {RentalConfirmationError::CommitFailed, std::nullopt};
    rental.release(); // Domain owns the accepted record.

    RentalConfirmationReceipt receipt;
    receipt.rentalId = rentalId;
    receipt.vehicleId = fresh.quote->vehicleId;
    receipt.startDate = fresh.quote->startDate;
    receipt.expectedReturnDate = fresh.quote->expectedReturnDate;
    receipt.days = fresh.quote->days;
    receipt.legacyTotal = fresh.quote->legacyTotal;
    return {RentalConfirmationError::None, std::move(receipt)};
}
