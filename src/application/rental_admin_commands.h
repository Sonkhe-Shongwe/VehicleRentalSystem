#ifndef RENTAL_STUDIO_ADMIN_COMMANDS_H
#define RENTAL_STUDIO_ADMIN_COMMANDS_H

#include <QString>

class RentalSystem;

enum class FleetVehicleKind { Car, Motorcycle };
struct FleetAddRequest {
    FleetVehicleKind kind = FleetVehicleKind::Car;
    QString id; // Empty means use the existing legacy next-ID policy.
    QString brand;
    QString model;
    double dailyRate = 0;
    int extra = 0; // Car doors / Motorcycle engineCC under legacy schema.
};
enum class FleetWriteError {
    None, InvalidDetails, DuplicateId, VehicleNotFound,
    VehicleInUse, VehicleReferencedByRental, CommitRejected
};
struct FleetWriteResult {
    FleetWriteError error = FleetWriteError::None;
    QString vehicleId;
    bool ok() const { return error == FleetWriteError::None &&
                             !vehicleId.isEmpty(); }
};

// Typed entry points for the fleet writes used by the legacy application.
// Domain validation and single-file atomic commits remain authoritative.
class FleetWriteService final {
public:
    explicit FleetWriteService(RentalSystem &system);
    FleetWriteResult add(const FleetAddRequest &request);
    FleetWriteResult remove(const QString &id);
private:
    RentalSystem &m_system;
};

enum class ArchivedDeleteError {
    None, MissingRental, NotArchived,
    ExplicitConfirmationRequired, CommitRejected
};
struct ArchivedDeleteResult {
    ArchivedDeleteError error = ArchivedDeleteError::None;
    QString rentalId;
    bool ok() const { return error == ArchivedDeleteError::None &&
                             !rentalId.isEmpty(); }
};

// Irreversible operation is intentionally NOT exposed to QML.
// A deliberate, exact confirmation text is required, and the existing
// domain guards against deletion of any non-archived rental.
class ArchivedDeleteService final {
public:
    explicit ArchivedDeleteService(RentalSystem &system);
    ArchivedDeleteResult permanentlyDelete(const QString &rentalId,
                                            const QString &confirmationText);
private:
    RentalSystem &m_system;
};

#endif
