#ifndef RENTAL_STUDIO_RENTAL_RETURN_CONTROLLER_H
#define RENTAL_STUDIO_RENTAL_RETURN_CONTROLLER_H

#include "src/application/rental_read_service.h"
#include <QDate>
#include <QObject>
#include <QString>
#include <functional>

class RentalSystem;
class RentalListModel;

enum class RentalReturnError {
    None, MissingRental, NotActive, InvalidReturnDate, CommitFailed
};

struct RentalReturnResult {
    RentalReturnError error = RentalReturnError::None;
    QString rentalId;
    bool ok() const { return error == RentalReturnError::None && !rentalId.isEmpty(); }
};

// Typed domain command: uses RentalSystem::completeRental, the sole writer.
// Today is injectable for preflight, but the legacy system independently
// checks real calendar date and durable QSaveFile success during the commit.
class RentalReturnService final {
public:
    using Today = std::function<QDate()>;
    RentalReturnService(RentalSystem &system, const RentalReadService &reads,
                        Today today = {});
    RentalReturnResult returnById(const QString &rentalId);

private:
    RentalSystem &m_system;
    const RentalReadService &m_reads;
    Today m_today;
};

// Qt Quick-facing seam, not a second domain writer. Borrows system, read
// service and model. Callback is the existing FleetController refresh,
// called only after a persisted successful return. No contact data exposed.
class RentalReturnController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool canReturnSelected READ canReturnSelected NOTIFY stateChanged)
    Q_PROPERTY(QString lastReturnMessage READ lastReturnMessage NOTIFY stateChanged)
public:
    RentalReturnController(RentalSystem &system, const RentalReadService &reads,
                           RentalListModel &rentals,
                           std::function<void()> refreshFleet,
                           QObject *parent = nullptr,
                           RentalReturnService::Today today =
                               {});

    bool canReturnSelected() const;
    QString lastReturnMessage() const { return m_lastMessage; }
    Q_INVOKABLE bool returnSelected();
    // Guard the original ID captured when the confirmation dialog opened.
    // A newly selected eligible contract must never inherit old consent.
    Q_INVOKABLE bool returnSelectedFor(const QString &expectedRentalId);

signals:
    void stateChanged();
    void returnSucceeded(const QString &rentalId);
    void returnRejected(RentalReturnError error);

private:
    RentalReturnService m_service;
    const RentalReadService &m_reads;
    RentalListModel &m_rentals;
    std::function<void()> m_refreshFleet;
    QString m_lastMessage;
};

#endif
