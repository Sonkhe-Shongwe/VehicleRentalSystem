#pragma once
// P7-J: purpose-limited normalized SQL -> existing Rental Studio read DTOs.
// Used only in explicitly opt-in generated-fictional builds. Never stores
// contacts or converts legacy domain doubles back into commercial money.
#include <QSet>
#include <optional>
#include "src/application/p7_sandbox/sqlite_repository.h"
#include "src/application/fleet_read_service.h"
#include "src/application/rental_read_service.h"
namespace p7fiction {
class ReadProjection final {
public:
    explicit ReadProjection(const Repository &repo):m_repo(repo){}
    std::optional<QList<FleetVehicleSummary>> fleet() const {
        auto snapshot=m_repo.fleetRows();
        if(!snapshot)return {};
        QSet<QString> occupied;
        for(const auto &r:snapshot->rentals) {
            if(r.status=="ACTIVE")occupied.insert(r.vehicleId.toUpper());
        }
        QList<FleetVehicleSummary> out;
        for(const auto &v:snapshot->vehicles) {
            FleetVehicleSummary row;
            row.id=v.id;row.brand=v.brand;row.model=v.model;row.type=v.kind;
            row.details=v.kind=="Car"?QString("%1 doors").arg(v.spec):
                       QString("%1 CC").arg(v.spec);
            // Existing QML expects legacy display doubles. The database
            // remains authoritative int64 cents; this is display-only.
            row.legacyDailyRate=static_cast<double>(v.rateCents)/100.0;
            row.available=v.state=="ACTIVE"&&!occupied.contains(v.id.toUpper());
            out.append(row);
        }
        return out;
    }
    std::optional<QList<RentalRecordSummary>> rentals(const QDate &today) const {
        if(!today.isValid())return {};
        // Fleet and Rentals expose the same cross-validated transaction.
        // Do not publish orphaned or active-on-retired contracts as verified.
        auto source=m_repo.fleetRows();
        if(!source)return {};
        QList<RentalRecordSummary> out;
        for(const auto &r:source->rentals) {
            RentalRecordSummary row;
            row.rentalId=r.id;row.vehicleId=r.vehicleId;
            row.startDate=r.start;row.expectedReturnDate=r.expected;
            row.actualReturnDate=r.actual;row.rentalDays=r.days;
            row.legacyDailyRate=static_cast<double>(r.dailyCents)/100.0;
            row.legacyTotal=static_cast<double>(r.totalCents)/100.0;
            row.status=r.status=="ACTIVE"?RentalRecordStatus::Active:
                       r.status=="RETURNED"?RentalRecordStatus::Returned:
                                             RentalRecordStatus::Archived;
            row.dueState=row.status!=RentalRecordStatus::Active?
                RentalDueState::NotApplicable:
                r.expected<today?RentalDueState::Overdue:
                r.expected==today?RentalDueState::DueToday:RentalDueState::NotDue;
            out.append(row);
        }
        return out;
    }
private:
    const Repository &m_repo;
};
}
