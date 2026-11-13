#pragma once
// P7-R: read-only operational summary of one generated-fictional SQLite snapshot.
// Never authorizes personal data, migration, or a commercial storage cutover.
#include <QObject>
#include <QSet>
#include <QString>
#include <QtGlobal>
#include <functional>
#include <limits>
#include <optional>
#include <utility>
#include "src/application/p7_sandbox/sqlite_repository.h"

namespace p7fiction {
struct OperationalValues {
    int totalVehicles=0;
    int availableVehicles=0;
    int activeRentals=0;
    int historyRentals=0;
    qint64 activeBookedCents=0;
};

class FictionalOperationsSnapshot final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool snapshotAvailable READ snapshotAvailable NOTIFY snapshotAvailabilityChanged)
    Q_PROPERTY(int totalVehicles READ totalVehicles NOTIFY metricsChanged)
    Q_PROPERTY(int availableVehicles READ availableVehicles NOTIFY metricsChanged)
    Q_PROPERTY(int activeRentals READ activeRentals NOTIFY metricsChanged)
    Q_PROPERTY(int historyRentals READ historyRentals NOTIFY metricsChanged)
    Q_PROPERTY(QString activeBookedLabel READ activeBookedLabel NOTIFY metricsChanged)
public:
    using Provider=std::function<std::optional<FleetRows>()>;
    explicit FictionalOperationsSnapshot(Provider provider, QObject *parent=nullptr)
        : QObject(parent), m_provider(std::move(provider)) {refresh();}
    bool snapshotAvailable() const {return m_available;}
    int totalVehicles() const {return m_values.totalVehicles;}
    int availableVehicles() const {return m_values.availableVehicles;}
    int activeRentals() const {return m_values.activeRentals;}
    int historyRentals() const {return m_values.historyRentals;}
    QString activeBookedLabel() const {
        const qint64 cents=m_values.activeBookedCents;
        return QString("R %1.%2").arg(cents/100).arg(cents%100,2,10,QChar('0'));
    }
    static std::optional<OperationalValues> project(const FleetRows &snapshot) {
        OperationalValues values;
        QSet<QString> occupied;
        for(const auto &r:snapshot.rentals) {
            if(r.status=="ACTIVE") {
                const QString id=r.vehicleId.toUpper();
                if(occupied.contains(id) || r.totalCents<=0 ||
                   values.activeBookedCents>
                       std::numeric_limits<qint64>::max()-r.totalCents)
                    return {};
                occupied.insert(id);
                ++values.activeRentals;
                values.activeBookedCents+=r.totalCents;
            } else if(r.status=="RETURNED" || r.status=="ARCHIVED") {
                ++values.historyRentals;
            } else return {};
        }
        for(const auto &v:snapshot.vehicles) {
            ++values.totalVehicles;
            if(v.state=="ACTIVE" && !occupied.contains(v.id.toUpper()))
                ++values.availableVehicles;
        }
        return values;
    }
    Q_INVOKABLE void refresh() {
        const auto loaded=m_provider?m_provider():std::optional<FleetRows>{};
        const auto values=loaded?project(*loaded):std::optional<OperationalValues>{};
        if(!values) {
            if(m_available) {
                m_available=false;
                emit snapshotAvailabilityChanged();
            }
            return;
        }
        const bool recovered=!m_available;
        m_values=*values;
        m_available=true;
        if(recovered)emit snapshotAvailabilityChanged();
        emit metricsChanged();
    }
signals:
    void snapshotAvailabilityChanged();
    void metricsChanged();
private:
    Provider m_provider;
    OperationalValues m_values;
    bool m_available=false;
};
} // namespace p7fiction
