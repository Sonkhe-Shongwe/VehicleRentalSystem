#include "rental.h"

Rental::Rental(const QString& rentalId,
               const QString& customerName,
               const QString& phoneNumber,
               const QString& vehicleId,
               const QDate& startDate,
               const QDate& expectedReturnDate,
               int rentalDays,
               double pricePerDay,
               double totalCost)
    : rentalId(rentalId),
      customerName(customerName),
      phoneNumber(phoneNumber),
      vehicleId(vehicleId),
      startDate(startDate),
      expectedReturnDate(expectedReturnDate),
      rentalDays(rentalDays),
      pricePerDay(pricePerDay),
      totalCost(totalCost),
      returned(false),
      archived(false)
{
}

QString Rental::getRentalId() const
{
    return rentalId;
}

QString Rental::getCustomerName() const
{
    return customerName;
}

QString Rental::getPhoneNumber() const
{
    return phoneNumber;
}

QString Rental::getVehicleId() const
{
    return vehicleId;
}

QDate Rental::getStartDate() const
{
    return startDate;
}

QDate Rental::getExpectedReturnDate() const
{
    return expectedReturnDate;
}

QDate Rental::getActualReturnDate() const
{
    return actualReturnDate;
}

int Rental::getRentalDays() const
{
    return rentalDays;
}

double Rental::getPricePerDay() const
{
    return pricePerDay;
}

double Rental::getTotalCost() const
{
    return totalCost;
}

bool Rental::getIsReturned() const
{
    return returned;
}

bool Rental::getIsArchived() const
{
    return archived;
}

void Rental::markReturned(const QDate& date)
{
    returned = true;
    actualReturnDate = date;
}

void Rental::restoreActive()
{
    returned = false;
    actualReturnDate = QDate();
}

void Rental::setArchived(bool value)
{
    archived = value;
}

QString Rental::statusText() const
{
    if (archived) {
        return "Archived";
    }

    return returned ? "Returned" : "Active";
}

QString Rental::toFileString() const
{
    const QString actualDateText = actualReturnDate.isValid()
        ? actualReturnDate.toString(Qt::ISODate)
        : QString();

    return QString("Rental|%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12")
        .arg(rentalId)
        .arg(customerName)
        .arg(phoneNumber)
        .arg(vehicleId)
        .arg(startDate.toString(Qt::ISODate))
        .arg(expectedReturnDate.toString(Qt::ISODate))
        .arg(actualDateText)
        .arg(rentalDays)
        .arg(QString::number(pricePerDay, 'f', 2))
        .arg(QString::number(totalCost, 'f', 2))
        .arg(returned ? "1" : "0")
        .arg(archived ? "1" : "0");
}
