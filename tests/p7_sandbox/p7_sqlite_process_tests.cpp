// P7-F: OFF-by-default fictional SQLite cross-process/crash checks, no app integration.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <QVariant>
#include <QDebug>
#include <cstdio>
static bool demand(bool ok,const char *msg){if(!ok)qCritical()<<"P7-F FAIL:"<<msg;return ok;}
class Db {
    const QString name="p7f-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
public:
    QSqlDatabase conn;
    ~Db(){conn.close();conn=QSqlDatabase();QSqlDatabase::removeDatabase(name);}
    bool exec(const QString &s){QSqlQuery q(conn);return q.exec(s);}
    QVariant val(const QString &s){QSqlQuery q(conn);return q.exec(s)&&q.next()?q.value(0):QVariant{};}
    bool open(const QString &path){
        if(!demand(QSqlDatabase::isDriverAvailable("QSQLITE"),"driver"))return false;
        conn=QSqlDatabase::addDatabase("QSQLITE",name);conn.setDatabaseName(path);
        return demand(conn.open(),"connect")&&exec("PRAGMA busy_timeout=0")&&
          exec("PRAGMA foreign_keys=ON")&&
          exec("PRAGMA synchronous=FULL")&&demand(val("PRAGMA foreign_keys").toInt()==1,"FK enabled");
    }
    bool valid(){
        if(val("PRAGMA user_version").toInt()!=1||
           val("PRAGMA integrity_check").toString()!="ok")return false;
        QSqlQuery q(conn);return q.exec("PRAGMA foreign_key_check")&&!q.next();
    }
    bool seed(){
        if(!exec("PRAGMA journal_mode=DELETE")||!exec("BEGIN IMMEDIATE"))return false;
        const QStringList a={
          "CREATE TABLE issued(kind TEXT PRIMARY KEY,next_id INTEGER NOT NULL CHECK(next_id>0))",
          "INSERT INTO issued VALUES('C',2),('R',1)",
          "CREATE TABLE vehicles(id TEXT PRIMARY KEY COLLATE NOCASE,rate_cents INTEGER NOT NULL CHECK(rate_cents>0))",
          "CREATE TABLE rentals(id TEXT PRIMARY KEY COLLATE NOCASE,vehicle_id TEXT NOT NULL REFERENCES vehicles(id) ON DELETE RESTRICT,status TEXT NOT NULL CHECK(status IN ('ACTIVE','RETURNED','ARCHIVED')),total_cents INTEGER NOT NULL CHECK(total_cents>0))",
          "CREATE UNIQUE INDEX one_active ON rentals(vehicle_id COLLATE NOCASE) WHERE status='ACTIVE'",
          "INSERT INTO vehicles VALUES('C001',45000)",
          "PRAGMA user_version=1"
        };
        for(const QString &s:a)if(!exec(s)){exec("ROLLBACK");return false;}
        return exec("COMMIT")&&valid();
    }
    bool empty(){return valid()&&val("SELECT count(*) FROM rentals").toInt()==0&&
        val("SELECT next_id FROM issued WHERE kind='R'").toInt()==1;}
    bool booked(){return valid()&&val("SELECT count(*) FROM rentals WHERE status='ACTIVE'").toInt()==1&&
        val("SELECT total_cents FROM rentals WHERE id='R001'").toLongLong()==135000&&
        val("SELECT next_id FROM issued WHERE kind='R'").toInt()==2;}
};
static bool authorized(const QString &root,const QString &token){
    const QString clean=QDir::cleanPath(root),tmp=QDir::cleanPath(QDir::tempPath());
    if(!clean.startsWith(tmp+"/")||!QFileInfo(clean).isDir()||
       !QFileInfo(clean).fileName().startsWith("rental-p7f-")||token.size()!=36)return false;
    QFile file(QDir(clean).filePath(".fictional-only"));
    return file.open(QIODevice::ReadOnly)&&QString::fromUtf8(file.readAll())==token;
}
static int child(QString action,const QString &root,const QString &token){
    if(!authorized(root,token))return 20;
    Db x;if(!x.open(QDir(root).filePath("synthetic.sqlite")))return 21;
    if(action=="inspect")return x.booked()?0:22;
    if(action!="book"&&action!="pending")return 23;
    if(!x.exec("BEGIN IMMEDIATE"))return 10;
    if(!x.valid()){x.exec("ROLLBACK");return 21;}
    if(x.val("SELECT count(*) FROM rentals WHERE status='ACTIVE'").toInt()!=0){
       x.exec("ROLLBACK");return 11;
    }
    if(x.val("SELECT next_id FROM issued WHERE kind='R'").toInt()!=1||
       !x.exec("UPDATE issued SET next_id=2 WHERE kind='R'")||
       !x.exec("INSERT INTO rentals VALUES('R001','C001','ACTIVE',135000)")){
       x.exec("ROLLBACK");return 24;
    }
    if(action=="pending"){
        std::puts("P7F_PENDING");std::fflush(stdout);
        for(;;)QThread::sleep(1); // Parent forcibly terminates before COMMIT.
    }
    if(!x.exec("COMMIT")){x.exec("ROLLBACK");return 25;}
    return 0;
}
class Fixture{
public:
    QTemporaryDir dir{QDir::tempPath()+"/rental-p7f-XXXXXX"};
    QString token=QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString file()const{return dir.filePath("synthetic.sqlite");}
    bool setup(){
        if(!dir.isValid())return false;
        QFile m(dir.filePath(".fictional-only"));
        if(!m.open(QIODevice::WriteOnly)||m.write(token.toUtf8())!=token.size())return false;
        m.close();
        Db d;return d.open(file())&&d.seed()&&d.empty();
    }
    void start(QProcess &p,const QString &mode)const{
        p.start(QCoreApplication::applicationFilePath(),{"--child-"+mode,dir.path(),token});
    }
    int run(const QString &mode)const{
        QProcess p;start(p,mode);
        if(!p.waitForStarted(10000)||!p.waitForFinished(15000)){
            p.kill();p.waitForFinished(10000);return 30;
        }
        return p.exitStatus()==QProcess::NormalExit?p.exitCode():31;
    }
    bool crash()const{
        QProcess p;start(p,"pending");
        if(!p.waitForStarted(10000))return false;
        if(!p.waitForReadyRead(10000)||!p.readAllStandardOutput().contains("P7F_PENDING")){
            p.kill();p.waitForFinished(10000);return false;
        }
        p.kill();
        return p.waitForFinished(10000)&&p.exitStatus()==QProcess::CrashExit;
    }
};
static bool recovery(){
    Fixture f;if(!f.setup()||!demand(f.crash(),"terminate uncommitted child"))return false;
    {Db d;if(!d.open(f.file())||!demand(d.empty(),"no phantom rental/ID after crash"))return false;}
    if(!demand(f.run("book")==0,"new child commit after crash"))return false;
    Db d;return demand(d.open(f.file())&&d.booked(),"recover correct record");
}
static bool contention(){
    Fixture f;if(!f.setup())return false;
    Db blocker;if(!blocker.open(f.file())||!blocker.exec("BEGIN IMMEDIATE"))return false;
    if(!demand(f.run("book")==10,"writer busy refusal")||
       !demand(blocker.exec("ROLLBACK"),"unlock")||
       !demand(blocker.empty(),"busy writer did not consume ID"))return false;
    return demand(f.run("book")==0,"post-unlock retry")&&
       demand(blocker.booked(),"one authoritative booking");
}
static bool restart(){
    Fixture f;if(!f.setup())return false;
    if(!demand(f.run("book")==0,"separate process confirmed")||
       !demand(f.run("inspect")==0,"separate process reloaded"))return false;
    Db d;return demand(d.open(f.file())&&d.booked(),"third connection confirms");
}
static bool duplicate(){
    Fixture f;if(!f.setup())return false;
    if(!demand(f.run("book")==0,"first booking")||
       !demand(f.run("book")==11,"repeat booking refused")||
       !demand(f.run("inspect")==0,"one active rental persists"))return false;
    Db d;return demand(d.open(f.file())&&d.booked(),"no consumed repeat ID");
}
static bool futureSchema(){
    Fixture f;if(!f.setup())return false;
    {Db d;if(!d.open(f.file())||!d.exec("PRAGMA user_version=2"))return false;}
    if(!demand(f.run("book")==21,"future schema child refused"))return false;
    Db d;return demand(d.open(f.file())&&d.val("PRAGMA user_version").toInt()==2&&
         d.val("SELECT count(*) FROM rentals").toInt()==0&&
         d.val("SELECT next_id FROM issued WHERE kind='R'").toInt()==1,
         "unsupported schema unchanged");
}
int main(int argc,char**argv){
    QCoreApplication app(argc,argv);
    if(argc==4){
        QString action=QString::fromLatin1(argv[1]);
        if(!action.startsWith("--child-"))return 2;
        return child(action.mid(8),QString::fromLocal8Bit(argv[2]),QString::fromLatin1(argv[3]));
    }
    if(argc!=2)return 2;
    const QString test=QString::fromLatin1(argv[1]);
    bool ok=false;
    if(test=="recovery")ok=recovery();
    else if(test=="contention")ok=contention();
    else if(test=="restart")ok=restart();
    else if(test=="duplicate")ok=duplicate();
    else if(test=="future_schema")ok=futureSchema();
    else return 3;
    if(ok)qInfo()<<"P7-F generated-only cross-process PASS:"<<test;
    return ok?0:1;
}
