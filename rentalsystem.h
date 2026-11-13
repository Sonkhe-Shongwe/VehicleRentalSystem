#ifndef RENTALSYSTEM_H
#define RENTALSYSTEM_H

#include <QList>
#include <QString>
#include <QDate>
#include <QByteArray>
#include <functional>
#include <memory>
#include <optional>

#include "vehicle.h"
#include "car.h"
#include "motorcycle.h"
#include "rental.h"

// Small injectable I/O boundary. Implementations must offer atomic replace
// semantics for each independent legacy text file. The default uses QSaveFile.
class RentalStorage {
public:
    virtual ~RentalStorage() = default;
    virtual std::optional<QByteArray> read(const QString &path) const = 0;
    virtual bool writeAtomic(const QString &path, const QByteArray &data) = 0;
};

// Central business-logic class for the fleet and rental transactions.
class RentalSystem
{
private:
    QList<Vehicle*> vehicles;
    QList<Rental*> rentals;

    QString vehiclesFileName;
    QString rentalsFileName;
    std::shared_ptr<RentalStorage> storage;
    std::function<QDate()> m_today;

public:
    using Today = std::function<QDate()>;
    explicit RentalSystem(const QString& dataDirectory = QString(),
                          std::shared_ptr<RentalStorage> store = {},
                          Today today = {});
    QDate today() const;
    ~RentalSystem();

    RentalSystem(const RentalSystem&) = delete;
    RentalSystem& operator=(const RentalSystem&) = delete;

    // Fleet operations.
    bool addVehicle(Vehicle* vehicle);
    Vehicle* findVehicleById(const QString& id) const;
    bool removeVehicle(const QString& id);
    // Historical references (active, returned, archived) protect vehicle IDs.
    bool hasRentalHistoryForVehicle(const QString& id) const;
    bool rentVehicle(const QString& id);
    bool returnVehicle(const QString& id);

    QString generateNextVehicleId(const QString& vehicleType) const;
    QList<Vehicle*> getVehicles() const;

    // Rental transaction operations.
    bool createRental(Rental* rental);
    Rental* findRentalById(const QString& rentalId) const;
    Rental* findActiveRentalByVehicleId(const QString& vehicleId) const;
    bool completeRental(const QString& rentalOrVehicleId,
                        const QDate& actualReturnDate);
    bool archiveRental(const QString& rentalId);
    bool deleteArchivedRental(const QString& rentalId);

    QString generateNextRentalId() const;
    QList<Rental*> getRentals() const;
    QList<Rental*> getActiveRentals() const;

    // Text-file persistence.
    void loadVehiclesFromFile();
    bool saveVehiclesToFile() const;
    void loadRentalsFromFile();
    bool saveRentalsToFile() const;
};

// Generic numeric validation helper.
template <typename T>
bool isPositiveValue(T value)
{
    return value > 0;
}

// Generic Qt-list filtering helper.
template <typename Item, typename Predicate>
QList<Item*> filterPointers(const QList<Item*>& items, Predicate condition)
{
    QList<Item*> result;

    for (Item* item : items) {
        if (condition(item)) {
            result.append(item);
        }
    }

    return result;
}

// Generic Qt-list counting helper.
template <typename Item, typename Predicate>
int countPointersWhere(const QList<Item*>& items, Predicate condition)
{
    int count = 0;

    for (Item* item : items) {
        if (condition(item)) {
            ++count;
        }
    }

    return count;
}

#endif
