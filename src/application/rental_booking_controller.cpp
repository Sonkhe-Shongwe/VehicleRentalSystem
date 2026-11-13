#include "src/application/rental_booking_controller.h"
#include "src/application/rental_list_model.h"
#include "rentalsystem.h"
#include <utility>

RentalBookingController::RentalBookingController(
    RentalSystem &system, RentalListModel &rentals,
    std::function<void()> refreshFleet, QObject *parent,
    RentalQuoteService::Today today)
    : QObject(parent), m_quotes(system, std::move(today)),
      m_confirm(system), m_rentals(rentals),
      m_refreshFleet(std::move(refreshFleet)) {}

QString RentalBookingController::quoteSummary() const {
    if (!m_reviewed)
        return QStringLiteral("Preview the current domain quote before confirming.");
    // All pricing and availability comes from RentalQuoteService; QML
    // receives only a formatted immutable snapshot for presentation.
    return QStringLiteral("%1 (%2) | %3 days | R %4/day | Total R %5 | "
                          "Return %6")
        .arg(m_reviewed->vehicleName, m_reviewed->vehicleId)
        .arg(m_reviewed->days)
        .arg(QString::number(m_reviewed->legacyDailyRate,'f',2))
        .arg(QString::number(m_reviewed->legacyTotal,'f',2))
        .arg(m_reviewed->expectedReturnDate.toString(Qt::ISODate));
}

bool RentalBookingController::preview(const QString &vehicleId, int days) {
    // Discard any previous preview BEFORE a new invalid request, ensuring no
    // stale Confirm action remains available.
    m_reviewed.reset();
    m_needsFreshQuote=false;
    const auto result=m_quotes.preview(vehicleId,days);
    if (!result.ok()) {
        switch (result.error) {
        case RentalQuoteError::InvalidDuration:
            m_message=QStringLiteral("Rental days must be between 1 and 365.");
            break;
        case RentalQuoteError::VehicleNotFound:
            m_message=QStringLiteral("Vehicle not found.");
            break;
        case RentalQuoteError::VehicleUnavailable:
            m_message=QStringLiteral("Vehicle is not available.");
            break;
        case RentalQuoteError::InvalidPrice:
        case RentalQuoteError::InvalidDate:
        case RentalQuoteError::None:
            m_message=QStringLiteral("Cannot preview that rental.");
            break;
        }
        emit quoteChanged();
        emit messageChanged();
        return false;
    }
    m_reviewed=*result.quote;
    m_message=QStringLiteral("Review the current quote, then confirm.");
    emit quoteChanged();
    emit messageChanged();
    return true;
}

void RentalBookingController::discardPreview() {
    if (m_reviewed || m_needsFreshQuote) {
        m_reviewed.reset();
        m_needsFreshQuote=false;
        emit quoteChanged();
    }
    m_message.clear();
    emit messageChanged();
}

bool RentalBookingController::confirm(const QString &customerName,
                                       const QString &phoneNumber) {
    if(m_persistentFictionalOnly &&
       (customerName!=QStringLiteral("Fictional Demo Session") ||
        phoneNumber!=QStringLiteral("0000000000"))) {
        m_message=QStringLiteral("Persistent demo accepts fictional contact only.");
        emit messageChanged();
        emit bookingRejected(RentalConfirmationError::InvalidCustomerName);
        return false;
    }
    if (!m_reviewed) {
        m_message=QStringLiteral("Preview a valid quote first.");
        emit messageChanged();
        emit bookingRejected(RentalConfirmationError::InvalidQuote);
        return false;
    }
    // The service independently recalculates and rejects stale prices,
    // changed availability, old dates, invalid input and failed QSaveFile.
    RentalConfirmationRequest request{*m_reviewed,customerName,phoneNumber};
    const auto result=m_confirm.confirm(request);
    if (!result.ok()) {
        switch (result.error) {
        case RentalConfirmationError::InvalidCustomerName:
            m_message=QStringLiteral("Enter a valid customer name."); break;
        case RentalConfirmationError::InvalidPhoneNumber:
            m_message=QStringLiteral("Enter a valid contact number."); break;
        case RentalConfirmationError::QuoteStale:
            m_message=QStringLiteral("Quote changed. Preview again."); break;
        case RentalConfirmationError::VehicleUnavailable:
            m_message=QStringLiteral("Vehicle became unavailable. Preview again."); break;
        case RentalConfirmationError::VehicleNotFound:
            m_message=QStringLiteral("Vehicle no longer exists. Preview again."); break;
        case RentalConfirmationError::CommitFailed:
            m_message=QStringLiteral("Booking was not saved. No rental confirmed."); break;
        case RentalConfirmationError::InvalidQuote:
        case RentalConfirmationError::None:
            m_message=QStringLiteral("Quote invalid. Preview again."); break;
        }
        if (result.error==RentalConfirmationError::QuoteStale ||
            result.error==RentalConfirmationError::VehicleUnavailable ||
            result.error==RentalConfirmationError::VehicleNotFound ||
            result.error==RentalConfirmationError::InvalidQuote) {
            m_reviewed.reset();
            m_needsFreshQuote=true;
            emit quoteChanged();
        }
        emit messageChanged();
        emit bookingRejected(result.error);
        return false;
    }
    // This path ONLY runs after the authoritative QSaveFile commit.
    const QString id=result.receipt->rentalId;
    m_reviewed.reset();
    m_needsFreshQuote=false;
    m_rentals.refresh();
    if (m_refreshFleet)
        m_refreshFleet();
    // A committed rental must be visible to the operator even if History or
    // a return-timing filter had hidden newly active contracts. Explicitly
    // revealing this receipt is read-only navigation: non-excluding filters
    // remain unchanged, while excluding filters reset to All.
    m_rentals.locateById(id);
    m_lastRentalId=id; // Store only transaction ID, never customer contact.
    m_message=QStringLiteral("Booking %1 saved.").arg(id);
    emit quoteChanged();
    emit messageChanged();
    emit bookingSucceeded(id);
    return true;
}
