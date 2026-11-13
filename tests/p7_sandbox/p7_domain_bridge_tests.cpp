// P7-G: TEMPORARY GENERATED-FICTIONAL STORAGE TRANSPORT for real C++ domain services.
//
// This test proves that RentalSystem's existing injectable RentalStorage
// interface is used by RentalQuoteService/RentalConfirmService, returns,
// archive and independent reopen. It DOES NOT implement the candidate
// commercial relational schema, exact-cents migration, privacy, ID issuance
// policy, double-file atomicity or real-data activation. Not linked to Qt GUI.
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>
#include <QUuid>
#include <QVariant>
#include <QDebug>
#include <memory>
#include <optional>
#include "rentalsystem.h"
#include "src/application/rental_quote_service.h"
#include "src/application/rental_confirm_service.h"

namespace {
bool check(bool pass, const char *reason) {
    if (!pass) qCritical() << "P7-G FAIL:" << reason;
    return pass;
}
const QByteArray vehicles =
    "Car|C001|Toyota|Corolla|450.00|0|4\n"
    "Motorcycle|M001|Honda|CB500F|280.00|0|471\n";
const QByteArray rentals = "";
const QByteArray marker = "P7-G GENERATED FICTIONAL ONLY\n";

class FictionalSqliteTransport final : public RentalStorage {
public:
    explicit FictionalSqliteTransport(const QString &root)
        : m_root(QDir::cleanPath(root)),
          m_connectionName("p7g-" + QUuid::createUuid().toString(QUuid::WithoutBraces)) {
        QFile flag(QDir(m_root).filePath(".fictional-only"));
        if (!flag.open(QIODevice::ReadOnly) || flag.readAll() != marker)
            return;
        if (!QSqlDatabase::isDriverAvailable("QSQLITE"))
            return;
        m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
        m_db.setDatabaseName(QDir(m_root).filePath("fictional.sqlite"));
        if (!m_db.open() || !execute("PRAGMA foreign_keys=ON") ||
            !execute("PRAGMA busy_timeout=0") ||
            !execute("PRAGMA synchronous=FULL"))
            return;
        const QVariant version = scalar("PRAGMA user_version");
        if (!version.isValid() || version.toInt() > 1)
            return;
        if (version.toInt() == 0) {
            if (!execute("BEGIN IMMEDIATE") ||
                !execute("CREATE TABLE demo_blob("
                         "name TEXT PRIMARY KEY CHECK(name IN ('vehicles.txt','rentals.txt')),"
                         "bytes BLOB NOT NULL, digest BLOB NOT NULL)") ||
                !execute("PRAGMA user_version=1") || !execute("COMMIT")) {
                execute("ROLLBACK");
                return;
            }
        }
        // Existing schema or just-created schema must have both properties.
        if (scalar("PRAGMA user_version").toInt() != 1 ||
            scalar("PRAGMA integrity_check").toString() != "ok")
            return;
        m_ready = true;
    }
    ~FictionalSqliteTransport() override {
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    bool ready() const { return m_ready; }
    bool bootstrap() {
        if (!m_ready || scalar("SELECT COUNT(*) FROM demo_blob").toInt() != 0)
            return false;
        if (!execute("BEGIN IMMEDIATE"))
            return false;
        if (!put("vehicles.txt", vehicles) || !put("rentals.txt", rentals)) {
            execute("ROLLBACK");
            return false;
        }
        if (!execute("COMMIT")) {
            execute("ROLLBACK");
            return false;
        }
        return true;
    }
    void failOneCommit() { m_rejectNext = true; }
    bool forceFutureSchemaForTest() {
        return m_ready && execute("PRAGMA user_version=2");
    }
    std::optional<QByteArray> read(const QString &path) const override {
        const QString key = allowedKey(path);
        if (!m_ready || key.isEmpty())
            return std::nullopt;
        QSqlQuery query(m_db);
        query.prepare("SELECT bytes, digest FROM demo_blob WHERE name=?");
        query.addBindValue(key);
        if (!query.exec() || !query.next())
            return std::nullopt;
        const QByteArray bytes = query.value(0).toByteArray();
        const QByteArray digest = query.value(1).toByteArray();
        if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) != digest)
            return std::nullopt;
        return bytes;
    }
    bool writeAtomic(const QString &path, const QByteArray &bytes) override {
        const QString key = allowedKey(path);
        if (!m_ready || key.isEmpty() || !rowExists(key))
            return false;
        if (m_rejectNext) {
            m_rejectNext = false;
            return false;
        }
        if (!execute("BEGIN IMMEDIATE"))
            return false;
        if (!put(key, bytes) || !execute("COMMIT")) {
            execute("ROLLBACK");
            return false;
        }
        return true;
    }
private:
    QString allowedKey(const QString &path) const {
        const QString clean = QDir::cleanPath(path);
        if (clean == QDir(m_root).filePath("vehicles.txt"))
            return "vehicles.txt";
        if (clean == QDir(m_root).filePath("rentals.txt"))
            return "rentals.txt";
        return {};
    }
    bool rowExists(const QString &key) const {
        QSqlQuery q(m_db);
        q.prepare("SELECT 1 FROM demo_blob WHERE name=?");
        q.addBindValue(key);
        return q.exec() && q.next();
    }
    bool put(const QString &key, const QByteArray &bytes) {
        QSqlQuery q(m_db);
        q.prepare("INSERT INTO demo_blob(name, bytes, digest) VALUES(?, ?, ?) "
                  "ON CONFLICT(name) DO UPDATE SET bytes=excluded.bytes, digest=excluded.digest");
        q.addBindValue(key);
        q.addBindValue(bytes);
        q.addBindValue(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
        return q.exec();
    }
    bool execute(const QString &sql) const {
        QSqlQuery q(m_db);
        return q.exec(sql);
    }
    QVariant scalar(const QString &sql) const {
        QSqlQuery q(m_db);
        return q.exec(sql) && q.next() ? q.value(0) : QVariant();
    }
    QString m_root;
    QString m_connectionName;
    QSqlDatabase m_db;
    bool m_ready = false;
    bool m_rejectNext = false;
};

bool makeTemp(QTemporaryDir &temp) {
    if (!check(temp.isValid(), "temporary root"))
        return false;
    QFile flag(QDir(temp.path()).filePath(".fictional-only"));
    if (!check(flag.open(QIODevice::WriteOnly | QIODevice::NewOnly), "create fictional marker"))
        return false;
    return check(flag.write(marker) == marker.size(), "write fictional marker");
}
std::optional<RentalConfirmationRequest> requestFor(RentalSystem &domain) {
    RentalQuoteService quotes(domain);
    auto response = quotes.preview("C001", 3);
    if (!check(response.ok(), "C++ quote accepted"))
        return std::nullopt;
    return RentalConfirmationRequest{*response.quote, "Fictional Demo Session", "0000000000"};
}
bool run(const QString &test) {
    QTemporaryDir temp(QDir::tempPath() + "/rental-p7g-XXXXXX");
    if (!makeTemp(temp))
        return false;
    const QString dir = temp.path();
    auto adapter = std::make_shared<FictionalSqliteTransport>(dir);
    if (!check(adapter->ready() && adapter->bootstrap(), "bootstrap private SQLite transport"))
        return false;
    if (test == "booking") {
        {
            RentalSystem domain(dir, adapter);
            const auto req = requestFor(domain);
            if (!req) return false;
            RentalConfirmService service(domain);
            const auto created = service.confirm(*req);
            if (!check(created.ok() && created.receipt->rentalId == "R001" &&
                created.receipt->legacyTotal == 1350.0, "actual C++ confirmation")) return false;
            if (!check(domain.findVehicleById("C001")->getIsRented(), "domain occupancy"))
                return false;
            if (!check(!service.confirm(*req).ok(), "duplicate booking refused"))
                return false;
        }
        // Actually close the SQL connection and open a NEW independent domain.
        adapter.reset();
        auto second = std::make_shared<FictionalSqliteTransport>(dir);
        if (!check(second->ready(), "SQLite reopens")) return false;
        RentalSystem loaded(dir, second);
        return check(loaded.getVehicles().size() == 2 &&
                     loaded.getRentals().size() == 1 &&
                     loaded.findActiveRentalByVehicleId("C001") != nullptr &&
                     loaded.findVehicleById("C001")->getIsRented() &&
                     loaded.generateNextRentalId() == "R002",
                     "real C++ RentalSystem reconstructs from SQLite adapter");
    }
    if (test == "lifecycle") {
        {
            RentalSystem domain(dir, adapter);
            const auto req = requestFor(domain);
            if (!req) return false;
            RentalConfirmService service(domain);
            if (!check(service.confirm(*req).ok(), "booking saved")) return false;
            if (!check(domain.completeRental("R001", domain.today()), "C++ return saved"))
                return false;
            if (!check(!domain.completeRental("R001", domain.today()), "double return denied"))
                return false;
            if (!check(domain.archiveRental("R001"), "C++ archive saved"))
                return false;
        }
        adapter.reset();
        auto next = std::make_shared<FictionalSqliteTransport>(dir);
        if (!check(next->ready(), "reopen returned history")) return false;
        RentalSystem loaded(dir, next);
        auto r = loaded.findRentalById("R001");
        return check(r && r->getIsArchived() && r->getIsReturned() &&
                     loaded.findVehicleById("C001") &&
                     !loaded.findVehicleById("C001")->getIsRented() &&
                     loaded.getRentals().size() == 1,
                     "archived history and availability reconstruct across instances");
    }
    if (test == "failure") {
        {
            RentalSystem domain(dir, adapter);
            const auto req = requestFor(domain);
            if (!req) return false;
            adapter->failOneCommit();
            RentalConfirmService service(domain);
            const auto failed = service.confirm(*req);
            if (!check(!failed.ok() && failed.error == RentalConfirmationError::CommitFailed &&
                       !failed.receipt && domain.getRentals().isEmpty() &&
                       domain.generateNextRentalId() == "R001" &&
                       !domain.findVehicleById("C001")->getIsRented(),
                       "injected SQLite commit failure produces no receipt/state"))
                return false;
            if (!check(adapter->read(QDir(dir).filePath("rentals.txt")).value_or("NOT_FOUND") == rentals,
                       "failed write leaves persisted empty ledger")) return false;
            if (!check(service.confirm(*req).ok(), "retry allocates same R001"))
                return false;
        }
        adapter.reset();
        auto next = std::make_shared<FictionalSqliteTransport>(dir);
        if (!check(next->ready(), "restart after injected failure")) return false;
        if (!check(next->forceFutureSchemaForTest(), "create simulated future schema"))
            return false;
        next.reset();
        auto refused = std::make_shared<FictionalSqliteTransport>(dir);
        return check(!refused->ready(), "newer schema cannot be opened by old adapter");
    }
    return check(false, "unknown test case");
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2)
        return 2;
    const bool pass = run(app.arguments().at(1));
    if (pass) qInfo() << "P7-G real C++ domain/SQLite fictional adapter PASS";
    return pass ? 0 : 1;
}
