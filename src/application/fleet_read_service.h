#ifndef RENTAL_STUDIO_FLEET_READ_SERVICE_H
#define RENTAL_STUDIO_FLEET_READ_SERVICE_H

#include <QList>
#include <QString>
#include <optional>

class RentalSystem;

// A value-only view of legacy Fleet data. Qt/QML consumers do not own
// Vehicle objects or receive pointers to mutable domain entities.
struct FleetVehicleSummary {
    QString id;
    QString brand;
    QString model;
    QString type;
    QString details;
    // Preserve the legacy domain's double in this *read-only* adapter.
    // Exact monetary types are a separate approved P7 change, not P3-A.
    double legacyDailyRate = 0.0;
    bool available = false;

    QString displayName() const { return brand + QLatin1Char(' ') + model; }
};

// Thin, read-only application boundary. Fleet is captured as *values* on
// request, so returned snapshots remain valid after domain objects are
// mutated or destroyed. Future refresh/commands will need explicit contracts.
class FleetReadService final {
public:
    explicit FleetReadService(const RentalSystem &system);

    QList<FleetVehicleSummary> vehicles() const;
    std::optional<FleetVehicleSummary> vehicleById(const QString &id) const;

private:
    const RentalSystem &m_system;
};

#endif
