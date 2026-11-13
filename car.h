#ifndef CAR_H
#define CAR_H

#include "vehicle.h"

class Car : public Vehicle
{
private:
    int numberOfDoors;

public:
    Car(const QString& id,
        const QString& brand,
        const QString& model,
        double pricePerDay,
        int numberOfDoors);

    int getNumberOfDoors() const;
    void setNumberOfDoors(int numberOfDoors);

    QString typeName() const override;
    QString extraDetails() const override;
    QString toFileString() const override;
};

#endif
