#include "rentalsystem.h"
#include "src/application/rental_quote_service.h"
#include "src/application/rental_confirm_service.h"
#include "src/application/rental_read_service.h"
#include "src/application/rental_return_controller.h"
#include "car.h"
#include "rental.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <iostream>
#include <cstdlib>
#include <memory>
#include <utility>
#include <QHash>

namespace {
void check(bool pass,const char *message) {
    if (!pass) {
        std::cerr<<"P3 storage/clock injection: "<<message<<'\n';
        std::exit(1);
    }
}
class MemoryStorage final : public RentalStorage {
public:
    mutable int reads=0;
    int writes=0;
    bool failNext=false;
    QHash<QString,QByteArray> values;
    std::optional<QByteArray> read(const QString &path) const override {
        ++reads;
        auto it=values.constFind(QFileInfo(path).fileName());
        if (it == values.cend()) return std::nullopt;
        return *it;
    }
    bool writeAtomic(const QString &path,const QByteArray &bytes) override {
        ++writes;
        if (failNext) { failNext=false; return false; }
        values.insert(QFileInfo(path).fileName(),bytes);
        return true;
    }
};
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir temp;
    check(temp.isValid(),"temporary dir");
    auto storage=std::make_shared<MemoryStorage>();
    const QDate fakeToday(2023,4,14);
    RentalSystem system(temp.path(),storage,[fakeToday]{return fakeToday;});
    check(system.today()==fakeToday && storage->reads==2,
          "injected clock and both data files read during startup");
    check(system.addVehicle(new Car("C001","Fictional","Corolla",450,4)),
          "injected vehicle store");
    check(storage->writes==1 && !storage->values["vehicles.txt"].isEmpty() &&
          !QFile::exists(temp.filePath("vehicles.txt")),
          "no filesystem writes with injected in-memory store");
    RentalQuoteService quotes(system);
    const auto quote=quotes.preview("C001",3);
    check(quote.ok() && quote.quote->startDate==fakeToday &&
          quote.quote->expectedReturnDate==fakeToday.addDays(3) &&
          quote.quote->legacyTotal==1350,
          "default quote resolves injected domain date and C++ price");
    RentalConfirmService confirm(system);
    const auto success=confirm.confirm({*quote.quote,"Fictional C","0000000000"});
    check(success.ok() && success.receipt->rentalId=="R001" &&
          system.findVehicleById("C001")->getIsRented() &&
          storage->values["rentals.txt"].contains("Rental|R001") &&
          !QFile::exists(temp.filePath("rentals.txt")),
          "confirmed historical date booking uses injected clock/store");
    auto oldLedger=storage->values["rentals.txt"];
    const auto rejected=confirm.confirm({*quote.quote,"Fictional C","0000000000"});
    check(!rejected.ok() && storage->values["rentals.txt"]==oldLedger,
          "already-rented vehicle never triggers write");
    RentalReadService reads(system);
    const auto row=reads.recordById("R001");
    check(row && row->startDate==fakeToday &&
          row->dueState==RentalDueState::NotDue,
          "read service shares injected calendar default");
    RentalReturnService returns(system,reads);
    storage->failNext=true;
    const auto badReturn=returns.returnById("R001");
    check(badReturn.error==RentalReturnError::CommitFailed &&
          storage->values["rentals.txt"]==oldLedger &&
          system.findVehicleById("C001")->getIsRented() &&
          !system.findRentalById("R001")->getIsReturned(),
          "injected failed save preserves stored ledger and domain occupancy");
    const auto returned=returns.returnById("R001");
    check(returned.ok() && !system.findVehicleById("C001")->getIsRented() &&
          system.findRentalById("R001")->getIsReturned(),
          "injected clock drives successful return");
    storage->failNext=true;
    const auto beforeArchive=storage->values["rentals.txt"];
    check(!system.archiveRental("R001") &&
          !system.findRentalById("R001")->getIsArchived() &&
          storage->values["rentals.txt"]==beforeArchive,
          "archive rolls back when injected commit fails");
    check(system.archiveRental("R001"),"injected archive successful");
    const QByteArray archived=storage->values["rentals.txt"];
    check(!system.deleteArchivedRental("R000"),"missing archived record rejected");
    storage->failNext=true;
    check(!system.deleteArchivedRental("R001") &&
          storage->values["rentals.txt"]==archived &&
          system.findRentalById("R001") &&
          system.findRentalById("R001")->getIsArchived(),
          "delete rollback preserves archived history on injected failure");

    // Verify reloading through the interface, including unchanged legacy
    // text wire representation and historical availability reconstruction.
    RentalSystem reopened(temp.path(),storage,[fakeToday]{return fakeToday;});
    check(reopened.getRentals().size()==1 &&
          reopened.findRentalById("R001")->getIsArchived() &&
          !reopened.findVehicleById("C001")->getIsRented(),
          "memory repository loads persisted history without disk I/O");
    check(!QFile::exists(temp.filePath("vehicles.txt")) &&
          !QFile::exists(temp.filePath("rentals.txt")),
          "no accidental real-file fallback");
    check(reopened.deleteArchivedRental("R001") &&
          reopened.getRentals().isEmpty(),
          "injected repository supports authorized domain delete");
    check(!storage->values["vehicles.txt"].isEmpty(),
          "fleet file format remains independent");
    std::cout<<"P3 injectable domain clock + storage rollback/reopen PASS\n";
    return 0;
}
