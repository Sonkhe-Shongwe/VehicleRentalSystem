// P7-H typed normalized SQLite candidate; generated fictional data only.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QDebug>
#include <memory>
#include "src/application/p7_sandbox/sqlite_repository.h"
#include "src/application/p7_sandbox/sqlite_read_projection.h"
using p7fiction::Repository;using p7fiction::Status;
namespace {
bool ok(bool b,const char *why){if(!b)qCritical()<<"P7-H FAIL:"<<why;return b;}
const QDate day=QDate::fromString("2026-11-02",Qt::ISODate);
struct Temp {
    QTemporaryDir dir{QDir::tempPath()+"/rental-p7h-XXXXXX"};
    Temp(){QFile f(dir.filePath(".fictional-only"));
        if(f.open(QIODevice::WriteOnly|QIODevice::NewOnly))
            f.write("P7-H GENERATED FICTIONAL ONLY\n");}
    std::unique_ptr<Repository> open(){return std::make_unique<Repository>(dir.path());}
};
bool seed(Repository &r){
    QString c,m;
    return ok(r.createNew()&&r.integrity(),"create normalized v1")&&
           ok(r.vehicle("Car","Toyota","Corolla","450.00",4,&c)==Status::Ok&&c=="C001","C001")&&
           ok(r.vehicle("Motorcycle","Honda","CB500F","280.00",471,&m)==Status::Ok&&m=="M001","M001");
}
bool lifecycle(){
    Temp t;auto r=t.open();if(!seed(*r))return false;
    auto q=r->quote("c001",3,day);
    if(!ok(q&&q->dailyCents==45000&&q->totalCents==135000,"exact cents quote"))return false;
    auto result=r->book(*q,day);
    if(!ok(result.status==Status::Ok&&result.id=="R001","committed R001")||
       !ok(r->next("R")==2&&r->count("rentals")==1,"durable allocation")||
       !ok(!r->quote("C001",2,day)&&r->book(*q,day).status==Status::Unavailable,"duplicate denied")||
       !ok(r->retire("C001")==Status::Unavailable,"rented vehicle retirement blocked"))return false;
    r.reset();r=t.open();auto rec=r->rental("R001");
    if(!ok(r->ready()&&rec&&rec->totalCents==135000&&rec->status=="ACTIVE","independent reopen"))return false;
    if(!ok(r->returnRental("R001",day,day)==Status::Ok,"return")||
       !ok(r->returnRental("R001",day,day)==Status::Unavailable,"no duplicate return")||
       !ok(r->archive("R001")==Status::Ok,"archive")||
       !ok(r->retire("C001")==Status::Ok,"non-destructive retirement"))return false;
    r.reset();r=t.open();rec=r->rental("R001");
    return ok(r->integrity()&&rec&&rec->status=="ARCHIVED"&&
              r->state("C001")=="RETIRED"&&!r->quote("C001",1,day),
              "history preserved across reopen");
}
bool rollback(){
    Temp t;auto r=t.open();if(!seed(*r))return false;
    auto q=r->quote("C001",3,day);if(!q)return false;
    r->failNextCommitForTest();
    auto refused=r->book(*q,day);
    if(!ok(refused.status==Status::Failed&&refused.id.isEmpty()&&
           r->next("R")==1&&r->count("rentals")==0,"rollback ID and booking"))return false;
    if(!ok(r->book(*q,day).id=="R001","retry no consumed ID")||
       !ok(r->returnRental("R001",day,day)==Status::Ok,"return"))return false;
    r->failNextCommitForTest();
    if(!ok(r->archive("R001")==Status::Failed&&
           r->rental("R001")->status=="RETURNED","archive rollback"))return false;
    return ok(r->archive("R001")==Status::Ok,"archive retry");
}
bool numbers(){
    if(!ok(Repository::cents("0.01")==1&&Repository::cents("450.00")==45000&&
           !Repository::cents("0.00")&&!Repository::cents("1.001")&&
           !Repository::cents("-2.00")&&!Repository::cents("1e3")&&
           !Repository::cents("92233720368547758.08"),"strict cents"))return false;
    Temp t;auto r=t.open();if(!seed(*r))return false;
    QString id="old";
    if(!ok(r->vehicle("Car","bad","price","0.00",4,&id)==Status::Invalid&&
           id.isEmpty()&&r->next("C")==2,"invalid does not issue ID"))return false;
    auto q=r->quote("C001",2,day);if(!q)return false;
    auto stale=*q;stale.totalCents+=1;
    if(!ok(r->book(stale,day).status==Status::Stale&&r->next("R")==1,
           "stale quote rejected before ID allocation"))return false;
    if(!ok(r->vehicle("Car","New","Asset","320.01",4,&id)==Status::Ok&&id=="C002","C002"))return false;
    r.reset();r=t.open();
    return ok(r->ready()&&r->next("C")==3,"ID high-water across reboot");
}
// P7-AB: the largest representable signed 64-bit cent amount is valid;
// the very next cent and multi-day total overflow must fail closed.
bool money_upper_boundary() {
    const qint64 ceiling=std::numeric_limits<qint64>::max();
    const auto upper=Repository::cents("92233720368547758.07");
    const auto whole=Repository::cents("92233720368547758.00");
    if(!ok(upper&&*upper==ceiling&&whole&&*whole==ceiling-7,
           "accept the full legal signed-int64 cent boundary")||
       !ok(!Repository::cents("92233720368547758.08")&&
           !Repository::cents("92233720368547758.99")&&
           !Repository::cents("92233720368547759.00")&&
           !Repository::cents("999999999999999999999999999999.99")&&
           !Repository::cents("0.00")&&
           !Repository::cents("-1.00"),
           "reject the first overflowing cent and other invalid money"))
        return false;
    Temp t; auto r=t.open();
    if(!seed(*r))return false;
    QString id;
    if(!ok(r->vehicle("Car","Fictional","UpperBound",
                      "92233720368547758.07",4,&id)==Status::Ok&&
           id=="C002"&&r->next("C")==3,
           "persist exact qint64 maximum as fictional vehicle rate"))
        return false;
    const auto single=r->quote(id,1,day);
    if(!ok(single&&single->dailyCents==ceiling&&
           single->totalCents==ceiling,
           "one-day exact maximum quote remains representable")||
       !ok(!r->quote(id,2,day)&&r->next("R")==1&&
           r->count("rentals")==0,
           "two-day multiplication overflow refused without allocating IDs"))
        return false;
    auto booked=r->book(*single,day);
    if(!ok(booked.status==Status::Ok&&booked.id=="R001"&&
           r->next("R")==2&&r->count("rentals")==1,
           "single-day maximum-cent booking commits atomically"))
        return false;
    r.reset(); r=t.open();
    const auto rental=r->rental("R001");
    const auto rows=r->rentalRows();
    return ok(r->ready()&&r->integrity()&&rental&&
              rental->totalCents==ceiling&&rental->status=="ACTIVE"&&
              rows&&rows->size()==1&&
              rows->first().dailyCents==ceiling&&
              rows->first().totalCents==ceiling&&
              r->next("R")==2,
              "exact largest-cent booking survives independent reopen");
}
bool invalid_return_date(){
    Temp t;
    auto r=t.open();
    if(!seed(*r))return false;
    auto q=r->quote("C001",3,day);
    if(!ok(q&&r->book(*q,day).status==Status::Ok&&
           r->returnRental("R001",day,day)==Status::Ok,
           "seed valid returned fictional contract"))return false;
    if(!ok(r->rentalRows().has_value()&&r->fleetRows().has_value(),
           "valid return visible before malformed row"))return false;
    // The v1 database CHECK only requires a returned row to have an actual
    // date; a manually corrupted earlier-than-start date can therefore exist
    // without failing SQLite integrity_check. Do not expose it in a read DTO.
    const QString name="p7m-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    bool changed=false;
    {
        auto db=QSqlDatabase::addDatabase("QSQLITE",name);
        db.setDatabaseName(t.dir.filePath("fictional.sqlite"));
        if(db.open()){
            QSqlQuery edit(db);
            changed=edit.exec("UPDATE rentals SET actual_date='2026-11-01' "
                              "WHERE id='R001' AND status='RETURNED'");
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    if(!ok(changed,"inject fixed impossible synthetic return date"))
        return false;
    return ok(r->integrity()&&!r->rentalRows().has_value()&&
              !r->fleetRows().has_value(),
              "reject impossible dates without showing partial Fleet/Rentals");
}
bool fleet_snapshot(){
    Temp t;
    auto r=t.open();
    if(!seed(*r))return false;
    auto clean=r->fleetRows();
    if(!ok(clean&&clean->vehicles.size()==2&&clean->rentals.isEmpty(),
           "single transaction empty fleet"))return false;
    auto q=r->quote("C001",1,day);
    if(!ok(q&&r->book(*q,day).status==Status::Ok,
           "seed an active fictional rental"))return false;
    auto competing=t.open();
    if(!ok(competing->ready(),"second independent connection"))return false;
    Status attempted=Status::Ok;
    // The second connection attempts to commit a return BETWEEN the Fleet
    // vehicle and rental SELECTs; it must not produce a mixed-time view.
    auto stable=r->fleetRows([&] {
        attempted=competing->returnRental("R001",day,day);
    });
    if(!ok(stable && stable->vehicles.size()==2 &&
           stable->rentals.size()==1 &&
           stable->rentals.front().status=="ACTIVE" &&
           attempted==Status::Failed,
           "concurrent write blocked until consistent Fleet read ends"))
        return false;
    if(!ok(r->rental("R001")->status=="ACTIVE" &&
           competing->returnRental("R001",day,day)==Status::Ok,
           "writer retries after read release"))return false;
    auto after=r->fleetRows();
    return ok(after && after->rentals.size()==1 &&
              after->rentals.front().status=="RETURNED",
              "new Fleet snapshot reflects later committed return");
}
// P7-U tests deliberately tamper only one disposable fictional SQLite file.
bool cross_record_integrity() {
    Temp t; auto r=t.open();
    if(!seed(*r))return false;
    auto quote=r->quote("C001",3,day);
    if(!ok(quote && r->book(*quote,day).status==Status::Ok,
           "seed fictional active rental"))return false;
    const auto accepted=[&]() {
        const p7fiction::ReadProjection projection(*r);
        const auto snapshot=r->fleetRows();
        const auto fleet=projection.fleet();
        const auto rentals=projection.rentals(day);
        return snapshot && fleet && rentals &&
               snapshot->vehicles.size()==2 && snapshot->rentals.size()==1 &&
               fleet->size()==2 && rentals->size()==1 &&
               rentals->front().rentalId=="R001";
    };
    const auto denied=[&]() {
        const p7fiction::ReadProjection projection(*r);
        return r->rentalRows().has_value() &&
               !r->fleetRows().has_value() &&
               !projection.fleet().has_value() &&
               !projection.rentals(day).has_value();
    };
    const auto alter=[&](const QString &sql,bool fkOff=false) {
        const QString name="p7u-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        bool success=false;
        {
            auto db=QSqlDatabase::addDatabase("QSQLITE",name);
            db.setDatabaseName(t.dir.filePath("fictional.sqlite"));
            if(db.open()) {
                {
                    QSqlQuery q(db);
                    success=(!fkOff||q.exec("PRAGMA foreign_keys=OFF"))&&q.exec(sql);
                }
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(name);
        return success;
    };
    QFile marker(t.dir.filePath(".fictional-only"));
    if(!ok(marker.open(QIODevice::ReadOnly),"open generated marker"))return false;
    const QByteArray original=marker.readAll(); marker.close();
    if(!ok(accepted(),"valid baseline cross-record read"))return false;
    if(!ok(alter("UPDATE vehicles SET state='RETIRED' WHERE id='C001'"),
           "inject active-on-retired synthetic relationship"))return false;
    if(!ok(r->integrity()&&denied(),
           "fail closed even if SQLite integrity_check passes"))return false;
    if(!ok(alter("UPDATE vehicles SET state='ACTIVE' WHERE id='C001'")&&
           accepted(),"restore verified Fleet and Rentals after repair"))return false;
    if(!ok(alter("UPDATE rentals SET vehicle_id='C999' WHERE id='R001'",true),
           "inject synthetic orphan reference"))return false;
    if(!ok(denied(),"fail closed on orphaned rental reference"))return false;
    if(!ok(alter("UPDATE rentals SET vehicle_id='C001' WHERE id='R001'",true)&&
           accepted()&&r->integrity(),"recover after orphan repair"))return false;
    if(!ok(marker.open(QIODevice::ReadOnly),"reopen marker"))return false;
    const QByteArray finalMarker=marker.readAll();
    return ok(finalMarker==original,"protected fictional-only marker unchanged");
}
// P7-V: reject a misleading v1 header if required SQL constraints or ID
// sequences were altered in this one generated-fictional temporary database.
bool schema_guard(){
    Temp t;auto r=t.open();if(!seed(*r))return false;
    r.reset(); // Only the test fixture edits the database, never the app.
    const auto edit=[&](const QString &statement) {
        const QString name="p7v-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        bool changed=false;
        {
            auto db=QSqlDatabase::addDatabase("QSQLITE",name);
            db.setDatabaseName(t.dir.filePath("fictional.sqlite"));
            if(db.open()) {
                {
                    QSqlQuery q(db);
                    changed=q.exec(statement);
                }
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(name);
        return changed;
    };
    const auto denied=[&] {
        auto suspect=t.open();
        QString issued="old";
        return !suspect->ready() && !suspect->integrity() &&
               !suspect->createNew() &&
               suspect->vehicle("Car","Demo","Car","100.00",4,&issued)==Status::Invalid &&
               issued.isEmpty();
    };
    if(!ok(edit("DROP INDEX one_active"),"drop generated-fictional index")||
       !ok(denied(),"missing active-occupancy index refused"))return false;
    if(!ok(edit("CREATE INDEX one_active ON rentals(vehicle_id COLLATE NOCASE) "
                "WHERE status='ACTIVE'"),"inject non-unique impostor")||
       !ok(denied(),"non-unique impostor index refused"))return false;
    if(!ok(edit("DROP INDEX one_active"),"remove non-unique impostor")||
       !ok(edit("CREATE UNIQUE INDEX one_active ON rentals(vehicle_id COLLATE NOCASE) "
                "WHERE status='ACTIVE'"),"restore exact active index"))return false;
    {
        auto recovered=t.open();
        if(!ok(recovered->ready()&&recovered->integrity()&&
               recovered->count("vehicles")==2&&recovered->next("R")==1,
               "restored index reopens without consuming IDs"))return false;
    }
    if(!ok(edit("DELETE FROM counters WHERE kind='R'"),
           "remove generated-fictional rental counter")||
       !ok(denied(),"missing rental ID sequence refused"))return false;
    if(!ok(edit("INSERT INTO counters(kind,next_id) VALUES('R',1)"),
           "restore rental ID sequence"))return false;
    r=t.open();
    QFile marker(t.dir.filePath(".fictional-only"));
    return ok(r->ready()&&r->integrity()&&r->next("R")==1&&
              r->count("vehicles")==2&&r->count("rentals")==0,
              "reopened recovered fictional schema and untouched data")&&
           ok(marker.open(QIODevice::ReadOnly)&&
              marker.readAll()=="P7-H GENERATED FICTIONAL ONLY\n",
              "fictional-only marker preserved");
}

// P7-AA: external counter rewind or malformed persisted ID must not reopen.
bool issued_id_guard(){
    Temp t; auto r=t.open();if(!seed(*r))return false;
    const auto quote=r->quote("C001",2,day);
    if(!ok(quote&&r->book(*quote,day).status==Status::Ok,
           "seed generated R001")||
       !ok(r->next("C")==2&&r->next("M")==2&&r->next("R")==2,
           "baseline high-water"))return false;
    r.reset();
    const auto edit=[&](const QString &statement){
        const QString name="p7aa-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        bool changed=false;
        {
            auto db=QSqlDatabase::addDatabase("QSQLITE",name);
            db.setDatabaseName(t.dir.filePath("fictional.sqlite"));
            if(db.open()){
                {
                    QSqlQuery query(db);
                    changed=query.exec(statement);
                }
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(name);
        return changed;
    };
    const auto denied=[&](const char *reason){
        auto suspect=t.open();QString assigned="sentinel";
        const bool refused=!suspect->ready()&&!suspect->integrity()&&
            suspect->count("vehicles")==-1&&suspect->count("rentals")==-1&&
            suspect->vehicle("Car","No","Write","100.00",4,&assigned)==Status::Invalid&&
            assigned.isEmpty();
        return ok(refused,reason);
    };
    for(const QString &kind:{QStringLiteral("C"),QStringLiteral("M"),QStringLiteral("R")}){
        if(!ok(edit("UPDATE counters SET next_id=1 WHERE kind='"+kind+"'"),
               "rewind fictional issued-ID counter")||
           !denied("rewound counter refuses reopen and writes")||
           !ok(edit("UPDATE counters SET next_id=2 WHERE kind='"+kind+"'"),
               "repair fictional issued-ID counter"))return false;
        auto recovered=t.open();
        if(!ok(recovered->ready()&&recovered->integrity()&&
               recovered->next(kind)==2&&recovered->count("rentals")==1,
               "repaired high-water allows safe reopen"))return false;
    }
    if(!ok(edit("UPDATE vehicles SET id='M0BAD' WHERE id='M001'"),
           "malform only unbooked motorcycle ID")||
       !denied("malformed issued-ID refuses reopen")||
       !ok(edit("UPDATE vehicles SET id='M001' WHERE id='M0BAD'"),
           "restore canonical motorcycle ID"))return false;
    auto restored=t.open();
    const auto rental=restored->rental("R001");
    if(!ok(restored->ready()&&restored->integrity()&&
           restored->count("vehicles")==2&&restored->count("rentals")==1&&
           rental&&rental->status=="ACTIVE"&&restored->next("C")==2&&
           restored->next("M")==2&&restored->next("R")==2,
           "issued history unmodified after repair"))return false;
    QString issued;
    return ok(restored->vehicle("Car","New","Fictional","250.00",4,&issued)==Status::Ok&&
              issued=="C002"&&restored->next("C")==3,
              "new C002 allocates after verified high-water");
}
// P7-AC: SQLite page integrity may remain healthy after v1 constraints
// disappear or the three table definitions are unexpectedly modified.
bool table_contract_guard(){
    const QStringList names={"counters","vehicles","rentals"};
    for(const QString &table:names){
        Temp t;auto r=t.open();
        if(!ok(t.dir.isValid()&&seed(*r),
               "create a clean generated-fictional v1 schema"))return false;
        const auto review=r->quote("C001",2,day);
        if(!ok(review&&r->book(*review,day).status==Status::Ok,
               "seed a real normalized fictional booking"))return false;
        r.reset(); // isolated out-of-band SQL fixture tampering only
        const QString connection="p7ac-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        bool changed=false,sqliteHealthy=false,recordsIntact=false;
        {
            auto db=QSqlDatabase::addDatabase("QSQLITE",connection);
            db.setDatabaseName(t.dir.filePath("fictional.sqlite"));
            if(db.open()){
                {
                    QSqlQuery query(db);
                    changed=query.exec("BEGIN IMMEDIATE");
                    const QStringList sql=table=="counters"
                        ?QStringList{
                            "CREATE TABLE weak_counters(kind TEXT PRIMARY KEY,next_id INTEGER NOT NULL)",
                            "INSERT INTO weak_counters SELECT kind,next_id FROM counters",
                            "DROP TABLE counters",
                            "ALTER TABLE weak_counters RENAME TO counters"
                        }
                        :QStringList{
                            "ALTER TABLE "+table+" ADD COLUMN unapproved_v1_extension TEXT"
                        };
                    for(const auto &statement:sql){
                        if(changed)changed=query.exec(statement);
                    }
                    if(changed)changed=query.exec("COMMIT");
                    else query.exec("ROLLBACK");
                    if(changed&&query.exec("PRAGMA integrity_check")&&query.next())
                        sqliteHealthy=query.value(0).toString()=="ok";
                    recordsIntact=changed&&query.exec(
                        "SELECT (SELECT count(*) FROM vehicles)=2 AND "
                        "(SELECT count(*) FROM rentals)=1 AND "
                        "(SELECT count(*) FROM counters)=3")&&
                        query.next()&&query.value(0).toInt()==1;
                }
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connection);
        if(!ok(changed&&sqliteHealthy&&recordsIntact,
               "tampered v1 tables remain SQLite-page-valid with all records")){
            return false;
        }
        auto suspect=t.open();
        QString id="sentinel";
        if(!ok(!suspect->ready()&&!suspect->integrity()&&
               !suspect->createNew()&&suspect->count("rentals")==-1&&
               suspect->vehicle("Car","No","Write","100.00",4,&id)==Status::Invalid&&
               id.isEmpty(),
               "reject altered table DDL without writes or v1 auto-migration"))
            return false;
        QFile marker(t.dir.filePath(".fictional-only"));
        if(!ok(marker.open(QIODevice::ReadOnly)&&
               marker.readAll()=="P7-H GENERATED FICTIONAL ONLY\n",
               "generated-fictional root marker remains intact"))
            return false;
    }
    Temp canonical;auto knownGood=canonical.open();
    if(!seed(*knownGood))return false;
    knownGood.reset();knownGood=canonical.open();
    return ok(knownGood->ready()&&knownGood->integrity()&&
              knownGood->count("vehicles")==2&&knownGood->count("rentals")==0,
              "canonical generated v1 still reopens without migration");
}
bool future(){
    Temp t;auto r=t.open();if(!seed(*r))return false;r.reset();
    const QString name="p7h-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    {auto db=QSqlDatabase::addDatabase("QSQLITE",name);
     db.setDatabaseName(t.dir.filePath("fictional.sqlite"));
     if(!db.open())return false;
     {QSqlQuery q(db);if(!q.exec("PRAGMA user_version=2"))return false;}
     db.close();}
    QSqlDatabase::removeDatabase(name);
    r=t.open();
    return ok(!r->ready()&&!r->createNew()&&
              r->vehicle("Car","x","y","10.00",4)==Status::Invalid,
              "newer schema fails closed");
}
bool contention(){
    Temp t;auto r=t.open();if(!seed(*r))return false;
    const QString name="p7h-lock-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    {auto db=QSqlDatabase::addDatabase("QSQLITE",name);
     db.setDatabaseName(t.dir.filePath("fictional.sqlite"));
     if(!db.open())return false;
     {QSqlQuery q(db);if(!q.exec("PRAGMA busy_timeout=0")||!q.exec("BEGIN IMMEDIATE"))return false;}
     if(!ok(r->vehicle("Car","x","y","200.00",4)==Status::Failed&&r->next("C")==2,
            "writer lock refusal preserves ID"))return false;
     {QSqlQuery q(db);if(!q.exec("ROLLBACK"))return false;}
     db.close();}
    QSqlDatabase::removeDatabase(name);
    QString id;
    return ok(r->vehicle("Car","New","Asset","200.00",4,&id)==Status::Ok&&id=="C002",
              "successful retry");
}
}
int main(int argc,char**argv){
    QCoreApplication app(argc,argv);if(app.arguments().size()!=2)return 2;
    const QString mode=app.arguments()[1];
    bool pass=mode=="lifecycle"?lifecycle():mode=="rollback"?rollback():
       mode=="money_ids"?numbers():mode=="future_schema"?future():
       mode=="contention"?contention():mode=="fleet_snapshot"?fleet_snapshot():
       mode=="invalid_return_date"?invalid_return_date():
       mode=="cross_record_integrity"?cross_record_integrity():
       mode=="schema_guard"?schema_guard():
       mode=="issued_id_guard"?issued_id_guard():
       mode=="money_upper_boundary"?money_upper_boundary():
       mode=="table_contract_guard"?table_contract_guard():false;
    if(pass)qInfo()<<"P7-H normalized fictional-only repository PASS"<<mode;
    return pass?0:1;
}
