#include "src/application/rental_admin_commands.h"
#include "src/application/rental_quote_service.h"
#include "src/application/rental_confirm_service.h"
#include "rentalsystem.h"
#include "car.h"
#include "rental.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <cmath>
#include <iostream>
#include <cstdlib>
namespace {
void require(bool ok,const char *reason) {
    if (!ok){std::cerr<<"P3 typed commands FAIL: "<<reason<<'\n';std::exit(1);}
}
QByteArray bytes(const QString &path) {
    QFile f(path);require(f.open(QIODevice::ReadOnly),"file read");return f.readAll();
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir temp;
    require(temp.isValid(),"disposable storage");
    RentalSystem system(temp.path());
    FleetWriteService fleet(system);
    ArchivedDeleteService purge(system);
    FleetAddRequest car{FleetVehicleKind::Car,"","Fictional","Corolla",450,4};
    FleetAddRequest bike{FleetVehicleKind::Motorcycle,"","Fictional","Scooter",200,125};
    require(fleet.add(FleetAddRequest{FleetVehicleKind::Car,"","", "Car",450,4}).error==
            FleetWriteError::InvalidDetails, "invalid input typed result");
    require(fleet.add(car).vehicleId=="C001","add generated car via domain");
    require(fleet.add(bike).vehicleId=="M001","add generated motorcycle via domain");
    require(system.findVehicleById("C001") && system.findVehicleById("M001"),
            "both kinds actually saved");
    car.id="c001";
    require(fleet.add(car).error==FleetWriteError::DuplicateId,
            "case-insensitive ID duplicate typed");
    const QString fleetFile=temp.filePath("vehicles.txt");
    const QString ledgerFile=temp.filePath("rentals.txt");
    const QByteArray before=bytes(fleetFile);
    car.id="C002";
    require(QFile::rename(fleetFile,temp.filePath("fleet-backup")),"backup fleet");
    require(QDir().mkdir(fleetFile),"block fleet commit target");
    require(fleet.add(car).error==FleetWriteError::CommitRejected &&
            !system.findVehicleById("C002"),"failed add rolls back domain");
    require(QDir().rmdir(fleetFile) &&
            QFile::rename(temp.filePath("fleet-backup"),fleetFile) &&
            bytes(fleetFile)==before,"restore fleet bytes after rejected write");

    RentalQuoteService quotes(system);
    RentalConfirmService confirm(system);
    auto preview=quotes.preview("C001",2);
    require(preview.ok(),"quote before rental");
    auto receipt=confirm.confirm({*preview.quote,"Fictional Customer","0000000000"});
    require(receipt.ok() && receipt.receipt->rentalId=="R001",
            "persistent fictional rental");
    require(fleet.remove("C001").error==FleetWriteError::VehicleInUse &&
            fleet.remove("unknown").error==FleetWriteError::VehicleNotFound,
            "fleet remove typed guard");
    require(purge.permanentlyDelete("R001","DELETE R001").error==
            ArchivedDeleteError::NotArchived,"active deletion forbidden");
    require(system.completeRental("R001",QDate::currentDate()),
            "commit returned rental");
    const QByteArray fleetBeforeHistory=bytes(fleetFile);
    const QByteArray returnedLedger=bytes(ledgerFile);
    require(system.hasRentalHistoryForVehicle(" c001 ") &&
            fleet.remove("C001").error==FleetWriteError::VehicleReferencedByRental &&
            !system.removeVehicle("C001") &&
            system.findVehicleById("C001") &&
            bytes(fleetFile)==fleetBeforeHistory &&
            bytes(ledgerFile)==returnedLedger,
            "returned reference blocks service and domain removal without writing");
    require(purge.permanentlyDelete("R001","DELETE R001").error==
            ArchivedDeleteError::NotArchived,"returned but unarchived guard");
    require(system.archiveRental("R001"),"archive before deletion");
    const QByteArray archived=bytes(ledgerFile);
    require(fleet.remove("c001").error==FleetWriteError::VehicleReferencedByRental &&
            !system.removeVehicle(" C001 ") &&
            system.hasRentalHistoryForVehicle("C001") &&
            bytes(fleetFile)==fleetBeforeHistory &&
            bytes(ledgerFile)==archived,
            "archived reference remains protected including normalized ID and disk bytes");
    require(purge.permanentlyDelete("R001","").error==
            ArchivedDeleteError::ExplicitConfirmationRequired &&
            purge.permanentlyDelete("R001","delete R001").error==
            ArchivedDeleteError::ExplicitConfirmationRequired &&
            purge.permanentlyDelete("R001","DELETE r001").error==
            ArchivedDeleteError::ExplicitConfirmationRequired &&
            bytes(ledgerFile)==archived,
            "explicit exact ID deletion guard leaves file unchanged");
    require(QFile::rename(ledgerFile,temp.filePath("ledger-backup")),
            "backup ledger");
    require(QDir().mkdir(ledgerFile),"force delete storage failure");
    require(purge.permanentlyDelete("R001","DELETE R001").error==
            ArchivedDeleteError::CommitRejected &&
            system.findRentalById("R001") &&
            system.findRentalById("R001")->getIsArchived(),
            "failed archive delete keeps in-memory record");
    require(QDir().rmdir(ledgerFile) &&
            QFile::rename(temp.filePath("ledger-backup"),ledgerFile) &&
            bytes(ledgerFile)==archived,"restore ledger after failed deletion");
    require(purge.permanentlyDelete("R001","DELETE R001").ok() &&
            !system.findRentalById("R001") &&
            !system.hasRentalHistoryForVehicle("C001"),
            "explicit confirmed deletion releases historical reference guard");
    require(purge.permanentlyDelete("R001","DELETE R001").error==
            ArchivedDeleteError::MissingRental,"repeat delete rejected");

    const QByteArray beforeRemove=bytes(fleetFile);
    require(QFile::rename(fleetFile,temp.filePath("fleet-backup")),"backup fleet remove");
    require(QDir().mkdir(fleetFile),"block remove target");
    require(fleet.remove("C001").error==FleetWriteError::CommitRejected &&
            system.findVehicleById("C001"),
            "failed fleet removal retains vehicle");
    require(QDir().rmdir(fleetFile) &&
            QFile::rename(temp.filePath("fleet-backup"),fleetFile) &&
            bytes(fleetFile)==beforeRemove,"fleet bytes protected on failure");
    require(fleet.remove("C001").ok() &&
            !system.findVehicleById("C001"),"returned vehicle removable");
    RentalSystem reopened(temp.path());
    require(!reopened.findVehicleById("C001") &&
            reopened.findVehicleById("M001") &&
            reopened.getRentals().isEmpty(),
            "persisted typed command results survive restart");
    std::cout<<"P3 typed fleet/admin commands, failpoints and explicit deletion guard PASS\n";
    return 0;
}
