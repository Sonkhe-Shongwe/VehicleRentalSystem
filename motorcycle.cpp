#include "motorcycle.h"

Motorcycle::Motorcycle(const QString& id,
                       const QString& brand,
                       const QString& model,
                       double pricePerDay,
                       int engineCapacityCC)
    : Vehicle(id, brand, model, pricePerDay),
      engineCapacityCC(engineCapacityCC)
{
}

int Motorcycle::getEngineCapacityCC() const
{
    return engineCapacityCC;
}

void Motorcycle::setEngineCapacityCC(int value)
{
    engineCapacityCC = value;
}

QString Motorcycle::typeName() const
{
    return "Motorcycle";
}

QString Motorcycle::extraDetails() const
{
    return QString("%1 CC").arg(engineCapacityCC);
}

QString Motorcycle::toFileString() const
{
    return QString("Motorcycle|%1|%2|%3|%4|%5|%6")
        .arg(getId(),
             getBrand(),
             getModel(),
             QString::number(getPricePerDay(), 'f', 2),
             getIsRented() ? "1" : "0",
             QString::number(engineCapacityCC));
}
