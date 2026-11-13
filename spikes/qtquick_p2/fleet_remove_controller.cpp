#include "fleet_remove_controller.h"
#include "rentalsystem.h"
#include "vehicle.h"
#include <utility>

FleetRemoveController::FleetRemoveController(
    RentalSystem &system,bool disposable,std::function<void()> afterCommit,
    QObject *parent)
    : QObject(parent),m_writer(system),m_system(system),
      m_disposable(disposable),m_afterCommit(std::move(afterCommit)) {}

bool FleetRemoveController::hasRentalHistory(const QString &id) const {
    return m_system.hasRentalHistoryForVehicle(id);
}

bool FleetRemoveController::canRemoveVehicle(const QString &id) const {
    const Vehicle *vehicle=m_system.findVehicleById(id);
    return m_disposable && vehicle && !vehicle->getIsRented() &&
           !m_system.hasRentalHistoryForVehicle(vehicle->getId());
}

void FleetRemoveController::clearMessage() {
    if(m_message.isEmpty())return;
    m_message.clear();
    emit messageChanged();
}

bool FleetRemoveController::removeVehicle(
    const QString &id,const QString &confirmation) {
    if(!m_disposable) {
        m_message=QStringLiteral("Vehicle removal is disabled in persistent fictional-only mode.");
        emit messageChanged();
        return false;
    }
    const Vehicle *vehicle=m_system.findVehicleById(id);
    // The C++ service uses case-insensitive lookup; an exact canonical ID
    // avoids deleting an unintended match after a stale UI selection.
    if(!vehicle || id!=vehicle->getId()) {
        m_message=QStringLiteral("Vehicle is no longer available. Refresh Fleet.");
        emit messageChanged();
        return false;
    }
    if(confirmation!=QStringLiteral("REMOVE ")+vehicle->getId()) {
        m_message=QStringLiteral("Type the exact confirmation phrase to remove this vehicle.");
        emit messageChanged();
        return false;
    }
    const FleetWriteResult result=m_writer.remove(vehicle->getId());
    if(!result.ok()) {
        if(result.error==FleetWriteError::VehicleInUse)
            m_message=QStringLiteral("An active rental still references this vehicle. Return it first.");
        else if(result.error==FleetWriteError::VehicleReferencedByRental)
            m_message=QStringLiteral("Rental history still references this vehicle. Preserve its record or explicitly delete archived contracts first.");
        else
            m_message=QStringLiteral("Vehicle removal failed. No change was saved.");
        emit messageChanged();
        return false;
    }
    if(m_afterCommit)m_afterCommit();
    m_message=QStringLiteral("Vehicle %1 removed from this temporary demo.").arg(result.vehicleId);
    emit messageChanged();
    return true;
}
