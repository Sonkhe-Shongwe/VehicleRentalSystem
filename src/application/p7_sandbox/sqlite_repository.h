#pragma once
// P7-H candidate typed SQLite repository. Not linked to Rental Studio runtime.
// Compile ONLY in the OFF-by-default fictional-only P7 CTest target.
#include <QDate>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QList>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QUuid>
#include <QVariant>
#include <limits>
#include <optional>
#include <functional>
#include <utility>

namespace p7fiction {
enum class Status { Ok, Invalid, Unavailable, Stale, Failed };
struct Quote {
    QString vehicleId;
    QDate date;
    int days=0;
    qint64 dailyCents=0;
    qint64 totalCents=0;
};
struct Booking { Status status=Status::Invalid; QString id; };
struct RentalView { QString id, vehicleId, status; qint64 totalCents=0; };
// Read-only P7-J projection DTOs: integer cents stay canonical.
// Both collections fail closed (nullopt) if any SQL field is invalid.
struct VehicleRow {
    QString id, kind, brand, model, state;
    int spec=0;
    qint64 rateCents=0;
};
struct RentalRow {
    QString id, vehicleId, status;
    QDate start, expected, actual;
    int days=0;
    qint64 dailyCents=0, totalCents=0;
};
// Fleet occupancy must be built from one consistent SQLite read transaction,
// not vehicle rows and rental rows observed across separate commits.
struct FleetRows {
    QList<VehicleRow> vehicles;
    QList<RentalRow> rentals;
};
class Repository final {
    QString m_root, m_connection;
    QSqlDatabase db;
    bool live=false, rejectNext=false;
    // P7-AC: one authoritative, fixed fictional v1 table contract is used
    // for both schema creation and read-only reopen verification.
    static QString countersDdl(){
        return QStringLiteral(
            "CREATE TABLE counters(kind TEXT PRIMARY KEY CHECK(kind IN ('C','M','R')),"
            "next_id INTEGER NOT NULL CHECK(next_id>0))");
    }
    static QString vehiclesDdl(){
        return QStringLiteral(
            "CREATE TABLE vehicles(id TEXT PRIMARY KEY COLLATE NOCASE,"
            "kind TEXT NOT NULL CHECK(kind IN ('Car','Motorcycle')),"
            "brand TEXT NOT NULL,model TEXT NOT NULL,"
            "spec INTEGER NOT NULL CHECK(spec>0),"
            "rate_cents INTEGER NOT NULL CHECK(rate_cents>0),"
            "state TEXT NOT NULL DEFAULT 'ACTIVE' CHECK(state IN ('ACTIVE','RETIRED')))");
    }
    static QString rentalsDdl(){
        return QStringLiteral(
            "CREATE TABLE rentals(id TEXT PRIMARY KEY COLLATE NOCASE,"
            "vehicle_id TEXT NOT NULL REFERENCES vehicles(id) ON DELETE RESTRICT,"
            "start_date TEXT NOT NULL,expected_date TEXT NOT NULL,actual_date TEXT,"
            "days INTEGER NOT NULL CHECK(days BETWEEN 1 AND 365),"
            "daily_cents INTEGER NOT NULL CHECK(daily_cents>0),"
            "total_cents INTEGER NOT NULL CHECK(total_cents>0),"
            "status TEXT NOT NULL CHECK(status IN ('ACTIVE','RETURNED','ARCHIVED')),"
            "CHECK((status='ACTIVE' AND actual_date IS NULL) OR "
            "(status!='ACTIVE' AND actual_date IS NOT NULL)))");
    }
    static QString occupancyIndexDdl(){
        return QStringLiteral(
            "CREATE UNIQUE INDEX one_active ON rentals(vehicle_id COLLATE NOCASE)"
            " WHERE status='ACTIVE'");
    }
    bool sql(const QString &statement) {
        QSqlQuery q(db); return q.exec(statement);
    }
    bool run(const QString &statement,const QVariantList &values={}) {
        QSqlQuery q(db);
        if(!q.prepare(statement))return false;
        for(const auto &value:values)q.addBindValue(value);
        return q.exec();
    }
    QVariant one(const QString &statement,const QVariantList &values={}) const {
        QSqlQuery q(db);
        if(!q.prepare(statement))return {};
        for(const auto &value:values)q.addBindValue(value);
        return q.exec()&&q.next()?q.value(0):QVariant();
    }
    bool begin() { return sql("BEGIN IMMEDIATE"); }
    void rollback() { sql("ROLLBACK"); }
    bool commit() {
        if(rejectNext){rejectNext=false;rollback();return false;}
        if(sql("COMMIT"))return true;
        rollback();return false;
    }
    std::optional<QString> issue(const QString &kind) {
        if(kind!="C"&&kind!="M"&&kind!="R")return {};
        auto value=one("SELECT next_id FROM counters WHERE kind=?",{kind});
        if(!value.isValid()||value.toLongLong()<1||
           value.toLongLong()==std::numeric_limits<qint64>::max())return {};
        if(!run("UPDATE counters SET next_id=next_id+1 WHERE kind=?",{kind}))return {};
        return kind+QString("%1").arg(value.toLongLong(),3,10,QChar('0'));
    }
public:
    explicit Repository(const QString &root)
       : m_root(QDir::cleanPath(root)),
         m_connection("p7h-"+QUuid::createUuid().toString(QUuid::WithoutBraces)) {
        QFileInfo folder(m_root),flag(QDir(m_root).filePath(".fictional-only"));
        if(!folder.isDir()||folder.isSymLink()||flag.isSymLink())return;
        QFile file(flag.filePath());
        if(!file.open(QIODevice::ReadOnly)||
           file.readAll()!="P7-H GENERATED FICTIONAL ONLY\n"||
           !QSqlDatabase::isDriverAvailable("QSQLITE"))return;
        QFileInfo path(QDir(m_root).filePath("fictional.sqlite"));
        if(path.isSymLink())return;
        db=QSqlDatabase::addDatabase("QSQLITE",m_connection);
        db.setDatabaseName(path.filePath());
        if(!db.open()||!sql("PRAGMA foreign_keys=ON")||
           !sql("PRAGMA busy_timeout=0")||!sql("PRAGMA synchronous=FULL"))return;
        if(one("PRAGMA user_version").toInt()==1&&integrity())live=true;
    }
    ~Repository() {
        db.close();db=QSqlDatabase();
        if(QSqlDatabase::contains(m_connection))QSqlDatabase::removeDatabase(m_connection);
    }
    Repository(const Repository&)=delete;
    Repository& operator=(const Repository&)=delete;
    bool ready()const{return live;}
    bool integrity()const {
        if(!db.isOpen()||one("PRAGMA user_version").toInt()!=1||
           one("PRAGMA foreign_keys").toInt()!=1||
           one("PRAGMA integrity_check").toString()!="ok")return false;
        // P7-AC: an unchanged user_version and SQLite integrity_check=ok
        // do not prove that the v1 CHECK, PK, FK and NOT NULL constraints
        // still exist. Reject a mutated DDL contract, without migrating it.
        const auto exactTable=[&](const QString &name,const QString &expected){
            const QVariant stored=one(
                "SELECT sql FROM sqlite_master WHERE type='table' AND name=?",
                {name});
            return stored.isValid()&&stored.toString()==expected;
        };
        if(!exactTable("counters",countersDdl())||
           !exactTable("vehicles",vehiclesDdl())||
           !exactTable("rentals",rentalsDdl()))return false;
        // P7-V: also require the exact v1 partial UNIQUE occupancy index.
        const QVariant activeIndex=one(
            "SELECT sql FROM sqlite_master WHERE type='index' AND name='one_active'"
        );
        if(!activeIndex.isValid()||
           activeIndex.toString()!=occupancyIndexDdl())return false;
        // All three issued-ID sequences must be present and usable.
        // PRAGMA integrity_check does not validate these business invariants.
        const QVariant all=one("SELECT count(*) FROM counters");
        const QVariant valid=one(
            "SELECT count(*) FROM counters WHERE kind IN ('C','M','R') AND next_id>0");
        if(!all.isValid()||!valid.isValid()||
           all.toInt()!=3||valid.toInt()!=3)return false;
        // P7-AA: require already-issued IDs below their C/M/R counters.
        // Page integrity does not detect a maliciously rewound sequence.
        QHash<QString,qint64> nextIds;
        QSqlQuery sequences(db);
        if(!sequences.exec("SELECT kind,next_id FROM counters"))return false;
        while(sequences.next()){
            const QString kind=sequences.value(0).toString();
            bool numeric=false;
            const qint64 nextId=sequences.value(1).toLongLong(&numeric);
            if((kind!="C"&&kind!="M"&&kind!="R")||
               !numeric||nextId<1||nextIds.contains(kind))return false;
            nextIds.insert(kind,nextId);
        }
        if(nextIds.size()!=3)return false;
        const auto validIssuedId=[&](const QString &id,const QString &prefix){
            if(!id.startsWith(prefix))return false;
            bool numeric=false;
            const qint64 number=id.mid(1).toLongLong(&numeric);
            return numeric&&number>0&&number<nextIds.value(prefix)&&
                id==prefix+QString("%1").arg(number,3,10,QChar('0'));
        };
        QSqlQuery vehicleIds(db);
        if(!vehicleIds.exec("SELECT id,kind FROM vehicles"))return false;
        while(vehicleIds.next()){
            const QString kind=vehicleIds.value(1).toString();
            const QString prefix=kind=="Car"?"C":kind=="Motorcycle"?"M":"";
            if(prefix.isEmpty()||
               !validIssuedId(vehicleIds.value(0).toString(),prefix))return false;
        }
        QSqlQuery rentalIds(db);
        if(!rentalIds.exec("SELECT id FROM rentals"))return false;
        while(rentalIds.next()){
            if(!validIssuedId(rentalIds.value(0).toString(),"R"))return false;
        }
        QSqlQuery q(db);
        return q.exec("PRAGMA foreign_key_check")&&!q.next()&&
            one("SELECT count(*) FROM vehicles").isValid()&&
            one("SELECT count(*) FROM rentals").isValid();
    }
    bool createNew() {
        if(live||!db.isOpen()||one("PRAGMA user_version").toInt()!=0||
           one("SELECT count(*) FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'").toInt()!=0)return false;
        const QStringList schema={
            countersDdl(),
            "INSERT INTO counters VALUES('C',1),('M',1),('R',1)",
            vehiclesDdl(),
            rentalsDdl(),
            occupancyIndexDdl(),
            "PRAGMA user_version=1"
        };
        if(!begin())return false;
        for(const auto &s:schema)if(!sql(s)){rollback();return false;}
        if(!commit())return false;
        live=integrity();return live;
    }
    static std::optional<qint64> cents(const QString &input) {
        static const QRegularExpression re("^[0-9]+\\.[0-9]{2}$");
        if(!re.match(input).hasMatch())return {};
        bool ok=false;const auto pos=input.indexOf('.');
        auto major=input.left(pos).toLongLong(&ok);
        if(!ok||major<0)return {};
        auto minor=input.mid(pos+1).toInt(&ok);
        if(!ok)return {};
        // P7-AB: reserve only the actual remaining cents in the last
        // representable whole unit. Reject .08+ after 92233720368547758,
        // but do not reject valid .00-.07 within the signed qint64 ceiling.
        const qint64 maxCents=std::numeric_limits<qint64>::max();
        if(major>maxCents/100||
           (major==maxCents/100&&minor>maxCents%100))return {};
        auto value=major*100+minor;
        return value>0?std::optional<qint64>(value):std::nullopt;
    }
    Status vehicle(const QString &kind,const QString &brand,
                   const QString &model,const QString &price,int spec,
                   QString *id=nullptr) {
        if(id)id->clear();
        auto value=cents(price);
        QString prefix=kind=="Car"?"C":kind=="Motorcycle"?"M":"";
        if(!live||prefix.isEmpty()||brand.trimmed().isEmpty()||
           model.trimmed().isEmpty()||brand.contains('|')||model.contains('|')||
           brand.contains('\n')||model.contains('\n')||
           brand.contains('\r')||model.contains('\r')||spec<=0||!value)return Status::Invalid;
        if(!begin())return Status::Failed;
        auto next=issue(prefix);
        if(!next||!run("INSERT INTO vehicles(id,kind,brand,model,spec,rate_cents,state)"
             " VALUES(?,?,?,?,?,?,'ACTIVE')",
             {*next,kind,brand.trimmed(),model.trimmed(),spec,*value})){
            rollback();return Status::Failed;
        }
        if(!commit())return Status::Failed;
        if(id)*id=*next;
        return Status::Ok;
    }
    std::optional<Quote> quote(const QString &id,int days,const QDate &date)const{
        if(!live||days<1||days>365||!date.isValid()||!date.addDays(days).isValid())return {};
        QSqlQuery q(db);
        q.prepare("SELECT id,rate_cents FROM vehicles v WHERE id=? AND state='ACTIVE'"
                  " AND NOT EXISTS(SELECT 1 FROM rentals r WHERE r.vehicle_id=v.id"
                  " AND r.status='ACTIVE')");
        q.addBindValue(id);
        if(!q.exec()||!q.next())return {};
        auto rate=q.value(1).toLongLong();
        if(rate<=0||rate>std::numeric_limits<qint64>::max()/days)return {};
        return Quote{q.value(0).toString(),date,days,rate,rate*days};
    }
    Booking book(const Quote &review,const QDate &today) {
        if(!live||!today.isValid()||review.date!=today||review.days<1||
           review.days>365)return {Status::Invalid,{}};
        if(!begin())return {Status::Failed,{}};
        auto fresh=quote(review.vehicleId,review.days,today);
        if(!fresh){rollback();return {Status::Unavailable,{}};}
        if(fresh->dailyCents!=review.dailyCents||
           fresh->totalCents!=review.totalCents){
            rollback();return {Status::Stale,{}};
        }
        auto id=issue("R");
        if(!id||!run("INSERT INTO rentals(id,vehicle_id,start_date,expected_date,"
            "actual_date,days,daily_cents,total_cents,status)"
            " VALUES(?,?,?,?,NULL,?,?,?,'ACTIVE')",
            {*id,fresh->vehicleId,today.toString(Qt::ISODate),
             today.addDays(review.days).toString(Qt::ISODate),
             review.days,fresh->dailyCents,fresh->totalCents})){
            rollback();return {Status::Failed,{}};
        }
        if(!commit())return {Status::Failed,{}};
        return {Status::Ok,*id};
    }
    Status returnRental(const QString &id,const QDate &actual,const QDate &today) {
        if(!live||!actual.isValid()||!today.isValid()||actual>today)return Status::Invalid;
        if(!begin())return Status::Failed;
        auto start=one("SELECT start_date FROM rentals WHERE id=? AND status='ACTIVE'",{id});
        if(!start.isValid()){rollback();return Status::Unavailable;}
        if(actual<QDate::fromString(start.toString(),Qt::ISODate)){
            rollback();return Status::Invalid;
        }
        if(!run("UPDATE rentals SET status='RETURNED',actual_date=? WHERE id=? AND status='ACTIVE'",
                 {actual.toString(Qt::ISODate),id})){
            rollback();return Status::Failed;
        }
        return commit()?Status::Ok:Status::Failed;
    }
    Status archive(const QString &id) {
        if(!live)return Status::Invalid;
        if(!begin())return Status::Failed;
        if(one("SELECT status FROM rentals WHERE id=?",{id}).toString()!="RETURNED"){
            rollback();return Status::Unavailable;
        }
        if(!run("UPDATE rentals SET status='ARCHIVED' WHERE id=? AND status='RETURNED'",{id})){
            rollback();return Status::Failed;
        }
        return commit()?Status::Ok:Status::Failed;
    }
    Status retire(const QString &id) {
        if(!live)return Status::Invalid;
        if(!begin())return Status::Failed;
        if(one("SELECT state FROM vehicles WHERE id=?",{id}).toString()!="ACTIVE"||
           one("SELECT 1 FROM rentals WHERE vehicle_id=? AND status='ACTIVE'",{id}).isValid()){
            rollback();return Status::Unavailable;
        }
        if(!run("UPDATE vehicles SET state='RETIRED' WHERE id=? AND state='ACTIVE'",{id})){
            rollback();return Status::Failed;
        }
        return commit()?Status::Ok:Status::Failed;
    }
    std::optional<RentalView> rental(const QString &id)const {
        if(!live)return {};
        QSqlQuery q(db);q.prepare("SELECT id,vehicle_id,status,total_cents FROM rentals WHERE id=?");
        q.addBindValue(id);if(!q.exec()||!q.next())return {};
        return RentalView{q.value(0).toString(),q.value(1).toString(),
                          q.value(2).toString(),q.value(3).toLongLong()};
    }
    QString state(const QString &id)const{
        return live?one("SELECT state FROM vehicles WHERE id=?",{id}).toString():QString();
    }
    int count(const QString &which)const{
        if(!live||(which!="rentals"&&which!="vehicles"))return -1;
        return one("SELECT count(*) FROM "+which).toInt();
    }
    qint64 next(const QString &kind)const{
        if(!live)return -1;
        auto v=one("SELECT next_id FROM counters WHERE kind=?",{kind});
        return v.isValid()?v.toLongLong():-1;
    }
    std::optional<QList<VehicleRow>> vehicleRows() const {
        if(!live)return {};
        QSqlQuery q(db);
        if(!q.exec("SELECT id,kind,brand,model,state,spec,rate_cents "
                   "FROM vehicles ORDER BY id COLLATE NOCASE"))return {};
        QList<VehicleRow> rows;
        while(q.next()) {
            VehicleRow v{q.value(0).toString(),q.value(1).toString(),
                q.value(2).toString(),q.value(3).toString(),
                q.value(4).toString(),q.value(5).toInt(),q.value(6).toLongLong()};
            if(v.id.isEmpty() || (v.kind!="Car"&&v.kind!="Motorcycle") ||
               v.brand.trimmed().isEmpty() || v.model.trimmed().isEmpty() ||
               (v.state!="ACTIVE"&&v.state!="RETIRED") ||
               v.spec<=0 || v.rateCents<=0)return {};
            rows.append(v);
        }
        return rows;
    }
    std::optional<QList<RentalRow>> rentalRows() const {
        if(!live)return {};
        QSqlQuery q(db);
        if(!q.exec("SELECT id,vehicle_id,status,start_date,expected_date,actual_date,"
                   "days,daily_cents,total_cents FROM rentals ORDER BY id COLLATE NOCASE"))
            return {};
        QList<RentalRow> rows;
        while(q.next()) {
            RentalRow r;
            r.id=q.value(0).toString();
            r.vehicleId=q.value(1).toString();
            r.status=q.value(2).toString();
            r.start=QDate::fromString(q.value(3).toString(),Qt::ISODate);
            r.expected=QDate::fromString(q.value(4).toString(),Qt::ISODate);
            r.actual=QDate::fromString(q.value(5).toString(),Qt::ISODate);
            r.days=q.value(6).toInt();
            r.dailyCents=q.value(7).toLongLong();
            r.totalCents=q.value(8).toLongLong();
            if(r.id.isEmpty() || r.vehicleId.isEmpty() ||
               (r.status!="ACTIVE"&&r.status!="RETURNED"&&r.status!="ARCHIVED") ||
               !r.start.isValid() || !r.expected.isValid() ||
               r.expected!=r.start.addDays(r.days) ||
               r.days<1 || r.days>365 ||
               (r.status=="ACTIVE"?r.actual.isValid():!r.actual.isValid()) ||
               (r.actual.isValid() && r.actual<r.start) ||
               r.dailyCents<=0 || r.totalCents<=0 ||
               r.dailyCents>std::numeric_limits<qint64>::max()/r.days ||
               r.dailyCents*r.days!=r.totalCents)return {};
            rows.append(r);
        }
        return rows;
    }
    std::optional<FleetRows> fleetRows(
        const std::function<void()> &betweenReadsForTest = {}) const {
        if(!live)return {};
        // BEGIN DEFERRED takes one snapshot on the first SELECT. This is a
        // read-only transaction and never reserves the database writer slot.
        QSqlQuery transaction(db);
        if(!transaction.exec("BEGIN"))return {};
        auto vehicles=vehicleRows();
        if(vehicles && betweenReadsForTest)
            betweenReadsForTest(); // Fixed sandbox regression hook only.
        auto rentals=vehicles ? rentalRows() : std::optional<QList<RentalRow>>{};
        if(!vehicles || !rentals) {
            transaction.exec("ROLLBACK");
            return {};
        }
        // P7-U: structural SQLite integrity does not necessarily guarantee
        // valid business relationships after external fixture tampering.
        // Refuse whole snapshot rather than publishing misleading KPIs.
        QSet<QString> vehicleIds, activeVehicles, occupied;
        for(const auto &v:*vehicles) {
            const QString id=v.id.toUpper();
            if(vehicleIds.contains(id)) {
                transaction.exec("ROLLBACK");
                return {};
            }
            vehicleIds.insert(id);
            if(v.state=="ACTIVE")activeVehicles.insert(id);
        }
        for(const auto &r:*rentals) {
            const QString id=r.vehicleId.toUpper();
            if(!vehicleIds.contains(id) ||
               (r.status=="ACTIVE" &&
                (!activeVehicles.contains(id) || occupied.contains(id)))) {
                transaction.exec("ROLLBACK");
                return {};
            }
            if(r.status=="ACTIVE")occupied.insert(id);
        }
        if(!transaction.exec("COMMIT")) {
            transaction.exec("ROLLBACK");
            return {};
        }
        return FleetRows{std::move(*vehicles),std::move(*rentals)};
    }
    void failNextCommitForTest(){rejectNext=true;}
};
}
