#ifndef RENTAL_STUDIO_RENTAL_CONFIRM_SERVICE_H
#define RENTAL_STUDIO_RENTAL_CONFIRM_SERVICE_H

#include "src/application/rental_quote_service.h"
#include <QDate>
#include <QString>
#include <optional>

class RentalSystem;

// This is an explicit application command, not navigation or quote preview.
// The quote is untrusted input and will be checked against today's C++ data.
struct RentalConfirmationRequest {
    RentalQuotePreview reviewedQuote;
    QString customerName;
    QString phoneNumber;
};

struct RentalConfirmationReceipt {
    QString rentalId;
    QString vehicleId;
    QDate startDate;
    QDate expectedReturnDate;
    int days = 0;
    double legacyTotal = 0.0;
};

enum class RentalConfirmationError {
    None,
    InvalidCustomerName,
    InvalidPhoneNumber,
    InvalidQuote,
    QuoteStale,
    VehicleNotFound,
    VehicleUnavailable,
    // Legacy RentalSystem::createRental returns bool, so failure can mean
    // persistence I/O or last-moment domain rejection. Do not mislabel it.
    CommitFailed
};

struct RentalConfirmationResult {
    RentalConfirmationError error = RentalConfirmationError::None;
    std::optional<RentalConfirmationReceipt> receipt;
    bool ok() const { return error == RentalConfirmationError::None && receipt.has_value(); }
};

// A single narrowly scoped C++ application boundary. The domain remains the
// authority on eligibility and the ONLY writer of the rental ledger.
class RentalConfirmService final {
public:
    explicit RentalConfirmService(RentalSystem &system);
    RentalConfirmationResult confirm(const RentalConfirmationRequest &request);
private:
    RentalSystem &m_system;
};

#endif
