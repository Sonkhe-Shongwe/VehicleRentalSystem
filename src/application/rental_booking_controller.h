#ifndef RENTAL_STUDIO_BOOKING_CONTROLLER_H
#define RENTAL_STUDIO_BOOKING_CONTROLLER_H

#include "src/application/rental_quote_service.h"
#include "src/application/rental_confirm_service.h"
#include <QObject>
#include <QString>
#include <functional>
#include <optional>

class RentalSystem;
class RentalListModel;

// QML view state, not a new pricing or persistence engine. Domain services
// validate and commit every decision; dates/rates never come from QML.
class RentalBookingController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool canConfirm READ canConfirm NOTIFY quoteChanged)
    Q_PROPERTY(bool needsFreshQuote READ needsFreshQuote NOTIFY quoteChanged)
    Q_PROPERTY(QString reviewedVehicleId READ reviewedVehicleId NOTIFY quoteChanged)
    Q_PROPERTY(QString quoteSummary READ quoteSummary NOTIFY quoteChanged)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY messageChanged)
    Q_PROPERTY(QString lastRentalId READ lastRentalId NOTIFY messageChanged)
public:
    RentalBookingController(RentalSystem &system, RentalListModel &rentals,
                            std::function<void()> refreshFleet,
                            QObject *parent = nullptr,
                            RentalQuoteService::Today today =
                                {});

    void setPersistentFictionalOnly(bool active) { m_persistentFictionalOnly=active; }
    bool canConfirm() const { return m_reviewed.has_value(); }
    bool needsFreshQuote() const { return m_needsFreshQuote; }
    QString reviewedVehicleId() const {
        return m_reviewed ? m_reviewed->vehicleId : QString();
    }
    QString quoteSummary() const;
    QString lastMessage() const { return m_message; }
    QString lastRentalId() const { return m_lastRentalId; }

    Q_INVOKABLE bool preview(const QString &vehicleId, int days);
    Q_INVOKABLE bool confirm(const QString &customerName,
                             const QString &phoneNumber);
    Q_INVOKABLE void discardPreview();

signals:
    void quoteChanged();
    void messageChanged();
    void bookingSucceeded(const QString &rentalId);
    void bookingRejected(RentalConfirmationError error);

private:
    RentalQuoteService m_quotes;
    RentalConfirmService m_confirm;
    RentalListModel &m_rentals;
    std::function<void()> m_refreshFleet;
    bool m_persistentFictionalOnly=false;
    bool m_needsFreshQuote=false;
    std::optional<RentalQuotePreview> m_reviewed;
    QString m_message;
    QString m_lastRentalId;
};
#endif
