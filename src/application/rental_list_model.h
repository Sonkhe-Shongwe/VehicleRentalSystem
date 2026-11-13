#ifndef RENTAL_STUDIO_RENTAL_LIST_MODEL_H
#define RENTAL_STUDIO_RENTAL_LIST_MODEL_H

#include "src/application/rental_read_service.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <optional>

// Native Qt read-only model. The service and its owning RentalSystem must
// outlive this adapter. No Rental* or customer PII is exposed through roles.
// The caller explicitly refreshes after an authoritative domain transaction;
// selection is stored by immutable-in-session rental ID, never an old row.
class RentalListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString selectedRentalId READ selectedRentalId NOTIFY selectionChanged)
    Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectionChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // False means the last provider read failed. Buffered rows/counts stay
    // intact for recovery, but role data, contract details and navigation are
    // denied until a successful new read.
    Q_PROPERTY(bool snapshotAvailable READ snapshotAvailable NOTIFY snapshotAvailabilityChanged)
    Q_PROPERTY(QString statusFilter READ statusFilter WRITE setStatusFilter NOTIFY statusFilterChanged)
    // Operator-only ID search; no contact fields or SQL queries. The full
    // C++ snapshot continues to determine all operational KPI metrics.
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    // Derived from the complete C++ snapshot, never the filtered Rentals view.
    Q_PROPERTY(int activeCount READ activeCount NOTIFY metricsChanged)
    Q_PROPERTY(int historyCount READ historyCount NOTIFY metricsChanged)
    Q_PROPERTY(int overdueCount READ overdueCount NOTIFY metricsChanged)
    Q_PROPERTY(int dueTodayCount READ dueTodayCount NOTIFY metricsChanged)
    Q_PROPERTY(double activeBookedValue READ activeBookedValue NOTIFY metricsChanged)
    Q_PROPERTY(QString oldestOverdueRentalId READ oldestOverdueRentalId NOTIFY metricsChanged)
    Q_PROPERTY(QVariantMap oldestOverduePreview READ oldestOverduePreview NOTIFY metricsChanged)
    // At most four latest rental START dates, then ID descending. Purpose-
    // limited maps: no customer/contact details, payment or event timestamps.
    Q_PROPERTY(QVariantList recentActivities READ recentActivities NOTIFY metricsChanged)
    // Only the current selected C++ rental summary. Never customer/phone.
    // Notified separately on selection AND domain-backed lifecycle refresh.
    Q_PROPERTY(QVariantMap selectedContract READ selectedContract NOTIFY selectedContractChanged)
public:
    enum Role {
        RentalIdRole = Qt::UserRole + 1,
        VehicleIdRole,
        StartDateRole,
        ExpectedReturnDateRole,
        ActualReturnDateRole,
        DaysRole,
        DailyRateRole,
        TotalRole,
        StatusRole,
        DueStateRole
    };

    explicit RentalListModel(const RentalReadService &service,
                             QObject *parent = nullptr);
    // P7-J compatibility seam: caller supplies validated, copy-only domain
    // snapshots. Nullopt means failed read, not an empty rental database.
    // Default legacy constructor and QML behavior remain unchanged.
    using SnapshotProvider = std::function<std::optional<QList<RentalRecordSummary>>()>;
    explicit RentalListModel(SnapshotProvider provider, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_items.size(); }
    bool snapshotAvailable() const { return m_snapshotAvailable; }
    int activeCount() const;
    int historyCount() const;
    int overdueCount() const;
    int dueTodayCount() const;
    double activeBookedValue() const;
    QString oldestOverdueRentalId() const;
    QVariantMap oldestOverduePreview() const;
    QVariantList recentActivities() const;
    QVariantMap selectedContract() const;
    QString statusFilter() const { return m_statusFilter; }
    void setStatusFilter(const QString &filter);
    QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    QString selectedRentalId() const { return m_selectedRentalId; }
    int selectedRow() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool selectById(const QString &rentalId);
    // Read-only operator lookup by exact rental ID or uniquely matched vehicle
    // ID across ALL lifecycle states. Multiple contracts for one vehicle
    // require an explicit rental ID; misses/ambiguity do not change selection.
    Q_INVOKABLE bool locateById(const QString &rentalOrVehicleId);
    // Same read-only lookup, returning one explicit machine-readable result:
    // Selected, Empty, NotFound, Ambiguous or Unavailable. No PII or writes.
    // Both entry points share exactly one authoritative resolution path.
    Q_INVOKABLE QString locateForOperator(const QString &rentalOrVehicleId);
    // Only selects a uniquely matched ACTIVE rental, identified by rental
    // ID or vehicle ID. Failed lookups disarm any previous return selection.
    // Never completes a return, reads contact details, or writes any file.
    Q_INVOKABLE bool locateActiveReturn(const QString &rentalOrVehicleId);
    Q_INVOKABLE bool selectVisibleRow(int row);
    // Navigate the oldest overdue active contract from the complete C++ snapshot.
    // Read/selection only: no booking, return, archive, or disk write.
    Q_INVOKABLE bool selectOldestOverdue();
    Q_INVOKABLE void clearSelection();

signals:
    void selectionChanged();
    void countChanged();
    void statusFilterChanged();
    void searchTextChanged();
    void metricsChanged();
    void snapshotAvailabilityChanged();
    void selectedContractChanged();

private:
    int rowForId(const QString &rentalId) const;
    QList<RentalRecordSummary> filtered(const QList<RentalRecordSummary> &all) const;

    SnapshotProvider m_recordsProvider;
    bool m_snapshotAvailable = false;
    QList<RentalRecordSummary> m_allItems;
    QList<RentalRecordSummary> m_items;
    QString m_statusFilter = QStringLiteral("All");
    QString m_searchText;
    QString m_selectedRentalId;
};

#endif
