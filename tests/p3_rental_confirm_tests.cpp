#include "src/application/rental_confirm_service.h"
#include "src/application/rental_quote_service.h"
#include "rentalsystem.h"
#include "car.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool ok, const char *message) {
    if (!ok) { std::cerr << "P3 confirm FAIL: " << message << '\n'; std::exit(1); }
}
QByteArray bytes(const QString &path) {
    QFile f(path);
    require(f.open(QIODevice::ReadOnly), "fixture file readable");
    return f.readAll();
}
RentalConfirmationRequest requestFor(const RentalQuotePreview &quote) {
    return {quote, "Fictional Customer", "0000000000"};
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    {
        QTemporaryDir temp;
        require(temp.isValid(), "disposable directory");
        RentalSystem system(temp.path());
        require(system.addVehicle(new Car("C001", "Toyota", "Corolla", 450, 4)),
                "fictional Corolla added");
        require(system.addVehicle(new Car("C002", "Ford", "Ranger", 700, 4)),
                "fictional Ranger added");
        const QByteArray before = bytes(temp.filePath("vehicles.txt"));
        const RentalQuoteService quoteService(system);
        RentalConfirmService confirmService(system);
        const auto quote = quoteService.preview("C001", 3);
        require(quote.ok(), "valid quote");

        auto badCustomer = requestFor(*quote.quote);
        badCustomer.customerName = "  ";
        require(confirmService.confirm(badCustomer).error ==
                RentalConfirmationError::InvalidCustomerName, "empty name rejected");
        badCustomer.customerName = "bad|customer";
        require(confirmService.confirm(badCustomer).error ==
                RentalConfirmationError::InvalidCustomerName, "delimiter injection rejected");
        auto badPhone = requestFor(*quote.quote);
        badPhone.phoneNumber = "000\n999";
        require(confirmService.confirm(badPhone).error ==
                RentalConfirmationError::InvalidPhoneNumber, "newline phone rejected");
        auto invalid = requestFor(*quote.quote);
        invalid.reviewedQuote.days = 0;
        require(confirmService.confirm(invalid).error ==
                RentalConfirmationError::InvalidQuote, "invalid quote rejected");
        auto stale = requestFor(*quote.quote);
        stale.reviewedQuote.legacyTotal += 100;
        require(confirmService.confirm(stale).error ==
                RentalConfirmationError::QuoteStale, "tampered total rejected");
        stale = requestFor(*quote.quote);
        stale.reviewedQuote.startDate = stale.reviewedQuote.startDate.addDays(-1);
        require(confirmService.confirm(stale).error ==
                RentalConfirmationError::QuoteStale, "stale review date rejected");
        stale = requestFor(*quote.quote);
        stale.reviewedQuote.legacyDailyRate = 1;
        require(confirmService.confirm(stale).error ==
                RentalConfirmationError::QuoteStale, "outdated rate rejected");
        require(system.getActiveRentals().isEmpty() &&
                bytes(temp.filePath("vehicles.txt")) == before &&
                !QFile::exists(temp.filePath("rentals.txt")),
                "all preflight failures are strictly non-mutating");

        // The successful path is an actual legacy QSaveFile commit, not a
        // mocked or optimistic 'rental confirmed' indicator.
        const auto result = confirmService.confirm(requestFor(*quote.quote));
        require(result.ok() && result.receipt->rentalId == "R001" &&
                result.receipt->vehicleId == "C001" &&
                result.receipt->days == 3 &&
                std::abs(result.receipt->legacyTotal - 1350) < 0.001,
                "typed persisted receipt");
        require(QFile::exists(temp.filePath("rentals.txt")) &&
                system.getActiveRentals().size() == 1 &&
                system.findVehicleById("C001")->getIsRented(),
                "domain persisted occupancy and rental list");
        require(confirmService.confirm(requestFor(*quote.quote)).error ==
                RentalConfirmationError::VehicleUnavailable,
                "confirmed rental cannot be booked twice");
        require(bytes(temp.filePath("vehicles.txt")) == before,
                "booking writes authoritative rental ledger, not fleet text file");

        // A second new service instance sees the same domain business truth.
        const auto second = quoteService.preview("C002", 1);
        require(second.ok(), "second vehicle can be quoted independently");
    }
    {
        QTemporaryDir temp;
        require(temp.isValid(), "restart persistence temp");
        {
            RentalSystem first(temp.path());
            require(first.addVehicle(new Car("C001", "Toyota", "Corolla", 450, 4)),
                    "seed restart");
            const auto q = RentalQuoteService(first).preview("C001", 2);
            require(q.ok(), "preview for restart");
            require(RentalConfirmService(first).confirm(requestFor(*q.quote)).ok(),
                    "saved rental accepted");
        }
        RentalSystem second(temp.path());
        require(second.findRentalById("R001") != nullptr &&
                second.getActiveRentals().size() == 1 &&
                second.findVehicleById("C001")->getIsRented(),
                "record and occupancy survive restart");
    }
    {
        QTemporaryDir temp;
        require(temp.isValid(), "storage failure sandbox");
        RentalSystem system(temp.path());
        require(system.addVehicle(new Car("C001", "Toyota", "Corolla", 450, 4)),
                "create file-backed vehicle");
        const QByteArray originalVehicles = bytes(temp.filePath("vehicles.txt"));
        const auto q = RentalQuoteService(system).preview("C001", 1);
        require(q.ok(), "quote before storage failure");
        // Block the expected QSaveFile target by creating a directory under
        // the rental ledger filename. No permissions or real user data touched.
        const QString blockedLedger = temp.filePath("rentals.txt");
        require(QDir().mkdir(blockedLedger), "block save target with directory");
        RentalConfirmService service(system);
        const auto failed = service.confirm(requestFor(*q.quote));
        require(failed.error == RentalConfirmationError::CommitFailed &&
                !failed.receipt.has_value(), "write failure cannot report success");
        require(system.getRentals().isEmpty() &&
                !system.findVehicleById("C001")->getIsRented() &&
                bytes(temp.filePath("vehicles.txt")) == originalVehicles,
                "write failure rolls back rental/occupancy without fleet rewrite");
        require(QDir().rmdir(blockedLedger), "remove blocked fixture directory");
        const auto recovered = service.confirm(requestFor(*q.quote));
        require(recovered.ok() && system.getActiveRentals().size() == 1 &&
                QFile::exists(blockedLedger),
                "retry after fixing storage safely persists once");
    }
    {
        // Domain state may change between Preview and Confirm. Always reject
        // stale customer-visible quotes rather than committing an old rate,
        // old vehicle description, or a vehicle that has since been removed.
        QTemporaryDir temp;
        require(temp.isValid(), "stale-domain test sandbox");
        RentalSystem system(temp.path());
        require(system.addVehicle(new Car("C001", "Toyota", "Corolla", 450, 4)),
                "seed mutable fictional vehicle");
        RentalQuoteService quotes(system);
        RentalConfirmService confirmations(system);
        const QString fleetPath = temp.filePath("vehicles.txt");
        const QString ledgerPath = temp.filePath("rentals.txt");
        const auto originalQuote = quotes.preview("C001", 3);
        require(originalQuote.ok(), "initial customer-visible quote");

        Vehicle* car = system.findVehicleById("C001");
        require(car != nullptr, "lookup of disposable vehicle");
        car->setPricePerDay(500.0);
        require(system.saveVehiclesToFile(), "persist changed rate in sandbox");
        const QByteArray updatedRateBytes = bytes(fleetPath);
        const auto changedRate = confirmations.confirm(requestFor(*originalQuote.quote));
        require(changedRate.error == RentalConfirmationError::QuoteStale &&
                !changedRate.receipt.has_value(),
                "domain rate changed since preview: reject stale quote");
        require(bytes(fleetPath) == updatedRateBytes &&
                !QFile::exists(ledgerPath) && system.getRentals().isEmpty() &&
                !car->getIsRented(),
                "stale rate rejection must not write or reserve a vehicle");

        const auto rateQuote = quotes.preview("C001", 3);
        require(rateQuote.ok() && rateQuote.quote->legacyTotal == 1500.0,
                "fresh preview reflects changed domain rate");
        car->setModel("Corolla Updated");
        require(system.saveVehiclesToFile(), "persist changed model in sandbox");
        const QByteArray updatedModelBytes = bytes(fleetPath);
        const auto changedModel = confirmations.confirm(requestFor(*rateQuote.quote));
        require(changedModel.error == RentalConfirmationError::QuoteStale &&
                !changedModel.receipt.has_value(),
                "domain vehicle details changed since preview: reject stale quote");
        require(bytes(fleetPath) == updatedModelBytes &&
                !QFile::exists(ledgerPath) && system.getRentals().isEmpty(),
                "stale model rejection remains strictly non-mutating");

        const auto finalQuote = quotes.preview("C001", 3);
        require(finalQuote.ok(), "preview before vehicle removal");
        require(system.removeVehicle("C001"), "remove disposable available vehicle");
        const QByteArray afterRemovalBytes = bytes(fleetPath);
        const auto removed = confirmations.confirm(requestFor(*finalQuote.quote));
        require(removed.error == RentalConfirmationError::VehicleNotFound &&
                !removed.receipt.has_value(),
                "vehicle removed since quote: reject confirmation without receipt");
        require(bytes(fleetPath) == afterRemovalBytes &&
                !QFile::exists(ledgerPath) && system.getRentals().isEmpty(),
                "removed-vehicle rejection cannot recreate a rental");
    }
    std::cout << "P3 typed rental confirmation and rollback PASS\n";
    return 0;
}
