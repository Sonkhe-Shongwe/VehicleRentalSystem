#include <QAccessible>
#include <QAbstractListModel>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QFontInfo>
#include <QScreen>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>
#include <QImage>
#include <QColor>
#include <QTest>
#include <QPointF>
#include <QProcess>
#include <QSortFilterProxyModel>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QSaveFile>
#include "p5_synthetic_session.h"
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <cmath>
#include <algorithm>
#include "rentalsystem.h"
#include "src/application/fleet_read_service.h"
#include "src/application/rental_read_service.h"
#include "src/application/rental_list_model.h"
#include "src/application/rental_quote_service.h"
#include "src/application/rental_confirm_service.h"
#include "src/application/rental_return_controller.h"
#include "src/application/rental_booking_controller.h"
#include "src/application/rental_archive_controller.h"
#include "p6_archived_delete_controller.h"
#include "fleet_add_controller.h"
#include "fleet_photo_store.h"
#include "fleet_remove_controller.h"
#include "motion_policy.h"
#ifdef RENTAL_STUDIO_P7_SQL_LAB
#include "src/application/p7_sandbox/repository_preview_controller.h"
#include "src/application/p7_sandbox/sqlite_read_projection.h"
#include "src/application/p7_sandbox/fictional_fleet_snapshot.h"
#include "src/application/p7_sandbox/fictional_operations_snapshot.h"
#endif

// The P3 typed application read boundary snapshots RentalSystem objects as
// values from disposable fictional fixtures. No Vehicle* reaches QML.
class FleetModel final : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        VehicleId = Qt::UserRole + 1,
        DisplayName, VehicleType, DailyRate, Available, Details
    };
    explicit FleetModel(const FleetReadService &readService, QObject *parent = nullptr)
        : QAbstractListModel(parent), m_readService(readService),
          m_vehicles(readService.vehicles()) {}
    // Explicitly re-snapshot the authoritative domain only when the caller
    // knows its transaction has committed. No implicit polling or UI writes.
    void refresh() {
        beginResetModel();
        m_vehicles = m_readService.vehicles();
        endResetModel();
    }
    const FleetVehicleSummary *findById(const QString &id) const {
        for (const auto &item : m_vehicles) {
            if (item.id.compare(id.trimmed(), Qt::CaseInsensitive) == 0)
                return &item;
        }
        return nullptr;
    }
    QVariantList availableHighlights(int limit = 2) const {
        QVariantList rows;
        for (const auto &vehicle : m_vehicles) {
            if (!vehicle.available)
                continue;
            rows.append(QVariantMap{
                {QStringLiteral("vehicleId"), vehicle.id},
                {QStringLiteral("displayName"), vehicle.displayName()},
                {QStringLiteral("vehicleType"), vehicle.type},
                {QStringLiteral("dailyRate"), vehicle.legacyDailyRate}
            });
            if (rows.size() >= limit)
                break;
        }
        return rows;
    }
    int availableCount() const {
        int count = 0;
        for (const auto &v : m_vehicles) count += v.available;
        return count;
    }
    // Count the whole committed domain snapshot, never the search/status proxy.
    int countType(const QString &type) const {
        int count = 0;
        for (const auto &v : m_vehicles)
            count += v.type.compare(type, Qt::CaseInsensitive) == 0;
        return count;
    }
    int rowCount(const QModelIndex &parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : m_vehicles.size();
    }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_vehicles.size())
            return {};
        const FleetVehicleSummary &v = m_vehicles.at(index.row());
        switch (role) {
        case VehicleId: return v.id;
        case DisplayName: return v.displayName();
        case VehicleType: return v.type;
        case DailyRate: return v.legacyDailyRate;
        case Available: return v.available;
        case Details: return v.details;
        default: return {};
        }
    }
    QHash<int, QByteArray> roleNames() const override {
        return {{VehicleId,"vehicleId"},{DisplayName,"displayName"},
                {VehicleType,"vehicleType"},{DailyRate,"dailyRate"},
                {Available,"available"},{Details,"details"}};
    }
private:
    const FleetReadService &m_readService;
    QList<FleetVehicleSummary> m_vehicles;
};

class FleetProxy final : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit FleetProxy(QObject *parent = nullptr) : QSortFilterProxyModel(parent) {
        setDynamicSortFilter(true);
        sort(0);
    }
    void setQuery(const QString &query) {
        if (m_query == query) return;
        m_query = query;
        invalidateFilter();
    }
    void setStatus(const QString &status) {
        if (m_status == status) return;
        m_status = status;
        invalidateFilter();
    }
    void setSortBy(const QString &sortBy) {
        if (m_sortBy == sortBy) return;
        m_sortBy = sortBy;
        invalidate();
        sort(0);
    }
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override {
        const auto idx = sourceModel()->index(row, 0, parent);
        const QString id = idx.data(FleetModel::VehicleId).toString();
        const QString name = idx.data(FleetModel::DisplayName).toString();
        const bool available = idx.data(FleetModel::Available).toBool();
        const bool queryMatch = id.contains(m_query, Qt::CaseInsensitive) ||
                                name.contains(m_query, Qt::CaseInsensitive);
        const bool statusMatch = m_status == "All" ||
                                 (m_status == "Available" && available) ||
                                 (m_status == "Rented" && !available);
        return queryMatch && statusMatch;
    }
    bool lessThan(const QModelIndex &lhs, const QModelIndex &rhs) const override {
        if (m_sortBy == "Rate") {
            const double a = lhs.data(FleetModel::DailyRate).toDouble();
            const double b = rhs.data(FleetModel::DailyRate).toDouble();
            if (a != b) return a < b;
        }
        return QString::localeAwareCompare(
                   lhs.data(FleetModel::DisplayName).toString(),
                   rhs.data(FleetModel::DisplayName).toString()) < 0;
    }
private:
    QString m_query;
    QString m_status = "All";
    QString m_sortBy = "Name";
};

class FleetController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* visibleModel READ visibleModel CONSTANT)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(QString status READ status WRITE setStatus NOTIFY statusChanged)
    Q_PROPERTY(QString sortBy READ sortBy WRITE setSortBy NOTIFY sortByChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedType READ selectedType NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedDetails READ selectedDetails NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedRate READ selectedRate NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedAvailable READ selectedAvailable NOTIFY selectionChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY fleetChanged)
    Q_PROPERTY(int availableCount READ availableCount NOTIFY fleetChanged)
    Q_PROPERTY(QVariantList availableHighlights READ availableHighlights NOTIFY fleetChanged)
    // Complete, unfiltered fictional fleet names for read-only cross-workspace labels.
    Q_PROPERTY(QVariantMap vehicleNames READ vehicleNames NOTIFY fleetChanged)
    Q_PROPERTY(int carCount READ carCount NOTIFY fleetChanged)
    Q_PROPERTY(int motorcycleCount READ motorcycleCount NOTIFY fleetChanged)
    Q_PROPERTY(int rentedCount READ rentedCount NOTIFY fleetChanged)
public:
    explicit FleetController(RentalSystem &system, QObject *parent = nullptr)
        : QObject(parent), m_readService(system), m_source(m_readService, this), m_proxy(this) {
        m_proxy.setSourceModel(&m_source);
        m_proxy.sort(0);
    }
    QObject *visibleModel() { return &m_proxy; }
    QString query() const { return m_query; }
    QString status() const { return m_status; }
    QString sortBy() const { return m_sort; }
    void setQuery(const QString &query) {
        if (query == m_query) return;
        m_query = query;
        m_proxy.setQuery(query);
        clearHiddenSelection();
        emit queryChanged();
    }
    void setStatus(const QString &status) {
        if (status != "All" && status != "Available" && status != "Rented") return;
        if (status == m_status) return;
        m_status = status;
        m_proxy.setStatus(status);
        clearHiddenSelection();
        emit statusChanged();
    }
    void setSortBy(const QString &sort) {
        if (sort != "Name" && sort != "Rate") return;
        if (sort == m_sort) return;
        m_sort = sort;
        m_proxy.setSortBy(sort);
        emit sortByChanged();
    }
    QString selectedId() const { return m_selectedId; }
    QString selectedName() const {
        const auto *v = selectedVehicle();
        return v ? v->displayName() : QString();
    }
    QString selectedType() const {
        const auto *v = selectedVehicle();
        return v ? v->type : QString();
    }
    QString selectedDetails() const {
        const auto *v = selectedVehicle();
        return v ? v->details : QString();
    }
    QString selectedRate() const {
        const auto *v = selectedVehicle();
        return v ? QString::number(v->legacyDailyRate, 'f', 2) : QString();
    }
    bool selectedAvailable() const {
        const auto *v = selectedVehicle();
        return v && v->available;
    }
    int totalCount() const { return m_source.rowCount(); }
    int availableCount() const { return m_source.availableCount(); }
    QVariantList availableHighlights() const { return m_source.availableHighlights(); }
    QVariantMap vehicleNames() const {
        QVariantMap names;
        for (int row = 0; row < m_source.rowCount(); ++row) {
            const auto index = m_source.index(row, 0);
            const QString id = m_source.data(index, FleetModel::VehicleId).toString();
            if (const auto *vehicle = m_source.findById(id))
                names.insert(id, vehicle->displayName());
        }
        return names;
    }
    int carCount() const { return m_source.countType(QStringLiteral("Car")); }
    int motorcycleCount() const { return m_source.countType(QStringLiteral("Motorcycle")); }
    int rentedCount() const { return totalCount() - availableCount(); }

    // An application command, NOT a QML Button, should call this after a
    // successful domain commit. Refresh both Fleet and Rentals in the same
    // non-reentrant caller turn so Overview/Fleet cannot stay stale.
    void refreshAfterCommit() {
        const QString previouslySelected = m_selectedId;
        m_source.refresh();
        if (!m_selectedId.isEmpty() && selectedVisibleIndex() < 0)
            m_selectedId.clear();
        emit fleetChanged();
        // Existing QML selected-rate/status roles depend on this signal even
        // when the same selected ID now has different domain properties.
        if (!previouslySelected.isEmpty())
            emit selectionChanged();
    }

    Q_INVOKABLE void selectById(const QString &id) {
        const auto *vehicle = m_source.findById(id);
        if (!vehicle) return;
        if (m_selectedId == vehicle->id) return;
        m_selectedId = vehicle->id;
        emit selectionChanged();
    }
    // Visible rows are defined by the authoritative C++ proxy, not QML's
    // scroll offsets. Keyboard navigation therefore follows filtered/sorted IDs.
    Q_INVOKABLE int selectedVisibleIndex() const {
        if (m_selectedId.isEmpty()) return -1;
        for (int row = 0; row < m_proxy.rowCount(); ++row) {
            if (m_proxy.index(row, 0).data(FleetModel::VehicleId).toString()
                == m_selectedId) return row;
        }
        return -1;
    }
    Q_INVOKABLE void selectVisibleIndex(int row) {
        if (row < 0 || row >= m_proxy.rowCount()) return;
        selectById(m_proxy.index(row, 0).data(FleetModel::VehicleId).toString());
    }
    Q_INVOKABLE void clearSelection() {
        if (m_selectedId.isEmpty()) return;
        m_selectedId.clear();
        emit selectionChanged();
    }
signals:
    void queryChanged();
    void statusChanged();
    void sortByChanged();
    void selectionChanged();
    void fleetChanged();
private:
    // Clearing a now-invisible ID prevents the Inspector from describing a
    // vehicle that no longer exists in the user's filtered Fleet view.
    void clearHiddenSelection() {
        if (!m_selectedId.isEmpty() && selectedVisibleIndex() < 0)
            clearSelection();
    }
    const FleetVehicleSummary *selectedVehicle() const {
        return m_selectedId.isEmpty()
            ? nullptr : m_source.findById(m_selectedId);
    }
    FleetReadService m_readService;
    FleetModel m_source;
    FleetProxy m_proxy;
    QString m_query;
    QString m_status = "All";
    QString m_sort = "Name";
    QString m_selectedId;
};

static bool readFile(const QString &path, QByteArray *bytes) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    *bytes = f.readAll();
    return f.error() == QFile::NoError;
}

// Native QML negative-path test only: the real domain still validates the
// quote and performs normal rollback when its injected durable writer fails.
class P5RejectWriteStorage final : public RentalStorage {
public:
    std::optional<QByteArray> read(const QString &path) const override {
        QFile file(path);
        if(!file.open(QIODevice::ReadOnly)) return std::nullopt;
        const QByteArray bytes=file.readAll();
        if(file.error()!=QFile::NoError) return std::nullopt;
        return bytes;
    }
    bool writeAtomic(const QString &, const QByteArray &) override {
        return false;
    }
};

static bool capturePortfolioDemo(QQuickWindow *window, FleetController &fleet,
                                 RentalListModel &rentals, RentalBookingController &booking,
                                 const RentalSystem &system) {
    const QSize size(1280, 720);
    window->resize(size);
    if (!QDir::current().mkdir(QStringLiteral("portfolio-demo-frames"))) {
        qCritical() << "Use a working directory without portfolio-demo-frames";
        return false;
    }
    const QDir output(QDir::current().filePath(QStringLiteral("portfolio-demo-frames")));
    const auto findItem=[](const auto &self, QQuickItem *item,
                           const QString &name) -> QQuickItem * {
        if (item->isVisible() && item->objectName() == name)
            return item;
        for (auto *child : item->childItems()) {
            if (auto *found = self(self, child, name))
                return found;
        }
        return nullptr;
    };
    const auto control=[&](const QString &name) {
        return findItem(findItem, window->contentItem(), name);
    };
    const auto press=[&](const QString &name) {
        auto *item = control(name);
        if (!item || !item->isEnabled()) {
            qCritical() << "Portfolio demo control unavailable:" << name;
            return false;
        }
        item->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Space);
        return true;
    };
    const auto frames=[&](const QString &stage) {
        for (int frame = 0; frame < 6; ++frame) {
            QTest::qWait(100);
            const auto image = window->grabWindow();
            const auto name = QStringLiteral("%1-%2.jpg").arg(stage).arg(frame);
            if (image.size() != size || !image.save(output.filePath(name), "JPEG", 95)) {
                qCritical() << "Portfolio demo frame failed:" << name << image.size();
                return false;
            }
        }
        return true;
    };

    if (fleet.totalCount() != 131 || !frames("01-overview") ||
        !press("workspaceNavFleet") || !frames("02-vehicles"))
        return false;

    auto *search = control("fleetSearchField");
    if (!search)
        return false;
    search->forceActiveFocus(Qt::TabFocusReason);
    for (const auto key : {Qt::Key_C, Qt::Key_0, Qt::Key_0, Qt::Key_1})
        QTest::keyClick(window, key);
    fleet.selectById("C001");
    if (fleet.selectedId() != "C001" || !fleet.selectedAvailable() ||
        !frames("03-available") || !press("fleetBookSelectedButton"))
        return false;

    QTest::qWait(180);
    auto *days = control("bookingDays");
    auto *name = control("bookingCustomer");
    auto *phone = control("bookingPhone");
    if (!days || !name || !phone)
        return false;
    days->setProperty("value", 3);
    if (!press("bookingPreviewButton") || !frames("04-quote") ||
        !booking.canConfirm() || !booking.quoteSummary().contains("Total R 1350.00"))
        return false;
    name->setProperty("text", "Fictional Demo Customer");
    phone->setProperty("text", "0000000000");
    if (!press("bookingConfirmButton"))
        return false;

    QTest::qWait(180);
    const auto rentalId = booking.lastRentalId();
    const auto *vehicle = system.findVehicleById("C001");
    if (rentalId.isEmpty() || !vehicle || !vehicle->getIsRented() ||
        !rentals.locateById(rentalId) || rentals.selectedContract().value("status").toString() != "Active" ||
        !frames("05-booked") || !press("rentalReturnButton") || !frames("06-return"))
        return false;

    if (!press("rentalReturnConfirm") || !frames("07-returned") ||
        vehicle->getIsRented() || rentals.selectedContract().value("status").toString() != "Returned" ||
        !press("rentalFilterHistory") || !frames("08-history") ||
        rentals.statusFilter() != "History" || rentals.selectedRentalId() != rentalId)
        return false;

    if (!press("workspaceNavFleet"))
        return false;
    QTest::qWait(150);
    fleet.selectById("C001");
    return fleet.selectedId() == "C001" && fleet.selectedAvailable() && frames("09-available-again");
}

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName("RentalStudioP2Spike");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"self-test", "Test model, filtering, selection, and fixture integrity without a GUI."});
    parser.addOption({"ui-smoke", "Load the native QML scene and capture actual Gallery/Table views."});
    parser.addOption({"startup-fit-smoke", "Verify default real Qt window fits the available desktop without changing OS scaling."});
    parser.addOption({"compact", "Use the compact 1280x720 viewport for the UI smoke."});
    parser.addOption({"input-smoke", "Drive actual QML buttons with keyboard events and test live resize."});
    parser.addOption({"arrows-smoke", "Exercise native QML Gallery/Table arrow navigation, focus, and filter parity."});
    parser.addOption({"focus-edges-smoke", "Test filtered selection, sorted focus, empty states, boundaries and responsive columns."});
    parser.addOption({"dpi-smoke", "Assert a real Qt window screenshot with synthetic device pixel ratio."});
    parser.addOption({"rentals-smoke", "Test the real read-only QML Rentals view and C++ commit-triggered refresh."});
    parser.addOption({"rentals-page-scroll-smoke", "Native Rentals page scroll and PageUp/PageDown reachability smoke."});
    parser.addOption({"rentals-filter-smoke", "Test real QML Active/History filtering and keyboard selected IDs."});
    parser.addOption({"rentals-return-smoke", "Test real QML confirmed return through C++ domain and model refresh."});
    parser.addOption({"rentals-booking-smoke", "Drive real QML booking review and domain confirm in temporary fixtures."});
    parser.addOption({"rentals-archive-smoke", "Drive real QML archival confirmation on a returned fictional rental."});
    parser.addOption({"persistent-fictional-demo", "Opt-in fixed app-owned fictional session surviving restart; never real customer storage."});
    parser.addOption({"p5-session-seed-test", "CI-only first process persists a controlled fictional booking."});
    parser.addOption({"p5-session-reopen-test", "CI-only separate process verifies the saved rental and refuses duplicates."});
    parser.addOption({"p5-session-clean-test", "CI-only remove the identified fixed test sandbox."});
    parser.addOption({"p5-ui-session-seed-test", "CI-only real QML booking inside a persistent fictional first launch."});
    parser.addOption({"p5-ui-session-reopen-test", "CI-only second real QML launch displays the previously saved booking."});
    parser.addOption({"p5-ui-session-clean-test", "CI-only clean the identified QML session test sandbox."});
    parser.addOption({"p5-ui-write-failure-test", "CI-only real QML confirmation with an injected failed durable write."});
    parser.addOption({"p5-session-tamper-test", "CI-only verify unexpected files and contact records are rejected."});
    parser.addOption({"p5-session-lock-probe-test", "CI-only child process verifies a held fictional session lock."});
    parser.addOption({"p5-booking-cancel-smoke", "Verify Cancel, Escape and declined booking abandonment scrub drafts without saving."});
    parser.addOption({"p5-stale-quote-smoke", "Test stale price and occupied vehicle rejection in real QML, with explicit quote refresh."});
    parser.addOption({"p5-fleet-to-booking-smoke", "Use actual Fleet inspector keyboard action to open and save an authoritative rental."});
    parser.addOption({"p5-overview-smoke", "Exercise real C++ operational overview, committed booking/return and safe navigation."});
    parser.addOption({"p6-recent-activity-smoke", "Test C++-backed bounded recent contracts, scrolling and read-only routing in real QML."});
    parser.addOption({"p6-contract-detail-smoke", "Test no-PII selected rental contract details and safe close on native Qt Quick."});
    parser.addOption({"ux-ctrl-002-smoke", "Verify owner-approved Qt directional keys, semantic danger and read-only contract Enter."});
    parser.addOption({"ux-ctrl-003-smoke", "Verify bidirectional cross-region Qt Quick keyboard navigation with no fixture writes."});
    parser.addOption({"ux-shell-004-smoke", "Native collapsible icon sidebar and Fleet All/Name/Search navigation proof."});
    parser.addOption({"pa-shell-001-smoke", "Precision Automotive Light real booking shortcut, icon rail and focus restoration."});
    parser.addOption({"r2-font-inter", "Opt in to the locally embedded official Inter font trial."});
    parser.addOption({"r2-font-trial-smoke", "CI-only real Qt Inter font and dual-size screenshot check."});
    parser.addOption({"pa-overview-002-smoke", "Native Precision Automotive Overview real C++ totals, availability ring and recent panel."});
    parser.addOption({"pa-fleet-003-smoke", "Native Precision Automotive Fleet gallery/table/inspector presentation and data-safety proof."});
    parser.addOption({"pa-rentals-004-smoke", "Inspect the actual Precision Automotive Rentals contract panels, register and status."});
    parser.addOption({"portfolio-catalog-smoke", "CI-only real 131-vehicle offline portfolio demo check."});
    parser.addOption({"portfolio-demo-capture", "Capture a fictional quote, booking and return for the portfolio video."});
    parser.addOption({"p6-fleet-add-smoke", "Native fictional fleet creation test."});
    parser.addOption({"fleet-photo-smoke", "Native fictional offline photo source, safe import and gallery/inspector test."});
    parser.addOption({"p6-fleet-remove-smoke", "Native confirmed fictional vehicle removal test."});
    parser.addOption({"p6-archived-delete-smoke", "Test exact-confirmation permanent deletion of archived fictional rentals."});
    parser.addOption({"p6-restart-flow", "CI-only separate-process P6 fictional lifecycle orchestrator."});
    parser.addOption({"p6-restart-child-seed", "CI-only first native P6 process, synthetic test working directory."});
    parser.addOption({"p6-restart-child-update", "CI-only second native P6 process, synthetic test working directory."});
    parser.addOption({"p6-restart-child-final", "CI-only third native P6 process, synthetic test working directory."});
    parser.addOption({"p6-integrated-flow", "CI-only genuine three-process native synthetic rental journey."});
    parser.addOption({"p6-integrated-child-seed", "CI-only native synthetic create-and-book stage."});
    parser.addOption({"p6-integrated-child-finish", "CI-only native synthetic return/archive/delete/remove stage."});
    parser.addOption({"p6-integrated-child-verify", "CI-only native synthetic final independent restart stage."});
    parser.addOption({"p4-shell-smoke", "Verify shared Qt Quick focus and keyboard/modal shell at wide/compact sizes."});
    parser.addOption({"p4-components-smoke", "Drive reusable rental fields, status and safe shared confirmation dialogs."});
    parser.addOption({"p4-tab-smoke", "Test Qt-native Fleet tab order and bounded modal Tab/Shift+Tab across workspaces."});
    parser.addOption({"p4-a11y-smoke", "Check live Qt accessibility names, roles and mutually exclusive states on fictional controls."});
    parser.addOption({"p4-motion-smoke", "Test native Qt Quick reduced-motion policy with fictional data only."});
    parser.addOption({"p4-reduce-motion", "Test-only reduced-motion preference; requires --p4-motion-smoke."});
#ifdef RENTAL_STUDIO_P7_SQL_LAB
    parser.addOption({"p7-fictional-sql-lab", "Explicit isolated generated-fictional SQLite Lab inside real Qt Quick app. No commercial data."});
    parser.addOption({"p7-fictional-sql-app-smoke", "CI-only native test of the real app's isolated fictional SQLite Lab."});
#endif
    parser.process(app);
    const bool portfolioDemoCapture = parser.isSet("portfolio-demo-capture");
    if (portfolioDemoCapture && parser.optionNames().size() != 1) {
        qCritical() << "Portfolio demo capture must run by itself";
        return 1;
    }
    // Load bundled font only on explicit request. No silent font substitution
    // and no download, file IO, or network access at application runtime.
    QString r2FontTrialFamily;
    if(parser.isSet("r2-font-trial-smoke") && !parser.isSet("r2-font-inter")) {
        qCritical()<<"R2 font trial smoke requires the opt-in Inter font";
        return 1050;
    }
    if(parser.isSet("r2-font-inter")) {
#ifdef RENTAL_STUDIO_R2_INTER_TRIAL
        const int fontId=QFontDatabase::addApplicationFont(
            QStringLiteral(":/r2-fonts/InterVariable.ttf"));
        if(fontId<0 || QFontDatabase::applicationFontFamilies(fontId).isEmpty()) {
            qCritical()<<"R2 pinned Inter font could not load from Qt resources";
            return 1051;
        }
        r2FontTrialFamily=QFontDatabase::applicationFontFamilies(fontId).first();
        if(!r2FontTrialFamily.contains(QStringLiteral("Inter"),Qt::CaseInsensitive)) {
            qCritical()<<"R2 source is not an Inter family"<<r2FontTrialFamily;
            return 1052;
        }
        QFont inter=app.font();
        inter.setFamily(r2FontTrialFamily);
        app.setFont(inter);
#else
        qCritical()<<"R2 font trial is not built into this offline executable";
        return 1053;
#endif
    }
    bool p7SqlLabEnabled=false;
    bool p7SqlLabSmoke=false;
#ifdef RENTAL_STUDIO_P7_SQL_LAB
    p7SqlLabSmoke=parser.isSet("p7-fictional-sql-app-smoke");
    p7SqlLabEnabled=p7SqlLabSmoke || parser.isSet("p7-fictional-sql-lab");
    if(p7SqlLabEnabled &&
       (parser.optionNames().size()!=(p7SqlLabSmoke && parser.isSet("compact")?2:1))) {
        qCritical()<<"P7 fictional Lab cannot be combined with other options";
        return 600;
    }
#endif
    const bool p6Parent=parser.isSet("p6-restart-flow");
    const bool p6Seed=parser.isSet("p6-restart-child-seed");
    const bool p6Update=parser.isSet("p6-restart-child-update");
    const bool p6Final=parser.isSet("p6-restart-child-final");
    const bool p6Child=p6Seed||p6Update||p6Final;
    const bool p6JourneyParent=parser.isSet("p6-integrated-flow");
    const bool p6JourneySeed=parser.isSet("p6-integrated-child-seed");
    const bool p6JourneyFinish=parser.isSet("p6-integrated-child-finish");
    const bool p6JourneyVerify=parser.isSet("p6-integrated-child-verify");
    const bool p6JourneyChild=p6JourneySeed||p6JourneyFinish||p6JourneyVerify;
    if(int(p6JourneyParent)+int(p6JourneySeed)+int(p6JourneyFinish)+
           int(p6JourneyVerify)+int(p6Parent)+int(p6Seed)+
           int(p6Update)+int(p6Final)>1 ||
       ((p6JourneyParent||p6JourneyChild) &&
           parser.optionNames().size()!=(parser.isSet("compact")?2:1))) {
        qCritical()<<"P6 integrated flow switches may not be combined";
        return 470;
    }
    if(int(p6Parent)+int(p6Seed)+int(p6Update)+int(p6Final)>1 ||
       ((p6Parent||p6Child) &&
        parser.optionNames().size()!=(parser.isSet("compact")?2:1))) {
        qCritical()<<"P6 test phases cannot combine with other options";
        return 436;
    }
    const bool seedTest=parser.isSet("p5-session-seed-test");
    const bool reopenTest=parser.isSet("p5-session-reopen-test");
    const bool uiSeedTest=parser.isSet("p5-ui-session-seed-test");
    const bool uiReopenTest=parser.isSet("p5-ui-session-reopen-test");
    const bool uiCleanupTest=parser.isSet("p5-ui-session-clean-test");
    const bool cleanupTest=parser.isSet("p5-session-clean-test") || uiCleanupTest;
    const bool tamperTest=parser.isSet("p5-session-tamper-test");
    const bool lockProbeTest=parser.isSet("p5-session-lock-probe-test");
    const bool failureTest=parser.isSet("p5-ui-write-failure-test");
    const bool sessionTest=seedTest || reopenTest || uiSeedTest ||
                           uiReopenTest || cleanupTest || tamperTest || lockProbeTest;
    const bool persistentDemo=parser.isSet("persistent-fictional-demo") || sessionTest;
    const bool portfolioShowcase=parser.optionNames().isEmpty() ||
        parser.isSet("portfolio-catalog-smoke") || portfolioDemoCapture;
    if(sessionTest) {
        // The UI and non-UI fixture sequences have separate test roots.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName(
            (uiSeedTest||uiReopenTest||uiCleanupTest)
              ? "RentalStudioP5QmlSyntheticTestOnly"
              : "RentalStudioP5SyntheticTestOnly");
    }
    if(sessionTest &&
       int(seedTest)+int(reopenTest)+int(uiSeedTest)+int(uiReopenTest)+
       int(cleanupTest)+int(tamperTest)+int(lockProbeTest)!=1) {
        qCritical()<<"Only one P5 synthetic CI phase is allowed"; return 308;
    }
    // Never run a legacy test which could intentionally mutate fixtures
    // against the fixed persistent demo directory. Explicit mode only.
    if(persistentDemo && parser.optionNames().size()!=1) {
        qCritical()<<"Persistent fictional mode cannot combine with other switches";
        return 309;
    }

    // No source-adjacent storage or external/customer-data CLI argument.
    // For a portable review kit prefer only bundled fake fixtures.
    // Otherwise fall back to source-tree fake fixtures for Qt Creator/CI.
    // No arbitrary --data-dir or real customer path can be supplied.
    const QDir bundled(QDir(QCoreApplication::applicationDirPath()).filePath("demo"));
    const QDir source(bundled.exists()
                      ? bundled.absolutePath()
                      : QStringLiteral(RENTAL_STUDIO_DEMO_DIR));
    QTemporaryDir disposable;
    if (!disposable.isValid()) {
        qCritical() << "Cannot create isolated fixture directory";
        return 2;
    }
    QByteArray beforeVehicles, beforeRentals;
    const QString vehicles = source.filePath("vehicles.txt");
    const QString rentals = source.filePath("rentals.txt");
    if (!readFile(vehicles, &beforeVehicles) ||
        !readFile(rentals, &beforeRentals)) {
        qCritical() << "Cannot read only fictional demo fixtures";
        return 3;
    }
    // Reject swapped fixtures. Build-time hashes are taken from the exact
    // committed demo files; no customer data is ever accidentally imported.
    const QByteArray vehicleHash =
        QCryptographicHash::hash(beforeVehicles, QCryptographicHash::Sha256).toHex();
    const QByteArray rentalHash =
        QCryptographicHash::hash(beforeRentals, QCryptographicHash::Sha256).toHex();
    if (vehicleHash != QByteArray(RENTAL_STUDIO_DEMO_VEHICLES_SHA256) ||
        rentalHash != QByteArray(RENTAL_STUDIO_DEMO_RENTALS_SHA256)) {
        qCritical() << "P2 fixture mismatch: refusing non-demo files";
        return 3;
    }
    // A disposable parent owns the entire cross-process fixture lifetime;
    // child processes may only use that exact synthetic test working folder.
    const QByteArray p6Marker=QByteArray("RENTAL_STUDIO_P6_PROCESS_TEST_ONLY_V1\\n")+
                              vehicleHash+"\\n"+rentalHash+"\\n";

    // P6-M is a separate allowlisted integration journey, never a general
    // data-directory option and never a mutation of committed fake fixtures.
    const QByteArray journeyMarker=QByteArray(
        "RENTAL_STUDIO_P6_INTEGRATED_TEST_ONLY_V1\n")+
        vehicleHash+"\n"+rentalHash+"\n";
    if(p6JourneyParent) {
        QTemporaryDir run(QDir(QDir::tempPath()).filePath(
            QStringLiteral("rental-studio-p6-integrated-XXXXXX")));
        if(!run.isValid() ||
           !QFile::copy(vehicles,run.filePath("vehicles.txt")) ||
           !QFile::copy(rentals,run.filePath("rentals.txt"))) {
            qCritical()<<"P6-M parent could not isolate fictional source";
            return 471;
        }
        QFile token(run.filePath("p6-integrated.marker"));
        if(!token.open(QIODevice::WriteOnly) ||
           token.write(journeyMarker)!=journeyMarker.size())return 472;
        token.close();
        const QStringList stages={
            QStringLiteral("--p6-integrated-child-seed"),
            QStringLiteral("--p6-integrated-child-finish"),
            QStringLiteral("--p6-integrated-child-verify")};
        for(const QString &stage:stages) {
            QProcess child;
            child.setProgram(QCoreApplication::applicationFilePath());
            child.setWorkingDirectory(run.path());
            QStringList args{stage};
            if(parser.isSet("compact"))args<<"--compact";
            child.setArguments(args);
            child.start();
            if(!child.waitForStarted(10000) ||
               !child.waitForFinished(45000) ||
               child.exitStatus()!=QProcess::NormalExit ||
               child.exitCode()!=0) {
                if(child.state()!=QProcess::NotRunning) {
                    child.kill();child.waitForFinished(3000);
                }
                qCritical()<<"P6-M independent native stage FAILED"<<stage
                           <<child.errorString()<<child.readAllStandardError()
                           <<child.readAllStandardOutput();
                return 473;
            }
            qInfo()<<"P6-M separate native stage PASS"<<stage
                   <<child.readAllStandardOutput()<<child.readAllStandardError();
        }
        QByteArray unchangedV,unchangedR;
        if(!readFile(vehicles,&unchangedV)||unchangedV!=beforeVehicles ||
           !readFile(rentals,&unchangedR)||unchangedR!=beforeRentals)
            return 474;
        qInfo()<<"P6-M real three-process synthetic lifecycle integration PASS"
               <<(parser.isSet("compact")?"1280x720":"1500x860");
        return 0;
    }
    if(p6JourneyChild) {
        // Never load any child-provided records before validating the exact
        // synthetic workspace marker, names and stage-specific record allowlist.
        const QDir root(QDir::currentPath());
        const QFileInfo folder(root.absolutePath());
        const QStringList names=root.entryList(
            QDir::AllEntries|QDir::Hidden|QDir::NoDotAndDotDot,QDir::Name);
        const QFileInfo mark(root.filePath("p6-integrated.marker"));
        const QFileInfo fv(root.filePath("vehicles.txt"));
        const QFileInfo fr(root.filePath("rentals.txt"));
        QByteArray marker,vbytes,rbytes;
        if(!folder.fileName().startsWith("rental-studio-p6-integrated-") ||
           !folder.isDir()||folder.isSymLink() ||
           names!=QStringList({"p6-integrated.marker","rentals.txt","vehicles.txt"}) ||
           !mark.isFile()||mark.isSymLink()||
           !fv.isFile()||fv.isSymLink()||
           !fr.isFile()||fr.isSymLink()||
           !readFile(mark.filePath(),&marker)||marker!=journeyMarker||
           !readFile(fv.filePath(),&vbytes)||
           !readFile(fr.filePath(),&rbytes)||
           vbytes.size()>8192||rbytes.size()>8192) {
            qCritical()<<"P6-M child rejected unsafe or non-fictional workspace";
            return 475;
        }
        const auto lines=[](QByteArray bytes) {
            QString s=QString::fromUtf8(bytes);
            s.replace(QStringLiteral("\r\n"),QStringLiteral("\n"));
            return s.split(QLatin1Char('\n'),Qt::SkipEmptyParts);
        };
        QStringList expectedV=lines(beforeVehicles);
        QStringList expectedR=lines(beforeRentals);
        if(p6JourneySeed) {
            if(vbytes!=beforeVehicles||rbytes!=beforeRentals) {
                qCritical()<<"P6-M seed not exact protected demo copy";
                return 476;
            }
        } else if(p6JourneyFinish) {
            // The new record must contain only the exact fictional contact
            // approved by this CI-only test. Verify its complete serialized
            // form before RentalSystem can load it, including domain dates.
            const QString today=QDate::currentDate().toString(Qt::ISODate);
            const QString due=QDate::currentDate().addDays(2).toString(Qt::ISODate);
            // Booking persists the new rental record; vehicle occupancy
            // is reconstructed from the rental ledger at process startup.
            // The Fleet inventory file keeps its original available flag.
            expectedV<<"Car|C004|Demo Motors|Orion|420.00|0|4";
            expectedR<<QStringLiteral("Rental|R003|Fictional Journey|0000000000|C004|%1|%2||2|420.00|840.00|0|0")
                        .arg(today,due);
            const QStringList actualV=lines(vbytes);
            const QStringList actualR=lines(rbytes);
            if(actualV!=expectedV || actualR!=expectedR) {
                // No arbitrary fixture text or contacts are printed. The
                // hashes pinpoint which exact allowlist field differs.
                const auto diagnose=[](const QStringList &actual,
                                       const QStringList &expected,
                                       const char *kind) {
                    qCritical()<<"P6-M allowlist" <<kind
                               <<"rows"<<actual.size()<<expected.size();
                    const int count=std::max(actual.size(),expected.size());
                    for(int i=0;i<count;++i) {
                        const QByteArray a=i<actual.size()?actual.at(i).toUtf8():QByteArray{};
                        const QByteArray e=i<expected.size()?expected.at(i).toUtf8():QByteArray{};
                        if(a!=e)
                            qCritical()<<"P6-M row mismatch"<<kind<<i
                                       <<"lengths"<<a.size()<<e.size()
                                       <<"sha12"<<QCryptographicHash::hash(
                                           a,QCryptographicHash::Sha256).toHex().left(12)
                                       <<QCryptographicHash::hash(
                                           e,QCryptographicHash::Sha256).toHex().left(12);
                    }
                };
                diagnose(actualV,expectedV,"fleet");
                diagnose(actualR,expectedR,"rentals");
                return 477;
            }
        } else {
            if(lines(vbytes)!=expectedV||lines(rbytes)!=expectedR) {
                qCritical()<<"P6-M final state differs from original fictional seed";
                return 478;
            }
        }
    }
    if(p6Parent) {
        QTemporaryDir run(QDir(QDir::tempPath()).filePath(
            QStringLiteral("rental-studio-p6-restart-XXXXXX")));
        if(!run.isValid() ||
           !QFile::copy(vehicles,run.filePath("vehicles.txt")) ||
           !QFile::copy(rentals,run.filePath("rentals.txt"))) {
            qCritical()<<"P6 restart parent could not isolate verified fictional sources";
            return 437;
        }
        QFile token(run.filePath("p6.marker"));
        if(!token.open(QIODevice::WriteOnly) ||
           token.write(p6Marker)!=p6Marker.size())return 438;
        token.close();
        const QStringList stages={
            QStringLiteral("--p6-restart-child-seed"),
            QStringLiteral("--p6-restart-child-update"),
            QStringLiteral("--p6-restart-child-final")};
        for(const QString &stage: stages) {
            QProcess child;
            child.setProgram(QCoreApplication::applicationFilePath());
            child.setWorkingDirectory(run.path());
            QStringList args={stage};
            if(parser.isSet("compact"))args<<"--compact";
            child.setArguments(args);
            child.start();
            if(!child.waitForStarted(10000) ||
               !child.waitForFinished(45000) ||
               child.exitStatus()!=QProcess::NormalExit ||
               child.exitCode()!=0) {
                if(child.state()!=QProcess::NotRunning) {
                    child.kill(); child.waitForFinished(3000);
                }
                qCritical()<<"P6 separate native process failed"<<stage
                           <<child.errorString()<<child.readAllStandardError()
                           <<child.readAllStandardOutput();
                return 439;
            }
            qInfo()<<"P6 separate process passed"<<stage
                   <<child.readAllStandardOutput()<<child.readAllStandardError();
        }
        QByteArray unchangedV,unchangedR;
        if(!readFile(vehicles,&unchangedV)||unchangedV!=beforeVehicles ||
           !readFile(rentals,&unchangedR)||unchangedR!=beforeRentals) {
            qCritical()<<"P6 original SHA-protected fixtures changed";
            return 440;
        }
        qInfo()<<"P6 three real native executable launches, restart parity PASS"
               <<(parser.isSet("compact")?"1280x720":"1500x860");
        return 0; // QTemporaryDir cleans the unique temporary test fixture.
    }
    if(p6Child) {
        const QDir root(QDir::currentPath());
        const QFileInfo folder(root.absolutePath());
        const QStringList names=root.entryList(QDir::AllEntries|QDir::Hidden|
                                          QDir::NoDotAndDotDot,QDir::Name);
        QFileInfo markerFile(root.filePath("p6.marker"));
        QFileInfo vehiclesFile(root.filePath("vehicles.txt"));
        QFileInfo rentalsFile(root.filePath("rentals.txt"));
        QByteArray actualMarker,childV,childR;
        if(!folder.fileName().startsWith("rental-studio-p6-restart-") ||
           !folder.isDir() || folder.isSymLink() ||
           names!=QStringList({"p6.marker","rentals.txt","vehicles.txt"}) ||
           !markerFile.isFile() || markerFile.isSymLink() ||
           !vehiclesFile.isFile() || vehiclesFile.isSymLink() ||
           !rentalsFile.isFile() || rentalsFile.isSymLink() ||
           !readFile(markerFile.filePath(),&actualMarker) ||
           actualMarker!=p6Marker ||
           !readFile(vehiclesFile.filePath(),&childV) ||
           !readFile(rentalsFile.filePath(),&childR) ||
           childV.size()>8192 || childR.size()>8192) {
            qCritical()<<"P6 test child refused foreign/unsafe working directory";
            return 441;
        }
        // Only this one exact fictional scenario is permitted. Never use a
        // command-line data path or the P5 fixed persistent session.
        if(p6Seed && (childV!=beforeVehicles || childR!=beforeRentals))
            return 442;
        if(!p6Seed) {
            // Exact stage allowlists, not substring checks. A foreign record
            // with real contact details must never reach RentalSystem.
            const auto lines=[](QByteArray bytes) {
                QString content=QString::fromUtf8(bytes);
                content.replace(QStringLiteral("\r\n"),QStringLiteral("\n"));
                return content.split(QLatin1Char('\n'),Qt::SkipEmptyParts);
            };
            const QStringList actualV=lines(childV);
            const QStringList actualR=lines(childR);
            QStringList expectedV=lines(beforeVehicles);
            QStringList expectedR=lines(beforeRentals);
            expectedV<<"Car|C004|Demo Motors|Orion|420.00|0|4"
                     <<"Motorcycle|M003|Demo Moto|Nova|300.00|0|450";
            if(p6Update) {
                if(expectedR.size()!=2 || !expectedR.at(1).endsWith("|1|0"))
                    return 443;
                expectedR[1].chop(1);
                expectedR[1]+="1"; // Original fictional R002 now archived.
            } else {
                expectedV.removeAll("Car|C001|Toyota|Corolla|450.00|0|4");
                expectedV.removeAll("Car|C003|Hyundai|i20|400.00|0|4");
                expectedR.removeLast(); // Only R001 remains after exact delete.
            }
            if(actualV!=expectedV || actualR!=expectedR) {
                qCritical()<<"P6 synthetic allowlist mismatch"
                           <<"vehicles"<<actualV.size()<<expectedV.size()
                           <<"match"<<(actualV==expectedV)
                           <<"rentals"<<actualR.size()<<expectedR.size()
                           <<"match"<<(actualR==expectedR);
                // Only hashes and row positions, never arbitrary file content
                // from a possibly tampered test working directory.
                for(int i=0;i<actualV.size()&&i<expectedV.size();++i) {
                    if(actualV.at(i)!=expectedV.at(i)) {
                        qCritical()<<"P6 different Fleet row"<<i
                           <<"len"<<actualV.at(i).size()<<expectedV.at(i).size()
                           <<"actual-sha"<<QCryptographicHash::hash(
                              actualV.at(i).toUtf8(),QCryptographicHash::Sha256).toHex().left(12)
                           <<"expected-sha"<<QCryptographicHash::hash(
                              expectedV.at(i).toUtf8(),QCryptographicHash::Sha256).toHex().left(12);
                    }
                }
                for(int i=0;i<actualR.size()&&i<expectedR.size();++i) {
                    if(actualR.at(i)!=expectedR.at(i)) {
                        qCritical()<<"P6 different rental row"<<i
                           <<"len"<<actualR.at(i).size()<<expectedR.at(i).size()
                           <<"actual-sha"<<QCryptographicHash::hash(
                              actualR.at(i).toUtf8(),QCryptographicHash::Sha256).toHex().left(12)
                           <<"expected-sha"<<QCryptographicHash::hash(
                              expectedR.at(i).toUtf8(),QCryptographicHash::Sha256).toHex().left(12);
                    }
                }
                return 443;
            }
        }
    }
    QString dataDirectory=disposable.path();
    const QByteArray marker=QByteArray("RENTAL_STUDIO_SYNTHETIC_ONLY_V1\n") +
                            vehicleHash + "\n" + rentalHash + "\n";
    if(persistentDemo) {
        QString reason;
        if(cleanupTest) {
            if(!P5SyntheticSession::cleanForTest(marker,&reason)) {
                qCritical()<<"Synthetic cleanup refused"<<reason; return 310;
            }
            qInfo()<<"P5 identified test session cleaned PASS"; return 0;
        }
        if(!P5SyntheticSession::open(beforeVehicles,beforeRentals,marker,&reason)) {
            if(lockProbeTest && reason==
               QStringLiteral("Fictional session already open in another process")) {
                qInfo()<<"P5 simultaneous fictional process correctly refused PASS";
                return 0;
            }
            qCritical()<<"Synthetic-only session refused"<<reason;
            return 311;
        }
        if(lockProbeTest) {
            qCritical()<<"P5 second process incorrectly acquired the fictional ledger";
            return 380;
        }
        dataDirectory=P5SyntheticSession::location();
    } else if(!p6Child && !p6JourneyChild &&
              (!QFile::copy(vehicles, disposable.filePath("vehicles.txt")) ||
               !QFile::copy(rentals, disposable.filePath("rentals.txt")))) {
        qCritical()<<"Cannot create disposable demo copies"; return 3;
    }
    if(p6Child||p6JourneyChild)dataDirectory=QDir::currentPath();
    if(portfolioShowcase) {
        QFile catalog(QStringLiteral(":/portfolio-demo/vehicles.txt"));
        if(!catalog.open(QIODevice::ReadOnly)) {
            qCritical()<<"Missing embedded fictional portfolio catalogue";return 711;
        }
        const QByteArray seed=catalog.readAll();
        const QByteArray sha=QCryptographicHash::hash(
            seed,QCryptographicHash::Sha256).toHex();
        if(sha!=QByteArray(RENTAL_STUDIO_PORTFOLIO_VEHICLES_SHA256)) {
            qCritical()<<"Portfolio catalogue fingerprint mismatch";return 712;
        }
        // Isolated temporary session only; never change the source fixtures.
        QFile copy(QDir(dataDirectory).filePath(QStringLiteral("vehicles.txt")));
        if(!copy.open(QIODevice::WriteOnly|QIODevice::Truncate) ||
           copy.write(seed)!=seed.size()) {
            qCritical()<<"Unable to seed disposable portfolio fleet";return 713;
        }
        copy.close();
    }
    RentalSystem system(
        dataDirectory,
        failureTest ? std::static_pointer_cast<RentalStorage>(
            std::make_shared<P5RejectWriteStorage>())
            : std::shared_ptr<RentalStorage>{});
    FleetController fleet(system);
    // Photo sidecars live only in the already verified fictional session root.
    // The default demo root is disposed on process exit. Domain records are
    // not modified to contain arbitrary file paths or customer media.
    FleetPhotoStore fleetPhotos(dataDirectory);
    fleetPhotos.setPortfolioShowcase(portfolioShowcase);
    // C++ RentalSystem is the identity authority. No default photo is
    // supplied for an unknown ID, a stale ID, or a mismatched vehicle model.
    const FleetPhotoStore::IdentityLookup livePhotoIdentity=[&system](
        const QString &id)->QPair<QString,QString> {
        const Vehicle *vehicle=system.findVehicleById(id);
        return vehicle
            ? QPair<QString,QString>{vehicle->getBrand(),vehicle->getModel()}
            : QPair<QString,QString>{};
    };
    fleetPhotos.setVehicleIdentityLookup(livePhotoIdentity);
    // Photo replacement is explicitly unavailable in the fixed fictional session.
    fleetPhotos.setReplacementEnabled(!persistentDemo);
    FleetAddController fleetAdmin(system,!persistentDemo,
        [&fleet](const QString &id) {
            fleet.setQuery(QString{});
            fleet.setStatus(QStringLiteral("All"));
            fleet.refreshAfterCommit();
            fleet.selectById(id);
        });
    fleetAdmin.setPhotoStore(&fleetPhotos);
    FleetRemoveController fleetRemove(system,!persistentDemo,
        [&fleet,&fleetPhotos] {
            // Successful domain removal first, then erase its optional
            // fictional media to prevent an ID reused later showing stale art.
            fleetPhotos.forgetVehicle(fleet.selectedId());
            fleet.clearSelection();
            fleet.refreshAfterCommit();
        });
    RentalReadService rentalRead(system);
    RentalListModel rentalModel(rentalRead);
    RentalReturnController rentalActions(system, rentalRead, rentalModel,
                                         [&fleet]{ fleet.refreshAfterCommit(); });
    RentalBookingController bookingActions(system, rentalModel,
                                            [&fleet]{ fleet.refreshAfterCommit(); });
    bookingActions.setPersistentFictionalOnly(persistentDemo);
    RentalArchiveController archiveActions(system, rentalRead, rentalModel);
    P6ArchivedDeleteController archivedDelete(system,rentalRead,rentalModel,!persistentDemo);
#ifdef RENTAL_STUDIO_P7_SQL_LAB
    // D1 owner authorization is limited to a separate fixed-fictional
    // SQLite workspace in this actual Qt Quick executable. The other three
    // workspaces remain QSaveFile-backed and are NEVER pointed at this repo.
    std::unique_ptr<p7fiction::PreviewController> p7SqlController;
    // Test-only provider fault toggles; never exposed to QML or user input.
    bool p7RefuseFleetReads=false, p7RefuseRentalReads=false;
    bool p7RefuseOperationsReads=false;
    std::unique_ptr<p7fiction::FictionalOperationsSnapshot> p7SqlOverview;
    std::unique_ptr<p7fiction::FictionalFleetSnapshot> p7SqlFleet;
    std::unique_ptr<RentalListModel> p7SqlRentals;
    if(p7SqlLabEnabled) {
        p7SqlController=std::make_unique<p7fiction::PreviewController>();
        if(!p7SqlController->ready()) {
            qCritical()<<"P7 generated-fictional SQL Lab unavailable";
            return 601;
        }
        auto *controller=p7SqlController.get();
        p7SqlOverview=std::make_unique<p7fiction::FictionalOperationsSnapshot>(
            [controller,&p7RefuseOperationsReads]() -> std::optional<p7fiction::FleetRows> {
                if(p7RefuseOperationsReads)return {};
                const auto *repo=controller->testOnlyReadRepository();
                return repo?repo->fleetRows():std::nullopt;
            });
        p7SqlFleet=std::make_unique<p7fiction::FictionalFleetSnapshot>(
            [controller,&p7RefuseFleetReads]() -> std::optional<QList<FleetVehicleSummary>> {
                if(p7RefuseFleetReads)return {};
                const auto *repo=controller->testOnlyReadRepository();
                return repo?p7fiction::ReadProjection(*repo).fleet():std::nullopt;
            });
        p7SqlRentals=std::make_unique<RentalListModel>(
            [controller,&p7RefuseRentalReads]() -> std::optional<QList<RentalRecordSummary>> {
                if(p7RefuseRentalReads)return {};
                const auto *repo=controller->testOnlyReadRepository();
                return repo?p7fiction::ReadProjection(*repo).rentals(QDate(2026,11,2)):
                            std::nullopt;
            });
        QObject::connect(controller,&p7fiction::PreviewController::committed,
                         p7SqlOverview.get(),&p7fiction::FictionalOperationsSnapshot::refresh);
        QObject::connect(controller,&p7fiction::PreviewController::committed,
                         p7SqlFleet.get(),&p7fiction::FictionalFleetSnapshot::refresh);
        QObject::connect(controller,&p7fiction::PreviewController::committed,
                         p7SqlRentals.get(),&RentalListModel::refresh);
        if(!p7SqlOverview->snapshotAvailable() ||
           !p7SqlFleet->snapshotAvailable() ||
           !p7SqlRentals->snapshotAvailable())return 602;
    }
#endif
    if(sessionTest) {
        const QString ledger=QDir(dataDirectory).filePath("rentals.txt");
        if(seedTest) {
            if(rentalModel.activeCount()!=1 ||
               fleet.availableCount()!=4 ||
               system.findRentalById("R003") ||
               !bookingActions.preview("C001",3) ||
               !bookingActions.confirm("Fictional Demo Session","0000000000") ||
               bookingActions.lastRentalId()!="R003" ||
               rentalModel.activeCount()!=2 ||
               fleet.availableCount()!=3) {
                qCritical()<<"P5 first native process did not save fictional rental";
                return 312;
            }
            QByteArray changed;
            if(!readFile(ledger,&changed)||changed==beforeRentals) return 313;
            qInfo()<<"P5 actual process one saved fictional rental PASS";
            return 0;
        }
        if(tamperTest) {
            // A second real executable must not load and write from the same
            // in-memory rental snapshot while this first instance is running.
            // The lock must remain held beyond initial fixture verification.
            QProcess contender;
            contender.setProgram(QCoreApplication::applicationFilePath());
            contender.setArguments({QStringLiteral("--p5-session-lock-probe-test")});
            contender.start();
            if(!contender.waitForStarted(5000)) {
                qCritical()<<"P5 second process could not start";
                return 381;
            }
            if(!contender.waitForFinished(10000)) {
                contender.kill();
                contender.waitForFinished(5000);
                qCritical()<<"P5 contention probe timed out";
                return 382;
            }
            if(contender.exitStatus()!=QProcess::NormalExit ||
               contender.exitCode()!=0) {
                qCritical()<<"P5 concurrent process did not refuse held session"
                           <<contender.readAllStandardError()
                           <<contender.readAllStandardOutput();
                return 383;
            }
            const QString extra=QDir(dataDirectory).filePath("UNTRUSTED_RECORD.txt");
            QFile unknown(extra);
            if(!unknown.open(QIODevice::WriteOnly) ||
               unknown.write("not fixture data")<0) {
                qCritical()<<"P5 couldn't stage disposable foreign-file negative test";
                return 318;
            }
            unknown.close();
            QString reason;
            const bool acceptedForeign=P5SyntheticSession::open(
                beforeVehicles,beforeRentals,marker,&reason);
            if(!QFile::remove(extra) || acceptedForeign) {
                qCritical()<<"P5 unrecognized persistent directory file accepted";
                return 319;
            }
            QByteArray beforeTamper;
            if(!readFile(ledger,&beforeTamper))return 320;
            const QByteArray foreignRecord=
                "Rental|R999|UNTRUSTED NONFICTIONAL|0000000000|C003|"
                "2026-11-01|2026-11-04||3|400.00|1200.00|0|0\\n";
            QSaveFile poisoned(ledger);
            if(!poisoned.open(QIODevice::WriteOnly) ||
               poisoned.write(beforeTamper+foreignRecord)!=
                   beforeTamper.size()+foreignRecord.size() ||
               !poisoned.commit())return 321;
            const bool acceptedContact=P5SyntheticSession::open(
                beforeVehicles,beforeRentals,marker,&reason);
            QSaveFile restored(ledger);
            if(!restored.open(QIODevice::WriteOnly) ||
               restored.write(beforeTamper)!=beforeTamper.size() ||
               !restored.commit() ||
               acceptedContact ||
               !P5SyntheticSession::open(
                   beforeVehicles,beforeRentals,marker,&reason)) {
                qCritical()<<"P5 foreign contact or restore guard failed"<<reason;
                return 322;
            }
            // A second active contract on the already-rented fictional
            // Ranger C002 is individually well-formed but impossible for
            // the domain to load without silently dropping an active row.
            // Reject the entire session instead of showing a partial ledger.
            const QByteArray conflictingActive =
                "Rental|R998|Fictional Demo Session|0000000000|C002|"
                "2026-11-01|2026-11-04||3|700.00|2100.00|0|0\n";
            QSaveFile conflicting(ledger);
            if(!conflicting.open(QIODevice::WriteOnly) ||
               conflicting.write(beforeTamper+conflictingActive)!=
                   beforeTamper.size()+conflictingActive.size() ||
               !conflicting.commit())return 377;
            const bool acceptedConflict=P5SyntheticSession::open(
                beforeVehicles,beforeRentals,marker,&reason);
            QSaveFile unconflicted(ledger);
            if(!unconflicted.open(QIODevice::WriteOnly) ||
               unconflicted.write(beforeTamper)!=beforeTamper.size() ||
               !unconflicted.commit())return 378;
            QByteArray recovered;
            if(acceptedConflict ||
               !P5SyntheticSession::open(beforeVehicles,beforeRentals,marker,&reason) ||
               !readFile(ledger,&recovered) || recovered!=beforeTamper) {
                qCritical()<<"P5 conflicting active contract was accepted or restore failed"
                           <<reason;
                return 379;
            }
            qInfo()<<"P5 synthetic session refused foreign file/contact and"
                     " conflicting active vehicle while restoring exact bytes PASS";
            return 0;
        }
        if(reopenTest) {
            QByteArray beforeDuplicate,afterDuplicate;
            if(!readFile(ledger,&beforeDuplicate) ||
               rentalModel.activeCount()!=2 ||
               std::abs(rentalModel.activeBookedValue()-2750.0)>0.01 ||
               fleet.availableCount()!=3 || !system.findRentalById("R003") ||
               !system.findVehicleById("C001") ||
               !system.findVehicleById("C001")->getIsRented()) {
                qCritical()<<"P5 new process did not reconstruct ledger/occupancy";
                return 314;
            }
            if(bookingActions.preview("C001",3) ||
               bookingActions.confirm("Real Person","0712345678") ||
               !readFile(ledger,&afterDuplicate) ||
               beforeDuplicate!=afterDuplicate ||
               rentalModel.activeCount()!=2) {
                qCritical()<<"P5 duplicate or non-fictional booking was not refused";
                return 315;
            }
            qInfo()<<"P5 real second executable process recovered R003 and refused duplicate PASS";
            return 0;
        }
    }
    if (parser.isSet("self-test")) {
    const Vehicle *corolla = system.findVehicleById("C001");
    if (!corolla) {
        qCritical() << "Expected fictional Corolla C001 missing";
        return 4;
    }
        bool ok = fleet.totalCount() == 5 && fleet.availableCount() == 4
               && fleet.carCount() == 3 && fleet.motorcycleCount() == 2
               && fleet.rentedCount() == 1
               && qFuzzyCompare(1.0 + corolla->getPricePerDay(), 451.0);
        fleet.setQuery("Honda");
        ok = ok && static_cast<FleetProxy*>(fleet.visibleModel())->rowCount() == 1
                && fleet.carCount() == 3 && fleet.motorcycleCount() == 2
                && fleet.rentedCount() == 1;
        fleet.selectById("M001");
        ok = ok && fleet.selectedId() == "M001";
        fleet.setQuery("");
        fleet.setStatus("Rented");
        // A hidden vehicle must no longer remain in the visible Inspector.
        ok = ok && static_cast<FleetProxy*>(fleet.visibleModel())->rowCount() == 1
                && fleet.selectedId().isEmpty()
                && fleet.carCount() == 3 && fleet.motorcycleCount() == 2
                && fleet.rentedCount() == 1;
        fleet.setSortBy("Rate");
        fleet.setStatus("All");
        fleet.selectById("M001");
        ok = ok && static_cast<FleetProxy*>(fleet.visibleModel())->rowCount() == 5
                && fleet.selectedId() == "M001"
                && fleet.selectedVisibleIndex() >= 0;
        fleet.selectById("C002");
        ok = ok && !fleet.selectedAvailable();
        fleet.clearSelection();
        ok = ok && fleet.selectedId().isEmpty();
        QByteArray afterVehicles, afterRentals;
        ok = ok && readFile(vehicles, &afterVehicles) && readFile(rentals, &afterRentals)
                && beforeVehicles == afterVehicles && beforeRentals == afterRentals;
        if (!ok) {
            qCritical() << "P2 C++ model self-test FAILED";
            return 4;
        }
        qInfo() << "P2 C++ model self-test PASS: 5 fleet, search/status/sort/selection, read-only demo";
        return 0;
    }
    // Only the dedicated synthetic smoke can override the OS preference.
    MotionPolicy motion;
    if (parser.isSet("p4-motion-smoke"))
        motion.setTestPreference(parser.isSet("p4-reduce-motion"));
    app.installNativeEventFilter(&motion);
    QObject::connect(&app, &QGuiApplication::applicationStateChanged,
                     &motion, [&motion](Qt::ApplicationState) {
                         motion.refreshFromSystem();
                     });
    QQuickStyle::setStyle("Basic");
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("fleetphotos"),
                            new FleetPhotoProvider(fleetPhotos));
    engine.rootContext()->setContextProperty("fleetPhotos", &fleetPhotos);
    engine.rootContext()->setContextProperty("motionPolicy", &motion);
    engine.rootContext()->setContextProperty("r2FontTrialFamily",r2FontTrialFamily);
    engine.rootContext()->setContextProperty("persistentFictionalDemo",persistentDemo);
    engine.rootContext()->setContextProperty("portfolioShowcase",portfolioShowcase);
    engine.rootContext()->setContextProperty("fleet", &fleet);
    engine.rootContext()->setContextProperty("fleetAdmin", &fleetAdmin);
    engine.rootContext()->setContextProperty("fleetRemove", &fleetRemove);
    engine.rootContext()->setContextProperty("rentalsModel", &rentalModel);
    engine.rootContext()->setContextProperty("rentalActions", &rentalActions);
    engine.rootContext()->setContextProperty("bookingActions", &bookingActions);
    engine.rootContext()->setContextProperty("archiveActions", &archiveActions);
    engine.rootContext()->setContextProperty("archivedDelete", &archivedDelete);
    engine.rootContext()->setContextProperty("p7SqlLabEnabled",p7SqlLabEnabled);
#ifdef RENTAL_STUDIO_P7_SQL_LAB
    engine.rootContext()->setContextProperty("p7SqlController",
                                              p7SqlController.get());
    engine.rootContext()->setContextProperty("p7SqlOverview",p7SqlOverview.get());
    engine.rootContext()->setContextProperty("p7SqlFleet",p7SqlFleet.get());
    engine.rootContext()->setContextProperty("p7SqlRentals",p7SqlRentals.get());
#else
    engine.rootContext()->setContextProperty("p7SqlController",static_cast<QObject*>(nullptr));
    engine.rootContext()->setContextProperty("p7SqlOverview",static_cast<QObject*>(nullptr));
    engine.rootContext()->setContextProperty("p7SqlFleet",static_cast<QObject*>(nullptr));
    engine.rootContext()->setContextProperty("p7SqlRentals",static_cast<QObject*>(nullptr));
#endif
    engine.loadFromModule("RentalStudioSpike", "Main");
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Qt Quick did not load the P2 window";
        return 5;
    }
    // FUNC-D1: keep a genuine operator launch usable at Windows display
    // scaling. QScreen availableGeometry is in logical Qt pixels and excludes
    // the taskbar. Preserve QML's 1024x660 minimum for tiny/unusable screens;
    // this does NOT claim physical Windows 100/125/150% acceptance.
    auto *startupWindow=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if(!startupWindow || !startupWindow->screen()) {
        qCritical()<<"No native screen for Rental Studio desktop window";
        return 600;
    }
    const QRect startupArea=startupWindow->screen()->availableGeometry();
    const int startupW=std::clamp(startupArea.width()-24,1024,1500);
    const int startupH=std::clamp(startupArea.height()-12,660,860);
    startupWindow->resize(startupW,startupH);
    // Center only inside the actual Qt available desktop rect, including
    // multi-monitor offsets and platform/taskbar work-area reservations.
    startupWindow->setPosition(startupArea.x()+
            std::max(0,(startupArea.width()-startupW)/2),
        startupArea.y()+std::max(0,(startupArea.height()-startupH)/2));
    if (portfolioDemoCapture) {
        QTimer::singleShot(800, &app, [&] {
            const bool ok = capturePortfolioDemo(startupWindow, fleet, rentalModel,
                                                 bookingActions, system);
            if (ok)
                qInfo() << "Portfolio demo: captured fictional booking and return";
            else
                qCritical() << "Portfolio demo capture failed";
            app.exit(ok ? 0 : 1);
        });
        return app.exec();
    }
    // Normal Direction A launch starts in Overview (workspace 0).
    // Historical P2 Fleet-only smokes explicitly open Fleet first, without
    // changing any real operator navigation, selection or domain records.
    if (parser.isSet("ui-smoke") || parser.isSet("input-smoke") ||
        parser.isSet("arrows-smoke") || parser.isSet("focus-edges-smoke")) {
        startupWindow->setProperty("workspace", 1);
        QCoreApplication::processEvents();
        if (parser.isSet("ui-smoke"))
            QTest::qWait(100);
    }
    if(parser.isSet("portfolio-catalog-smoke")) {
        startupWindow->resize(parser.isSet("compact")?1280:1500,
                              parser.isSet("compact")?720:860);
        QTimer::singleShot(800,&app,[&,startupWindow] {
            QByteArray originalVehicles,originalRentals;
            const bool protectedBytes=
                readFile(vehicles,&originalVehicles) &&
                readFile(rentals,&originalRentals) &&
                originalVehicles==beforeVehicles &&
                originalRentals==beforeRentals;
            if(!protectedBytes || fleet.totalCount()!=131 ||
               fleet.carCount()!=121 || fleet.motorcycleCount()!=10 ||
               fleet.availableCount()!=130 || fleet.rentedCount()!=1 ||
               rentalModel.count()!=2 || !fleetPhotos.hasPhoto("C001") ||
               !fleetPhotos.hasPhoto("C004") ||
               !fleetPhotos.hasPhoto("C005") ||
               !fleetPhotos.hasPhoto("M001") ||
               !fleetPhotos.hasPhoto("C012") ||
               !fleetPhotos.hasPhoto("C091") ||
               fleetPhotos.hasPhoto("C017") ||
               fleetPhotos.hasPhoto("C121") ||
               fleetPhotos.hasPhoto("C019") ||
               fleetPhotos.hasPhoto("C022") ||
               fleetPhotos.hasPhoto("C099") ||
               fleetPhotos.hasPhoto("C103") ||
               fleetPhotos.hasPhoto("C114") ||
               fleetPhotos.hasPhoto("C117") ||
               fleetPhotos.hasPhoto("C118") ||
               fleetPhotos.hasPhoto("C120")) {
                qCritical()<<"Portfolio source, counts or base photo invalid";
                app.exit(714);return;
            }
            // Real 131-vehicle Overview showcase, not the five-record
            // regression fixture. Capture the actual C++-backed dashboard
            // before switching to the portfolio Gallery photo audit.
            auto *portfolioTotal=startupWindow->findChild<QQuickItem*>(
                "overviewTotalCard");
            auto *portfolioAvailable=startupWindow->findChild<QQuickItem*>(
                "overviewAvailableCard");
            auto *portfolioActive=startupWindow->findChild<QQuickItem*>(
                "overviewActiveCard");
            auto *portfolioBooked=startupWindow->findChild<QQuickItem*>(
                "overviewValueCard");
            if(startupWindow->property("workspace").toInt()!=0 ||
               !portfolioTotal || !portfolioAvailable ||
               !portfolioActive || !portfolioBooked ||
               portfolioTotal->property("value").toString()!="131" ||
               portfolioAvailable->property("value").toString()!="130" ||
               portfolioActive->property("value").toString()!="1" ||
               std::abs(rentalModel.activeBookedValue()-1400.0)>0.01 ||
               portfolioBooked->property("value").toString()!="R 1400.00" ||
               rentalModel.oldestOverdueRentalId()!="R001") {
                qCritical()<<"Portfolio actual Overview KPI/source data mismatch";
                app.exit(774);return;
            }
            const QString portfolioSize=parser.isSet("compact")
                ? QStringLiteral("1280x720") : QStringLiteral("1500x860");
            QTest::qWait(150);
            const auto savePortfolioShot=[&](const QString &name)->bool {
                const QImage shot=startupWindow->grabWindow();
                return !shot.isNull() && shot.size()==startupWindow->size() &&
                    shot.save(QStringLiteral("catalogue-qt-%1-%2.png")
                                   .arg(name,portfolioSize),"PNG");
            };
            // Repeater delegates are in the Qt Quick visual item tree,
            // but are not guaranteed to be QObject-owned by the window.
            // Search real childItems(), as the existing native Gallery and
            // recent-contract tests already do, and log the observed value.
            const auto findVisibleItem=[](const auto &self,QQuickItem *item,
                                          const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name)return item;
                for(auto *child:item->childItems())
                    if(auto *found=self(self,child,name))return found;
                return nullptr;
            };
            const auto recentRows=rentalModel.recentActivities();
            auto *recentAmount=findVisibleItem(
                findVisibleItem,startupWindow->contentItem(),
                QStringLiteral("overviewRecentAmount0"));
            const QString actualRecentAmount=recentAmount
                ? recentAmount->property("text").toString()
                : QStringLiteral("<missing QML delegate>");
            if(recentRows.isEmpty() ||
               recentRows.first().toMap().value("rentalId").toString()!=
                   QStringLiteral("R001") ||
               actualRecentAmount!=QStringLiteral("R 1,400.00")) {
                qCritical()<<"Portfolio visible active booked amount formatting mismatch"
                           <<"source rows"<<recentRows
                           <<"rendered"<<actualRecentAmount;
                app.exit(777);return;
            }
            if(!savePortfolioShot(QStringLiteral("overview"))) {
                qCritical()<<"Portfolio source-backed Overview screenshot failed";
                app.exit(775);return;
            }
            startupWindow->setProperty("workspace",1);
            QCoreApplication::processEvents();
            auto *gallery=startupWindow->findChild<QQuickItem*>("fleetGallery");
            if(!gallery || gallery->property("count").toInt()!=131) {
                qCritical()<<"Portfolio 131-entry actual QML Gallery missing";
                app.exit(715);return;
            }
            QTest::qWait(130);
            if(!savePortfolioShot(QStringLiteral("fleet-all"))) {
                qCritical()<<"Portfolio actual 131-entry Gallery screenshot failed";
                app.exit(776);return;
            }
            fleet.setQuery("Toyota");
            fleet.selectById("C001");
            if(fleet.selectedId()!="C001" || fleet.selectedVisibleIndex()<0) {
                qCritical()<<"Portfolio C++ filtering/selection failed";
                app.exit(716);return;
            }
            fleet.setQuery("NotAnExistingVehicleModel");
            if(fleet.selectedVisibleIndex()>=0 ||
               gallery->property("count").toInt()!=0) {
                qCritical()<<"Portfolio empty search failed";
                app.exit(717);return;
            }
            fleet.setQuery("");
            if(gallery->property("count").toInt()!=131 ||
               fleet.totalCount()!=131) {
                qCritical()<<"Portfolio gallery search reset failed";
                app.exit(718);return;
            }
            // Check real Gallery and Inspector states for both the retained
            // CC0 reference and the silhouette used for omitted photos.
            for(const QString &id : QStringList{
                    QStringLiteral("C017"),QStringLiteral("C091"),
                    QStringLiteral("C121")}) {
                fleet.setQuery(id);
                fleet.selectById(id);
                QTest::qWait(250);
                const bool hasReference=id==QStringLiteral("C091");
                if(fleet.selectedId()!=id ||
                   gallery->property("count").toInt()!=1 ||
                   fleetPhotos.hasPhoto(id)!=hasReference) {
                    qCritical()<<"Catalogue model/photo identity mismatch"<<id;
                    app.exit(719);return;
                }
                // Four-digit Fleet rates must have the same visible
                // grouping as Overview and Rentals, while raw pricing stays
                // sourced from the C++ FleetController.
                if(id==QStringLiteral("C091")) {
                    auto *inspectorRate=findVisibleItem(
                        findVisibleItem,startupWindow->contentItem(),
                        QStringLiteral("fleetInspectorRateValue"));
                    auto *galleryRate=findVisibleItem(
                        findVisibleItem,startupWindow->contentItem(),
                        QStringLiteral("fleetCardRate"));
                    if(fleet.selectedRate()!=QStringLiteral("1750.00") ||
                       !inspectorRate || !galleryRate ||
                       inspectorRate->property("text").toString()!=
                           QStringLiteral("R 1,750.00") ||
                       galleryRate->property("text").toString()!=
                           QStringLiteral("R 1,750.00")) {
                        qCritical()<<"Portfolio native Fleet grouped daily rate mismatch"
                                   <<"raw"<<fleet.selectedRate()
                                   <<"inspector"<<(inspectorRate
                                       ? inspectorRate->property("text")
                                       : QVariant())
                                   <<"gallery"<<(galleryRate
                                       ? galleryRate->property("text")
                                       : QVariant());
                        app.exit(780);return;
                    }
                    // Verify the third rate presentation too, in the real
                    // 131-vehicle Table, and capture native QML at both sizes.
                    // This changes the view only; selection and C++ data stay
                    // fixed on C091. Revert to Gallery for photo provenance.
                    startupWindow->setProperty("gallery",false);
                    QTest::qWait(170);
                    auto *fleetTable=findVisibleItem(
                        findVisibleItem,startupWindow->contentItem(),
                        QStringLiteral("fleetTable"));
                    auto *tableRate=findVisibleItem(
                        findVisibleItem,startupWindow->contentItem(),
                        QStringLiteral("fleetTableRate"));
                    const QString tableRateText=tableRate
                        ? tableRate->property("text").toString() : QString();
                    if(!fleetTable || !fleetTable->isVisible() ||
                       !tableRate || !tableRate->isVisible() ||
                       fleet.selectedId()!=QStringLiteral("C091") ||
                       tableRateText!=QStringLiteral("R 1,750.00") ||
                       !savePortfolioShot(QStringLiteral("C091-table"))) {
                        qCritical()<<"Portfolio real Fleet Table grouped daily rate mismatch"
                                   <<"observed"<<tableRateText
                                   <<"selected"<<fleet.selectedId();
                        app.exit(781);return;
                    }
                    startupWindow->setProperty("gallery",true);
                    QTest::qWait(120);
                }
                const auto record=fleetPhotos.attributionFor(id);
                auto *mediaLabel=findVisibleItem(
                    findVisibleItem,startupWindow->contentItem(),
                    QStringLiteral("fleetPhotoDisclaimer"));
                const QString expectedMedia=hasReference
                    ? QStringLiteral("Reference image")
                    : QStringLiteral("Illustration");
                if(!mediaLabel || !mediaLabel->isVisible() ||
                   mediaLabel->property("text").toString()!=expectedMedia ||
                   (!hasReference && !record.isEmpty()) ||
                   (hasReference && (record.value("kind").toString()!="reference" ||
                    record.value("creator").toString().isEmpty() ||
                    record.value("licence").toString()!="CC0" ||
                    !record.value("sourcePage").toString().startsWith(
                        "https://commons.wikimedia.org/wiki/File:")))) {
                    qCritical()<<"Portfolio displayed photo lacks exact-source credit"<<id;
                    app.exit(773);return;
                }
                const QString size=parser.isSet("compact")
                    ? QStringLiteral("1280x720") : QStringLiteral("1500x860");
                const QImage photoShot=startupWindow->grabWindow();
                if(photoShot.isNull() || photoShot.size()!=startupWindow->size() ||
                   !photoShot.save(QStringLiteral("catalogue-qt-%1-%2.png")
                                   .arg(id,size),"PNG")) {
                    qCritical()<<"Curated photo QML screenshot invalid"<<id;
                    app.exit(720);return;
                }
            }
            fleet.setQuery("");
            if(gallery->property("count").toInt()!=131) {
                qCritical()<<"Catalogue photo review did not restore full Gallery";
                app.exit(721);return;
            }
            // Capture the third and final owner-approved workspace with the
            // same genuine 131-vehicle synthetic C++ state. Selecting R001 is
            // read-only, preserving every protected source fixture byte.
            startupWindow->setProperty("workspace",2);
            if(!rentalModel.locateById(QStringLiteral("R001"))) {
                qCritical()<<"Portfolio real R001 contract could not be selected";
                app.exit(778);return;
            }
            QTest::qWait(200);
            auto *registerPanel=findVisibleItem(
                findVisibleItem,startupWindow->contentItem(),
                QStringLiteral("rentalRecordsPanel"));
            auto *inspectorHero=findVisibleItem(
                findVisibleItem,startupWindow->contentItem(),
                QStringLiteral("rentalInspectorHero"));
            if(!registerPanel || !inspectorHero ||
               !registerPanel->isVisible() || !inspectorHero->isVisible() ||
               rentalModel.count()!=2 ||
               rentalModel.selectedRentalId()!=QStringLiteral("R001") ||
               !savePortfolioShot(QStringLiteral("rentals"))) {
                qCritical()<<"Portfolio actual 131-vehicle Rentals screenshot invalid";
                app.exit(779);return;
            }
            qInfo()<<"PORTFOLIO fictional 131-vehicle model-photo Fleet PASS"
                   <<(parser.isSet("compact")?"1280x720":"1500x860");
            app.exit(0);
        });
    }
    if(parser.isSet("startup-fit-smoke")) {
        QTimer::singleShot(300,&app,[&,startupWindow] {
            if(!startupWindow->isVisible() ||
               !startupWindow->screen() ||
               startupWindow->width()!=startupW ||
               startupWindow->height()!=startupH ||
               startupWindow->width()<1024 || startupWindow->height()<660 ||
               startupWindow->width()>1500 || startupWindow->height()>860 ||
               (startupArea.width()>=1024 &&
                   startupWindow->width()>startupArea.width()) ||
               (startupArea.height()>=660 &&
                   startupWindow->height()>startupArea.height()) ||
               !startupArea.contains(startupWindow->position())) {
                qCritical()<<"Native startup viewport did not fit available desktop"
                           <<startupArea<<startupWindow->geometry()
                           <<startupW<<startupH;
                app.exit(601);return;
            }
            // Product shell must be navigable without artificial CTest
            // manual resizing or a screenshot-only mock.
            if(!startupWindow->contentItem() ||
               startupWindow->contentItem()->childItems().isEmpty()) {
                qCritical()<<"Startup Qt Quick operator shell is empty";
                app.exit(602);return;
            }
            // Normal portable sessions discard changes on exit.
            // The actual Qt shell must disclose this, not just the CMD log.
            auto *mode=startupWindow->findChild<QQuickItem*>("sessionModeNotice");
            auto *sidebar=startupWindow->findChild<QQuickItem*>("sessionModeSidebar");
            if(!mode||!sidebar||!mode->isVisible()||!sidebar->isVisible()||
               !mode->property("text").toString().contains("RESETS ON EXIT")||
               !sidebar->property("text").toString().contains("reset when you close")) {
                qCritical()<<"Fictional disposable session notice missing";
                app.exit(603);return;
            }
            qInfo()<<"FUNC-D1 native desktop-fit startup PASS"
                   <<startupArea<<startupWindow->geometry();
            app.exit(0);
        });
        return app.exec();
    }
    // Actual Qt Quick Gallery, Inspector and Add Vehicle photo paths.
    // Every source is a pinned resource or a deliberately chosen local file
    // in the disposable fictional root; source fixture bytes remain intact.
    if(parser.isSet("fleet-photo-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window)return 710;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",1);
        QTimer::singleShot(1100,&app,[&,window,compact] {
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name && item->isVisible())return item;
                for(auto *child:item->childItems())
                    if(auto *hit=self(self,child,name))return hit;
                return nullptr;
            };
            const auto press=[&](const QString &id)->bool {
                auto *item=find(find,window->contentItem(),id);
                if(!item||!item->isEnabled())return false;
                item->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window,Qt::Key_Space);
                QCoreApplication::processEvents();
                QTest::qWait(200);
                return true;
            };
            if(!engine.imageProvider("fleetphotos") ||
               !fleetPhotos.hasPhoto("C001")||!fleetPhotos.hasPhoto("C002")||
               !fleetPhotos.hasPhoto("C003")||fleetPhotos.hasPhoto("M001")||
               fleetPhotos.hasPhoto("M002")||fleetPhotos.hasPhoto("C004")||
               fleetPhotos.imageFor("C001").isNull()) {
                qCritical()<<"Fleet seeded photo / generic motorcycle fallback failed";
                app.exit(711);return;
            }
            const auto demoCredit=fleetPhotos.attributionFor("C001");
            if(demoCredit.value("creator").toString()!="01x07x2022000" ||
               demoCredit.value("licence").toString()!="CC0 1.0" ||
               !demoCredit.value("sourcePage").toString().contains("Toyota_Corolla_IMG") ||
               !fleetPhotos.attributionFor("M001").isEmpty()) {
                qCritical()<<"Fleet demo exact-source attribution or silhouette identity invalid";
                app.exit(771);return;
            }
            // Verify photo-led gallery proportions in the actual native
            // scene, not only the existence of a provider URL.
            auto *galleryPhoto=find(find,window->contentItem(),
                                    "vehicleCardPhoto");
            if(!galleryPhoto || galleryPhoto->height()<108 ||
               galleryPhoto->property("status").toInt()!=1) {
                qCritical()<<"Fleet Gallery photo crop area too small or unreadable";
                app.exit(724);return;
            }
            auto *galleryMedia=find(find,window->contentItem(),
                                    "fleetGalleryMediaStatus");
            if(!galleryMedia ||
               galleryMedia->property("text").toString()!="Reference image") {
                qCritical()<<"Fleet Gallery reference photo lacks truthful media label";
                app.exit(754);return;
            }
            fleet.selectById("C001");
            QTest::qWait(180);
            auto *photo=find(find,window->contentItem(),"fleetInspectorPhoto");
            auto *changePhoto=find(find,window->contentItem(),
                                   "fleetReplacePhotoButton");
            auto *bookAction=find(find,window->contentItem(),
                                  "fleetBookSelectedButton");
            const auto inWindow=[window](QQuickItem *item) {
                if(!item || !item->isVisible())return false;
                const QPointF at=item->mapToScene(QPointF{0,0});
                return at.x()>=0 && at.y()>=0 &&
                       at.x()+item->width()<=window->width() &&
                       at.y()+item->height()<=window->height();
            };
            if(!photo ||
               !photo->property("source").toString().contains("C001") ||
               photo->property("status").toInt()!=1 ||
               !inWindow(changePhoto) || !inWindow(bookAction) ||
               bookAction->height()<36) {
                qCritical()<<"Fleet image not rendered or compact Inspector action clipped";
                app.exit(712);return;
            }
            // Save real selected-Inspector evidence for visual review on
            // both hosted operating systems; never substitute a mockup.
            auto *photoDisclaimer=find(find,window->contentItem(),
                                         "fleetPhotoDisclaimer");
            auto *selectedArt=find(find,window->contentItem(),
                                    "fleetInspectorSelectedArt");
            if(!photoDisclaimer || !selectedArt ||
               photoDisclaimer->property("text").toString()!="Reference image" ||
               selectedArt->height()<84 || changePhoto->height()>33 ||
               !inWindow(photoDisclaimer)) {
                qCritical()<<"Fleet Inspector photo action/disclaimer not contained";
                app.exit(727);return;
            }
            const QRectF artRect=selectedArt->mapRectToScene(
                QRectF(0,0,selectedArt->width(),selectedArt->height()));
            const QPointF captionEnd=photoDisclaimer->mapToScene(
                QPointF(photoDisclaimer->width(),photoDisclaimer->height()));
            if(captionEnd.y()>artRect.bottom()+1 ||
               captionEnd.x()>artRect.right()+1) {
                qCritical()<<"Fleet photo disclaimer overflowed the Inspector";
                app.exit(728);return;
            }
            // R2 white details credit is no longer placed over photography.
            // Native test must enforce the new truthful credit placement
            // while retaining the original source, View full and badge checks.
            auto *creditBadge=find(find,window->contentItem(),
                                   "fleetInspectorPhotoCredit");
            auto *creditView=find(find,window->contentItem(),
                                  "fleetViewFullPhotoButton");
            auto *inspectorPanel=find(find,window->contentItem(),
                                     "vehicleInspector");
            if(!creditBadge || !creditView || !inspectorPanel ||
               !inWindow(creditBadge) ||
               !creditBadge->property("text").toString().contains(
                   QStringLiteral("CC0 1.0"))) {
                qCritical()<<"Fleet photo credit absent or false source record";
                app.exit(774);return;
            }
            const auto mediaRect=[](QQuickItem *item) {
                return item->mapRectToScene(
                    QRectF(0,0,item->width(),item->height()));
            };
            const QRectF creditRect=mediaRect(creditBadge);
            const QRectF inspectorRect=mediaRect(inspectorPanel);
            if(!inspectorRect.contains(creditRect) ||
               creditRect.top()<artRect.bottom()-2 ||
               creditRect.intersects(mediaRect(photoDisclaimer)) ||
               creditRect.intersects(mediaRect(creditView))) {
                qCritical()<<"R2 Fleet attribution not in white inspector below photograph";
                app.exit(775);return;
            }
            if(!compact) {
                auto *factId=find(find,window->contentItem(),
                                  "fleetInspectorIdentityId");
                auto *factType=find(find,window->contentItem(),
                                    "fleetInspectorIdentityType");
                if(!factId || !factType ||
                   factId->property("text").toString()!=QStringLiteral("C001") ||
                   factType->property("text").toString()!=fleet.selectedType() ||
                   !inWindow(factId) || !inWindow(factType)) {
                    qCritical()<<"R2 Fleet factual wide inspector identity regression";
                    app.exit(776);return;
                }
            }
            const QString photoViewport=compact?"1280x720":"1500x860";
            // The real native inspector must offer an uncropped, read-only
            // reference view for decoded photos, with honest disclosure and
            // keyboard focus restoration. It is not a licence acceptance.
            auto *viewFull=find(find,window->contentItem(),
                                 "fleetViewFullPhotoButton");
            if(!viewFull || !inWindow(viewFull) ||
               !press("fleetViewFullPhotoButton")) {
                qCritical()<<"Full-photo inspection action missing";
                app.exit(758);return;
            }
            QTest::qWait(170);
            auto *fullPhoto=find(find,window->contentItem(),
                                  "fleetFullPhotoImage");
            auto *fullHeading=find(find,window->contentItem(),
                                    "fleetFullPhotoHeading");
            auto *fullDisclosure=find(find,window->contentItem(),
                                       "fleetFullPhotoDisclosure");
            auto *fullClose=find(find,window->contentItem(),
                                  "fleetFullPhotoClose");
            auto *fullCredit=find(find,window->contentItem(),
                                   "fleetFullPhotoCredit");
            auto *fullSource=find(find,window->contentItem(),
                                   "fleetFullPhotoSource");
            if(!fullPhoto || !fullHeading || !fullDisclosure || !fullClose ||
               !fullCredit || !fullSource ||
               !fullCredit->property("text").toString().contains("01x07x2022000") ||
               !fullSource->property("text").toString().contains("Toyota_Corolla_IMG") ||
               fullPhoto->property("status").toInt()!=1 ||
               !fullPhoto->property("source").toString().contains("C001") ||
               !fullHeading->property("text").toString().contains("Toyota Corolla") ||
               !fullDisclosure->property("text").toString().contains("not a verified photo") ||
               // QtQuick Image.PreserveAspectFit = 1, no misleading source crop.
               fullPhoto->property("fillMode").toInt()!=1 ||
               !inWindow(fullHeading) || !inWindow(fullDisclosure) ||
               !inWindow(fullClose) || !inWindow(fullPhoto)) {
                qCritical()<<"Full-photo review disclosure or image fit failed";
                app.exit(759);return;
            }
            // PHOTO-P1-H: capture the actual OPEN popup, not just the
            // underlying vehicle card. Both hosted OS/viewport configurations
            // must produce a reviewable before/after evidence image.
            const QImage fullCorollaShot=window->grabWindow();
            const QString fullCorollaFile=QStringLiteral(
                "ux-q3-full-reference-C001-%1.png").arg(photoViewport);
            if(fullCorollaShot.size()!=window->size() ||
               !fullCorollaShot.save(fullCorollaFile,"PNG")) {
                qCritical()<<"Uncropped Corolla review screenshot failed";
                app.exit(761);return;
            }
            if(!press("fleetFullPhotoClose")) {
                qCritical()<<"Full-photo Close inaccessible";
                app.exit(762);return;
            }
            QTest::qWait(130);
            if(find(find,window->contentItem(),"fleetFullPhotoImage") ||
               !viewFull->hasActiveFocus()) {
                qCritical()<<"Photo preview leaked or keyboard focus was lost";
                app.exit(760);return;
            }
            // Independently verify the most visually problematic i20 source,
            // including ESC dismissal and source-bound title, without
            // changing/unlicensing any of the existing app photo binaries.
            fleet.selectById("C003");
            QTest::qWait(230);
            if(!press("fleetViewFullPhotoButton")) {
                qCritical()<<"i20 full-photo action missing";
                app.exit(763);return;
            }
            QTest::qWait(180);
            fullPhoto=find(find,window->contentItem(),"fleetFullPhotoImage");
            fullHeading=find(find,window->contentItem(),"fleetFullPhotoHeading");
            fullDisclosure=find(find,window->contentItem(),"fleetFullPhotoDisclosure");
            fullClose=find(find,window->contentItem(),"fleetFullPhotoClose");
            if(!fullPhoto || !fullHeading || !fullDisclosure || !fullClose ||
               fullPhoto->property("status").toInt()!=1 ||
               fullPhoto->property("fillMode").toInt()!=1 ||
               !fullPhoto->property("source").toString().contains("C003") ||
               !fullHeading->property("text").toString().contains("Hyundai i20") ||
               !inWindow(fullPhoto) || !inWindow(fullHeading) ||
               !inWindow(fullDisclosure) || !inWindow(fullClose)) {
                qCritical()<<"i20 uncropped popup is wrong or clipped";
                app.exit(764);return;
            }
            const QImage fullI20Shot=window->grabWindow();
            const QString fullI20File=QStringLiteral(
                "ux-q3-full-reference-C003-%1.png").arg(photoViewport);
            if(fullI20Shot.size()!=window->size() ||
               !fullI20Shot.save(fullI20File,"PNG")) {
                qCritical()<<"Uncropped i20 review screenshot failed";
                app.exit(765);return;
            }
            QTest::keyClick(window,Qt::Key_Escape);
            QTest::qWait(200);
            auto *i20ViewFull=find(find,window->contentItem(),
                                    "fleetViewFullPhotoButton");
            if(find(find,window->contentItem(),"fleetFullPhotoImage") ||
               !i20ViewFull || !i20ViewFull->hasActiveFocus()) {
                qCritical()<<"Escape did not dismiss image popup/restore focus";
                app.exit(766);return;
            }
            // A selection change while open must dismiss stale imagery.
            if(!press("fleetViewFullPhotoButton")) {
                app.exit(767);return;
            }
            QTest::qWait(130);
            fleet.selectById("C001");
            QTest::qWait(230);
            if(find(find,window->contentItem(),"fleetFullPhotoImage") ||
               fleet.selectedId()!="C001") {
                qCritical()<<"Full image viewer persisted after vehicle change";
                app.exit(768);return;
            }
            // Verify the model-neutral fallback actually renders for a
            // real motorcycle selection, not merely a provider boolean.
            fleet.selectById("M001");
            QTest::qWait(220);
            auto *motorcycleArt=find(find,window->contentItem(),
                                      "fleetInspectorFallbackArt");
            if(!motorcycleArt ||
               find(find,window->contentItem(),"fleetInspectorPhoto") ||
               !inWindow(motorcycleArt) ||
               photoDisclaimer->property("text").toString()!="Illustration" ||
               !inWindow(photoDisclaimer)) {
                qCritical()<<"Motorcycle Fleet fallback or its media label was hidden";
                app.exit(752);return;
            }
            // Real Gallery filtering must label motorcycle artwork as art,
            // not suggest that an unapproved Honda photograph is present.
            fleet.setQuery("M001");
            QTest::qWait(180);
            galleryMedia=find(find,window->contentItem(),
                              "fleetGalleryMediaStatus");
            if(!galleryMedia ||
               galleryMedia->property("text").toString()!="Illustration" ||
               !inWindow(galleryMedia)) {
                qCritical()<<"Fleet Gallery motorcycle fallback mislabeled";
                app.exit(755);return;
            }
            fleet.setQuery("");
            QTest::qWait(130);
            const QImage motorcycleShot=window->grabWindow();
            if(motorcycleShot.size()!=window->size() ||
               !motorcycleShot.save("ux-q3-motorcycle-fallback-"+
                       photoViewport+".png","PNG")) {
                qCritical()<<"Motorcycle fallback native screenshot failed";
                app.exit(753);return;
            }
            // PHOTO-P1 baseline: real Qt screenshots of every currently
            // shipped fictional model, one visible Gallery card plus the
            // selected Inspector. These show UNAPPROVED references/fallbacks,
            // not a claim about new source photographs or actual rental stock.
            const QStringList reviewIds={
                QStringLiteral("C001"),QStringLiteral("C002"),
                QStringLiteral("C003"),QStringLiteral("M001"),
                QStringLiteral("M002")};
            for(const QString &id:reviewIds) {
                const bool expectPhoto=id.startsWith(QLatin1Char('C'));
                const QString expectedLabel=expectPhoto
                    ? QStringLiteral("Reference image")
                    : QStringLiteral("Illustration");
                fleet.setQuery(id);
                fleet.selectById(id);
                QTest::qWait(320);
                auto *cardImage=find(find,window->contentItem(),
                                     "vehicleCardPhoto");
                auto *cardFallback=find(find,window->contentItem(),
                                        "vehicleCardArt");
                auto *cardLabel=find(find,window->contentItem(),
                                     "fleetGalleryMediaStatus");
                auto *inspectorImage=find(find,window->contentItem(),
                                          "fleetInspectorPhoto");
                auto *inspectorFallback=find(find,window->contentItem(),
                                             "fleetInspectorFallbackArt");
                auto *cardFallbackIdentity=find(find,window->contentItem(),
                                               "fleetGalleryFallbackModelLabel");
                auto *inspectorFallbackIdentity=find(find,window->contentItem(),
                                               "fleetInspectorFallbackModelLabel");
                auto *visibleFullPhotoAction=find(find,window->contentItem(),
                                                  "fleetViewFullPhotoButton");
                // Both bikes use one make-neutral silhouette. The actual
                // C++ model name must distinguish the media stage, without
                // pretending that the drawing depicts a particular model.
                const bool motorcycleIdentityValid=expectPhoto
                    ? (!cardFallbackIdentity && !inspectorFallbackIdentity)
                    : (cardFallbackIdentity && inspectorFallbackIdentity &&
                       inWindow(cardFallbackIdentity) &&
                       inWindow(inspectorFallbackIdentity) &&
                       cardFallbackIdentity->property("text").toString()==
                           fleet.selectedName() &&
                       inspectorFallbackIdentity->property("text").toString()==
                           fleet.selectedName());
                if(fleet.selectedId()!=id ||
                   fleet.selectedName().isEmpty() ||
                   !motorcycleIdentityValid ||
                   bool(visibleFullPhotoAction)!=expectPhoto ||
                   (expectPhoto && !inWindow(visibleFullPhotoAction)) ||
                   !cardLabel || !inWindow(cardLabel) ||
                   cardLabel->property("text").toString()!=expectedLabel ||
                   photoDisclaimer->property("text").toString()!=expectedLabel ||
                   !inWindow(photoDisclaimer) ||
                   (expectPhoto && (!cardImage || !inspectorImage ||
                                    !inWindow(cardImage) ||
                                    !inWindow(inspectorImage) ||
                                    cardFallback || inspectorFallback)) ||
                   (!expectPhoto && (!cardFallback || !inspectorFallback ||
                                     !inWindow(cardFallback) ||
                                     !inWindow(inspectorFallback) ||
                                     cardImage || inspectorImage))) {
                    qCritical()<<"Five-model native Fleet review missing/mislabeled"
                               <<id<<expectedLabel;
                    app.exit(756);return;
                }
                const QImage modelShot=window->grabWindow();
                const QString filename=QStringLiteral("ux-q3-five-model-%1-%2.png")
                    .arg(id,photoViewport);
                if(modelShot.size()!=window->size() ||
                   !modelShot.save(filename,"PNG")) {
                    qCritical()<<"Five-model native screenshot failed"<<id;
                    app.exit(757);return;
                }
            }
            fleet.setQuery("");
            QTest::qWait(180);
            fleet.selectById("C001");
            QTest::qWait(170);
            const QImage selectedShot=window->grabWindow();
            if(selectedShot.size()!=window->size() ||
               !selectedShot.save("ux-q3-selected-fleet-"+photoViewport+".png","PNG")) {
                qCritical()<<"Fleet selected Inspector capture failed";
                app.exit(725);return;
            }
            if(fleetPhotos.chooseFile(QUrl("https://invalid.example/image.jpg")) ||
               fleetPhotos.hasStaged()) {
                qCritical()<<"Fleet image accepted remote URL";
                app.exit(713);return;
            }
            const QString invalid=QDir(dataDirectory).filePath("not-photo.png");
            QFile bogus(invalid);
            if(!bogus.open(QIODevice::WriteOnly) ||
               bogus.write("not-image")!=9) {app.exit(714);return;}
            bogus.close();
            if(fleetPhotos.chooseFile(QUrl::fromLocalFile(invalid)) ||
               fleetPhotos.hasStaged()) {
                qCritical()<<"Fleet image accepted invalid bytes";
                app.exit(715);return;
            }
            const QString path=QDir(dataDirectory).filePath("fictional-photo.png");
            QImage image(96,64,QImage::Format_RGB32);
            image.fill(QColor(31,163,92));
            if(!image.save(path,"PNG") || !press("fleetAddButton") ||
               !fleetPhotos.chooseFile(QUrl::fromLocalFile(path)) ||
               !fleetPhotos.hasStaged()) {
                qCritical()<<"Fleet optional local photo staging failed";
                app.exit(716);return;
            }
            auto *preview=find(find,window->contentItem(),"fleetPhotoPreview");
            auto *picker=find(find,window->contentItem(),"fleetChoosePhoto");
            auto *brand=find(find,window->contentItem(),"fleetAddBrand");
            auto *model=find(find,window->contentItem(),"fleetAddModel");
            auto *rate=find(find,window->contentItem(),"fleetAddRate");
            if(!preview||!picker||!brand||!model||!rate||
               !preview->property("source").toString().contains("__staged")) {
                qCritical()<<"Fleet QML photo picker/preview missing";
                app.exit(717);return;
            }
            brand->setProperty("text","Fictional Studio");
            model->setProperty("text","Concept");
            rate->setProperty("text","415");
            if(!press("fleetAddConfirm")||fleet.selectedId()!="C004"||
               !fleetPhotos.hasPhoto("C004")||fleetPhotos.hasStaged()) {
                qCritical()<<"Fleet photo did not attach after actual Add Vehicle";
                app.exit(718);return;
            }
            QTest::qWait(250);
            photo=find(find,window->contentItem(),"fleetInspectorPhoto");
            FleetPhotoStore second(dataDirectory);
            second.setVehicleIdentityLookup(livePhotoIdentity);
            const QImage recovered=second.imageFor("C004");
            if(!photo||!photo->property("source").toString().contains("C004")||
               photo->property("status").toInt()!=1 ||
               !inWindow(changePhoto) || !inWindow(bookAction) ||
               !second.hasPhoto("C004")||recovered.size()!=image.size()||
               recovered.pixelColor(5,5)!=image.pixelColor(5,5)) {
                qCritical()<<"Fleet image not bound/recoverable from fictional sidecar";
                app.exit(719);return;
            }
            // Exercise the actual QML Change photo callback and the guarded
            // C++ write. Previously the QML called a non-invokable C++ method.
            const QString replacementPath=QDir(dataDirectory).filePath(
                "replacement-photo.png");
            QImage replacementImage(64,64,QImage::Format_RGB32);
            replacementImage.fill(QColor(40,70,210));
            if(!replacementImage.save(replacementPath,"PNG")) {
                app.exit(729);return;
            }
            FleetPhotoStore lockedPhotoStore(dataDirectory);
            if(lockedPhotoStore.replaceFor("C001",
                    QUrl::fromLocalFile(replacementPath)) ||
               QFileInfo::exists(QDir(dataDirectory).filePath("fleet_images/C001.png"))) {
                qCritical()<<"Fixed fictional photo replacement was not denied";
                app.exit(730);return;
            }
            const auto applyPhoto=[window](const QUrl &url) {
                return QMetaObject::invokeMethod(window,"applySelectedFleetPhoto",
                    Q_ARG(QVariant,QVariant(url)));
            };
            fleet.selectById("C001");
            window->setProperty("photoChangeTargetId","C001");
            if(!applyPhoto(QUrl::fromLocalFile(replacementPath))) {
                qCritical()<<"QML replacement action could not be invoked";
                app.exit(731);return;
            }
            QTest::qWait(220);
            const QString c001Sidecar=QDir(dataDirectory).filePath(
                "fleet_images/C001.png");
            QFile persisted(c001Sidecar);
            if(!persisted.open(QIODevice::ReadOnly)) {app.exit(732);return;}
            const QByteArray beforeRejected=persisted.readAll();
            persisted.close();
            auto *replacementNotice=find(find,window->contentItem(),
                                          "fleetAddMessage");
            photo=find(find,window->contentItem(),"fleetInspectorPhoto");
            if(!replacementNotice ||
               !replacementNotice->property("text").toString().contains(
                   "Photo updated for C001") ||
               !fleetPhotos.hasPhoto("C001") ||
               fleetPhotos.imageFor("C001").pixelColor(3,3)!=
                   replacementImage.pixelColor(3,3) ||
               !photo || photo->property("status").toInt()!=1) {
                qCritical()<<"Replacement photo write or on-screen notice failed";
                app.exit(733);return;
            }
            // Custom photos expose both View full and Restore default.
            // Guard independent hit targets in the actual compact/wide Qt UI.
            auto *customView=find(find,window->contentItem(),
                                  "fleetViewFullPhotoButton");
            auto *customRestore=find(find,window->contentItem(),
                                     "fleetRestoreDefaultPhotoButton");
            const auto customCredit=fleetPhotos.attributionFor("C001");
            auto *inspectorCredit=find(find,window->contentItem(),
                                       "fleetInspectorPhotoCredit");
            if(customCredit.value("kind").toString()!="custom" ||
               !customCredit.value("sourcePage").toString().isEmpty() ||
               !inspectorCredit ||
               !inspectorCredit->property("text").toString().contains("unverified")) {
                qCritical()<<"Custom Fleet photo inherited false source licence";
                app.exit(772);return;
            }
            const auto sceneRect=[](QQuickItem *item) {
                return item->mapRectToScene(
                    QRectF(0,0,item->width(),item->height()));
            };
            if(!customView || !customRestore ||
               !inWindow(customView) || !inWindow(customRestore) ||
               sceneRect(customView).intersects(sceneRect(customRestore)) ||
               !sceneRect(selectedArt).contains(sceneRect(customView)) ||
               !sceneRect(selectedArt).contains(sceneRect(customRestore))) {
                qCritical()<<"Custom photo View full / Restore default overlap or clip";
                app.exit(769);return;
            }
            const QImage customActionsShot=window->grabWindow();
            if(customActionsShot.size()!=window->size() ||
               !customActionsShot.save(
                   "ux-q3-custom-photo-actions-"+photoViewport+".png","PNG")) {
                qCritical()<<"Custom Fleet photo actions screenshot failed";
                app.exit(770);return;
            }
            window->setProperty("photoChangeTargetId","C001");
            if(!applyPhoto(QUrl("https://invalid.example/image.jpg")) ||
               !QFileInfo::exists(c001Sidecar)) {
                qCritical()<<"QML photo rejection callback not available";
                app.exit(734);return;
            }
            QFile afterDenied(c001Sidecar);
            if(!afterDenied.open(QIODevice::ReadOnly)) {app.exit(735);return;}
            const QByteArray unchanged=afterDenied.readAll();
            afterDenied.close();
            if(unchanged!=beforeRejected ||
               !replacementNotice->property("text").toString().contains(
                   "Photo unchanged for C001")) {
                qCritical()<<"Rejected photo lost previous image or gave false success";
                app.exit(736);return;
            }
            // A selection switched while the picker was open must not write
            // the old target ID, even when the new URL is valid.
            window->setProperty("photoChangeTargetId","C001");
            fleet.selectById("C002");
            if(!applyPhoto(QUrl::fromLocalFile(replacementPath)) ||
               !window->property("photoChangeNotice").toString().contains(
                   "Photo unchanged")) {
                qCritical()<<"Stale selected vehicle image import not refused";
                app.exit(737);return;
            }
            QFile afterStale(c001Sidecar);
            if(!afterStale.open(QIODevice::ReadOnly) ||
               afterStale.readAll()!=beforeRejected) {
                qCritical()<<"Stale photo selection modified previous image";
                app.exit(738);return;
            }
            afterStale.close();
            // A custom image can be discarded only through the actual
            // confirmation UI. Fixed fictional-only storage may not reset it.
            if(lockedPhotoStore.restoreDefaultFor("C001") ||
               !fleetPhotos.hasCustomPhoto("C001")) {
                qCritical()<<"Fixed fictional session reset was not guarded";
                app.exit(739);return;
            }
            fleet.selectById("C001");
            QTest::qWait(150);
            if(!press("fleetRestoreDefaultPhotoButton")) {
                qCritical()<<"Fleet restore button missing for custom photo";
                app.exit(740);return;
            }
            // Escape/cancel must retain the source bytes and active photo.
            if(!press("fleetRestoreDefaultCancel") ||
               !fleetPhotos.hasCustomPhoto("C001")) {
                qCritical()<<"Cancelling photo reset discarded custom media";
                app.exit(741);return;
            }
            QFile afterCancel(c001Sidecar);
            if(!afterCancel.open(QIODevice::ReadOnly) ||
               afterCancel.readAll()!=beforeRejected) {
                qCritical()<<"Cancelled photo reset changed stored bytes";
                app.exit(742);return;
            }
            afterCancel.close();
            QTest::qWait(100);
            auto *resetFocus=find(find,window->contentItem(),
                                  "fleetRestoreDefaultPhotoButton");
            if(!resetFocus || !resetFocus->hasActiveFocus()) {
                qCritical()<<"Cancelling photo reset did not restore keyboard focus";
                app.exit(748);return;
            }
            if(!press("fleetRestoreDefaultPhotoButton") ||
               !press("fleetRestoreDefaultConfirm")) {
                qCritical()<<"Fleet photo reset confirmation unavailable";
                app.exit(743);return;
            }
            QTest::qWait(150);
            photo=find(find,window->contentItem(),"fleetInspectorPhoto");
            if(fleetPhotos.hasCustomPhoto("C001") ||
               QFileInfo::exists(c001Sidecar) ||
               !fleetPhotos.hasPhoto("C001") ||
               fleetPhotos.imageFor("C001").isNull() ||
               !photo || photo->property("status").toInt()!=1 ||
               !replacementNotice->property("text").toString().contains(
                   "Default image restored for C001")) {
                qCritical()<<"Reset did not restore seeded image and feedback";
                app.exit(744);return;
            }
            if(!changePhoto->hasActiveFocus()) {
                qCritical()<<"Successful photo reset left keyboard focus on a hidden control";
                app.exit(749);return;
            }
            // Reused ID safety: a deleted C001 later assigned to a Demo
            // Motors vehicle cannot show the old Toyota Corolla artwork.
            // Missing/unregistered IDs also fail closed for local sidecars.
            FleetPhotoStore reusedId(dataDirectory);
            reusedId.setVehicleIdentityLookup([](const QString &id) {
                return id==QStringLiteral("C001")
                    ? QPair<QString,QString>{QStringLiteral("Demo Motors"),
                                             QStringLiteral("Orion")}
                    : QPair<QString,QString>{};
            });
            if(reusedId.hasPhoto("C001") ||
               !reusedId.imageFor("C001").isNull() ||
               reusedId.hasPhoto("C004") ||
               !reusedId.imageFor("C004").isNull()) {
                qCritical()<<"Wrong vehicle identity inherited default or orphan photo";
                app.exit(745);return;
            }
            reusedId.setVehicleIdentityLookup([](const QString &id) {
                return id==QStringLiteral("C001")
                    ? QPair<QString,QString>{QStringLiteral(" Toyota "),
                                             QStringLiteral("corolla")}
                    : QPair<QString,QString>{};
            });
            if(!reusedId.hasPhoto("C001") ||
               reusedId.imageFor("C001").isNull() ||
               reusedId.hasPhoto("C004")) {
                qCritical()<<"Live make/model could not recover correct reference image";
                app.exit(746);return;
            }
            FleetPhotoStore noIdentity(dataDirectory);
            if(noIdentity.hasPhoto("C001") || noIdentity.hasPhoto("C004") ||
               !noIdentity.imageFor("C001").isNull()) {
                qCritical()<<"Image provider allowed unverified vehicle identities";
                app.exit(747);return;
            }
            fleet.selectById("C004");
            QTest::qWait(130);
            const QImage importedShot=window->grabWindow();
            if(importedShot.size()!=window->size() ||
               !importedShot.save("ux-q3-imported-fleet-"+photoViewport+".png","PNG")) {
                qCritical()<<"Fleet imported photo capture failed";
                app.exit(726);return;
            }
            // A small PNG file can still claim huge dimensions. Refuse a
            // rewritten sidecar before the decoder allocates those pixels.
            const QString sidecar=QDir(dataDirectory).filePath(
                "fleet_images/C004.png");
            QImage oversized(1281,961,QImage::Format_RGB32);
            oversized.fill(QColor(31,163,92));
            if(!oversized.save(sidecar,"PNG") ||
               fleetPhotos.hasPhoto("C004") ||
               !fleetPhotos.imageFor("C004").isNull()) {
                qCritical()<<"Fleet oversized sidecar was not rejected";
                app.exit(722);return;
            }
            // Exercise actual QML invalid-image fallback and recovery.
            fleetPhotos.discardStaged();
            QTest::qWait(180);
            if(find(find,window->contentItem(),"fleetInspectorPhoto") ||
               !find(find,window->contentItem(),"fleetInspectorFallbackArt") ||
               photoDisclaimer->property("text").toString()!="Illustration") {
                qCritical()<<"Unsafe image did not reveal clearly labelled silhouette";
                app.exit(750);return;
            }
            if(!image.save(sidecar,"PNG") ||
               !fleetPhotos.hasPhoto("C004") ||
               fleetPhotos.imageFor("C004").size()!=image.size()) {
                qCritical()<<"Fleet valid sidecar could not be recovered";
                app.exit(723);return;
            }
            fleetPhotos.discardStaged();
            QTest::qWait(220);
            auto *recoveredPhoto=find(find,window->contentItem(),
                                     "fleetInspectorPhoto");
            if(!recoveredPhoto || recoveredPhoto->property("status").toInt()!=1 ||
               find(find,window->contentItem(),"fleetInspectorFallbackArt") ||
               photoDisclaimer->property("text").toString()!="Reference image") {
                qCritical()<<"Inspector photo/media label did not recover after valid image restored";
                app.exit(751);return;
            }
            QByteArray fixedV,fixedR;
            if(!readFile(vehicles,&fixedV)||!readFile(rentals,&fixedR)||
               fixedV!=beforeVehicles||fixedR!=beforeRentals) {
                qCritical()<<"Fleet photo changed protected fixture source";
                app.exit(720);return;
            }
            fleetPhotos.forgetVehicle("C004");
            if(fleetPhotos.hasPhoto("C004")||second.hasPhoto("C004")) {
                qCritical()<<"Fleet photo did not return to fallback";
                app.exit(721);return;
            }
            qInfo()<<"Fleet illustrative photos + guarded user image import PASS"
                   <<(compact?"1280x720":"1500x860");
            app.exit(0);
        });
        return app.exec();
    }
#ifdef RENTAL_STUDIO_P7_SQL_LAB
    if(p7SqlLabSmoke) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window)return 603;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",3);
        QTimer::singleShot(1150,&app,[&,window,compact] {
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name && item->isVisible())return item;
                for(auto *child:item->childItems())
                    if(auto *hit=self(self,child,name))return hit;
                return nullptr;
            };
            auto *root=window->contentItem();
            auto *nav=find(find,root,"workspaceNavSQLite Lab");
            auto *mode=find(find,root,"sessionModeNotice");
            auto *overviewFleet=find(find,root,"p7AppOverviewFleet");
            auto *overviewRentals=find(find,root,"p7AppOverviewRentals");
            auto *overviewValue=find(find,root,"p7AppOverviewValue");
            auto *fleetCard=find(find,root,"p7AppFleetSnapshotCard");
            auto *rentalsCard=find(find,root,"p7AppRentalsSnapshotCard");
            auto *fleetText=find(find,root,"p7AppFleetMetrics");
            auto *rentalsText=find(find,root,"p7AppRentalsMetrics");
            auto *receipt=find(find,root,"p7AppReceipt");
            auto *lab=find(find,root,"p7AppSqlLab");
            auto *demoStep=find(find,root,"p7AppNextStep");
            auto *fleetSelection=find(find,root,"p7AppFleetSelected");
            auto *fleetSearch=find(find,root,"p7AppFleetSearch");
            auto *rentalSearch=find(find,root,"p7AppRentalSearch");
            auto *rentalSelection=find(find,root,"p7AppRentalSelected");
            auto *fleetVisible=find(find,root,"p7AppFleetVisibleCount");
            auto *rentalVisible=find(find,root,"p7AppRentalVisibleCount");
            if(!nav||!mode||
               !mode->property("text").toString().contains("SQLITE LAB · ISOLATED")||
               !fleetCard||!rentalsCard||
               !overviewFleet||!overviewRentals||!overviewValue||
               !fleetText||!rentalsText||!receipt||!lab||!demoStep||
               !demoStep->property("text").toString().startsWith("STEP 1/5")||
               !fleetSelection||!fleetSearch||!rentalSearch||
               !rentalSelection||!fleetVisible||!rentalVisible||
               !fleetText->property("text").toString().contains("1 available") ||
               !rentalsText->property("text").toString().contains("Active: 0") ||
               !p7SqlOverview->snapshotAvailable() ||
               p7SqlOverview->totalVehicles()!=1 ||
               p7SqlOverview->availableVehicles()!=1 ||
               p7SqlOverview->activeRentals()!=0 ||
               p7SqlOverview->historyRentals()!=0 ||
               p7SqlOverview->activeBookedLabel()!="R 0.00" ||
               !overviewValue->property("text").toString().contains("R 0.00") ||
               p7SqlFleet->totalCount()!=1 || p7SqlRentals->activeCount()!=0 ||
               p7SqlFleet->visibleCount()!=1 || p7SqlRentals->count()!=0 ||
               !fleetVisible->property("text").toString().contains("1 matching") ||
               !rentalVisible->property("text").toString().contains("0 visible")) {
                qCritical()<<"P7 real app fictional Lab failed to load isolated read models";
                app.exit(604);return;
            }
            // UX-S16: PageDown/PageUp must reach the Lab's lower records
            // with native keyboard input at both window sizes. This is
            // navigation only; the verified SQLite read/command models
            // remain unmodified until an explicit operation is clicked.
            lab->setProperty("contentY",0.0);
            nav->forceActiveFocus(Qt::TabFocusReason);
            QCoreApplication::processEvents();
            const qreal maxScroll = qMax(0.0,
                lab->property("contentHeight").toDouble() - lab->height());
            QTest::keyClick(window,Qt::Key_PageDown);
            QCoreApplication::processEvents();
            const qreal scrolled = lab->property("contentY").toDouble();
            if(maxScroll < 140.0 || scrolled < 100.0 ||
               scrolled > maxScroll + 1.0) {
                qCritical()<<"UX-S16 Lab PageDown failed to reveal lower content"
                           <<scrolled<<maxScroll;
                app.exit(866);return;
            }
            QTest::keyClick(window,Qt::Key_PageUp);
            QCoreApplication::processEvents();
            if(lab->property("contentY").toDouble()>1.0) {
                qCritical()<<"UX-S16 Lab PageUp failed to return to top";
                app.exit(867);return;
            }
            const auto click=[&](const QString &name) {
                auto *target=find(find,root,name);
                if(!target||!target->isEnabled()||target->width()<10||
                   target->height()<10)return false;
                const auto scene=target->mapToScene(
                    QPointF(target->width()/2.0,target->height()/2.0));
                QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,scene.toPoint());
                QCoreApplication::processEvents();
                return true;
            };
            if(!click("p7AppPreview")||!p7SqlController->canConfirm()||
               !demoStep->property("text").toString().startsWith("STEP 2/5")||
               p7SqlFleet->availableCount()!=1||p7SqlRentals->activeCount()!=0||
               !p7SqlController->rejectNextBookingForTest() ||
               !click("p7AppConfirm")||p7SqlController->rentalId()!=""||
               !demoStep->property("text").toString().startsWith("STEP 2/5")||
               p7SqlFleet->availableCount()!=1||p7SqlRentals->activeCount()!=0||
               !receipt->property("text").toString().contains("not issued") ||
               p7SqlOverview->activeRentals()!=0 ||
               p7SqlOverview->activeBookedLabel()!="R 0.00") {
                qCritical()<<"P7 real app leaked uncommitted/rejected SQL booking";
                app.exit(605);return;
            }
            if(!click("p7AppConfirm") || p7SqlController->rentalId()!="R001"||
               !demoStep->property("text").toString().startsWith("STEP 3/5")||
               p7SqlFleet->availableCount()!=0||p7SqlRentals->activeCount()!=1||
               !fleetText->property("text").toString().contains("0 available")||
               !rentalsText->property("text").toString().contains("Active: 1")||
               p7SqlOverview->activeRentals()!=1 ||
               p7SqlOverview->availableVehicles()!=0 ||
               p7SqlOverview->activeBookedLabel()!="R 1350.00" ||
               !overviewValue->property("text").toString().contains("R 1350.00")||
               !click("p7AppReturn")||
               !demoStep->property("text").toString().startsWith("STEP 4/5")||
               p7SqlFleet->availableCount()!=1||
               !click("p7AppArchive")||
               !demoStep->property("text").toString().startsWith("STEP 5/5")||
               p7SqlRentals->historyCount()!=1||
               !click("p7AppRetire")||
               !demoStep->property("text").toString().startsWith("COMPLETE")||
               p7SqlFleet->availableCount()!=0||
               !p7SqlController->independentReopen()||
               !p7SqlRentals->snapshotAvailable()||
               !p7SqlFleet->snapshotAvailable() ||
               !p7SqlOverview->snapshotAvailable() ||
               p7SqlOverview->activeRentals()!=0 ||
               p7SqlOverview->historyRentals()!=1 ||
               p7SqlOverview->availableVehicles()!=0 ||
               p7SqlOverview->activeBookedLabel()!="R 0.00" ||
               p7SqlRentals->count()!=1 || p7SqlFleet->visibleCount()!=1) {
                qCritical()<<"P7 real app SQL commit and QML availability parity FAILED";
                app.exit(606);return;
            }
            // Actual-application read-only record explorer: the same
            // production-facing RentalListModel owns status/selection rules,
            // while the fictional SQL Fleet adapter owns availability filtering.
            lab->setProperty("contentY",420.0);
            QCoreApplication::processEvents();
            auto *fleetList=find(find,root,"p7AppFleetList");
            auto *rentalList=find(find,root,"p7AppRentalList");
            if(!fleetList || !rentalList ||
               fleetList->height()<90.0 || rentalList->height()<90.0) {
                qCritical()<<"P7 reusable record explorer list rows are too cramped";
                app.exit(618);return;
            }
            if(!click("p7AppFleetRowC001") ||
               p7SqlFleet->selectedVehicleId()!="C001" ||
               !fleetSelection->property("text").toString().contains("4 doors") ||
               !click("p7AppRentalRowR001") ||
               p7SqlRentals->selectedRentalId()!="R001" ||
               !rentalSelection->property("text").toString().contains("Archived") ||
               !rentalSelection->property("text").toString().contains("1350.00")) {
                qCritical()<<"P7 actual-app record inspector failed real QML row selection";
                app.exit(609);return;
            }
            p7SqlFleet->setStatusFilter("Available");
            p7SqlRentals->setStatusFilter("Active");
            QCoreApplication::processEvents();
            if(p7SqlFleet->visibleCount()!=0 ||
               !p7SqlFleet->selectedVehicleId().isEmpty() ||
               p7SqlRentals->count()!=0 ||
               !p7SqlRentals->selectedRentalId().isEmpty() ||
               !fleetVisible->property("text").toString().contains("0 matching") ||
               !rentalVisible->property("text").toString().contains("0 visible")) {
                qCritical()<<"P7 SQL Fleet/Rentals filtered inspector showed hidden records";
                app.exit(610);return;
            }
            p7SqlFleet->setStatusFilter("Unavailable");
            p7SqlRentals->setStatusFilter("History");
            QCoreApplication::processEvents();
            if(p7SqlFleet->visibleCount()!=1 || p7SqlRentals->count()!=1 ||
               !fleetVisible->property("text").toString().contains("1 matching") ||
               !rentalVisible->property("text").toString().contains("1 visible") ||
               !p7SqlFleet->selectById("c001") ||
               !p7SqlRentals->selectById("r001")) {
                qCritical()<<"P7 SQL C++ read-only history/status filter mismatch";
                app.exit(611);return;
            }
            // UX-S13: true C++ RentalListModel text filter in the actual
            // P7 application explorer. Match contract and vehicle IDs only,
            // combine with History, preserve full-snapshot Overview metrics.
            rentalSearch->setProperty("text",QStringLiteral("not-found-in-demo"));
            QCoreApplication::processEvents();
            if(p7SqlRentals->searchText()!="not-found-in-demo" ||
               p7SqlRentals->count()!=0 ||
               !p7SqlRentals->selectedRentalId().isEmpty() ||
               p7SqlRentals->historyCount()!=1 ||
               p7SqlOverview->historyRentals()!=1 ||
               p7SqlRentals->selectById("R001") ||
               !rentalSelection->property("text").toString().contains(
                   "Select a contract to inspect")) {
                qCritical()<<"UX-S13 search showed hidden record or changed Overview";
                app.exit(860);return;
            }
            rentalSearch->setProperty("text",QStringLiteral("  c001  "));
            QCoreApplication::processEvents();
            if(p7SqlRentals->searchText()!="c001" ||
               p7SqlRentals->count()!=1 ||
               !p7SqlRentals->selectById("r001") ||
               p7SqlRentals->selectedRentalId()!="R001" ||
               !rentalSelection->property("text").toString().contains(
                   "Archived")) {
                qCritical()<<"UX-S13 case-insensitive vehicle ID search failed";
                app.exit(861);return;
            }
            rentalSearch->setProperty("text",QStringLiteral("r001"));
            QCoreApplication::processEvents();
            if(p7SqlRentals->searchText()!="r001" ||
               p7SqlRentals->count()!=1 ||
               p7SqlRentals->selectedRentalId()!="R001" ||
               !rentalSelection->property("text").toString().contains(
                   "1350.00")) {
                qCritical()<<"UX-S13 contract ID search lost verified receipt";
                app.exit(862);return;
            }
            p7SqlRentals->setStatusFilter("Active");
            QCoreApplication::processEvents();
            if(p7SqlRentals->count()!=0 ||
               !p7SqlRentals->selectedRentalId().isEmpty() ||
               p7SqlRentals->historyCount()!=1) {
                qCritical()<<"UX-S13 lifecycle and ID filter do not compose";
                app.exit(863);return;
            }
            p7SqlRentals->setStatusFilter("History");
            p7SqlRentals->selectById("R001");
            // Actual QML search filters verified C++ Fleet DTOs in conjunction
            // with availability; hidden selection never leaks through.
            fleetSearch->setProperty("text",QStringLiteral("not-a-fictional-car"));
            QCoreApplication::processEvents();
            if(p7SqlFleet->searchText()!="not-a-fictional-car" ||
               p7SqlFleet->visibleCount()!=0 ||
               !p7SqlFleet->selectedVehicleId().isEmpty() ||
               p7SqlFleet->totalCount()!=1 ||
               p7SqlFleet->selectById("C001") ||
               !fleetVisible->property("text").toString().contains("0 matching") ||
               !find(find,root,"p7AppFleetEmpty")) {
                qCritical()<<"P7 Fleet search exposed hidden fictional record";
                app.exit(614);return;
            }
            fleetSearch->setProperty("text",QStringLiteral("corOLLa"));
            QCoreApplication::processEvents();
            if(p7SqlFleet->visibleCount()!=1 ||
               !p7SqlFleet->selectById("c001") ||
               p7SqlFleet->selectedVehicleId()!="C001" ||
               !fleetSelection->property("text").toString().contains("4 doors") ||
               !fleetVisible->property("text").toString().contains("1 matching") ||
               find(find,root,"p7AppFleetEmpty")) {
                qCritical()<<"P7 case-insensitive Fleet search did not restore visible match";
                app.exit(615);return;
            }
            // Search remains active across a failed read and recovery.
            // Explicit provider failure must retain known-good C++ records
            // but HIDE every unverifiable row and selected contract in QML.
            p7RefuseFleetReads=true;
            p7RefuseRentalReads=true;
            p7RefuseOperationsReads=true;
            p7SqlOverview->refresh();
            p7SqlFleet->refresh();
            p7SqlRentals->refresh();
            QCoreApplication::processEvents();
            if(p7SqlFleet->snapshotAvailable() ||
               p7SqlRentals->snapshotAvailable() ||
               p7SqlOverview->snapshotAvailable() ||
               !overviewFleet->property("text").toString().contains("unavailable") ||
               !overviewRentals->property("text").toString().contains("unavailable") ||
               !overviewValue->property("text").toString().contains("unavailable") ||
               p7SqlFleet->totalCount()!=1 || p7SqlRentals->count()!=1 ||
               !fleetSelection->property("text").toString().contains("snapshot unavailable") ||
               !rentalSelection->property("text").toString().contains("snapshot unavailable") ||
               rentalSearch->isEnabled() ||
               p7SqlRentals->searchText()!="r001" ||
               p7SqlRentals->selectById("R001") ||
               !fleetVisible->property("text").toString().contains("Unverified") ||
               !rentalVisible->property("text").toString().contains("Unverified")) {
                qCritical()<<"P7 real app SQL read failure leaked stale inspector";
                app.exit(612);return;
            }
            p7RefuseFleetReads=false;
            p7RefuseRentalReads=false;
            p7RefuseOperationsReads=false;
            // The Overview panel sits below the always-accessible
            // booking controls on compact windows. Scroll as a real
            // operator would to retry the failed read, then return to
            // the top for the existing Fleet/Rentals retry controls.
            lab->setProperty("contentY",230.0);
            QCoreApplication::processEvents();
            if(!click("p7AppRetryOverview")) {
                qCritical()<<"P7 actual-app Overview retry control inaccessible";
                app.exit(617);return;
            }
            lab->setProperty("contentY",0.0);
            QCoreApplication::processEvents();
            if(!click("p7AppRetryFleet") || !click("p7AppRetryRentals") ||
               !p7SqlFleet->snapshotAvailable() ||
               !p7SqlRentals->snapshotAvailable() ||
               !p7SqlOverview->snapshotAvailable() ||
               p7SqlOverview->activeBookedLabel()!="R 0.00" ||
               p7SqlFleet->visibleCount()!=1 || p7SqlRentals->count()!=1 ||
               !rentalSearch->isEnabled() ||
               p7SqlRentals->searchText()!="r001" ||
               !fleetSelection->property("text").toString().contains("4 doors") ||
               !rentalSelection->property("text").toString().contains("Archived")) {
                qCritical()<<"P7 actual-app SQL read recovery did not restore verified inspector";
                app.exit(613);return;
            }
            // Main app's other three workspaces remain a distinct protected
            // fictional text demo; SQL actions must not mutate their source.
            QByteArray vafter,rafter;
            if(!readFile(vehicles,&vafter)||!readFile(rentals,&rafter)||
               vafter!=beforeVehicles||rafter!=beforeRentals||
               rentalModel.activeCount()!=1||fleet.availableCount()!=4) {
                qCritical()<<"P7 SQL Lab modified baseline Qt Quick demo";
                app.exit(607);return;
            }
            const QString size=compact?"1280x720":"1500x860";
            if(window->grabWindow().isNull()||
               !window->grabWindow().save("p7-real-app-fictional-sql-lab-"+size+".png","PNG")) {
                qCritical()<<"P7 real app fictional Lab capture failed";
                app.exit(608);return;
            }
            qInfo()<<"P7 actual Qt Quick fictional SQL Lab PASS"
                   <<size<<"separate engine, exact cents, committed UI, no source mutation";
            app.exit(0);
        });
    }
#endif
    if (uiSeedTest || uiReopenTest || failureTest) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window) return 323;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",2);
        QTimer::singleShot(1050,&app,[&,window,compact] {
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->isVisible() && item->objectName()==name)return item;
                for(auto *child:item->childItems())
                    if(auto *found=self(self,child,name))return found;
                return nullptr;
            };
            const auto press=[&](QQuickItem *control) {
                control->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window,Qt::Key_Space);
                QCoreApplication::processEvents();
                QTest::qWait(90);
            };
            auto *root=window->contentItem();
            auto *newRental=find(find,root,"sidebarNewRentalButton");
            auto *message=find(find,root,"rentalBookingMessage");
            QByteArray originalLedger,currentLedger;
            const QString ledgerPath=QDir(dataDirectory).filePath("rentals.txt");
            if(!newRental||!message||
               !readFile(ledgerPath,&originalLedger) ||
               originalLedger.isEmpty()) {
                qCritical()<<"P5 Qt UI session failed to load verified ledger";
                app.exit(324);return;
            }
            if(uiReopenTest) {
                // This native window is in a genuinely separate executable
                // process from the initial QML booking; no in-memory carryover.
                window->setProperty("workspace",0);
                QCoreApplication::processEvents();
                QTest::qWait(140);
                auto *active=find(find,root,"overviewActiveCard");
                auto *value=find(find,root,"overviewValueCard");
                auto *mode=find(find,root,"sessionModeNotice");
                if(!active||!value||!mode||
                   !mode->property("text").toString().contains("BOOKINGS SAVED")||
                   active->property("value").toString()!="2" ||
                   value->property("value").toString()!="R 2750.00" ||
                   rentalModel.activeCount()!=2 ||
                   rentalModel.count()!=3 || fleet.availableCount()!=3 ||
                   !system.findVehicleById("C001") ||
                   !system.findVehicleById("C001")->getIsRented() ||
                   !system.findRentalById("R003")) {
                    qCritical()<<"P5 reopened REAL QML Overview did not show committed rental";
                    app.exit(325);return;
                }
                const auto shot=window->grabWindow();
                if(shot.isNull() ||
                   !shot.save("p5-qt-persistent-reopened-overview.png","PNG")) {
                    qCritical()<<"P5 reopened QML Overview screenshot missing";
                    app.exit(326);return;
                }
                window->setProperty("workspace",2);
                QCoreApplication::processEvents();
                QTest::qWait(140);
                newRental=find(find,root,"sidebarNewRentalButton");
                if(!newRental)return app.exit(327);
                press(newRental);
                auto *vehicle=find(find,root,"bookingVehicleId");
                auto *preview=find(find,root,"bookingPreviewButton");
                auto *confirm=find(find,root,"bookingConfirmButton");
                auto *errorLabel=find(find,root,"bookingDialogFeedback");
                if(!vehicle || !preview || !confirm || !errorLabel) {
                    qCritical()<<"P5 reopened QML duplicate-check controls missing";
                    app.exit(328);return;
                }
                vehicle->setProperty("text",QStringLiteral("C001"));
                press(preview);
                if(bookingActions.canConfirm() || confirm->isEnabled() ||
                   errorLabel->property("text").toString()!="Vehicle is not available." ||
                   !readFile(ledgerPath,&currentLedger) ||
                   currentLedger!=originalLedger ||
                   !window->property("bookingReceiptNotice").toString().isEmpty()) {
                    qCritical()<<"P5 reopened QML duplicate refusal / no-receipt failed";
                    app.exit(329);return;
                }
                qInfo()<<"P5 real QML cross-process restart + duplicate refusal PASS";
                app.exit(0);return;
            }
            // Initial QML booking and injected writer failure share the same
            // actual modal path, quote, keyboard-activated buttons and C++ API.
            if((!failureTest && (rentalModel.count()!=2 ||
                                 fleet.availableCount()!=4)) ||
               (failureTest && rentalModel.count()!=2)) {
                qCritical()<<"P5 initial QML booking baseline unexpected";
                app.exit(330);return;
            }
            press(newRental);
            auto *vehicle=find(find,root,"bookingVehicleId");
            auto *days=find(find,root,"bookingDays");
            auto *preview=find(find,root,"bookingPreviewButton");
            auto *confirm=find(find,root,"bookingConfirmButton");
            auto *errorLabel=find(find,root,"bookingDialogFeedback");
            auto *name=find(find,root,"bookingCustomer");
            auto *phone=find(find,root,"bookingPhone");
            if(!vehicle||!days||!preview||!confirm||!errorLabel||
               !name||!phone) {
                qCritical()<<"P5 guided native QML rental controls missing";
                app.exit(331);return;
            }
            vehicle->setProperty("text",QStringLiteral("C001"));
            days->setProperty("value",3);
            press(preview);
            if(!bookingActions.canConfirm() ||
               !bookingActions.quoteSummary().contains("Total R 1350.00") ||
               !confirm->isEnabled()) {
                qCritical()<<"P5 QML did not review authoritative Corolla quote";
                app.exit(332);return;
            }
            if(failureTest) {
                name->setProperty("text",QStringLiteral("Fictional Write Failure"));
                phone->setProperty("text",QStringLiteral("0000000000"));
            } else if(name->isEnabled() || phone->isEnabled() ||
                      !persistentDemo) {
                qCritical()<<"P5 fictional-only QML contact restriction missing";
                app.exit(333);return;
            }
            press(confirm);
            QTest::qWait(160);
            if(failureTest) {
                if(errorLabel->property("text").toString()!=
                       "Booking was not saved. No rental confirmed." ||
                   !window->property("bookingReceiptNotice").toString().isEmpty() ||
                   !bookingActions.lastRentalId().isEmpty() ||
                   rentalModel.count()!=2 ||
                   rentalModel.activeCount()!=1 || fleet.availableCount()!=4 ||
                   !readFile(ledgerPath,&currentLedger) ||
                   currentLedger!=originalLedger ||
                   system.findRentalById("R003")) {
                    qCritical()<<"P5 injected QSaveFile failure changed disk/state or issued receipt";
                    app.exit(334);return;
                }
                qInfo()<<"P5 real QML rejected injected durable write PASS";
                app.exit(0);return;
            }
            if(bookingActions.lastRentalId()!="R003" ||
               message->property("text").toString()!="Booking R003 saved." ||
               window->property("bookingReceiptNotice").toString()!="Booking R003 saved." ||
               rentalModel.activeCount()!=2 ||
               fleet.availableCount()!=3 ||
               !system.findVehicleById("C001")->getIsRented() ||
               !readFile(ledgerPath,&currentLedger) ||
               currentLedger==originalLedger) {
                qCritical()<<"P5 real QML did not save fictional R003 with receipt";
                app.exit(335);return;
            }
            const auto shot=window->grabWindow();
            if(shot.isNull() ||
               !shot.save("p5-qt-persistent-booking-receipt.png","PNG")) {
                qCritical()<<"P5 real QML persistent receipt screenshot missing";
                app.exit(336);return;
            }
            qInfo()<<"P5 native QML first process persisted fake rental PASS";
            app.exit(0);
        });
    }
    if (parser.isSet("ui-smoke")) {
        auto *window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window) {
            qCritical() << "P2 QML root is not a native Quick window";
            return 6;
        }
        auto *gallery = window->findChild<QQuickItem*>("fleetGallery");
        auto *table = window->findChild<QQuickItem*>("fleetTable");
        auto *snapshot = window->findChild<QQuickItem*>("fleetSnapshotSummary");
        auto *galleryToggle = window->findChild<QQuickItem*>("fleetGalleryToggle");
        auto *tableToggle = window->findChild<QQuickItem*>("fleetTableToggle");
        auto *emptyArt = window->findChild<QQuickItem*>("fleetInspectorEmptyArt");
        auto *shown = window->findChild<QQuickItem*>("fleetShownCount");
        auto *emptyTable = window->findChild<QQuickItem*>("fleetTableEmpty");
        if (!gallery || !table || !snapshot || !galleryToggle || !tableToggle ||
            !shown || !emptyTable || !emptyArt || !emptyArt->isVisible() ||
            shown->property("text").toString() != "(5 shown)" ||
            !galleryToggle->property("visuallySelected").toBool() ||
            tableToggle->property("visuallySelected").toBool() ||
            fleet.totalCount() != 5 || fleet.availableCount() != 4 ||
            fleet.carCount() != 3 || fleet.motorcycleCount() != 2 ||
            fleet.rentedCount() != 1 ||
            !snapshot->property("text").toString().contains(
                QStringLiteral("1 of 5 currently rented"))) {
            qCritical() << "P2 native QML view objects or demo domain data missing";
            return 7;
        }

        const bool compact = parser.isSet("compact");
        window->setWidth(compact ? 1280 : 1500);
        window->setHeight(compact ? 720 : 860);
        const QString suffix = compact ? "1280x720" : "1500x860";
        // A real QQuickWindow screenshot is deliberately required for this
        // optional CI smoke. Failure cannot masquerade as passing native UI.
        QTimer::singleShot(1000, &app, [&, suffix] {
            if (!gallery->isVisible() || table->isVisible()) {
                qCritical() << "Gallery is not the visible initial Fleet view";
                app.exit(8); return;
            }
            QImage image = window->grabWindow();
            const QSize expected(compact ? 1280 : 1500, compact ? 720 : 860);
            if (image.size() != expected) {
                qCritical() << "Native screenshot viewport mismatch; expected"
                            << expected << "actual" << image.size();
                app.exit(12); return;
            }
            if (image.isNull() ||
                !image.save("p2-qt-gallery-" + suffix + ".png", "PNG")) {
                qCritical() << "Cannot capture actual QML Gallery";
                app.exit(9); return;
            }
            fleet.setQuery("Honda");
            fleet.selectById("M001");
            window->setProperty("gallery", false);
            QTimer::singleShot(450, &app, [&, suffix] {
                if (gallery->isVisible() || !table->isVisible() ||
                    fleet.selectedId() != "M001" ||
                    static_cast<FleetProxy*>(fleet.visibleModel())->rowCount() != 1 ||
                    !snapshot->property("text").toString().contains(
                        QStringLiteral("3 cars")) ||
                    !snapshot->property("text").toString().contains(
                        QStringLiteral("2 motorcycles")) ||
                    fleet.rentedCount() != 1 ||
                    galleryToggle->property("visuallySelected").toBool() ||
                    !tableToggle->property("visuallySelected").toBool() ||
                    shown->property("text").toString() != "(1 shown)") {
                    qCritical() << "QML Gallery/Table visibility or C++ model parity failed";
                    app.exit(10); return;
                }
                QImage tableImage = window->grabWindow();
                if (tableImage.size() != QSize(compact ? 1280 : 1500,
                                              compact ? 720 : 860)) {
                    qCritical() << "Native Table screenshot viewport mismatch; actual"
                                << tableImage.size();
                    app.exit(13); return;
                }
                if (tableImage.isNull() ||
                    !tableImage.save("p2-qt-table-" + suffix + ".png", "PNG")) {
                    qCritical() << "Cannot capture actual QML Table";
                    app.exit(11); return;
                }
                fleet.setQuery("no-such-vehicle-000");
                QCoreApplication::processEvents();
                if (static_cast<FleetProxy*>(fleet.visibleModel())->rowCount() != 0 ||
                    !emptyTable->isVisible() ||
                    shown->property("text").toString() != "(0 shown)" ||
                    fleet.carCount() != 3 || fleet.motorcycleCount() != 2) {
                    qCritical() << "P6 Fleet no-results state / full-snapshot count mismatch";
                    app.exit(14); return;
                }
                fleet.setQuery("");
                QCoreApplication::processEvents();
                if (emptyTable->isVisible() ||
                    shown->property("text").toString() != "(5 shown)") {
                    qCritical() << "P6 Fleet no-results reset did not restore Table";
                    app.exit(15); return;
                }
                qInfo() << "P2 native Qt Quick UI smoke PASS:" << suffix
                        << "Gallery, Table, selected ID, filtered/empty snapshot";
                app.exit(0);
            });
        });
    }
    if (parser.isSet("input-smoke")) {
        auto *window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window) return 20;
        QTimer::singleShot(850, &app, [&, window] {
            auto *tableToggle = window->findChild<QQuickItem*>("fleetTableToggle");
            auto *galleryToggle = window->findChild<QQuickItem*>("fleetGalleryToggle");
            auto *inspector = window->findChild<QQuickItem*>("vehicleInspector");
            if (!tableToggle || !galleryToggle || !inspector) {
                qCritical() << "Missing native keyboard test controls"; app.exit(21); return;
            }
            tableToggle->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window, Qt::Key_Space);
            QCoreApplication::processEvents();
            if (window->property("gallery").toBool()) {
                qCritical() << "Space did not activate Table button"; app.exit(22); return;
            }
            // ListView delegates are dynamically instantiated and can be
            // visually parented without being QObject children of QQuickWindow.
            // Wait for the actual native frame and traverse *visual* items.
            QTest::qWait(180);
            const auto findVisual = [](const auto &self, QQuickItem *item,
                                       const QString &name) -> QQuickItem* {
                if (!item) return nullptr;
                if (item->objectName() == name && item->isVisible()) return item;
                for (QQuickItem *child : item->childItems()) {
                    if (auto *match = self(self, child, name)) return match;
                }
                return nullptr;
            };
            auto *tableView = findVisual(findVisual, window->contentItem(),
                                         QStringLiteral("fleetTable"));
            auto *row = findVisual(findVisual, tableView,
                                   QStringLiteral("fleetTableRow"));
            if (!tableView || !tableView->isVisible() ||
                !row || !row->property("vehicleId").isValid()) {
                qCritical() << "Visible Qt Table delegate not found after layout"
                            << "table" << tableView
                            << "row" << row;
                app.exit(23); return;
            }
            const QString expectedId = row->property("vehicleId").toString();
            row->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window, Qt::Key_Space);
            QCoreApplication::processEvents();
            if (fleet.selectedId() != expectedId) {
                qCritical() << "Space did not select C++ row" << fleet.selectedId()
                            << expectedId;
                app.exit(24); return;
            }
            QTest::keyClick(window, Qt::Key_Escape);
            QCoreApplication::processEvents();
            if (!fleet.selectedId().isEmpty()) {
                qCritical() << "Escape did not clear Inspector"; app.exit(25); return;
            }
            galleryToggle->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window, Qt::Key_Space);
            QCoreApplication::processEvents();
            if (!window->property("gallery").toBool()) {
                qCritical() << "Space did not restore Gallery"; app.exit(26); return;
            }

            // Resize the *same* running Qt Quick window rather than creating
            // separate fixed-size screenshots. No restart or mock view swap.
            window->resize(1280, 720);
            QTimer::singleShot(300, &app, [&, window, inspector] {
                const QPointF topLeft = inspector->mapToScene(QPointF(0, 0));
                const QPointF bottomRight = inspector->mapToScene(
                    QPointF(inspector->width(), inspector->height()));
                if (window->width() != 1280 || window->height() != 720 ||
                    topLeft.x() < -1 || topLeft.y() < -1 ||
                    bottomRight.x() > window->width() + 1 ||
                    bottomRight.y() > window->height() + 1) {
                    qCritical() << "Live compact resize clipped Inspector"
                                << topLeft << bottomRight
                                << window->width() << window->height();
                    app.exit(27); return;
                }
                window->resize(1500, 860);
                QTimer::singleShot(200, &app, [&, window] {
                    if (window->size() != QSize(1500, 860)) {
                        qCritical() << "Live wide resize failed" << window->size();
                        app.exit(28); return;
                    }
                    qInfo() << "P2 REAL keyboard Space/Escape and live compact/wide resize PASS";
                    app.exit(0);
                });
            });
        });
    }
    if (parser.isSet("arrows-smoke")) {
        auto *window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window) return 40;
        // The compact viewport has two visual columns. Validate *real*
        // QQuickWindow key delivery and C++ proxy selection, not an isolated JS helper.
        window->resize(1280, 720);
        QTimer::singleShot(900, &app, [&, window] {
            auto *gallery = window->findChild<QQuickItem*>("fleetGallery");
            auto *table = window->findChild<QQuickItem*>("fleetTable");
            auto *search = window->findChild<QQuickItem*>("fleetSearchField");
            if (!gallery || !table || !search || gallery->width() <= 0 ||
                !gallery->property("cellWidth").isValid()) {
                qCritical() << "Missing keyboard navigation views"; app.exit(41); return;
            }
            fleet.clearSelection();
            gallery->forceActiveFocus(Qt::TabFocusReason);
            auto press = [&](Qt::Key key, const QString &expected, int error) {
                QTest::keyClick(window, key);
                QCoreApplication::processEvents();
                if (fleet.selectedId() != expected) {
                    qCritical() << "Arrow did not select the correct C++ proxy ID:"
                                << key << "expected" << expected
                                << "actual" << fleet.selectedId();
                    app.exit(error); return false;
                }
                return true;
            };
            if (!press(Qt::Key_Right, "C002", 42) ||
                !press(Qt::Key_Right, "M001", 43) ||
                !press(Qt::Key_Down, "C001", 44) ||
                !press(Qt::Key_Left, "C003", 45) ||
                !press(Qt::Key_Up, "C002", 46)) return;
            // The read-only inspector follows visual navigation, and moving
            // within the first rows does not flick the Gallery independently.
            if (fleet.selectedName() != "Ford Ranger" ||
                gallery->property("contentY").toDouble() > 1.0) {
                qCritical() << "Inspector mismatch or stray scroll on arrow" ;
                app.exit(47); return;
            }
            // A keyboard-only Backtab/Tab round-trip must not trap focus.
            QTest::keyClick(window, Qt::Key_Backtab);
            QCoreApplication::processEvents();
            if (gallery->hasActiveFocus()) {
                qCritical() << "Shift+Tab trapped focus inside Fleet Gallery";
                app.exit(48); return;
            }
            QTest::keyClick(window, Qt::Key_Tab);
            QCoreApplication::processEvents();
            if (!gallery->hasActiveFocus()) {
                qCritical() << "Tab failed to return to Gallery focus scope";
                app.exit(49); return;
            }
            // Input caret arrow is NOT a vehicle selection shortcut.
            search->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window, Qt::Key_Down);
            QCoreApplication::processEvents();
            if (fleet.selectedId() != "C002") {
                qCritical() << "Search input stole vehicle arrow handling";
                app.exit(50); return;
            }
            window->setProperty("gallery", false);
            table->forceActiveFocus(Qt::TabFocusReason);
            if (!press(Qt::Key_Down, "M001", 51) ||
                !press(Qt::Key_Down, "C003", 52) ||
                !press(Qt::Key_Up, "M001", 53)) return;
            fleet.setStatus("Available");
            if (fleet.selectedVisibleIndex() != 0) {
                qCritical() << "Filtered table did not retain visible Honda ID";
                app.exit(54); return;
            }
            fleet.selectById("C002"); // The rented Ranger is now filtered out.
            if (fleet.selectedVisibleIndex() != -1 ||
                !press(Qt::Key_Down, "M001", 55)) return;
            qInfo() << "P2 REAL QML arrow navigation / Tab/Backtab / no stray Gallery scrolling PASS";
            app.exit(0);
        });
    }
    if (parser.isSet("focus-edges-smoke")) {
        auto *window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window) return 60;
        window->resize(1280, 720);
        QTimer::singleShot(900, &app, [&, window] {
            auto *gallery = window->findChild<QQuickItem*>("fleetGallery");
            auto *table = window->findChild<QQuickItem*>("fleetTable");
            auto *search = window->findChild<QQuickItem*>("fleetSearchField");
            if (!gallery || !table || !search) {
                qCritical() << "Missing real QML Fleet collections"; app.exit(61); return;
            }
            const auto step = [&](Qt::Key key, const QString &id, int error) {
                QTest::keyClick(window, key);
                QCoreApplication::processEvents();
                if (fleet.selectedId() != id) {
                    qCritical() << "Focus edge mismatch" << key << id << fleet.selectedId();
                    app.exit(error); return false;
                }
                return true;
            };
            // A user who filters out the inspected Ranger must no longer see
            // an Inspector for an invisible record.
            fleet.selectById("C002");
            fleet.setStatus("Available");
            if (!fleet.selectedId().isEmpty()) {
                // rowCount is a model method, not a Qt property.
                qCritical() << "Filtered-out selection was not dismissed";
                app.exit(62); return;
            }
            fleet.selectById("C001");
            fleet.setSortBy("Rate");
            QTest::qWait(90);
            gallery->forceActiveFocus(Qt::TabFocusReason);
            QCoreApplication::processEvents();
            if (fleet.selectedVisibleIndex() != 3 ||
                gallery->property("currentIndex").toInt() != 3) {
                qCritical() << "Sort left gallery focus on incorrect vehicle row"
                            << fleet.selectedVisibleIndex()
                            << gallery->property("currentIndex").toInt();
                app.exit(63); return;
            }
            if (!step(Qt::Key_Left, "C003", 64) ||
                !step(Qt::Key_Right, "C001", 65)) return;
            const qreal scrollBefore = gallery->property("contentY").toDouble();
            if (!step(Qt::Key_Right, "C001", 66) ||
                std::abs(gallery->property("contentY").toDouble()-scrollBefore)>1.1) {
                qCritical() << "Right boundary must not wrap or scroll";
                app.exit(67); return;
            }
            // Matching and empty search both reconcile the Inspector; Up
            // with no records may neither select nor scroll.
            fleet.setQuery("Toyota");
            QTest::qWait(60);
            if (fleet.selectedId() != "C001" ||
                fleet.selectedVisibleIndex() != 0 ||
                gallery->property("currentIndex").toInt() != 0) {
                qCritical() << "Matching query did not preserve stable vehicle ID";
                app.exit(68); return;
            }
            fleet.setQuery("no-such-demo-vehicle");
            QTest::qWait(60);
            if (!fleet.selectedId().isEmpty() ||
                gallery->property("count").toInt() != 0 ||
                !step(Qt::Key_Down, QString(), 69)) {
                qCritical() << "Empty query left selected stale vehicle";
                app.exit(70); return;
            }
            fleet.setQuery("");
            fleet.setStatus("All");
            fleet.setSortBy("Name");
            window->resize(1500, 860); // Three visual Gallery columns.
            QTest::qWait(100);
            fleet.selectById("C002");
            gallery->forceActiveFocus(Qt::TabFocusReason);
            QTest::qWait(60);
            const int columns = qRound(gallery->width() /
                                       gallery->property("cellWidth").toDouble());
            if (columns != 3 ||
                !step(Qt::Key_Down, "C001", 71) ||
                !step(Qt::Key_Left, "C001", 72) ||
                !step(Qt::Key_Right, "M002", 73)) {
                qCritical() << "Responsive grid direction wrong:" << columns;
                app.exit(74); return;
            }
            search->forceActiveFocus(Qt::TabFocusReason);
            if (!step(Qt::Key_Left, "M002", 75)) return;
            qInfo() << "P2 real Qt Grid/Table focus boundary, filtered Inspector,"
                       " sort-ID sync and responsive grid PASS";
            app.exit(0);
        });
    }
    // UX-S1 regression: exercise the actual Qt Quick Rentals workspace
    // whose lower record panel was clipped on the owner's Windows screen.
    if(parser.isSet("rentals-page-scroll-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window)return 805;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",2);
        QTimer::singleShot(1050,&app,[&,window,compact] {
            auto *page=window->findChild<QQuickItem*>("rentalsPageScroll");
            auto *list=window->findChild<QQuickItem*>("rentalList");
            auto *panel=window->findChild<QQuickItem*>("rentalRecordsPanel");
            auto *details=window->findChild<QQuickItem*>("rentalDetailsButton");
            // Page scrolling must originate on an actual rendered
            // control INSIDE the page, not the removed register booking
            // action. A Repeater's delegates belong to Qt's visual tree
            // and are not guaranteed QObject children of QQuickWindow.
            const auto findVisual=[](const auto &self,QQuickItem *item,
                                     const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name && item->isVisible())return item;
                for(auto *child:item->childItems())
                    if(auto *found=self(self,child,name))return found;
                return nullptr;
            };
            auto *scrollOrigin=findVisual(findVisual,window->contentItem(),
                                          "rentalFilterAll");
            if(!page||!list||!panel||!details||!scrollOrigin||
               !page->isVisible()||!list->isVisible()||
               rentalModel.count()!=2) {
                qCritical()<<"UX-S1 native Rentals scroller or data absent";
                app.exit(806);return;
            }
            // A large desktop may fit the whole page with only a few
            // pixels of overflow. Compact must genuinely scroll; both
            // layouts must expose the contract action without clipping.
            const double maximum=std::max(0.0,
                page->property("contentHeight").toDouble()-page->height());
            if((compact && maximum<90) || panel->height()<340 ||
               list->height()<160 ||
               page->property("contentY").toDouble()>2) {
                qCritical()<<"UX-S1 Rentals page cannot reach records"
                           <<maximum<<panel->height()<<list->height();
                app.exit(807);return;
            }
            scrollOrigin->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_PageDown);
            QTest::qWait(120);
            if(maximum>3 &&
               page->property("contentY").toDouble()<std::min(50.0,maximum)-2) {
                qCritical()<<"UX-S1 keyboard PageDown did not reveal records";
                app.exit(808);return;
            }
            page->setProperty("contentY",maximum);
            QCoreApplication::processEvents();
            QTest::qWait(100);
            // QML ListView delegates belong to the visual QQuickItem
            // hierarchy, not necessarily the window's QObject parent chain.
            // Query the live visual tree after the page reveals its rows.
            const auto findVisible=[](const auto &self,QQuickItem *item,
                                      const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name && item->isVisible())return item;
                for(auto *child:item->childItems())
                    if(auto *hit=self(self,child,name))return hit;
                return nullptr;
            };
            auto *activeDue=findVisible(findVisible,window->contentItem(),
                                        "rentalDueLabelR001");
            auto *returnedDue=findVisible(findVisible,window->contentItem(),
                                          "rentalDueLabelR002");
            if(!activeDue||!returnedDue||
               activeDue->property("text").toString()!="Overdue"||
               returnedDue->property("text").toString()!="No return due") {
                qCritical()<<"UX-S2 readable due-state delegate did not render"
                           <<"active"<<(activeDue?activeDue->property("text").toString():"<not instantiated>")
                           <<"returned"<<(returnedDue?returnedDue->property("text").toString():"<not instantiated>");
                app.exit(813);return;
            }
            const QPointF pos=details->mapToItem(page,QPointF(0,0));
            if(pos.y()<0 || pos.y()+details->height()>page->height()+2) {
                qCritical()<<"UX-S1 contract details action not reachable"
                           <<pos<<page->size()<<details->height();
                app.exit(809);return;
            }
            const QString size=compact?"1280x720":"1500x860";
            const QImage shot=window->grabWindow();
            if(shot.size()!=QSize(compact?1280:1500,compact?720:860)||
               !shot.save("ux-s1-rentals-bottom-"+size+".png","PNG")) {
                qCritical()<<"UX-S1 native bottom-of-page screenshot failed";
                app.exit(810);return;
            }
            QTest::keyClick(window,Qt::Key_PageUp);
            QTest::qWait(120);
            if((maximum>3 &&
                page->property("contentY").toDouble()>
                    maximum-std::min(40.0,maximum)+2) ||
               !rentalModel.selectedRentalId().isEmpty()) {
                qCritical()<<"UX-S1 keyboard PageUp/selection mutation";
                app.exit(811);return;
            }
            QByteArray v,r;
            if(!readFile(vehicles,&v)||!readFile(rentals,&r)||
               v!=beforeVehicles||r!=beforeRentals) {
                qCritical()<<"UX-S1 page scroll modified demo source";
                app.exit(812);return;
            }
            qInfo()<<"UX-S1 real scroll and keyboard PASS"<<size;
            app.exit(0);
        });
        return app.exec();
    }
    if (parser.isSet("rentals-smoke")) {
        auto *window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window) { qCritical() << "P3-F root is not native Qt Quick"; return 80; }

        const bool compact = parser.isSet("compact");
        window->resize(compact ? 1280 : 1500, compact ? 720 : 860);
        window->setProperty("workspace", 2);
        QTimer::singleShot(1050, &app, [&, window, compact] {
            auto *list = window->findChild<QQuickItem*>("rentalList");
            auto *selection = window->findChild<QQuickItem*>("rentalSelectionText");
            if (!list || !selection || !list->isVisible() ||
                rentalModel.count() != 2 || list->property("count").toInt() != 2) {
                qCritical() << "P3-F real QML Rentals list/count absent"
                            << list << selection << rentalModel.count();
                app.exit(81); return;
            }

            const auto findRentalRow = [](const auto &self, QQuickItem *item,
                                          const QString &id) -> QQuickItem* {
                if (!item) return nullptr;
                if (item->objectName() == "rentalRow" &&
                    item->property("rentalId").toString() == id &&
                    item->isVisible()) return item;
                for (QQuickItem *child : item->childItems()) {
                    if (auto *found = self(self, child, id)) return found;
                }
                return nullptr;
            };
            auto *activeRow = findRentalRow(findRentalRow, list, "R001");
            auto *returnedRow = findRentalRow(findRentalRow, list, "R002");
            if (!activeRow || !returnedRow ||
                activeRow->property("status").toString() != "Active" ||
                returnedRow->property("status").toString() != "Returned") {
                qCritical() << "P3-F real QML delegates did not bind domain status";
                app.exit(82); return;
            }
            activeRow->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window, Qt::Key_Space);
            QCoreApplication::processEvents();
            if (rentalModel.selectedRentalId() != "R001" ||
                !selection->property("text").toString().contains("R001")) {
                qCritical() << "P3-F keyboard selection not synced with QML"
                            << rentalModel.selectedRentalId();
                app.exit(83); return;
            }
            // Fleet is also a snapshot; its availability must NOT change
            // until explicit success-triggered refresh, just like Rentals.
            fleet.selectById("C001");
            if (fleet.availableCount() != 4 || !fleet.selectedAvailable()) {
                qCritical() << "P3-G pre-commit Fleet snapshot inconsistent";
                app.exit(92); return;
            }
            const QString sizeLabel = compact ? "1280x720" : "1500x860";
            const QImage shot = window->grabWindow();
            const QSize expected(compact ? 1280 : 1500, compact ? 720 : 860);
            if (shot.isNull() || shot.size() != expected ||
                !shot.save("p3-qt-rentals-" + sizeLabel + ".png", "PNG")) {
                qCritical() << "P3-F real QML screenshot invalid" << shot.size();
                app.exit(84); return;
            }

            // The UI has NO write controls. This explicit test-only C++
            // command acts on the disposable SHA-pinned fictional fixture.
            // It must not optimistically change the read model before refresh.
            RentalQuoteService quotes(system);
            const auto quote = quotes.preview("C001", 3);
            if (!quote.ok()) {
                qCritical() << "P3-F domain quote not available";
                app.exit(85); return;
            }
            const RentalConfirmationRequest request{
                *quote.quote, "Fictional Preview Customer", "0000000000"
            };
            const auto committed = RentalConfirmService(system).confirm(request);
            if (!committed.ok() || committed.receipt->rentalId != "R003" ||
                rentalModel.count() != 2 || list->property("count").toInt() != 2 ||
                fleet.availableCount() != 4 || !fleet.selectedAvailable()) {
                qCritical() << "P3-F/G commit/refresh boundary failed"
                            << committed.ok() << rentalModel.count();
                app.exit(86); return;
            }

            // One explicit C++ commit-success integration point updates both
            // read models. No QObject refresh is connected to a QML action.
            rentalModel.refresh();
            fleet.refreshAfterCommit();
            QTest::qWait(180);
            QCoreApplication::processEvents();
            if (rentalModel.count() != 3 || list->property("count").toInt() != 3 ||
                rentalModel.selectedRentalId() != "R001" ||
                !findRentalRow(findRentalRow, list, "R003") ||
                fleet.availableCount() != 3 || fleet.selectedAvailable() ||
                fleet.selectedId() != "C001") {
                qCritical() << "P3-F/G QML and Fleet disagree after committed rental";
                app.exit(87); return;
            }

            // The availability filter must now hide the freshly rented
            // vehicle, and the Fleet Inspector must not retain a hidden ID.
            fleet.setStatus("Available");
            if (!fleet.selectedId().isEmpty() ||
                static_cast<FleetProxy*>(fleet.visibleModel())->rowCount() != 3) {
                qCritical() << "P3-G refreshed available filter/selection stale";
                app.exit(93); return;
            }
            fleet.setStatus("All");

            if (!system.archiveRental("R002")) {
                qCritical() << "P3-F fictional returned rental could not archive";
                app.exit(88); return;
            }
            returnedRow = findRentalRow(findRentalRow, list, "R002");
            if (!returnedRow || returnedRow->property("status").toString() != "Returned") {
                qCritical() << "P3-F QML changed before archive refresh";
                app.exit(89); return;
            }
            rentalModel.refresh();
            QTest::qWait(180);
            returnedRow = findRentalRow(findRentalRow, list, "R002");
            if (!returnedRow ||
                returnedRow->property("status").toString() != "Archived" ||
                rentalModel.selectedRentalId() != "R001") {
                qCritical() << "P3-F archive state/selection QML sync failed";
                app.exit(90); return;
            }

            QByteArray afterVehicles, afterRentals;
            if (!readFile(vehicles, &afterVehicles) ||
                !readFile(rentals, &afterRentals) ||
                beforeVehicles != afterVehicles || beforeRentals != afterRentals) {
                qCritical() << "P3-F SHA-pinned demo fixtures were modified";
                app.exit(91); return;
            }
            qInfo() << "P3-F/G REAL QML Rentals and Fleet PASS:" << sizeLabel
                    << "keyboard selection, committed booking, both explicit"
                    << " model refreshes, Fleet availability/filter/Inspector,"
                    << " archived history, source-fixture protection";
            app.exit(0);
        });
    }
    if (parser.isSet("rentals-filter-smoke")) {
        auto *window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window) return 100;
        const bool compact = parser.isSet("compact");
        window->resize(compact ? 1280 : 1500, compact ? 720 : 860);
        window->setProperty("workspace", 2);
        QTimer::singleShot(1000, &app, [&, window, compact] {
            auto *list = window->findChild<QQuickItem*>("rentalList");
            auto *selected = window->findChild<QQuickItem*>("rentalSelectionText");
            const auto findVisual = [](const auto &self, QQuickItem *item,
                                       const QString &name) -> QQuickItem* {
                if (!item) return nullptr;
                if (item->objectName()==name && item->isVisible()) return item;
                for (auto *child : item->childItems()) {
                    if (auto *found=self(self, child, name)) return found;
                }
                return nullptr;
            };
            auto *activeButton = findVisual(findVisual, window->contentItem(),
                                             "rentalFilterActive");
            auto *historyButton = findVisual(findVisual, window->contentItem(),
                                              "rentalFilterHistory");
            auto *returnedButton = findVisual(findVisual, window->contentItem(),
                                              "rentalFilterReturned");
            auto *archivedButton = findVisual(findVisual, window->contentItem(),
                                              "rentalFilterArchived");
            auto *allButton = findVisual(findVisual, window->contentItem(),
                                          "rentalFilterAll");
            auto *overdueButton=findVisual(findVisual,window->contentItem(),"rentalFilterOverdue");
            auto *dueTodayButton=findVisual(findVisual,window->contentItem(),"rentalFilterDueToday");
            auto *dueRow=findVisual(findVisual,window->contentItem(),"rentalDueRow");
            auto *emptyState=window->findChild<QQuickItem*>("rentalEmptyState");
            auto *emptyMessage=window->findChild<QQuickItem*>("rentalEmptyMessage");
            auto *emptyReset=window->findChild<QQuickItem*>("rentalEmptyReset");
            auto *statusRow=findVisual(findVisual,window->contentItem(),"rentalStatusRow");
            auto *lookupRow=findVisual(findVisual,window->contentItem(),"rentalLookupRow");
            auto *toolbarLookupField=findVisual(findVisual,window->contentItem(),"rentalLookupField");
            auto *toolbarLookupButton=findVisual(findVisual,window->contentItem(),"rentalLookupButton");
            auto *visibleCount=findVisual(findVisual,window->contentItem(),"rentalVisibleCount");
            if (!list || !list->isVisible() || !selected ||
                !statusRow || !dueRow || !overdueButton || !dueTodayButton ||
                !lookupRow || !toolbarLookupField || !toolbarLookupButton || !visibleCount ||
                !activeButton || !historyButton || !allButton ||
                !returnedButton || !archivedButton ||
                !emptyState || !emptyMessage || !emptyReset ||
                emptyState->isVisible() || emptyReset->isVisible() ||
                rentalModel.count()!=2 || list->property("count").toInt()!=2) {
                qCritical() << "P3-H real filter/list QML controls absent";
                app.exit(101); return;
            }
            const auto press = [window](QQuickItem *item) {
                item->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window,Qt::Key_Space);
                QCoreApplication::processEvents();
                QTest::qWait(70);
            };
            list->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Down);
            QCoreApplication::processEvents();
            if (rentalModel.selectedRentalId()!="R001" ||
                rentalModel.selectedRow()!=0 || !selected->property("text").toString().contains("R001")) {
                qCritical() << "P3-H rental arrow did not select first visible ID";
                app.exit(102); return;
            }
            // UX-S5: the real QML calendar-change trigger calls the C++
            // snapshot exactly once for a changed day. QML never classifies
            // due-state or writes source fixtures. Exercise at both sizes.
            auto *dateTimer=window->findChild<QObject*>("rentalDayRefreshTimer");
            const QString dayKey=window->property("lastRentalCalendarDay").toString();
            const QDate parsedDay=QDate::fromString(dayKey,Qt::ISODate);
            int dateRefreshSignals=0;
            const auto dateConnection=QObject::connect(
                &rentalModel,&RentalListModel::metricsChanged,&app,
                [&dateRefreshSignals]{++dateRefreshSignals;});
            const auto askDay=[window](const QString &day) {
                return QMetaObject::invokeMethod(window,
                    "refreshRentalDueIfCalendarChanged",
                    Q_ARG(QVariant,QVariant(day)));
            };
            if(!dateTimer || !parsedDay.isValid() ||
               !askDay(dayKey) || dateRefreshSignals!=0 ||
               !askDay(parsedDay.addDays(1).toString(Qt::ISODate)) ||
               dateRefreshSignals!=1 ||
               !askDay(parsedDay.addDays(1).toString(Qt::ISODate)) ||
               dateRefreshSignals!=1 ||
               !askDay(dayKey) || dateRefreshSignals!=2 ||
               rentalModel.selectedRentalId()!="R001" ||
               rentalModel.statusFilter()!="All") {
                qCritical()<<"UX-S5 real QML calendar refresh not idempotent or selection unsafe";
                app.exit(817);return;
            }
            QObject::disconnect(dateConnection);
            // The compact Windows/Linux view must keep both extra
            // lifecycle choices in the visible viewport, not offscreen.
            for (auto *item : {returnedButton, archivedButton}) {
                const QPointF pos=item->mapToScene(QPointF(0,0));
                if(pos.x()<0 || pos.y()<0 ||
                   pos.x()+item->width()>window->width() ||
                   pos.y()+item->height()>window->height() ||
                   item->width()<55 || item->height()<28) {
                    qCritical()<<"P6-G lifecycle filter outside viewport"
                               <<pos<<item->size();
                    app.exit(116);return;
                }
            }
            // UX-TR2: status is the first row; due filters and ID lookup
            // share the SECOND row. Retain native viewport, control-size,
            // zero-overlap, selected ID and real keyboard assertions.
            const QPointF statusPos=statusRow->mapToScene(QPointF(0,0));
            const QPointF duePos=dueRow->mapToScene(QPointF(0,0));
            const QPointF overduePos=overdueButton->mapToScene(QPointF(0,0));
            const QPointF dueTodayPos=dueTodayButton->mapToScene(QPointF(0,0));
            const QPointF lookupPos=lookupRow->mapToScene(QPointF(0,0));
            const QPointF fieldPos=toolbarLookupField->mapToScene(QPointF(0,0));
            const QPointF buttonPos=toolbarLookupButton->mapToScene(QPointF(0,0));
            const auto withinViewport=[window](QQuickItem *item,const QPointF &pos) {
                return pos.x()>=0 && pos.y()>=0 &&
                    pos.x()+item->width()<=window->width() &&
                    pos.y()+item->height()<=window->height();
            };
            if(statusPos.y()<0 ||
               duePos.y()+1<statusPos.y()+statusRow->height() ||
               std::abs(lookupPos.y()-duePos.y())>8 ||
               lookupPos.x()+1<duePos.x()+dueRow->width() ||
               !withinViewport(statusRow,statusPos) ||
               !withinViewport(dueRow,duePos) ||
               !withinViewport(overdueButton,overduePos) ||
               !withinViewport(dueTodayButton,dueTodayPos) ||
               overdueButton->width()<55 || dueTodayButton->width()<70 ||
               !withinViewport(lookupRow,lookupPos) ||
               !withinViewport(toolbarLookupField,fieldPos) ||
               !withinViewport(toolbarLookupButton,buttonPos) ||
               toolbarLookupField->width()<150 || toolbarLookupButton->height()<36 ||
               list->height()<65 ||
               !visibleCount->property("text").toString().contains("2 matching")) {
                qCritical()<<"UX-Q2 compact Rentals toolbar overlaps or clips"
                           <<statusPos<<lookupPos<<fieldPos<<buttonPos
                           <<list->height();
                app.exit(620);return;
            }
            const QString suffix=compact?"1280x720":"1500x860";
            // UX-S3: genuine Qt keyboard urgency filtering, no domain writes.
            const int baselineActive=rentalModel.activeCount();
            const int baselineHistory=rentalModel.historyCount();
            const double baselineValue=rentalModel.activeBookedValue();
            press(overdueButton);
            if(rentalModel.statusFilter()!="Overdue" || rentalModel.count()!=1 ||
               rentalModel.selectedRentalId()!="R001" ||
               rentalModel.data(rentalModel.index(0,0),RentalListModel::DueStateRole)!="Overdue" ||
               rentalModel.activeCount()!=baselineActive ||
               rentalModel.historyCount()!=baselineHistory ||
               rentalModel.activeBookedValue()!=baselineValue) {
                qCritical()<<"UX-S3 Overdue filter or KPI mismatch";
                app.exit(814);return;
            }
            press(dueTodayButton);
            if(rentalModel.statusFilter()!="DueToday" || rentalModel.count()!=0 ||
               !rentalModel.selectedRentalId().isEmpty() || !emptyState->isVisible() ||
               !emptyReset->isVisible() ||
               !emptyMessage->property("text").toString().contains("due today") ||
               rentalModel.activeCount()!=baselineActive ||
               rentalModel.activeBookedValue()!=baselineValue) {
                qCritical()<<"UX-S3 Due today empty state or selection wrong";
                app.exit(815);return;
            }
            press(allButton);
            if(rentalModel.statusFilter()!="All" || rentalModel.count()!=2 ||
               !rentalModel.selectedRentalId().isEmpty()) {
                qCritical()<<"UX-S3 All failed to clear urgency view";
                app.exit(816);return;
            }
            press(returnedButton);
            if(rentalModel.statusFilter()!="Returned" ||
               rentalModel.count()!=1 || list->property("count").toInt()!=1 ||
               rentalModel.selectedRentalId()!="" ||
               rentalModel.data(rentalModel.index(0,0),
                                RentalListModel::StatusRole).toString()!="Returned" ||
               rentalModel.activeCount()!=1 || rentalModel.historyCount()!=1 ||
               !visibleCount->property("text").toString().contains("1 matching")) {
                qCritical()<<"P6-G Returned view must show only returned records";
                app.exit(117);return;
            }
            const QImage returnedShot=window->grabWindow();
            if(returnedShot.isNull() ||
               returnedShot.size()!=QSize(compact?1280:1500,compact?720:860) ||
               !returnedShot.save("p3-qt-rentals-returned-"+suffix+".png","PNG")) {
                qCritical()<<"P6-G Returned screenshot invalid";
                app.exit(118);return;
            }
            press(archivedButton);
            if(rentalModel.statusFilter()!="Archived" ||
               rentalModel.count()!=0 || list->property("count").toInt()!=0 ||
               !rentalModel.selectedRentalId().isEmpty() ||
               rentalModel.activeCount()!=1 || rentalModel.historyCount()!=1 ||
               !emptyState->isVisible() || !emptyReset->isVisible() ||
               !emptyReset->isEnabled() ||
               !emptyMessage->property("text").toString().contains("Archived") ||
               !visibleCount->property("text").toString().contains("0 matching")) {
                qCritical()<<"P6-G Archived empty-state must not include Returned";
                app.exit(119);return;
            }
            // Clear a filter-only empty result via the real keyboard button.
            // No archived/returned record, value or selected ID may mutate.
            press(emptyReset);
            if(rentalModel.statusFilter()!="All" ||
               rentalModel.count()!=2 || list->property("count").toInt()!=2 ||
               emptyState->isVisible() || emptyReset->isVisible() ||
               !rentalModel.selectedRentalId().isEmpty()) {
                qCritical()<<"P6-G Rentals empty-state reset failed";
                app.exit(619);return;
            }
            press(historyButton);
            if (rentalModel.statusFilter()!="History" || rentalModel.count()!=1 ||
                list->property("count").toInt()!=1 ||
                !rentalModel.selectedRentalId().isEmpty() ||
                rentalModel.data(rentalModel.index(0,0),RentalListModel::RentalIdRole).toString()!="R002") {
                qCritical() << "P3-H keyboard History filter or hidden selection failed";
                app.exit(103); return;
            }
            list->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Down);
            QCoreApplication::processEvents();
            if (rentalModel.selectedRentalId()!="R002" ||
                rentalModel.selectedRow()!=0) {
                qCritical() << "P3-H history list arrow selection failed";
                app.exit(104); return;
            }
            const QImage historyShot=window->grabWindow();
            if (historyShot.isNull() ||
                historyShot.size()!=QSize(compact?1280:1500,compact?720:860) ||
                !historyShot.save("p3-qt-rentals-history-"+suffix+".png","PNG")) {
                qCritical() << "P3-H real History QML screenshot failed" << historyShot.size();
                app.exit(105); return;
            }
            // Returned and archived records must both stay in History after a
            // real C++ domain operation and explicit model refresh.
            if (!system.archiveRental("R002")) {
                qCritical() << "P3-H fictional archive failed";
                app.exit(106); return;
            }
            if (rentalModel.data(rentalModel.index(0,0),RentalListModel::StatusRole).toString()!="Returned") {
                qCritical() << "P3-H History was optimistically updated";
                app.exit(107); return;
            }
            rentalModel.refresh();
            QCoreApplication::processEvents();
            if (rentalModel.statusFilter()!="History" || rentalModel.count()!=1 ||
                rentalModel.selectedRentalId()!="R002" ||
                rentalModel.data(rentalModel.index(0,0),RentalListModel::StatusRole).toString()!="Archived") {
                qCritical() << "P3-H archived record left History or lost selection";
                app.exit(108); return;
            }
            press(archivedButton);
            if(rentalModel.statusFilter()!="Archived" ||
               rentalModel.count()!=1 || list->property("count").toInt()!=1 ||
               rentalModel.selectedRentalId()!="R002" ||
               rentalModel.data(rentalModel.index(0,0),
                                RentalListModel::StatusRole).toString()!="Archived") {
                qCritical()<<"P6-G Archived view lost newly archived selection";
                app.exit(120);return;
            }
            const QImage archivedShot=window->grabWindow();
            if(archivedShot.isNull() ||
               archivedShot.size()!=QSize(compact?1280:1500,compact?720:860) ||
               !archivedShot.save("p3-qt-rentals-archived-"+suffix+".png","PNG")) {
                qCritical()<<"P6-G Archived screenshot invalid";
                app.exit(121);return;
            }
            press(returnedButton);
            if(rentalModel.statusFilter()!="Returned" ||
               rentalModel.count()!=0 || list->property("count").toInt()!=0 ||
               !rentalModel.selectedRentalId().isEmpty() ||
               !emptyState->isVisible() ||
               !emptyMessage->property("text").toString().contains("Returned")) {
                qCritical()<<"P6-G Returned view retained archived selection";
                app.exit(122);return;
            }
            press(activeButton);
            if (rentalModel.statusFilter()!="Active" || rentalModel.count()!=1 ||
                rentalModel.selectedRentalId()!="" ||
                rentalModel.data(rentalModel.index(0,0),RentalListModel::RentalIdRole).toString()!="R001") {
                qCritical() << "P3-H Active filter and selection dismissal failed";
                app.exit(109); return;
            }
            // No hidden selection is resurrected on All.
            press(allButton);
            if (rentalModel.statusFilter()!="All" || rentalModel.count()!=2 ||
                !rentalModel.selectedRentalId().isEmpty()) {
                qCritical() << "P3-H All filter resurrected hidden selection";
                app.exit(110); return;
            }
            // P6 parity: use the real Find rental control, not just a direct
            // model call. Hidden lookup reveals a record, while unknown/blank
            // input leaves the selected ID and filter unchanged.
            auto *lookupField = findVisual(findVisual,window->contentItem(),
                                            "rentalLookupField");
            auto *lookupButton = findVisual(findVisual,window->contentItem(),
                                             "rentalLookupButton");
            // The response label is intentionally invisible until a
            // lookup returns. Discover the real QML object by QObject ID,
            // then assert that the native UI actually shows it after input.
            auto *lookupFeedback = window->findChild<QQuickItem*>("rentalLookupFeedback");
            if(!lookupField || !lookupButton || !lookupFeedback) {
                qCritical() << "P6-F rental lookup control missing";
                app.exit(112);return;
            }
            rentalModel.setStatusFilter("History");
            lookupField->setProperty("text", "  r001  ");
            press(lookupButton);
            QTest::qWait(90);
            if(rentalModel.statusFilter()!="All" ||
               rentalModel.selectedRentalId()!="R001" ||
               rentalModel.selectedRow()!=0 ||
               list->property("currentIndex").toInt()!=0) {
                qCritical() << "P6-F hidden rental lookup failed";
                app.exit(113);return;
            }
            lookupField->setProperty("text", "R999");
            press(lookupButton);
            QCoreApplication::processEvents();
            if(rentalModel.statusFilter()!="All" ||
               rentalModel.selectedRentalId()!="R001" ||
               lookupFeedback->property("text").toString()!="Rental or vehicle ID not found." ||
               !lookupFeedback->isVisible() ||
               lookupFeedback->width()<320 ||
               lookupFeedback->height()<10) {
                qCritical() << "UX-S18 precise and fully visible missing-ID feedback failed";
                app.exit(114);return;
            }
            lookupField->setProperty("text", "");
            press(lookupButton);
            if(rentalModel.selectedRentalId()!="R001" ||
               lookupFeedback->property("text").toString()!="Enter a rental ID or vehicle ID.") {
                qCritical() << "UX-S18 empty lookup did not give precise guidance";
                app.exit(115);return;
            }
            // UX-S17: the real Find rental button resolves a unique vehicle
            // ID, including its hidden historical rental, via typed C++.
            rentalModel.setStatusFilter("Active");
            lookupField->setProperty("text"," c001 ");
            press(lookupButton);
            QCoreApplication::processEvents();
            if(rentalModel.statusFilter()!="All" ||
               rentalModel.selectedRentalId()!="R002" ||
               rentalModel.selectedRow()!=1 ||
               !lookupFeedback->property("text").toString().contains("Selected R002")) {
                qCritical()<<"UX-S17 unique vehicle ID did not reveal historical rental";
                app.exit(868);return;
            }
            // UX-S10: a successful lookup response must never falsely
            // describe an earlier contract after keyboard/pointer navigation
            // or an authoritative C++ refresh of the same selected ID.
            lookupField->setProperty("text", "R002");
            press(lookupButton);
            if(rentalModel.selectedRentalId()!="R002" ||
               !lookupFeedback->property("text").toString().contains("Selected R002")) {
                qCritical()<<"UX-S10 successful visible rental lookup missing feedback";
                app.exit(837);return;
            }
            if(!rentalModel.selectById("R001") ||
               !lookupFeedback->property("text").toString().isEmpty()) {
                qCritical()<<"UX-S10 old rental lookup success persisted after selection changed";
                app.exit(838);return;
            }
            lookupField->setProperty("text", "R002");
            press(lookupButton);
            rentalModel.refresh();
            QCoreApplication::processEvents();
            if(rentalModel.selectedRentalId()!="R002" ||
               !lookupFeedback->property("text").toString().isEmpty()) {
                qCritical()<<"UX-S10 stale lookup success after same-ID model refresh";
                app.exit(839);return;
            }
            lookupField->setProperty("text", "R999");
            press(lookupButton);
            if(!lookupFeedback->property("text").toString().contains("not found") ||
               rentalModel.selectedRentalId()!="R002") {
                qCritical()<<"UX-S10 unknown lookup baseline unexpectedly changed selection";
                app.exit(840);return;
            }
            rentalModel.setStatusFilter("Active");
            QCoreApplication::processEvents();
            if(!lookupFeedback->property("text").toString().isEmpty() ||
               rentalModel.statusFilter()!="Active" ||
               !rentalModel.selectedRentalId().isEmpty()) {
                qCritical()<<"UX-S10 stale lookup miss after lifecycle filter changed";
                app.exit(841);return;
            }
            QByteArray originalFleet, originalRentals;
            if (!readFile(vehicles,&originalFleet) || !readFile(rentals,&originalRentals) ||
                originalFleet!=beforeVehicles || originalRentals!=beforeRentals) {
                qCritical() << "P3-H source SHA-pinned fictional fixtures mutated";
                app.exit(111); return;
            }
            qInfo() << "P3-H/P6-G REAL QML All/Active/Returned/Archived/History PASS" << suffix
                    << "keyboard filter, stable IDs, archive, empty states,"
                    << " ID lookup, KPI independence and protected demo bytes";
            app.exit(0);
        });
    }
    if (parser.isSet("rentals-return-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window) return 120;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",2);
        QTimer::singleShot(1050,&app,[&,window,compact] {
            const auto findVisual=[](const auto &self,QQuickItem *item,
                                     const QString &name) -> QQuickItem* {
                if (!item) return nullptr;
                if (item->objectName()==name && item->isVisible()) return item;
                for (QQuickItem *child:item->childItems()) {
                    if (auto *found=self(self,child,name)) return found;
                }
                return nullptr;
            };
            auto *list=findVisual(findVisual,window->contentItem(),"rentalList");
            auto *returnButton=findVisual(findVisual,window->contentItem(),"rentalReturnButton");
            auto *returnLookupField=findVisual(findVisual,window->contentItem(),"returnLookupField");
            auto *returnLookupButton=findVisual(findVisual,window->contentItem(),"returnLookupButton");
            auto *returnLookupFeedback=findVisual(findVisual,window->contentItem(),"returnLookupFeedback");
            if (!list || !returnButton || !returnLookupField ||
                !returnLookupButton || !returnLookupFeedback ||
                returnButton->isEnabled()) {
                qCritical()<<"P3-I return button missing or enabled with no selection";
                app.exit(121);return;
            }
            const QByteArray originalTempRentals=[](const QString &file){
                QFile f(file); if (!f.open(QIODevice::ReadOnly)) return QByteArray();
                return f.readAll();
            }(disposable.filePath("rentals.txt"));
            const auto press=[&](QQuickItem *control) {
                control->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window,Qt::Key_Space);
                QCoreApplication::processEvents();
                QTest::qWait(70);
            };
            // P6-H: locate a hidden active contract by VEHICLE ID using
            // the real keyboard-activated native lookup. This does not save.
            rentalModel.setStatusFilter("History");
            returnLookupField->setProperty("text", "  c002  ");
            press(returnLookupButton);
            QTest::qWait(80);
            QByteArray afterLookup;
            if (rentalModel.statusFilter()!="Active" ||
                rentalModel.selectedRentalId()!="R001" ||
                rentalModel.selectedRow()!=0 ||
                list->property("currentIndex").toInt()!=0 ||
                !returnButton->isEnabled() ||
                !returnButton->hasActiveFocus() ||
                fleet.availableCount()!=4 ||
                system.findRentalById("R001")->getIsReturned() ||
                !readFile(disposable.filePath("rentals.txt"),&afterLookup) ||
                afterLookup!=originalTempRentals) {
                qCritical()<<"P6-H vehicle ID lookup did not safely reveal active rental";
                app.exit(122);return;
            }
            // UX-S10: a successful active-return lookup gives accurate
            // feedback, but a filter change that retains the selected ID
            // must clear that one-time success instead of showing stale UI.
            if(!returnLookupFeedback->property("text").toString().contains(
                   "Ready to review return for R001")) {
                qCritical()<<"UX-S10 return lookup success message missing";
                app.exit(842);return;
            }
            rentalModel.setStatusFilter("All");
            QCoreApplication::processEvents();
            if(rentalModel.selectedRentalId()!="R001" ||
               !returnLookupFeedback->property("text").toString().isEmpty()) {
                qCritical()<<"UX-S10 stale return lookup success after filter changed";
                app.exit(843);return;
            }
            rentalModel.setStatusFilter("Active");
            // Unknown/historical target must disable any previous return.
            returnLookupField->setProperty("text", "R002");
            press(returnLookupButton);
            if (returnButton->isEnabled() ||
                !rentalModel.selectedRentalId().isEmpty() ||
                rentalModel.statusFilter()!="Active" ||
                !returnLookupFeedback->property("text").toString().contains("No active rental")) {
                qCritical()<<"P6-H historical rental did not disarm old selection";
                app.exit(133);return;
            }
            returnLookupField->setProperty("text", "C999");
            press(returnLookupButton);
            if (returnButton->isEnabled() ||
                !rentalModel.selectedRentalId().isEmpty()) {
                qCritical()<<"P6-H unknown vehicle ID left stale selection";
                app.exit(134);return;
            }
            returnLookupField->setProperty("text", "");
            press(returnLookupButton);
            if (returnButton->isEnabled() ||
                !returnLookupFeedback->property("text").toString().contains("Enter an active")) {
                qCritical()<<"P6-H empty target not rejected";
                app.exit(135);return;
            }
            // A RENTAL ID can also locate the same active contract without
            // returning it. The existing C++ confirmation remains mandatory.
            returnLookupField->setProperty("text", " r001 ");
            press(returnLookupButton);
            QTest::qWait(80);
            if (rentalModel.selectedRentalId()!="R001" ||
                !returnButton->isEnabled() ||
                system.findRentalById("R001")->getIsReturned() ||
                !readFile(disposable.filePath("rentals.txt"),&afterLookup) ||
                afterLookup!=originalTempRentals) {
                qCritical()<<"P6-H rental ID lookup auto-committed or failed";
                app.exit(136);return;
            }
            press(returnButton);
            auto *cancel=findVisual(findVisual,window->contentItem(),"rentalReturnCancel");
            if (!cancel) {
                qCritical()<<"P3-I native confirmation dialog did not appear";
                app.exit(123);return;
            }
            press(cancel);
            if (!rentalActions.canReturnSelected() ||
                rentalModel.selectedRentalId()!="R001" ||
                !system.findVehicleById("C002")->getIsRented() ||
                fleet.availableCount()!=4) {
                qCritical()<<"P3-I dialog Cancel mutated domain/view";
                app.exit(124);return;
            }
            QByteArray tempAfterCancel;
            if (!readFile(disposable.filePath("rentals.txt"),&tempAfterCancel) ||
                tempAfterCancel!=originalTempRentals) {
                qCritical()<<"P3-I Cancel wrote a file";
                app.exit(125);return;
            }
            press(returnButton);
            auto *confirm=findVisual(findVisual,window->contentItem(),"rentalReturnConfirm");
            if (!confirm || !confirm->isEnabled()) {
                qCritical()<<"P3-I native confirmed action not available";
                app.exit(126);return;
            }
            press(confirm);
            QTest::qWait(150);
            // UX-S6: return from an Active-only view must reveal and select
            // the newly Returned contract, while disabling repeat returns.
            if (rentalModel.statusFilter()!="All" ||
                rentalModel.count()!=2 ||
                rentalModel.selectedRentalId()!="R001" ||
                rentalModel.selectedContract().value("status").toString()!="Returned" ||
                rentalActions.canReturnSelected() || returnButton->isEnabled() ||
                fleet.availableCount()!=5 ||
                system.findVehicleById("C002")->getIsRented() ||
                !system.findRentalById("R001")->getIsReturned()) {
                qCritical()<<"UX-S6 saved return not visible or still actionable"
                           <<rentalModel.selectedRentalId()<<fleet.availableCount();
                app.exit(127);return;
            }
            rentalModel.setStatusFilter("History");
            if(rentalModel.count()!=2 ||
               rentalModel.data(rentalModel.index(0,0),
                                RentalListModel::StatusRole).toString()!="Returned" ||
               rentalModel.selectedRentalId()!="R001" ||
               returnButton->isEnabled()) {
                qCritical()<<"P6-H persisted return not visible in History";
                app.exit(137);return;
            }
            if (rentalModel.locateActiveReturn("C002") ||
                !rentalModel.selectedRentalId().isEmpty() ||
                rentalActions.returnSelected() ||
                fleet.availableCount()!=5 || !system.findRentalById("R001")->getIsReturned()) {
                qCritical()<<"P3-I repeated return not safely rejected";
                app.exit(128);return;
            }
            rentalModel.setStatusFilter("Active");
            if (rentalModel.count()!=0 || !rentalModel.selectedRentalId().isEmpty()) {
                qCritical()<<"P3-I returned contract not removed from Active";
                app.exit(129);return;
            }
            rentalModel.setStatusFilter("History");
            if (rentalModel.count()!=2 ||
                rentalModel.data(rentalModel.index(0,0),
                                 RentalListModel::StatusRole).toString()!="Returned") {
                qCritical()<<"P3-I returned contract absent from History";
                app.exit(130);return;
            }
            QByteArray afterVehicles,afterRentals,persistedLedger;
            if (!readFile(vehicles,&afterVehicles) ||
                !readFile(rentals,&afterRentals) ||
                !readFile(disposable.filePath("rentals.txt"),&persistedLedger) ||
                afterVehicles!=beforeVehicles || afterRentals!=beforeRentals ||
                persistedLedger==originalTempRentals) {
                qCritical()<<"P3-I protected demo input or persistence incorrect";
                app.exit(131);return;
            }
            const QString size=compact?"1280x720":"1500x860";
            QImage screenshot=window->grabWindow();
            if (screenshot.isNull() ||
                screenshot.size()!=QSize(compact?1280:1500,compact?720:860) ||
                !screenshot.save("p3-qt-rentals-return-"+size+".png","PNG")) {
                qCritical()<<"P3-I real QML return screenshot failed";
                app.exit(132);return;
            }
            qInfo()<<"P3-I/P6-H QML confirmed return PASS" <<size
                   <<"vehicle/rental ID lookup, stale target disarm, Cancel,"
                   <<"explicit C++ persisted return, no duplicate, protected demo data";
            app.exit(0);
        });
    }
    if (parser.isSet("rentals-booking-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window) return 150;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",2);
        QTimer::singleShot(1200,&app,[&,window,compact]{
            const auto find=[](const auto &self,QQuickItem *item,
                                const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name && item->isVisible())return item;
                for(auto *child:item->childItems())
                    if(auto *candidate=self(self,child,name))return candidate;
                return nullptr;
            };
            auto *root=window->contentItem();
            auto *newButton=find(find,root,"sidebarNewRentalButton");
            if(!newButton || rentalModel.count()!=2 || fleet.availableCount()!=4) {
                qCritical()<<"P3 guided booking launch source invalid";
                app.exit(151);return;
            }
            const auto press=[&](QQuickItem *item){
                item->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window,Qt::Key_Space);
                QCoreApplication::processEvents();
                QTest::qWait(70);
            };
            press(newButton);
            auto *vehicle=find(find,root,"bookingVehicleId");
            auto *preview=find(find,root,"bookingPreviewButton");
            auto *cancel=find(find,root,"bookingCancelButton");
            auto *confirm=find(find,root,"bookingConfirmButton");
            if(!vehicle||!preview||!cancel||!confirm||confirm->isEnabled()) {
                qCritical()<<"P3 real guided QML booking inputs missing";
                app.exit(152);return;
            }
            vehicle->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_C);
            QTest::keyClick(window,Qt::Key_0);
            QTest::keyClick(window,Qt::Key_0);
            QTest::keyClick(window,Qt::Key_3);
            press(preview);
            if(!bookingActions.canConfirm() ||
                bookingActions.reviewedVehicleId()!="C003" ||
                !bookingActions.quoteSummary().contains("Total R 800.00") ||
                !confirm->isEnabled()) {
                qCritical()<<"P3 C++ quote not reflected by QML" << bookingActions.quoteSummary();
                app.exit(153);return;
            }
            const QString suffix=compact?"1280x720":"1500x860";
            const auto image=window->grabWindow();
            if(image.isNull() || image.size()!=QSize(compact?1280:1500,compact?720:860) ||
                !image.save("p3-qt-rentals-booking-"+suffix+".png","PNG")) {
                qCritical()<<"P3 guided QML quote screenshot invalid";
                app.exit(154);return;
            }
            press(cancel);
            QByteArray tempLedger;
            if(bookingActions.canConfirm() || rentalModel.count()!=2 ||
               fleet.availableCount()!=4 ||
               !readFile(disposable.filePath("rentals.txt"),&tempLedger) ||
               tempLedger!=beforeRentals) {
                qCritical()<<"P3 Cancel changed authoritative domain or preview";
                app.exit(155);return;
            }
            press(newButton);
            vehicle=find(find,root,"bookingVehicleId");
            preview=find(find,root,"bookingPreviewButton");
            confirm=find(find,root,"bookingConfirmButton");
            auto *customer=find(find,root,"bookingCustomer");
            auto *phone=find(find,root,"bookingPhone");
            if(!vehicle||!preview||!confirm||!customer||!phone) {
                qCritical()<<"P3 re-opened quote dialog controls missing";
                app.exit(156);return;
            }
            // Vehicle ID remains from the canceled, never-committed preview.
            press(preview);
            auto *feedback=find(find,root,"bookingDialogFeedback");
            if(!feedback || !bookingActions.canConfirm()) {
                qCritical()<<"P5 inline booking feedback control missing";
                app.exit(301);return;
            }
            // Invalid C++ confirmation keeps the real modal open and gives
            // an inline explanation, without mutating any test records.
            press(confirm);
            QByteArray declinedLedger;
            if(feedback->property("text").toString()!=
                   QStringLiteral("Enter a valid customer name.") ||
               !bookingActions.lastRentalId().isEmpty() ||
               !readFile(disposable.filePath("rentals.txt"),&declinedLedger) ||
               declinedLedger!=beforeRentals || fleet.availableCount()!=4) {
                qCritical()<<"P5 invalid booking did not show safe inline reason";
                app.exit(302);return;
            }
            customer->forceActiveFocus(Qt::TabFocusReason);
            // QtTest has no keyClicks(QWindow*, QString) overload.
            // Set fixture-only contact text; the live preview button and
            // confirmation still receive actual Qt keyboard events.
            customer->setProperty("text",QStringLiteral("Fictional Demo C"));
            phone->forceActiveFocus(Qt::TabFocusReason);
            for(int i=0;i<10;++i)QTest::keyClick(window,Qt::Key_0);
            if(!bookingActions.canConfirm() || !confirm->isEnabled()) {
                qCritical()<<"P3 refreshed quote unavailable";
                app.exit(157);return;
            }
            press(confirm);
            QTest::qWait(140);
            if(bookingActions.lastRentalId()!="R003" ||
               bookingActions.canConfirm() || rentalModel.count()!=3 ||
               fleet.availableCount()!=3 ||
               !system.findVehicleById("C003")->getIsRented() ||
               rentalModel.data(rentalModel.index(2,0),
                     RentalListModel::StatusRole).toString()!="Active") {
                qCritical()<<"P3 real booking commit did not refresh both models"
                           <<bookingActions.lastRentalId()<<fleet.availableCount()
                           <<rentalModel.count();
                app.exit(158);return;
            }
            // Closing the modal clears its ephemeral preview and lastMessage,
            // but the saved receipt must remain visible in Rentals.
            auto *visibleReceipt=find(find,root,"rentalBookingMessage");
            if(!visibleReceipt ||
               visibleReceipt->property("text").toString()!=
                   QStringLiteral("Booking R003 saved.")) {
                qCritical()<<"P5 successful receipt missing after modal closed";
                app.exit(303);return;
            }
            const auto receiptShot=window->grabWindow();
            if(receiptShot.isNull() ||
               receiptShot.size()!=QSize(compact?1280:1500,compact?720:860) ||
               !receiptShot.save("p5-qt-booking-receipt-"+suffix+".png","PNG")) {
                qCritical()<<"P5 native booking receipt screenshot unavailable";
                app.exit(304);return;
            }
            QByteArray afterOriginalFleet,afterOriginalRentals,afterTempLedger;
            if(!readFile(vehicles,&afterOriginalFleet) || !readFile(rentals,&afterOriginalRentals) ||
               !readFile(disposable.filePath("rentals.txt"),&afterTempLedger) ||
               afterOriginalFleet!=beforeVehicles ||
               afterOriginalRentals!=beforeRentals ||
               afterTempLedger==beforeRentals) {
                qCritical()<<"P3 booking saved outside disposable input or was not persisted";
                app.exit(159);return;
            }
            qInfo()<<"P3 actual QML guided booking PASS"<<suffix
                    <<"C++ price/availability, Cancel, confirmed receipt,"
                    <<"no optimistic model write, dual-model refresh and SHA fixture guard";
            app.exit(0);
        });
    }
    if (parser.isSet("rentals-archive-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window)return 170;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",2);
        QTimer::singleShot(1100,&app,[&,window,compact]{
            const auto find=[](const auto &self,QQuickItem *item,
                                const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name && item->isVisible())return item;
                for(auto *child:item->childItems())
                    if(auto *candidate=self(self,child,name))return candidate;
                return nullptr;
            };
            auto *list=find(find,window->contentItem(),"rentalList");
            auto *archive=find(find,window->contentItem(),"rentalArchiveButton");
            if(!list||!archive||archive->isEnabled()){
                qCritical()<<"P3 archive button should be initially disabled";
                app.exit(171);return;
            }
            const auto press=[&](QQuickItem *item){
                item->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window,Qt::Key_Space);
                QCoreApplication::processEvents();
                QTest::qWait(70);
            };
            list->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Down);
            QTest::keyClick(window,Qt::Key_Down);
            QCoreApplication::processEvents();
            if(rentalModel.selectedRentalId()!="R002"||!archiveActions.canArchiveSelected() ||
               !archive->isEnabled()){
                qCritical()<<"P3 archival returned selection not enabled";
                app.exit(172);return;
            }
            // UX-S6: archive must reveal the saved contract even when the
            // Returned-only view would hide its new Archived state.
            rentalModel.setStatusFilter("Returned");
            if (rentalModel.selectedRentalId()!="R002" ||
                rentalModel.count()!=1 || !archive->isEnabled()) {
                qCritical()<<"UX-S6 Returned filter did not preserve selected receipt";
                app.exit(818);return;
            }
            QByteArray initial;
            if(!readFile(disposable.filePath("rentals.txt"),&initial)){
                app.exit(173);return;
            }
            press(archive);
            auto *cancel=find(find,window->contentItem(),"rentalArchiveCancel");
            if(!cancel){qCritical()<<"P3 archive modal absent";app.exit(174);return;}
            press(cancel);
            QByteArray afterCancel;
            if(!readFile(disposable.filePath("rentals.txt"),&afterCancel)||
               afterCancel!=initial||!archiveActions.canArchiveSelected()){
                qCritical()<<"P3 cancel unexpectedly archived";
                app.exit(175);return;
            }
            press(archive);
            auto *confirm=find(find,window->contentItem(),"rentalArchiveConfirm");
            if(!confirm||!confirm->isEnabled()){
                qCritical()<<"P3 confirm archive unavailable";
                app.exit(176);return;
            }
            press(confirm);
            QTest::qWait(120);
            if(rentalModel.selectedRentalId()!="R002" ||
               rentalModel.statusFilter()!="All" || rentalModel.count()!=2 ||
               archiveActions.canArchiveSelected() || archive->isEnabled() ||
               rentalModel.data(rentalModel.index(1,0),
                        RentalListModel::StatusRole).toString()!="Archived" ||
               !system.findRentalById("R002")->getIsArchived() ||
               fleet.availableCount()!=4 || archiveActions.archiveSelected()) {
                qCritical()<<"P3 archival commit/selection/status failed";
                app.exit(177);return;
            }
            rentalModel.setStatusFilter("History");
            if(rentalModel.count()!=1 || rentalModel.selectedRentalId()!="R002"){
                qCritical()<<"P3 archived history filter lost record";
                app.exit(178);return;
            }
            const QString suffix=compact?"1280x720":"1500x860";
            const QImage shot=window->grabWindow();
            if(shot.isNull()||shot.size()!=QSize(compact?1280:1500,compact?720:860) ||
               !shot.save("p3-qt-rentals-archive-"+suffix+".png","PNG")){
                qCritical()<<"P3 archived QML screenshot invalid";
                app.exit(179);return;
            }
            QByteArray sourceFleet,sourceRental,changed;
            if(!readFile(vehicles,&sourceFleet)||
               !readFile(rentals,&sourceRental)||
               !readFile(disposable.filePath("rentals.txt"),&changed)||
               sourceFleet!=beforeVehicles || sourceRental!=beforeRentals ||
               changed==initial){
                qCritical()<<"P3 archive affected source fixtures or did not save";
                app.exit(180);return;
            }
            qInfo()<<"P3 QML returned-rental archive PASS"<<suffix
                    <<"Cancel, persisted archive, selected History and no source writes";
            app.exit(0);
        });
    }
    if (parser.isSet("p4-shell-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window) return 190;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",1);
        QTimer::singleShot(1200,&app,[&,window,compact] {
            const auto findVisible=[](const auto &self,QQuickItem *item,
                                      const QString &name)->QQuickItem*{
                if(!item)return nullptr;
                if(item->objectName()==name && item->isVisible())return item;
                for(QQuickItem *child:item->childItems())
                    if(auto *hit=self(self,child,name))return hit;
                return nullptr;
            };
            auto *root=window->contentItem();
            auto *fleetNav=findVisible(findVisible,root,"workspaceNavFleet");
            auto *rentalNav=findVisible(findVisible,root,"workspaceNavRentals");
            auto *overviewNav=findVisible(findVisible,root,"workspaceNavOverview");
            auto *search=findVisible(findVisible,root,"fleetSearchField");
            auto *gallery=findVisible(findVisible,root,"fleetGallery");
            auto *frame=findVisible(findVisible,root,"fleetGalleryFocus");
            if(!fleetNav||!rentalNav||!overviewNav||!search||!gallery||!frame) {
                qCritical()<<"P4 native component/navigation controls missing";
                app.exit(191);return;
            }
            rentalNav->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Up);
            QCoreApplication::processEvents();
            if(!fleetNav->hasActiveFocus() ||
               fleet.selectedId()!="") {
                qCritical()<<"P4 group Up changed business state or missed Fleet focus";
                app.exit(192);return;
            }
            QTest::keyClick(window,Qt::Key_Up);
            QCoreApplication::processEvents();
            if(!overviewNav->hasActiveFocus()) {
                qCritical()<<"P4 second group Up missed Overview";
                app.exit(193);return;
            }
            QTest::keyClick(window,Qt::Key_Up);
            if(!overviewNav->hasActiveFocus()) {
                qCritical()<<"P4 sidebar boundary wrapped";
                app.exit(194);return;
            }
            QTest::keyClick(window,Qt::Key_Down);
            QTest::keyClick(window,Qt::Key_Right);
            QTest::qWait(90);
            if(window->property("workspace").toInt()!=1 ||
               !search->hasActiveFocus()) {
                qCritical()<<"P4 Fleet Right did not enter Search";
                app.exit(195);return;
            }
            // The focused TextField owns its arrow keys, not Fleet selection.
            QTest::keyClick(window,Qt::Key_Right);
            QCoreApplication::processEvents();
            if(!search->hasActiveFocus() || !fleet.selectedId().isEmpty()) {
                qCritical()<<"P4 text input arrows were intercepted";
                app.exit(196);return;
            }
            gallery->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Down);
            QCoreApplication::processEvents();
            QTest::qWait(60);
            // The collection may already have a selected visible row.
            // Assert state agreement rather than assuming a specific ID.
            if(fleet.selectedId().isEmpty() ||
               !frame->property("keyboardFocused").toBool() ||
               !frame->property("selected").toBool()) {
                qCritical()<<"P4 shared Fleet focus/selection distinction missing"
                           <<fleet.selectedId()<<frame->property("keyboardFocused")
                           <<frame->property("selected");
                app.exit(197);return;
            }
            rentalNav->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QCoreApplication::processEvents();
            if(window->property("workspace").toInt()!=2) {
                qCritical()<<"P4 workspace native Space activation failed";
                app.exit(198);return;
            }
            auto *rentalListItem=findVisible(findVisible,root,"rentalList");
            auto *returnButton=findVisible(findVisible,root,"rentalReturnButton");
            if(!rentalListItem||!returnButton) {
                qCritical()<<"P4 rental list or dialog action absent";
                app.exit(199);return;
            }
            rentalListItem->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Down);
            QCoreApplication::processEvents();
            if(rentalModel.selectedRentalId()!="R001" || !returnButton->isEnabled()) {
                qCritical()<<"P4 rental selection did not follow C++ visible rows";
                app.exit(200);return;
            }
            returnButton->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QTest::qWait(80);
            const auto dialogIsVisible=[&] {
                auto *dialog=window->findChild<QObject*>("rentalReturnDialog");
                return dialog && dialog->property("visible").toBool();
            };
            if(!dialogIsVisible()) {
                qCritical()<<"P4 modal did not acquire topmost context";
                app.exit(201);return;
            }
            QTest::keyClick(window,Qt::Key_Escape);
            QTest::qWait(120);
            if(dialogIsVisible() || rentalModel.selectedRentalId()!="R001" ||
               !returnButton->hasActiveFocus() ||
               system.findRentalById("R001")->getIsReturned()) {
                qCritical()<<"P4 Escape cleared selection or failed modal restoration"
                           <<rentalModel.selectedRentalId()
                           <<returnButton->hasActiveFocus();
                app.exit(202);return;
            }
            const QString size=compact?"1280x720":"1500x860";
            const QImage image=window->grabWindow();
            if(image.isNull() ||
               image.size()!=QSize(compact?1280:1500,compact?720:860) ||
               !image.save("p4-qt-shared-focus-"+size+".png","PNG")) {
                qCritical()<<"P4 focus screenshot mismatch";
                app.exit(203);return;
            }
            QByteArray sourceVehicles,sourceRentals,temporaryRentals;
            if(!readFile(vehicles,&sourceVehicles) ||
               !readFile(rentals,&sourceRentals) ||
               !readFile(disposable.filePath("rentals.txt"),&temporaryRentals) ||
               sourceVehicles!=beforeVehicles || sourceRentals!=beforeRentals ||
               temporaryRentals!=beforeRentals) {
                qCritical()<<"P4 keyboard/modal test mutated fictional source or temp ledger";
                app.exit(204);return;
            }
            qInfo()<<"P4 real shared shell PASS"<<size
                   <<"sidebar Up/Down/Right, native Search, separate focus,"
                   <<"modal Escape priority/restored focus and no writes";
            app.exit(0);
        });
    }
    if (parser.isSet("p4-components-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window) return 210;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",2);
        QTimer::singleShot(1200,&app,[&,window,compact] {
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name && item->isVisible())return item;
                for(auto *child:item->childItems())
                    if(auto *hit=self(self,child,name))return hit;
                return nullptr;
            };
            QQuickItem *root=window->contentItem();
            auto *newRental=find(find,root,"sidebarNewRentalButton");
            if(!newRental) {qCritical()<<"P4 New Rental control missing";app.exit(211);return;}
            const auto press=[&](QQuickItem *item) {
                item->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window,Qt::Key_Space);
                QTest::qWait(90);
            };
            press(newRental);
            auto *vehicle=find(find,root,"bookingVehicleId");
            auto *customer=find(find,root,"bookingCustomer");
            auto *phone=find(find,root,"bookingPhone");
            auto *quote=find(find,root,"bookingQuoteSummary");
            auto *cancel=find(find,root,"bookingCancelButton");
            if(!vehicle||!customer||!phone||!quote||!cancel ||
               vehicle->height()<43 || customer->height()<43 || phone->height()<43) {
                qCritical()<<"P4 reusable rental field/quote UI missing or too small";
                app.exit(212);return;
            }
            vehicle->forceActiveFocus(Qt::TabFocusReason);
            for(Qt::Key key : {Qt::Key_C,Qt::Key_0,Qt::Key_0,Qt::Key_1})
                QTest::keyClick(window,key);
            QTest::keyClick(window,Qt::Key_Left);
            QCoreApplication::processEvents();
            if(!vehicle->hasActiveFocus() ||
               vehicle->property("text").toString().toUpper()!="C001" ||
               !fleet.selectedId().isEmpty()) {
                qCritical()<<"P4 text caret or C++ selection changed unexpectedly";
                app.exit(213);return;
            }
            press(cancel);
            QTest::qWait(110);
            if(!newRental->hasActiveFocus() || bookingActions.canConfirm()) {
                qCritical()<<"P4 Booking Cancel focus/quote invalidation missing";
                app.exit(214);return;
            }
            auto *returnButton=find(find,root,"rentalReturnButton");
            if(!returnButton || !rentalModel.selectById("R001") ||
               !returnButton->isEnabled()) {
                qCritical()<<"P4 active fictional rental not selectable";
                app.exit(215);return;
            }
            press(returnButton);
            auto *returnCancel=find(find,root,"rentalReturnCancel");
            auto *returnConfirm=find(find,root,"rentalReturnConfirm");
            if(!returnCancel||!returnConfirm||!returnConfirm->isEnabled()) {
                qCritical()<<"P4 shared Return confirm/cancel controls unavailable";
                app.exit(216);return;
            }
            // UX-S7: a modal must remain bound to R001 even if a C++ model
            // update changes selection while it is open. No hidden relabel.
            auto *returnDialog=window->findChild<QObject*>("rentalReturnDialog");
            if(!returnDialog || !returnDialog->property("question").toString().contains("R001") ||
               !rentalModel.selectById("R002")) {
                qCritical()<<"UX-S7 return modal setup/selection change failed";
                app.exit(819);return;
            }
            QCoreApplication::processEvents();
            if(returnConfirm->isEnabled() ||
               !returnDialog->property("question").toString().contains("R001") ||
               system.findRentalById("R001")->getIsReturned()) {
                qCritical()<<"UX-S7 return dialog target changed during open modal";
                app.exit(820);return;
            }
            if(!rentalModel.selectById("R001")) {app.exit(821);return;}
            QCoreApplication::processEvents();
            // UX-S8: a round-trip selection cannot resurrect old consent.
            if(returnConfirm->isEnabled() ||
               !returnDialog->property("reviewInvalidated").toBool() ||
               !returnDialog->property("question").toString().contains(
                   "Close and reopen to review this return.")) {
                qCritical()<<"UX-S8 return consent re-enabled after selection round-trip";
                app.exit(827);return;
            }
            press(returnCancel);
            QTest::qWait(110);
            if(!returnButton->hasActiveFocus() ||
               system.findRentalById("R001")->getIsReturned() ||
               rentalModel.selectedRentalId()!="R001") {
                qCritical()<<"P4 Return Cancel changed domain/selection/focus";
                app.exit(217);return;
            }
            // A newly opened modal explicitly re-arms valid consent.
            press(returnButton);
            QTest::qWait(60);
            if(!returnConfirm->isEnabled() ||
               returnDialog->property("reviewInvalidated").toBool() ||
               !returnDialog->property("question").toString().contains("R001")) {
                qCritical()<<"UX-S8 fresh return confirmation failed to re-arm";
                app.exit(828);return;
            }
            // UX-S9: a C++ model reset (including after calendar rollover)
            // can change the contract snapshot without changing selected ID.
            // Even a same-data refresh invalidates the in-progress review.
            rentalModel.refresh();
            QCoreApplication::processEvents();
            if(returnConfirm->isEnabled() ||
               !returnDialog->property("reviewInvalidated").toBool() ||
               returnDialog->property("reviewInvalidationReason").toString()
                  !="Rental data refreshed." ||
               !returnDialog->property("question").toString().contains(
                   "Rental data refreshed. Close and reopen to review this return.") ||
               rentalModel.selectedRentalId()!="R001" ||
               !rentalActions.canReturnSelected()) {
                qCritical()<<"UX-S9 return modal did not invalidate on model reset";
                app.exit(833);return;
            }
            press(returnCancel);
            QTest::qWait(90);
            press(returnButton);
            QTest::qWait(60);
            if(!returnConfirm->isEnabled() ||
               returnDialog->property("reviewInvalidated").toBool() ||
               !returnDialog->property("reviewInvalidationReason").toString().isEmpty()) {
                qCritical()<<"UX-S9 fresh return review after reset not armed";
                app.exit(834);return;
            }
            press(returnCancel);
            QTest::qWait(90);
            if(system.findRentalById("R001")->getIsReturned()) {
                qCritical()<<"UX-S8 reopened return Cancel changed record";
                app.exit(829);return;
            }
            if(!rentalModel.selectById("R002")) {
                qCritical()<<"P4 returned demo rental absent";
                app.exit(218);return;
            }
            auto *archiveButton=find(find,root,"rentalArchiveButton");
            if(!archiveButton || !archiveButton->isEnabled()) {
                qCritical()<<"P4 Archive action not eligible";
                app.exit(219);return;
            }
            press(archiveButton);
            auto *archiveCancel=find(find,root,"rentalArchiveCancel");
            auto *archiveConfirm=find(find,root,"rentalArchiveConfirm");
            if(!archiveCancel||!archiveConfirm||!archiveConfirm->isEnabled()) {
                qCritical()<<"P4 shared Archive confirm/cancel controls unavailable";
                app.exit(220);return;
            }
            // UX-S7: archive wording remains R002 and its confirm action
            // is disabled when selection moves to an ineligible rental.
            auto *archiveDialog=window->findChild<QObject*>("rentalArchiveDialog");
            if(!archiveDialog || !archiveDialog->property("question").toString().contains("R002") ||
               !rentalModel.selectById("R001")) {app.exit(823);return;}
            QCoreApplication::processEvents();
            if(archiveConfirm->isEnabled() ||
               !archiveDialog->property("question").toString().contains("R002")) {
                qCritical()<<"UX-S7 archive dialog target changed while open";
                app.exit(824);return;
            }
            if(!rentalModel.selectById("R002")) {app.exit(825);return;}
            QCoreApplication::processEvents();
            if(archiveConfirm->isEnabled() ||
               !archiveDialog->property("reviewInvalidated").toBool() ||
               !archiveDialog->property("question").toString().contains(
                   "Close and reopen to review this archive.")) {
                qCritical()<<"UX-S8 archived consent re-enabled after selection round-trip";
                app.exit(830);return;
            }
            QTest::keyClick(window,Qt::Key_Escape);
            QTest::qWait(155);
            if(!archiveButton->hasActiveFocus() ||
               system.findRentalById("R002")->getIsArchived() ||
               rentalModel.selectedRentalId()!="R002") {
                qCritical()<<"P4 Archive Escape stole selection/changed domain";
                app.exit(221);return;
            }
            press(archiveButton);
            QTest::qWait(60);
            if(!archiveConfirm->isEnabled() ||
               archiveDialog->property("reviewInvalidated").toBool() ||
               !archiveDialog->property("question").toString().contains("R002")) {
                qCritical()<<"UX-S8 fresh archive confirmation failed to re-arm";
                app.exit(831);return;
            }
            rentalModel.refresh();
            QCoreApplication::processEvents();
            if(archiveConfirm->isEnabled() ||
               !archiveDialog->property("reviewInvalidated").toBool() ||
               archiveDialog->property("reviewInvalidationReason").toString()
                  !="Rental data refreshed." ||
               !archiveDialog->property("question").toString().contains(
                   "Rental data refreshed. Close and reopen to review this archive.") ||
               rentalModel.selectedRentalId()!="R002" ||
               !archiveActions.canArchiveSelected()) {
                qCritical()<<"UX-S9 archive modal did not invalidate on model reset";
                app.exit(835);return;
            }
            QTest::keyClick(window,Qt::Key_Escape);
            QTest::qWait(90);
            press(archiveButton);
            QTest::qWait(60);
            if(!archiveConfirm->isEnabled() ||
               archiveDialog->property("reviewInvalidated").toBool() ||
               !archiveDialog->property("reviewInvalidationReason").toString().isEmpty()) {
                qCritical()<<"UX-S9 fresh archive review after reset not armed";
                app.exit(836);return;
            }
            QTest::keyClick(window,Qt::Key_Escape);
            QTest::qWait(110);
            if(system.findRentalById("R002")->getIsArchived()) {
                qCritical()<<"UX-S8 reopened archive Escape changed record";
                app.exit(832);return;
            }
            QByteArray vehiclesNow,rentalsNow,tempRentalsNow;
            if(!readFile(vehicles,&vehiclesNow) ||
               !readFile(rentals,&rentalsNow) ||
               !readFile(disposable.filePath("rentals.txt"),&tempRentalsNow) ||
               vehiclesNow!=beforeVehicles || rentalsNow!=beforeRentals ||
               tempRentalsNow!=beforeRentals) {
                qCritical()<<"P4 Cancel/Escape wrote domain or demo ledger";
                app.exit(222);return;
            }
            const QString size=compact?"1280x720":"1500x860";
            const QImage shot=window->grabWindow();
            if(shot.isNull() ||
               shot.size()!=QSize(compact?1280:1500,compact?720:860) ||
               !shot.save("p4-qt-shared-components-"+size+".png","PNG")) {
                qCritical()<<"P4 reusable component screenshot mismatch";
                app.exit(223);return;
            }
            qInfo()<<"P4 shared field, domain message, Return/Archive modal PASS"
                   <<size<<"keyboard Cancel/Escape and no writes";
            app.exit(0);
        });
    }
    if (parser.isSet("p4-tab-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window) return 230;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",1);
        QTimer::singleShot(1250,&app,[&,window,compact]{
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name && item->isVisible())return item;
                for(QQuickItem *child:item->childItems())
                    if(auto *hit=self(self,child,name))return hit;
                return nullptr;
            };
            auto *root=window->contentItem();
            auto *search=find(find,root,"fleetSearchField");
            auto *status=find(find,root,"fleetStatusCombo");
            auto *sort=find(find,root,"fleetSortCombo");
            if(!search||!status||!sort) {
                qCritical()<<"P4 Fleet tab controls not found";
                app.exit(231);return;
            }
            // UX-SHELL-004 matches real visual reading order: All -> Name -> Search.
            status->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Tab);
            QCoreApplication::processEvents();
            if(!sort->hasActiveFocus()) {
                qCritical()<<"P4 Tab status to sort failed";app.exit(232);return;
            }
            QTest::keyClick(window,Qt::Key_Tab);
            QCoreApplication::processEvents();
            if(!search->hasActiveFocus()) {
                qCritical()<<"P4 Tab sort to search failed";app.exit(233);return;
            }
            QTest::keyClick(window,Qt::Key_Backtab);
            QCoreApplication::processEvents();
            if(!sort->hasActiveFocus()) {
                qCritical()<<"P4 Shift+Tab search to sort failed";app.exit(234);return;
            }
            QTest::keyClick(window,Qt::Key_Backtab);
            QCoreApplication::processEvents();
            if(!status->hasActiveFocus()) {
                qCritical()<<"P4 Shift+Tab sort to status failed";app.exit(235);return;
            }
            search->forceActiveFocus(Qt::TabFocusReason);
            search->setProperty("text","AB");
            search->setProperty("cursorPosition",1);
            QTest::keyClick(window,Qt::Key_Left);
            QCoreApplication::processEvents();
            if(!search->hasActiveFocus() ||
               search->property("cursorPosition").toInt()!=0 ||
               !fleet.selectedId().isEmpty()) {
                qCritical()<<"P4 nonempty TextField caret Left intercepted";
                app.exit(236);return;
            }
            search->setProperty("text","");
            // An invisible Fleet selection must survive Escape on Overview.
            // Only the visible workspace may own selection-dismissal.
            fleet.selectById("C001");
            if(fleet.selectedId()!="C001") {
                qCritical()<<"P4 fictional Fleet selection setup failed";
                app.exit(253);return;
            }
            window->setProperty("workspace",0);
            QCoreApplication::processEvents();
            if(window->property("canDismissSelection").toBool()) {
                qCritical()<<"P4 Overview captured hidden selection dismissal";
                app.exit(254);return;
            }
            QTest::keyClick(window,Qt::Key_Escape);
            QCoreApplication::processEvents();
            if(window->property("workspace").toInt()!=0 ||
               fleet.selectedId()!="C001") {
                qCritical()<<"P4 Overview Escape cleared hidden Fleet record";
                app.exit(255);return;
            }
            window->setProperty("workspace",1);
            QCoreApplication::processEvents();
            if(!window->property("canDismissSelection").toBool()) {
                qCritical()<<"P4 Fleet selected record cannot be dismissed";
                app.exit(256);return;
            }
            QTest::keyClick(window,Qt::Key_Escape);
            QCoreApplication::processEvents();
            if(!fleet.selectedId().isEmpty() ||
               window->property("canDismissSelection").toBool()) {
                qCritical()<<"P4 visible Fleet Escape failed to dismiss selection";
                app.exit(257);return;
            }
            // Switch to actual rentals UI using a keyboard-activated sidebar.
            auto *rentalNav=find(find,root,"workspaceNavRentals");
            if(!rentalNav){app.exit(237);return;}
            rentalNav->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QTest::qWait(70);
            if(window->property("workspace").toInt()!=2 ||
               !rentalModel.selectById("R001")) {
                qCritical()<<"P4 keyboard workspace change or selected ID";
                app.exit(238);return;
            }
            // One Tab stop for the Rentals collection, not every row.
            auto *rentalsList=find(find,root,"rentalList");
            auto *rentalRow=find(find,root,"rentalRow");
            if(!rentalsList || !rentalRow ||
               !rentalsList->property("activeFocusOnTab").toBool() ||
               rentalRow->property("activeFocusOnTab").toBool()) {
                qCritical()<<"P4 Rentals collection/delegate Tab policy violated";
                app.exit(251);return;
            }
            rentalsList->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Tab);
            QCoreApplication::processEvents();
            if(rentalRow->hasActiveFocus()) {
                qCritical()<<"P4 Tab incorrectly entered an individual rental row";
                app.exit(252);return;
            }
            auto *returnButton=find(find,root,"rentalReturnButton");
            if(!returnButton || !returnButton->isEnabled()) {
                app.exit(239);return;
            }
            returnButton->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QTest::qWait(120);
            auto *cancel=find(find,root,"rentalReturnCancel");
            auto *confirm=find(find,root,"rentalReturnConfirm");
            if(!cancel||!confirm||!cancel->hasActiveFocus() ||
               !confirm->isEnabled()) {
                qCritical()<<"P4 modal initial Cancel focus failed";
                app.exit(240);return;
            }
            QTest::keyClick(window,Qt::Key_Tab);
            QCoreApplication::processEvents();
            if(!confirm->hasActiveFocus()) {
                qCritical()<<"P4 modal Tab did not remain within dialog";
                app.exit(241);return;
            }
            QTest::keyClick(window,Qt::Key_Backtab);
            QCoreApplication::processEvents();
            if(!cancel->hasActiveFocus()) {
                qCritical()<<"P4 modal Shift+Tab failed";
                app.exit(242);return;
            }
            QTest::keyClick(window,Qt::Key_Backtab);
            QCoreApplication::processEvents();
            if(!confirm->hasActiveFocus()) {
                qCritical()<<"P4 modal reverse boundary escaped";
                app.exit(243);return;
            }
            QTest::keyClick(window,Qt::Key_Tab);
            QCoreApplication::processEvents();
            if(!cancel->hasActiveFocus()) {
                qCritical()<<"P4 modal forward boundary escaped";
                app.exit(244);return;
            }
            QTest::keyClick(window,Qt::Key_Escape);
            QTest::qWait(120);
            if(!returnButton->hasActiveFocus() ||
               rentalModel.selectedRentalId()!="R001" ||
               system.findRentalById("R001")->getIsReturned()) {
                qCritical()<<"P4 modal Escape changed business state or lost invoker";
                app.exit(245);return;
            }
            // Booking must open on first actual entry field, not background.
            auto *booking=find(find,root,"sidebarNewRentalButton");
            if(!booking){app.exit(246);return;}
            booking->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QTest::qWait(110);
            auto *vehicle=find(find,root,"bookingVehicleId");
            if(!vehicle||!vehicle->hasActiveFocus()) {
                qCritical()<<"P4 Booking initial vehicle field focus failed";
                app.exit(247);return;
            }
            // Ctrl+F must never steal focus from an open booking modal.
            QTest::keyClick(window,Qt::Key_F,Qt::ControlModifier);
            QTest::qWait(65);
            if(window->property("workspace").toInt()!=2 ||
               !vehicle->hasActiveFocus()) {
                qCritical()<<"P4 Ctrl+F escaped the active booking dialog";
                app.exit(258);return;
            }
            QTest::keyClick(window,Qt::Key_Escape);
            QTest::qWait(120);
            if(!booking->hasActiveFocus()||bookingActions.canConfirm()){
                qCritical()<<"P4 booking Escape did not cancel preview/restore";
                app.exit(248);return;
            }
            // Real cross-workspace Fleet search accelerator should give a
            // visible keyboard focus target, without changing any records.
            QTest::keyClick(window,Qt::Key_F,Qt::ControlModifier);
            QTest::qWait(90);
            if(window->property("workspace").toInt()!=1 ||
               !search->hasActiveFocus() || !search->isVisible()) {
                qCritical()<<"P4 Ctrl+F did not route to visible Fleet search";
                app.exit(259);return;
            }
            // Repeat from Overview, preserving the source's usual search
            // navigation and the prior selection/draft safety rules.
            window->setProperty("workspace",0);
            QCoreApplication::processEvents();
            QTest::keyClick(window,Qt::Key_F,Qt::ControlModifier);
            QTest::qWait(90);
            if(window->property("workspace").toInt()!=1 ||
               !search->hasActiveFocus()) {
                qCritical()<<"P4 Ctrl+F from Overview failed Fleet focus";
                app.exit(260);return;
            }
            QByteArray sourceFleet,sourceRental,tempRental;
            if(!readFile(vehicles,&sourceFleet) ||
               !readFile(rentals,&sourceRental) ||
               !readFile(disposable.filePath("rentals.txt"),&tempRental) ||
               sourceFleet!=beforeVehicles || sourceRental!=beforeRentals ||
               tempRental!=beforeRentals) {
                qCritical()<<"P4 Tab/ShiftTab flow modified fixture data";
                app.exit(249);return;
            }
            const QString size=compact?"1280x720":"1500x860";
            const QImage screenshot=window->grabWindow();
            if(screenshot.isNull()||
               screenshot.size()!=QSize(compact?1280:1500,compact?720:860)||
               !screenshot.save("p4-qt-focus-route-"+size+".png","PNG")) {
                qCritical()<<"P4 focus route screenshot invalid";
                app.exit(250);return;
            }
            qInfo()<<"P4 native focus route PASS"<<size
                   <<"Tab/ShiftTab Fleet, bounded modal, Booking focus/Escape, no writes";
            app.exit(0);
        });
    }
    if (parser.isSet("p5-booking-cancel-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window)return 364;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",2);
        QTimer::singleShot(1160,&app,[&,window,compact] {
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name && item->isVisible())return item;
                for(auto *child:item->childItems())
                    if(auto *found=self(self,child,name))return found;
                return nullptr;
            };
            const auto press=[&](QQuickItem *button) {
                button->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window,Qt::Key_Space);
                QCoreApplication::processEvents();
                QTest::qWait(120);
            };
            auto *root=window->contentItem();
            auto *newRental=find(find,root,"sidebarNewRentalButton");
            auto *outcome=find(find,root,"rentalBookingMessage");
            const QString ledgerPath=disposable.filePath("rentals.txt");
            QByteArray original,still;
            if(!newRental || !outcome || !readFile(ledgerPath,&original) ||
               rentalModel.count()!=2 || rentalModel.activeCount()!=1 ||
               fleet.availableCount()!=4 || bookingActions.canConfirm() ||
               !window->property("bookingReceiptNotice").toString().isEmpty()) {
                qCritical()<<"P5 cancellation baseline or receipt invalid";
                app.exit(365);return;
            }
            // Cancel button path: reviewed C001 + entered fictional contact
            // must never persist anything, including on a valid quote.
            press(newRental);
            auto *vehicle=find(find,root,"bookingVehicleId");
            auto *days=find(find,root,"bookingDays");
            auto *preview=find(find,root,"bookingPreviewButton");
            auto *cancel=find(find,root,"bookingCancelButton");
            auto *confirm=find(find,root,"bookingConfirmButton");
            auto *customer=find(find,root,"bookingCustomer");
            auto *phone=find(find,root,"bookingPhone");
            if(!vehicle || !days || !preview || !cancel || !confirm ||
               !customer || !phone) {
                qCritical()<<"P5 cancel UI control missing";
                app.exit(366);return;
            }
            vehicle->setProperty("text",QStringLiteral("C001"));
            days->setProperty("value",3);
            customer->setProperty("text",QStringLiteral("Fictional Cancel Button"));
            phone->setProperty("text",QStringLiteral("0000000000"));
            press(preview);
            if(!bookingActions.canConfirm() || !confirm->isEnabled() ||
               !bookingActions.quoteSummary().contains("Total R 1350.00")) {
                qCritical()<<"P5 cancel prepared quote missing";
                app.exit(367);return;
            }
            press(cancel);
            QTest::qWait(100);
            if(bookingActions.canConfirm() || bookingActions.needsFreshQuote() ||
               !customer->property("text").toString().isEmpty() ||
               !phone->property("text").toString().isEmpty() ||
               !newRental->hasActiveFocus() ||
               !window->property("bookingReceiptNotice").toString().isEmpty() ||
               outcome->property("text").toString()!="Booking cancelled. No rental saved." ||
               !readFile(ledgerPath,&still) || still!=original ||
               rentalModel.count()!=2 || fleet.availableCount()!=4 ||
               system.findRentalById("R003")) {
                qCritical()<<"P5 Cancel left contact, quote, false receipt or disk mutation";
                app.exit(368);return;
            }
            // Escape must take the identical safe cleanup route, not merely
            // hide the popup while retaining the previous customer's data.
            press(newRental);
            vehicle=find(find,root,"bookingVehicleId");
            preview=find(find,root,"bookingPreviewButton");
            confirm=find(find,root,"bookingConfirmButton");
            customer=find(find,root,"bookingCustomer");
            phone=find(find,root,"bookingPhone");
            if(!vehicle||!preview||!confirm||!customer||!phone ||
               !customer->property("text").toString().isEmpty() ||
               !phone->property("text").toString().isEmpty() ||
               !window->property("bookingCancelledNotice").toString().isEmpty() ||
               confirm->isEnabled()) {
                qCritical()<<"P5 reopen after Cancel has stale contact or quote";
                app.exit(369);return;
            }
            vehicle->setProperty("text",QStringLiteral("C001"));
            customer->setProperty("text",QStringLiteral("Fictional Escape Cancel"));
            phone->setProperty("text",QStringLiteral("0000000000"));
            press(preview);
            if(!bookingActions.canConfirm()) {app.exit(370);return;}
            QTest::keyClick(window,Qt::Key_Escape);
            QTest::qWait(180);
            if(bookingActions.canConfirm() || bookingActions.needsFreshQuote() ||
               !customer->property("text").toString().isEmpty() ||
               !phone->property("text").toString().isEmpty() ||
               !newRental->hasActiveFocus() ||
               !window->property("bookingReceiptNotice").toString().isEmpty() ||
               outcome->property("text").toString()!="Booking cancelled. No rental saved." ||
               !readFile(ledgerPath,&still) || still!=original ||
               rentalModel.count()!=2 || fleet.availableCount()!=4) {
                qCritical()<<"P5 Escape did not cancel and scrub draft correctly";
                app.exit(371);return;
            }
            // After a declined confirmation with missing customer, abandoning
            // the modal must not revive the old quote or preserve contact.
            press(newRental);
            vehicle=find(find,root,"bookingVehicleId");
            preview=find(find,root,"bookingPreviewButton");
            confirm=find(find,root,"bookingConfirmButton");
            customer=find(find,root,"bookingCustomer");
            phone=find(find,root,"bookingPhone");
            if(!vehicle||!preview||!confirm||!customer||!phone) {
                app.exit(372);return;
            }
            vehicle->setProperty("text",QStringLiteral("C001"));
            phone->setProperty("text",QStringLiteral("0000000000"));
            press(preview);
            press(confirm);
            auto *feedback=find(find,root,"bookingDialogFeedback");
            if(!feedback ||
               feedback->property("text").toString()!="Enter a valid customer name." ||
               !bookingActions.canConfirm() ||
               !readFile(ledgerPath,&still) || still!=original) {
                qCritical()<<"P5 invalid customer pre-cancel wrong";
                app.exit(373);return;
            }
            QTest::keyClick(window,Qt::Key_Escape);
            QTest::qWait(180);
            if(bookingActions.canConfirm() ||
               !phone->property("text").toString().isEmpty() ||
               outcome->property("text").toString()!="Booking cancelled. No rental saved." ||
               !readFile(ledgerPath,&still) || still!=original) {
                qCritical()<<"P5 declined then abandoned quote mutated state";
                app.exit(374);return;
            }
            const QString size=compact?"1280x720":"1500x860";
            const auto screenshot=window->grabWindow();
            if(screenshot.isNull() ||
               screenshot.size()!=QSize(compact?1280:1500,compact?720:860) ||
               !screenshot.save("p5-qt-cancel-outcome-"+size+".png","PNG")) {
                qCritical()<<"P5 canceled booking status screenshot missing";
                app.exit(375);return;
            }
            QByteArray sourceVehicles,sourceRentals;
            if(!readFile(vehicles,&sourceVehicles) ||
               !readFile(rentals,&sourceRentals) ||
               sourceVehicles!=beforeVehicles || sourceRentals!=beforeRentals) {
                qCritical()<<"P5 cancellation altered protected fictional source";
                app.exit(376);return;
            }
            qInfo()<<"P5 Cancel/Escape/declined safe booking abandonment PASS"<<size;
            app.exit(0);
        });
    }
    if (parser.isSet("p5-stale-quote-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window)return 347;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",2);
        QTimer::singleShot(1120,&app,[&,window,compact] {
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->isVisible() && item->objectName()==name)return item;
                for(auto *child:item->childItems())
                    if(auto *hit=self(self,child,name))return hit;
                return nullptr;
            };
            const auto press=[&](QQuickItem *button) {
                button->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window,Qt::Key_Space);
                QCoreApplication::processEvents();
                QTest::qWait(90);
            };
            auto *root=window->contentItem();
            const QString ledger=disposable.filePath("rentals.txt");
            QByteArray seed,afterDecline,afterSave,afterUnavailable;
            if(!readFile(ledger,&seed) || rentalModel.count()!=2 ||
               fleet.availableCount()!=4 || bookingActions.needsFreshQuote()) {
                qCritical()<<"P5 stale test initial fictional data invalid";
                app.exit(348);return;
            }
            auto *newRental=find(find,root,"sidebarNewRentalButton");
            if(!newRental){app.exit(349);return;}
            press(newRental);
            auto *vehicle=find(find,root,"bookingVehicleId");
            auto *days=find(find,root,"bookingDays");
            auto *preview=find(find,root,"bookingPreviewButton");
            auto *confirm=find(find,root,"bookingConfirmButton");
            auto *feedback=find(find,root,"bookingDialogFeedback");
            auto *customer=find(find,root,"bookingCustomer");
            auto *phone=find(find,root,"bookingPhone");
            if(!vehicle || !days || !preview || !confirm ||
               !feedback || !customer || !phone) {
                qCritical()<<"P5 native booking modal missing";
                app.exit(350);return;
            }
            vehicle->setProperty("text",QStringLiteral("C001"));
            days->setProperty("value",3);
            customer->setProperty("text",QStringLiteral("Fictional Quote Retry"));
            phone->setProperty("text",QStringLiteral("0000000000"));
            press(preview);
            if(!bookingActions.canConfirm() ||
               bookingActions.needsFreshQuote() ||
               !bookingActions.quoteSummary().contains("Total R 1350.00") ||
               !confirm->isEnabled()) {
                qCritical()<<"P5 original C++ quote not reviewed";
                app.exit(351);return;
            }
            // Simulate the authoritative C++ vehicle's rate changing AFTER
            // the operator reviewed a price and BEFORE they click Confirm.
            Vehicle *corolla=system.findVehicleById("C001");
            if(!corolla) {app.exit(352);return;}
            corolla->setPricePerDay(500.0);
            press(confirm);
            if(!bookingActions.needsFreshQuote() ||
               bookingActions.canConfirm() || confirm->isEnabled() ||
               !preview->hasActiveFocus() ||
               preview->property("text").toString()!="Refresh quote" ||
               feedback->property("text").toString()!="Quote changed. Preview again." ||
               rentalModel.count()!=2 || fleet.availableCount()!=4 ||
               !window->property("bookingReceiptNotice").toString().isEmpty() ||
               !readFile(ledger,&afterDecline) || afterDecline!=seed) {
                qCritical()<<"P5 stale price not blocked, focused or reported"
                           <<bookingActions.needsFreshQuote()
                           <<bookingActions.canConfirm()
                           <<preview->property("text")
                           <<feedback->property("text");
                app.exit(353);return;
            }
            // Retry must call the C++ quote service, show the NEW R500/day
            // and require a separate second Confirm. No optimistic writes.
            press(preview);
            if(bookingActions.needsFreshQuote() ||
               !bookingActions.canConfirm() || !confirm->isEnabled() ||
               !bookingActions.quoteSummary().contains("R 500.00/day") ||
               !bookingActions.quoteSummary().contains("Total R 1500.00") ||
               !readFile(ledger,&afterDecline) || afterDecline!=seed) {
                qCritical()<<"P5 refreshed quote not authoritative or mutated disk";
                app.exit(354);return;
            }
            press(confirm);
            QTest::qWait(130);
            auto *receipt=find(find,root,"rentalBookingMessage");
            if(!receipt ||
               receipt->property("text").toString()!="Booking R003 saved." ||
               bookingActions.lastRentalId()!="R003" ||
               bookingActions.needsFreshQuote() ||
               rentalModel.activeCount()!=2 || fleet.availableCount()!=3 ||
               !corolla->getIsRented() ||
               !readFile(ledger,&afterSave) || afterSave==seed) {
                qCritical()<<"P5 refreshed quote failed to book at current rate";
                app.exit(355);return;
            }
            const Rental *saved=system.findRentalById("R003");
            if(!saved || std::abs(saved->getTotalCost()-1500.0)>0.01) {
                qCritical()<<"P5 save did not use refreshed R1500 contract value";
                app.exit(356);return;
            }
            // A second quote for another available car is invalidated if
            // the C++ domain marks it unavailable while the popup is open.
            newRental=find(find,root,"sidebarNewRentalButton");
            if(!newRental){app.exit(357);return;}
            press(newRental);
            vehicle=find(find,root,"bookingVehicleId");
            days=find(find,root,"bookingDays");
            preview=find(find,root,"bookingPreviewButton");
            confirm=find(find,root,"bookingConfirmButton");
            feedback=find(find,root,"bookingDialogFeedback");
            customer=find(find,root,"bookingCustomer");
            phone=find(find,root,"bookingPhone");
            if(!vehicle||!days||!preview||!confirm||!feedback||!customer||!phone) {
                app.exit(358);return;
            }
            // Prior successful booking deliberately cleared customer inputs.
            // Re-enter valid fictional details so this negative-path test
            // reaches changed availability, not InvalidCustomerName first.
            customer->setProperty("text",QStringLiteral("Fictional Availability Retry"));
            phone->setProperty("text",QStringLiteral("0000000000"));
            vehicle->setProperty("text",QStringLiteral("C003"));
            days->setProperty("value",2);
            press(preview);
            if(!bookingActions.canConfirm()) {
                qCritical()<<"P5 second fictional quote missing";
                app.exit(359);return;
            }
            Vehicle *other=system.findVehicleById("C003");
            if(!other){app.exit(360);return;}
            other->setIsRented(true);
            press(confirm);
            if(!bookingActions.needsFreshQuote() ||
               bookingActions.canConfirm() || confirm->isEnabled() ||
               !preview->hasActiveFocus() ||
               feedback->property("text").toString()!="Vehicle became unavailable. Preview again." ||
               !window->property("bookingReceiptNotice").toString().isEmpty() ||
               !readFile(ledger,&afterUnavailable) || afterUnavailable!=afterSave) {
                qCritical()<<"P5 unavailable vehicle not rejected in real modal";
                app.exit(361);return;
            }
            press(preview);
            if(bookingActions.canConfirm() || confirm->isEnabled() ||
               feedback->property("text").toString()!="Vehicle is not available." ||
               !readFile(ledger,&afterUnavailable) || afterUnavailable!=afterSave) {
                qCritical()<<"P5 retry incorrectly allowed unavailable C003";
                app.exit(362);return;
            }
            // No temporary test state can leak into the seed files.
            QByteArray protectedVehicles,protectedRentals;
            if(!readFile(vehicles,&protectedVehicles) ||
               !readFile(rentals,&protectedRentals) ||
               protectedVehicles!=beforeVehicles ||
               protectedRentals!=beforeRentals) {
                qCritical()<<"P5 rejected quote altered pinned source files";
                app.exit(363);return;
            }
            qInfo()<<"P5 stale price and changed availability real QML retry PASS"
                   <<(compact?"1280x720":"1500x860");
            app.exit(0);
        });
    }
    if(parser.isSet("p5-fleet-to-booking-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window)return 338;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",1);
        QTimer::singleShot(1150,&app,[&,window,compact] {
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->isVisible() && item->objectName()==name)return item;
                for(auto *child:item->childItems())
                    if(auto *hit=self(self,child,name))return hit;
                return nullptr;
            };
            const auto press=[&](QQuickItem *button) {
                button->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window,Qt::Key_Space);
                QCoreApplication::processEvents();
                QTest::qWait(130);
            };
            auto *root=window->contentItem();
            auto *action=find(find,root,"fleetBookSelectedButton");
            QByteArray initialLedger,afterLedger;
            const QString ledger=disposable.filePath("rentals.txt");
            if(!action || action->isEnabled() ||
               !readFile(ledger,&initialLedger) ||
               fleet.selectedId()!="" ||
               fleet.availableCount()!=4) {
                qCritical()<<"P5 Fleet booking initial action not safely disabled";
                app.exit(339);return;
            }
            // Existing fictional Ranger C002 is rented: never expose a
            // working booking CTA or advance to the confirmation dialog.
            fleet.selectById("C002");
            QCoreApplication::processEvents();
            if(action->isEnabled() || fleet.selectedAvailable()) {
                qCritical()<<"P5 rented Fleet vehicle offered booking action";
                app.exit(340);return;
            }
            // Choose an AVAILABLE real C++ Fleet summary, then use an
            // actual keyboard-activated button, not an invented QML shortcut.
            fleet.selectById("C001");
            QCoreApplication::processEvents();
            if(!action->isEnabled() || !fleet.selectedAvailable()) {
                qCritical()<<"P5 available Corolla missing Fleet action";
                app.exit(341);return;
            }
            press(action);
            auto *vehicle=find(find,root,"bookingVehicleId");
            auto *days=find(find,root,"bookingDays");
            auto *preview=find(find,root,"bookingPreviewButton");
            auto *confirm=find(find,root,"bookingConfirmButton");
            auto *name=find(find,root,"bookingCustomer");
            auto *phone=find(find,root,"bookingPhone");
            // QML Dialog derives from Popup, not QQuickItem. The recursive
            // find() deliberately walks only visible QQuickItem controls:
            // detecting the actual visible modal inputs is the reliable
            // cross-platform test of an opened popup, not querying a Popup
            // objectName as though it were a visual QQuickItem delegate.
            if(window->property("workspace").toInt()!=2 ||
               !vehicle || !days || !preview || !confirm ||
               !name || !phone ||
               vehicle->property("text").toString()!="C001" ||
               bookingActions.canConfirm() || confirm->isEnabled()) {
                qCritical()<<"P5 Fleet did not prefill the real safe rental dialog"
                           <<"workspace"<<window->property("workspace")
                           <<"vehicle control"<<bool(vehicle)
                           <<"days"<<bool(days)<<"preview"<<bool(preview)
                           <<"confirm"<<bool(confirm)
                           <<"name"<<bool(name)<<"phone"<<bool(phone)
                           <<"vehicle ID"<<(vehicle?vehicle->property("text"):QVariant{});
                app.exit(342);return;
            }
            // UX-FB2: the physically open owner Fleet C001 action is a
            // continuous workflow, not two separately tested buttons.
            // Preview a legitimate quote, Cancel it from the Fleet-origin
            // modal, then reopen via the same selected Fleet identity.
            // No C++ booking commit or protected rental file may occur.
            days->setProperty("value",3);
            name->setProperty("text",QStringLiteral("Fictional Fleet Cancel"));
            phone->setProperty("text",QStringLiteral("0000000000"));
            press(preview);
            auto *cancel=find(find,root,"bookingCancelButton");
            if(!cancel || !bookingActions.canConfirm() || !confirm->isEnabled() ||
               !bookingActions.quoteSummary().contains("Total R 1350.00")) {
                qCritical()<<"UX-FB2 available Fleet C001 preview missing before Cancel";
                app.exit(920);return;
            }
            press(cancel);
            QTest::qWait(135);
            auto *sidebarAction=find(find,root,"sidebarNewRentalButton");
            if(!sidebarAction || !sidebarAction->hasActiveFocus() ||
               find(find,root,"bookingConfirmButton") ||
               bookingActions.canConfirm() || bookingActions.needsFreshQuote() ||
               !window->property("bookingReceiptNotice").toString().isEmpty() ||
               window->property("bookingCancelledNotice").toString()!=
                    "Booking cancelled. No rental saved." ||
               !name->property("text").toString().isEmpty() ||
               !phone->property("text").toString().isEmpty() ||
               fleet.selectedId()!="C001" || !fleet.selectedAvailable() ||
               fleet.availableCount()!=4 || rentalModel.count()!=2 ||
               rentalModel.activeCount()!=1 || system.findRentalById("R003") ||
               !readFile(ledger,&afterLedger) || afterLedger!=initialLedger) {
                qCritical()<<"UX-FB2 Fleet C001 Cancel wrote rental, lost selection or leaked draft";
                app.exit(921);return;
            }
            window->setProperty("workspace",1);
            QCoreApplication::processEvents();
            QTest::qWait(100);
            action=find(find,root,"fleetBookSelectedButton");
            if(!action || !action->isEnabled() || fleet.selectedId()!="C001") {
                qCritical()<<"UX-FB2 Fleet C001 action unavailable after Cancel";
                app.exit(922);return;
            }
            press(action);
            vehicle=find(find,root,"bookingVehicleId");
            days=find(find,root,"bookingDays");
            preview=find(find,root,"bookingPreviewButton");
            confirm=find(find,root,"bookingConfirmButton");
            name=find(find,root,"bookingCustomer");
            phone=find(find,root,"bookingPhone");
            if(window->property("workspace").toInt()!=2 ||
               !vehicle || !days || !preview || !confirm || !name || !phone ||
               vehicle->property("text").toString()!="C001" ||
               !name->property("text").toString().isEmpty() ||
               !phone->property("text").toString().isEmpty() ||
               !window->property("bookingCancelledNotice").toString().isEmpty() ||
               bookingActions.canConfirm() || confirm->isEnabled()) {
                qCritical()<<"UX-FB2 Fleet reopening reused stale customer, quote or target";
                app.exit(923);return;
            }
            // Continue the original positive real Fleet C001 booking path
            // unchanged. This demonstrates Cancel did not poison its next use.
            days->setProperty("value",3);
            press(preview);
            if(!bookingActions.canConfirm() ||
               !bookingActions.quoteSummary().contains("Total R 1350.00") ||
               !confirm->isEnabled()) {
                qCritical()<<"P5 Fleet-provided C001 quote bypassed authoritative C++";
                app.exit(343);return;
            }
            name->setProperty("text",QStringLiteral("Fictional Fleet Quote"));
            phone->setProperty("text",QStringLiteral("0000000000"));
            press(confirm);
            QTest::qWait(130);
            auto *receipt=find(find,root,"rentalBookingMessage");
            if(!receipt || find(find,root,"bookingConfirmButton") ||
               bookingActions.lastRentalId()!="R003" ||
               receipt->property("text").toString()!="Booking R003 saved." ||
               rentalModel.activeCount()!=2 ||
               fleet.availableCount()!=3 ||
               !system.findVehicleById("C001")->getIsRented() ||
               !readFile(ledger,&afterLedger) ||
               afterLedger==initialLedger) {
                qCritical()<<"P5 Fleet booking did not persist and show a real receipt";
                app.exit(344);return;
            }
            const QString suffix=compact?"1280x720":"1500x860";
            const auto shot=window->grabWindow();
            if(shot.isNull() || shot.size()!=QSize(compact?1280:1500,compact?720:860) ||
               !shot.save("p5-qt-fleet-booking-receipt-"+suffix+".png","PNG")) {
                qCritical()<<"P5 Fleet booking capture failed";
                app.exit(345);return;
            }
            window->setProperty("workspace",1);
            QCoreApplication::processEvents();
            QTest::qWait(90);
            action=find(find,root,"fleetBookSelectedButton");
            if(!action || action->isEnabled() || fleet.selectedId()!="C001" ||
               fleet.selectedAvailable()) {
                qCritical()<<"P5 previously booked Fleet vehicle still bookable";
                app.exit(346);return;
            }
            qInfo()<<"P5 real Fleet-to-Rentals guided keyboard booking PASS"<<suffix;
            app.exit(0);
        });
    }
    // Precision Automotive Light: native shell and the existing real booking
    // modal. This is a presentation-only shortcut, not a new domain action.
    // Precision Automotive 1B: protect the operational semantics while
    // rendering the genuine Light dashboard and accessible availability ring.
    // Precision Automotive Light: real Qt Fleet, not a generated picture.
    // Validate selected identity, status, photo-aware cards, table headers,
    // source-safe keyboard switching and both logical desktop sizes.
    if (parser.isSet("pa-fleet-003-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window) return 1060;
        const bool compact=parser.isSet("compact");
        const QSize expected(compact?1280:1500,compact?720:860);
        window->resize(expected);
        window->setProperty("workspace",1);
        QTimer::singleShot(1350,&app,[&,window,compact,expected] {
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name)return item;
                for(auto *child:item->childItems())
                    if(auto *hit=self(self,child,name))return hit;
                return nullptr;
            };
            const auto get=[&](const QString &name)->QQuickItem* {
                if(auto *item=find(find,window->contentItem(),name))return item;
                return window->findChild<QQuickItem*>(name);
            };
            auto *toolbar=get("fleetToolbar");
            auto *gallery=get("fleetGallery");
            auto *table=get("fleetTable");
            auto *inspector=get("vehicleInspector");
            auto *hero=get("fleetInspectorSelectedArt");
            auto *heroName=get("fleetSelectedHeroName");
            auto *heroIdentity=get("fleetSelectedHeroIdentity");
            auto *detailsHeading=get("fleetInspectorDetailsHeading");
            auto *closeButton=get("fleetInspectorCloseButton");
            auto *closeIcon=get("fleetInspectorCloseIcon");
            auto *photoCredit=get("fleetInspectorPhotoCredit");
            auto *bookButton=get("fleetBookSelectedButton");
            auto *rateBand=get("fleetInspectorRateBand");
            auto *rate=get("fleetInspectorRateValue");
            auto *status=get("fleetInspectorStatusPill");
            auto *cardRate=get("fleetCardRate");
            auto *cardPill=get("fleetCardStatusPill");
            auto *search=get("fleetSearchField");
            auto *switchToTable=get("fleetTableToggle");
            if(!toolbar||!gallery||!table||!inspector||!hero||
               !heroName||!heroIdentity||!detailsHeading||!bookButton||
               !rateBand||!rate||!status||!cardRate||!cardPill||!search||
               !switchToTable || gallery->property("count").toInt()<1 ||
               !gallery->isVisible() ||
               search->property("placeholderText").toString()!=
                  QStringLiteral("Search make, model or vehicle ID")) {
                qCritical()<<"PA Fleet 003 real Gallery/Inspector controls missing";
                app.exit(1061);return;
            }
            QByteArray initialFleet,initialRentals;
            if(!readFile(vehicles,&initialFleet) ||
               !readFile(rentals,&initialRentals) ||
               initialFleet!=beforeVehicles || initialRentals!=beforeRentals) {
                qCritical()<<"PA Fleet 003 protected source baseline mismatch";
                app.exit(1062);return;
            }
            // Native proof that the unselected wide inspector is useful but
            // does not pretend any model has already been selected.
            auto *emptyGuide=get("fleetInspectorEmptyGuide");
            auto *emptyTotal=get("fleetInspectorEmptyTotal");
            auto *emptyAvailable=get("fleetInspectorEmptyAvailable");
            auto *emptyDescription=get("fleetInspectorEmptyDescription");
            auto *emptyHint=get("inspectorHint");
            if(fleet.selectedId()!="" || !emptyGuide || !emptyTotal ||
               !emptyAvailable || !emptyDescription || !emptyHint ||
               emptyGuide->isVisible()==compact ||
               emptyDescription->isVisible()!=compact ||
               emptyHint->isVisible()!=compact ||
               emptyTotal->property("text").toString()!=
                   QString::number(fleet.totalCount()) ||
               emptyAvailable->property("text").toString()!=
                   QString::number(fleet.availableCount())) {
                qCritical()<<"R2 Fleet empty inspector values or visibility wrong";
                app.exit(1075);return;
            }
            fleet.selectById("C001");
            if(fleet.selectedId()!="C001" || emptyGuide->isVisible() ||
               emptyDescription->isVisible() || !emptyHint->isVisible()) {
                qCritical()<<"PA Fleet 003 cannot select real demo car";
                app.exit(1063);return;
            }
            QTest::qWait(120);
            if(!rateBand->isVisible() ||
               rate->property("text").toString()!=
                   QStringLiteral("R ")+fleet.selectedRate() ||
               status->property("available").toBool()!=fleet.selectedAvailable() ||
               !cardRate->property("text").toString().startsWith("R ") ||
               cardPill->property("statusText").toString().isEmpty()) {
                qCritical()<<"PA Fleet 003 fake or stale rate/status state"
                           <<rate->property("text")<<status->property("available");
                app.exit(1064);return;
            }
            // Verify dark C++-selected identity, truthful booking gating and
            // the R2 60/40 gallery-to-inspector split at both desktop sizes.
            // Native evidence: a local licensed SVG replaces the old
            // Unicode close glyph, with the real C++ credit still legible.
            if(!closeButton || !closeIcon || !photoCredit ||
               !closeButton->isVisible() || closeButton->width()<35 ||
               !closeIcon->property("assetReady").toBool() ||
               !photoCredit->isVisible() ||
               !photoCredit->property("text").toString().startsWith("PHOTO CREDIT") ||
               !photoCredit->property("text").toString().contains("CC0 1.0") ||
               photoCredit->property("font").value<QFont>().pixelSize()<11 ||
               hero->property("color").value<QColor>()!=QColor("#072A28") ||
               heroName->property("text").toString()!=fleet.selectedName() ||
               !heroIdentity->property("text").toString().contains("C001") ||
               !heroIdentity->property("text").toString().contains("Available") ||
               detailsHeading->property("text").toString()!="VEHICLE DETAILS" ||
               !bookButton->isEnabled()) {
                qCritical()<<"R2 Fleet selected-vehicle header or booking state wrong";
                app.exit(1069);return;
            }
            fleet.selectById("C002");
            QTest::qWait(80);
            if(fleet.selectedAvailable() || bookButton->isEnabled() ||
               !heroIdentity->property("text").toString().contains("C002") ||
               !heroIdentity->property("text").toString().contains("Rented")) {
                qCritical()<<"R2 Fleet rented vehicle falsely bookable";
                app.exit(1070);return;
            }
            fleet.selectById("C001");
            QTest::qWait(80);
            const qreal inspectorShare=inspector->width()/
                (gallery->width()+inspector->width());
            if(qAbs(inspectorShare-0.40)>0.045 ||
               hero->width()<inspector->width()-45 ||
               !hero->isVisible() || !detailsHeading->isVisible()) {
                qCritical()<<"R2 Fleet 60/40 geometry or contextual inspector missing"
                           <<inspectorShare<<hero->width()<<inspector->width();
                app.exit(1071);return;
            }
            // At the compact desktop target, two fully legible photo-led
            // rows must fit without an automatic second-row scroll crop.
            if(compact &&
               (gallery->property("cellHeight").toReal()>236 ||
                gallery->property("contentY").toReal()>3 ||
                gallery->property("count").toInt()<4)) {
                qCritical()<<"R2 compact Fleet first row cropped by gallery scroll"
                           <<gallery->property("contentY")
                           <<gallery->property("cellHeight");
                app.exit(1072);return;
            }
            // Verify the actual C++ historical-record warning is not drawn
            // underneath the inspector edge at either approved window size.
            // This caught a wide-only layout clip despite previous green CTests.
            auto *historyNotice=get("fleetRentalHistoryHint");
            if(!historyNotice || !historyNotice->isVisible() ||
               historyNotice->height()<10 ||
               !fleetRemove.hasRentalHistory(QStringLiteral("C001"))) {
                qCritical()<<"R2 Fleet historical removal explanation not rendered";
                app.exit(1073);return;
            }
            const QPointF noticeBottom=historyNotice->mapToScene(
                QPointF(0,historyNotice->height()));
            const QPointF inspectorBottom=inspector->mapToScene(
                QPointF(0,inspector->height()));
            if(noticeBottom.y()>inspectorBottom.y()-5 ||
               noticeBottom.y()>window->height()-5) {
                qCritical()<<"R2 Fleet historical removal explanation clipped"
                           <<noticeBottom<<inspectorBottom<<window->height();
                app.exit(1074);return;
            }
            const QPointF galleryX=gallery->mapToScene(QPointF(0,0));
            const QPointF inspectorX=inspector->mapToScene(QPointF(0,0));
            if(gallery->width()<280 || inspector->width()<320 ||
               galleryX.x()<0 || inspectorX.x()<galleryX.x()+gallery->width()-2 ||
               inspectorX.x()+inspector->width()>window->width()-5) {
                qCritical()<<"PA Fleet 003 columns clipped or overlapping"
                           <<galleryX<<gallery->width()<<inspectorX
                           <<inspector->width();
                app.exit(1065);return;
            }
            const auto capture=[&](const QString &state)->bool {
                const QString size=compact?QStringLiteral("1280x720"):
                                           QStringLiteral("1500x860");
                const QImage shot=window->grabWindow();
                return !shot.isNull() && shot.size()==expected &&
                    shot.save(QStringLiteral("pa-fleet-003-%1-%2.png")
                                  .arg(state,size),"PNG");
            };
            if(!capture("gallery")) {
                qCritical()<<"PA Fleet 003 gallery screenshot failed";
                app.exit(1066);return;
            }
            switchToTable->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QTest::qWait(140);
            auto *header=get("fleetTableHeader");
            auto *row=get("fleetTableRow");
            auto *rowStatus=get("fleetTableStatusPill");
            if(window->property("gallery").toBool() ||
               !table->isVisible() || table->property("count").toInt()<1 ||
               !header||!header->isVisible()||!row||!rowStatus ||
               rowStatus->property("statusText").toString().isEmpty() ||
               fleet.selectedId()!="C001") {
                qCritical()<<"PA Fleet 003 Gallery/Table switch broke selection/header";
                app.exit(1067);return;
            }
            if(!capture("table")) {
                qCritical()<<"PA Fleet 003 table screenshot failed";
                app.exit(1068);return;
            }
            QByteArray finalFleet,finalRentals;
            if(!readFile(vehicles,&finalFleet)||
               !readFile(rentals,&finalRentals)||
               finalFleet!=initialFleet || finalRentals!=initialRentals) {
                qCritical()<<"PA Fleet 003 visual switch changed protected fixtures";
                app.exit(1069);return;
            }
            qInfo()<<"Precision Automotive Light real Fleet PASS"
                   <<(compact?"1280x720":"1500x860")
                   <<"identity, status pills, rate, table header, no writes";
            app.exit(0);
        });
    }
    // Precision Automotive Light Rentals: inspect real Qt object geometry,
    // status rows, screenshots and immutable source fixtures. No domain write.
    if (parser.isSet("pa-rentals-004-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window) return 1080;
        const bool compact=parser.isSet("compact");
        const QSize expected(compact?1280:1500,compact?720:860);
        window->resize(expected);
        window->setProperty("workspace",2);
        QTimer::singleShot(1350,&app,[&,window,compact,expected] {
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name)return item;
                for(auto *child:item->childItems())
                    if(auto *hit=self(self,child,name))return hit;
                return nullptr;
            };
            const auto get=[&](const QString &name)->QQuickItem* {
                if(auto *item=find(find,window->contentItem(),name))return item;
                return window->findChild<QQuickItem*>(name);
            };
            auto *scroll=get("rentalsPageScroll");
            auto *heading=get("rentalSectionHeading");
            auto *pageSubtitle=get("workspacePageSubtitle");
            auto *actions=get("rentalActionPanel");
            auto *filters=get("rentalFilterPanel");
            auto *records=get("rentalRecordsPanel");
            auto *summary=get("rentalEditorialSummary");
            auto *bookedMetric=get("rentalMetricBookedValue");
            auto *hero=get("rentalInspectorHero");
            auto *sparseSurface=get("rentalSparseSurface");
            auto *listViewport=get("rentalListViewport");
            auto *heroId=get("rentalInspectorId");
            auto *heroVehicle=get("rentalInspectorVehicle");
            auto *dueState=get("rentalInspectorDueState");
            auto *header=get("rentalTableHeader");
            auto *count=get("rentalRegisterCount");
            auto *booking=get("sidebarNewRentalButton");
            // UX-DR1 requires physical absence, not hidden/disabled duplicate.
            auto *retiredRegisterBooking=get("rentalNewBookingButton");
            auto *moneyNotice=get("rentalFictionalDisclosure");
            auto *returnAction=get("rentalReturnButton");
            auto *archiveAction=get("rentalArchiveButton");
            auto *deleteAction=get("rentalDeleteArchivedButton");
            auto *managementRow=get("rentalRecordManagementRow");
            if(!scroll||!heading||!pageSubtitle||!actions||!filters||!records||
               !summary||!bookedMetric||!hero||!heroId||!heroVehicle||dueState==nullptr||
               !header||!count||!booking||retiredRegisterBooking||!moneyNotice||
               !returnAction||!archiveAction||!deleteAction||!managementRow||
               heading->property("text").toString()!=QStringLiteral("Contract Register")||
               pageSubtitle->property("text").toString()!=
                   QStringLiteral("Review rental contracts, return dates and history")||
               !pageSubtitle->isVisible()||
               actions->width()<288||filters->width()<300||
               records->width()<300||!booking->isVisible()||
               booking->property("text").toString()!=QStringLiteral("New rental")||
               moneyNotice->property("text").toString()!=
                   QStringLiteral("Booked values describe contracts, not payments")||
               !count->property("text").toString().contains(QStringLiteral("record"))) {
                qCritical()<<"PA Rentals 004 missing rendered controls";
                app.exit(1081);return;
            }
            // R2 register-first 69/31 split, embedded filters and source-backed
            // compact contract inspector; business state is unchanged.
            const auto sceneRect=[](QQuickItem *item) {
                return item->mapRectToScene(
                    QRectF(0,0,item->width(),item->height()));
            };
            const qreal inspectorShare=actions->width()/
                (records->width()+actions->width());
            const QRectF recordsRect=sceneRect(records);
            const QRectF filterRect=sceneRect(filters);
            // UX-RH1: archive and permanent delete are visibly grouped but
            // distinct. Keep both click targets >=44px, inside the inspector,
            // with native keyboard and C++ command eligibility unchanged.
            const QRectF manageRect=sceneRect(managementRow);
            const QRectF archiveRect=sceneRect(archiveAction);
            const QRectF deleteRect=sceneRect(deleteAction);
            const QRectF inspectorRect=sceneRect(actions);
            if (!inspectorRect.adjusted(-3,-3,3,3).contains(manageRect) ||
                !manageRect.adjusted(-3,-3,3,3).contains(archiveRect) ||
                !manageRect.adjusted(-3,-3,3,3).contains(deleteRect) ||
                archiveRect.intersects(deleteRect) ||
                qAbs(archiveRect.center().y()-deleteRect.center().y())>5 ||
                archiveRect.height()<43 || deleteRect.height()<43 ||
                archiveRect.width()<115 || deleteRect.width()<115) {
                qCritical()<<"UX-RH1 inspector record-management controls clipped"
                           <<manageRect<<archiveRect<<deleteRect<<inspectorRect;
                app.exit(1091);return;
            }
            // UX-TR2: status filters remain on the first line; real native
            // due-state buttons and ID search share the second, with neither
            // control clipped on wide or compact desktop Qt rendering.
            auto *statusRow=get("rentalStatusRow");
            auto *secondary=get("rentalSecondaryFilterRow");
            auto *dueRow=get("rentalDueRow");
            auto *lookupRow=get("rentalLookupRow");
            auto *overdueControl=get("rentalFilterOverdue");
            auto *findControl=get("rentalLookupButton");
            if (!statusRow || !secondary || !dueRow || !lookupRow ||
                !overdueControl || !findControl) {
                qCritical()<<"UX-TR2 filter/search controls missing";
                app.exit(1089);return;
            }
            const QRectF secondLine=sceneRect(secondary);
            const QRectF dueRect=sceneRect(dueRow);
            const QRectF lookupRect=sceneRect(lookupRow);
            if (!filterRect.adjusted(-3,-3,3,3).contains(secondLine) ||
                !secondLine.adjusted(-3,-3,3,3).contains(dueRect) ||
                !secondLine.adjusted(-3,-3,3,3).contains(lookupRect) ||
                !secondLine.adjusted(-3,-3,3,3).contains(sceneRect(overdueControl)) ||
                !secondLine.adjusted(-3,-3,3,3).contains(sceneRect(findControl)) ||
                qAbs(dueRect.center().y()-lookupRect.center().y())>25 ||
                sceneRect(statusRow).bottom()>secondLine.top()+3) {
                qCritical()<<"UX-TR2 filter and search row clipped or stacked"
                           <<secondLine<<dueRect<<lookupRect<<filterRect;
                app.exit(1090);return;
            }
            // UX-LC1: keep a source-backed two-row register compact, without
            // clipping native filter controls or shortening large lists.
            // This tests actual rendered Qt geometry at both viewport sizes.
            if (rentalModel.count()==2 &&
                (compact ? (records->height()<595 || records->height()>615) :
                           (records->height()<485 || records->height()>505))) {
                qCritical()<<"UX-LC1 sparse register geometry wrong"
                           <<records->height()<<compact;
                app.exit(1088);return;
            }
             // UX-LC2: source-backed sparse white card ends near its records,
             // while the list and overall page keep their safety geometry.
             if (rentalModel.count()==2 &&
                 (!sparseSurface || !listViewport ||
                  !sparseSurface->isVisible() ||
                  listViewport->height()<160 ||
                  sparseSurface->height()>records->height()-22 ||
                  sparseSurface->height()<335 ||
                  !recordsRect.adjusted(-2,-2,2,2).contains(sceneRect(sparseSurface)) ||
                  sparseSurface->property("color").value<QColor>()!=QColor("#FFFFFF"))) {
                 qCritical()<<"UX-LC2 sparse surface or visible list viewport wrong"
                            <<(sparseSurface?sparseSurface->height():-1)
                            <<(listViewport?listViewport->height():-1)
                            <<records->height()<<compact;
                 app.exit(1092);return;
             }
            if(qAbs(inspectorShare-0.31)>0.05 ||
               !recordsRect.contains(filterRect) ||
               hero->property("color").value<QColor>()!=QColor("#072A28") ||
                heroId->property("color").value<QColor>()!=QColor("#FFFFFF") ||
                heroVehicle->property("color").value<QColor>()!=QColor("#D5E8E2") ||
               qAbs(bookedMetric->property("metricValue").toDouble()-
                    rentalModel.activeBookedValue())>0.01) {
                qCritical()<<"R2 Rentals evergreen header/filter geometry or money source wrong"
                           <<inspectorShare<<recordsRect<<filterRect;
                app.exit(1085);return;
            }
            // The inspector must reflect the real C++ selection, with no
            // fabricated default contract or unauthorized return action.
            if(!rentalModel.selectById(QStringLiteral("R001"))) {
                qCritical()<<"R2 Rentals protected example contract missing";
                app.exit(1086);return;
            }
            QTest::qWait(90);
            if(heroId->property("text").toString()!=QStringLiteral("R001") ||
               !heroVehicle->property("text").toString().contains("C002") ||
               !dueState->property("text").toString().contains("Overdue") ||
               !returnAction->isEnabled() ||
               archiveAction->isEnabled()) {
                qCritical()<<"R2 Rentals C++ selection/due/action state regression";
                app.exit(1087);return;
            }
            const auto capture=[&](const QString &state)->bool {
                const QString size=compact?QStringLiteral("1280x720"):
                                           QStringLiteral("1500x860");
                const QImage shot=window->grabWindow();
                return !shot.isNull()&&shot.size()==expected&&
                       shot.save(QStringLiteral("pa-rentals-004-%1-%2.png")
                                     .arg(state,size),"PNG");
            };
            scroll->setProperty("contentY",0);
            QTest::qWait(130);
            if(!capture("controls")) {
                qCritical()<<"PA Rentals controls screenshot unavailable";
                app.exit(1082);return;
            }
            const qreal maxY=qMax(0.0,scroll->property("contentHeight").toReal()-
                                      scroll->height());
            scroll->setProperty("contentY",maxY);
            QTest::qWait(180);
            auto *pill=get("rentalStatusPill");
            if(!header->isVisible()||!records->isVisible()||
               !pill||pill->property("statusValue").toString().isEmpty()||
               !capture("register")) {
                qCritical()<<"PA Rentals register / status screenshot unavailable";
                app.exit(1083);return;
            }
            QByteArray afterFleet,afterRentals;
            if(!readFile(vehicles,&afterFleet)||!readFile(rentals,&afterRentals)||
               afterFleet!=beforeVehicles||afterRentals!=beforeRentals) {
                qCritical()<<"PA Rentals appearance changed protected fixtures";
                app.exit(1084);return;
            }
            qInfo()<<"Precision Automotive Rentals source PASS"
                   <<(compact?"1280x720":"1500x860")
                   <<"real contract panels and unchanged fixtures";
            app.exit(0);
        });
    }
    if(parser.isSet("r2-font-trial-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window)return 1054;
        const bool compact=parser.isSet("compact");
        const QSize size(compact?1280:1500,compact?720:860);
        window->resize(size);
        window->setProperty("workspace",0);
        QTimer::singleShot(1400,&app,[&,window,size]{
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name)return item;
                for(auto *child:item->childItems())
                    if(auto *match=self(self,child,name))return match;
                return nullptr;
            };
            const auto get=[&](const QString &name)->QQuickItem* {
                return find(find,window->contentItem(),name);
            };
            auto *heading=get("workspacePageHeading");
            auto *subtitle=get("workspacePageSubtitle");
            auto *metrics=get("overviewMetricGrid");
            auto *hero=get("editorialOverviewHero");
            auto *readiness=get("overviewAvailabilityPanel");
            auto *disclosure=get("overviewDemoDisclosure");
            if(!heading||!subtitle||!metrics||!hero||!readiness||!disclosure ||
               r2FontTrialFamily.isEmpty()) {
                qCritical()<<"R2 Inter trial missing actual QML content or font";
                app.exit(1055);return;
            }
            const QFont headingFont=heading->property("font").value<QFont>();
            const QString resolved=QFontInfo(headingFont).family();
            if(!headingFont.family().contains(QStringLiteral("Inter"),Qt::CaseInsensitive) ||
               !resolved.contains(QStringLiteral("Inter"),Qt::CaseInsensitive) ||
               !app.font().family().contains(QStringLiteral("Inter"),Qt::CaseInsensitive)) {
                qCritical()<<"R2 font did not actually render using Inter"
                           <<headingFont.family()<<resolved<<app.font().family();
                app.exit(1056);return;
            }
            const auto rect=[](QQuickItem *item) {
                return item->mapRectToScene(QRectF(0,0,item->width(),item->height()));
            };
            const QRectF win(0,0,size.width(),size.height());
            for(auto *item:{heading,subtitle,metrics,hero,readiness,disclosure}) {
                if(!item->isVisible() || !win.contains(rect(item))) {
                    qCritical()<<"R2 Inter caused QML clipping"
                               <<item->objectName()<<rect(item);
                    app.exit(1057);return;
                }
            }
            if(rect(metrics).bottom()>rect(hero).top()+2 ||
               rect(hero).right()>rect(readiness).left()+2) {
                qCritical()<<"R2 Inter lost the approved KPI/hero layout";
                app.exit(1058);return;
            }
            const QString dimensions=QString::number(size.width())+"x"+
                                     QString::number(size.height());
            const QImage shot=window->grabWindow();
            if(shot.isNull() || shot.size()!=size ||
               !shot.save("pa-shell-001-inter-font-"+dimensions+".png","PNG")) {
                qCritical()<<"R2 Inter native screenshot failed";
                app.exit(1059);return;
            }
            QByteArray vf,rf;
            if(!readFile(vehicles,&vf)||!readFile(rentals,&rf)||
               vf!=beforeVehicles||rf!=beforeRentals) {
                qCritical()<<"R2 Inter trial changed protected fictional data";
                app.exit(1060);return;
            }
            qInfo()<<"R2 Inter installed, used by Qt and source unchanged PASS"
                   <<resolved<<dimensions;
            app.exit(0);
        });
    }
    if (parser.isSet("pa-overview-002-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window) return 1020;
        const bool compact=parser.isSet("compact");
        const QSize expected(compact?1280:1500,compact?720:860);
        window->resize(expected);
        window->setProperty("workspace",0);
        QTimer::singleShot(1350,&app,[&,window,compact,expected] {
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name)return item;
                for(auto *child:item->childItems())
                    if(auto *hit=self(self,child,name))return hit;
                return nullptr;
            };
            const auto get=[&](const QString &name)->QQuickItem* {
                if(auto *item=find(find,window->contentItem(),name))return item;
                return window->findChild<QQuickItem*>(name);
            };
            auto *total=get("overviewTotalCard");
            auto *availableCard=get("overviewAvailableCard");
            auto *metrics=get("overviewMetricGrid");
            auto *active=get("overviewActiveCard");
            auto *donut=get("overviewAvailabilityPanel");
            auto *ring=get("overviewAvailabilityRing");
            auto *percent=get("overviewAvailablePercent");
            auto *value=get("overviewValueCard");
            auto *due=get("overviewDueTodayText");
            auto *history=get("overviewHistoryText");
            auto *recent=get("overviewRecentPanel");
            auto *row=get("overviewRecentRow0");
            auto *activeAction=get("overviewOpenActive");
            auto *heroFleetAction=get("overviewOpenFleet");
            auto *fleetSearch=get("fleetSearchField");
            auto *activeChevron=get("overviewActiveChevron");
            auto *rentalActiveFilter=get("rentalFilterActive");
            auto *vehicleName=get("overviewRecentVehicleName0");
            auto *review=get("overviewReviewOverdue");
            auto *heroPhotoBox=get("overviewHeroPhotoBox");
            auto *heroPhoto=get("overviewHeroPhoto");
            auto *readyChevron=get("overviewReadyChevron");
            auto *readyFleetAction=get("overviewReadyFleetAction");
            auto *readyFleetActionIcon=get("overviewReadyFleetActionChevron");
            auto *fleetStatus=get("fleetStatusCombo");
            auto *disclosure=get("overviewDemoDisclosure");
            if(!total||!availableCard||!metrics||!active||!donut||!ring||
               !percent||!value||!due||!history||!recent||!row||
               !activeAction||!heroFleetAction||!fleetSearch||
               !activeChevron||!rentalActiveFilter||
               !vehicleName||!review||!heroPhotoBox||!heroPhoto||
               !readyChevron||!readyFleetAction||!readyFleetActionIcon||
               !fleetStatus||!disclosure) {
                qCritical()<<"PA Overview controls or real history row missing";
                app.exit(1021);return;
            }
            const QString expectedPercent=QString::number(
                fleet.totalCount()>0
                  ? qRound(100.0*fleet.availableCount()/fleet.totalCount())
                  : 0)+QStringLiteral("%");
            if(total->property("value").toString()!=QString::number(fleet.totalCount()) ||
               availableCard->property("value").toString()!=
                    QString::number(fleet.availableCount()) ||
               !availableCard->property("note").toString().contains(
                    fleet.totalCount()>0
                      ? QString::number(qRound(100.0*fleet.availableCount()/fleet.totalCount()))+"%"
                      : QStringLiteral("No fleet")) ||
               active->property("value").toString()!=QString::number(rentalModel.activeCount()) ||
               donut->property("total").toInt()!=fleet.totalCount() ||
               donut->property("available").toInt()!=fleet.availableCount() ||
               donut->property("rented").toInt()!=fleet.rentedCount() ||
               percent->property("text").toString()!=expectedPercent ||
               value->property("value").toString()!=
                    QStringLiteral("R %1").arg(rentalModel.activeBookedValue(),0,'f',2) ||
               !row->property("text").toString().contains("R001") ||
               vehicleName->property("text").toString()!=
                   fleet.vehicleNames().value("C002").toString() ||
               vehicleName->property("text").toString().isEmpty() ||
               (!disclosure->property("text").toString().contains("contract totals") ||
                 !disclosure->property("text").toString().contains("not collected payments"))) {
                qCritical()<<"PA Overview data mismatch"<<expectedPercent
                           <<percent->property("text")<<donut->property("rented");
                app.exit(1022);return;
            }
            // The actual 1280 and 1500 desktop compositions must preserve
            // both side-by-side rows and the source-backed vehicle teaser.
            auto *hero=get("editorialOverviewHero");
            auto *ready=get("overviewReadyPanel");
            auto *readyVehicle=get("overviewReadyVehicle0");
            auto *fleetGallery=get("fleetGallery");
            if(!hero||!ready||!readyVehicle||!fleetGallery||
               fleet.availableHighlights().isEmpty()||
               readyVehicle->property("vehicleId").toString()!=
                 fleet.availableHighlights().first().toMap().value("vehicleId").toString()) {
                qCritical()<<"Direction A preview not backed by actual C++ fleet";
                app.exit(1027);return;
            }
            const auto availabilityTopLeft=donut->mapToScene(QPointF(0,0));
            const auto recentTopLeft=recent->mapToScene(QPointF(0,0));
            const auto heroTopLeft=hero->mapToScene(QPointF(0,0));
            const auto metricsTopLeft=metrics->mapToScene(QPointF(0,0));
            const auto availableTopLeft=availableCard->mapToScene(QPointF(0,0));
            // R2 composition: KPI strip must precede and fit above the hero.
            if(metrics->width()<800 || metricsTopLeft.y()>=heroTopLeft.y() ||
               availableTopLeft.y()<metricsTopLeft.y()-2 ||
               availableCard->width()<170 ||
               availableTopLeft.x()+availableCard->width()>window->width()-5 ||
               hero->property("color").value<QColor>()!=QColor("#072A28")) {
                qCritical()<<"R2 Overview KPI/hero layout or palette regression";
                app.exit(1028);return;
            }
            const auto readyTopLeft=ready->mapToScene(QPointF(0,0));
            // R2 reference specifies a 65/35 hero/readiness composition,
            // preserved across both real native desktop target resolutions.
            const qreal heroShare=hero->width()/(hero->width()+donut->width());
            const QRectF heroPhotoBounds=heroPhotoBox->mapRectToScene(
                QRectF(0,0,heroPhotoBox->width(),heroPhotoBox->height()));
            const QRectF heroBounds=hero->mapRectToScene(
                QRectF(0,0,hero->width(),hero->height()));
            if(qAbs(heroShare-0.65)>0.045 ||
               !heroBounds.contains(heroPhotoBounds) ||
               heroPhotoBox->width()<(compact?180:250) ||
               !readyChevron->property("assetReady").toBool() ||
               readyChevron->property("iconName").toString()!="chevron-right" ||
               !activeChevron->property("assetReady").toBool() ||
               activeChevron->property("iconName").toString()!="chevron-right" ||
               !activeAction->isVisible() || activeAction->height()<43 ||
               !recent->mapRectToScene(QRectF(0,0,recent->width(),recent->height()))
                   .contains(activeAction->mapRectToScene(
                       QRectF(0,0,activeAction->width(),activeAction->height()))) ||
               !readyFleetActionIcon->property("assetReady").toBool() ||
               readyFleetActionIcon->property("iconName").toString()!="chevron-right" ||
               !readyFleetAction->isVisible() || !readyFleetAction->isEnabled() ||
               readyFleetAction->height()<43 ||
               !ready->mapRectToScene(QRectF(0,0,ready->width(),ready->height()))
                    .contains(readyFleetAction->mapRectToScene(
                        QRectF(0,0,readyFleetAction->width(),readyFleetAction->height()))) ||
               hero->property("featuredVehicleId").toString()!="C002" ||
               !heroPhoto->property("source").toString().contains("C002")) {
                qCritical()<<"R2 Overview 65/35 split/photo/right-vector regression"
                           <<heroShare<<heroPhotoBounds<<heroBounds
                           <<hero->property("featuredVehicleId");
                app.exit(1029);return;
            }
            if(donut->width()<290||recent->width()<440||
               hero->width()<430||ready->width()<290||
               heroTopLeft.x()<0||heroTopLeft.y()<0||
               availabilityTopLeft.x()<heroTopLeft.x()+hero->width()-2||
               recentTopLeft.x()<0||recentTopLeft.y()<heroTopLeft.y()||
               readyTopLeft.x()<recentTopLeft.x()+recent->width()-2||
               readyTopLeft.x()+ready->width()>window->width()-5) {
                qCritical()<<"Direction A panels too narrow or clipped"
                           <<hero->width()<<donut->width()<<recent->width()
                           <<ready->width();
                app.exit(1026);return;
            }
            // Existing compact P5 tests require immediate visibility of these
            // controls rather than accidental placement below the fold.
            for(QQuickItem *item:{review,disclosure}) {
                const auto pos=item->mapToScene(QPointF(0,0));
                if(!item->isVisible() || pos.y()<0 ||
                   pos.y()+item->height()>window->height()-2) {
                    qCritical()<<"PA Overview safety action/disclosure clipped"
                               <<item->objectName()<<pos;
                    app.exit(1023);return;
                }
            }
            const QString size=compact?QStringLiteral("1280x720"):
                QStringLiteral("1500x860");
            const QImage shot=window->grabWindow();
            if(shot.isNull() || shot.size()!=expected ||
               !shot.save(QStringLiteral("pa-overview-002-%1.png").arg(size),"PNG")) {
                qCritical()<<"PA Overview screenshot failed";
                app.exit(1024);return;
            }
            QByteArray vehicleSource,rentalSource;
            if(!readFile(vehicles,&vehicleSource)||
               !readFile(rentals,&rentalSource)||
               vehicleSource!=beforeVehicles||rentalSource!=beforeRentals) {
                qCritical()<<"PA Overview mutated protected demo fixtures";
                app.exit(1025);return;
            }
            // UX-OV1: the visible See Fleet CTA is a real keyboard action,
            // not a decorative label. Verify routing, visible focus arrival
            // and unchanged protected fictional sources.
            readyFleetAction->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QCoreApplication::processEvents();
            QTest::qWait(90);
            QByteArray routedFleet,routedRentals;
            if(window->property("workspace").toInt()!=1 ||
               !window->property("gallery").toBool() ||
               !fleetStatus->hasActiveFocus() ||
               !readFile(vehicles,&routedFleet) ||
               !readFile(rentals,&routedRentals) ||
               routedFleet!=beforeVehicles || routedRentals!=beforeRentals) {
                qCritical()<<"UX-OV1 See Fleet route, focus or source integrity failed"
                           <<window->property("workspace")
                           <<fleetStatus->hasActiveFocus();
                app.exit(1094);return;
            }
            // UX-OV2: real Active rental route must restore keyboard focus
            // on the actual C++-filtered Rentals status control.
            window->setProperty("workspace",0);
            activeAction->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QCoreApplication::processEvents();
            QTest::qWait(90);
            QByteArray activeFleet,activeRentals;
            if(window->property("workspace").toInt()!=2 ||
               rentalModel.statusFilter()!="Active" ||
               !rentalActiveFilter->hasActiveFocus() ||
               !readFile(vehicles,&activeFleet) ||
               !readFile(rentals,&activeRentals) ||
               activeFleet!=beforeVehicles || activeRentals!=beforeRentals) {
                qCritical()<<"UX-OV2 Active route/focus or source integrity failed"
                           <<window->property("workspace")
                           <<rentalModel.statusFilter()
                           <<rentalActiveFilter->hasActiveFocus();
                app.exit(1095);return;
            }
            // UX-OV3: opening the actual C++ available vehicle from a
            // source-backed Overview preview must focus its Fleet gallery
            // selection, not leave keyboard focus inside a hidden workspace.
            window->setProperty("workspace",0);
            QCoreApplication::processEvents();
            const QString previewVehicleId=readyVehicle->property("vehicleId").toString();
            readyVehicle->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QCoreApplication::processEvents();
            QTest::qWait(100);
            QByteArray previewFleet,previewRentals;
            if(window->property("workspace").toInt()!=1 ||
               !window->property("gallery").toBool() ||
               previewVehicleId.isEmpty() ||
               fleet.selectedId()!=previewVehicleId ||
               fleet.selectedVisibleIndex()<0 ||
               !fleetGallery->isVisible() ||
               !fleetGallery->hasActiveFocus() ||
               fleetGallery->property("currentIndex").toInt()!=
                   fleet.selectedVisibleIndex() ||
               !readFile(vehicles,&previewFleet) ||
               !readFile(rentals,&previewRentals) ||
               previewFleet!=beforeVehicles || previewRentals!=beforeRentals) {
                qCritical()<<"UX-OV3 Overview preview gallery focus/identity/safety failed"
                           <<window->property("workspace")
                           <<fleet.selectedId()<<previewVehicleId
                           <<fleetGallery->hasActiveFocus()
                           <<fleetGallery->property("currentIndex")
                           <<fleet.selectedVisibleIndex();
                app.exit(1096);return;
            }
            // UX-OV4: the hero's Browse fleet button must land on Fleet search,
            // not leave focus on an invisible Overview action. It must neither
            // reset Gallery/Table mode nor clear an existing C++ selection.
            const bool browsePreviousGallery=window->property("gallery").toBool();
            const QString browseSelectedId=fleet.selectedId();
            window->setProperty("workspace",0);
            QCoreApplication::processEvents();
            heroFleetAction->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QCoreApplication::processEvents();
            QTest::qWait(100);
            QByteArray browseFleet,browseRentals;
            if(window->property("workspace").toInt()!=1 ||
               window->property("gallery").toBool()!=browsePreviousGallery ||
               fleet.selectedId()!=browseSelectedId ||
               !fleetSearch->isVisible() ||
               !fleetSearch->hasActiveFocus() ||
               !readFile(vehicles,&browseFleet) ||
               !readFile(rentals,&browseRentals) ||
               browseFleet!=beforeVehicles || browseRentals!=beforeRentals) {
                qCritical()<<"UX-OV4 Browse fleet focus, mode, selection or files failed"
                           <<window->property("workspace")
                           <<fleetSearch->hasActiveFocus()
                           <<fleet.selectedId()<<browseSelectedId;
                app.exit(1097);return;
            }
            qInfo()<<"R2 Overview selected vehicle and Browse fleet focus routes PASS"
                   <<size<<"authoritative C++ IDs and unchanged fictional files";
            app.exit(0);
        });
    }
    if (parser.isSet("pa-shell-001-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window) return 1001;
        const bool compact=parser.isSet("compact");
        const QSize expected(compact?1280:1500,compact?720:860);
        window->resize(expected);
        window->setProperty("workspace",0);
        QTimer::singleShot(1250,&app,[&,window,compact,expected] {
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name)return item;
                for(auto *child:item->childItems())
                    if(auto *match=self(self,child,name))return match;
                return nullptr;
            };
            const auto get=[&](const QString &name)->QQuickItem* {
                if(auto *item=find(find,window->contentItem(),name))return item;
                return window->findChild<QQuickItem*>(name);
            };
            auto *newRental=get("sidebarNewRentalButton");
            auto *logoActionIcon=get("sidebarNewRentalIcon");
            auto *toggle=get("sidebarToggleButton");
            auto *sidebar=get("appSidebar");
            auto *overviewNav=get("workspaceNavOverview");
            auto *overviewIcon=get("workspaceIconOverview");
            auto *fleetIcon=get("workspaceIconFleet");
            auto *rentalsIcon=get("workspaceIconRentals");
            auto *decorativeWave=get("editorialSidebarWave");
            auto *brandMark=get("editorialBrandMark");
            auto *clockCard=get("editorialSidebarClock");
            auto *clockHour=get("editorialSidebarClockHour");
            auto *clockMinute=get("editorialSidebarClockMinute");
            auto *clockDate=get("editorialSidebarClockDate");
            auto *sessionLabel=get("sessionModeSidebar");
            auto *heading=get("workspacePageHeading");
            auto *subtitle=get("workspacePageSubtitle");
            auto *bookingField=get("bookingVehicleId");
            auto *dialog=window->findChild<QObject*>("rentalBookingDialog");
            if(!newRental||!logoActionIcon||!toggle||!sidebar||
               !overviewNav||!overviewIcon||!fleetIcon||!rentalsIcon||
               !decorativeWave||!brandMark||!clockCard||!clockHour||
               !clockMinute||!clockDate||!sessionLabel||
               !heading||!subtitle||!bookingField||!dialog||
               !newRental->isVisible()||!logoActionIcon->isVisible()||
               newRental->property("text").toString()!="New rental"||
               heading->property("text").toString()!="Rental <font color='#0F766E'>Operations.</font>"||
               sidebar->width()<210) {
                qCritical()<<"Precision Automotive initial real Qt shell missing";
                app.exit(1002);return;
            }
            // Real native Qt geometry and colour assertions prevent the
            // reference's charcoal shell and white canvas from degenerating
            // into all-dark or earlier all-light variants.
            if(sidebar->property("color").value<QColor>()!=QColor("#151B1B") ||
               window->color()!=QColor("#F7F8FA") ||
               !overviewNav->property("currentWorkspace").toBool() ||
               !decorativeWave->isVisible()) {
                qCritical()<<"Editorial Contrast hybrid shell or selected workspace absent";
                app.exit(1012);return;
            }
            // Real Qt must decode the locally packaged vectors at both sizes.
            for(QQuickItem *icon : {overviewIcon,fleetIcon,rentalsIcon,logoActionIcon}) {
                if(!icon->property("assetReady").toBool()) {
                    qCritical()<<"R2 Lucide SVG could not decode"
                               <<icon->objectName()<<icon->property("assetUrl");
                    app.exit(1014);return;
                }
            }
            if(overviewIcon->property("iconName").toString()!="layout-grid" ||
               fleetIcon->property("iconName").toString()!="car-front" ||
               rentalsIcon->property("iconName").toString()!="clipboard-list" ||
               logoActionIcon->property("iconName").toString()!="plus") {
                qCritical()<<"R2 Lucide icon mapping regression";
                app.exit(1015);return;
            }
            // Real Qt Linux originally clipped the toggle behind oversized
            // brand text, despite 140/140 green CI. Reject it geometrically.
            const auto sceneRect=[](QQuickItem *item) {
                return item->mapRectToScene(
                    QRectF(0,0,item->width(),item->height()));
            };
            const QRectF shellBounds=sceneRect(sidebar);
            if(!shellBounds.contains(sceneRect(toggle)) ||
               !shellBounds.contains(sceneRect(brandMark)) ||
               !shellBounds.contains(sceneRect(newRental)) ||
               sceneRect(brandMark).intersects(sceneRect(toggle))) {
                qCritical()<<"Editorial Contrast sidebar branding clips the toggle or action";
                app.exit(1013);return;
            }
            // R2-CLK1: real Qt at both sizes. No mocked time or business data.
            bool validHour=false, validMinute=false;
            const int hour=clockHour->property("text").toString().toInt(&validHour);
            const int minute=clockMinute->property("text").toString().toInt(&validMinute);
            if(!clockCard->isVisible() || !clockDate->isVisible() ||
               !validHour || hour<0 || hour>23 ||
               !validMinute || minute<0 || minute>59 ||
               clockDate->property("text").toString().isEmpty() ||
               !shellBounds.contains(sceneRect(clockCard)) ||
               sceneRect(clockCard).bottom()>sceneRect(sessionLabel).top()) {
                qCritical()<<"R2 display-only sidebar clock missing or overlapping";
                app.exit(1016);return;
            }
            // R2-CLK2: emulate a clock label left two minutes stale, then
            // allow the REAL running QML timer to resynchronize it. This
            // verifies boundary catch-up without changing the OS time or
            // exercising any C++ booking clock or persisted rental data.
            const QDateTime stale=QDateTime::currentDateTime().addSecs(-120);
            if(!clockCard->setProperty("displayNow",stale)) {
                qCritical()<<"R2 clock stale-time test seam unavailable";
                app.exit(1018);return;
            }
            QCoreApplication::processEvents();
            const QDateTime beforeTick=clockCard->property("displayNow").toDateTime();
            if(!beforeTick.isValid() ||
               beforeTick.secsTo(QDateTime::currentDateTime())<110) {
                qCritical()<<"R2 local display did not accept stale test state";
                app.exit(1019);return;
            }
            QTest::qWait(1450); // One real 1000ms QML timer tick.
            const QDateTime afterTick=clockCard->property("displayNow").toDateTime();
            // R2-CLK2-F: a test hitting XX:59.999 -> (XX+1):00 must not
            // compare the current OS minute with an earlier display sample.
            // Require a fresh (within 6s) real timer sample and verify both
            // visible labels against THAT SAME sampled QML date.
            const QDateTime wallSample=QDateTime::currentDateTime();
            if(!afterTick.isValid() ||
               std::abs(afterTick.secsTo(wallSample))>6 ||
               clockHour->property("text").toString()!=afterTick.toString("hh") ||
               clockMinute->property("text").toString()!=afterTick.toString("mm")) {
                qCritical()<<"R2 clock failed real minute resynchronization"
                           <<beforeTick<<afterTick;
                app.exit(1020);return;
            }
            QByteArray initialFleet,initialRentals;
            if(!readFile(vehicles,&initialFleet)||!readFile(rentals,&initialRentals)||
               initialFleet!=beforeVehicles || initialRentals!=beforeRentals) {
                qCritical()<<"Precision Automotive initial protected fixtures changed";
                app.exit(1003);return;
            }
            const auto capture=[&](const QString &state)->bool {
                const QString size=compact?QStringLiteral("1280x720"):
                                           QStringLiteral("1500x860");
                const QImage screenshot=window->grabWindow();
                return !screenshot.isNull() && screenshot.size()==expected &&
                    screenshot.save(QStringLiteral("pa-shell-001-%1-%2.png")
                        .arg(state,size),"PNG");
            };
            if(!capture("overview-expanded")) {
                qCritical()<<"Precision Automotive Overview capture failed";
                app.exit(1004);return;
            }
            newRental->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QTest::qWait(160);
            if(window->property("workspace").toInt()!=2 ||
               !dialog->property("visible").toBool() ||
               !bookingField->hasActiveFocus() ||
               bookingActions.canConfirm()) {
                qCritical()<<"Sidebar shortcut did not open real safe booking flow";
                app.exit(1005);return;
            }
            if(!capture("real-booking-dialog")) {
                qCritical()<<"Precision Automotive booking capture failed";
                app.exit(1006);return;
            }
            QTest::keyClick(window,Qt::Key_Escape);
            QTest::qWait(150);
            if(dialog->property("visible").toBool() ||
               !newRental->hasActiveFocus() || bookingActions.canConfirm()) {
                qCritical()<<"Sidebar booking Escape did not restore invoker";
                app.exit(1007);return;
            }
            toggle->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            // Full source-visible state is instant, but ordinary-mode cosmetic
            // sidebar width settles over 220ms. Await its real final geometry.
            QTest::qWait(280);
            if(!window->property("sidebarCollapsed").toBool() ||
               sidebar->width()>82 || !newRental->isVisible() ||
               !logoActionIcon->isVisible()) {
                qCritical()<<"Precision Automotive icon-only New Rental missing";
                app.exit(1008);return;
            }
            if(clockCard->isVisible() || clockDate->isVisible()) {
                qCritical()<<"R2 display clock visible in collapsed sidebar";
                app.exit(1017);return;
            }
            window->setProperty("workspace",1);
            QCoreApplication::processEvents();
            if(!capture("fleet-collapsed")) {
                qCritical()<<"Precision Automotive collapsed Fleet capture failed";
                app.exit(1009);return;
            }
            newRental->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QTest::qWait(160);
            if(window->property("workspace").toInt()!=2 ||
               !dialog->property("visible").toBool() ||
               !bookingField->hasActiveFocus()) {
                qCritical()<<"Collapsed New Rental did not open real dialog";
                app.exit(1010);return;
            }
            QTest::keyClick(window,Qt::Key_Escape);
            QTest::qWait(150);
            QByteArray finalFleet,finalRentals;
            if(dialog->property("visible").toBool() ||
               !newRental->hasActiveFocus() || bookingActions.canConfirm() ||
               !readFile(vehicles,&finalFleet)||!readFile(rentals,&finalRentals)||
               finalFleet!=initialFleet || finalRentals!=initialRentals) {
                qCritical()<<"Precision Automotive shortcut changed protected data/focus";
                app.exit(1011);return;
            }
            qInfo()<<"Editorial Contrast real Qt hybrid shell PASS"
                   <<(compact?"1280x720":"1500x860")
                   <<"Overview/Fleet, expanded/collapsed New Rental, safe Escape";
            app.exit(0);
        });
    }
    if (parser.isSet("ux-shell-004-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window) return 980;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",1);
        QTimer::singleShot(1250,&app,[&,window,compact] {
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name)return item;
                for(auto *child:item->childItems())
                    if(auto *hit=self(self,child,name))return hit;
                return nullptr;
            };
            auto *root=window->contentItem();
            const auto get=[&](const QString &name)->QQuickItem* {
                if(auto *item=find(find,root,name))return item;
                return window->findChild<QQuickItem*>(name);
            };
            auto *sidebar=get("appSidebar");
            auto *toggle=get("sidebarToggleButton");
            auto *fleetNav=get("workspaceNavFleet");
            auto *overviewNav=get("workspaceNavOverview");
            auto *fleetIcon=get("workspaceIconFleet");
            auto *search=get("fleetSearchField");
            auto *status=get("fleetStatusCombo");
            auto *sort=get("fleetSortCombo");
            if(!sidebar||!toggle||!fleetNav||!overviewNav||
               !fleetIcon||!search||!status||!sort ||
               !fleetIcon->isVisible() ||
               window->property("sidebarCollapsed").toBool() ||
               sidebar->width()<210) {
                qCritical()<<"UX-SHELL-004 expanded UI/accessible icons missing";
                app.exit(981);return;
            }
            const auto key=[&](Qt::Key k) {
                QTest::keyClick(window,k);
                QCoreApplication::processEvents();
            };
            const auto capture=[&](const QString &mode)->bool {
                const QString size=compact?"1280x720":"1500x860";
                const QImage shot=window->grabWindow();
                return !shot.isNull() &&
                       shot.size()==QSize(compact?1280:1500,compact?720:860) &&
                       shot.save("ux-shell-004-"+mode+"-"+size+".png","PNG");
            };
            QByteArray originalFleet,originalRentals;
            if(!readFile(vehicles,&originalFleet)||
               !readFile(rentals,&originalRentals)||
               originalFleet!=beforeVehicles||
               originalRentals!=beforeRentals) {
                qCritical()<<"UX-SHELL-004 fixture baseline mismatch";
                app.exit(982);return;
            }
            if(!capture("expanded")) {
                qCritical()<<"UX-SHELL-004 expanded screenshot invalid";
                app.exit(983);return;
            }
            toggle->forceActiveFocus(Qt::TabFocusReason);
            key(Qt::Key_Space);
            QTest::qWait(280);
            if(!window->property("sidebarCollapsed").toBool() ||
               sidebar->width()>82 || !fleetIcon->isVisible() ||
               window->property("workspace").toInt()!=1 ||
               toggle->property("text").toString()!="Expand sidebar") {
                qCritical()<<"UX-SHELL-004 compact navigation regression"
                           <<sidebar->width()<<toggle->property("text");
                app.exit(984);return;
            }
            if(!capture("collapsed")) {
                qCritical()<<"UX-SHELL-004 collapsed screenshot invalid";
                app.exit(985);return;
            }
            overviewNav->forceActiveFocus(Qt::TabFocusReason);
            key(Qt::Key_Down);
            if(!fleetNav->hasActiveFocus()||
               window->property("workspace").toInt()!=1) {
                qCritical()<<"UX-SHELL-004 compact sidebar focus changed workspace";
                app.exit(986);return;
            }
            key(Qt::Key_Right);
            if(!search->hasActiveFocus()) {
                qCritical()<<"UX-SHELL-004 icon nav Right missed Fleet Search";
                app.exit(987);return;
            }
            toggle->forceActiveFocus(Qt::TabFocusReason);
            key(Qt::Key_Space);
            QTest::qWait(280);
            if(window->property("sidebarCollapsed").toBool() ||
               sidebar->width()<210 ||
               toggle->property("text").toString()!="Collapse sidebar") {
                qCritical()<<"UX-SHELL-004 expanded sidebar restoration failed";
                app.exit(988);return;
            }
            // Real Qt keyboard: All -> Name -> Search (visible left-to-right),
            // then reverse toward All, without modifying filter/sort values.
            status->forceActiveFocus(Qt::TabFocusReason);
            key(Qt::Key_Right);
            if(!sort->hasActiveFocus() ||
               status->property("currentText").toString()!="All") {
                qCritical()<<"UX-SHELL-004 All Right failed or changed filter";
                app.exit(989);return;
            }
            key(Qt::Key_Right);
            if(!search->hasActiveFocus() ||
               sort->property("currentText").toString()!="Name") {
                qCritical()<<"UX-SHELL-004 Name Right failed or changed sort";
                app.exit(990);return;
            }
            search->setProperty("text","");
            search->setProperty("cursorPosition",0);
            key(Qt::Key_Left);
            if(!sort->hasActiveFocus()) {
                qCritical()<<"UX-SHELL-004 empty Search Left did not focus Name";
                app.exit(991);return;
            }
            key(Qt::Key_Left);
            if(!status->hasActiveFocus()) {
                qCritical()<<"UX-SHELL-004 Name Left did not focus All";
                app.exit(992);return;
            }
            key(Qt::Key_Right);
            key(Qt::Key_Right);
            search->setProperty("text","AB");
            search->setProperty("cursorPosition",1);
            key(Qt::Key_Left);
            if(!search->hasActiveFocus()||
               search->property("cursorPosition").toInt()!=0) {
                qCritical()<<"UX-SHELL-004 text caret Left incorrectly moved focus";
                app.exit(993);return;
            }
            search->setProperty("text","");
            key(Qt::Key_Down);
            auto *gallery=get("fleetGalleryToggle");
            if(!gallery||!gallery->hasActiveFocus()) {
                qCritical()<<"UX-SHELL-004 Search Down missed Gallery";
                app.exit(994);return;
            }
            QByteArray finalFleet,finalRentals;
            if(!readFile(vehicles,&finalFleet)||
               !readFile(rentals,&finalRentals)||
               finalFleet!=originalFleet||finalRentals!=originalRentals) {
                qCritical()<<"UX-SHELL-004 focus or collapse changed protected files";
                app.exit(995);return;
            }
            qInfo()<<"UX-SHELL-004 icon sidebar collapse/expand, Fleet filter arrows, native caret, and no writes PASS"
                   <<(compact?"1280x720":"1500x860");
            app.exit(0);
        });
    }
    if (parser.isSet("ux-ctrl-003-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window) return 920;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",2);
        QTimer::singleShot(1300,&app,[&,window,compact] {
            const auto findItem=[](const auto &self,QQuickItem *item,
                                   const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name)return item;
                for(auto *child:item->childItems())
                    if(auto *hit=self(self,child,name))return hit;
                return nullptr;
            };
            const auto get=[&](const QString &name)->QQuickItem* {
                if(auto *item=findItem(findItem,window->contentItem(),name))
                    return item;
                return window->findChild<QQuickItem*>(name);
            };
            auto *all=get("rentalFilterAll");
            auto *active=get("rentalFilterActive");
            auto *overdue=get("rentalFilterOverdue");
            auto *returnField=get("returnLookupField");
            auto *newBooking=get("sidebarNewRentalButton");
            auto *findField=get("rentalLookupField");
            auto *records=get("rentalList");
            auto *details=get("rentalDetailsButton");
            auto *fleetSearch=get("fleetSearchField");
            auto *galleryChoice=get("fleetGalleryToggle");
            auto *tableChoice=get("fleetTableToggle");
            auto *gallery=get("fleetGallery");
            auto *table=get("fleetTable");
            auto *overviewAction=get("overviewOpenActive");
            auto *overviewRow=get("overviewRecentRow0");
            if(!all||!active||!overdue||!returnField||!newBooking||
               !findField||!records||!details||!fleetSearch||
               !galleryChoice||!tableChoice||!gallery||!table||
               !overviewAction||!overviewRow) {
                qCritical()<<"UX-CTRL-003 native QML focus objects missing";
                app.exit(921);return;
            }
            const auto key=[&](Qt::Key k) {
                QTest::keyClick(window,k);
                QCoreApplication::processEvents();
            };
            QByteArray originalFleet,originalRentals;
            if(!readFile(vehicles,&originalFleet) ||
               !readFile(rentals,&originalRentals) ||
               originalFleet!=beforeVehicles || originalRentals!=beforeRentals) {
                qCritical()<<"UX-CTRL-003 initial fictional source mismatch";
                app.exit(922);return;
            }
            all->forceActiveFocus(Qt::TabFocusReason);
            key(Qt::Key_Up);
            if(!returnField->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-003 All Up did not focus Return lookup";
                app.exit(923);return;
            }
            key(Qt::Key_Up);
            if(!newBooking->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-003 Return lookup Up missed available actions";
                app.exit(924);return;
            }
            key(Qt::Key_Down);
            if(!returnField->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-003 top action Down missed Return lookup";
                app.exit(925);return;
            }
            key(Qt::Key_Down);
            if(!all->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-003 Return lookup Down failed status memory";
                app.exit(926);return;
            }
            key(Qt::Key_Right);
            if(!active->hasActiveFocus() || rentalModel.statusFilter()!="All") {
                qCritical()<<"UX-CTRL-003 status arrow silently changed filter";
                app.exit(927);return;
            }
            key(Qt::Key_Up);key(Qt::Key_Down);
            if(!active->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-003 status focus memory missing";
                app.exit(928);return;
            }
            key(Qt::Key_Left);key(Qt::Key_Down);
            if(!overdue->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-003 All Down missed Overdue";
                app.exit(929);return;
            }
            key(Qt::Key_Up);key(Qt::Key_Down);
            if(!overdue->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-003 status and return timing not reversible";
                app.exit(930);return;
            }
            key(Qt::Key_Down);
            if(!findField->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-003 timing Down missed Find rental";
                app.exit(931);return;
            }
            key(Qt::Key_Up);key(Qt::Key_Down);
            if(!findField->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-003 Find Up/Down not reversible";
                app.exit(932);return;
            }
            key(Qt::Key_Down);
            if(!records->hasActiveFocus() ||
               rentalModel.selectedRentalId()!="R001") {
                qCritical()<<"UX-CTRL-003 Find Down missed first rental R001";
                app.exit(933);return;
            }
            key(Qt::Key_Up);
            if(!findField->hasActiveFocus() ||
               rentalModel.selectedRentalId()!="R001") {
                qCritical()<<"UX-CTRL-003 first rental Up missed Find";
                app.exit(934);return;
            }
            key(Qt::Key_Down);key(Qt::Key_Down);
            if(!records->hasActiveFocus() ||
               rentalModel.selectedRentalId()!="R002") {
                qCritical()<<"UX-CTRL-003 rental row movement missed R002";
                app.exit(935);return;
            }
            key(Qt::Key_Up);
            if(!records->hasActiveFocus() ||
               rentalModel.selectedRentalId()!="R001") {
                qCritical()<<"UX-CTRL-003 internal rental Up failed";
                app.exit(936);return;
            }
            key(Qt::Key_Down);key(Qt::Key_Down);
            if(!details->hasActiveFocus() ||
               rentalModel.selectedRentalId()!="R002") {
                qCritical()<<"UX-CTRL-003 last rental Down missed View details";
                app.exit(937);return;
            }
            key(Qt::Key_Up);
            if(!records->hasActiveFocus() ||
               rentalModel.selectedRentalId()!="R002") {
                qCritical()<<"UX-CTRL-003 View details Up failed to restore R002";
                app.exit(938);return;
            }
            key(Qt::Key_Return);
            auto *dialog=window->findChild<QObject*>("rentalContractDetailsDialog");
            if(!dialog || !dialog->property("visible").toBool()) {
                qCritical()<<"UX-CTRL-003 Enter did not open read-only contract";
                app.exit(939);return;
            }
            key(Qt::Key_Escape);
            QTest::qWait(90);
            if(dialog->property("visible").toBool() ||
               !records->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-003 Escape did not restore record focus";
                app.exit(940);return;
            }
            window->setProperty("workspace",1);
            fleet.clearSelection();
            fleetSearch->forceActiveFocus(Qt::TabFocusReason);
            key(Qt::Key_Down);
            if(!galleryChoice->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-003 Search Down missed Gallery";
                app.exit(941);return;
            }
            key(Qt::Key_Up);key(Qt::Key_Down);
            if(!galleryChoice->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-003 Search and Gallery not reversible";
                app.exit(942);return;
            }
            key(Qt::Key_Down);
            if(!gallery->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-003 Gallery Down missed records";
                app.exit(943);return;
            }
            key(Qt::Key_Up);
            if(!galleryChoice->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-003 top Gallery row Up missed choice";
                app.exit(944);return;
            }
            key(Qt::Key_Down);key(Qt::Key_Return);
            if(!gallery->hasActiveFocus() || fleet.selectedId()!="C002") {
                qCritical()<<"UX-CTRL-003 Fleet Enter did not inspect Ford";
                app.exit(945);return;
            }
            key(Qt::Key_Up);key(Qt::Key_Down);
            if(!gallery->hasActiveFocus() || fleet.selectedId()!="C002") {
                qCritical()<<"UX-CTRL-003 Gallery reentry lost Ford C002";
                app.exit(946);return;
            }
            key(Qt::Key_Down);
            if(!gallery->hasActiveFocus() || fleet.selectedId()=="C002") {
                qCritical()<<"UX-CTRL-003 Gallery inner Down did not change vehicle";
                app.exit(947);return;
            }
            key(Qt::Key_Up);
            if(!gallery->hasActiveFocus() || fleet.selectedId()!="C002") {
                qCritical()<<"UX-CTRL-003 Gallery inner Up lost Ford";
                app.exit(948);return;
            }
            tableChoice->forceActiveFocus(Qt::TabFocusReason);
            key(Qt::Key_Return);
            if(!table->hasActiveFocus() || fleet.selectedId()!="C002") {
                qCritical()<<"UX-CTRL-003 Table Enter failed to preserve Ford";
                app.exit(949);return;
            }
            key(Qt::Key_Up);
            if(!tableChoice->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-003 Table first row Up missed view choice";
                app.exit(950);return;
            }
            key(Qt::Key_Down);
            if(!table->hasActiveFocus() || fleet.selectedId()!="C002") {
                qCritical()<<"UX-CTRL-003 Table Down did not restore Ford";
                app.exit(951);return;
            }
            window->setProperty("workspace",0);
            overviewAction->forceActiveFocus(Qt::TabFocusReason);
            key(Qt::Key_Down);
            if(!overviewRow->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-003 Overview Down missed recent contracts";
                app.exit(952);return;
            }
            key(Qt::Key_Up);
            if(!overviewAction->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-003 recent Up missed quick actions";
                app.exit(953);return;
            }
            QByteArray afterFleet,afterRentals;
            if(!readFile(vehicles,&afterFleet) ||
               !readFile(rentals,&afterRentals) ||
               afterFleet!=originalFleet || afterRentals!=originalRentals) {
                qCritical()<<"UX-CTRL-003 navigation wrote protected fictional files";
                app.exit(954);return;
            }
            qInfo()<<"UX-CTRL-003 real Qt reversible focus and file integrity PASS"
                   <<(compact?"1280x720":"1500x860");
            app.exit(0);
        });
    }
    if (parser.isSet("ux-ctrl-002-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window) return 880;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",0);
        QTimer::singleShot(1200,&app,[&,window,compact] {
            // Repeater delegates may be visual children rather than QObject
            // children. Also inspect initially hidden StackLayout pages:
            // the route itself makes them visible after Enter navigation.
            const auto findItem=[](const auto &self,QQuickItem *item,
                                   const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name)return item;
                for(auto *child:item->childItems())
                    if(auto *hit=self(self,child,name))return hit;
                return nullptr;
            };
            const auto get=[&](const QString &name) -> QQuickItem* {
                if(auto *item=findItem(findItem,window->contentItem(),name))
                    return item;
                return window->findChild<QQuickItem*>(name);
            };
            auto *overviewNav=get("workspaceNavOverview");
            auto *fleetNav=get("workspaceNavFleet");
            auto *rentalsNav=get("workspaceNavRentals");
            auto *search=get("fleetSearchField");
            auto *active=get("overviewOpenActive");
            auto *fleetAction=get("overviewOpenFleet");
            auto *overdue=get("overviewReviewOverdue");
            auto *filterAll=get("rentalFilterAll");
            auto *filterActive=get("rentalFilterActive");
            auto *filterOverdue=get("rentalFilterOverdue");
            auto *lookup=get("rentalLookupField");
            auto *list=get("rentalList");
            if(!overviewNav||!fleetNav||!rentalsNav||!search||!active||
               !fleetAction||!overdue||!filterAll||!filterActive||
               !filterOverdue||!lookup||!list) {
                qCritical()<<"UX-CTRL-002 missing keyboard targets"
                           <<"overview"<<(overviewNav!=nullptr)
                           <<"fleet"<<(fleetNav!=nullptr)
                           <<"rentals"<<(rentalsNav!=nullptr)
                           <<"search"<<(search!=nullptr)
                           <<"actions"<<(active!=nullptr)<< (fleetAction!=nullptr) <<(overdue!=nullptr)
                           <<"filters"<<(filterAll!=nullptr)<<(filterActive!=nullptr)
                                      <<(filterOverdue!=nullptr)
                           <<"lookup/list"<<(lookup!=nullptr)<<(list!=nullptr);
                app.exit(881);return;
            }
            const auto key=[&](Qt::Key k) {
                QTest::keyClick(window,k);
                QCoreApplication::processEvents();
            };
            QByteArray originalFleet,originalRentals;
            if(!readFile(vehicles,&originalFleet)||
               !readFile(rentals,&originalRentals)||
               originalFleet!=beforeVehicles || originalRentals!=beforeRentals) {
                qCritical()<<"UX-CTRL-002 source fiction data invalid";
                app.exit(882);return;
            }
            overviewNav->forceActiveFocus(Qt::TabFocusReason);
            key(Qt::Key_Down);
            if(!fleetNav->hasActiveFocus() ||
               window->property("workspace").toInt()!=0) {
                qCritical()<<"UX-CTRL-002 Down changed page or missed Fleet";
                app.exit(883);return;
            }
            key(Qt::Key_Return);
            if(window->property("workspace").toInt()!=1) {
                qCritical()<<"UX-CTRL-002 Enter did not open Fleet";
                app.exit(884);return;
            }
            fleetNav->forceActiveFocus(Qt::TabFocusReason);
            key(Qt::Key_Right);
            if(!search->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-002 Fleet Right did not focus search";
                app.exit(885);return;
            }
            search->setProperty("text","AB");
            search->setProperty("cursorPosition",0);
            key(Qt::Key_Right);
            if(!search->hasActiveFocus() ||
               search->property("cursorPosition").toInt()!=1) {
                qCritical()<<"UX-CTRL-002 Right stole native search caret";
                app.exit(886);return;
            }
            search->setProperty("text","");
            key(Qt::Key_Down);
            auto *galleryChoice=get("fleetGalleryToggle");
            if(!galleryChoice || !galleryChoice->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-003 Fleet Search Down missed Gallery choice";
                app.exit(900);return;
            }
            key(Qt::Key_Down);
            auto *gallery=get("fleetGallery");
            if(!gallery || !gallery->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-002 Gallery choice Down missed collection";
                app.exit(900);return;
            }
            key(Qt::Key_Return);
            if(fleet.selectedId().isEmpty()) {
                qCritical()<<"UX-CTRL-002 Fleet Enter did not inspect a vehicle";
                app.exit(901);return;
            }
            overviewNav->forceActiveFocus(Qt::TabFocusReason);
            key(Qt::Key_Return);
            key(Qt::Key_Right);
            if(window->property("workspace").toInt()!=0 ||
               !active->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-002 Overview right missed first action";
                app.exit(887);return;
            }
            key(Qt::Key_Right);
            if(!fleetAction->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-002 overview second quick action missing";
                app.exit(888);return;
            }
            key(Qt::Key_Right);
            if(!overdue->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-002 overview third quick action missing";
                app.exit(889);return;
            }
            key(Qt::Key_Left);key(Qt::Key_Left);key(Qt::Key_Left);
            if(!overviewNav->hasActiveFocus() ||
               window->property("workspace").toInt()!=0) {
                qCritical()<<"UX-CTRL-002 Overview Left did not reach sidebar";
                app.exit(890);return;
            }
            rentalsNav->forceActiveFocus(Qt::TabFocusReason);
            key(Qt::Key_Return);
            key(Qt::Key_Right);
            if(window->property("workspace").toInt()!=2 ||
               !filterAll->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-002 Rentals Right missed All";
                app.exit(891);return;
            }
            key(Qt::Key_Right);
            if(!filterActive->hasActiveFocus() ||
               rentalModel.statusFilter()!="All") {
                qCritical()<<"UX-CTRL-002 status arrow changed filter";
                app.exit(892);return;
            }
            key(Qt::Key_Left);
            key(Qt::Key_Down);
            if(!filterOverdue->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-002 All Down missed Overdue";
                app.exit(893);return;
            }
            key(Qt::Key_Down);
            if(!lookup->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-002 Overdue Down missed Find";
                app.exit(894);return;
            }
            key(Qt::Key_Down);
            if(!list->hasActiveFocus() ||
               rentalModel.selectedRentalId()!="R001") {
                qCritical()<<"UX-CTRL-002 Find Down missed first rental";
                app.exit(895);return;
            }
            key(Qt::Key_Return);
            auto *dialog=window->findChild<QObject*>("rentalContractDetailsDialog");
            auto *id=get("rentalDetailIdentity");
            auto *total=get("rentalDetailTotal");
            auto *panel=get("rentalDetailTotalPanel");
            auto *close=get("rentalDetailsClose");
            if(!dialog||!dialog->property("visible").toBool()||
               !id||id->property("text").toString()!="R001" ||
               !total||total->property("text").toString()!="R 1,400.00"||
               !panel||panel->property("color").value<QColor>()!=QColor("#E8F5F0")||
               !close||!close->hasActiveFocus()) {
                qCritical()<<"UX-CTRL-002 rental Enter or layout failed"
                           <<(dialog?dialog->property("visible"):QVariant{})
                           <<(id?id->property("text"):QVariant{})
                           <<(total?total->property("text"):QVariant{});
                app.exit(896);return;
            }
            key(Qt::Key_Escape);
            QTest::qWait(90);
            if(dialog->property("visible").toBool() ||
               !list->hasActiveFocus() ||
               rentalModel.selectedRentalId()!="R001") {
                qCritical()<<"UX-CTRL-002 modal Escape did not restore list";
                app.exit(897);return;
            }
            window->setProperty("workspace",1);
            fleet.selectById("C003");
            QCoreApplication::processEvents();
            auto *remove=get("fleetRemoveSelectedButton");
            auto *background=remove ? remove->property("background").value<QQuickItem*>() : nullptr;
            if(!remove||!remove->isEnabled()||!background||
               background->property("color").value<QColor>()!=QColor("#B42318")) {
                qCritical()<<"UX-CTRL-002 destructive action not red";
                app.exit(898);return;
            }
            QByteArray finalFleet,finalRentals;
            if(!readFile(vehicles,&finalFleet) ||
               !readFile(rentals,&finalRentals) ||
               finalFleet!=originalFleet || finalRentals!=originalRentals) {
                qCritical()<<"UX-CTRL-002 keyboard navigation wrote to source";
                app.exit(899);return;
            }
            qInfo()<<"UX-CTRL-002 real Qt focus, contract and destructive colors PASS"
                   <<(compact?"1280x720":"1500x860");
            app.exit(0);
        });
    }
    if (parser.isSet("p6-contract-detail-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window)return 560;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",2);
        QTimer::singleShot(1200,&app,[&,window,compact] {
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name && item->isVisible())return item;
                for(auto *child:item->childItems())
                    if(auto *hit=self(self,child,name))return hit;
                return nullptr;
            };
            auto *root=window->contentItem();
            auto *button=find(find,root,"rentalDetailsButton");
            if(!button || button->isEnabled() ||
               !rentalModel.selectedContract().isEmpty()) {
                qCritical()<<"P6-K no-selection detail button not safely disabled";
                app.exit(561);return;
            }
            QByteArray fleetBefore,rentalsBefore;
            if(!readFile(disposable.filePath("vehicles.txt"),&fleetBefore)||
               !readFile(disposable.filePath("rentals.txt"),&rentalsBefore)||
               fleetBefore!=beforeVehicles||rentalsBefore!=beforeRentals) {
                qCritical()<<"P6-K source fixtures unexpectedly modified";
                app.exit(562);return;
            }
            rentalModel.setStatusFilter("History");
            if(!rentalModel.locateById("r001")||
               rentalModel.selectedRentalId()!="R001" ||
               rentalModel.statusFilter()!="All" ||
               !button->isEnabled()) {
                qCritical()<<"P6-K hidden active detail navigation not selected";
                app.exit(563);return;
            }
            button->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QCoreApplication::processEvents();
            QTest::qWait(80);
            // Qt Quick Dialog is a QQuickPopup QObject, not a QQuickItem.
            // Find its QObject by name as the existing P3 return smoke does.
            auto *dialog=window->findChild<QObject*>("rentalContractDetailsDialog");
            auto *start=find(find,root,"rentalDetailStart");
            auto *expected=find(find,root,"rentalDetailExpected");
            auto *actual=find(find,root,"rentalDetailActual");
            auto *total=find(find,root,"rentalDetailTotal");
            auto *status=find(find,root,"rentalDetailStatus");
            auto *close=find(find,root,"rentalDetailsClose");
            if(!dialog||!start||!expected||!actual||!total||!status||!close||
               start->property("text").toString()!="2026-10-28" ||
               expected->property("text").toString()!="2026-10-30" ||
               actual->property("text").toString()!="Not returned"||
               total->property("text").toString()!="R 1,400.00"||
               !status->property("text").toString().contains("Active") ||
               !close->hasActiveFocus()) {
                qCritical()<<"P6-K active modal dates/rate/keyboard focus missing"
                           <<"dialog"<<dialog<<"start"<<start<<"expected"<<expected
                           <<"actual"<<actual<<"total"<<total<<"status"<<status
                           <<"close"<<close
                           <<"startText"<<(start?start->property("text").toString():QString{})
                           <<"expectedText"<<(expected?expected->property("text").toString():QString{})
                           <<"actualText"<<(actual?actual->property("text").toString():QString{})
                           <<"totalText"<<(total?total->property("text").toString():QString{})
                           <<"statusText"<<(status?status->property("text").toString():QString{})
                           <<"focused"<<(close?close->hasActiveFocus():false);
                app.exit(564);return;
            }
            // UX-VIS-001: the actual popup must fit its window, preserve
            // the approved two-column card order, and reveal the Close action
            // without clipping. A full-window capture below retains proof
            // while the modal is OPEN, unlike background Rentals captures.
            auto *identity=find(find,root,"rentalDetailIdentity");
            auto *vehicle=find(find,root,"rentalDetailVehicle");
            auto *grid=find(find,root,"rentalDetailGrid");
            auto *panel=find(find,root,"rentalDetailTotalPanel");
            auto *duration=find(find,root,"rentalDetailDuration");
            auto *rate=find(find,root,"rentalDetailRate");
            // UX-VIS-002: the accessible Dialog retains its title, but Qt's
            // redundant default visible title bar must not precede the custom
            // RENTAL CONTRACT header. Check the real popup component.
            // The standard find() helper deliberately excludes hidden items.
            // Inspect Dialog.header directly so the regression is meaningful.
            auto *chromeHeader=dialog->property("header").value<QQuickItem*>();
            if(!chromeHeader ||
               chromeHeader->objectName()!="rentalContractSuppressedDefaultHeader" ||
               chromeHeader->isVisible() ||
               dialog->property("title").toString()!="Rental contract") {
                qCritical()<<"UX-VIS-002 duplicate contract title chrome not suppressed"
                           <<"header"<<chromeHeader
                           <<"visible"<<(chromeHeader?chromeHeader->isVisible():true)
                           <<"semantic title"<<dialog->property("title");
                app.exit(573);return;
            }
            const auto fitsWindow=[window](QQuickItem *item)->bool {
                if(!item || !item->isVisible() || item->width()<=0 ||
                   item->height()<=0)return false;
                const QPointF a=item->mapToScene(QPointF(0,0));
                const QPointF b=item->mapToScene(QPointF(item->width(),item->height()));
                return a.x()>=-1 && a.y()>=-1 &&
                       b.x()<=window->width()+1 && b.y()<=window->height()+1;
            };
            if(!identity||!vehicle||!grid||!panel||!duration||!rate||
               !fitsWindow(identity)||!fitsWindow(vehicle)||
               !fitsWindow(grid)||!fitsWindow(panel)||
               !fitsWindow(start)||!fitsWindow(expected)||
               !fitsWindow(actual)||!fitsWindow(total)||
               !fitsWindow(close)||
               grid->property("columns").toInt()!=2 ||
               identity->property("text").toString()!="R001"||
               vehicle->property("text").toString()!="Vehicle C002"||
               duration->property("text").toString()!="2 days"||
               rate->property("text").toString()!="R 700.00"||
               panel->property("color").value<QColor>()!=QColor("#E8F5F0")||
               std::abs(start->mapToScene(QPointF(0,0)).y()-
                        expected->mapToScene(QPointF(0,0)).y())>8.0 ||
               !(actual->mapToScene(QPointF(0,0)).y() >
                 start->mapToScene(QPointF(0,0)).y()) ||
               !(total->mapToScene(QPointF(0,0)).y() >
                 actual->mapToScene(QPointF(0,0)).y()) ||
               !(close->mapToScene(QPointF(0,0)).y() >
                 total->mapToScene(QPointF(0,0)).y())) {
                qCritical()<<"UX-VIS-001 real contract dialog geometry or card order failed"
                           <<"size"<<window->size()
                           <<"grid columns"<<(grid?grid->property("columns"):QVariant{})
                           <<"panel"<<(panel?panel->property("color"):QVariant{})
                           <<"identity"<<(identity?identity->property("text"):QVariant{});
                app.exit(572);return;
            }
            const QString size=compact?"1280x720":"1500x860";
            const QImage shot=window->grabWindow();
            if(shot.isNull()||shot.size()!=QSize(compact?1280:1500,compact?720:860)||
               !shot.save("p6-qt-contract-detail-"+size+".png","PNG")) {
                qCritical()<<"P6-K native detail capture failed";
                app.exit(565);return;
            }
            QTest::keyClick(window,Qt::Key_Escape);
            QCoreApplication::processEvents();
            QTest::qWait(75);
            if(dialog->property("visible").toBool() || !button->hasActiveFocus() ||
               rentalModel.selectedRentalId()!="R001") {
                qCritical()<<"P6-K Escape/focus recovery altered selection";
                app.exit(566);return;
            }
            rentalModel.setStatusFilter("Returned");
            if(rentalModel.selectedRentalId()!="" || button->isEnabled() ||
               !rentalModel.selectedContract().isEmpty()) {
                qCritical()<<"P6-K filter did not disarm hidden selected details";
                app.exit(567);return;
            }
            if(!rentalModel.selectById("R002")||!button->isEnabled()) {
                qCritical()<<"P6-K historical contract not selectable";
                app.exit(568);return;
            }
            button->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QCoreApplication::processEvents();
            QTest::qWait(75);
            actual=find(find,root,"rentalDetailActual");
            status=find(find,root,"rentalDetailStatus");
            if(!actual||!status ||
               actual->property("text").toString()!="2026-10-22"||
               !status->property("text").toString().contains("Returned")) {
                qCritical()<<"P6-K returned contract actual date/status incorrect";
                app.exit(569);return;
            }
            close=find(find,root,"rentalDetailsClose");
            if(!close) {app.exit(570);return;}
            close->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QCoreApplication::processEvents();
            QTest::qWait(75);
            QByteArray afterFleet,afterRental;
            if(dialog->property("visible").toBool() || !button->hasActiveFocus() ||
               !readFile(disposable.filePath("vehicles.txt"),&afterFleet)||
               !readFile(disposable.filePath("rentals.txt"),&afterRental)||
               afterFleet!=fleetBefore||afterRental!=rentalsBefore ||
               rentalModel.activeCount()!=1||rentalModel.historyCount()!=1) {
                qCritical()<<"P6-K detail opened/closed with domain mutation";
                app.exit(571);return;
            }
            qInfo()<<"P6-K native purpose-limited contract detail PASS"<<size;
            app.exit(0);
        });
    }
    if (parser.isSet("p6-recent-activity-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window) return 550;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",0);
        QTimer::singleShot(1150,&app,[&,window,compact] {
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name && item->isVisible())return item;
                for(auto *child:item->childItems())
                    if(auto *match=self(self,child,name))return match;
                return nullptr;
            };
            auto *root=window->contentItem();
            auto *scroll=find(find,root,"overviewScroll");
            auto *panel=find(find,root,"overviewRecentPanel");
            auto *first=find(find,root,"overviewRecentRow0");
            auto *second=find(find,root,"overviewRecentRow1");
            auto *disclosure=find(find,root,"overviewDemoDisclosure");
            const auto rows=rentalModel.recentActivities();
            if(!scroll||!panel||!first||!second||!disclosure||
               rows.size()!=2 ||
               rows.at(0).toMap().value("rentalId").toString()!="R001" ||
               rows.at(1).toMap().value("rentalId").toString()!="R002" ||
               rentalModel.activeCount()!=1||rentalModel.historyCount()!=1 ||
               first->property("text").toString().contains("Demo Customer") ||
               first->property("text").toString().contains("0000000000")) {
                qCritical()<<"P6-J recent contracts missing, unsorted or unsafe";
                app.exit(551);return;
            }
            const QPointF disclosurePos=disclosure->mapToScene(QPointF(0,0));
            if(disclosurePos.y()<0 ||
               disclosurePos.y()+disclosure->height()>window->height()-2) {
                qCritical()<<"P6-J Overview existing payment disclosure clipped";
                app.exit(552);return;
            }
            rentalModel.setStatusFilter("Archived");
            if(rentalModel.count()!=0 || rentalModel.recentActivities().size()!=2 ||
               !first->property("text").toString().contains("R001") ||
               rentalModel.activeCount()!=1) {
                qCritical()<<"P6-J recent contracts incorrectly tied to filter";
                app.exit(553);return;
            }
            QByteArray nowVehicles,nowRentals;
            if(!readFile(disposable.filePath("vehicles.txt"),&nowVehicles) ||
               !readFile(disposable.filePath("rentals.txt"),&nowRentals) ||
               nowVehicles!=beforeVehicles || nowRentals!=beforeRentals) {
                qCritical()<<"P6-J read-only panel changed disposable or protected inputs";
                app.exit(554);return;
            }
            // Scroll the actual viewport, then keyboard-activate a record
            // that was hidden by the current Rentals status filter.
            scroll->setProperty("contentY",scroll->property("contentHeight").toDouble());
            QCoreApplication::processEvents();
            QTest::qWait(90);
            const QPointF pos=second->mapToScene(QPointF(0,0));
            if(pos.y()<0 || pos.y()+second->height()>window->height()-2 ||
               pos.x()<0 || pos.x()+second->width()>window->width()) {
                qCritical()<<"P6-J recent row not reachable after native scroll"<<pos;
                app.exit(555);return;
            }
            const QString size=compact?"1280x720":"1500x860";
            const QImage shot=window->grabWindow();
            if(shot.isNull()||shot.size()!=QSize(compact?1280:1500,compact?720:860)||
               !shot.save("p6-qt-recent-contracts-"+size+".png","PNG")) {
                qCritical()<<"P6-J real Overview capture failed";
                app.exit(556);return;
            }
            second->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QCoreApplication::processEvents();
            QTest::qWait(90);
            QByteArray afterVehicles,afterRentals;
            if(window->property("workspace").toInt()!=2 ||
               rentalModel.selectedRentalId()!="R002" ||
               rentalModel.statusFilter()!="All" ||
               !readFile(disposable.filePath("vehicles.txt"),&afterVehicles)||
               !readFile(disposable.filePath("rentals.txt"),&afterRentals)||
               afterVehicles!=nowVehicles || afterRentals!=nowRentals ||
               rentalModel.activeCount()!=1||rentalModel.historyCount()!=1) {
                qCritical()<<"P6-J readonly recent contract navigation broke state";
                app.exit(557);return;
            }
            qInfo()<<"P6-J native read-only bounded recent rental routes PASS"<<size;
            app.exit(0);
        });
    }
    if (parser.isSet("p5-overview-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window) return 290;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",0);
        QTimer::singleShot(1100,&app,[&,window,compact] {
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name && item->isVisible())return item;
                for(auto *child:item->childItems())
                    if(auto *hit=self(self,child,name))return hit;
                return nullptr;
            };
            auto *root=window->contentItem();
            auto *activeCard=find(find,root,"overviewActiveCard");
            auto *valueCard=find(find,root,"overviewValueCard");
            auto *dueCard=find(find,root,"overviewDueTodayText");
            auto *historyCard=find(find,root,"overviewHistoryText");
            auto *activeAction=find(find,root,"overviewOpenActive");
            auto *fleetAction=find(find,root,"overviewOpenFleet");
            auto *overdueAction=find(find,root,"overviewReviewOverdue");
            auto *overdueText=find(find,root,"overviewOldestOverdueText");
            auto *disclosure=find(find,root,"overviewDemoDisclosure");
            if(!activeCard||!valueCard||!dueCard||!historyCard||
               !activeAction||!fleetAction||!overdueAction||!overdueText ||
               !disclosure || !overdueAction->isEnabled() ||
               rentalModel.oldestOverdueRentalId()!="R001" ||
               overdueText->property("text").toString()!="Oldest overdue contract: R001" ||
               rentalModel.activeCount()!=1 || rentalModel.historyCount()!=1 ||
               std::abs(rentalModel.activeBookedValue()-1400.0)>0.01 ||
               fleet.availableCount()!=4 ||
               activeCard->property("value").toString()!="1" ||
               valueCard->property("value").toString()!="R 1400.00") {
                qCritical()<<"P5 operational Overview initial C++ data missing";
                app.exit(291);return;
            }
            // The compact 1280x720 view must keep the actionable row AND
            // the explicit fictional/payment disclosure within the window.
            for(QQuickItem *item:{overdueAction,disclosure}) {
                const QPointF pos=item->mapToScene(QPointF(0,0));
                if(!item->isVisible() || pos.y()<0 ||
                   pos.y()+item->height()>window->height()-3 ||
                   pos.x()<0 || pos.x()+item->width()>window->width()) {
                    qCritical()<<"P5 Overview compact action or disclosure clipped"
                               <<item->objectName()<<pos<<item->size();
                    app.exit(385);return;
                }
            }
            const QString size=compact?"1280x720":"1500x860";
            const QImage screenshot=window->grabWindow();
            if(screenshot.isNull() ||
               screenshot.size()!=QSize(compact?1280:1500,compact?720:860) ||
               !screenshot.save("p5-qt-operational-overview-"+size+".png","PNG")) {
                qCritical()<<"P5 real Overview screenshot failed";
                app.exit(292);return;
            }
            // Presentation-only filter must NEVER change aggregate totals.
            rentalModel.setStatusFilter("History");
            if(rentalModel.count()!=1 || rentalModel.activeCount()!=1 ||
               rentalModel.historyCount()!=1 ||
               std::abs(rentalModel.activeBookedValue()-1400.0)>0.01) {
                qCritical()<<"P5 Overview KPIs incorrectly scoped to visible filter";
                app.exit(293);return;
            }
            // A keyboard action on Overview must navigate to and select the
            // authoritative oldest overdue contract, even from History filter.
            overdueAction->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QCoreApplication::processEvents();
            if(window->property("workspace").toInt()!=2 ||
               rentalModel.statusFilter()!="Active" ||
               rentalModel.selectedRentalId()!="R001" ||
               rentalModel.selectedRow()!=0 ||
               rentalModel.activeCount()!=1) {
                qCritical()<<"P5 actionable overdue navigation failed";
                app.exit(384);return;
            }
            window->setProperty("workspace",0);
            rentalModel.setStatusFilter("All");
            rentalModel.clearSelection();
            // Keep the original reviewed quote for a genuine second-booking
            // refusal against a newly constructed domain after disk reload.
            const auto firstQuote=RentalQuoteService(system).preview("C001",3);
            if(!firstQuote.ok()) {
                qCritical()<<"P5 original Corolla quote missing";
                app.exit(305);return;
            }
            // The existing C++ quote + confirmation service is the only
            // writer. Committed records update Fleet and Overview through
            // the already-established post-commit refresh callback.
            if(!bookingActions.preview("C001",3) ||
               !bookingActions.confirm("Fictional Demo Overview","0000000000")) {
                qCritical()<<"P5 real C++ booked Corolla flow failed"
                           <<bookingActions.lastMessage();
                app.exit(294);return;
            }
            QCoreApplication::processEvents();
            if(rentalModel.activeCount()!=2 || rentalModel.historyCount()!=1 ||
               std::abs(rentalModel.activeBookedValue()-2750.0)>0.01 ||
               fleet.availableCount()!=3 ||
               activeCard->property("value").toString()!="2" ||
               valueCard->property("value").toString()!="R 2750.00") {
                qCritical()<<"P5 Overview did not update after durable booking"
                           <<rentalModel.activeCount()<<rentalModel.activeBookedValue()
                           <<valueCard->property("value");
                app.exit(295);return;
            }
            if(!rentalModel.selectById("R001") || !rentalActions.returnSelected()) {
                qCritical()<<"P5 real domain return failed";
                app.exit(296);return;
            }
            QCoreApplication::processEvents();
            if(rentalModel.activeCount()!=1 || rentalModel.historyCount()!=2 ||
               std::abs(rentalModel.activeBookedValue()-1350.0)>0.01 ||
               fleet.availableCount()!=4 ||
               activeCard->property("value").toString()!="1" ||
               valueCard->property("value").toString()!="R 1350.00" ||
               historyCard->property("text").toString()!="Returned or archived: 2" ||
               rentalModel.oldestOverdueRentalId()!="" ||
               overdueAction->isEnabled()) {
                qCritical()<<"P5 Overview did not reconcile committed return";
                app.exit(297);return;
            }
            activeAction->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QCoreApplication::processEvents();
            if(window->property("workspace").toInt()!=2 ||
               rentalModel.statusFilter()!="Active" ||
               rentalModel.count()!=1 ||
               rentalModel.activeCount()!=1) {
                qCritical()<<"P5 Overview-to-Active action changed metrics or navigation";
                app.exit(298);return;
            }
            window->setProperty("workspace",0);
            QCoreApplication::processEvents();
            fleetAction->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QCoreApplication::processEvents();
            if(window->property("workspace").toInt()!=1) {
                qCritical()<<"P5 Overview-to-Fleet action failed";
                app.exit(299);return;
            }
            QByteArray originalVehicles,originalRentals,afterLedger;
            if(!readFile(vehicles,&originalVehicles) ||
               !readFile(rentals,&originalRentals) ||
               !readFile(disposable.filePath("rentals.txt"),&afterLedger) ||
               originalVehicles!=beforeVehicles ||
               originalRentals!=beforeRentals ||
               afterLedger==beforeRentals) {
                qCritical()<<"P5 persisted disposable change/original-file safety failed";
                app.exit(300);return;
            }
            // Recreate the C++ rental domain and models from actual on-disk
            // disposable QSaveFile records. The public preview intentionally
            // does NOT persist its temp directory across application launches.
            RentalSystem reopened(disposable.path());
            RentalReadService reopenedRead(reopened);
            RentalListModel reopenedModel(reopenedRead);
            const auto *persistedCorolla=reopened.findVehicleById("C001");
            const auto *persistedRanger=reopened.findVehicleById("C002");
            const auto *persistedBooking=reopened.findRentalById("R003");
            const auto *persistedReturn=reopened.findRentalById("R001");
            if(!persistedCorolla || !persistedRanger ||
               !persistedBooking || !persistedReturn ||
               !persistedCorolla->getIsRented() ||
               persistedRanger->getIsRented() ||
               persistedBooking->getIsReturned() ||
               !persistedReturn->getIsReturned() ||
               reopenedModel.count()!=3 ||
               reopenedModel.activeCount()!=1 ||
               reopenedModel.historyCount()!=2 ||
               std::abs(reopenedModel.activeBookedValue()-1350.0)>0.01) {
                qCritical()<<"P5 disk-reloaded C++ domain/model state incorrect";
                app.exit(306);return;
            }
            // Same reviewed Corolla quote must not create a duplicate.
            const RentalConfirmationRequest again{
                *firstQuote.quote,QStringLiteral("Fictional Demo Duplicate"),
                QStringLiteral("0000000000")};
            const auto rejected=RentalConfirmService(reopened).confirm(again);
            QByteArray afterDuplicate;
            if(rejected.error!=RentalConfirmationError::VehicleUnavailable ||
               rejected.receipt.has_value() ||
               !readFile(disposable.filePath("rentals.txt"),&afterDuplicate) ||
               afterDuplicate!=afterLedger ||
               reopenedModel.activeCount()!=1 ||
               !persistedCorolla->getIsRented()) {
                qCritical()<<"P5 repeated booking damaged saved ledger or occupancy";
                app.exit(307);return;
            }
            qInfo()<<"P5 operational overview/reconstruction PASS"<<size
                   <<"C++ committed booking/return, disk rehydration, duplicate refusal, safe demo";
            app.exit(0);
        });
    }

    if(p6JourneyChild) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window)return 480;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",p6JourneySeed?1:(p6JourneyFinish?2:0));
        QTimer::singleShot(1250,&app,[&,window,compact] {
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name && item->isVisible())return item;
                for(auto *child:item->childItems())
                    if(auto *x=self(self,child,name))return x;
                return nullptr;
            };
            const auto press=[&](QQuickItem *target)->bool {
                if(!target||!target->isVisible()||!target->isEnabled())return false;
                target->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window,Qt::Key_Space);
                QCoreApplication::processEvents();
                QTest::qWait(155);
                return true;
            };
            auto *root=window->contentItem();
            const QString vehicleLedger=QDir(dataDirectory).filePath("vehicles.txt");
            const QString rentalLedger=QDir(dataDirectory).filePath("rentals.txt");
            QByteArray initialV,initialR;
            if(!readFile(vehicleLedger,&initialV)||
               !readFile(rentalLedger,&initialR)) {app.exit(481);return;}
            if(p6JourneySeed) {
                if(fleet.totalCount()!=5||fleet.availableCount()!=4 ||
                   rentalModel.activeCount()!=1||rentalModel.historyCount()!=1 ||
                   !press(find(find,root,"fleetAddButton"))) {
                    qCritical()<<"P6-M seed initial Fleet/add unavailable";
                    app.exit(482);return;
                }
                auto *brand=find(find,root,"fleetAddBrand");
                auto *model=find(find,root,"fleetAddModel");
                auto *rate=find(find,root,"fleetAddRate");
                auto *confirm=find(find,root,"fleetAddConfirm");
                if(!brand||!model||!rate||!confirm) {app.exit(483);return;}
                brand->setProperty("text",QStringLiteral("Demo Motors"));
                model->setProperty("text",QStringLiteral("Orion"));
                rate->setProperty("text",QStringLiteral("420"));
                if(!press(confirm)||fleet.selectedId()!="C004"||
                   fleet.totalCount()!=6||fleet.availableCount()!=5||
                   !press(find(find,root,"fleetBookSelectedButton"))) {
                    qCritical()<<"P6-M seed Fleet Car/create-to-book handoff failed";
                    app.exit(484);return;
                }
                auto *days=find(find,root,"bookingDays");
                auto *vehicle=find(find,root,"bookingVehicleId");
                auto *preview=find(find,root,"bookingPreviewButton");
                auto *customer=find(find,root,"bookingCustomer");
                auto *phone=find(find,root,"bookingPhone");
                auto *save=find(find,root,"bookingConfirmButton");
                if(!days||!vehicle||!preview||!customer||!phone||!save||
                   vehicle->property("text").toString()!="C004") {
                    qCritical()<<"P6-M seeded vehicle was not prefilled";
                    app.exit(485);return;
                }
                days->setProperty("value",2);
                if(!press(preview)||!save->isEnabled()||
                   !bookingActions.quoteSummary().contains("Total R 840.00")) {
                    qCritical()<<"P6-M authoritative QML quote absent";
                    app.exit(486);return;
                }
                customer->setProperty("text",QStringLiteral("Fictional Journey"));
                phone->setProperty("text",QStringLiteral("0000000000"));
                if(!press(save)||bookingActions.lastRentalId()!="R003"||
                   rentalModel.activeCount()!=2||fleet.availableCount()!=4||
                   !system.findRentalById("R003")||
                   !system.findVehicleById("C004")->getIsRented() ||
                   bookingActions.preview("C004",2) ||
                   bookingActions.canConfirm()) {
                    qCritical()<<"P6-M QML booking/duplicate refusal failed";
                    app.exit(487);return;
                }
                QByteArray v,r;
                if(!readFile(vehicleLedger,&v)||!readFile(rentalLedger,&r)||
                   v==initialV||r==initialR) {
                    qCritical()<<"P6-M new contract and occupancy not durable";
                    app.exit(488);return;
                }
                qInfo()<<"P6-M process 1 UI create + quote + confirm + reject duplicate PASS";
                app.exit(0);return;
            }
            if(p6JourneyFinish) {
                if(fleet.totalCount()!=6||fleet.availableCount()!=4||
                   rentalModel.activeCount()!=2||
                   !system.findRentalById("R003")||
                   !system.findVehicleById("C004")->getIsRented()) {
                    qCritical()<<"P6-M process 2 did not reconstruct confirmed booking";
                    app.exit(489);return;
                }
                auto *lookup=find(find,root,"returnLookupField");
                auto *locate=find(find,root,"returnLookupButton");
                if(!lookup||!locate) {app.exit(490);return;}
                lookup->setProperty("text",QStringLiteral("c004"));
                if(!press(locate)||rentalModel.selectedRentalId()!="R003" ||
                   rentalModel.statusFilter()!="Active" ||
                   !press(find(find,root,"rentalReturnButton")) ||
                   !press(find(find,root,"rentalReturnConfirm"))||
                   !system.findRentalById("R003")->getIsReturned()||
                   fleet.availableCount()!=5 || rentalModel.activeCount()!=1) {
                    qCritical()<<"P6-M process 2 QML return/occupancy failed";
                    app.exit(491);return;
                }
                // The active-only return lookup intentionally hides returned
                // records after commit. Reveal the now-historical ID using the
                // existing read-only, privacy-safe generic rental lookup.
                if(!rentalModel.locateById("R003")) {
                    qCritical()<<"P6-M historical record not found after return";
                    app.exit(492);return;
                }
                const QVariantMap returned=rentalModel.selectedContract();
                if(returned.value("rentalId").toString()!="R003" ||
                   returned.value("status").toString()!="Returned" ||
                   returned.value("actualReturnDate").toString()!=
                       QDate::currentDate().toString(Qt::ISODate) ||
                   fleetRemove.canRemoveVehicle("C004")) {
                    qCritical()<<"P6-M returned rental detail/reference guard missing";
                    app.exit(492);return;
                }
                if(!press(find(find,root,"rentalArchiveButton")) ||
                   !press(find(find,root,"rentalArchiveConfirm")) ||
                   !system.findRentalById("R003")->getIsArchived()) {
                    qCritical()<<"P6-M process 2 archive failed";
                    app.exit(493);return;
                }
                if(!press(find(find,root,"rentalDeleteArchivedButton"))) {
                    qCritical()<<"P6-M archive deletion action absent";
                    app.exit(494);return;
                }
                auto *phrase=find(find,root,"rentalDeletePhrase");
                auto *deleteButton=find(find,root,"rentalDeleteConfirm");
                if(!phrase||!deleteButton) {app.exit(495);return;}
                phrase->setProperty("text",QStringLiteral("DELETE R001"));
                QByteArray beforeReject,afterReject;
                if(!readFile(rentalLedger,&beforeReject)) {app.exit(496);return;}
                press(deleteButton); // invalid target remains protected
                if(!readFile(rentalLedger,&afterReject)||afterReject!=beforeReject ||
                   !system.findRentalById("R003")) {
                    qCritical()<<"P6-M invalid historical deletion mutated data";
                    app.exit(497);return;
                }
                phrase->setProperty("text",QStringLiteral("DELETE R003"));
                if(!press(deleteButton)||system.findRentalById("R003")||
                   rentalModel.historyCount()!=1 ||
                   !fleetRemove.canRemoveVehicle("C004")) {
                    qCritical()<<"P6-M archived deletion or history release failed";
                    app.exit(498);return;
                }
                window->setProperty("workspace",1);
                QTest::qWait(185);
                fleet.selectById("C004");
                QCoreApplication::processEvents();
                if(!press(find(find,root,"fleetRemoveSelectedButton"))) {
                    qCritical()<<"P6-M unreferenced Fleet delete disabled";
                    app.exit(499);return;
                }
                auto *fleetPhrase=find(find,root,"fleetRemoveConfirmation");
                auto *fleetConfirm=find(find,root,"fleetRemoveConfirm");
                if(!fleetPhrase||!fleetConfirm) {app.exit(500);return;}
                fleetPhrase->setProperty("text",QStringLiteral("REMOVE C004"));
                if(!press(fleetConfirm)||system.findVehicleById("C004")||
                   fleet.totalCount()!=5||fleet.availableCount()!=4) {
                    qCritical()<<"P6-M final Fleet removal failed";
                    app.exit(501);return;
                }
                qInfo()<<"P6-M process 2 UI return/archive/exact-delete/vehicle-remove PASS";
                app.exit(0);return;
            }
            if(!p6JourneyVerify || fleet.totalCount()!=5||
               fleet.availableCount()!=4 || rentalModel.activeCount()!=1 ||
               rentalModel.historyCount()!=1 ||
               system.findVehicleById("C004")||system.findRentalById("R003") ||
               rentalModel.recentActivities().size()!=2) {
                qCritical()<<"P6-M independent process 3 final reconstruction failed";
                app.exit(502);return;
            }
            auto *available=find(find,root,"overviewAvailabilityPanel");
            auto *active=find(find,root,"overviewActiveCard");
            auto *recent=find(find,root,"overviewRecentRow0");
            if(!available||!active||!recent ||
               available->property("available").toInt()!=4||
               active->property("value").toString()!="1"||
               !recent->property("text").toString().contains("R001")) {
                qCritical()<<"P6-M final QML Overview inconsistent with disk";
                app.exit(503);return;
            }
            window->setProperty("workspace",2);
            QCoreApplication::processEvents();
            QTest::qWait(150);
            auto *history=find(find,root,"rentalFilterHistory");
            if(!press(history)||rentalModel.count()!=1 ||
               !rentalModel.locateById("R002")||
               rentalModel.selectedContract().value("status").toString()!="Returned" ||
               !find(find,root,"rentalDetailsButton")) {
                qCritical()<<"P6-M third process Rentals history/detail UI wrong";
                app.exit(504);return;
            }
            window->setProperty("workspace",1);
            QTest::qWait(135);
            fleet.selectById("C002");
            QCoreApplication::processEvents();
            if(fleet.selectedId()!="C002" ||
               fleetRemove.canRemoveVehicle("C002") ||
               !system.hasRentalHistoryForVehicle("C001")) {
                qCritical()<<"P6-M final Fleet rented/historic reference guards absent";
                app.exit(505);return;
            }
            QByteArray nowV,nowR,originalV,originalR;
            if(!readFile(vehicleLedger,&nowV)||!readFile(rentalLedger,&nowR)||
               !readFile(vehicles,&originalV)||!readFile(rentals,&originalR)||
               originalV!=beforeVehicles||originalR!=beforeRentals||
               nowV!=beforeVehicles||nowR!=beforeRentals) {
                qCritical()<<"P6-M final test fixtures altered or not restored";
                app.exit(506);return;
            }
            const QString size=compact?"1280x720":"1500x860";
            const QImage image=window->grabWindow();
            if(image.isNull()||image.size()!=QSize(compact?1280:1500,compact?720:860)||
               !image.save("p6-qt-integrated-lifecycle-"+size+".png","PNG")) {
                qCritical()<<"P6-M final screenshot missing";
                app.exit(507);return;
            }
            qInfo()<<"P6-M process 3 reconstructed Overview/Fleet/Rentals PASS"<<size;
            app.exit(0);
        });
    }
    if(p6Child) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window)return 444;
        window->resize(parser.isSet("compact")?1280:1500,
                       parser.isSet("compact")?720:860);
        window->setProperty("workspace",p6Seed?1:(p6Update?2:0));
        QTimer::singleShot(1150,&app,[&,window]{
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name && item->isVisible())return item;
                for(auto *c:item->childItems())
                    if(auto *hit=self(self,c,name))return hit;
                return nullptr;
            };
            const auto press=[&](QQuickItem *control) {
                if(!control || !control->isVisible() || !control->isEnabled())
                    return false;
                control->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window,Qt::Key_Space);
                QCoreApplication::processEvents();
                QTest::qWait(140);
                return true;
            };
            auto *root=window->contentItem();
            auto *add=find(find,root,"fleetAddButton");
            if(p6Seed) {
                if(!add || fleet.totalCount()!=5 || rentalModel.count()!=2 ||
                   !press(add)) {
                    qCritical()<<"P6 first process did not show initial Fleet add";
                    app.exit(445);return;
                }
                auto *brand=find(find,root,"fleetAddBrand");
                auto *model=find(find,root,"fleetAddModel");
                auto *rate=find(find,root,"fleetAddRate");
                auto *extra=find(find,root,"fleetAddExtra");
                auto *confirm=find(find,root,"fleetAddConfirm");
                if(!brand||!model||!rate||!extra||!confirm ||
                   extra->property("value").toInt()!=4) {
                    qCritical()<<"P6 first native Add Car default is not 4 doors";
                    app.exit(446);return;
                }
                brand->setProperty("text",QStringLiteral("Demo Motors"));
                model->setProperty("text",QStringLiteral("Orion"));
                rate->setProperty("text",QStringLiteral("420"));
                if(!press(confirm) || fleet.selectedId()!="C004" ||
                   fleet.totalCount()!=6 || !press(add)) {
                    qCritical()<<"P6 first process could not add first car";
                    app.exit(447);return;
                }
                auto *kind=find(find,root,"fleetAddType");
                brand=find(find,root,"fleetAddBrand");
                model=find(find,root,"fleetAddModel");
                rate=find(find,root,"fleetAddRate");
                extra=find(find,root,"fleetAddExtra");
                confirm=find(find,root,"fleetAddConfirm");
                if(!kind||!brand||!model||!rate||!extra||!confirm) {
                    app.exit(448);return;
                }
                kind->setProperty("currentIndex",1);
                brand->setProperty("text",QStringLiteral("Demo Moto"));
                model->setProperty("text",QStringLiteral("Nova"));
                rate->setProperty("text",QStringLiteral("300"));
                extra->setProperty("value",450);
                if(!press(confirm) || fleet.selectedId()!="M003" ||
                   fleet.totalCount()!=7 || fleet.availableCount()!=6) {
                    qCritical()<<"P6 first process failed actual Motorcycle commit";
                    app.exit(449);return;
                }
                window->setProperty("workspace",2);
                QTest::qWait(200);
                auto *list=find(find,root,"rentalList");
                auto *archive=find(find,root,"rentalArchiveButton");
                if(!list||!archive) {app.exit(450);return;}
                list->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window,Qt::Key_Down);
                QTest::keyClick(window,Qt::Key_Down);
                QCoreApplication::processEvents();
                if(rentalModel.selectedRentalId()!="R002" || !press(archive) ||
                   !press(find(find,root,"rentalArchiveConfirm")) ||
                   !system.findRentalById("R002") ||
                   !system.findRentalById("R002")->getIsArchived() ||
                   fleetRemove.canRemoveVehicle("C001")) {
                    qCritical()<<"P6 first process failed native archival/history";
                    app.exit(451);return;
                }
                qInfo()<<"P6 first native process: Car, Motorcycle, archive saved PASS";
                app.exit(0);return;
            }
            if(p6Update) {
                if(fleet.totalCount()!=7 || fleet.availableCount()!=6 ||
                   !system.findVehicleById("C004") ||
                   !system.findVehicleById("M003") ||
                   !system.findRentalById("R002") ||
                   !system.findRentalById("R002")->getIsArchived() ||
                   !fleetRemove.hasRentalHistory("C001")) {
                    qCritical()<<"P6 second process did not reconstruct previous UI commits";
                    app.exit(452);return;
                }
                auto *list=find(find,root,"rentalList");
                auto *remove=find(find,root,"rentalDeleteArchivedButton");
                if(!list || !remove){app.exit(453);return;}
                list->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window,Qt::Key_Down);
                QTest::keyClick(window,Qt::Key_Down);
                QCoreApplication::processEvents();
                if(rentalModel.selectedRentalId()!="R002" ||
                   !press(remove)) {
                    qCritical()<<"P6 reopened real UI did not select archived R002";
                    app.exit(454);return;
                }
                auto *phrase=find(find,root,"rentalDeletePhrase");
                auto *deleteButton=find(find,root,"rentalDeleteConfirm");
                if(!phrase||!deleteButton){app.exit(455);return;}
                phrase->setProperty("text",QStringLiteral("DELETE R002"));
                if(!press(deleteButton) || system.findRentalById("R002") ||
                   rentalModel.historyCount()!=0 ||
                   !fleetRemove.canRemoveVehicle("C001")) {
                    qCritical()<<"P6 second process failed exact archive deletion";
                    app.exit(456);return;
                }
                window->setProperty("workspace",1);
                QTest::qWait(180);
                auto *fleetRemoveAction=find(find,root,"fleetRemoveSelectedButton");
                if(!fleetRemoveAction) {app.exit(457);return;}
                for(const QString &id:{QStringLiteral("C003"),QStringLiteral("C001")}) {
                    fleet.selectById(id);
                    QCoreApplication::processEvents();
                    if(!press(fleetRemoveAction)) {
                        qCritical()<<"P6 reopen removal disabled unexpectedly"<<id;
                        app.exit(458);return;
                    }
                    auto *field=find(find,root,"fleetRemoveConfirmation");
                    auto *confirm=find(find,root,"fleetRemoveConfirm");
                    if(!field||!confirm){app.exit(459);return;}
                    field->setProperty("text",QStringLiteral("REMOVE ")+id);
                    if(!press(confirm) || system.findVehicleById(id)) {
                        qCritical()<<"P6 second process could not remove unreferenced vehicle"<<id;
                        app.exit(460);return;
                    }
                }
                if(fleet.totalCount()!=5 || fleet.availableCount()!=4 ||
                   rentalModel.count()!=1 || rentalModel.activeCount()!=1 ||
                   !system.findRentalById("R001")) {
                    qCritical()<<"P6 second process Fleet/Rentals totals invalid";
                    app.exit(461);return;
                }
                qInfo()<<"P6 second native process: archived deletion, Fleet removal PASS";
                app.exit(0);return;
            }
            // Third independent executable must render the reconstructed
            // operator Overview, Fleet and Rentals after previous processes exit.
            if(!p6Final || fleet.totalCount()!=5 ||
               fleet.availableCount()!=4 ||
               rentalModel.count()!=1 || rentalModel.activeCount()!=1 ||
               rentalModel.historyCount()!=0 ||
               system.findVehicleById("C001") ||
               system.findVehicleById("C003") ||
               !system.findVehicleById("C004") ||
               !system.findVehicleById("M003") ||
               system.findRentalById("R002") ||
               !system.findRentalById("R001") ||
               !system.hasRentalHistoryForVehicle("C002")) {
                qCritical()<<"P6 third process failed final domain reconstruction";
                app.exit(462);return;
            }
            auto *active=find(find,root,"overviewActiveCard");
            if(!active || active->property("value").toString()!="1") {
                qCritical()<<"P6 third process operator Overview inaccurate";
                app.exit(463);return;
            }
            window->setProperty("workspace",1);
            QTest::qWait(140);
            auto *remove=find(find,root,"fleetRemoveSelectedButton");
            fleet.selectById("C002");
            QCoreApplication::processEvents();
            if(!remove || remove->isEnabled() ||
               fleetRemove.canRemoveVehicle("C002")) {
                qCritical()<<"P6 third process allowed active vehicle removal";
                app.exit(464);return;
            }
            fleet.selectById("C004");
            QCoreApplication::processEvents();
            if(!remove->isEnabled() || !fleetRemove.canRemoveVehicle("C004")) {
                qCritical()<<"P6 third process lost new Car or selected action";
                app.exit(465);return;
            }
            QByteArray persistedV,persistedR;
            if(!readFile(QDir(dataDirectory).filePath("vehicles.txt"),&persistedV) ||
               !readFile(QDir(dataDirectory).filePath("rentals.txt"),&persistedR) ||
               persistedV.isEmpty() || persistedR.isEmpty()) {
                app.exit(466);return;
            }
            window->setProperty("workspace",2);
            QTest::qWait(130);
            if(rentalModel.count()!=1 || rentalModel.historyCount()!=0) {
                qCritical()<<"P6 third process Rentals UI counts inaccurate";
                app.exit(467);return;
            }
            QByteArray afterV,afterR;
            if(!readFile(QDir(dataDirectory).filePath("vehicles.txt"),&afterV) ||
               !readFile(QDir(dataDirectory).filePath("rentals.txt"),&afterR) ||
               afterV!=persistedV || afterR!=persistedR) {
                qCritical()<<"P6 third process read-only navigation mutated ledger";
                app.exit(468);return;
            }
            qInfo()<<"P6 third native process: Overview Fleet Rentals restart PASS";
            app.exit(0);
        });
    }
    if (parser.isSet("p6-archived-delete-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window)return 410;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",2);
        QTimer::singleShot(1100,&app,[&,window,compact]{
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->objectName()==name && item->isVisible())return item;
                for(auto *child:item->childItems())
                    if(auto *hit=self(self,child,name))return hit;
                return nullptr;
            };
            const auto press=[&](QQuickItem *item) {
                item->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window,Qt::Key_Space);
                QCoreApplication::processEvents();
                QTest::qWait(110);
            };
            auto *root=window->contentItem();
            auto *list=find(find,root,"rentalList");
            auto *archive=find(find,root,"rentalArchiveButton");
            auto *remove=find(find,root,"rentalDeleteArchivedButton");
            QByteArray initial,now,vehicleInitial,vehicleNow;
            const QString ledger=disposable.filePath("rentals.txt");
            if(!list||!archive||!remove||archive->isEnabled()||
               remove->isEnabled()||rentalModel.count()!=2||
               !readFile(ledger,&initial)||
               !readFile(disposable.filePath("vehicles.txt"),&vehicleInitial)) {
                qCritical()<<"P6-C deletion initial state not disabled/protected";
                app.exit(411);return;
            }
            ArchivedDeleteService typed(system);
            if(typed.permanentlyDelete("R001","DELETE R001").error!=
                   ArchivedDeleteError::NotArchived ||
               typed.permanentlyDelete("R002","DELETE R002").error!=
                   ArchivedDeleteError::NotArchived ||
               archivedDelete.permanentlyDelete("R001","DELETE R001") ||
               !readFile(ledger,&now)||now!=initial) {
                qCritical()<<"P6-C active/returned deletion was permitted";
                app.exit(412);return;
            }
            // Select returned R002 through the live ListView, then archive it
            // through its existing separate real keyboard-confirmed QML flow.
            list->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Down);
            QTest::keyClick(window,Qt::Key_Down);
            QCoreApplication::processEvents();
            if(rentalModel.selectedRentalId()!="R002"||!archive->isEnabled()||
               remove->isEnabled()) {
                qCritical()<<"P6-C returned selection had unsafe actions";
                app.exit(413);return;
            }
            press(archive);
            auto *archiveConfirm=find(find,root,"rentalArchiveConfirm");
            if(!archiveConfirm){app.exit(414);return;}
            press(archiveConfirm);
            QTest::qWait(130);
            if(!system.findRentalById("R002") ||
               !system.findRentalById("R002")->getIsArchived() ||
               fleetRemove.canRemoveVehicle("C001") ||
               !system.hasRentalHistoryForVehicle("C001") ||
               !remove->isEnabled() || archive->isEnabled() ||
               rentalModel.selectedRentalId()!="R002") {
                qCritical()<<"P6-C archived state did not enable deletion";
                app.exit(415);return;
            }
            auto *history=find(find,root,"rentalFilterHistory");
            if(!history){app.exit(416);return;}
            press(history);
            if(rentalModel.statusFilter()!="History" ||
               rentalModel.selectedRentalId()!="R002" || rentalModel.count()!=1) {
                qCritical()<<"P6-C History selection changed before removal";
                app.exit(417);return;
            }
            QByteArray archivedBytes;
            if(!readFile(ledger,&archivedBytes)||archivedBytes==initial) {
                qCritical()<<"P6-C archive did not persist";
                app.exit(418);return;
            }
            // UX-S12: the irreversible archived-delete presentation adapter
            // must fail closed when its last rental snapshot is unavailable.
            // A retained selected archived ID is NOT valid permission.
            {
                bool refuseRentalRead=false;
                RentalListModel isolated([&]() -> std::optional<QList<RentalRecordSummary>> {
                    if(refuseRentalRead)return std::nullopt;
                    return rentalRead.records();
                });
                P6ArchivedDeleteController guarded(
                    system,rentalRead,isolated,true);
                int observedStateChanges=0;
                QObject::connect(&guarded,&P6ArchivedDeleteController::stateChanged,
                    [&]{++observedStateChanges;});
                if(!isolated.snapshotAvailable() ||
                   !isolated.selectById("R002") ||
                   !guarded.canDeleteSelected()) {
                    qCritical()<<"UX-S12 verified archived record not eligible";
                    app.exit(855);return;
                }
                const int beforeOutage=observedStateChanges;
                refuseRentalRead=true;
                isolated.refresh();
                if(isolated.snapshotAvailable() ||
                   isolated.selectedRentalId()!="R002" ||
                   guarded.canDeleteSelected() ||
                   observedStateChanges!=beforeOutage+1 ||
                   guarded.permanentlyDelete("R002","DELETE R002") ||
                   !guarded.lastMessage().contains("data unavailable") ||
                   !system.findRentalById("R002") ||
                   !system.findRentalById("R002")->getIsArchived() ||
                   !readFile(ledger,&now) || now!=archivedBytes ||
                   !readFile(disposable.filePath("vehicles.txt"),&vehicleNow) ||
                   vehicleNow!=vehicleInitial) {
                    qCritical()<<"UX-S12 stale snapshot permitted irreversible delete";
                    app.exit(856);return;
                }
                // Further failed reads must stay blocked. Recovery may only
                // restore eligibility, never perform a deferred deletion.
                isolated.refresh();
                if(guarded.canDeleteSelected() ||
                   observedStateChanges!=beforeOutage+2) {
                    // Exactly one additional stateChanged is from the rejected
                    // direct command above, not the repeated failed refresh.
                    qCritical()<<"UX-S12 repeated failed read changed delete state";
                    app.exit(857);return;
                }
                refuseRentalRead=false;
                isolated.refresh();
                if(!isolated.snapshotAvailable() ||
                   isolated.selectedRentalId()!="R002" ||
                   !guarded.canDeleteSelected() ||
                   !guarded.lastMessage().contains("data unavailable") ||
                   !system.findRentalById("R002") ||
                   !readFile(ledger,&now) || now!=archivedBytes ||
                   !readFile(disposable.filePath("vehicles.txt"),&vehicleNow) ||
                   vehicleNow!=vehicleInitial) {
                    qCritical()<<"UX-S12 successful retry did not restore safe eligibility";
                    app.exit(858);return;
                }
            }
            const QPointF pos=remove->mapToScene(QPointF(0,0));
            if(pos.x()<0 || pos.y()<0 ||
               pos.x()+remove->width()>window->width()-2 ||
               pos.y()+remove->height()>window->height()-2) {
                qCritical()<<"P6-C destructive action clipped"<<pos;
                app.exit(419);return;
            }
            press(remove);
            auto *field=find(find,root,"rentalDeletePhrase");
            auto *confirm=find(find,root,"rentalDeleteConfirm");
            auto *cancel=find(find,root,"rentalDeleteCancel");
            auto *target=find(find,root,"rentalDeleteTarget");
            if(!field||!confirm||!cancel||!target||
               !target->property("text").toString().contains("R002")) {
                qCritical()<<"P6-C exact-target deletion modal not visible";
                app.exit(420);return;
            }
            // UX-S14: confirmed permanent deletion cannot silently re-arm
            // after selection leaves the reviewed ID and comes back.
            auto *warning=find(find,root,"rentalDeleteReviewWarning");
            if(!confirm->isEnabled() || warning) {
                qCritical()<<"UX-S14 fresh archived-delete review not armed";
                app.exit(865);return;
            }
            field->setProperty("text",QStringLiteral("DELETE R002"));
            rentalModel.clearSelection();
            QCoreApplication::processEvents();
            QTest::qWait(65);
            warning=find(find,root,"rentalDeleteReviewWarning");
            if(confirm->isEnabled() || !warning ||
               !warning->property("text").toString().contains("Selection changed.") ||
               !target->property("text").toString().contains("R002") ||
               !rentalModel.selectById("R002")) {
                qCritical()<<"UX-S14 selection-away confirmation did not invalidate";
                app.exit(866);return;
            }
            QCoreApplication::processEvents();
            if(!archivedDelete.canDeleteSelected() ||
               confirm->isEnabled() ||
               !warning->property("text").toString().contains(
                   "Close and reopen to review this deletion.") ||
               !system.findRentalById("R002") ||
               !readFile(ledger,&now)||now!=archivedBytes ||
               !readFile(disposable.filePath("vehicles.txt"),&vehicleNow)||
               vehicleNow!=vehicleInitial) {
                qCritical()<<"UX-S14 A-B-A revived archived-delete review or changed files";
                app.exit(867);return;
            }
            press(cancel);
            press(remove);
            field=find(find,root,"rentalDeletePhrase");
            confirm=find(find,root,"rentalDeleteConfirm");
            if(!field||!confirm||!confirm->isEnabled() ||
               !field->property("text").toString().isEmpty() ||
               find(find,root,"rentalDeleteReviewWarning")) {
                qCritical()<<"UX-S14 fresh review after selection change not armed";
                app.exit(868);return;
            }
            // A model refresh with the same selected ID also invalidates the
            // reviewed snapshot; neither selection nor domain writes change.
            rentalModel.refresh();
            QCoreApplication::processEvents();
            QTest::qWait(65);
            warning=find(find,root,"rentalDeleteReviewWarning");
            if(rentalModel.selectedRentalId()!="R002" ||
               !archivedDelete.canDeleteSelected() ||
               confirm->isEnabled() || !warning ||
               !warning->property("text").toString().contains("Rental data refreshed.") ||
               !readFile(ledger,&now)||now!=archivedBytes) {
                qCritical()<<"UX-S14 model reset did not invalidate archived-delete review";
                app.exit(869);return;
            }
            QTest::keyClick(window,Qt::Key_Escape);
            QCoreApplication::processEvents();
            QTest::qWait(90);
            press(remove);
            field=find(find,root,"rentalDeletePhrase");
            confirm=find(find,root,"rentalDeleteConfirm");
            if(!field||!confirm||!confirm->isEnabled() ||
               find(find,root,"rentalDeleteReviewWarning")) {
                qCritical()<<"UX-S14 refresh recovery did not require fresh review";
                app.exit(870);return;
            }
            press(confirm); // Blank phrase.
            field->setProperty("text",QStringLiteral("delete R002"));
            press(confirm); // Case mismatch.
            field->setProperty("text",QStringLiteral("DELETE R001"));
            press(confirm); // Wrong ID.
            if(!system.findRentalById("R002") ||
               !readFile(ledger,&now)||now!=archivedBytes) {
                qCritical()<<"P6-C invalid deletion phrase wrote ledger";
                app.exit(421);return;
            }
            press(cancel);
            if(!readFile(ledger,&now)||now!=archivedBytes) {
                qCritical()<<"P6-C Cancel wrote ledger";
                app.exit(422);return;
            }
            press(remove);
            QTest::keyClick(window,Qt::Key_Escape);
            QCoreApplication::processEvents();
            QTest::qWait(110);
            if(!readFile(ledger,&now)||now!=archivedBytes) {
                qCritical()<<"P6-C Escape wrote ledger";
                app.exit(423);return;
            }
            // Even a forged current-UI call cannot swap a stale target.
            if(archivedDelete.permanentlyDelete("R001","DELETE R001")) {
                qCritical()<<"P6-C stale selected ID deletion accepted";
                app.exit(424);return;
            }
            // The real writer must leave the archived record intact on failure.
            RentalSystem failing(disposable.path(),
                                 std::make_shared<P5RejectWriteStorage>());
            ArchivedDeleteService failedWriter(failing);
            const auto failed=failedWriter.permanentlyDelete("R002","DELETE R002");
            if(failed.error!=ArchivedDeleteError::CommitRejected ||
               !failing.findRentalById("R002") ||
               !failing.findRentalById("R002")->getIsArchived() ||
               !readFile(ledger,&now)||now!=archivedBytes) {
                qCritical()<<"P6-C rejected atomic write did not roll back";
                app.exit(425);return;
            }
            P6ArchivedDeleteController fixedSession(
                system,rentalRead,rentalModel,false);
            if(fixedSession.canDeleteSelected() ||
               fixedSession.permanentlyDelete("R002","DELETE R002") ||
               !readFile(ledger,&now)||now!=archivedBytes) {
                qCritical()<<"P6-C fixed fictional session allowed deletion";
                app.exit(426);return;
            }
            press(remove);
            field=find(find,root,"rentalDeletePhrase");
            confirm=find(find,root,"rentalDeleteConfirm");
            if(!field||!confirm){app.exit(427);return;}
            field->setProperty("text",QStringLiteral("DELETE R002"));
            press(confirm);
            QTest::qWait(180);
            if(system.findRentalById("R002") ||
               rentalModel.count()!=0 || rentalModel.historyCount()!=0 ||
               rentalModel.activeCount()!=1 ||
               rentalModel.statusFilter()!="History" ||
               !rentalModel.selectedRentalId().isEmpty() ||
               remove->isEnabled() ||
               !list->hasActiveFocus() ||
               !readFile(ledger,&now)||now==archivedBytes ||
               !readFile(disposable.filePath("vehicles.txt"),&vehicleNow) ||
               vehicleNow!=vehicleInitial) {
                qCritical()<<"P6-C genuine deletion/model/focus reconciliation failed";
                app.exit(428);return;
            }
            RentalSystem reopened(disposable.path());
            QByteArray originalV,originalR;
            if(reopened.findRentalById("R002") ||
               !reopened.findRentalById("R001") ||
               !reopened.findActiveRentalByVehicleId("C002") ||
               !readFile(vehicles,&originalV) ||
               !readFile(rentals,&originalR) ||
               originalV!=beforeVehicles || originalR!=beforeRentals) {
                qCritical()<<"P6-C restart or protected fixtures failed";
                app.exit(429);return;
            }
            const QString size=compact?"1280x720":"1500x860";
            const QImage image=window->grabWindow();
            if(image.isNull() ||
               image.size()!=QSize(compact?1280:1500,compact?720:860) ||
               !image.save("p6-qt-archived-delete-"+size+".png","PNG")) {
                qCritical()<<"P6-C native screenshot invalid";
                app.exit(430);return;
            }
            // Cross-workspace transaction proof: removing the last archived
            // reference releases an otherwise available historical vehicle.
            if(!fleetRemove.canRemoveVehicle("C001") ||
               system.hasRentalHistoryForVehicle("C001") ||
               !fleetRemove.hasRentalHistory("C002")) {
                qCritical()<<"P6 historical vehicle guard not released after actual archived deletion";
                app.exit(432);return;
            }
            window->setProperty("workspace",1);
            fleet.selectById("C001");
            QCoreApplication::processEvents();
            QTest::qWait(120);
            auto *fleetRemoveAction=find(find,root,"fleetRemoveSelectedButton");
            if(!fleetRemoveAction || !fleetRemoveAction->isEnabled()) {
                qCritical()<<"P6 Fleet action did not enable after history deletion";
                app.exit(433);return;
            }
            press(fleetRemoveAction);
            auto *fleetDeletePhrase=find(find,root,"fleetRemoveConfirmation");
            auto *fleetDeleteConfirm=find(find,root,"fleetRemoveConfirm");
            if(!fleetDeletePhrase || !fleetDeleteConfirm) {
                qCritical()<<"P6 Fleet removal modal absent after release";
                app.exit(434);return;
            }
            fleetDeletePhrase->setProperty("text",QStringLiteral("REMOVE C001"));
            press(fleetDeleteConfirm);
            QTest::qWait(160);
            RentalSystem finalReopen(disposable.path());
            QByteArray finalLedger,finalVehicles;
            if(system.findVehicleById("C001") ||
               finalReopen.findVehicleById("C001") ||
               finalReopen.findRentalById("R002") ||
               !finalReopen.findRentalById("R001") ||
               !finalReopen.findVehicleById("C002") ||
               !readFile(disposable.filePath("rentals.txt"),&finalLedger) ||
               finalLedger!=now ||
               !readFile(disposable.filePath("vehicles.txt"),&finalVehicles) ||
               finalVehicles==vehicleInitial ||
               !readFile(vehicles,&originalV) || originalV!=beforeVehicles ||
               !readFile(rentals,&originalR) || originalR!=beforeRentals) {
                qCritical()<<"P6 cross-workspace delete/archive/remove/reopen integrity failed";
                app.exit(435);return;
            }
            qInfo()<<"P6 native archive->history guard->delete->Fleet removal/reopen PASS"
                   <<size;
            app.exit(0);
        });
    }
    if (parser.isSet("p6-fleet-remove-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window)return 395;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",1);
        QTimer::singleShot(1100,&app,[&,window,compact] {
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->isVisible() && item->objectName()==name)return item;
                for(auto *child:item->childItems())
                    if(auto *hit=self(self,child,name))return hit;
                return nullptr;
            };
            const auto press=[&](QQuickItem *item) {
                item->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window,Qt::Key_Space);
                QCoreApplication::processEvents();
                QTest::qWait(110);
            };
            auto *root=window->contentItem();
            auto *remove=find(find,root,"fleetRemoveSelectedButton");
            QByteArray vehiclesInitial,rentalsInitial,vehiclesCurrent,rentalsCurrent;
            const QString ledger=disposable.filePath("vehicles.txt");
            const QString rentalsLedger=disposable.filePath("rentals.txt");
            if(!remove || remove->isEnabled() || fleet.totalCount()!=5 ||
               !readFile(ledger,&vehiclesInitial) ||
               !readFile(rentalsLedger,&rentalsInitial)) {
                qCritical()<<"P6 Remove Vehicle initial state unsafe";
                app.exit(396);return;
            }
            // A checked-out Ranger is never offered destructive controls.
            fleet.selectById("C002");
            QCoreApplication::processEvents();
            if(remove->isEnabled()) {
                qCritical()<<"P6 active rental exposed Remove action";
                app.exit(397);return;
            }
            // C001 is available yet R002 still references it as a returned
            // historical contract. The inspector must expose this safeguard
            // instead of offering a confirmation that can only fail.
            fleet.selectById("C001");
            QCoreApplication::processEvents();
            auto *historyHint=find(find,root,"fleetRentalHistoryHint");
            if(remove->isEnabled() || !fleet.selectedAvailable() ||
               !historyHint || !historyHint->property("text").toString().contains("history") ||
               fleetRemove.canRemoveVehicle("C001") ||
               fleetRemove.removeVehicle("C001","REMOVE C001") ||
               !system.findVehicleById("C001") ||
               !readFile(ledger,&vehiclesCurrent) ||
               vehiclesCurrent!=vehiclesInitial) {
                qCritical()<<"P6 historical returned rental orphan prevention failed";
                app.exit(431);return;
            }
            fleet.selectById("C003");
            QCoreApplication::processEvents();
            if(!remove->isEnabled() || !fleet.selectedAvailable()) {
                qCritical()<<"P6 available vehicle removal action missing";
                app.exit(398);return;
            }
            const QPointF pos=remove->mapToScene(QPointF(0,0));
            if(pos.x()<0 || pos.y()<0 ||
               pos.x()+remove->width()>window->width()-2 ||
               pos.y()+remove->height()>window->height()-2) {
                qCritical()<<"P6 destructive control outside compact viewport"<<pos;
                app.exit(399);return;
            }
            press(remove);
            auto *field=find(find,root,"fleetRemoveConfirmation");
            auto *confirm=find(find,root,"fleetRemoveConfirm");
            auto *cancel=find(find,root,"fleetRemoveCancel");
            auto *target=find(find,root,"fleetRemoveTarget");
            if(!field||!confirm||!cancel||!target ||
               !target->property("text").toString().contains("C003")) {
                qCritical()<<"P6 removal dialog target missing";
                app.exit(400);return;
            }
            // R2-M2D: real mouse, real shared destructive Button, no command
            // activation until release on the SAME safe control.
            auto *dangerSurface=find(find,confirm,"destructiveActionFeedbackSurface");
            if(!dangerSurface ||
               confirm->property("hoverFeedbackMs").toInt()<0 ||
               confirm->property("pressFeedbackMs").toInt()<0 ||
               confirm->property("feedbackSurface").value<QColor>()!=QColor("#B42318")) {
                qCritical()<<"R2-M2D destructive action idle feedback missing";
                app.exit(915);return;
            }
            const auto dangerCenter=confirm->mapToScene(
                QPointF(confirm->width()/2,confirm->height()/2)).toPoint();
            QTest::mouseMove(window,dangerCenter);
            QCoreApplication::processEvents();
            if(!confirm->property("hovered").toBool() ||
               confirm->property("feedbackSurface").value<QColor>()!=QColor("#912018")) {
                qCritical()<<"R2-M2D destructive hover state missing";
                app.exit(916);return;
            }
            QTest::qWait(145);
            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,dangerCenter);
            QCoreApplication::processEvents();
            if(!confirm->property("down").toBool() ||
               confirm->property("feedbackSurface").value<QColor>()!=QColor("#771B16")) {
                qCritical()<<"R2-M2D pressed feedback is not distinct from hover";
                app.exit(917);return;
            }
            // Native colour animation and software renderer can settle on
            // different frames; reject an incorrect final pressed colour.
            QColor renderedDanger=dangerSurface->property("color").value<QColor>();
            for(int tick=0;tick<18 && renderedDanger!=QColor("#771B16");++tick) {
                QTest::qWait(20);
                renderedDanger=dangerSurface->property("color").value<QColor>();
            }
            if(renderedDanger!=QColor("#771B16")) {
                qCritical()<<"R2-M2D destructive pressed colour did not settle"
                           <<renderedDanger;
                app.exit(918);return;
            }
            // Abandoning a mouse press must NOT cause accidental removal.
            const QPoint abandoned(2,2);
            QTest::mouseMove(window,abandoned);
            QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,abandoned);
            QCoreApplication::processEvents();
            if(confirm->property("down").toBool() ||
               fleet.totalCount()!=5 || !system.findVehicleById("C003") ||
               !readFile(ledger,&vehiclesCurrent) || vehiclesCurrent!=vehiclesInitial) {
                qCritical()<<"R2-M2D abandoned destructive press changed fleet";
                app.exit(919);return;
            }
            // UX-S15: an in-progress typed REMOVE C003 must never survive
            // a change in the independently selected Fleet card. Returning
            // to C003 must not re-arm the original consent.
            if(!confirm->isEnabled() ||
               find(find,root,"fleetRemoveReviewWarning")) {
                qCritical()<<"UX-S15 fresh Fleet removal confirmation not armed";
                app.exit(871);return;
            }
            field->setProperty("text",QStringLiteral("REMOVE C003"));
            fleet.clearSelection();
            QCoreApplication::processEvents();
            QTest::qWait(55);
            auto *warning=find(find,root,"fleetRemoveReviewWarning");
            if(confirm->isEnabled() || !warning ||
               !warning->property("text").toString().contains("Selection changed.") ||
               !target->property("text").toString().contains("C003") ||
               fleet.selectedId()!="" ||
               !readFile(ledger,&vehiclesCurrent) ||
               vehiclesCurrent!=vehiclesInitial) {
                qCritical()<<"UX-S15 selection change did not invalidate Fleet removal";
                app.exit(872);return;
            }
            fleet.selectById("C003");
            QCoreApplication::processEvents();
            if(!fleetRemove.canRemoveVehicle("C003") ||
               confirm->isEnabled() ||
               !warning->property("text").toString().contains(
                   "Close and reopen to review this removal.") ||
               !readFile(ledger,&vehiclesCurrent) ||
               vehiclesCurrent!=vehiclesInitial) {
                qCritical()<<"UX-S15 A-B-A restored stale Fleet deletion consent";
                app.exit(873);return;
            }
            press(cancel);
            press(remove);
            field=find(find,root,"fleetRemoveConfirmation");
            confirm=find(find,root,"fleetRemoveConfirm");
            if(!field||!confirm||!confirm->isEnabled() ||
               !field->property("text").toString().isEmpty() ||
               find(find,root,"fleetRemoveReviewWarning")) {
                qCritical()<<"UX-S15 fresh Fleet removal review not armed";
                app.exit(874);return;
            }
            // A domain Fleet refresh that retains the selected ID must also
            // expire the old review, without removing any fictional record.
            fleet.refreshAfterCommit();
            QCoreApplication::processEvents();
            QTest::qWait(55);
            warning=find(find,root,"fleetRemoveReviewWarning");
            if(fleet.selectedId()!="C003" ||
               confirm->isEnabled() || !warning ||
               !warning->property("text").toString().contains(
                   "Fleet data refreshed.") ||
               !readFile(ledger,&vehiclesCurrent) ||
               vehiclesCurrent!=vehiclesInitial) {
                qCritical()<<"UX-S15 Fleet refresh did not invalidate review";
                app.exit(875);return;
            }
            QTest::keyClick(window,Qt::Key_Escape);
            QCoreApplication::processEvents();
            QTest::qWait(80);
            press(remove);
            field=find(find,root,"fleetRemoveConfirmation");
            confirm=find(find,root,"fleetRemoveConfirm");
            if(!field||!confirm||!confirm->isEnabled() ||
               !field->property("text").toString().isEmpty() ||
               find(find,root,"fleetRemoveReviewWarning")) {
                qCritical()<<"UX-S15 Fleet refresh recovery skipped new review";
                app.exit(876);return;
            }
            press(confirm); // Empty phrase.
            field->setProperty("text",QStringLiteral("remove C003")); // wrong case
            press(confirm);
            if(fleet.totalCount()!=5 || !system.findVehicleById("C003") ||
               !readFile(ledger,&vehiclesCurrent) ||
               vehiclesCurrent!=vehiclesInitial) {
                qCritical()<<"P6 invalid confirmation changed the fleet";
                app.exit(401);return;
            }
            press(cancel);
            QTest::qWait(100);
            if(fleet.totalCount()!=5 || !readFile(ledger,&vehiclesCurrent) ||
               vehiclesCurrent!=vehiclesInitial) {
                qCritical()<<"P6 Cancel modified the fleet";
                app.exit(402);return;
            }
            press(remove);
            QTest::keyClick(window,Qt::Key_Escape);
            QCoreApplication::processEvents();
            QTest::qWait(110);
            if(fleet.totalCount()!=5 || !readFile(ledger,&vehiclesCurrent) ||
               vehiclesCurrent!=vehiclesInitial) {
                qCritical()<<"P6 Escape modified the fleet";
                app.exit(403);return;
            }
            press(remove);
            field=find(find,root,"fleetRemoveConfirmation");
            confirm=find(find,root,"fleetRemoveConfirm");
            if(!field || !confirm) {app.exit(404);return;}
            field->setProperty("text",QStringLiteral("REMOVE C003"));
            press(confirm);
            QTest::qWait(160);
            auto *remainingAction=find(find,root,"fleetAddButton");
            if(!remainingAction || !remainingAction->hasActiveFocus()) {
                qCritical()<<"P6 removed selection failed keyboard focus recovery";
                app.exit(409);return;
            }
            if(fleet.totalCount()!=4 || fleet.availableCount()!=3 ||
               !fleet.selectedId().isEmpty() ||
               system.findVehicleById("C003") ||
               !readFile(ledger,&vehiclesCurrent) ||
               vehiclesCurrent==vehiclesInitial ||
               !readFile(rentalsLedger,&rentalsCurrent) ||
               rentalsCurrent!=rentalsInitial) {
                qCritical()<<"P6 genuine confirmed removal or model reconciliation failed";
                app.exit(405);return;
            }
            RentalSystem reopened(disposable.path());
            if(reopened.findVehicleById("C003") ||
               !reopened.findVehicleById("C002") ||
               !reopened.findActiveRentalByVehicleId("C002")) {
                qCritical()<<"P6 removal did not rehydrate correctly";
                app.exit(406);return;
            }
            // A failed atomic writer must restore the vehicle in memory,
            // preserve on-disk bytes and emit no false success message.
            RentalSystem failing(disposable.path(),
                                 std::make_shared<P5RejectWriteStorage>());
            FleetRemoveController failController(failing,true,{});
            if(failController.removeVehicle("M001","REMOVE M001") ||
               !failing.findVehicleById("M001") ||
               !readFile(ledger,&vehiclesInitial) ||
               vehiclesInitial!=vehiclesCurrent) {
                qCritical()<<"P6 failed write was not rolled back";
                app.exit(407);return;
            }
            FleetRemoveController blocked(system,false,{});
            QByteArray seedVehicles,seedRentals;
            if(blocked.removeVehicle("C001","REMOVE C001") ||
               !system.findVehicleById("C001") ||
               !readFile(vehicles,&seedVehicles) ||
               !readFile(rentals,&seedRentals) ||
               seedVehicles!=beforeVehicles || seedRentals!=beforeRentals) {
                qCritical()<<"P6 fixed fictional source / persistent guard failed";
                app.exit(408);return;
            }
            qInfo()<<"P6 native guarded Fleet remove, cancel, rollback PASS"
                   <<(compact?"1280x720":"1500x860");
            app.exit(0);
        });
    }
    if (parser.isSet("p6-fleet-add-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(!window)return 386;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",1);
        QTimer::singleShot(1100,&app,[&,window,compact] {
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if(!item)return nullptr;
                if(item->isVisible() && item->objectName()==name)return item;
                for(auto *child:item->childItems())
                    if(auto *hit=self(self,child,name))return hit;
                return nullptr;
            };
            const auto press=[&](QQuickItem *item) {
                item->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window,Qt::Key_Space);
                QCoreApplication::processEvents();
                QTest::qWait(90);
            };
            auto *root=window->contentItem();
            auto *add=find(find,root,"fleetAddButton");
            if(!add || !add->isEnabled() || !fleetAdmin.canAdd() ||
               fleet.totalCount()!=5 || fleet.availableCount()!=4) {
                qCritical()<<"P6 Add Vehicle initial action unavailable";
                app.exit(387);return;
            }
            QByteArray originalVehicle,originalRental,afterVehicle;
            const QString ledger=disposable.filePath("vehicles.txt");
            if(!readFile(ledger,&originalVehicle) ||
               !readFile(disposable.filePath("rentals.txt"),&originalRental)) {
                app.exit(388);return;
            }
            press(add);
            auto *kind=find(find,root,"fleetAddType");
            auto *brand=find(find,root,"fleetAddBrand");
            auto *model=find(find,root,"fleetAddModel");
            auto *rate=find(find,root,"fleetAddRate");
            auto *extra=find(find,root,"fleetAddExtra");
            auto *confirm=find(find,root,"fleetAddConfirm");
            if(!kind||!brand||!model||!rate||!extra||!confirm) {
                qCritical()<<"P6 real Car/Motorcycle dialog fields missing";
                app.exit(389);return;
            }
            // Reject blank/invalid inputs in the actual QML form.
            rate->setProperty("text",QStringLiteral("420"));
            press(confirm);
            if(fleet.totalCount()!=5 || !readFile(ledger,&afterVehicle) ||
               afterVehicle!=originalVehicle) {
                qCritical()<<"P6 invalid form incorrectly wrote fleet";
                app.exit(390);return;
            }
            brand->setProperty("text",QStringLiteral("Demo Motors"));
            model->setProperty("text",QStringLiteral("Orion"));
            extra->setProperty("value",4);
            press(confirm);
            if(fleet.totalCount()!=6 || fleet.availableCount()!=5 ||
               fleet.selectedId()!="C004" || !system.findVehicleById("C004") ||
               !readFile(ledger,&afterVehicle) || afterVehicle==originalVehicle) {
                qCritical()<<"P6 Car commit or Fleet UI refresh failed";
                app.exit(391);return;
            }
            press(add);
            kind=find(find,root,"fleetAddType");
            brand=find(find,root,"fleetAddBrand");
            model=find(find,root,"fleetAddModel");
            rate=find(find,root,"fleetAddRate");
            extra=find(find,root,"fleetAddExtra");
            confirm=find(find,root,"fleetAddConfirm");
            if(!kind||!brand||!model||!rate||!extra||!confirm) {
                qCritical()<<"P6 second dialog failed to reopen";
                app.exit(392);return;
            }
            kind->setProperty("currentIndex",1);
            brand->setProperty("text",QStringLiteral("Demo Moto"));
            model->setProperty("text",QStringLiteral("Nova"));
            rate->setProperty("text",QStringLiteral("300"));
            extra->setProperty("value",450);
            press(confirm);
            if(fleet.totalCount()!=7 || fleet.availableCount()!=6 ||
               fleet.selectedId()!="M003" || !system.findVehicleById("M003")) {
                qCritical()<<"P6 Motorcycle commit or selection failed";
                app.exit(393);return;
            }
            // Rehydrate the domain from this disposable directory and prove
            // the checked-in fictional seed is still byte-for-byte protected.
            RentalSystem reopened(disposable.path());
            QByteArray protectedV,protectedR,afterRentals;
            FleetAddController blocked(system,false,[](const QString&){});
            if(!reopened.findVehicleById("C004") ||
               !reopened.findVehicleById("M003") ||
               blocked.addVehicle("Car","Demo","Nope",100,4) ||
               !readFile(vehicles,&protectedV) ||
               !readFile(rentals,&protectedR) ||
               !readFile(disposable.filePath("rentals.txt"),&afterRentals) ||
               protectedV!=beforeVehicles || protectedR!=beforeRentals ||
               afterRentals!=originalRental) {
                qCritical()<<"P6 persisted temp records, read-only guard, or protected seeds failed";
                app.exit(394);return;
            }
            qInfo()<<"P6 native disposable Add Car and Motorcycle PASS"
                   <<(compact?"1280x720":"1500x860");
            app.exit(0);
        });
    }
    if (parser.isSet("p4-a11y-smoke")) {
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window) return 270;
        const bool compact=parser.isSet("compact");
        window->resize(compact?1280:1500,compact?720:860);
        window->setProperty("workspace",1);
        QTimer::singleShot(1250,&app,[&,window,compact] {
            const auto find=[](const auto &self,QQuickItem *item,
                               const QString &name)->QQuickItem* {
                if (!item) return nullptr;
                if (item->objectName()==name && item->isVisible()) return item;
                for (auto *child:item->childItems())
                    if (auto *hit=self(self,child,name)) return hit;
                return nullptr;
            };
            auto *root=window->contentItem();
            const auto checkRadio=[&](const QString &id,const QString &expectedName,
                                      bool expectedChecked)->bool {
                auto *item=find(find,root,id);
                auto *iface=item ? QAccessible::queryAccessibleInterface(item) : nullptr;
                if (!iface || !iface->isValid() ||
                    iface->role()!=QAccessible::RadioButton ||
                    iface->text(QAccessible::Name)!=expectedName ||
                    iface->state().checked!=expectedChecked) {
                    qCritical()<<"P4 accessibility radio mismatch"<<id
                               <<expectedName<<expectedChecked
                               <<(iface ? iface->text(QAccessible::Name) : QStringLiteral("missing"))
                               <<(iface ? static_cast<int>(iface->role()) : -1);
                    return false;
                }
                return true;
            };
            // Side navigation must expose which workspace is current.
            if (!checkRadio("workspaceNavOverview","Overview workspace",false) ||
                !checkRadio("workspaceNavFleet","Vehicles workspace",true) ||
                !checkRadio("workspaceNavRentals","Rentals workspace",false)) {
                app.exit(278);return;
            }
            auto *overviewNav=find(find,root,"workspaceNavOverview");
            overviewNav->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QCoreApplication::processEvents();
            if (window->property("workspace").toInt()!=0 ||
                !checkRadio("workspaceNavOverview","Overview workspace",true) ||
                !checkRadio("workspaceNavFleet","Vehicles workspace",false)) {
                qCritical()<<"P4 Overview workspace accessibility did not update";
                app.exit(279);return;
            }
            auto *fleetNav=find(find,root,"workspaceNavFleet");
            fleetNav->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QCoreApplication::processEvents();
            if (window->property("workspace").toInt()!=1 ||
                !checkRadio("workspaceNavOverview","Overview workspace",false) ||
                !checkRadio("workspaceNavFleet","Vehicles workspace",true)) {
                qCritical()<<"P4 Fleet workspace accessibility did not update";
                app.exit(280);return;
            }
            if (!checkRadio("fleetGalleryToggle","Fleet gallery view",true) ||
                !checkRadio("fleetTableToggle","Fleet table view",false)) {
                app.exit(271);return;
            }
            // These native input controls must have meaningful, public names.
            const auto checkName=[&](const QString &id,const QString &expected)->bool {
                auto *item=find(find,root,id);
                auto *iface=item ? QAccessible::queryAccessibleInterface(item) : nullptr;
                return iface && iface->isValid() &&
                       iface->text(QAccessible::Name)==expected;
            };
            if (!checkName("fleetSearchField","Search fleet") ||
                !checkName("fleetStatusCombo","Filter fleet status") ||
                !checkName("fleetSortCombo","Sort fleet")) {
                qCritical()<<"P4 native Fleet input accessible names absent";
                app.exit(272);return;
            }
            auto *table=find(find,root,"fleetTableToggle");
            table->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QCoreApplication::processEvents();
            if (window->property("gallery").toBool() ||
                !checkRadio("fleetGalleryToggle","Fleet gallery view",false) ||
                !checkRadio("fleetTableToggle","Fleet table view",true)) {
                qCritical()<<"P4 native Fleet view selection inaccessible after keyboard";
                app.exit(273);return;
            }
            window->setProperty("workspace",2);
            QCoreApplication::processEvents();
            if (!checkRadio("rentalFilterAll","All rentals",true) ||
                !checkRadio("rentalFilterActive","Active rentals",false) ||
                !checkRadio("rentalFilterHistory","History rentals",false)) {
                app.exit(274);return;
            }
            auto *history=find(find,root,"rentalFilterHistory");
            history->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QCoreApplication::processEvents();
            if (rentalModel.statusFilter()!="History" ||
                !checkRadio("rentalFilterAll","All rentals",false) ||
                !checkRadio("rentalFilterHistory","History rentals",true)) {
                qCritical()<<"P4 History accessibility state stale after keyboard";
                app.exit(275);return;
            }
            auto *active=find(find,root,"rentalFilterActive");
            active->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Space);
            QCoreApplication::processEvents();
            if (rentalModel.statusFilter()!="Active" ||
                !checkRadio("rentalFilterActive","Active rentals",true) ||
                !checkRadio("rentalFilterHistory","History rentals",false)) {
                qCritical()<<"P4 Active accessibility state stale after keyboard";
                app.exit(276);return;
            }
            if (!checkRadio("workspaceNavRentals","Rentals workspace",true) ||
                !checkRadio("workspaceNavFleet","Vehicles workspace",false)) {
                qCritical()<<"P4 Rentals workspace accessible state stale";
                app.exit(281);return;
            }
            // Qt ListView recreates delegates after model reset. Wait for the
            // actual QQuickWindow render turn, not just synchronous C++ events.
            // Never pass this test by skipping a missing visible rental row.
            QTest::qWait(130);
            // Rental row exposes status, due status, value and current selection
            // as spoken metadata; no fictional customer contact is leaked.
            auto *view=find(find,root,"rentalList");
            auto *row=find(find,root,"rentalRow");
            auto *rowInterface=row ? QAccessible::queryAccessibleInterface(row) : nullptr;
            if (rentalModel.count()==0 || !view || !rowInterface ||
                !rowInterface->isValid()) {
                qCritical()<<"P4 rental row missing from accessible tree"
                           <<"visible filtered records"<<rentalModel.count()
                           <<"list view present"<<bool(view)
                           <<"visible row present"<<bool(row);
                app.exit(282);return;
            }
            const auto rowName=[&] { return rowInterface->text(QAccessible::Name); };
            const QString id=row->property("rentalId").toString();
            const QString due=row->property("dueState").toString();
            const auto total=row->property("total").toDouble();
            const QString value=QStringLiteral("total R %1").arg(total,0,'f',2);
            if (id.isEmpty() || due.isEmpty() ||
                !rowName().contains(QStringLiteral("Rental ")+id) ||
                !rowName().contains(due) || !rowName().contains(value)) {
                qCritical()<<"P4 rental accessible row omitted due/total" <<rowName();
                app.exit(283);return;
            }
            if (!rentalModel.selectById(id)) {
                qCritical()<<"P4 rental accessible selection setup failed"<<id;
                app.exit(284);return;
            }
            QCoreApplication::processEvents();
            if (!rowName().startsWith(QStringLiteral("Selected rental ")+id)) {
                qCritical()<<"P4 selected rental not announced in accessible name"<<rowName();
                app.exit(285);return;
            }
            rentalModel.clearSelection();
            QCoreApplication::processEvents();
            if (!rowName().startsWith(QStringLiteral("Rental ")+id)) {
                qCritical()<<"P4 cleared rental still labelled selected"<<rowName();
                app.exit(286);return;
            }
            QByteArray fv,fr,tr;
            if (!readFile(vehicles,&fv) || !readFile(rentals,&fr) ||
                !readFile(disposable.filePath("rentals.txt"),&tr) ||
                fv!=beforeVehicles || fr!=beforeRentals || tr!=beforeRentals) {
                qCritical()<<"P4 accessibility navigation mutated fictional files";
                app.exit(277);return;
            }
            qInfo()<<"P4 native accessibility controls PASS"
                   <<(compact?"1280x720":"1500x860")
                   <<"real roles/names/checked states and immutable fixtures";
            app.exit(0);
        });
    }
    if (parser.isSet("p4-motion-smoke")) {
        auto *window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window) return 260;
        const bool compact = parser.isSet("compact");
        const bool reduce = parser.isSet("p4-reduce-motion");
        window->resize(compact ? 1280 : 1500, compact ? 720 : 860);
        window->setProperty("workspace", 1);
        QTimer::singleShot(1200, &app, [&, window, compact, reduce]{
            const auto find=[](const auto &self, QQuickItem *item,
                               const QString &name) -> QQuickItem* {
                if (!item) return nullptr;
                if (item->objectName() == name && item->isVisible())
                    return item;
                for (auto *child : item->childItems())
                    if (auto *found=self(self, child, name)) return found;
                return nullptr;
            };
            auto *frame=find(find, window->contentItem(), "fleetGalleryFocus");
            auto matches=[&](bool expected) {
                const int duration=expected ? 0 : 90;
                return motion.reducedMotion() == expected &&
                       motion.selectionFadeMs() == duration &&
                       window->property("reducedMotion").toBool() == expected &&
                       window->property("selectionFadeMs").toInt() == duration &&
                       frame && frame->property("selectionFadeMs").toInt() == duration;
            };
            if (!matches(reduce)) {
                qCritical()<<"P4 initial OS-motion injection/binding failed"
                           <<reduce << motion.selectionFadeMs();
                app.exit(261);return;
            }
            // Simulate a later OS preference change (no host Windows settings
            // modification) and prove the native QML view responds live.
            motion.setTestPreference(!reduce);
            QCoreApplication::processEvents();
            if (!matches(!reduce)) {
                qCritical()<<"P4 live reduced-motion preference changed but QML stale";
                app.exit(262);return;
            }
            motion.setTestPreference(reduce);
            QCoreApplication::processEvents();
            if (!matches(reduce)) {
                qCritical()<<"P4 motion restoration failed";
                app.exit(263);return;
            }
            // M1: exercise real sidebar animation using the existing P4
            // injectable OS preference, not static source-string inspection.
            // State, keyboard focus and nav accessibility remain immediate.
            auto *sidebar=find(find,window->contentItem(),"appSidebar");
            auto *toggle=find(find,window->contentItem(),"sidebarToggleButton");
            auto *fleetNav=find(find,window->contentItem(),"workspaceNavFleet");
            const auto progress=[&]{
                return window->property("sidebarCollapseProgress").toDouble();
            };
            if (!sidebar || !toggle || !fleetNav ||
                window->property("sidebarMotionMs").toInt()!=(reduce ? 0 : 220) ||
                sidebar->width()<210 || progress()>0.01 ||
                window->property("sidebarCollapsed").toBool()) {
                qCritical()<<"M1 sidebar animation initial state invalid"
                           <<reduce<<progress();
                app.exit(287);return;
            }
            const auto toggleFromKeyboard=[&] {
                toggle->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window,Qt::Key_Space);
                QCoreApplication::processEvents();
            };
            toggleFromKeyboard();
            if (!window->property("sidebarCollapsed").toBool() ||
                toggle->property("text").toString()!="Expand sidebar" ||
                !toggle->hasActiveFocus() ||
                !fleetNav->property("currentWorkspace").toBool()) {
                qCritical()<<"M1 logical sidebar collapse delayed keyboard or workspace";
                app.exit(288);return;
            }
            if (!reduce) {
                QTest::qWait(65);
                if(progress()<=0.04 || progress()>=0.99 ||
                   sidebar->width()>=214 || sidebar->width()<=77) {
                    qCritical()<<"M1 ordinary sidebar has no intermediate motion"
                               <<progress()<<sidebar->width();
                    app.exit(289);return;
                }
            } else if(progress()<0.999) {
                qCritical()<<"M1 reduced-motion collapse did not snap";
                app.exit(290);return;
            }
            QTest::qWait(reduce ? 30 : 260);
            if(sidebar->width()>82 || progress()<0.999) {
                qCritical()<<"M1 sidebar did not reach collapsed width"
                           <<sidebar->width()<<progress();
                app.exit(291);return;
            }
            const QImage collapsedImage=window->grabWindow();
            const QString sidebarSize=compact?"1280x720":"1500x860";
            const QString sidebarMode=reduce?"reduced":"ordinary";
            if(collapsedImage.isNull() ||
               collapsedImage.size()!=QSize(compact?1280:1500,compact?720:860) ||
               !collapsedImage.save("p4-qt-sidebar-"+sidebarMode+"-"+
                                    sidebarSize+"-collapsed.png","PNG")) {
                qCritical()<<"M1 actual collapsed screenshot missing";
                app.exit(292);return;
            }
            // Reversing an animation mid-flight should still converge.
            toggleFromKeyboard();
            if(window->property("sidebarCollapsed").toBool() ||
               toggle->property("text").toString()!="Collapse sidebar") {
                qCritical()<<"M1 expansion did not change state immediately";
                app.exit(293);return;
            }
            if(!reduce) {
                QTest::qWait(55);
                toggleFromKeyboard();
                QTest::qWait(275);
                if(!window->property("sidebarCollapsed").toBool() ||
                   sidebar->width()>82 || progress()<0.999) {
                    qCritical()<<"M1 rapid toggle reversal missed final compact width";
                    app.exit(294);return;
                }
                toggleFromKeyboard();
            }
            QTest::qWait(reduce ? 25 : 275);
            if(window->property("sidebarCollapsed").toBool() ||
               sidebar->width()<210 || progress()>0.01 ||
               !toggle->hasActiveFocus() ||
               !fleetNav->property("currentWorkspace").toBool() ||
               window->property("workspace").toInt()!=1) {
                qCritical()<<"M1 expansion changed focus/selection or final geometry"
                           <<sidebar->width()<<progress();
                app.exit(295);return;
            }
            // M3-A: actual sidebar keyboard routing changes the StackLayout
            // and accessible heading immediately. Only the arrival opacity
            // is cosmetic; reduced motion must never fade the destination.
            auto *pages=find(find,window->contentItem(),"workspacePageStack");
            auto *rentalsNav=find(find,window->contentItem(),"workspaceNavRentals");
            auto *heading=find(find,window->contentItem(),"workspacePageHeading");
            if(!pages||!rentalsNav||!heading ||
               window->property("workspaceArrivalMs").toInt()!=(reduce?0:165)) {
                qCritical()<<"M3-A navigation transition controls or duration missing";
                app.exit(296);return;
            }
            const auto activateNav=[&](QQuickItem *nav) {
                nav->forceActiveFocus(Qt::TabFocusReason);
                QTest::keyClick(window,Qt::Key_Return);
                QCoreApplication::processEvents();
            };
            activateNav(rentalsNav);
            if(window->property("workspace").toInt()!=2 ||
               heading->property("text").toString()!="Rentals" ||
               !rentalsNav->property("currentWorkspace").toBool() ||
               !rentalsNav->hasActiveFocus()) {
                qCritical()<<"M3-A Rentals route/heading/focus did not update immediately";
                app.exit(297);return;
            }
            if(!reduce) {
                QTest::qWait(65);
                if(pages->opacity()<=0.73 || pages->opacity()>=0.995) {
                    qCritical()<<"M3-A no visible ordinary destination arrival"
                               <<pages->opacity();
                    app.exit(298);return;
                }
            } else if(pages->opacity()<0.999) {
                qCritical()<<"M3-A reduced-motion destination did not snap"
                           <<pages->opacity();
                app.exit(299);return;
            }
            QTest::qWait(reduce?20:200);
            if(pages->opacity()<0.999) {
                qCritical()<<"M3-A destination opacity did not finish"
                           <<pages->opacity();
                app.exit(300);return;
            }
            activateNav(fleetNav);
            if(window->property("workspace").toInt()!=1 ||
               !fleetNav->property("currentWorkspace").toBool())
            {
                qCritical()<<"M3-A immediate return to Vehicles failed";
                app.exit(301);return;
            }
            if(!reduce) QTest::qWait(40);
            activateNav(rentalsNav);
            QTest::qWait(reduce?20:210);
            if(window->property("workspace").toInt()!=2 ||
               !rentalsNav->property("currentWorkspace").toBool() ||
               pages->opacity()<0.999) {
                qCritical()<<"M3-A rapid page change failed to settle";
                app.exit(302);return;
            }
            activateNav(fleetNav);
            QTest::qWait(reduce?20:210);
            if(window->property("workspace").toInt()!=1 ||
               !fleetNav->property("currentWorkspace").toBool() ||
               pages->opacity()<0.999) {
                qCritical()<<"M3-A page return failed to restore full opacity";
                app.exit(303);return;
            }
            // M2: native mouse hover/press on the real shared buttons.
            // Business selection, keyboard focus and activation remain immediate.
            const auto m2Fail=[&](int code) {
                // GUI-subsystem Windows Qt suppresses qCritical() text in
                // CTest on failure. Encode the exact assertion in a genuine
                // Qt screenshot filename collected by the existing CI glob.
                const QString name=QStringLiteral("p4-qt-motion-m2-fail-%1-%2-%3.png")
                    .arg(code)
                    .arg(reduce?"reduced":"ordinary")
                    .arg(compact?"1280x720":"1500x860");
                window->grabWindow().save(name,"PNG");
                app.exit(code);
            };
            auto *primary=find(find,window->contentItem(),"sidebarNewRentalButton");
            auto *primarySurface=primary
                ? find(find,primary,"primaryActionFeedbackSurface") : nullptr;
            auto *overviewNav=find(find,window->contentItem(),"workspaceNavOverview");
            auto *overviewSurface=overviewNav
                ? find(find,overviewNav,"workspaceNavFeedbackSurface") : nullptr;
            if (!primary || !primarySurface || !overviewNav || !overviewSurface ||
                primary->property("hoverFeedbackMs").toInt()!=(reduce?0:100) ||
                primary->property("pressFeedbackMs").toInt()!=(reduce?0:70) ||
                overviewNav->property("hoverFeedbackMs").toInt()!=(reduce?0:100) ||
                overviewNav->property("pressFeedbackMs").toInt()!=(reduce?0:70)) {
                qCritical()<<"M2 feedback controls or motion policy missing";
                m2Fail(304);return;
            }
            const auto center=[&](QQuickItem *item)->QPoint {
                return item->mapToScene(QPointF(item->width()/2,
                                               item->height()/2)).toPoint();
            };
            const QPoint vacant(window->width()-6,window->height()-6);
            QTest::mouseMove(window,vacant);
            QTest::qWait(reduce?10:140);
            const auto paintedColor=[&](QQuickItem *item)->QColor {
                return item->property("color").value<QColor>();
            };
            const QColor primaryIdle=paintedColor(primarySurface);
            QTest::mouseMove(window,center(primary));
            QCoreApplication::processEvents();
            const QColor primaryHover=primary->property("feedbackSurface").value<QColor>();
            // M2 contrast guard: the sidebar action uses normal-size white
            // label text on this hover surface. Assert native token and 4.5:1
            // text contrast so later visual tweaks cannot silently regress it.
            const auto linearChannel=[](double v) {
                v/=255.0;
                return v<=0.04045 ? v/12.92 :
                       std::pow((v+0.055)/1.055,2.4);
            };
            const auto relativeLuminance=[&](const QColor &c) {
                return 0.2126*linearChannel(c.red()) +
                       0.7152*linearChannel(c.green()) +
                       0.0722*linearChannel(c.blue());
            };
            const double hoverContrast=1.05/(relativeLuminance(primaryHover)+0.05);
            if (!primary->property("hovered").toBool() ||
                primaryHover!=QColor("#07805F") ||
                hoverContrast<4.5) {
                qCritical()<<"M2 primary hover token or white-text contrast invalid"
                           <<primaryHover<<hoverContrast;
                m2Fail(305);return;
            }
            // Windows software-QPA frame delivery is not synchronised to an
            // arbitrary 40ms sleep. Sample successive REAL rendered colours
            // over the permitted 100ms animation instead of demanding one
            // exact frame time. Do not weaken the intermediate-state gate.
            bool sawIntermediate=false;
            QColor sampled=paintedColor(primarySurface);
            for (int sample=0;sample<12;++sample) {
                QTest::qWait(9);
                sampled=paintedColor(primarySurface);
                if (sampled!=primaryIdle && sampled!=primaryHover)
                    sawIntermediate=true;
            }
            if (reduce ? (sampled!=primaryHover) : !sawIntermediate) {
                qCritical()<<"M2 primary hover easing or reduced snap missing"
                           <<primaryIdle<<sampled<<primaryHover
                           <<"intermediate"<<sawIntermediate;
                m2Fail(306);return;
            }
            QTest::qWait(reduce?10:100);
            if (paintedColor(primarySurface)!=primaryHover) {
                qCritical()<<"M2 primary hover did not settle";
                m2Fail(307);return;
            }
            QTest::mouseMove(window,center(overviewNav));
            QTest::qWait(reduce?10:130);
            if (!overviewNav->property("hovered").toBool() ||
                overviewNav->property("feedbackSurface").value<QColor>()!=QColor("#263734") ||
                paintedColor(overviewSurface)!=QColor("#263734")) {
                qCritical()<<"M2 nav hover is not aligned to shell tokens";
                m2Fail(308);return;
            }
            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,
                              center(overviewNav));
            QCoreApplication::processEvents();
            if (!overviewNav->property("down").toBool() ||
                overviewNav->property("feedbackSurface").value<QColor>()!=QColor("#355048") ||
                window->property("workspace").toInt()!=1) {
                qCritical()<<"M2 pressed feedback delayed selection or domain";
                m2Fail(309);return;
            }
            // The Windows software-QPA failure capture sampled RGB(52,79,71)
            // just before the exact #355048 final frame. The 70ms animation
            // duration is a design target, not a guarantee of host frame
            // delivery within a single QTest::qWait(110) call. Keep STRICT
            // final RGBA equality and poll the actual rendered QML property.
            const QColor expectedPress("#355048");
            QColor observedPress=paintedColor(overviewSurface);
            if (!reduce) {
                // The timeout is bounded; a stuck or inaccurate color fails.
                // Reduced-motion is separately required to snap immediately.
                for (int tick=0;tick<18 && observedPress!=expectedPress;++tick) {
                    QTest::qWait(20);
                    observedPress=paintedColor(overviewSurface);
                }
            }
            if (observedPress!=expectedPress) {
                qCritical()<<"M2 pressed nav surface failed exact final RGBA"
                           <<observedPress<<expectedPress;
                m2Fail(310);return;
            }
            // Releasing outside the button must cancel, not route or book.
            QTest::mouseMove(window,vacant);
            QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,vacant);
            QCoreApplication::processEvents();
            if (overviewNav->property("down").toBool() ||
                window->property("workspace").toInt()!=1 ||
                !fleetNav->property("currentWorkspace").toBool()) {
                qCritical()<<"M2 abandoned press activated a workspace";
                m2Fail(311);return;
            }
            // Focus ring remains immediate and selected-ID updates remain
            // native C++ operations, without a cosmetic animation dependency.
            auto *gallery=find(find,window->contentItem(),"fleetGallery");
            if (!gallery) { app.exit(264);return; }
            gallery->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window,Qt::Key_Down);
            QCoreApplication::processEvents();
            if (fleet.selectedId().isEmpty() ||
                !frame->property("keyboardFocused").toBool()) {
                qCritical()<<"P4 motion setting affected keyboard selection";
                app.exit(265);return;
            }
            QByteArray vf,rf,temporary;
            if (!readFile(vehicles,&vf) || !readFile(rentals,&rf) ||
                !readFile(disposable.filePath("rentals.txt"),&temporary) ||
                vf!=beforeVehicles || rf!=beforeRentals ||
                temporary!=beforeRentals) {
                qCritical()<<"P4 motion test mutated fictional source/ledger";
                app.exit(266);return;
            }
            const QString size=compact ? "1280x720" : "1500x860";
            const QString mode=reduce ? "reduced" : "ordinary";
            const QImage image=window->grabWindow();
            if (image.isNull() ||
                image.size()!=QSize(compact?1280:1500,compact?720:860) ||
                !image.save("p4-qt-motion-"+mode+"-"+size+".png","PNG")) {
                qCritical()<<"P4 motion screenshot incorrect";
                app.exit(267);return;
            }
            qInfo()<<"P4 motion policy PASS" <<mode<<size
                   <<"duration" <<motion.selectionFadeMs()
                   <<"live change, instant focus, unmodified fixtures";
            app.exit(0);
        });
    }
    if (parser.isSet("dpi-smoke")) {
        auto *window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window) return 30;
        // 1024x660 logical: remains inside a 1920x1080 hosted desktop at 1.5x.
        window->resize(1024, 660);
        QTimer::singleShot(850, &app, [&, window] {
            bool valid = false;
            const double requested = qEnvironmentVariable("QT_SCALE_FACTOR").toDouble(&valid);
            const double actual = window->devicePixelRatio();
            const QImage shot = window->grabWindow();
            const QSize expected(qRound(window->width() * actual),
                                 qRound(window->height() * actual));
            if (!valid || requested < 1.2 || std::abs(actual-requested) > 0.02 ||
                shot.isNull() || std::abs(shot.width() - expected.width()) > 2 ||
                std::abs(shot.height() - expected.height()) > 2) {
                qCritical() << "Synthetic DPI image mismatch; requested" << requested
                            << "actual DPR" << actual << "image" << shot.size()
                            << "expected" << expected;
                app.exit(31); return;
            }
            const QString file = QString("p2-qt-synthetic-%1.png")
                                    .arg(QString::number(requested, 'f', 2));
            if (!shot.save(file, "PNG")) { app.exit(32); return; }
            qInfo() << "P2 synthetic DPI PASS" << requested << actual << shot.size();
            app.exit(0);
        });
    }
    return app.exec();
}
#include "main.moc"
