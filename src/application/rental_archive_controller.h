#ifndef RENTAL_STUDIO_RENTAL_ARCHIVE_CONTROLLER_H
#define RENTAL_STUDIO_RENTAL_ARCHIVE_CONTROLLER_H

#include "src/application/rental_read_service.h"
#include <QObject>
#include <QString>

class RentalSystem;
class RentalListModel;

enum class RentalArchiveError {
    None, MissingRental, NotReturned, AlreadyArchived, CommitFailed
};
struct RentalArchiveResult {
    RentalArchiveError error = RentalArchiveError::None;
    QString rentalId;
    bool ok() const { return error == RentalArchiveError::None && !rentalId.isEmpty(); }
};

// A non-destructive typed command. Only the domain archives a returned
// contract using its existing single-ledger QSaveFile and rollback behavior.
class RentalArchiveService final {
public:
    RentalArchiveService(RentalSystem &system, const RentalReadService &reads);
    RentalArchiveResult archiveById(const QString &id);
private:
    RentalSystem &m_system;
    const RentalReadService &m_reads;
};

class RentalArchiveController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool canArchiveSelected READ canArchiveSelected NOTIFY stateChanged)
    Q_PROPERTY(QString lastArchiveMessage READ lastArchiveMessage NOTIFY stateChanged)
public:
    RentalArchiveController(RentalSystem &system,
                            const RentalReadService &reads,
                            RentalListModel &rentals,
                            QObject *parent = nullptr);
    bool canArchiveSelected() const;
    QString lastArchiveMessage() const { return m_message; }
    Q_INVOKABLE bool archiveSelected();
    // Exact-ID guard for the contract reviewed in the confirmation dialog.
    Q_INVOKABLE bool archiveSelectedFor(const QString &expectedRentalId);
signals:
    void stateChanged();
    void archiveSucceeded(const QString &rentalId);
    void archiveRejected(RentalArchiveError error);
private:
    RentalArchiveService m_service;
    const RentalReadService &m_reads;
    RentalListModel &m_rentals;
    QString m_message;
};
#endif
