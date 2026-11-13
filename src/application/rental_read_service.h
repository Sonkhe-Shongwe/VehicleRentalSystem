#ifndef RENTAL_STUDIO_RENTAL_READ_SERVICE_H
#define RENTAL_STUDIO_RENTAL_READ_SERVICE_H

#include <QDate>
#include <QList>
#include <QString>
#include <functional>
#include <optional>

class RentalSystem;

// Archived is a distinct historical state, not an active or merely returned
// record. Overdue is a derived due state, never an additional booking status.
enum class RentalRecordStatus { Active, Returned, Archived };
enum class RentalDueState { NotApplicable, NotDue, DueToday, Overdue, Unknown };

// Copy-only, read-only summary. Deliberately exclude customer name and phone;
// sensitive contact details need a separately reviewed, purpose-limited path.
struct RentalRecordSummary {
    QString rentalId;
    QString vehicleId;
    QDate startDate;
    QDate expectedReturnDate;
    QDate actualReturnDate;
    int rentalDays = 0;
    double legacyDailyRate = 0.0;
    double legacyTotal = 0.0;
    RentalRecordStatus status = RentalRecordStatus::Active;
    RentalDueState dueState = RentalDueState::Unknown;
};

// Borrows a domain with longer lifetime; returned summaries own only values.
// A new query reflects current C++ state, but old snapshots remain unchanged.
// Never owns or mutates rentals, vehicle availability, or persisted files.
class RentalReadService final {
public:
    using Today = std::function<QDate()>;
    explicit RentalReadService(const RentalSystem &system,
                               Today today = {});

    QList<RentalRecordSummary> records() const;
    QList<RentalRecordSummary> recordsByStatus(RentalRecordStatus status) const;
    std::optional<RentalRecordSummary> recordById(const QString &id) const;

private:
    const RentalSystem &m_system;
    Today m_today;
};

#endif
