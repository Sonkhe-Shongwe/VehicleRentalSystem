// P7-I: real Qt Quick controls exercising typed normalized repository via
// QObject controller. Built ONLY by explicitly-enabled fictional P7 CTests.
// No arbitrary file path or real PII input. Never a production executable.
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTest>
#include <QTimer>
#include <QDebug>
#include <QSignalSpy>
#include "src/application/p7_sandbox/sqlite_read_projection.h"
#include "src/application/p7_sandbox/fictional_fleet_snapshot.h"
#include "src/application/rental_list_model.h"
#include "src/application/p7_sandbox/repository_preview_controller.h"

static QQuickItem *find(QQuickItem *item, const QString &name) {
    if(!item)return nullptr;
    if(item->objectName()==name)return item;
    for(auto *child:item->childItems())
        if(auto *value=find(child,name))return value;
    return nullptr;
}
int main(int argc,char **argv) {
    QGuiApplication app(argc,argv);
    if(app.arguments().size()!=2 ||
       (app.arguments()[1]!="journey" && app.arguments()[1]!="rollback"
        && app.arguments()[1]!="read_model")) {
        qCritical()<<"Only fixed fictional P7-I validation cases accepted";return 2;
    }
    p7fiction::PreviewController preview;
    if(!preview.ready() || !preview.committedStateConsistent())return 3;
    bool refuseReads=false;
    bool refuseFleet=false;
    p7fiction::FictionalFleetSnapshot fleetModel(
        [&preview,&refuseFleet]() -> std::optional<QList<FleetVehicleSummary>> {
            if(refuseFleet)return {};
            const auto *repo=preview.testOnlyReadRepository();
            if(!repo)return {};
            return p7fiction::ReadProjection(*repo).fleet();
        });
    RentalListModel readModel(
        [&preview,&refuseReads]() -> std::optional<QList<RentalRecordSummary>> {
            if(refuseReads)return {}; // Fixed harness fault, no file tampering.
            const auto *repo=preview.testOnlyReadRepository();
            if(!repo)return {};
            return p7fiction::ReadProjection(*repo).rentals(QDate(2026,11,2));
        });
    QObject::connect(&preview,&p7fiction::PreviewController::committed,
                     &readModel,&RentalListModel::refresh);
    QObject::connect(&preview,&p7fiction::PreviewController::committed,
                     &fleetModel,&p7fiction::FictionalFleetSnapshot::refresh);
    if(!fleetModel.snapshotAvailable() || fleetModel.totalCount()!=1 ||
       fleetModel.availableCount()!=1)return 16;
    QSignalSpy fleetUpdates(&fleetModel,&p7fiction::FictionalFleetSnapshot::snapshotChanged);
    if(!readModel.snapshotAvailable() || readModel.count()!=0 ||
       readModel.activeCount()!=0 || readModel.historyCount()!=0)return 11;
    QSignalSpy metrics(&readModel,&RentalListModel::metricsChanged);
    QQuickStyle::setStyle("Basic");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("previewController",&preview);
    engine.rootContext()->setContextProperty("sharedRentals",&readModel);
    engine.rootContext()->setContextProperty("fleetSnapshot",&fleetModel);
    engine.load(QUrl("qrc:/p7/p7_fictional_preview.qml"));
    auto *window=engine.rootObjects().isEmpty()?nullptr:
        qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if(!window){qCritical()<<"Qt Quick preview failed to load";return 4;}
    window->resize(880,720);
    QTimer::singleShot(800,&app,[&] {
        auto *root=window->contentItem();
        auto *quote=find(root,"p7QuoteLabel");
        auto *receipt=find(root,"p7ReceiptLabel");
        auto *state=find(root,"p7StateLabel");
        auto *shared=find(root,"p7SharedMetrics");
        auto *fleetLabel=find(root,"p7FleetMetrics");
        if(!quote||!receipt||!state||!shared||!fleetLabel ||
           !shared->property("text").toString().contains("Active rentals: 0") ||
           !fleetLabel->property("text").toString().contains("1 available")){
            app.exit(5);return;
        }
        const auto click=[&](const QString &name)->bool {
            auto *control=find(root,name);
            if(!control || !control->isVisible() || !control->isEnabled() ||
               control->width()<10 || control->height()<10)return false;
            const auto point=control->mapToScene(
                QPointF(control->width()/2.0,control->height()/2.0));
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,point.toPoint());
            QCoreApplication::processEvents();
            return true;
        };
        if(preview.rentalId()!="" ||
           !receipt->property("text").toString().contains("not issued") ||
           !click("p7PreviewButton") ||
           !quote->property("text").toString().contains("R 1350.00") ||
           !preview.committedStateConsistent() ||
           metrics.count()!=0 || readModel.activeCount()!=0 ||
           fleetUpdates.count()!=0 || fleetModel.availableCount()!=1){
            qCritical()<<"P7-J uncommitted quote incorrectly refreshed shared RentalListModel";
            app.exit(6);return;
        }
        if(app.arguments()[1]=="rollback") {
            if(!preview.rejectNextBookingForTest() ||
               !click("p7ConfirmButton") ||
               !preview.committedStateConsistent() ||
               preview.rentalId()!="" ||
               !receipt->property("text").toString().contains("not issued") ||
               metrics.count()!=0 || readModel.count()!=0 ||
               fleetUpdates.count()!=0 || fleetModel.availableCount()!=1 ||
               !click("p7ConfirmButton")) {
                qCritical()<<"P7-I rejected transaction issued false UI receipt";
                app.exit(7);return;
            }
        } else {
            if(!click("p7ConfirmButton")){app.exit(8);return;}
        }
        if(preview.rentalId()!="R001" ||
           !preview.committedStateConsistent() ||
           !receipt->property("text").toString().contains("R001") ||
           !state->property("text").toString().contains("ACTIVE") ||
           !preview.independentReopen() ||
           readModel.activeCount()!=1 || readModel.historyCount()!=0 ||
           readModel.count()!=1 || readModel.activeBookedValue()!=1350.0 ||
           metrics.count()!=1 || fleetUpdates.count()!=1 ||
           fleetModel.availableCount()!=0 ||
           !fleetLabel->property("text").toString().contains("0 available") ||
           !shared->property("text").toString().contains("Active rentals: 1") ||
           !readModel.selectById("r001") ||
           readModel.selectedContract().value("status").toString()!="Active"){
            qCritical()<<"P7-J shared RentalListModel did not reflect committed SQL booking";
            app.exit(9);return;
        }
        if(!click("p7ReturnButton") || !preview.committedStateConsistent() ||
           readModel.activeCount()!=0 || readModel.historyCount()!=1 ||
           readModel.selectedContract().value("status").toString()!="Returned" ||
           fleetModel.availableCount()!=1 || fleetUpdates.count()!=2 ||
           !click("p7ArchiveButton") || !preview.committedStateConsistent() ||
           readModel.selectedContract().value("status").toString()!="Archived" ||
           !click("p7RetireButton") || !preview.committedStateConsistent() ||
           !state->property("text").toString().contains("RETIRED") ||
           fleetUpdates.count()!=4 || fleetModel.availableCount()!=0 ||
           !preview.independentReopen() ||
           readModel.historyCount()!=1 || readModel.activeCount()!=0 ||
           metrics.count()!=4 ||
           !shared->property("text").toString().contains("History: 1")) {
            qCritical()<<"P7-I return/archive/retire not durable through Qt";
            app.exit(10);return;
        }
        if(app.arguments()[1]=="read_model") {
            readModel.setStatusFilter("Active");
            if(readModel.count()!=0 || readModel.activeCount()!=0 ||
               readModel.historyCount()!=1 || !readModel.locateById("R001") ||
               readModel.selectedContract().value("status").toString()!="Archived"){
                qCritical()<<"P7-J existing RentalListModel failed status filter and locate";
                app.exit(12);return;
            }
            const auto *repo=preview.testOnlyReadRepository();
            const auto fleet=repo?p7fiction::ReadProjection(*repo).fleet():std::nullopt;
            if(!fleet || fleet->size()!=1 || fleet->front().available ||
               fleet->front().details!="4 doors") {
                qCritical()<<"P7-J read-only Fleet mapping ignored retired occupancy";
                app.exit(13);return;
            }
            // P7-N: Fleet is a native C++/SQL projection wired to actual QML.
            // Corruption or provider failure must NOT publish stale figures,
            // pretend a COMMIT happened, or erase the last confirmed rows.
            QSignalSpy fleetAvailable(&fleetModel,
                &p7fiction::FictionalFleetSnapshot::snapshotAvailabilityChanged);
            const int fleetBefore=fleetUpdates.count();
            const auto retained=fleetModel.vehicles();
            refuseFleet=true;
            fleetModel.refresh();
            auto *fleetRetry=find(root,"p7RetryFleetButton");
            if(fleetModel.snapshotAvailable() ||
               fleetModel.totalCount()!=1 || fleetModel.availableCount()!=0 ||
               fleetModel.vehicles()!=retained ||
               fleetUpdates.count()!=fleetBefore || fleetAvailable.count()!=1 ||
               !fleetLabel->property("text").toString().contains("Fleet read unavailable") ||
               !fleetRetry || !fleetRetry->isVisible()) {
                qCritical()<<"P7-N failed Fleet read showed stale availability";
                app.exit(17);return;
            }
            refuseFleet=false;
            if(!click("p7RetryFleetButton") || !fleetModel.snapshotAvailable() ||
               fleetUpdates.count()!=fleetBefore+1 || fleetAvailable.count()!=2 ||
               !fleetLabel->property("text").toString().contains("0 available") ||
               fleetRetry->isVisible()) {
                qCritical()<<"P7-N Fleet retry failed to show verified availability";
                app.exit(18);return;
            }
            // P7-K: a failed provider read is visible and never presents old
            // figures as current, resets selection, or triggers false KPIs.
            QSignalSpy availability(&readModel,&RentalListModel::snapshotAvailabilityChanged);
            const int priorMetrics=metrics.count();
            const QString chosen=readModel.selectedRentalId();
            refuseReads=true;
            readModel.refresh();
            auto *retry=find(root,"p7RetryReadButton");
            if(readModel.snapshotAvailable() || readModel.count()!=1 ||
               readModel.historyCount()!=1 || readModel.selectedRentalId()!=chosen ||
               metrics.count()!=priorMetrics || availability.count()!=1 ||
               !shared->property("text").toString().contains("Read unavailable") ||
               !retry || !retry->isVisible()) {
                qCritical()<<"P7-K failed read exposed stale figures as current";
                app.exit(14);return;
            }
            refuseReads=false;
            if(!click("p7RetryReadButton") || !readModel.snapshotAvailable() ||
               metrics.count()!=priorMetrics+1 || availability.count()!=2 ||
               !shared->property("text").toString().contains("History: 1") ||
               retry->isVisible()) {
                qCritical()<<"P7-K successful retry failed to restore verified KPIs";
                app.exit(15);return;
            }
        }
        qInfo()<<"P7-J existing shared Rentals read model / native QML PASS"
               <<app.arguments()[1]<<"R001 fictional, zero contact fields";
        app.exit(0);
    });
    return app.exec();
}
