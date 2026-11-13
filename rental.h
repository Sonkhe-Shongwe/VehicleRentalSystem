#ifndef RENTAL_H
#define RENTAL_H

#include <QString>
#include <QDate>

// Represents one customer rental transaction.
class Rental
{
private:
    QString rentalId;
    QString customerName;
    QString phoneNumber;
    QString vehicleId;

    QDate startDate;
    QDate expectedReturnDate;
    QDate actualReturnDate;

    int rentalDays;
    double pricePerDay;
    double totalCost;

    bool returned;
    bool archived;

public:
    Rental(const QString& rentalId,
           const QString& customerName,
           const QString& phoneNumber,
           const QString& vehicleId,
           const QDate& startDate,
           const QDate& expectedReturnDate,
           int rentalDays,
           double pricePerDay,
           double totalCost);

    QString getRentalId() const;
    QString getCustomerName() const;
    QString getPhoneNumber() const;
    QString getVehicleId() const;
    QDate getStartDate() const;
    QDate getExpectedReturnDate() const;
    QDate getActualReturnDate() const;
    int getRentalDays() const;
    double getPricePerDay() const;
    double getTotalCost() const;
    bool getIsReturned() const;
    bool getIsArchived() const;

    void markReturned(const QDate& actualReturnDate);
    void restoreActive();
    void setArchived(bool archived);

    QString statusText() const;
    QString toFileString() const;
};

#endif
