#ifndef MOTORCYCLE_H
#define MOTORCYCLE_H

#include "vehicle.h"

class Motorcycle : public Vehicle
{
private:
    int engineCapacityCC;

public:
    Motorcycle(const QString& id,
               const QString& brand,
               const QString& model,
               double pricePerDay,
               int engineCapacityCC);

    int getEngineCapacityCC() const;
    void setEngineCapacityCC(int engineCapacityCC);

    QString typeName() const override;
    QString extraDetails() const override;
    QString toFileString() const override;
};

#endif
