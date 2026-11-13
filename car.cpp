#include "car.h"

Car::Car(const QString& id,
         const QString& brand,
         const QString& model,
         double pricePerDay,
         int numberOfDoors)
    : Vehicle(id, brand, model, pricePerDay),
      numberOfDoors(numberOfDoors)
{
}

int Car::getNumberOfDoors() const
{
    return numberOfDoors;
}

void Car::setNumberOfDoors(int value)
{
    numberOfDoors = value;
}

QString Car::typeName() const
{
    return "Car";
}

QString Car::extraDetails() const
{
    return QString("%1 doors").arg(numberOfDoors);
}

QString Car::toFileString() const
{
    return QString("Car|%1|%2|%3|%4|%5|%6")
        .arg(getId(),
             getBrand(),
             getModel(),
             QString::number(getPricePerDay(), 'f', 2),
             getIsRented() ? "1" : "0",
             QString::number(numberOfDoors));
}
