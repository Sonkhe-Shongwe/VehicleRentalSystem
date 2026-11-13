#ifndef VEHICLE_H
#define VEHICLE_H

#include <QString>

// Abstract base class shared by every vehicle type in the fleet.
class Vehicle
{
private:
    QString id;
    QString brand;
    QString model;
    double pricePerDay;
    bool isRented;

public:
    Vehicle(const QString& id,
            const QString& brand,
            const QString& model,
            double pricePerDay);
    virtual ~Vehicle();

    QString getId() const;
    QString getBrand() const;
    QString getModel() const;
    double getPricePerDay() const;
    bool getIsRented() const;

    void setBrand(const QString& brand);
    void setModel(const QString& model);
    void setPricePerDay(double pricePerDay);
    void setIsRented(bool rented);

    virtual QString typeName() const = 0;
    virtual QString extraDetails() const = 0;
    virtual QString toFileString() const = 0;
};

#endif
