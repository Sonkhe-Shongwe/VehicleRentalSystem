#pragma once
#include <QObject>
#include <QString>
#include <functional>
#include "src/application/rental_admin_commands.h"

class RentalSystem;

// Presentation-only command: the existing domain owns the transaction.
// A fresh canonical vehicle ID and an exact confirmation phrase are required.
class FleetRemoveController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY messageChanged)
    Q_PROPERTY(bool canRemove READ canRemove CONSTANT)
public:
    FleetRemoveController(RentalSystem &system, bool disposable,
                          std::function<void()> afterCommit,
                          QObject *parent=nullptr);
    QString lastMessage() const { return m_message; }
    bool canRemove() const { return m_disposable; }
    // May remove only when the domain has no surviving rental references.
    Q_INVOKABLE bool canRemoveVehicle(const QString &id) const;
    Q_INVOKABLE bool hasRentalHistory(const QString &id) const;
    Q_INVOKABLE bool removeVehicle(const QString &id,const QString &confirmation);
    Q_INVOKABLE void clearMessage();
signals:
    void messageChanged();
private:
    FleetWriteService m_writer;
    RentalSystem &m_system;
    bool m_disposable=false;
    std::function<void()> m_afterCommit;
    QString m_message;
};
