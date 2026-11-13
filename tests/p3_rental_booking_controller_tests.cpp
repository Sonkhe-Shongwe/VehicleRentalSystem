#include "src/application/rental_booking_controller.h"
#include "src/application/rental_list_model.h"
#include "src/application/rental_read_service.h"
#include "rentalsystem.h"
#include "car.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <cstdlib>
#include <iostream>
namespace {
void check(bool x,const char *m) {
    if(!x) {std::cerr<<"P3 closure booking test failed: "<<m<<'\n';std::exit(1);}
}
QByteArray bytes(const QString &path) {
    QFile f(path);check(f.open(QIODevice::ReadOnly),"file read");return f.readAll();
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir temp;check(temp.isValid(),"temporary");
    RentalSystem system(temp.path());
    check(system.addVehicle(new Car("C001","Toyota","Corolla",450,4)),"C001");
    check(system.addVehicle(new Car("C002","Ford","Ranger",700,4)),"C002");
    RentalReadService reads(system);
    RentalListModel rentals(reads);
    int fleetRefresh=0,ok=0,failed=0,resets=0;
    RentalBookingController booking(system,rentals,[&]{++fleetRefresh;});
    QObject::connect(&booking,&RentalBookingController::bookingSucceeded,
                     [&](const QString &id){++ok;check(!id.isEmpty(),"receipt ID");});
    QObject::connect(&booking,&RentalBookingController::bookingRejected,
                     [&](RentalConfirmationError){++failed;});
    QObject::connect(&rentals,&QAbstractItemModel::modelReset,[&]{++resets;});
    const QByteArray originalFleet=bytes(temp.filePath("vehicles.txt"));
    check(!booking.confirm("Demo Customer","0000000000") &&
          fleetRefresh==0 && failed==1,"cannot confirm before preview");
    check(!booking.preview("C001",0) && !booking.canConfirm(),
          "invalid duration never leaves confirmation available");
    check(!booking.preview("C404",2) && !booking.canConfirm(),"nonexistent vehicle");
    check(booking.preview("C001",3) && booking.canConfirm() &&
          booking.quoteSummary().contains("Total R 1350.00"),"C++ quote price and total");
    booking.discardPreview();
    check(!booking.canConfirm() && rentals.count()==0,"Cancel no write");
    check(booking.preview("C001",3),"re-preview");
    check(!booking.confirm(" ","0000000000") && booking.canConfirm() &&
          fleetRefresh==0 && resets==0 && failed==2,"invalid name nonmutating");
    check(!QFile::exists(temp.filePath("rentals.txt")),"no optimistic ledger");
    check(!booking.confirm("Demo Customer","|broken") && failed==3 &&
          fleetRefresh==0,"bad phone rejected");
    check(booking.confirm("Demo Customer","0000000000") &&
          booking.lastRentalId()=="R001" && rentals.count()==1 &&
          rentals.selectedRentalId()=="R001" && fleetRefresh==1 &&
          resets==1 && ok==1 && !booking.canConfirm(),"successful domain write and refresh");
    check(bytes(temp.filePath("vehicles.txt"))==originalFleet,"rental commit never rewrites fleet");
    check(system.findVehicleById("C001")->getIsRented(),"domain occupancy");
    check(!booking.preview("C001",1) && !booking.canConfirm(),
          "cannot quote newly unavailable vehicle");

    // A stale quote must not reserve the newly changed price.
    check(booking.preview("C002",2),"second quote");
    auto *car=system.findVehicleById("C002");
    car->setPricePerDay(730);check(system.saveVehiclesToFile(),"change price");
    const QByteArray updatedFleet=bytes(temp.filePath("vehicles.txt"));
    const QByteArray originalLedger=bytes(temp.filePath("rentals.txt"));
    check(!booking.confirm("Demo Customer","0000000000") &&
          !booking.canConfirm() && rentals.count()==1 &&
          fleetRefresh==1 && resets==1 && failed==4 &&
          bytes(temp.filePath("rentals.txt"))==originalLedger &&
          bytes(temp.filePath("vehicles.txt"))==updatedFleet,
          "stale quote fails without refresh or receipt");
    check(booking.preview("C002",2) &&
          booking.quoteSummary().contains("Total R 1460.00"),
          "re-preview uses new authoritative daily rate");

    // New active bookings cannot appear in a History-only view. A failed
    // commit must preserve that filter; a successful commit must reveal its
    // actual saved receipt without an additional manual lookup.
    rentals.setStatusFilter("History");
    check(rentals.statusFilter()=="History" && rentals.count()==0 &&
          rentals.selectedRentalId().isEmpty(),
          "History filter excludes existing active bookings before retry");

    // Force the authoritative QSaveFile commit failure and prove rollback.
    const QString ledger=temp.filePath("rentals.txt");
    check(QFile::rename(ledger,temp.filePath("rentals-backup")),"save ledger backup");
    check(QDir().mkdir(ledger),"block file target");
    check(!booking.confirm("Demo Customer","0000000000") &&
          booking.canConfirm() && fleetRefresh==1 &&
          !car->getIsRented() && system.getRentals().size()==1 &&
          rentals.count()==0 && rentals.statusFilter()=="History" &&
          rentals.selectedRentalId().isEmpty() && failed==5 &&
          booking.lastRentalId()=="R001","no false success on QSaveFile failure");
    check(QDir().rmdir(ledger) &&
          QFile::rename(temp.filePath("rentals-backup"),ledger) &&
          bytes(ledger)==originalLedger,"restore bytes");
    check(booking.confirm("Demo Customer","0000000000") &&
          booking.lastRentalId()=="R002" && fleetRefresh==2 &&
          rentals.count()==2 && rentals.statusFilter()=="All" &&
          rentals.selectedRentalId()=="R002" && rentals.selectedRow()==1 &&
          rentals.selectedContract().value("rentalId").toString()=="R002" &&
          ok==2 && !booking.canConfirm(),
          "successful retry reveals the committed booking from a hidden History filter");
    const QByteArray persisted=bytes(ledger);
    booking.discardPreview();
    rentals.refresh();
    check(bytes(ledger)==persisted && bytes(temp.filePath("vehicles.txt"))==updatedFleet,
          "read-only refresh never rewrites files");
    RentalSystem reopened(temp.path());
    check(reopened.getActiveRentals().size()==2 &&
          reopened.findVehicleById("C001")->getIsRented() &&
          reopened.findVehicleById("C002")->getIsRented(),"real persisted restart");
    std::cout<<"P3 full C++ booking guided quote/confirm/rollback PASS\n";
    return 0;
}
