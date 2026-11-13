#include "src/application/rental_admin_commands.h"
#include "rentalsystem.h"
#include "car.h"
#include "motorcycle.h"
#include "rental.h"
#include "vehicle.h"
#include <cmath>
#include <memory>

namespace {
bool validText(const QString &s) {
    return !s.trimmed().isEmpty() &&
        !s.contains('|') && !s.contains('\r') && !s.contains('\n');
}
}
FleetWriteService::FleetWriteService(RentalSystem &system)
    : m_system(system) {}

FleetWriteResult FleetWriteService::add(const FleetAddRequest &req) {
    if (!validText(req.brand) || !validText(req.model) ||
        ( !req.id.isEmpty() && !validText(req.id)) ||
        !std::isfinite(req.dailyRate) || req.dailyRate<=0 || req.extra<=0)
        return {FleetWriteError::InvalidDetails, {}};
    const QString type=req.kind==FleetVehicleKind::Car
        ? QStringLiteral("Car") : QStringLiteral("Motorcycle");
    const QString id=req.id.isEmpty()
        ? m_system.generateNextVehicleId(type) : req.id.trimmed();
    if (!validText(id))
        return {FleetWriteError::InvalidDetails, {}};
    if (m_system.findVehicleById(id))
        return {FleetWriteError::DuplicateId, {}};
    std::unique_ptr<Vehicle> v;
    if (req.kind==FleetVehicleKind::Car) {
        v=std::make_unique<Car>(id,req.brand.trimmed(),req.model.trimmed(),
                                req.dailyRate,req.extra);
    } else {
        v=std::make_unique<Motorcycle>(id,req.brand.trimmed(),req.model.trimmed(),
                                       req.dailyRate,req.extra);
    }
    if (!m_system.addVehicle(v.get()))
        return {FleetWriteError::CommitRejected, {}};
    v.release(); // Domain now owns the accepted object.
    return {FleetWriteError::None,id};
}

FleetWriteResult FleetWriteService::remove(const QString &id) {
    const Vehicle *item=m_system.findVehicleById(id);
    if (!item)
        return {FleetWriteError::VehicleNotFound,{}};
    if (item->getIsRented() ||
        m_system.findActiveRentalByVehicleId(item->getId()))
        return {FleetWriteError::VehicleInUse,{}};
    // Active contracts are reported as in use; returned/archived contracts
    // still protect the vehicle's foreign-key-like identifier.
    if(m_system.hasRentalHistoryForVehicle(item->getId()))
        return {FleetWriteError::VehicleReferencedByRental,{}};
    const QString canonical=item->getId();
    if (!m_system.removeVehicle(canonical))
        return {FleetWriteError::CommitRejected,{}};
    return {FleetWriteError::None,canonical};
}

ArchivedDeleteService::ArchivedDeleteService(RentalSystem &system)
    : m_system(system) {}

ArchivedDeleteResult ArchivedDeleteService::permanentlyDelete(
        const QString &id,const QString &confirmation) {
    const Rental *item=m_system.findRentalById(id);
    if (!item)
        return {ArchivedDeleteError::MissingRental,{}};
    if (!item->getIsArchived())
        return {ArchivedDeleteError::NotArchived,{}};
    const QString canonical=item->getRentalId();
    // Exact, case-sensitive explicit operation and canonical identifier.
    // No accidental deletion from a merely selected row or generic yes/no.
    if (confirmation!=QStringLiteral("DELETE ")+canonical)
        return {ArchivedDeleteError::ExplicitConfirmationRequired,{}};
    if (!m_system.deleteArchivedRental(canonical))
        return {ArchivedDeleteError::CommitRejected,{}};
    return {ArchivedDeleteError::None,canonical};
}
