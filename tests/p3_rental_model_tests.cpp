#include "src/application/rental_list_model.h"
#include "src/application/rental_read_service.h"
#include "car.h"
#include "motorcycle.h"
#include "rental.h"
#include "rentalsystem.h"

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QDate>
#include <QFile>
#include <QObject>
#include <QTemporaryDir>
#include <QVariant>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
void check(bool value, const char *message) {
    if (!value) {
        std::cerr << "P3 Rental list model FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
QByteArray readBytes(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "fixture must be readable");
    return file.readAll();
}
void book(RentalSystem &system, const QString &id,
          const QString &vehicleId, int days, double rate) {
    const QDate today = QDate::currentDate();
    auto rental = std::make_unique<Rental>(
        id, "Fictional Customer", "0000000000", vehicleId,
        today, today.addDays(days), days, rate, rate * days);
    check(system.createRental(rental.get()), "persist fictional rental");
    rental.release();
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir disposable;
    check(disposable.isValid(), "isolated temporary rental ledger");
    const QDate today = QDate::currentDate();
    check(today.isValid(), "date is valid");
    const QString vehiclesPath = disposable.filePath("vehicles.txt");
    const QString rentalsPath = disposable.filePath("rentals.txt");

    {
        RentalSystem system(disposable.path());
        RentalReadService service(system, [today] { return today.addDays(2); });
        RentalListModel model(service);

        int resets = 0;
        int countSignals = 0;
        int selectionSignals = 0;
        QObject::connect(&model, &QAbstractItemModel::modelReset,
                         [&] { ++resets; });
        QObject::connect(&model, &RentalListModel::countChanged,
                         [&] { ++countSignals; });
        QObject::connect(&model, &RentalListModel::selectionChanged,
                         [&] { ++selectionSignals; });

        check(model.count() == 0 && model.rowCount() == 0 &&
              model.rowCount(model.index(0, 0)) == 0 &&
              model.selectedRow() == -1 && model.selectedRentalId().isEmpty(),
              "fresh model is empty and never has an implicit selection");
        check(!model.selectById("R001"), "cannot select an unknown rental");
        const auto roles = model.roleNames();
        check(roles.value(RentalListModel::RentalIdRole) == "rentalId" &&
              roles.value(RentalListModel::StatusRole) == "status" &&
              roles.value(RentalListModel::DueStateRole) == "dueState" &&
              roles.value(RentalListModel::TotalRole) == "total",
              "typed semantic Qt model roles exist");
        for (const auto &roleName : roles) {
            const QByteArray lower = roleName.toLower();
            check(!lower.contains("customer") && !lower.contains("phone"),
                  "general-purpose Qt roles exclude personal contact data");
        }

        check(system.addVehicle(new Car("C001", "Toyota", "Corolla", 450, 4)),
              "add fictional Corolla");
        check(system.addVehicle(new Car("C002", "Ford", "Ranger", 700, 4)),
              "add fictional Ranger");
        check(system.addVehicle(new Motorcycle("M001", "Honda", "CB500F", 280, 471)),
              "add fictional Honda");
        book(system, "R001", "C001", 3, 450);
        book(system, "R002", "C002", 1, 700);
        book(system, "R003", "M001", 2, 280);

        check(model.count() == 0 && resets == 0,
              "domain changes are not presented until explicit refresh");
        const QByteArray fleetBefore = readBytes(vehiclesPath);
        const QByteArray rentalsBefore = readBytes(rentalsPath);
        model.refresh();
        check(model.rowCount() == 3 && model.count() == 3 &&
              resets == 1 && countSignals == 1 && selectionSignals == 0,
              "first explicit refresh resets model and notifies count once");
        check(model.rowCount(model.index(0, 0)) == 0,
              "Qt list model has no child rows");
        // UX-S5: advancing the injected read-only calendar changes due-state
        // metrics and visible urgency rows without a domain transaction.
        QDate movingDay=today;
        RentalReadService movingRead(system,[&movingDay] {return movingDay;});
        RentalListModel rollover(movingRead);
        const double movingBookedValue=rollover.activeBookedValue();
        rollover.setStatusFilter("DueToday");
        check(rollover.count()==0 && rollover.dueTodayCount()==0 &&
              rollover.overdueCount()==0,
              "day zero has no due-today or overdue active rentals");
        movingDay=today.addDays(1);
        rollover.refresh();
        check(rollover.count()==1 && rollover.dueTodayCount()==1 &&
              rollover.overdueCount()==0 &&
              rollover.data(rollover.index(0,0),RentalListModel::RentalIdRole)=="R002",
              "next calendar day refresh reveals R002 due today");
        check(rollover.selectById("R002"),"read-only due-today selection");
        movingDay=today.addDays(2);
        rollover.refresh();
        check(rollover.count()==1 && rollover.dueTodayCount()==1 &&
              rollover.overdueCount()==1 &&
              rollover.oldestOverdueRentalId()=="R002" &&
              rollover.data(rollover.index(0,0),RentalListModel::RentalIdRole)=="R003" &&
              rollover.selectedRentalId().isEmpty() &&
              rollover.activeCount()==3 &&
              rollover.activeBookedValue()==movingBookedValue &&
              readBytes(vehiclesPath)==fleetBefore &&
              readBytes(rentalsPath)==rentalsBefore,
              "second day refresh changes urgency and disarms hidden selection without writes");

        // This navigation reuses the complete snapshot and does not interfere
        // with lifecycle filters, selection count assertions or persistence.
        // UX-S3: urgency filters use authoritative C++ due-state snapshots.
        RentalListModel timingRoute(service);
        const double fullValue=timingRoute.activeBookedValue();
        check(timingRoute.overdueCount()==1 && timingRoute.dueTodayCount()==1 &&
              timingRoute.activeCount()==3,"fixture has overdue/due-today active rentals");
        timingRoute.setStatusFilter("Overdue");
        check(timingRoute.count()==1 &&
              timingRoute.data(timingRoute.index(0,0),RentalListModel::RentalIdRole)=="R002" &&
              timingRoute.selectVisibleRow(0) && timingRoute.selectedRentalId()=="R002" &&
              timingRoute.activeBookedValue()==fullValue,
              "Overdue filters one active overdue; complete metrics remain");
        timingRoute.setStatusFilter("DueToday");
        check(timingRoute.count()==1 &&
              timingRoute.data(timingRoute.index(0,0),RentalListModel::RentalIdRole)=="R003" &&
              timingRoute.selectedRentalId().isEmpty() &&
              timingRoute.activeBookedValue()==fullValue,
              "DueToday shows one and disarms hidden overdue selection");
        timingRoute.setStatusFilter("dueToday"); // invalid case
        check(timingRoute.statusFilter()=="DueToday" &&
              !timingRoute.selectById("R002") &&
              timingRoute.locateById(" r002 ") &&
              timingRoute.statusFilter()=="All" &&
              timingRoute.selectedRentalId()=="R002",
              "invalid filter no-op, explicit lookup reveals hidden match");
        timingRoute.setStatusFilter("Overdue");
        check(timingRoute.locateActiveReturn(" M001 ") &&
              timingRoute.statusFilter()=="Active" &&
              timingRoute.selectedRentalId()=="R003" &&
              readBytes(vehiclesPath)==fleetBefore &&
              readBytes(rentalsPath)==rentalsBefore,
              "read-only return lookup safely reveals due-hidden active contract");
        RentalListModel overdueRoute(service);
        overdueRoute.setStatusFilter("History");
        check(overdueRoute.oldestOverdueRentalId()=="R002" &&
              overdueRoute.selectOldestOverdue() &&
              overdueRoute.statusFilter()=="Active" &&
              overdueRoute.selectedRentalId()=="R002" &&
              overdueRoute.selectedRow()==1,
              "oldest overdue selectable from history by domain due state");
        check(readBytes(vehiclesPath)==fleetBefore &&
              readBytes(rentalsPath)==rentalsBefore,
              "overdue routing is read-only");


        // P6 operator parity: explicit ID lookup must reveal filtered-out
        // records but never change selection, filter, or disk on a miss.
        RentalListModel lookup(service);
        lookup.setStatusFilter("History");
        check(lookup.count() == 0 && !lookup.locateById(" ") &&
              !lookup.locateById("R999") &&
              lookup.statusFilter() == "History" &&
              lookup.selectedRentalId().isEmpty(),
              "blank/unknown lookup preserves hidden History filter");
        check(lookup.locateById("  r002  ") &&
              lookup.statusFilter() == "All" &&
              lookup.selectedRentalId() == "R002" &&
              lookup.selectedRow() == 1,
              "case-insensitive lookup reveals and selects hidden rental");
        lookup.setStatusFilter("Active");
        check(lookup.locateById("R003") &&
              lookup.statusFilter() == "Active" &&
              lookup.selectedRentalId() == "R003" &&
              lookup.selectedRow() == 2,
              "visible lookup preserves the current filter");
        check(!lookup.locateById("R999") &&
              lookup.selectedRentalId() == "R003" &&
              lookup.statusFilter() == "Active" &&
              readBytes(vehiclesPath) == fleetBefore &&
              readBytes(rentalsPath) == rentalsBefore,
              "failed/read-only lookups preserve selection and storage");

        // UX-S18: rejected general lookups expose precise operator outcomes,
        // preserving the current selection/filter without silent guesses.
        lookup.setStatusFilter("Active");
        check(lookup.locateForOperator("  ")=="Empty" &&
              lookup.locateForOperator("R999")=="NotFound" &&
              lookup.statusFilter()=="Active" &&
              lookup.selectedRentalId()=="R003",
              "blank and unknown lookup outcomes preserve existing selection and filter");

        // UX-S17: normal Find rental accepts a uniquely matched vehicle ID,
        // including history hidden by a status filter, but never guesses
        // between multiple contracts for the same vehicle.
        lookup.setStatusFilter("History");
        check(lookup.locateById("  c002  ") &&
              lookup.statusFilter()=="All" &&
              lookup.selectedRentalId()=="R002" &&
              lookup.selectedRow()==1,
              "unique vehicle ID reveals hidden contract without writes");
        QList<RentalRecordSummary> overlapping=service.records();
        auto returnedCopy=overlapping.first();
        returnedCopy.rentalId="R004";
        returnedCopy.status=RentalRecordStatus::Returned;
        overlapping.append(returnedCopy);
        RentalListModel ambiguous([overlapping]() -> std::optional<QList<RentalRecordSummary>> {
            return overlapping;
        });
        ambiguous.setStatusFilter("Active");
        check(ambiguous.selectById("R001") &&
              ambiguous.locateForOperator(" c001 ")=="Ambiguous" &&
              !ambiguous.locateById(" c001 ") &&
              ambiguous.selectedRentalId()=="R001" &&
              ambiguous.statusFilter()=="Active" &&
              ambiguous.locateById("r004") &&
              ambiguous.selectedRentalId()=="R004" &&
              ambiguous.statusFilter()=="All" &&
              readBytes(vehiclesPath)==fleetBefore &&
              readBytes(rentalsPath)==rentalsBefore,
              "ambiguous vehicle lookup preserves selection and filter, exact rental ID remains usable");

        // UX-S13: reusable rental ID/vehicle ID search composes with
        // lifecycle filters, never filters PII and never changes KPIs/files.
        RentalListModel searchModel(service);
        int searchNotices=0, searchResets=0, searchCounts=0;
        QObject::connect(&searchModel,&RentalListModel::searchTextChanged,
                         [&]{++searchNotices;});
        QObject::connect(&searchModel,&QAbstractItemModel::modelReset,
                         [&]{++searchResets;});
        QObject::connect(&searchModel,&RentalListModel::countChanged,
                         [&]{++searchCounts;});
        check(searchModel.selectById("R001") && searchModel.count()==3 &&
              searchModel.searchText().isEmpty(), "search baseline");
        searchModel.setSearchText("  c002  ");
        check(searchModel.searchText()=="c002" &&
              searchModel.count()==1 &&
              searchModel.data(searchModel.index(0,0),
                  RentalListModel::RentalIdRole)=="R002" &&
              searchModel.selectedRentalId().isEmpty() &&
              searchModel.activeCount()==3 &&
              searchModel.historyCount()==0 &&
              searchCounts==1 && searchNotices==1 && searchResets==1,
              "vehicle ID case-insensitive search clears hidden selection, retains full KPIs");
        searchModel.setSearchText("C002");
        check(searchModel.searchText()=="C002" &&
              searchModel.count()==1 && searchNotices==2,
              "case-only search refreshes normalized query without changing count");
        searchModel.setStatusFilter("History");
        check(searchModel.count()==0 && searchModel.historyCount()==0 &&
              !searchModel.selectById("R002"),
              "search and lifecycle filter combine without hidden-row navigation");
        searchModel.setStatusFilter("Active");
        check(searchModel.count()==1 && searchModel.selectById("r002"),
              "active search selects verified matching ID");
        searchModel.setSearchText("R003");
        check(searchModel.count()==1 &&
              searchModel.data(searchModel.index(0,0),
                  RentalListModel::RentalIdRole)=="R003" &&
              searchModel.selectedRentalId().isEmpty(),
              "rental ID search rebinds filtered rows and clears stale selection");
        searchModel.setSearchText("unknown");
        check(searchModel.count()==0 && searchModel.selectedRow()==-1 &&
              !searchModel.selectVisibleRow(0) && searchModel.activeCount()==3,
              "unknown ID safely shows no matches, not empty operational fleet");
        check(searchModel.locateById("r001") &&
              searchModel.searchText().isEmpty() &&
              searchModel.statusFilter()=="Active" &&
              searchModel.selectedRentalId()=="R001" &&
              searchModel.locateActiveReturn("C002") &&
              searchModel.selectedRentalId()=="R002",
              "explicit ID lookup reveals search-hidden record without writes");
        searchModel.setSearchText("R001");
        // R003 is the fictional motorcycle M001, not a third car.
        check(searchModel.locateActiveReturn("M001") &&
              searchModel.searchText().isEmpty() &&
              searchModel.selectedRentalId()=="R003" &&
              readBytes(vehiclesPath)==fleetBefore &&
              readBytes(rentalsPath)==rentalsBefore,
              "active return lookup reveals search-hidden target and never mutates files");

        // P6-H: return search is a read-only active-only navigation step.
        // An unknown, empty or historical ID disarms a stale selection.
        RentalListModel returnRoute(service);
        returnRoute.setStatusFilter("History");
        check(returnRoute.locateActiveReturn("  c002  ") &&
              returnRoute.statusFilter()=="Active" &&
              returnRoute.selectedRentalId()=="R002" &&
              returnRoute.selectedRow()==1,
              "vehicle-ID return lookup reveals matching active rental");
        check(returnRoute.locateActiveReturn("r003") &&
              returnRoute.selectedRentalId()=="R003" &&
              returnRoute.statusFilter()=="Active",
              "rental-ID lookup selects canonical active record");
        check(!returnRoute.locateActiveReturn("C999") &&
              returnRoute.selectedRentalId().isEmpty() &&
              returnRoute.selectedRow()==-1 &&
              returnRoute.statusFilter()=="Active",
              "unknown vehicle ID disarms previously eligible return");
        check(returnRoute.locateActiveReturn("M001") &&
              !returnRoute.locateActiveReturn("  ") &&
              returnRoute.selectedRentalId().isEmpty() &&
              returnRoute.activeCount()==3 &&
              readBytes(vehiclesPath)==fleetBefore &&
              readBytes(rentalsPath)==rentalsBefore,
              "blank lookup disarms stale return without writing or changing KPIs");

        const QModelIndex first = model.index(0, 0);
        const QModelIndex second = model.index(1, 0);
        const QModelIndex third = model.index(2, 0);
        check(model.data(first, RentalListModel::RentalIdRole).toString() == "R001" &&
              model.data(first, RentalListModel::VehicleIdRole).toString() == "C001" &&
              model.data(first, RentalListModel::DaysRole).toInt() == 3 &&
              model.data(first, RentalListModel::DailyRateRole).toDouble() == 450.0 &&
              model.data(first, RentalListModel::TotalRole).toDouble() == 1350.0 &&
              model.data(first, RentalListModel::StartDateRole).toDate() == today &&
              model.data(first, RentalListModel::ExpectedReturnDateRole).toDate()
                  == today.addDays(3),
              "correct authoritative ID/date/rate/total roles");
        check(model.data(first, RentalListModel::StatusRole).toString() == "Active" &&
              model.data(first, RentalListModel::DueStateRole).toString() == "NotDue" &&
              model.data(second, RentalListModel::DueStateRole).toString() == "Overdue" &&
              model.data(third, RentalListModel::DueStateRole).toString() == "DueToday",
              "C++ determines lifecycle and injected-clock due states");
        check(!model.data(QModelIndex(), RentalListModel::StatusRole).isValid() &&
              !model.data(first, Qt::UserRole + 999).isValid() &&
              !(model.flags(first) & Qt::ItemIsEditable),
              "invalid role/index and editable item state are not exposed");
        check(readBytes(vehiclesPath) == fleetBefore &&
              readBytes(rentalsPath) == rentalsBefore,
              "read model does not modify stored fleet or rental records");

        check(model.selectById("  r003  ") &&
              model.selectedRentalId() == "R003" &&
              model.selectedRow() == 2 && selectionSignals == 1,
              "stable trimmed/case-insensitive rental selection");
        check(model.selectById("R003") && selectionSignals == 1,
              "reselecting same rental does not emit a duplicate signal");
        check(!model.selectById("R999") && !model.selectById(" ") &&
              model.selectedRentalId() == "R003",
              "unavailable ID does not discard existing selection");
        check(system.completeRental("R002", today),
              "return an independently persisted rental");
        // Until a successful domain commit is followed by refresh, the
        // previously captured list deliberately remains unchanged.
        returnRoute.refresh();
        const QByteArray afterReturnFleet=readBytes(vehiclesPath);
        const QByteArray afterReturnLedger=readBytes(rentalsPath);
        check(returnRoute.locateActiveReturn("C001") &&
              returnRoute.selectedRentalId()=="R001" &&
              !returnRoute.locateActiveReturn("C002") &&
              returnRoute.selectedRentalId().isEmpty() &&
              !returnRoute.locateActiveReturn("r002") &&
              returnRoute.selectedRentalId().isEmpty() &&
              readBytes(vehiclesPath)==afterReturnFleet &&
              readBytes(rentalsPath)==afterReturnLedger,
              "returned vehicle/rental IDs refused, no second return or mutation");
        check(model.data(second, RentalListModel::StatusRole).toString() == "Active",
              "model does not optimistically apply domain change");
        model.refresh();
        check(model.data(model.index(1, 0), RentalListModel::StatusRole).toString()
                  == "Returned" &&
              model.data(model.index(1, 0), RentalListModel::DueStateRole).toString()
                  == "NotApplicable" &&
              model.selectedRentalId() == "R003" &&
              model.selectedRow() == 2 && selectionSignals == 1,
              "refresh reflects returned status without changing selected rental");

        check(system.archiveRental("R002"), "archive returned contract");
        model.refresh();
        check(model.data(model.index(1, 0), RentalListModel::StatusRole).toString()
                  == "Archived" &&
              model.selectedRentalId() == "R003",
              "archived status reads from domain and preserves selected ID");

        check(system.deleteArchivedRental("R002"), "remove archived contract");
        model.refresh();
        check(model.count() == 2 &&
              model.selectedRentalId() == "R003" &&
              model.selectedRow() == 1 &&
              selectionSignals == 2,
              "selection follows stable rental ID after prior row disappears");
        check(model.data(model.index(1, 0), RentalListModel::RentalIdRole)
                  .toString() == "R003",
              "selected row now points to the correct rental, not stale position");

        check(model.oldestOverdueRentalId().isEmpty() &&
              !model.selectOldestOverdue(),
              "no overdue action after confirmed overdue return");
        check(model.selectById("R001") && model.selectedRow() == 0,
              "select available active contract");
        check(system.completeRental("R001", today), "complete selected contract");
        model.refresh();
        check(model.selectedRentalId() == "R001" &&
              model.data(model.index(0, 0), RentalListModel::StatusRole)
                  .toString() == "Returned" &&
              model.data(model.index(0, 0), RentalListModel::ActualReturnDateRole)
                  .toDate() == today,
              "selected record remains canonical after return");
        const QByteArray beforeHistoryRead = readBytes(rentalsPath);
        const QByteArray beforeHistoryFleet = readBytes(vehiclesPath);
        check(!system.removeVehicle("C001") &&
              system.hasRentalHistoryForVehicle("C001") &&
              system.findVehicleById("C001") != nullptr &&
              readBytes(vehiclesPath) == beforeHistoryFleet,
              "returned rental preserves its referenced vehicle in Fleet");
        model.refresh();
        check(model.data(model.index(0, 0), RentalListModel::VehicleIdRole)
                  .toString() == "C001" &&
              readBytes(rentalsPath) == beforeHistoryRead,
              "historical rental and existing vehicle survive read refresh");

        check(system.completeRental("R003", today), "return last active rental");
        check(system.archiveRental("R003"), "archive last returned rental");
        model.refresh();
        check(model.data(model.index(1, 0), RentalListModel::StatusRole)
                  .toString() == "Archived",
              "archive change follows explicit refresh");
        check(model.selectById("R003"), "select contract that is about to be deleted");
        const int beforeMissing = selectionSignals;
        check(system.deleteArchivedRental("R003"), "delete archived selected contract");
        model.refresh();
        check(model.count() == 1 &&
              model.selectedRentalId().isEmpty() &&
              model.selectedRow() == -1 &&
              selectionSignals == beforeMissing + 1,
              "missing selected ID clears exactly once on explicit refresh");

        const int afterClear = selectionSignals;
        model.clearSelection();
        check(selectionSignals == afterClear,
              "clearSelection is idempotent when selection is absent");
        check(model.selectById("R001"), "reselect remaining returned rental");
        model.clearSelection();
        check(model.selectedRow() == -1 && model.selectedRentalId().isEmpty(),
              "explicit close/deselect clears selection without domain mutation");
        const QByteArray lastFleet = readBytes(vehiclesPath);
        const QByteArray lastRental = readBytes(rentalsPath);
        model.refresh();
        check(readBytes(vehiclesPath) == lastFleet &&
              readBytes(rentalsPath) == lastRental,
              "final refresh cannot write any operational data");
    }

    // P6-J: the Overview's deliberately small read-only contract history
    // comes from the complete C++ snapshot, not the selected Rentals filter.
    {
        QTemporaryDir recentDir;
        check(recentDir.isValid(), "isolated bounded recent ledger");
        RentalSystem recentSystem(recentDir.path());
        RentalReadService recentRead(recentSystem);
        RentalListModel recent(recentRead);
        check(recent.recentActivities().isEmpty(),
              "empty recent panel derives no synthetic or placeholder rows");
        for(int number=1;number<=6;++number) {
            const QString vehicle=QString("C%1").arg(number,3,10,QLatin1Char('0'));
            const QString rental=QString("R%1").arg(number,3,10,QLatin1Char('0'));
            check(recentSystem.addVehicle(new Car(vehicle,"Fictional","Car",450,4)),
                  "create isolated vehicle for recent sorting");
            book(recentSystem,rental,vehicle,number,450);
        }
        const QByteArray vehicleBytes=readBytes(recentDir.filePath("vehicles.txt"));
        const QByteArray ledgerBytes=readBytes(recentDir.filePath("rentals.txt"));
        recent.refresh();
        const QVariantList latest=recent.recentActivities();
        check(latest.size()==4 &&
              latest.at(0).toMap().value("rentalId").toString()=="R006" &&
              latest.at(3).toMap().value("rentalId").toString()=="R003" &&
              latest.at(0).toMap().value("startDate").toString()==today.toString(Qt::ISODate) &&
              latest.at(0).toMap().value("status").toString()=="Active" &&
              latest.at(0).toMap().keys()==QStringList({"rentalId","startDate","status","total","vehicleId"}),
              "bounded four recent contracts with stable same-day order and no contact keys");
        recent.setStatusFilter("Archived");
        check(recent.count()==0 && recent.recentActivities()==latest &&
              recent.activeCount()==6 &&
              readBytes(recentDir.filePath("vehicles.txt"))==vehicleBytes &&
              readBytes(recentDir.filePath("rentals.txt"))==ledgerBytes,
              "read-only recent panel ignores status filter and never writes");
        check(recentSystem.completeRental("R006",today),
              "return isolated recent contract");
        const QVariantList stale=recent.recentActivities();
        check(stale.at(0).toMap().value("status").toString()=="Active",
              "recent panel does not optimistically change before domain refresh");
        recent.refresh();
        check(recent.recentActivities().at(0).toMap().value("status").toString()=="Returned" &&
              recent.recentActivities().size()==4 &&
              recent.activeCount()==5,
              "recent rows rehydrate lifecycle state after authoritative commit");
        // P6-K: typed selected-contract projection must be narrow, update on
        // selection and after lifecycle refresh, and drop hidden/stale IDs.
        int detailNotifications=0;
        QObject::connect(&recent,&RentalListModel::selectedContractChanged,
                         [&detailNotifications]{ ++detailNotifications; });
        check(recent.selectedContract().isEmpty(),
              "no selected rental has no contract detail or contact data");
        recent.setStatusFilter("All");
        check(recent.selectById("R006") && detailNotifications==1,
              "selected contract change notified exactly once");
        const QVariantMap detail=recent.selectedContract();
        check(detail.value("rentalId").toString()=="R006" &&
              detail.value("vehicleId").toString()=="C006" &&
              detail.value("startDate").toString()==today.toString(Qt::ISODate) &&
              detail.value("expectedReturnDate").toString()==today.addDays(6).toString(Qt::ISODate) &&
              detail.value("actualReturnDate").toString()==today.toString(Qt::ISODate) &&
              detail.value("days").toInt()==6 &&
              detail.value("dailyRate").toDouble()==450.0 &&
              detail.value("total").toDouble()==2700.0 &&
              detail.value("status").toString()=="Returned" &&
              detail.keys()==QStringList({"actualReturnDate","dailyRate","days","dueState",
                                           "expectedReturnDate","rentalId","startDate","status",
                                           "total","vehicleId"}),
              "selected contract contains verified dates and exact no-contact keys");
        const int beforeSelect=detailNotifications;
        check(!recent.selectById("R999") && detailNotifications==beforeSelect &&
              recent.selectedContract()==detail,
              "unknown selection neither exposes another record nor changes details");
        recent.setStatusFilter("Active");
        check(recent.selectedRentalId().isEmpty() &&
              recent.selectedContract().isEmpty() &&
              detailNotifications==beforeSelect+1,
              "filtered-out history clears details and notification");
        recent.setStatusFilter("All");
        check(recent.selectById("R006"),"reselect returned contract for archived refresh");
        const int beforeArchive=detailNotifications;
        check(recentSystem.archiveRental("R006"),"archive returned fixture for selected-detail refresh");
        check(recent.selectedContract().value("status").toString()=="Returned",
              "details never optimistically update before C++ refresh");
        recent.refresh();
        check(recent.selectedContract().value("status").toString()=="Archived" &&
              detailNotifications==beforeArchive+1,
              "details rebind when selected record status changes but ID is stable");
        check(recentSystem.deleteArchivedRental("R006"),
              "delete archived fictional selection for stale-detail test");
        recent.refresh();
        check(recent.selectedContract().isEmpty() &&
              recent.selectedRentalId().isEmpty(),
              "deleted selected rental revokes details after authoritative refresh");
    }

    // UX-S11: failed provider reads retain their known-good buffer, but
    // neither public Qt roles nor direct C++ navigation may treat it as
    // verified. Recovery restores the exact selected contract read-only.
    {
        RentalSystem recoveredDomain(disposable.path());
        RentalReadService recoveredRead(recoveredDomain);
        bool failProvider=false;
        RentalListModel guarded([&]() -> std::optional<QList<RentalRecordSummary>> {
            if(failProvider)return std::nullopt;
            return recoveredRead.records();
        });
        int resets=0, availabilitySignals=0, detailSignals=0, metricSignals=0;
        QObject::connect(&guarded,&QAbstractItemModel::modelReset,
                         [&]{++resets;});
        QObject::connect(&guarded,&RentalListModel::snapshotAvailabilityChanged,
                         [&]{++availabilitySignals;});
        QObject::connect(&guarded,&RentalListModel::selectedContractChanged,
                         [&]{++detailSignals;});
        QObject::connect(&guarded,&RentalListModel::metricsChanged,
                         [&]{++metricSignals;});
        check(guarded.snapshotAvailable() && guarded.count()==1 &&
              guarded.selectById("R001") &&
              guarded.selectedContract().value("rentalId")=="R001" &&
              guarded.data(guarded.index(0,0),
                           RentalListModel::RentalIdRole)=="R001",
              "baseline verified fictional return receipt");
        guarded.setSearchText("  c001 ");
        check(guarded.searchText()=="c001" &&
              guarded.selectedRentalId()=="R001" &&
              guarded.count()==1, "verified search retained before read outage");
        const auto originalSelection=guarded.selectedRentalId();
        const auto originalCount=guarded.count();
        const auto fleetBytes=readBytes(vehiclesPath);
        const auto rentalBytes=readBytes(rentalsPath);
        const int initialDetails=detailSignals;
        const int beforeOutageResets=resets;
        const int beforeOutageMetrics=metricSignals;
        failProvider=true;
        guarded.refresh();
        check(!guarded.snapshotAvailable() &&
              guarded.count()==originalCount &&
              guarded.selectedRentalId()==originalSelection &&
              guarded.statusFilter()=="All" &&
              guarded.searchText()=="c001" &&
              guarded.selectedContract().isEmpty() &&
              guarded.recentActivities().isEmpty() &&
              guarded.oldestOverdueRentalId().isEmpty() &&
              !guarded.data(guarded.index(0,0),
                            RentalListModel::RentalIdRole).isValid() &&
              !guarded.selectById("R001") &&
              !guarded.selectVisibleRow(0) &&
              !guarded.locateById("R001") &&
              guarded.locateForOperator("R001")=="Unavailable" &&
              !guarded.locateActiveReturn("R001") &&
              !guarded.selectOldestOverdue() &&
              guarded.selectedRentalId()==originalSelection &&
              availabilitySignals==1 && detailSignals==initialDetails+1 &&
              resets==beforeOutageResets &&
              metricSignals==beforeOutageMetrics &&
              readBytes(vehiclesPath)==fleetBytes &&
              readBytes(rentalsPath)==rentalBytes,
              "failed read hides retained roles/details and blocks all lookup routes without writes");
        guarded.refresh();
        check(availabilitySignals==1 && detailSignals==initialDetails+1 &&
              resets==beforeOutageResets && guarded.count()==originalCount,
              "repeated failed reads never emit false resets or empty database");
        failProvider=false;
        guarded.refresh();
        check(guarded.snapshotAvailable() && guarded.count()==originalCount &&
              guarded.searchText()=="c001" &&
              guarded.selectedRentalId()==originalSelection &&
              guarded.selectedContract().value("rentalId")=="R001" &&
              guarded.data(guarded.index(0,0),
                           RentalListModel::RentalIdRole)=="R001" &&
              guarded.selectById("R001") &&
              guarded.locateById("r001") &&
              guarded.locateForOperator("r001")=="Selected" &&
              availabilitySignals==2 && resets==beforeOutageResets+1 &&
              metricSignals==beforeOutageMetrics+1 &&
              readBytes(vehiclesPath)==fleetBytes &&
              readBytes(rentalsPath)==rentalBytes,
              "successful retry restores verified roles, navigation and no-write persistence");
    }

    {
        RentalSystem reopened(disposable.path());
        RentalReadService service(reopened);
        RentalListModel model(service);
        check(model.count() == 1 && model.rowCount() == 1 &&
              model.data(model.index(0, 0), RentalListModel::RentalIdRole)
                  .toString() == "R001" &&
              model.data(model.index(0, 0), RentalListModel::StatusRole)
                  .toString() == "Returned" &&
              model.data(model.index(0, 0), RentalListModel::DueStateRole)
                  .toString() == "NotApplicable" &&
              model.selectedRentalId().isEmpty(),
              "real restart reconstructs history, no implicit selection");
    }

    std::cout << "P3 Rental Qt list model roles, selection and refresh PASS\n";
    return EXIT_SUCCESS;
}
