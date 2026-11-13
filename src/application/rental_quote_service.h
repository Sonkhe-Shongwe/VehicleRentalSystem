#ifndef RENTAL_STUDIO_RENTAL_QUOTE_SERVICE_H
#define RENTAL_STUDIO_RENTAL_QUOTE_SERVICE_H
#include <QDate>
#include <QString>
#include <functional>
#include <optional>
class RentalSystem;

// Immutable-by-value preview, never a booking confirmation.
// Legacy double is retained until the separately gated money migration.
struct RentalQuotePreview {
    QString vehicleId;
    QString vehicleName;
    QDate startDate;
    QDate expectedReturnDate;
    int days = 0;
    double legacyDailyRate = 0.0;
    double legacyTotal = 0.0;
};
enum class RentalQuoteError {
    None, InvalidDuration, VehicleNotFound, VehicleUnavailable,
    InvalidPrice, InvalidDate
};
struct RentalQuoteResult {
    RentalQuoteError error = RentalQuoteError::None;
    std::optional<RentalQuotePreview> quote;
    bool ok() const {
        return error == RentalQuoteError::None && quote.has_value();
    }
};

// Narrow, read-only boundary; injectable date callback makes tests deterministic.
// The legacy domain must independently validate occupancy, price and dates
// when a future explicit createRental command actually commits a booking.
class RentalQuoteService final {
public:
    using Today = std::function<QDate()>;
    explicit RentalQuoteService(const RentalSystem &system,
                                Today today = {});
    RentalQuoteResult preview(const QString &vehicleId, int rentalDays) const;
private:
    const RentalSystem &m_system;
    Today m_today;
};
#endif
