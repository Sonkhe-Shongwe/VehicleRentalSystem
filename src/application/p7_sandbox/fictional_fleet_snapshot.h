#pragma once
// P7-P: generated-fictional Fleet read/selection model. Linked into the
// actual Qt Quick executable ONLY under the OFF-by-default SQLite Lab build;
// it never authorizes real customer data, migration or commercial cutover.
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QList>
#include <QString>
#include <functional>
#include <optional>
#include <utility>
#include "src/application/fleet_read_service.h"

namespace p7fiction {
class FictionalFleetSnapshot final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool snapshotAvailable READ snapshotAvailable NOTIFY snapshotAvailabilityChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY snapshotChanged)
    Q_PROPERTY(int availableCount READ availableCount NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantList vehicles READ vehicles NOTIFY snapshotChanged)
    // Read-only Fleet inspection uses the same verified C++ values, not
    // QML-side occupancy or status rules. Never returns stale DTOs as current.
    Q_PROPERTY(QString statusFilter READ statusFilter WRITE setStatusFilter NOTIFY viewChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY viewChanged)
    Q_PROPERTY(QVariantList visibleVehicles READ visibleVehicles NOTIFY viewChanged)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY viewChanged)
    Q_PROPERTY(QString selectedVehicleId READ selectedVehicleId NOTIFY viewChanged)
    Q_PROPERTY(QVariantMap selectedVehicle READ selectedVehicle NOTIFY viewChanged)
public:
    using Provider=std::function<std::optional<QList<FleetVehicleSummary>>()>;
    explicit FictionalFleetSnapshot(Provider provider, QObject *parent=nullptr)
        : QObject(parent), m_provider(std::move(provider)) {refresh();}
    bool snapshotAvailable() const {return m_available;}
    int totalCount() const {return m_rows.size();}
    int availableCount() const {
        int count=0;
        for(const auto &r:m_rows)if(r.available)++count;
        return count;
    }
    static QVariantMap valueMap(const FleetVehicleSummary &r) {
        return {
            {"id",r.id}, {"displayName",r.displayName()},
            {"type",r.type}, {"details",r.details},
            {"dailyRate",r.legacyDailyRate}, {"available",r.available}
        };
    }
    QVariantList vehicles() const {
        QVariantList result;
        for(const auto &r:m_rows)result.append(valueMap(r));
        return result;
    }
    QString statusFilter() const {return m_filter;}
    QString searchText() const {return m_search;}
    QString selectedVehicleId() const {return m_selectedId;}
    bool passes(const FleetVehicleSummary &r) const {
        const bool statusMatches=m_filter=="All" ||
            (m_filter=="Available" && r.available) ||
            (m_filter=="Unavailable" && !r.available);
        if(!statusMatches)return false;
        // Only verified C++ DTO values participate; no SQL reaches QML.
        return m_search.isEmpty() ||
            r.id.contains(m_search,Qt::CaseInsensitive) ||
            r.displayName().contains(m_search,Qt::CaseInsensitive) ||
            r.type.contains(m_search,Qt::CaseInsensitive) ||
            r.details.contains(m_search,Qt::CaseInsensitive);
    }
    QVariantList visibleVehicles() const {
        QVariantList result;
        if(!m_available)return result;
        for(const auto &r:m_rows)if(passes(r))result.append(valueMap(r));
        return result;
    }
    int visibleCount() const {
        if(!m_available)return 0;
        int count=0;
        for(const auto &r:m_rows)if(passes(r))++count;
        return count;
    }
    QVariantMap selectedVehicle() const {
        if(!m_available)return {};
        for(const auto &r:m_rows)
            if(passes(r) && r.id.compare(m_selectedId,Qt::CaseInsensitive)==0)
                return valueMap(r);
        return {};
    }
    Q_INVOKABLE void setStatusFilter(const QString &filter) {
        if(filter!="All" && filter!="Available" && filter!="Unavailable")return;
        if(m_filter==filter)return;
        m_filter=filter;
        if(selectedVehicle().isEmpty())m_selectedId.clear();
        emit viewChanged();
    }
    Q_INVOKABLE void setSearchText(const QString &text) {
        const QString query=text.trimmed();
        if(query.size()>80 || m_search==query)return;
        m_search=query;
        if(selectedVehicle().isEmpty())m_selectedId.clear();
        emit viewChanged();
    }
    Q_INVOKABLE bool selectById(const QString &id) {
        if(!m_available || id.trimmed().isEmpty())return false;
        for(const auto &r:m_rows) {
            if(passes(r) && r.id.compare(id.trimmed(),Qt::CaseInsensitive)==0) {
                if(m_selectedId!=r.id) {
                    m_selectedId=r.id;
                    emit viewChanged();
                }
                return true;
            }
        }
        return false; // Invalid or hidden selection must not displace valid ID.
    }
    Q_INVOKABLE void clearSelection() {
        if(m_selectedId.isEmpty())return;
        m_selectedId.clear();
        emit viewChanged();
    }
    Q_INVOKABLE void refresh() {
        auto rows=m_provider?m_provider():std::nullopt;
        if(!rows) {
            // Preserve the last verified snapshot internally. UI hides all
            // potentially stale counts until the operator explicitly retries.
            if(m_available) {
                m_available=false;
                emit snapshotAvailabilityChanged();
                emit viewChanged();
            }
            return;
        }
        const bool wasAvailable=m_available;
        m_rows=std::move(*rows);
        // A returned/retired asset can leave the selected filter; do not
        // retain a hidden or non-existent inspector record.
        m_available=true;
        if(selectedVehicle().isEmpty())m_selectedId.clear();
        emit viewChanged();
        if(!wasAvailable) {
            m_available=true;
            emit snapshotAvailabilityChanged();
        }
        emit snapshotChanged();
    }
signals:
    void snapshotChanged();
    void snapshotAvailabilityChanged();
    void viewChanged();
private:
    Provider m_provider;
    bool m_available=false;
    QList<FleetVehicleSummary> m_rows;
    QString m_filter="All";
    QString m_search;
    QString m_selectedId;
};
} // namespace p7fiction
