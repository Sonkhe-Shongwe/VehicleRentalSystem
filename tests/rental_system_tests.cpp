#include "car.h"
#include "motorcycle.h"
#include "rental.h"
#include "rentalsystem.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void testFullLifecycle()
{
    QTemporaryDir temp;
    check(temp.isValid(), "temporary storage available");
    {
        RentalSystem system(temp.path());
        auto* car = new Car("C001", "Test", "Hatchback", 125.00, 4);
        check(system.addVehicle(car), "add vehicle persists");
        check(system.findVehicleById(" c001 ") == car, "lookup case-insensitive");
        check(!system.addVehicle(nullptr), "null vehicle rejected");
        auto* duplicate = new Car("c001", "Duplicate", "Car", 125.00, 4);
        check(!system.addVehicle(duplicate), "duplicate ID rejected");
        delete duplicate;
        auto* unsafe = new Motorcycle("M002", "Injection|text", "Bike", 99.00, 125);
        check(!system.addVehicle(unsafe), "delimited record injection rejected");
        delete unsafe;

        const QDate today = QDate::currentDate();
        auto* rental = new Rental("R001", "Test Customer", "0000000000", "C001",
                                  today, today.addDays(2), 2, 125.00, 250.00);
        check(system.createRental(rental), "rental created");
        check(car->getIsRented(), "vehicle rented in memory");
        check(!system.removeVehicle("C001"), "active rented vehicle cannot be removed");
        check(!system.archiveRental("R001"), "active rental cannot be archived");
        check(!system.deleteArchivedRental("R001"), "active rental cannot be deleted");
        auto* doubleBooking = new Rental("R002", "Other Customer", "0000000000", "C001",
                                         today, today.addDays(1), 1, 125.00, 125.00);
        check(!system.createRental(doubleBooking), "double booking rejected");
        delete doubleBooking;
        check(!system.completeRental("R001", today.addDays(1)), "future return rejected");
    }
    {
        RentalSystem reloaded(temp.path());
        check(reloaded.getVehicles().size() == 1, "vehicle survives restart");
        check(reloaded.getActiveRentals().size() == 1, "active rental survives restart");
        check(reloaded.findVehicleById("C001")->getIsRented(), "occupancy reconstructed");
        check(reloaded.completeRental("C001", QDate::currentDate()), "return by vehicle ID");
        check(!reloaded.completeRental("R001", QDate::currentDate()), "second return rejected");
        check(!reloaded.deleteArchivedRental("R001"), "unarchived returned rental cannot be deleted");
        check(reloaded.getActiveRentals().isEmpty(), "return clears active list");
        check(reloaded.hasRentalHistoryForVehicle(" c001 "), "returned rental still references canonical vehicle");
        check(!reloaded.removeVehicle("C001"), "returned vehicle remains protected by historical rental");
        check(reloaded.archiveRental("R001"), "returned rental archived");
        check(!reloaded.archiveRental("R001"), "duplicate archive rejected");
    }
    {
        RentalSystem reloaded(temp.path());
        check(reloaded.findRentalById("R001") != nullptr, "archive persists");
        check(reloaded.findRentalById("R001")->getIsArchived(), "archived state persists");
        check(reloaded.hasRentalHistoryForVehicle("c001"), "archived reference persists through restart");
        check(!reloaded.removeVehicle("C001"), "archived rental prevents orphaned vehicle reference");
        check(reloaded.deleteArchivedRental("R001"), "archived rental deletable");
        check(!reloaded.hasRentalHistoryForVehicle("C001"), "removed historical record releases deletion guard");
        check(reloaded.removeVehicle("C001"), "vehicle removable after archived references explicitly deleted");
        check(reloaded.findRentalById("R001") == nullptr, "deleted record removed from memory");
    }
    {
        RentalSystem reloaded(temp.path());
        check(reloaded.getVehicles().isEmpty(), "vehicle deletion persists after restart");
        check(reloaded.getRentals().isEmpty(), "archived rental deletion persists after restart");
    }
}

void testInvalidDataAndWriteFailure()
{
    QTemporaryDir temp;
    check(temp.isValid(), "isolated storage available");
    RentalSystem system(temp.path());
    auto* car = new Car("C001", "Test", "Car", 125.00, 4);
    check(system.addVehicle(car), "vehicle created");
    const QDate today = QDate::currentDate();
    auto* wrongPrice = new Rental("R001", "Customer", "0000000000", "C001",
                                  today, today.addDays(2), 2, 125.00, 1.00);
    check(!system.createRental(wrongPrice), "tampered total rejected in domain");
    delete wrongPrice;
    auto* injected = new Rental("R001", "Customer\nRental|R999", "0000000000", "C001",
                                today, today.addDays(2), 2, 125.00, 250.00);
    check(!system.createRental(injected), "newline injection rejected");
    delete injected;
    auto* invalidPrice = new Car("C002", "Test", "Car", std::nan(""), 4);
    check(!system.addVehicle(invalidPrice), "nonfinite price rejected");
    delete invalidPrice;

    // A missing parent directory prevents an atomic save, with no in-memory mutation.
    const QString missingPath = temp.path() + "/does_not_exist";
    RentalSystem badStorage(missingPath);
    auto* unsaved = new Car("C900", "Test", "Car", 10.00, 4);
    check(!badStorage.addVehicle(unsaved), "failure of persistence reported");
    check(badStorage.getVehicles().isEmpty(), "failed write does not mutate fleet");
    delete unsaved;
}

void testReadDoesNotRewrite()
{
    QTemporaryDir temp;
    check(temp.isValid(), "temporary storage available");
    const QString filePath = temp.filePath("vehicles.txt");
    QFile file(filePath);
    // Write the exact fixture bytes. QIODevice::Text translates LF to CRLF
    // on Windows, which would make the later raw-byte comparison fail before
    // the application even loads the file.
    check(file.open(QIODevice::WriteOnly), "fixture writable");
    const QByteArray original("Car|C001|Demo|Car|125.00|0|4\n");
    check(file.write(original) == original.size(), "fixture saved");
    file.close();
    check(file.open(QIODevice::ReadOnly), "fixture readable before startup");
    check(file.readAll() == original, "exact fixture bytes before startup");
    file.close();
    {
        RentalSystem loaded(temp.path());
        check(loaded.getVehicles().size() == 1, "fixture loaded");
    }
    check(file.open(QIODevice::ReadOnly), "fixture readable after startup");
    check(file.readAll() == original, "opening application never rewrites source file");
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    testFullLifecycle();
    testInvalidDataAndWriteFailure();
    testReadDoesNotRewrite();
    std::cout << "All rental domain tests passed.\n";
    return EXIT_SUCCESS;
}
