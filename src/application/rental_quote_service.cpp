#include "src/application/rental_quote_service.h"
#include "rentalsystem.h"
#include "vehicle.h"
#include <cmath>
#include <utility>

RentalQuoteService::RentalQuoteService(const RentalSystem &system, Today today)
    : m_system(system), m_today(std::move(today)) {}

RentalQuoteResult RentalQuoteService::preview(const QString &vehicleId,
                                               int rentalDays) const {
    if (rentalDays < 1 || rentalDays > 365)
        return {RentalQuoteError::InvalidDuration, std::nullopt};

    const Vehicle *vehicle = m_system.findVehicleById(vehicleId);
    if (vehicle == nullptr)
        return {RentalQuoteError::VehicleNotFound, std::nullopt};
    if (vehicle->getIsRented() ||
        m_system.findActiveRentalByVehicleId(vehicle->getId()) != nullptr)
        return {RentalQuoteError::VehicleUnavailable, std::nullopt};

    const double daily = vehicle->getPricePerDay();
    const double total = daily * static_cast<double>(rentalDays);
    if (!std::isfinite(daily) || daily <= 0 ||
        !std::isfinite(total) || total <= 0)
        return {RentalQuoteError::InvalidPrice, std::nullopt};

    const QDate start = m_today ? m_today() : m_system.today();
    const QDate end = start.addDays(rentalDays);
    if (!start.isValid() || !end.isValid())
        return {RentalQuoteError::InvalidDate, std::nullopt};

    RentalQuotePreview quote;
    quote.vehicleId = vehicle->getId();
    quote.vehicleName = vehicle->getBrand() + QLatin1Char(' ') + vehicle->getModel();
    quote.startDate = start;
    quote.expectedReturnDate = end;
    quote.days = rentalDays;
    quote.legacyDailyRate = daily;
    quote.legacyTotal = total;
    return {RentalQuoteError::None, std::move(quote)};
}
