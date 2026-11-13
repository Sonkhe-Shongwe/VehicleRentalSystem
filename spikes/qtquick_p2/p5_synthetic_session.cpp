#include "p5_synthetic_session.h"
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <cmath>
#include <memory>

namespace {
void error(QString *out,const QString &message) { if(out) *out=message; }
// The persistent fictional demo has a fixed writable ledger. QSaveFile protects
// each individual write, but two running app processes could each load an old
// snapshot and overwrite the other's bookings. Keep one OS-backed lock for the
// lifetime of this process. The lock sits BESIDE the strictly inventoried data
// directory so it never weakens the fictional-data file allowlist.
std::unique_ptr<QLockFile> &heldSessionLock() {
    static std::unique_ptr<QLockFile> lock;
    return lock;
}
bool lockSession(QString *reason) {
    if(heldSessionLock()) return true;
    const QFileInfo target(P5SyntheticSession::location());
    const QFileInfo parent(target.absolutePath());
    if(parent.exists() && (!parent.isDir() || parent.isSymLink())) {
        error(reason,QStringLiteral("Untrusted application-data parent"));
        return false;
    }
    if(!QDir().mkpath(target.absolutePath())) {
        error(reason,QStringLiteral("Cannot access fictional session parent"));
        return false;
    }
    auto lock=std::make_unique<QLockFile>(target.absoluteFilePath()+QStringLiteral(".lock"));
    // An operator may keep the app open longer than Qt's default stale age.
    // A dead process may still be detected by QLockFile's PID/host checks.
    lock->setStaleLockTime(0);
    if(!lock->tryLock(0)) {
        error(reason,QStringLiteral("Fictional session already open in another process"));
        return false;
    }
    heldSessionLock()=std::move(lock);
    return true;
}
bool readFile(const QString &path,QByteArray *result) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))return false;
    *result=file.readAll();
    return file.error()==QFile::NoError;
}
bool atomicFile(const QString &path,const QByteArray &value) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) &&
           file.write(value)==value.size() && file.commit();
}
bool ordinaryFile(const QString &path) {
    const QFileInfo info(path);
    return info.exists() && info.isFile() && !info.isSymLink();
}
bool correctEntries(const QDir &dir) {
    const QStringList entries=dir.entryList(QDir::AllEntries | QDir::Hidden |
                                            QDir::NoDotAndDotDot,QDir::Name);
    return entries==QStringList({"rentals.txt","session.marker","vehicles.txt"});
}
bool syntheticLedger(const QByteArray &bytes,const QByteArray &seed) {
    if(bytes.size()>65536 || bytes.isEmpty())return false;
    const QString content=QString::fromUtf8(bytes);
    if(content.toUtf8()!=bytes)return false;
    // Git checkout can materialize the exact pinned fictional TXT as CRLF on
    // Windows and LF on Linux. Normalize line separators for validation
    // ONLY, never rewrite or alter the original verified bytes.
    QString normalized=content;
    normalized.replace(QStringLiteral("\r\n"),QStringLiteral("\n"));
    QString seedNormalized=QString::fromUtf8(seed);
    seedNormalized.replace(QStringLiteral("\r\n"),QStringLiteral("\n"));
    const QStringList records=normalized.split('\n',Qt::SkipEmptyParts);
    const QStringList original=seedNormalized.split('\n',Qt::SkipEmptyParts);
    if(records.size()<original.size() || records.size()>40 ||
       original.size()!=2)return false;
    const QRegularExpression id(QStringLiteral("^R[0-9]{3,8}$"));
    const QStringList fleet={"C001","C002","C003","M001","M002"};
    // The domain loader skips a second active rental for the same vehicle.
    // Reject the entire persisted session instead: a silent skip would leave
    // the ledger and displayed Fleet/active-rental counts inconsistent.
    QSet<QString> activeVehicleIds;
    for(int i=0;i<records.size();++i) {
        const QStringList f=records.at(i).split('|',Qt::KeepEmptyParts);
        if(f.size()!=13 || f.at(0)!="Rental" ||
           !id.match(f.at(1)).hasMatch() || !fleet.contains(f.at(4)) ||
           f.at(3)!="0000000000") return false;
        if(i<original.size()) {
            const QStringList expected=original.at(i).split('|',Qt::KeepEmptyParts);
            // Seed details cannot be replaced by customer data. Only lifecycle
            // return/archive flags or actual return date may legitimately change.
            for(const int k:{0,1,2,3,4,5,6,8,9,10})
                if(f.at(k)!=expected.at(k))return false;
        } else if(f.at(2)!="Fictional Demo Session") {
            return false;
        }
        if(f.at(11)!="0" && f.at(11)!="1")return false;
        if(f.at(12)!="0" && f.at(12)!="1")return false;
        const bool returned=f.at(11)=="1";
        if(f.at(12)=="1" && !returned)return false;
        if(!returned && activeVehicleIds.contains(f.at(4)))
            return false;
        if(!returned) activeVehicleIds.insert(f.at(4));
        const QDate start=QDate::fromString(f.at(5),Qt::ISODate);
        const QDate due=QDate::fromString(f.at(6),Qt::ISODate);
        const QDate actual=QDate::fromString(f.at(7),Qt::ISODate);
        bool daysOk=false,rateOk=false,totalOk=false;
        const int days=f.at(8).toInt(&daysOk);
        const double rate=f.at(9).toDouble(&rateOk);
        const double total=f.at(10).toDouble(&totalOk);
        if(!daysOk || days<1 || days>365 || !rateOk || !totalOk ||
           !std::isfinite(rate) || !std::isfinite(total) || rate<=0 ||
           std::abs(rate*days-total)>0.01 ||
           !start.isValid() || due!=start.addDays(days) ||
           (returned && (!actual.isValid() || actual<start)) ||
           (!returned && !f.at(7).isEmpty()))return false;
        for(int j=0;j<i;++j)
            if(records.at(j).section('|',1,1)==f.at(1))return false;
    }
    return true;
}
} // namespace

QString P5SyntheticSession::location() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("rental-studio-p5-fictional-only-v1"));
}

bool P5SyntheticSession::validate(const QByteArray &vehiclesSeed,
                                  const QByteArray &rentalsSeed,
                                  const QByteArray &marker,QString *reason) {
    const QFileInfo info(location());
    if(!info.exists() || !info.isDir() || info.isSymLink()) {
        error(reason,QStringLiteral("Untrusted session directory"));
        return false;
    }
    const QDir dir(location());
    QByteArray m,v,r;
    if(!correctEntries(dir) ||
       !ordinaryFile(dir.filePath("session.marker")) ||
       !ordinaryFile(dir.filePath("vehicles.txt")) ||
       !ordinaryFile(dir.filePath("rentals.txt")) ||
       !readFile(dir.filePath("session.marker"),&m) ||
       !readFile(dir.filePath("vehicles.txt"),&v) ||
       !readFile(dir.filePath("rentals.txt"),&r) ||
       m!=marker || v!=vehiclesSeed || !syntheticLedger(r,rentalsSeed)) {
        error(reason,QStringLiteral("Unexpected files or non-fictional demo data; refusing to load"));
        return false;
    }
    return true;
}

bool P5SyntheticSession::open(const QByteArray &vehiclesSeed,
                              const QByteArray &rentalsSeed,
                              const QByteArray &marker,QString *reason) {
    if(!lockSession(reason))return false;
    if(QFileInfo::exists(location()))
        return validate(vehiclesSeed,rentalsSeed,marker,reason);
    const QFileInfo parent(QFileInfo(location()).absolutePath());
    if(parent.exists() && (!parent.isDir() || parent.isSymLink())) {
        error(reason,QStringLiteral("Untrusted application-data parent"));
        return false;
    }
    if(!QDir().mkpath(location())) {
        error(reason,QStringLiteral("Cannot create fictional session"));
        return false;
    }
    const QDir dir(location());
    // Write marker LAST. Interrupted initialization is rejected next launch.
    if(!atomicFile(dir.filePath("vehicles.txt"),vehiclesSeed) ||
       !atomicFile(dir.filePath("rentals.txt"),rentalsSeed) ||
       !atomicFile(dir.filePath("session.marker"),marker)) {
        error(reason,QStringLiteral("Could not initialize fictional session"));
        return false;
    }
    return validate(vehiclesSeed,rentalsSeed,marker,reason);
}

bool P5SyntheticSession::cleanForTest(const QByteArray &marker,QString *reason) {
    if(!lockSession(reason))return false;
    const QDir dir(location());
    if(!dir.exists())return true;
    QByteArray actual;
    if(!correctEntries(dir) ||
       !ordinaryFile(dir.filePath("session.marker")) ||
       !readFile(dir.filePath("session.marker"),&actual) ||
       actual!=marker) {
        error(reason,QStringLiteral("Refusing to clean unidentified test data"));
        return false;
    }
    for(const QString &file:{"rentals.txt","vehicles.txt","session.marker"})
        if(!QFile::remove(dir.filePath(file)))return false;
    return QDir().rmdir(location());
}
