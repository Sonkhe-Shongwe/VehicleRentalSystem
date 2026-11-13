#include "src/application/rental_list_model.h"

#include <QVariant>
#include <QVariantMap>
#include <algorithm>
#include <utility>

namespace {
QString statusText(RentalRecordStatus status) {
    switch (status) {
    case RentalRecordStatus::Active:   return QStringLiteral("Active");
    case RentalRecordStatus::Returned: return QStringLiteral("Returned");
    case RentalRecordStatus::Archived: return QStringLiteral("Archived");
    }
    return QStringLiteral("Unknown");
}

QString dueStateText(RentalDueState due) {
    switch (due) {
    case RentalDueState::NotApplicable: return QStringLiteral("NotApplicable");
    case RentalDueState::NotDue:        return QStringLiteral("NotDue");
    case RentalDueState::DueToday:      return QStringLiteral("DueToday");
    case RentalDueState::Overdue:       return QStringLiteral("Overdue");
    case RentalDueState::Unknown:       return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}
}


// P5 overview metrics always use the *entire* authoritative read snapshot.
// A UI filter is a presentation choice, not an operational data boundary.
int RentalListModel::activeCount() const {
    int n=0;
    for(const auto &item:m_allItems)
        if(item.status==RentalRecordStatus::Active) ++n;
    return n;
}
int RentalListModel::historyCount() const {
    int n=0;
    for(const auto &item:m_allItems)
        if(item.status!=RentalRecordStatus::Active) ++n;
    return n;
}
int RentalListModel::overdueCount() const {
    int n=0;
    for(const auto &item:m_allItems)
        if(item.status==RentalRecordStatus::Active &&
           item.dueState==RentalDueState::Overdue) ++n;
    return n;
}
int RentalListModel::dueTodayCount() const {
    int n=0;
    for(const auto &item:m_allItems)
        if(item.status==RentalRecordStatus::Active &&
           item.dueState==RentalDueState::DueToday) ++n;
    return n;
}
double RentalListModel::activeBookedValue() const {
    double amount=0.0;
    for(const auto &item:m_allItems)
        if(item.status==RentalRecordStatus::Active)
            amount+=item.legacyTotal;
    // Contract totals are not actual payments, deposits or realized revenue.
    return amount;
}

QString RentalListModel::oldestOverdueRentalId() const {
    if (!m_snapshotAvailable)
        return {}; // Never route operators using a failed read's cached due state.
    const RentalRecordSummary *oldest=nullptr;
    for(const auto &item:m_allItems) {
        if(item.status!=RentalRecordStatus::Active ||
           item.dueState!=RentalDueState::Overdue)
            continue;
        if(!oldest || item.expectedReturnDate<oldest->expectedReturnDate ||
           (item.expectedReturnDate==oldest->expectedReturnDate &&
            item.rentalId<oldest->rentalId))
            oldest=&item;
    }
    return oldest ? oldest->rentalId : QString{};
}

QVariantMap RentalListModel::oldestOverduePreview() const {
    const QString id = oldestOverdueRentalId();
    if (id.isEmpty())
        return {};
    for (const auto &record : m_allItems) {
        if (record.rentalId == id)
            return {
                {QStringLiteral("rentalId"), record.rentalId},
                {QStringLiteral("vehicleId"), record.vehicleId},
                {QStringLiteral("expectedReturnDate"),
                    record.expectedReturnDate.toString(Qt::ISODate)}
            };
    }
    return {};
}

QVariantList RentalListModel::recentActivities() const {
    if (!m_snapshotAvailable)
        return {}; // Cached records are kept for recovery, not presented as verified.
    // This is latest *contract start date*, not an event/audit log. Do not
    // treat return/archive state changes as a new chronological event.
    QList<RentalRecordSummary> ordered=m_allItems;
    std::sort(ordered.begin(),ordered.end(),
        [](const RentalRecordSummary &a,const RentalRecordSummary &b) {
            if(a.startDate!=b.startDate) return a.startDate>b.startDate;
            return a.rentalId>b.rentalId; // Stable deterministic same-day tie.
        });
    QVariantList rows;
    const int count=std::min(4,static_cast<int>(ordered.size()));
    for(int i=0;i<count;++i) {
        const auto &record=ordered.at(i);
        QVariantMap row;
        row.insert(QStringLiteral("rentalId"),record.rentalId);
        row.insert(QStringLiteral("vehicleId"),record.vehicleId);
        row.insert(QStringLiteral("startDate"),record.startDate.toString(Qt::ISODate));
        row.insert(QStringLiteral("status"),statusText(record.status));
        row.insert(QStringLiteral("total"),record.legacyTotal);
        rows.append(row);
    }
    return rows;
}

QVariantMap RentalListModel::selectedContract() const {
    if (!m_snapshotAvailable || m_selectedRentalId.isEmpty() ||
        rowForId(m_selectedRentalId) < 0)
        return {}; // Never disclose stale or hidden rental selection.
    for (const auto &record : m_allItems) {
        if (record.rentalId != m_selectedRentalId)
            continue;
        // Explicit purpose-limited allowlist; no customer, phone, payments.
        return {
            {QStringLiteral("rentalId"),record.rentalId},
            {QStringLiteral("vehicleId"),record.vehicleId},
            {QStringLiteral("startDate"),record.startDate.toString(Qt::ISODate)},
            {QStringLiteral("expectedReturnDate"),record.expectedReturnDate.toString(Qt::ISODate)},
            {QStringLiteral("actualReturnDate"),record.actualReturnDate.isValid()
                ? record.actualReturnDate.toString(Qt::ISODate) : QString{}},
            {QStringLiteral("days"),record.rentalDays},
            {QStringLiteral("dailyRate"),record.legacyDailyRate},
            {QStringLiteral("total"),record.legacyTotal},
            {QStringLiteral("status"),statusText(record.status)},
            {QStringLiteral("dueState"),dueStateText(record.dueState)}
        };
    }
    return {};
}

RentalListModel::RentalListModel(const RentalReadService &service, QObject *parent)
    : RentalListModel([&service]() -> std::optional<QList<RentalRecordSummary>> {
          return service.records();
      }, parent) {}

RentalListModel::RentalListModel(SnapshotProvider provider, QObject *parent)
    : QAbstractListModel(parent), m_recordsProvider(std::move(provider)) {
    // A failed first read is not interpreted as a successfully loaded
    // business ledger. No production backend uses this constructor yet.
    if(m_recordsProvider) {
        auto records=m_recordsProvider();
        if(records) {
            m_allItems=std::move(*records);
            m_items=m_allItems;
            m_snapshotAvailable=true;
        }
    }
}

int RentalListModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_items.size();
}

QVariant RentalListModel::data(const QModelIndex &index, int role) const {
    if (!m_snapshotAvailable)
        return {}; // A retained row is not a verified record after read failure.
    if (!index.isValid() || index.model() != this || index.column() != 0 ||
        index.row() < 0 || index.row() >= m_items.size())
        return {};

    const auto &item = m_items.at(index.row());
    switch (role) {
    case RentalIdRole:           return item.rentalId;
    case VehicleIdRole:          return item.vehicleId;
    case StartDateRole:          return item.startDate;
    case ExpectedReturnDateRole: return item.expectedReturnDate;
    case ActualReturnDateRole:   return item.actualReturnDate;
    case DaysRole:               return item.rentalDays;
    case DailyRateRole:          return item.legacyDailyRate;
    case TotalRole:              return item.legacyTotal;
    case StatusRole:             return statusText(item.status);
    case DueStateRole:           return dueStateText(item.dueState);
    default:                     return {};
    }
}

QHash<int, QByteArray> RentalListModel::roleNames() const {
    return {
        {RentalIdRole, "rentalId"},
        {VehicleIdRole, "vehicleId"},
        {StartDateRole, "startDate"},
        {ExpectedReturnDateRole, "expectedReturnDate"},
        {ActualReturnDateRole, "actualReturnDate"},
        {DaysRole, "days"},
        {DailyRateRole, "dailyRate"},
        {TotalRole, "total"},
        {StatusRole, "status"},
        {DueStateRole, "dueState"}
    };
}


QList<RentalRecordSummary> RentalListModel::filtered(
        const QList<RentalRecordSummary> &all) const {
    if (m_statusFilter == QStringLiteral("All") && m_searchText.isEmpty())
        return all;
    QList<RentalRecordSummary> result;
    for (const auto &item : all) {
        const bool active = item.status == RentalRecordStatus::Active;
        const bool matchingStatus =
            m_statusFilter == QStringLiteral("All") ||
            (m_statusFilter == QStringLiteral("Active") && active) ||
            (m_statusFilter == QStringLiteral("Returned") &&
             item.status == RentalRecordStatus::Returned) ||
            (m_statusFilter == QStringLiteral("Archived") &&
             item.status == RentalRecordStatus::Archived) ||
            (m_statusFilter == QStringLiteral("History") && !active) ||
            (m_statusFilter == QStringLiteral("Overdue") && active &&
             item.dueState == RentalDueState::Overdue) ||
            (m_statusFilter == QStringLiteral("DueToday") && active &&
             item.dueState == RentalDueState::DueToday);
        // Search only stable, non-contact identifiers. Never filter using
        // customer name, phone, price, notes or unverified SQL text.
        const bool matchingId = m_searchText.isEmpty() ||
            item.rentalId.contains(m_searchText, Qt::CaseInsensitive) ||
            item.vehicleId.contains(m_searchText, Qt::CaseInsensitive);
        if (matchingStatus && matchingId)
            result.append(item);
    }
    return result;
}

void RentalListModel::setStatusFilter(const QString &filter) {
    // The C++ model, never QML, determines which lifecycle flags are history.
    if (filter != QStringLiteral("All") &&
        filter != QStringLiteral("Active") &&
        filter != QStringLiteral("Returned") &&
        filter != QStringLiteral("Archived") &&
        filter != QStringLiteral("History") &&
        filter != QStringLiteral("Overdue") &&
        filter != QStringLiteral("DueToday"))
        return;
    if (filter == m_statusFilter)
        return;
    const int oldCount = m_items.size();
    const int oldRow = selectedRow();
    const QString oldId = m_selectedRentalId;
    m_statusFilter = filter;
    QList<RentalRecordSummary> visible = filtered(m_allItems);
    beginResetModel();
    m_items = std::move(visible);
    if (rowForId(oldId) < 0)
        m_selectedRentalId.clear();
    endResetModel();
    emit statusFilterChanged();
    if (m_items.size() != oldCount)
        emit countChanged();
    if (oldId != m_selectedRentalId || oldRow != selectedRow()) {
        emit selectionChanged();
        emit selectedContractChanged();
    }
}

void RentalListModel::setSearchText(const QString &text) {
    // A bounded, read-only filter for verified rental and vehicle IDs.
    const QString normalized = text.trimmed().left(80);
    if (normalized == m_searchText)
        return;
    const int oldCount = m_items.size();
    const int oldRow = selectedRow();
    const QString oldId = m_selectedRentalId;
    m_searchText = normalized;
    QList<RentalRecordSummary> visible = filtered(m_allItems);
    beginResetModel();
    m_items = std::move(visible);
    if (rowForId(oldId) < 0)
        m_selectedRentalId.clear();
    endResetModel();
    emit searchTextChanged();
    if (m_items.size() != oldCount)
        emit countChanged();
    if (oldId != m_selectedRentalId || oldRow != selectedRow()) {
        emit selectionChanged();
        emit selectedContractChanged();
    }
}

bool RentalListModel::selectVisibleRow(int row) {
    if (!m_snapshotAvailable || row < 0 || row >= m_items.size())
        return false;
    return selectById(m_items.at(row).rentalId);
}

bool RentalListModel::selectOldestOverdue() {
    if (!m_snapshotAvailable)
        return false;
    const QString id=oldestOverdueRentalId();
    if(id.isEmpty())return false;
    // The user may currently be viewing History. Restore Active before
    // selecting, without ever mutating the underlying rental domain.
    setStatusFilter(QStringLiteral("Active"));
    return selectById(id);
}

int RentalListModel::rowForId(const QString &id) const {
    const QString normalized = id.trimmed();
    if (normalized.isEmpty())
        return -1;
    for (int row = 0; row < m_items.size(); ++row) {
        if (m_items.at(row).rentalId.compare(normalized, Qt::CaseInsensitive) == 0)
            return row;
    }
    return -1;
}

int RentalListModel::selectedRow() const {
    return rowForId(m_selectedRentalId);
}

bool RentalListModel::selectById(const QString &id) {
    if (!m_snapshotAvailable)
        return false;
    const int row = rowForId(id);
    if (row < 0)
        return false; // An unknown or hidden ID never changes current selection.
    const QString canonicalId = m_items.at(row).rentalId;
    if (canonicalId != m_selectedRentalId) {
        m_selectedRentalId = canonicalId;
        emit selectionChanged();
        emit selectedContractChanged();
    }
    return true;
}

bool RentalListModel::locateById(const QString &rentalOrVehicleId) {
    return locateForOperator(rentalOrVehicleId) == QStringLiteral("Selected");
}

QString RentalListModel::locateForOperator(const QString &rentalOrVehicleId) {
    // Do not mistake a failed read for an empty or missing ledger.
    if (!m_snapshotAvailable)
        return QStringLiteral("Unavailable");
    const QString requested = rentalOrVehicleId.trimmed();
    if (requested.isEmpty())
        return QStringLiteral("Empty");
    QString canonical;
    // An exact rental ID wins even when the same input could be a vehicle
    // identifier. Never guess which historical contract belongs to a vehicle.
    for (const auto &item : m_allItems) {
        if (item.rentalId.compare(requested, Qt::CaseInsensitive) == 0) {
            canonical = item.rentalId;
            break;
        }
    }
    if (canonical.isEmpty()) {
        for (const auto &item : m_allItems) {
            if (item.vehicleId.compare(requested, Qt::CaseInsensitive) != 0)
                continue;
            if (!canonical.isEmpty())
                return QStringLiteral("Ambiguous"); // Require a rental ID.
            canonical = item.rentalId;
        }
    }
    if (canonical.isEmpty())
        return QStringLiteral("NotFound"); // Never disturb selection/filter.
    // Explicit lookup must also reveal a record hidden by the optional
    // search text, not just an excluding lifecycle filter.
    if (rowForId(canonical) < 0 && !m_searchText.isEmpty())
        setSearchText(QString{});
    if (rowForId(canonical) < 0)
        setStatusFilter(QStringLiteral("All"));
    return selectById(canonical)
        ? QStringLiteral("Selected") : QStringLiteral("Unavailable");
}

bool RentalListModel::locateActiveReturn(const QString &identifier) {
    if (!m_snapshotAvailable)
        return false; // Preserve cached selection for recovery, but deny actions.
    const QString id = identifier.trimmed();
    QString matchedRental;
    int matchCount = 0;
    if (!id.isEmpty()) {
        for (const auto &item : m_allItems) {
            if (item.status != RentalRecordStatus::Active)
                continue;
            if (item.rentalId.compare(id, Qt::CaseInsensitive) == 0 ||
                item.vehicleId.compare(id, Qt::CaseInsensitive) == 0) {
                matchedRental = item.rentalId;
                ++matchCount;
                if (matchCount > 1)
                    break; // Refuse ambiguous or inconsistent active records.
            }
        }
    }
    if (matchCount != 1) {
        clearSelection(); // Explicitly disarm a previously eligible return.
        return false;     // Failed lookup cannot mutate filter or storage.
    }
    // A successful explicit return lookup reveals only active contracts,
    // including those hidden by an optional read-only ID search.
    setStatusFilter(QStringLiteral("Active"));
    if (rowForId(matchedRental) < 0 && !m_searchText.isEmpty())
        setSearchText(QString{});
    return selectById(matchedRental);
}

void RentalListModel::clearSelection() {
    if (m_selectedRentalId.isEmpty())
        return;
    m_selectedRentalId.clear();
    emit selectionChanged();
    emit selectedContractChanged();
}

void RentalListModel::refresh() {
    // No signals are emitted by RentalSystem when it changes. A future
    // command controller must invoke refresh() only after domain state is
    // actually committed. Model reset rebinds QML/list views to latest values.
    const int priorCount = m_items.size();
    const int priorRow = selectedRow();
    const QString priorId = m_selectedRentalId;
    // A failed SQL read must never erase a valid existing ListModel
    // snapshot or emit false post-commit metric change notifications.
    const auto loaded=m_recordsProvider ? m_recordsProvider() :
        std::optional<QList<RentalRecordSummary>>{};
    if(!loaded) {
        if(m_snapshotAvailable) {
            m_snapshotAvailable=false;
            emit snapshotAvailabilityChanged();
            emit selectedContractChanged(); // Revoke previously exposed detail.
        }
        return; // Do not reset, emit metrics, or imply an empty ledger.
    }
    const bool recovered=!m_snapshotAvailable;
    m_snapshotAvailable=true;
    QList<RentalRecordSummary> latest=std::move(*loaded);
    QList<RentalRecordSummary> visible = filtered(latest);

    beginResetModel();
    m_allItems = std::move(latest);
    m_items = std::move(visible);
    if (rowForId(priorId) < 0)
        m_selectedRentalId.clear();
    endResetModel();

    if (m_items.size() != priorCount)
        emit countChanged();
    if (priorId != m_selectedRentalId || selectedRow() != priorRow)
        emit selectionChanged();
    // Lifecycle/dates can change while the selected rental ID stays the same.
    // Force the separate contract view to rebind after authoritative refresh.
    emit selectedContractChanged();
    if(recovered)emit snapshotAvailabilityChanged();
    // Emitted only after the caller's authoritative persisted transaction
    // requests refresh(). Filters and unconfirmed previews never alter KPIs.
    emit metricsChanged();
}
