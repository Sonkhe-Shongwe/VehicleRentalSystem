#include "vehicle.h"

Vehicle::Vehicle(const QString& id,
                 const QString& brand,
                 const QString& model,
                 double pricePerDay)
    : id(id),
      brand(brand),
      model(model),
      pricePerDay(pricePerDay),
      isRented(false)
{
}

Vehicle::~Vehicle() = default;

QString Vehicle::getId() const
{
    return id;
}

QString Vehicle::getBrand() const
{
    return brand;
}

QString Vehicle::getModel() const
{
    return model;
}

double Vehicle::getPricePerDay() const
{
    return pricePerDay;
}

bool Vehicle::getIsRented() const
{
    return isRented;
}

void Vehicle::setBrand(const QString& value)
{
    brand = value;
}

void Vehicle::setModel(const QString& value)
{
    model = value;
}

void Vehicle::setPricePerDay(double value)
{
    pricePerDay = value;
}

void Vehicle::setIsRented(bool rented)
{
    isRented = rented;
}
