#include "fleet_add_controller.h"
#include "rentalsystem.h"
#include "fleet_photo_store.h"
#include <cmath>
#include <utility>

FleetAddController::FleetAddController(RentalSystem &system,bool disposable,
    std::function<void(const QString &)> afterCommit,QObject *parent)
    : QObject(parent),m_writer(system),m_disposable(disposable),
      m_afterCommit(std::move(afterCommit)) {}

bool FleetAddController::addVehicle(const QString &kind,const QString &brand,
    const QString &model,double dailyRate,int extra) {
    if(!m_disposable) {
        m_message=QStringLiteral("Fleet additions are disabled in persistent fictional-only mode.");
        emit messageChanged();
        return false;
    }
    if(kind!=QStringLiteral("Car") && kind!=QStringLiteral("Motorcycle")) {
        m_message=QStringLiteral("Choose Car or Motorcycle.");
        emit messageChanged();
        return false;
    }
    if(!std::isfinite(dailyRate) || dailyRate<=0 || dailyRate>1000000 ||
       (kind==QStringLiteral("Car") && (extra<2 || extra>8)) ||
       (kind==QStringLiteral("Motorcycle") && (extra<50 || extra>3000))) {
        m_message=QStringLiteral("Check the rate and vehicle specifications.");
        emit messageChanged();
        return false;
    }
    const FleetAddRequest request{
        kind==QStringLiteral("Car") ? FleetVehicleKind::Car : FleetVehicleKind::Motorcycle,
        QString{},brand,model,dailyRate,extra};
    const FleetWriteResult result=m_writer.add(request);
    if(!result.ok()) {
        m_message=result.error==FleetWriteError::InvalidDetails
          ? QStringLiteral("Enter a valid brand and model.")
          : QStringLiteral("Vehicle not saved. No fleet change confirmed.");
        emit messageChanged();
        return false;
    }
    // Business record has already committed. A photo is independent optional
    // fictional media; never report an image failure as a rejected vehicle.
    const bool attached=!m_photos || m_photos->saveStagedFor(result.vehicleId);
    if(m_afterCommit)m_afterCommit(result.vehicleId);
    m_message=attached
        ? QStringLiteral("Vehicle %1 added to this temporary demo.").arg(result.vehicleId)
        : QStringLiteral("Vehicle %1 added; optional photo was not attached.").arg(result.vehicleId);
    emit messageChanged();
    return true;
}
