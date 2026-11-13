#pragma once
#include <QObject>
#include <QString>
#include "src/application/rental_admin_commands.h"

class RentalSystem;
class RentalListModel;
class RentalReadService;

// QML-facing presentation adapter, not a second storage/validation service.
// Existing ArchivedDeleteService remains the only operation entry point.
class P6ArchivedDeleteController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool canDeleteSelected READ canDeleteSelected NOTIFY stateChanged)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY stateChanged)
public:
    P6ArchivedDeleteController(RentalSystem &system,
                               const RentalReadService &reads,
                               RentalListModel &rentals, bool disposable,
                               QObject *parent=nullptr);
    bool canDeleteSelected() const;
    QString lastMessage() const { return m_message; }
    Q_INVOKABLE bool permanentlyDelete(const QString &rentalId,
                                       const QString &confirmation);
    Q_INVOKABLE void clearMessage();
signals:
    void stateChanged();
private:
    ArchivedDeleteService m_service;
    const RentalReadService &m_reads;
    RentalListModel &m_rentals;
    const bool m_disposable;
    QString m_message;
};
