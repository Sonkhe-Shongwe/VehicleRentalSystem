// P7-E: independently compiled, OFF-by-default, synthetic-only backup/recovery proof.
// No RentalSystem, QML, arbitrary file arguments, customer data or runtime integration.
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QVariant>
#include <QDebug>

static bool check(bool ok, const char *message) {
    if (!ok) qCritical() << "P7-E synthetic-only FAIL:" << message;
    return ok;
}

class Database {
    const QString connectionName =
        QStringLiteral("p7e-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
public:
    QSqlDatabase db;
    ~Database() {
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }
    bool open(const QString &path, bool wal = true) {
        if (!check(QSqlDatabase::isDriverAvailable("QSQLITE"), "SQLite driver")) return false;
        db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(path);
        if (!check(db.open(), "SQLite open")) return false;
        return exec("PRAGMA busy_timeout=0") &&
               exec("PRAGMA foreign_keys=ON") &&
               exec(wal ? "PRAGMA journal_mode=WAL" : "PRAGMA journal_mode=DELETE") &&
               exec("PRAGMA synchronous=FULL") &&
               check(value("PRAGMA foreign_keys").toInt() == 1, "FK enabled");
    }
    bool exec(const QString &sql) {
        QSqlQuery q(db);
        return q.exec(sql);
    }
    bool run(const QString &sql, const QVariantList &params = {}) {
        QSqlQuery q(db);
        if (!q.prepare(sql)) return false;
        for (const QVariant &v : params) q.addBindValue(v);
        return q.exec();
    }
    QVariant value(const QString &sql) {
        QSqlQuery q(db);
        return q.exec(sql) && q.next() ? q.value(0) : QVariant();
    }
    bool integrity() {
        if (!db.isOpen() || value("PRAGMA user_version").toInt() != 1 ||
            value("PRAGMA integrity_check").toString() != QStringLiteral("ok") ||
            value("PRAGMA foreign_keys").toInt() != 1) return false;
        // P7-Z: page integrity and valid existing records do not prove that
        // the fixed v1 active-vehicle uniqueness rule is still enforced.
        // Refuse backups/restores with a missing or weakened partial index.
        const QString activeIndex = value(
            "SELECT sql FROM sqlite_master WHERE type='index' AND name='active_vehicle'"
        ).toString();
        if (activeIndex != QStringLiteral(
                "CREATE UNIQUE INDEX active_vehicle ON rentals(vehicle_id COLLATE NOCASE)"
                " WHERE status='ACTIVE'")) return false;
        QSqlQuery fk(db);
        return fk.exec("PRAGMA foreign_key_check") && !fk.next();
    }
    bool seed() {
        if (!db.transaction()) return false;
        const QStringList statements = {
            "CREATE TABLE issued(kind TEXT PRIMARY KEY, next_id INTEGER NOT NULL CHECK(next_id > 0))",
            "INSERT INTO issued VALUES('C',3),('R',3)",
            "CREATE TABLE vehicles(id TEXT PRIMARY KEY COLLATE NOCASE, state TEXT NOT NULL "
                "CHECK(state IN ('ACTIVE','RETIRED')), rate_cents INTEGER NOT NULL CHECK(rate_cents > 0))",
            "CREATE TABLE rentals(id TEXT PRIMARY KEY COLLATE NOCASE, vehicle_id TEXT NOT NULL "
                "REFERENCES vehicles(id) ON DELETE RESTRICT, status TEXT NOT NULL "
                "CHECK(status IN ('ACTIVE','RETURNED','ARCHIVED')), total_cents INTEGER NOT NULL "
                "CHECK(total_cents > 0))",
            "CREATE UNIQUE INDEX active_vehicle ON rentals(vehicle_id COLLATE NOCASE) WHERE status='ACTIVE'",
            "INSERT INTO vehicles VALUES('C001','ACTIVE',45000),('C002','RETIRED',42000)",
            "INSERT INTO rentals VALUES('R001','C001','ACTIVE',135000),"
                "('R002','C002','ARCHIVED',84000)",
            "PRAGMA user_version=1"
        };
        for (const QString &sql : statements) {
            if (!exec(sql)) { db.rollback(); return false; }
        }
        return db.commit() && integrity();
    }
    bool baseline() {
        return integrity() &&
               value("SELECT count(*) FROM vehicles").toInt() == 2 &&
               value("SELECT count(*) FROM rentals").toInt() == 2 &&
               value("SELECT sum(total_cents) FROM rentals").toLongLong() == 219000 &&
               value("SELECT count(*) FROM rentals WHERE status='ACTIVE'").toInt() == 1 &&
               value("SELECT state FROM vehicles WHERE id='C002'").toString() == "RETIRED" &&
               value("SELECT next_id FROM issued WHERE kind='C'").toInt() == 3 &&
               value("SELECT next_id FROM issued WHERE kind='R'").toInt() == 3;
    }
};

// VACUUM INTO makes a consistent snapshot, not a live DB file copy.
// The candidate is validated in private staging and renamed only if the final
// path is absent. It has no API capable of accepting arbitrary user paths.
static bool backup(Database &source, const QString &destination) {
    const QString staging = destination + QStringLiteral(".stage");
    if (QFileInfo::exists(destination) || QFileInfo::exists(staging) ||
        !source.integrity()) return false;
    QSqlQuery vacuum(source.db);
    if (!vacuum.prepare("VACUUM INTO ?")) return false;
    vacuum.addBindValue(staging);
    if (!vacuum.exec()) {
        vacuum.finish();
        QFile::remove(staging);
        return false;
    }
    vacuum.finish();
    bool validStage = false;
    {
        Database staged;
        validStage = staged.open(staging) && staged.integrity();
    }
    if (!validStage) {
        QFile::remove(staging);
        return false;
    }
    // QFile::rename refuses an existing destination. Never remove it.
    QFile file(staging);
    if (!file.rename(destination)) {
        QFile::remove(staging);
        return false;
    }
    return QFileInfo(destination).size() > 0;
}
// Generated-fictional-only restore proof. Restore verified snapshots to a
// new independent file, never over an existing DB or an in-use application.
static bool restoreVerified(const QString &backupPath,const QString &destination) {
    const QString staging=destination+QStringLiteral(".restore-stage");
    const QFileInfo source(backupPath),target(destination),stage(staging);
    if(!source.isFile()||source.isSymLink()||source.size()<=0||
       target.exists()||target.isSymLink()||stage.exists()||stage.isSymLink()||
       QFileInfo::exists(backupPath+"-wal")||
       QFileInfo::exists(backupPath+"-shm")||
       QFileInfo::exists(backupPath+"-journal"))
        return false;
    // QFile::copy never overwrites an existing stage. We only clean up a
    // staging file we actually created, never a pre-existing file.
    if(!QFile::copy(backupPath,staging))return false;
    bool verified=false;
    {
        Database candidate;
        // Validation occurs on the private staging copy, not the source.
        verified=candidate.open(staging,false)&&candidate.baseline();
    }
    if(!verified) {
        QFile::remove(staging);
        return false;
    }
    // QFile::rename refuses an occupied final path, including a racing writer.
    QFile file(staging);
    if(!file.rename(destination)) {
        QFile::remove(staging);
        return false;
    }
    return QFileInfo(destination).size()>0;
}

static QByteArray sha256(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    return h.addData(&file) ? h.result() : QByteArray();
}
static bool makeSeed(const QString &path) {
    Database db;
    return db.open(path) && db.seed() && db.baseline();
}
static bool restored(const QString &path) {
    Database db;
    return db.open(path) && db.baseline();
}

static bool snapshot() {
    QTemporaryDir dir;
    if (!check(dir.isValid(), "temporary root")) return false;
    const QString src = dir.filePath("source.sqlite");
    const QString dst = dir.filePath("restored.sqlite");
    if (!makeSeed(src)) return false;
    {
        Database db;
        if (!db.open(src) || !backup(db, dst)) return false;
        if (!check(sha256(dst).size() == 32, "snapshot SHA256")) return false;
        if (!check(db.run("INSERT INTO vehicles VALUES('C003','ACTIVE',10000)"),
                   "later source-only change")) return false;
    }
    Database copy;
    return check(copy.open(dst) && copy.baseline(), "restored snapshot excludes later writes") &&
           check(copy.run("INSERT INTO vehicles VALUES('C003','ACTIVE',20000)"),
                 "restore is independently writable") &&
           check(copy.value("SELECT count(*) FROM vehicles").toInt() == 3,
                 "restored copy is independent");
}

static bool readers() {
    QTemporaryDir dir;
    if (!dir.isValid()) return false;
    const QString src = dir.filePath("source.sqlite");
    const QString dst = dir.filePath("backup.sqlite");
    if (!makeSeed(src)) return false;
    Database writer, reader;
    if (!writer.open(src) || !reader.open(src)) return false;
    QSqlQuery cursor(reader.db);
    if (!check(cursor.exec("SELECT id FROM rentals ORDER BY id") && cursor.next(),
               "independent reader remains open")) return false;
    if (!check(backup(writer, dst), "snapshot with active reader")) return false;
    return check(cursor.next() && cursor.value(0).toString() == "R002",
                 "reader was not invalidated") &&
           check(restored(dst), "consistent restored copy with reader") &&
           check(writer.baseline(), "source unchanged with reader");
}

static bool uncommitted() {
    QTemporaryDir dir;
    if (!dir.isValid()) return false;
    const QString src = dir.filePath("source.sqlite");
    const QString dst = dir.filePath("backup.sqlite");
    if (!makeSeed(src)) return false;
    Database writer, snapshotter;
    if (!writer.open(src) || !snapshotter.open(src) || !writer.db.transaction()) return false;
    if (!check(writer.run("INSERT INTO vehicles VALUES('C003','ACTIVE',50000)"),
               "transactional uncommitted edit")) { writer.db.rollback(); return false; }
    const bool ok = backup(snapshotter, dst);
    const bool rolledBack = writer.db.rollback();
    return check(ok && rolledBack, "backup without dirty reads") &&
           check(restored(dst), "uncommitted edit excluded from backup") &&
           check(writer.baseline(), "rollback left original intact");
}

static bool damaged() {
    QTemporaryDir dir;
    if (!dir.isValid()) return false;
    const QString src = dir.filePath("source.sqlite");
    const QString dst = dir.filePath("good.sqlite");
    const QString damagedPath = dir.filePath("damaged.sqlite");
    if (!makeSeed(src)) return false;
    {
        Database db;
        if (!db.open(src) || !backup(db, dst)) return false;
    }
    const QByteArray goodHash = sha256(dst);
    if (!check(goodHash.size() == 32 && QFile::copy(dst, damagedPath),
               "copy backup for deliberate corruption")) return false;
    {
        QFile corrupt(damagedPath);
        if (!corrupt.open(QIODevice::ReadWrite) || !corrupt.seek(0) ||
            corrupt.write("NOT-SQLITE", 10) != 10) return false;
    }
    bool accepted = false;
    {
        Database candidate;
        accepted = candidate.open(damagedPath) && candidate.integrity();
    }
    return check(!accepted, "corrupt backup rejected") &&
           check(sha256(dst) == goodHash && restored(dst),
                 "good backup retained after rejected corrupt copy");
}

static bool refuseOverwrite() {
    QTemporaryDir dir;
    if (!dir.isValid()) return false;
    const QString src = dir.filePath("source.sqlite");
    const QString dst = dir.filePath("existing.sqlite");
    if (!makeSeed(src)) return false;
    QFile sentinel(dst);
    if (!sentinel.open(QIODevice::WriteOnly) ||
        sentinel.write("DO-NOT-OVERWRITE", 16) != 16) return false;
    sentinel.close();
    const QByteArray original = sha256(dst);
    Database db;
    return check(db.open(src), "open source") &&
           check(!backup(db, dst), "existing destination refused") &&
           check(sha256(dst) == original, "existing bytes untouched") &&
           check(!QFileInfo::exists(dst + ".stage"), "no partial staged backup") &&
           check(db.baseline(), "source unchanged after overwrite denial");
}

static bool locked() {
    QTemporaryDir dir;
    if (!dir.isValid()) return false;
    const QString src = dir.filePath("source.sqlite");
    const QString dst = dir.filePath("backup.sqlite");
    {
        Database db;
        if (!db.open(src, false) || !db.seed()) return false;
    }
    Database blocker, snapshotter;
    if (!blocker.open(src, false) || !snapshotter.open(src, false) ||
        !check(blocker.exec("BEGIN EXCLUSIVE"), "exclusive source lock")) return false;
    const bool denied = !backup(snapshotter, dst);
    const bool release = blocker.exec("ROLLBACK");
    if (!check(denied && release, "locked source backup refused") ||
        !check(!QFileInfo::exists(dst) && !QFileInfo::exists(dst + ".stage"),
               "no completed or partial backup under lock")) return false;
    return check(backup(snapshotter, dst), "backup retries after lock release") &&
           check(restored(dst), "restored data intact after lock retry");
}

static bool restoreRoundtrip() {
    QTemporaryDir dir;
    if(!check(dir.isValid(),"restore temp root"))return false;
    const QString src=dir.filePath("source.sqlite");
    const QString backupPath=dir.filePath("backup.sqlite");
    const QString dst=dir.filePath("restored.sqlite");
    if(!makeSeed(src))return false;
    {
        Database source;
        if(!source.open(src)||!backup(source,backupPath))return false;
    }
    const QByteArray before=sha256(backupPath);
    if(!check(before.size()==32,"backup SHA before restore")||
       !check(restoreVerified(backupPath,dst),"new target restored")||
       !check(restored(dst),"restore passes independent full baseline")||
       !check(before==sha256(backupPath),"source backup bytes unchanged"))
        return false;
    {
        Database output;
        if(!output.open(dst)||
           !check(output.run("INSERT INTO vehicles VALUES('C003','ACTIVE',25000)"),
                  "restored DB can be independently edited")||
           !check(output.value("SELECT count(*) FROM vehicles").toInt()==3,
                  "restored extra record visible"))return false;
    }
    return check(restored(backupPath),"backup unaffected by restored edits")&&
           check(restored(src),"original live source unaffected");
}
static bool restoreDamaged() {
    QTemporaryDir dir;
    if(!dir.isValid())return false;
    const QString src=dir.filePath("source.sqlite");
    const QString good=dir.filePath("backup.sqlite");
    const QString bad=dir.filePath("damaged.sqlite");
    const QString dst=dir.filePath("new-target.sqlite");
    if(!makeSeed(src))return false;
    {
        Database source;
        if(!source.open(src)||!backup(source,good))return false;
    }
    const QByteArray hash=sha256(good);
    if(!check(hash.size()==32&&QFile::copy(good,bad),"corrupt fixture copied"))
        return false;
    {
        QFile bytes(bad);
        if(!bytes.open(QIODevice::ReadWrite)||!bytes.seek(0)||
           bytes.write("NOT-SQLITE",10)!=10)return false;
    }
    return check(!restoreVerified(bad,dst),"damaged backup restore refused")&&
           check(!QFileInfo::exists(dst)&&
                 !QFileInfo::exists(dst+".restore-stage"),
                 "failed restore cleans only owned stage")&&
           check(hash==sha256(good),"valid backup bytes retained")&&
           check(restoreVerified(good,dst)&&restored(dst),
                 "valid backup restores after corrupt refusal")&&
           check(restored(src),"live source unaffected");
}
static bool restoreRefuseOverwrite() {
    QTemporaryDir dir;
    if(!dir.isValid())return false;
    const QString src=dir.filePath("source.sqlite");
    const QString good=dir.filePath("backup.sqlite");
    const QString dst=dir.filePath("existing.sqlite");
    const QString stage=dst+".restore-stage";
    if(!makeSeed(src))return false;
    {
        Database source;
        if(!source.open(src)||!backup(source,good))return false;
    }
    const QByteArray hash=sha256(good);
    QFile existing(dst);
    if(!existing.open(QIODevice::WriteOnly|QIODevice::NewOnly)||
       existing.write("KEEP-DESTINATION",16)!=16)return false;
    existing.close();
    const QByteArray original=sha256(dst);
    if(!check(!restoreVerified(good,dst),"existing destination refused")||
       !check(original==sha256(dst),"existing bytes unchanged")||
       !check(!QFileInfo::exists(stage),"no partial restore stage"))return false;
    if(!QFile::remove(dst))return false;
    QFile stale(stage);
    if(!stale.open(QIODevice::WriteOnly|QIODevice::NewOnly)||
       stale.write("KEEP-STAGE",10)!=10)return false;
    stale.close();
    const QByteArray stageHash=sha256(stage);
    if(!check(!restoreVerified(good,dst),"existing staging file refused")||
       !check(stageHash==sha256(stage),"foreign staging bytes unchanged")||
       !check(!QFileInfo::exists(dst),"refused restore created no target")||
       !check(hash==sha256(good),"backup original unchanged"))return false;
    if(!QFile::remove(stage))return false;
    return check(restoreVerified(good,dst)&&restored(dst),
                 "valid recovery after staging conflict")&&
           check(restored(src),"source left unchanged");
}


// P7-Y: a SQLite file can pass its page-level integrity_check and still be
// unusable as a v1 fictional backup. Refuse future-version, broken-FK and
// suspicious sidecar restores without modifying source/backup or target.
static bool restoreFutureSchema() {
    QTemporaryDir dir;
    if (!check(dir.isValid(), "future schema temporary root")) return false;
    const QString src = dir.filePath("source.sqlite");
    const QString good = dir.filePath("good.sqlite");
    const QString future = dir.filePath("future.sqlite");
    const QString dst = dir.filePath("recovered.sqlite");
    const QString stage = dst + ".restore-stage";
    if (!makeSeed(src)) return false;
    {
        Database db;
        if (!db.open(src) || !backup(db, good)) return false;
    }
    const QByteArray goodSha = sha256(good);
    if (!check(goodSha.size() == 32 && QFile::copy(good, future),
               "future schema test copy")) return false;
    {
        Database modified;
        if (!modified.open(future, false) ||
            !check(modified.exec("PRAGMA user_version=2"),
                   "future v2 version committed") ||
            !check(modified.value("PRAGMA user_version").toInt() == 2,
                   "future version observed") ||
            !check(modified.value("PRAGMA integrity_check").toString() == "ok",
                   "future source remains structurally valid"))
            return false;
    }
    if (!check(!restoreVerified(future, dst), "future-version restore denied") ||
        !check(!QFileInfo::exists(dst) && !QFileInfo::exists(stage),
               "refused future version left no target/stage") ||
        !check(goodSha == sha256(good),
               "good backup unchanged by version refusal"))
        return false;
    if (!check(restoreVerified(good, dst) && restored(dst),
               "known v1 backup restores after future-version refusal"))
        return false;
    return check(restored(src), "live original intact after version refusal");
}

static bool restoreBrokenForeignKey() {
    QTemporaryDir dir;
    if (!check(dir.isValid(), "FK corruption temporary root")) return false;
    const QString src = dir.filePath("source.sqlite");
    const QString good = dir.filePath("good.sqlite");
    const QString bad = dir.filePath("orphaned.sqlite");
    const QString dst = dir.filePath("restored.sqlite");
    const QString stage = dst + ".restore-stage";
    if (!makeSeed(src)) return false;
    {
        Database db;
        if (!db.open(src) || !backup(db, good)) return false;
    }
    const QByteArray goodSha = sha256(good);
    if (!check(goodSha.size() == 32 && QFile::copy(good, bad),
               "FK corruption copied fictional snapshot")) return false;
    {
        Database modified;
        if (!modified.open(bad, false) ||
            !check(modified.exec("PRAGMA foreign_keys=OFF"),
                   "FK temporarily off only in corrupt fixture") ||
            !check(modified.run(
                       "UPDATE rentals SET vehicle_id='C999' WHERE id='R002'"),
                   "create missing-vehicle FK reference") ||
            !check(modified.exec("PRAGMA foreign_keys=ON"),
                   "FK enforcement re-enabled for fixture") ||
            !check(modified.value("PRAGMA integrity_check").toString() == "ok",
                   "orphan fixture remains page-level valid"))
            return false;
        QSqlQuery fk(modified.db);
        if (!check(fk.exec("PRAGMA foreign_key_check") && fk.next(),
                   "broken FK separately detectable"))
            return false;
    }
    return check(!restoreVerified(bad, dst), "foreign-key-invalid restore denied") &&
           check(!QFileInfo::exists(dst) && !QFileInfo::exists(stage),
                 "broken-FK restore left no target/stage") &&
           check(goodSha == sha256(good), "original good backup unchanged") &&
           check(restoreVerified(good, dst) && restored(dst),
                 "good backup restores after FK refusal") &&
           check(restored(src), "live source retained after FK refusal");
}

static bool restoreUnexpectedSidecar() {
    QTemporaryDir dir;
    if (!check(dir.isValid(), "sidecar temporary root")) return false;
    const QString src = dir.filePath("source.sqlite");
    const QString good = dir.filePath("good.sqlite");
    const QString dst = dir.filePath("restored.sqlite");
    const QString stage = dst + ".restore-stage";
    if (!makeSeed(src)) return false;
    {
        Database db;
        if (!db.open(src) || !backup(db, good)) return false;
    }
    const QByteArray original = sha256(good);
    if (!check(original.size() == 32, "sidecar original snapshot SHA")) return false;
    const QStringList suffixes = { "-wal", "-shm", "-journal" };
    for (const QString &suffix : suffixes) {
        const QString foreignSidecar = good + suffix;
        QFile marker(foreignSidecar);
        if (!check(marker.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
                       marker.write("FOREIGN-SIDECAR", 15) == 15,
                   "write private fictional sidecar sentinel"))
            return false;
        marker.close();
        const QByteArray sentinelSha = sha256(foreignSidecar);
        if (!check(!restoreVerified(good, dst), "unexpected sidecar restore refused") ||
            !check(!QFileInfo::exists(dst) && !QFileInfo::exists(stage),
                   "sidecar refusal left no target/stage") ||
            !check(sha256(foreignSidecar) == sentinelSha,
                   "foreign sidecar bytes untouched") ||
            !check(sha256(good) == original,
                   "backup bytes untouched on sidecar refusal"))
            return false;
        if (!check(QFile::remove(foreignSidecar),
                   "test-only cleanup of owned fictional sidecar"))
            return false;
    }
    return check(restoreVerified(good, dst) && restored(dst),
                 "good snapshot restores after sidecars removed") &&
           check(restored(src), "original database unchanged by sidecar refusals");
}


// P7-Z: the right row counts and SQLite page integrity are not sufficient
// when the UNIQUE active-occupancy index has been removed from a v1 backup.
// Both new backup creation and restore must refuse, without data mutation.
static bool restoreMissingActiveIndex() {
    QTemporaryDir dir;
    if (!check(dir.isValid(), "missing-index temporary root")) return false;
    const QString src = dir.filePath("source.sqlite");
    const QString good = dir.filePath("good.sqlite");
    const QString unsafe = dir.filePath("indexless.sqlite");
    const QString deniedBackup = dir.filePath("indexless-backup.sqlite");
    const QString dst = dir.filePath("recovered.sqlite");
    if (!makeSeed(src)) return false;
    {
        Database source;
        if (!source.open(src) || !backup(source, good)) return false;
    }
    const QByteArray originalGood = sha256(good);
    if (!check(originalGood.size() == 32 && QFile::copy(good, unsafe),
               "copy only generated v1 snapshot")) return false;
    {
        Database altered;
        if (!altered.open(unsafe, false) ||
            !check(altered.exec("DROP INDEX active_vehicle"),
                   "remove only unique occupancy index in disposable copy") ||
            !check(altered.value("PRAGMA user_version").toInt() == 1,
                   "indexless backup still declares v1") ||
            !check(altered.value("PRAGMA integrity_check").toString() == "ok",
                   "indexless database still passes page-level check") ||
            !check(altered.value("SELECT count(*) FROM rentals").toInt() == 2,
                   "unmodified fictional rental records remain readable") ||
            !check(!backup(altered, deniedBackup),
                   "new backup creation refuses indexless source"))
            return false;
    }
    const QByteArray alteredHash = sha256(unsafe);
    if (!check(alteredHash.size() == 32 &&
               !QFileInfo::exists(deniedBackup) &&
               !QFileInfo::exists(deniedBackup + ".stage"),
               "indexless backup refusal left no final or staged copy") ||
        !check(!restoreVerified(unsafe, dst),
               "structurally valid but indexless restore refused") ||
        !check(!QFileInfo::exists(dst) &&
               !QFileInfo::exists(dst + ".restore-stage"),
               "indexless restore left no destination or stage") ||
        !check(originalGood == sha256(good) && alteredHash == sha256(unsafe),
               "both original and rejected snapshot bytes unchanged") ||
        !check(restoreVerified(good, dst) && restored(dst),
               "known v1 backup still restores after indexless refusal"))
        return false;
    return check(restored(src), "live original unchanged after indexless refusals");
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2; // fixed test names only; never file paths
    const QString mode = QString::fromLatin1(argv[1]);
    bool ok = false;
    if (mode == "snapshot") ok = snapshot();
    else if (mode == "readers") ok = readers();
    else if (mode == "uncommitted") ok = uncommitted();
    else if (mode == "damaged") ok = damaged();
    else if (mode == "refuse_overwrite") ok = refuseOverwrite();
    else if (mode == "locked") ok = locked();
    else if (mode == "restore_roundtrip") ok = restoreRoundtrip();
    else if (mode == "restore_damaged") ok = restoreDamaged();
    else if (mode == "restore_refuse_overwrite") ok = restoreRefuseOverwrite();
    else if (mode == "restore_future_schema") ok = restoreFutureSchema();
    else if (mode == "restore_broken_foreign_key") ok = restoreBrokenForeignKey();
    else if (mode == "restore_unexpected_sidecar") ok = restoreUnexpectedSidecar();
    else if (mode == "restore_missing_active_index") ok = restoreMissingActiveIndex();
    else return 3;
    if (ok) qInfo() << "P7-E synthetic SQLite backup proof PASS:" << mode;
    return ok ? 0 : 1;
}
