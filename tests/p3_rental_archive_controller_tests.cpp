#include "src/application/rental_archive_controller.h"
#include "src/application/rental_list_model.h"
#include "src/application/rental_read_service.h"
#include "rentalsystem.h"
#include "car.h"
#include "rental.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <cstdlib>
#include <iostream>
#include <memory>
namespace {
void check(bool x, const char *why) {
    if (!x) { std::cerr<<"P3 archive FAIL: "<<why<<'\n'; std::exit(1); }
}
QByteArray read(const QString &path) {
    QFile f(path);check(f.open(QIODevice::ReadOnly),"read fixture");return f.readAll();
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir tmp;check(tmp.isValid(),"temporary fixtures");
    RentalSystem system(tmp.path());
    check(system.addVehicle(new Car("C001","Fictional","Car",450,4)),"add car");
    const QDate today=QDate::currentDate();
    auto rental=std::make_unique<Rental>("R001","Fictional Person","0000000000",
                                        "C001",today,today.addDays(2),2,450,900);
    check(system.createRental(rental.get()),"persist rental");
    rental.release();
    const QString fleet=tmp.filePath("vehicles.txt");
    const QString ledger=tmp.filePath("rentals.txt");
    const QByteArray sourceFleet=read(fleet);
    RentalReadService reads(system);
    RentalListModel model(reads);
    int reset=0,passed=0,rejected=0;
    QObject::connect(&model,&QAbstractItemModel::modelReset,[&]{++reset;});
    RentalArchiveController action(system,reads,model);
    QObject::connect(&action,&RentalArchiveController::archiveSucceeded,
                     [&](const QString &id){++passed;check(id=="R001","receipt ID");});
    QObject::connect(&action,&RentalArchiveController::archiveRejected,
                     [&](RentalArchiveError){++rejected;});

    check(!action.archiveSelected() && rejected==1 && reset==0,
          "empty selection rejected without refresh");
    check(model.selectById("R001") && !action.canArchiveSelected() &&
          !action.archiveSelected() && rejected==2,"active cannot archive");
    check(system.completeRental("R001",today),"committed returned rental");
    model.refresh();
    check(action.canArchiveSelected() && model.selectedRentalId()=="R001",
          "returned record eligible with stable selection");
    const QByteArray returnedLedger=read(ledger);
    // UX-S11: a retained selected returned rental is NOT eligible when the
    // authoritative read provider fails, even though the domain is healthy.
    {
        bool refuse=false;
        RentalListModel stale([&]() -> std::optional<QList<RentalRecordSummary>> {
            if(refuse)return std::nullopt;
            return reads.records();
        });
        int blocked=0,stateChanges=0;
        RentalArchiveController guard(system,reads,stale);
        QObject::connect(&guard,&RentalArchiveController::archiveRejected,
                         [&](RentalArchiveError){++blocked;});
        QObject::connect(&guard,&RentalArchiveController::stateChanged,
                         [&]{++stateChanges;});
        check(stale.selectById("R001") && guard.canArchiveSelected(),
              "verified returned contract may be archived");
        refuse=true;
        stale.refresh();
        check(!stale.snapshotAvailable() && !guard.canArchiveSelected() &&
              stale.selectedRentalId()=="R001" &&
              !guard.archiveSelectedFor("R001") &&
              guard.lastArchiveMessage().contains("data unavailable") &&
              blocked==1 && stateChanges>=2 &&
              !system.findRentalById("R001")->getIsArchived() &&
              read(ledger)==returnedLedger && read(fleet)==sourceFleet,
              "failed snapshot denies stale archive without writing");
        refuse=false;
        stale.refresh();
        check(stale.snapshotAvailable() && guard.canArchiveSelected() &&
              read(ledger)==returnedLedger,
              "successful provider retry restores eligibility without archiving");
    }
    // Protect initial ledger by using a directory as the QSaveFile target.
    check(QFile::rename(ledger,tmp.filePath("rentals-backup.txt")),"backup ledger");
    check(QDir().mkdir(ledger),"block ledger writer");
    const int before=reset;
    check(!action.archiveSelected() && rejected==3 && passed==0 &&
          reset==before && action.canArchiveSelected() &&
          !system.findRentalById("R001")->getIsArchived(),
          "persistence failure rolls back without false refresh/success");
    check(QDir().rmdir(ledger) &&
          QFile::rename(tmp.filePath("rentals-backup.txt"),ledger) &&
          read(ledger)==returnedLedger,"restore file bytes");
    model.setStatusFilter("History");
    check(model.selectedRentalId()=="R001" && model.count()==1,
          "History selected returned record");
    check(action.archiveSelected() && passed==1 &&
          !action.canArchiveSelected() &&
          model.selectedRentalId()=="R001" &&
          model.data(model.index(0,0),RentalListModel::StatusRole)=="Archived" &&
          read(ledger)!=returnedLedger,
          "successful archive persisted and refreshed history");
    check(!action.archiveSelected() && rejected==4,
          "repeat archive blocked without another commit");
    model.setStatusFilter("Active");
    check(model.count()==0 && model.selectedRentalId().isEmpty(),
          "Archived never exposed as Active");
    const auto after=read(ledger);
    model.setStatusFilter("All");
    model.refresh();
    check(read(ledger)==after && read(fleet)==sourceFleet,
          "read models do not write original fleet or archive ledger");
    RentalSystem reopened(tmp.path());
    check(reopened.findRentalById("R001")->getIsArchived() &&
          reopened.findRentalById("R001")->getIsReturned() &&
          !reopened.findVehicleById("C001")->getIsRented(),
          "archived record persists across restart and occupancy remains free");
    // UX-S6: a separate fictional Returned-only session must not lose its
    // newly archived receipt. The write is still exclusively domain-owned.
    {
        QTemporaryDir other;
        check(other.isValid(),"separate archive receipt fixture");
        RentalSystem different(other.path());
        check(different.addVehicle(new Car("C200","Fictional","Car",300,4)) &&
              different.addVehicle(new Car("C201","Fictional","Car",300,4)),
              "separate archive vehicles");
        auto entry=std::make_unique<Rental>("R200","Fictional Person","0000000000",
                                            "C200",today,today.addDays(2),2,300,600);
        check(different.createRental(entry.get()),"separate archive booking");
        entry.release();
        check(different.completeRental("R200",today),"separate returned receipt");
        auto otherEntry=std::make_unique<Rental>("R201","Fictional Person","0000000000",
                                                 "C201",today,today.addDays(2),2,300,600);
        check(different.createRental(otherEntry.get()),"second archive booking");
        otherEntry.release();
        check(different.completeRental("R201",today),"second eligible archive record");
        const QByteArray unchangedFleet=read(other.filePath("vehicles.txt"));
        const QByteArray guardedLedger=read(other.filePath("rentals.txt"));
        RentalReadService differentReads(different);
        RentalListModel differentModel(differentReads);
        RentalArchiveController differentArchive(different,differentReads,differentModel);
        differentModel.setStatusFilter("Returned");
        check(differentModel.count()==2 &&
              differentModel.selectById("R200") &&
              differentArchive.canArchiveSelected(),
              "two returned-only records available for review");
        int guardRejections=0;
        QObject::connect(&differentArchive,&RentalArchiveController::archiveRejected,
                         [&](RentalArchiveError){++guardRejections;});
        check(!differentArchive.archiveSelectedFor("R201") &&
              guardRejections==1 &&
              differentArchive.lastArchiveMessage().contains("Selection changed") &&
              differentModel.selectedRentalId()=="R200" &&
              differentArchive.canArchiveSelected() &&
              !different.findRentalById("R200")->getIsArchived() &&
              !different.findRentalById("R201")->getIsArchived() &&
              read(other.filePath("rentals.txt"))==guardedLedger &&
              read(other.filePath("vehicles.txt"))==unchangedFleet,
              "stale archive confirmation cannot act on another eligible returned ID");
        check(differentArchive.archiveSelectedFor("R200") &&
              differentModel.statusFilter()=="All" &&
              differentModel.count()==2 &&
              differentModel.selectedRentalId()=="R200" &&
              differentModel.selectedContract().value("status").toString()=="Archived" &&
              !differentArchive.canArchiveSelected() &&
              read(other.filePath("vehicles.txt"))==unchangedFleet,
              "archive reveals saved record from Returned filter without fleet changes");
        differentModel.setStatusFilter("History");
        check(differentModel.count()==2 &&
              differentModel.selectedRentalId()=="R200" &&
              differentModel.selectedContract().value("status").toString()=="Archived" &&
              !different.findRentalById("R201")->getIsArchived(),
              "archived receipt remains selected in History; other returned rental unchanged");
    }
    std::cout<<"P3 archive C++ commit, rollback, history and restart PASS\n";
    return 0;
}
