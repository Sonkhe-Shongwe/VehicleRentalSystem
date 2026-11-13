#include "src/application/rental_quote_service.h"
#include "car.h"
#include "motorcycle.h"
#include "rental.h"
#include "rentalsystem.h"
#include <QCoreApplication>
#include <QDate>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
static void check(bool ok, const char *msg) {
    if (!ok) { std::cerr << "P3 quote FAIL: " << msg << '\n'; std::exit(1); }
}
static QByteArray bytes(const QString &path) {
    QFile f(path);
    check(f.open(QIODevice::ReadOnly), "fixture readable");
    return f.readAll();
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    check(dir.isValid(), "disposable storage");
    RentalSystem domain(dir.path());
    check(domain.addVehicle(new Car("C001", "Toyota", "Corolla", 450.0, 4)),
          "add Corolla");
    check(domain.addVehicle(new Car("C002", "Ford", "Ranger", 700.0, 4)),
          "add Ranger");
    check(domain.addVehicle(new Motorcycle("M001", "Honda", "CB500F", 280.0, 471)),
          "add Honda");
    check(domain.rentVehicle("C002"), "rent fictional Ranger");
    const QString vehicles = dir.filePath("vehicles.txt");
    const QString rentals = dir.filePath("rentals.txt");

    const RentalQuoteService service(domain, [] { return QDate(2026, 10, 30); });
    const QByteArray before = bytes(vehicles);
    const auto valid = service.preview(" c001 ", 3);
    check(valid.ok(), "3-day Corolla quote succeeds");
    check(valid.quote->vehicleId == "C001" &&
          valid.quote->vehicleName == "Toyota Corolla",
          "domain-normalized ID and name");
    check(valid.quote->startDate == QDate(2026, 10, 30) &&
          valid.quote->expectedReturnDate == QDate(2026, 11, 2),
          "clock injection and return date");
    check(valid.quote->days == 3 &&
          std::abs(valid.quote->legacyDailyRate - 450.0) < .001 &&
          std::abs(valid.quote->legacyTotal - 1350.0) < .001,
          "R450 x 3 = R1350");
    check(service.preview("C002", 3).error == RentalQuoteError::VehicleUnavailable,
          "rented Ford rejected");
    check(service.preview("M999", 1).error == RentalQuoteError::VehicleNotFound &&
          service.preview("", 1).error == RentalQuoteError::VehicleNotFound,
          "missing/empty vehicle ID");
    check(service.preview("C001", 0).error == RentalQuoteError::InvalidDuration &&
          service.preview("C001", -1).error == RentalQuoteError::InvalidDuration &&
          service.preview("C001", 366).error == RentalQuoteError::InvalidDuration,
          "invalid 1..365 day bounds");
    const auto maxDays = service.preview("C001", 365);
    check(maxDays.ok() &&
          maxDays.quote->expectedReturnDate == QDate(2026, 10, 30).addDays(365),
          "365 days accepted");
    const RentalQuoteService noDate(domain, [] { return QDate(); });
    check(noDate.preview("C001", 1).error == RentalQuoteError::InvalidDate,
          "invalid clock yields no quote");
    check(bytes(vehicles) == before && !QFile::exists(rentals),
          "all preview branches leave files untouched");

    check(domain.addVehicle(new Car("C900", "Test", "Overflow",
                                     std::numeric_limits<double>::max(), 4)),
          "huge finite price added to disposable domain");
    const QByteArray afterSetup = bytes(vehicles);
    check(service.preview("C900", 365).error == RentalQuoteError::InvalidPrice,
          "overflowing quote total rejected");
    const auto unavailable = service.preview("C002", 2);
    check(!unavailable.ok() && !unavailable.quote.has_value(),
          "typed failure has no stale quote");
    check(bytes(vehicles) == afterSetup && !QFile::exists(rentals),
          "no preview creates/changes persistence");

    // Only existing C++ domain operation can commit a rental. A successful
    // preview does not reserve the vehicle and is revalidated on commit.
    const RentalQuoteService live(domain);
    const auto q = live.preview("C001", 3);
    check(q.ok(), "same-day production preview");
    auto *record = new Rental("R001", "Fictional Customer", "0000000000", "C001",
                              q.quote->startDate, q.quote->expectedReturnDate,
                              q.quote->days, q.quote->legacyDailyRate,
                              q.quote->legacyTotal);
    const bool saved = domain.createRental(record);
    if (!saved) delete record;
    check(saved && domain.findActiveRentalByVehicleId("C001") != nullptr,
          "authoritative domain independently accepts matching quote");
    check(live.preview("C001", 3).error == RentalQuoteError::VehicleUnavailable,
          "quote unavailable immediately after booking");

    std::cout << "P3 typed quote and authoritative domain parity PASS\n";
    return 0;
}
