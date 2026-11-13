#include "src/application/rental_read_service.h"
#include "car.h"
#include "motorcycle.h"
#include "rental.h"
#include "rentalsystem.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>

namespace {
void check(bool value, const char *message) {
    if (!value) {
        std::cerr << "P3 Rental read FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
QByteArray readBytes(const QString &filename) {
    QFile file(filename);
    check(file.open(QIODevice::ReadOnly), "disposable file must be readable");
    return file.readAll();
}
void createRental(RentalSystem &system, const QString &id,
                  const QString &vehicle, int days, double rate) {
    const QDate today = QDate::currentDate();
    auto record = std::make_unique<Rental>(
        id, "Fictional Customer", "0000000000", vehicle,
        today, today.addDays(days), days, rate, rate * days);
    check(system.createRental(record.get()), "persist fictional rental");
    record.release(); // Domain owns only successfully persisted rentals.
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    check(dir.isValid(), "QTemporaryDir works");
    const QDate today = QDate::currentDate();
    check(today.isValid(), "system clock valid");
    const QString fleetPath = dir.filePath("vehicles.txt");
    const QString ledgerPath = dir.filePath("rentals.txt");
    QList<RentalRecordSummary> independentSnapshot;
    std::optional<RentalRecordSummary> independentLookup;

    {
        RentalSystem system(dir.path());
        RentalReadService empty(system);
        check(empty.records().isEmpty() &&
              empty.recordsByStatus(RentalRecordStatus::Active).isEmpty() &&
              !empty.recordById("R001").has_value(),
              "empty/missing rental collection");

        check(system.addVehicle(new Car("C001", "Toyota", "Corolla", 450, 4)),
              "fictional Corolla");
        check(system.addVehicle(new Car("C002", "Ford", "Ranger", 700, 4)),
              "fictional Ranger");
        check(system.addVehicle(new Motorcycle("M001", "Honda", "CB500F", 280, 471)),
              "fictional Honda");
        check(system.addVehicle(new Motorcycle("M002", "Yamaha", "MT-07", 300, 689)),
              "fictional Yamaha");
        createRental(system, "R001", "C001", 1, 450);
        createRental(system, "R002", "C002", 365, 700);
        createRental(system, "R003", "M001", 2, 280);
        createRental(system, "R004", "M002", 3, 300);
        check(system.completeRental("R003", today), "returned motorcycle");
        check(system.completeRental("R004", today), "other returned motorcycle");
        check(system.archiveRental("R004"), "archived motorcycle rental");

        const QByteArray beforeFleet = readBytes(fleetPath);
        const QByteArray beforeLedger = readBytes(ledgerPath);
        RentalReadService service(system, [today] { return today.addDays(3); });
        const auto items = service.records();
        check(items.size() == 4, "all real persisted records included");
        check(items.at(0).rentalId == "R001" &&
              items.at(0).vehicleId == "C001" &&
              items.at(0).status == RentalRecordStatus::Active &&
              items.at(0).dueState == RentalDueState::Overdue &&
              !items.at(0).actualReturnDate.isValid() &&
              items.at(0).rentalDays == 1 &&
              std::abs(items.at(0).legacyTotal - 450) < 0.001,
              "active overdue record projected from C++ state");
        check(items.at(1).status == RentalRecordStatus::Active &&
              items.at(1).dueState == RentalDueState::NotDue &&
              items.at(1).rentalDays == 365,
              "long active rental is not due");
        check(items.at(2).status == RentalRecordStatus::Returned &&
              items.at(2).dueState == RentalDueState::NotApplicable &&
              items.at(2).actualReturnDate == today,
              "returned rental status and actual date");
        check(items.at(3).status == RentalRecordStatus::Archived &&
              items.at(3).dueState == RentalDueState::NotApplicable &&
              items.at(3).actualReturnDate == today,
              "archived status has precedence over returned");
        check(service.recordsByStatus(RentalRecordStatus::Active).size() == 2 &&
              service.recordsByStatus(RentalRecordStatus::Returned).size() == 1 &&
              service.recordsByStatus(RentalRecordStatus::Archived).size() == 1,
              "active returned archived mutually exclusive filters");
        const auto found = service.recordById("  r003  ");
        check(found.has_value() && found->status == RentalRecordStatus::Returned &&
              found->vehicleId == "M001", "trimmed case-insensitive rental ID");
        check(!service.recordById("").has_value() &&
              !service.recordById("R999").has_value(),
              "blank/unknown ID returns no record");
        check(readBytes(fleetPath) == beforeFleet &&
              readBytes(ledgerPath) == beforeLedger,
              "all read and filter calls are strictly file non-mutating");

        const RentalReadService dueToday(system, [today] { return today.addDays(1); });
        check(dueToday.recordById("R001")->dueState == RentalDueState::DueToday,
              "deterministic injected clock recognizes due today");
        const RentalReadService invalidClock(system, [] { return QDate(); });
        check(invalidClock.recordById("R001")->dueState == RentalDueState::Unknown &&
              invalidClock.recordById("R003")->dueState ==
                  RentalDueState::NotApplicable,
              "invalid clock is explicit, not silently on-time");
        check(readBytes(ledgerPath) == beforeLedger,
              "clock variants cannot rewrite persisted rental data");

        independentSnapshot = items;
        independentLookup = found;
        check(system.completeRental("R001", today), "real domain return after read");
        check(independentSnapshot.at(0).status == RentalRecordStatus::Active,
              "previous summary not aliased to mutated domain");
        check(service.recordById("R001")->status == RentalRecordStatus::Returned,
              "fresh summary observes new domain state");
    }

    check(independentSnapshot.size() == 4 &&
          independentSnapshot.at(3).status == RentalRecordStatus::Archived &&
          independentLookup.has_value() &&
          independentLookup->rentalId == "R003",
          "read snapshots outlive RentalSystem safely");

    // Reload from actual temporary persistence, not in-memory mocks.
    {
        RentalSystem reloaded(dir.path());
        RentalReadService history(reloaded, [today] { return today.addDays(4); });
        check(history.records().size() == 4 &&
              history.recordsByStatus(RentalRecordStatus::Active).size() == 1 &&
              history.recordsByStatus(RentalRecordStatus::Returned).size() == 2 &&
              history.recordsByStatus(RentalRecordStatus::Archived).size() == 1,
              "all states reconstructed after actual domain restart");
        const QByteArray beforeHistoryFleet = readBytes(fleetPath);
        const QByteArray beforeHistoryRead = readBytes(ledgerPath);
        check(reloaded.hasRentalHistoryForVehicle(" m001 ") &&
              !reloaded.removeVehicle("M001") &&
              reloaded.findVehicleById("M001") != nullptr,
              "returned historical rental prevents orphaning the referenced vehicle");
        const auto historical = history.recordById("r003");
        check(historical && historical->vehicleId == "M001" &&
              historical->status == RentalRecordStatus::Returned,
              "historical rental remains accessible with existing vehicle");
        check(readBytes(fleetPath) == beforeHistoryFleet &&
              readBytes(ledgerPath) == beforeHistoryRead,
              "read and protected failed removal never change either ledger");
    }
    std::cout << "P3 read-only typed Rentals states and restart PASS\n";
    return EXIT_SUCCESS;
}
