#include "src/application/rental_list_model.h"
#include "src/application/rental_read_service.h"
#include "car.h"
#include "rental.h"
#include "rentalsystem.h"

#include <QCoreApplication>
#include <QDate>
#include <QFile>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
void check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "P3-H Rental status filter FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
QByteArray contents(const QString &path) {
    QFile f(path);
    check(f.open(QIODevice::ReadOnly), "disposable file readable");
    return f.readAll();
}
void book(RentalSystem &system, const QString &id, const QString &car) {
    const QDate today = QDate::currentDate();
    auto item = std::make_unique<Rental>(
        id, "Fictional Person", "0000000000", car,
        today, today.addDays(2), 2, 450, 900);
    check(system.createRental(item.get()), "persist fictional rental");
    item.release();
}
QString itemId(const RentalListModel &model, int row) {
    return model.data(model.index(row, 0), RentalListModel::RentalIdRole).toString();
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    check(temp.isValid(), "QTemporaryDir");
    const QDate today = QDate::currentDate();
    RentalSystem system(temp.path());
    for (int n=1; n<=4; ++n) {
        check(system.addVehicle(new Car(
            QString("C%1").arg(n,3,10,QChar('0')),
            "Fictional", "Car", 450, 4)), "fake vehicle");
    }
    book(system,"R001","C001");
    book(system,"R002","C002");
    book(system,"R003","C003");
    check(system.completeRental("R002", today), "returned fixture");
    check(system.completeRental("R003", today), "historical fixture");
    check(system.archiveRental("R003"), "archived fixture");

    const QByteArray fleetBefore=contents(temp.filePath("vehicles.txt"));
    const QByteArray ledgerBefore=contents(temp.filePath("rentals.txt"));
    RentalReadService service(system, [today]{return today;});
    RentalListModel model(service);
    int resets=0, counts=0, selections=0, filters=0;
    QObject::connect(&model,&QAbstractItemModel::modelReset,[&]{++resets;});
    QObject::connect(&model,&RentalListModel::countChanged,[&]{++counts;});
    QObject::connect(&model,&RentalListModel::selectionChanged,[&]{++selections;});
    QObject::connect(&model,&RentalListModel::statusFilterChanged,[&]{++filters;});

    check(model.statusFilter()=="All" && model.count()==3 &&
          itemId(model,0)=="R001" && itemId(model,1)=="R002" &&
          itemId(model,2)=="R003", "default All includes every state");
    // P6-G: explicit Returned/Archived filters are derived from the
    // existing domain status, never a second QML lifecycle implementation.
    RentalListModel detailed(service);
    const int stableActive = detailed.activeCount();
    const int stableHistory = detailed.historyCount();
    detailed.setStatusFilter("Returned");
    check(detailed.count()==1 && itemId(detailed,0)=="R002" &&
          detailed.data(detailed.index(0,0),RentalListModel::StatusRole)=="Returned",
          "Returned shows only returned, not archived rentals");
    check(detailed.selectVisibleRow(0) && detailed.selectedRentalId()=="R002",
          "Returned selected canonical rental ID");
    detailed.setStatusFilter("Archived");
    check(detailed.count()==1 && itemId(detailed,0)=="R003" &&
          detailed.data(detailed.index(0,0),RentalListModel::StatusRole)=="Archived" &&
          detailed.selectedRentalId().isEmpty(),
          "Archived shows only archived and clears hidden returned selection");
    check(detailed.locateById("  r002  ") &&
          detailed.statusFilter()=="All" &&
          detailed.selectedRentalId()=="R002",
          "cross-filter ID lookup reveals Returned from Archived without writing");
    detailed.setStatusFilter("Archived");
    check(detailed.locateById("r003") &&
          detailed.statusFilter()=="Archived" &&
          detailed.selectedRentalId()=="R003",
          "lookup preserves Archived when match is visible");
    detailed.setStatusFilter("History");
    check(detailed.count()==2 &&
          itemId(detailed,0)=="R002" && itemId(detailed,1)=="R003" &&
          detailed.selectedRentalId()=="R003" &&
          detailed.selectedRow()==1,
          "History retains both statuses and preserves visible archived ID");
    detailed.setStatusFilter("Active");
    check(detailed.count()==1 && itemId(detailed,0)=="R001" &&
          detailed.selectedRentalId().isEmpty() &&
          detailed.activeCount()==stableActive &&
          detailed.historyCount()==stableHistory,
          "filter changes clear hidden selection but never alter full-snapshot KPIs");
    const int selectedAfterActive=detailed.selectedRow();
    detailed.setStatusFilter("returned"); // invalid lower-case filter
    check(detailed.statusFilter()=="Active" &&
          detailed.selectedRow()==selectedAfterActive &&
          contents(temp.filePath("vehicles.txt"))==fleetBefore &&
          contents(temp.filePath("rentals.txt"))==ledgerBefore,
          "invalid filter and all read-only views leave exact files untouched");

    check(model.selectById("  r002  ") && model.selectedRow()==1,
          "select returned rental by canonical ID");
    model.setStatusFilter("History");
    check(model.statusFilter()=="History" && model.count()==2 &&
          itemId(model,0)=="R002" && itemId(model,1)=="R003" &&
          model.selectedRentalId()=="R002" && model.selectedRow()==0,
          "History combines Returned+Archived, preserves visible ID after row shift");
    check(model.data(model.index(0,0),RentalListModel::StatusRole)=="Returned" &&
          model.data(model.index(1,0),RentalListModel::StatusRole)=="Archived",
          "History decisions derive from C++ lifecycle flags");
    check(!model.selectById("R001") && model.selectedRentalId()=="R002",
          "cannot select filtered-out active record");
    check(!model.selectVisibleRow(-1) && !model.selectVisibleRow(2) &&
          model.selectVisibleRow(1) && model.selectedRentalId()=="R003",
          "selection uses only currently visible row IDs");
    const int beforeInvalid=resets;
    model.setStatusFilter("history");
    model.setStatusFilter("Unknown");
    model.setStatusFilter("History");
    check(model.statusFilter()=="History" && resets==beforeInvalid,
          "invalid and repeated filters are no-ops");

    model.setStatusFilter("Active");
    check(model.count()==1 && itemId(model,0)=="R001" &&
          model.selectedRentalId().isEmpty() && model.selectedRow()==-1,
          "hiding archived selection clears it rather than selecting first");
    model.setStatusFilter("All");
    check(model.count()==3 && model.selectedRentalId().isEmpty(),
          "returning All never silently reselects previous hidden record");

    check(model.selectVisibleRow(0), "select real active row");
    model.setStatusFilter("Active");
    check(model.selectedRentalId()=="R001" && model.selectedRow()==0,
          "visible active selection survives filter");
    check(system.completeRental("R001",today), "return selected active rental");
    check(model.count()==1 && model.selectedRentalId()=="R001",
          "model remains snapshot until explicit refresh");
    model.refresh();
    check(model.count()==0 && model.selectedRentalId().isEmpty(),
          "selected rental that becomes history is cleared on refresh");
    model.setStatusFilter("History");
    check(model.count()==3 && itemId(model,0)=="R001" &&
          itemId(model,1)=="R002" && itemId(model,2)=="R003",
          "returned rental appears in history after committed refresh");
    check(model.selectVisibleRow(1) && model.selectedRentalId()=="R002",
          "select returned record in history");
    check(system.archiveRental("R002"), "archive selected returned");
    model.refresh();
    check(model.selectedRentalId()=="R002" && model.selectedRow()==1 &&
          model.data(model.index(1,0),RentalListModel::StatusRole)=="Archived",
          "archived transition keeps same visible selection");
    check(system.deleteArchivedRental("R002"),"permanently delete archived selected");
    model.refresh();
    check(model.count()==2 && model.selectedRentalId().isEmpty(),
          "selected removed rental clears on refresh");
    check(resets>=8 && counts>=6 && selections>=5 && filters==5,
          "native Qt reset/count/selection/filter signals emitted as appropriate");

    // Returns/archive/delete persist in rentals.txt, not vehicles.txt.
    // Rental read filtering must not imply any fleet rewrite.
    check(contents(temp.filePath("vehicles.txt")) == fleetBefore &&
          contents(temp.filePath("rentals.txt")) != ledgerBefore,
          "domain rental lifecycle persists without rewriting fleet file");
    const auto fleetAfter=contents(temp.filePath("vehicles.txt"));
    const auto ledgerAfter=contents(temp.filePath("rentals.txt"));
    model.setStatusFilter("All");
    model.setStatusFilter("History");
    model.refresh();
    check(contents(temp.filePath("vehicles.txt"))==fleetAfter &&
          contents(temp.filePath("rentals.txt"))==ledgerAfter,
          "all filtering and read model operations are byte-nonmutating");

    RentalSystem reloaded(temp.path());
    RentalReadService reloadedService(reloaded);
    RentalListModel history(reloadedService);
    history.setStatusFilter("History");
    check(history.count()==2 && itemId(history,0)=="R001" &&
          itemId(history,1)=="R003" &&
          history.data(history.index(1,0),RentalListModel::StatusRole)=="Archived",
          "persisted returned and archived history survives restart");
    std::cout << "P3-H C++ Active/History filtered read model PASS\n";
    return EXIT_SUCCESS;
}
