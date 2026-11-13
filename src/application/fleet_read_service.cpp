#include "src/application/fleet_read_service.h"
#include "rentalsystem.h"
#include "vehicle.h"

namespace {
FleetVehicleSummary makeSummary(const Vehicle &vehicle) {
    FleetVehicleSummary summary;
    summary.id = vehicle.getId();
    summary.brand = vehicle.getBrand();
    summary.model = vehicle.getModel();
    summary.type = vehicle.typeName();
    summary.details = vehicle.extraDetails();
    summary.legacyDailyRate = vehicle.getPricePerDay();
    summary.available = !vehicle.getIsRented();
    return summary;
}
}

FleetReadService::FleetReadService(const RentalSystem &system)
    : m_system(system) {}

QList<FleetVehicleSummary> FleetReadService::vehicles() const {
    const QList<Vehicle*> domainVehicles = m_system.getVehicles();
    QList<FleetVehicleSummary> result;
    result.reserve(domainVehicles.size());
    for (const Vehicle *vehicle : domainVehicles) {
        if (vehicle != nullptr)
            result.append(makeSummary(*vehicle));
    }
    return result;
}

std::optional<FleetVehicleSummary> FleetReadService::vehicleById(const QString &id) const {
    // Retain the domain's case-insensitive / whitespace-trimmed ID semantics.
    const Vehicle *vehicle = m_system.findVehicleById(id);
    if (vehicle == nullptr)
        return std::nullopt;
    return makeSummary(*vehicle);
}
