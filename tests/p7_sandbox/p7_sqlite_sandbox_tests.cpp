// P7-B: opt-in QtSql proof. All data invented here in disposable OS temp.
// No RentalSystem, QML, personal contacts, import/export or production path.
#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>
#include <QUuid>
#include <QVariant>
#include <limits>
#include <optional>
#include <QDebug>
static bool require(bool ok,const char *why) {
    if(!ok) qCritical()<<"P7-B FAIL:"<<why;
    return ok;
}
class Sandbox {
    QString name=QStringLiteral("p7-")+QUuid::createUuid().toString(QUuid::WithoutBraces);
public:
    QSqlDatabase db;
    ~Sandbox(){
        db.close();db=QSqlDatabase();
        if(QSqlDatabase::contains(name)) QSqlDatabase::removeDatabase(name);
    }
    bool open(const QString &path) {
        if(!require(QSqlDatabase::isDriverAvailable("QSQLITE"),"QSQLITE driver unavailable"))
            return false;
        db=QSqlDatabase::addDatabase("QSQLITE",name); db.setDatabaseName(path);
        if(!require(db.open(),"isolated QSQLITE open"))return false;
        if(!exec("PRAGMA foreign_keys=ON")||!exec("PRAGMA busy_timeout=0")||
           !exec("PRAGMA journal_mode=DELETE")||!exec("PRAGMA synchronous=FULL"))
           return false;
        return require(value("PRAGMA foreign_keys").toInt()==1,"foreign keys enabled");
    }
    bool exec(const QString &sql) {
        QSqlQuery q(db);
        if(q.exec(sql))return true;
        qCritical()<<"P7-B statement rejected:"<<sql.left(42)
                   <<"SQL type:"<<q.lastError().type();
        return false;
    }
    bool run(const QString &sql,const QVariantList &v={}) {
        QSqlQuery q(db);
        if(!q.prepare(sql))return false;
        for(const auto &x:v)q.addBindValue(x);
        return q.exec();
    }
    QVariant value(const QString &sql,const QVariantList &v={}) {
        QSqlQuery q(db);
        if(!q.prepare(sql))return {};
        for(const auto &x:v)q.addBindValue(x);
        return q.exec()&&q.next()?q.value(0):QVariant{};
    }
    bool init() {
        if(!db.transaction())return false;
        const QStringList statements={
            "CREATE TABLE issued(kind TEXT PRIMARY KEY CHECK(kind IN ('C','M','R')),"
            "next_id INTEGER NOT NULL CHECK(next_id>=1))",
            "INSERT INTO issued VALUES('C',1),('M',1),('R',1)",
            "CREATE TABLE vehicles(id TEXT PRIMARY KEY COLLATE NOCASE,"
            "kind TEXT NOT NULL CHECK(kind IN ('Car','Motorcycle')),"
            "rate_cents INTEGER NOT NULL CHECK(rate_cents>0),"
            "state TEXT NOT NULL CHECK(state IN ('ACTIVE','RETIRED')))",
            "CREATE TABLE rentals(id TEXT PRIMARY KEY COLLATE NOCASE,"
            "vehicle_id TEXT NOT NULL REFERENCES vehicles(id) ON DELETE RESTRICT,"
            "days INTEGER NOT NULL CHECK(days BETWEEN 1 AND 365),"
            "rate_cents INTEGER NOT NULL CHECK(rate_cents>0),"
            "total_cents INTEGER NOT NULL CHECK(total_cents>0),"
            "actual_date TEXT,"
            "status TEXT NOT NULL CHECK(status IN ('ACTIVE','RETURNED','ARCHIVED')),"
            "CHECK((status='ACTIVE' AND actual_date IS NULL) OR "
            "(status IN ('RETURNED','ARCHIVED') AND actual_date IS NOT NULL)))",
            "CREATE UNIQUE INDEX one_active ON rentals(vehicle_id COLLATE NOCASE)"
            " WHERE status='ACTIVE'",
            "PRAGMA user_version=1"
        };
        for(const auto &sql:statements)if(!exec(sql)){db.rollback();return false;}
        return db.commit();
    }
    bool valid() {
        return value("PRAGMA user_version").toInt()==1 &&
               value("PRAGMA integrity_check").toString()=="ok" &&
               value("PRAGMA foreign_keys").toInt()==1 &&
               !value("SELECT 1 FROM pragma_foreign_key_check LIMIT 1").isValid();
    }
    QString issue(const QString &kind) {
        if(kind!="C"&&kind!="M"&&kind!="R")return {};
        QVariant n=value("SELECT next_id FROM issued WHERE kind=?",{kind});
        if(!n.isValid()||n.toLongLong()<1||
           n.toLongLong()==std::numeric_limits<qint64>::max())return {};
        if(!run("UPDATE issued SET next_id=next_id+1 WHERE kind=?",{kind}))
            return {};
        return QStringLiteral("%1%2").arg(kind).arg(n.toLongLong(),3,10,QChar('0'));
    }
    bool vehicle(const QString &id,qint64 cents) {
        return run("INSERT INTO vehicles VALUES(?,'Car',?,'ACTIVE')",{id,cents});
    }
    bool rental(const QString &id,const QString &vehicle,qint64 cents) {
        if(cents<=0||cents>std::numeric_limits<qint64>::max()/2)return false;
        return run("INSERT INTO rentals(id,vehicle_id,days,rate_cents,total_cents,status)"
                   " VALUES(?,?,2,?,?,'ACTIVE')",{id,vehicle,cents,cents*2});
    }
};
static std::optional<qint64> cents(const QString &s) {
    static const QRegularExpression regex(QStringLiteral("^[0-9]+\\.[0-9]{2}$"));
    if(!regex.match(s).hasMatch())return std::nullopt;
    bool ok=false; const qsizetype dot=s.indexOf('.');
    const qint64 whole=s.left(dot).toLongLong(&ok);
    if(!ok||whole<0||whole>(std::numeric_limits<qint64>::max()-99)/100)
        return std::nullopt;
    const qint64 result=whole*100+s.mid(dot+1).toInt(&ok);
    if(!ok||result<=0)return std::nullopt;
    return result;
}
static std::optional<qint64> total(qint64 c,int days){
    if(c<=0||days<1||days>365||c>std::numeric_limits<qint64>::max()/days)
        return std::nullopt;
    return c*days;
}
static bool money(){
    return require(cents("450.00")==45000,"45000 cents") &&
      require(cents("0.01")==1,"smallest cent") &&
      require(!cents("0.00"),"zero rejected") &&
      require(!cents("-2.00")&&!cents("1.001")&&!cents("1e3")&&
              !cents("1,00"),"strict two-decimal format") &&
      require(!cents("92233720368547758.08"),"int64 overflow") &&
      require(total(45000,3)==135000,"exact cents multiplication") &&
      require(!total(std::numeric_limits<qint64>::max(),2) &&
              !total(42000,366),"duration and overflow rejected");
}
static bool constraints(){
    QTemporaryDir t; if(!require(t.isValid(),"temp directory"))return false;
    Sandbox s; if(!s.open(t.filePath("constraints.db"))||!s.init()||!s.valid())return false;
    return require(!s.rental("R001","C404",45000),"FK orphan refused") &&
      require(s.vehicle("C001",45000),"first vehicle") &&
      require(!s.vehicle("c001",45000),"case-fold duplicate refused") &&
      require(!s.vehicle("C002",0),"invalid money refused") &&
      require(s.rental("R001","C001",45000),"first rental") &&
      require(!s.rental("R002","c001",45000),"duplicate active vehicle refused") &&
      require(!s.rental("r001","C001",45000),"case-fold rental ID refused") &&
      require(!s.run("DELETE FROM vehicles WHERE id='C001'"),"FK historical delete refused") &&
      require(!s.run("UPDATE rentals SET status='ARCHIVED' WHERE id='R001'"),
              "cannot archive active without actual return") && s.valid();
}
static bool rollback(){
    QTemporaryDir t; if(!t.isValid())return false;
    Sandbox s;if(!s.open(t.filePath("rollback.db"))||!s.init())return false;
    if(!s.db.transaction())return false;
    QString id=s.issue("C");
    if(!require(id=="C001"&&s.vehicle(id,42000),"first uncommitted create")||
       !require(s.db.rollback(),"explicit rollback"))return false;
    if(!require(s.value("SELECT count(*) FROM vehicles").toInt()==0 &&
                s.value("SELECT next_id FROM issued WHERE kind='C'").toInt()==1,
                "rollback restored ID and Fleet"))return false;
    if(!s.db.transaction())return false;
    id=s.issue("C");
    if(!require(id=="C001"&&s.vehicle(id,42000),"new committed C001")||
       !require(s.db.commit(),"commit succeeded"))return false;
    if(!s.db.transaction())return false;
    QString rid=s.issue("R");
    if(!require(rid=="R001"&&!s.rental(rid,"M404",42000),"FK failure occurs")||
       !require(s.db.rollback(),"failure rollback"))return false;
    return require(s.value("SELECT next_id FROM issued WHERE kind='R'").toInt()==1 &&
                   s.value("SELECT count(*) FROM rentals").toInt()==0,
                   "failed booking had no receipt or consumed ID") && s.valid();
}
static bool restart(){
    QTemporaryDir t; if(!t.isValid())return false;
    QString path=t.filePath("restart.db");
    {
      Sandbox s;if(!s.open(path)||!s.init()||!s.db.transaction())return false;
      QString v=s.issue("C"),rid=s.issue("R");
      if(!require(v=="C001"&&rid=="R001"&&s.vehicle(v,42000)&&
                  s.rental(rid,v,42000),"booking in one transaction")||
         !require(s.db.commit(),"booked commit"))return false;
    }
    {
      Sandbox s;if(!s.open(path)||!s.valid())return false;
      if(!require(s.value("SELECT count(*) FROM rentals WHERE status='ACTIVE'").toInt()==1 &&
         s.value("SELECT total_cents FROM rentals WHERE id='R001'").toLongLong()==84000,
         "restart rehydrated exact booking"))return false;
      if(!s.db.transaction())return false;
      if(!require(s.run("UPDATE rentals SET actual_date='2026-11-02',"
                        "status='RETURNED' WHERE id='R001'"),"return")||
         !require(s.run("UPDATE rentals SET status='ARCHIVED' WHERE id='R001'"),"archive")||
         !require(s.run("UPDATE vehicles SET state='RETIRED' WHERE id='C001'"),"retire")||
         !require(s.db.commit(),"lifecycle transaction"))return false;
      if(!require(!s.run("DELETE FROM vehicles WHERE id='C001'"),
                  "history blocks retired vehicle deletion"))return false;
      if(!s.db.transaction())return false;
      QString next=s.issue("C");
      if(!require(next=="C002"&&s.vehicle(next,30000),"never reuse C001")||
         !require(s.db.commit(),"new identity commit"))return false;
    }
    {
      Sandbox s;if(!s.open(path)||!s.valid())return false;
      return require(s.value("SELECT state FROM vehicles WHERE id='C001'")=="RETIRED" &&
            s.value("SELECT status FROM rentals WHERE id='R001'")=="ARCHIVED" &&
            s.value("SELECT count(*) FROM vehicles").toInt()==2 &&
            s.value("SELECT next_id FROM issued WHERE kind='C'").toInt()==3,
            "independent third connection verifies state and counter");
    }
}
static bool corrupt(){
    QTemporaryDir t; if(!t.isValid())return false;
    {
      Sandbox s;if(!s.open(t.filePath("future.db"))||!s.init()||
        !s.exec("PRAGMA user_version=2")||
        !require(!s.valid(),"future schema rejected"))return false;
    }
    QFile file(t.filePath("broken.db"));
    if(!file.open(QIODevice::WriteOnly) ||
       file.write("not a sqlite database")!=21)return false;
    file.close();
    Sandbox s;
    if(!s.open(file.fileName()))return true; // file was rejected by driver
    return require(!s.valid(),"corrupted SQLite rejected before use");
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if(argc!=2)return 2; // no arbitrary paths or imported input
    const QString mode=QString::fromLatin1(argv[1]);
    bool ok=false;
    if(mode=="money")ok=money();
    else if(mode=="constraints")ok=constraints();
    else if(mode=="rollback")ok=rollback();
    else if(mode=="restart")ok=restart();
    else if(mode=="corrupt")ok=corrupt();
    else return 3;
    if(ok)qInfo()<<"P7-B fictional-only QSQLITE PASS"<<mode;
    return ok?0:1;
}
