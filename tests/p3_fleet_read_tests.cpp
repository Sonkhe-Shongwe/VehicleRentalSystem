#include "src/application/fleet_read_service.h"
#include "car.h"
#include "motorcycle.h"
#include "rentalsystem.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>

static void check(bool ok, const char *message) {
    if (!ok) {
        std::cerr << "P3 Fleet read boundary FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

static QByteArray readBytes(const QString &path) {
    QFile f(path);
    check(f.open(QIODevice::ReadOnly), "test fixture file must be readable");
    return f.readAll();
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir disposable;
    check(disposable.isValid(), "QTemporaryDir available");
    QList<FleetVehicleSummary> survivingSnapshot;
    std::optional<FleetVehicleSummary> survivingLookup;
    {
        RentalSystem domain(disposable.path());
        auto *car = new Car("C001", "Toyota", "Corolla", 450.0, 4);
        auto *motorcycle = new Motorcycle("M001", "Honda", "CB500F", 280.0, 471);
        check(domain.addVehicle(car), "add fictional car");
        check(domain.addVehicle(motorcycle), "add fictional motorcycle");
        check(domain.rentVehicle("M001"), "fictional motorcycle rented");
        const QByteArray originalVehicles = readBytes(disposable.filePath("vehicles.txt"));

        const FleetReadService service(domain);
        const auto items = service.vehicles();
        check(items.size() == 2, "snapshot count");
        check(items.at(0).id == "C001" && items.at(0).type == "Car",
              "car stable ID and type");
        check(items.at(0).brand == "Toyota" && items.at(0).model == "Corolla"
              && items.at(0).displayName() == "Toyota Corolla",
              "brand/model and display name");
        check(items.at(0).details == "4 doors" && items.at(0).available,
              "car specs and availability");
        check(std::abs(items.at(0).legacyDailyRate - 450.0) < 0.001,
              "legacy money read unchanged");
        check(items.at(1).id == "M001" && items.at(1).type == "Motorcycle"
              && items.at(1).details == "471 CC" && !items.at(1).available,
              "motorcycle specs and rental state");

        const auto lookup = service.vehicleById("  c001  ");
        check(lookup.has_value() && lookup->id == "C001",
              "case-insensitive trimmed domain ID lookup");
        check(!service.vehicleById("M999").has_value(), "unknown ID has no value");
        check(!service.vehicleById("  ").has_value(), "empty ID has no value");
        check(readBytes(disposable.filePath("vehicles.txt")) == originalVehicles,
              "all read-only operations leave persistence unmodified");

        survivingSnapshot = items;
        survivingLookup = lookup;
        // A previously copied summary remains immutable and doesn't follow
        // subsequent domain state, which is the intentional P3-A snapshot API.
        check(domain.returnVehicle("M001"), "return fictional motorcycle");
        check(!survivingSnapshot.at(1).available, "snapshot not aliasing domain state");
        check(domain.removeVehicle("C001"), "remove fictional car");
        check(survivingSnapshot.at(0).id == "C001" &&
              survivingLookup->displayName() == "Toyota Corolla",
              "copy unaffected by domain deletion");
    }
    check(survivingSnapshot.size() == 2 &&
          survivingSnapshot.at(0).details == "4 doors" &&
          survivingLookup->id == "C001",
          "snapshot has independent lifetime beyond owning RentalSystem");

    // Missing/empty Fleet input must be a first-class result, not an exception.
    {
        QTemporaryDir emptyDir;
        check(emptyDir.isValid(), "empty temp directory available");
        RentalSystem emptyDomain(emptyDir.path());
        FleetReadService emptyService(emptyDomain);
        check(emptyService.vehicles().isEmpty(), "empty Fleet is an empty snapshot");
        check(!emptyService.vehicleById("C001"), "missing Fleet lookup");
    }
    std::cout << "P3 typed Fleet read snapshot and safety PASS\n";
    return EXIT_SUCCESS;
}
