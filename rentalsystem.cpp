#include "rentalsystem.h"

#include <QDir>
#include <utility>
#include <QFile>
#include <QSaveFile>
#include <cmath>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>
#include <QtAlgorithms>

namespace {
// The production default retains the pre-existing QSaveFile single-file
// transaction. This is not a new storage format or path migration.
class FileRentalStorage final : public RentalStorage {
public:
    std::optional<QByteArray> read(const QString &path) const override {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return std::nullopt;
        return file.readAll();
    }
    bool writeAtomic(const QString &path, const QByteArray &bytes) override {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        if (file.write(bytes) != bytes.size()) {
            file.cancelWriting();
            return false;
        }
        return file.commit();
    }
};
}

RentalSystem::RentalSystem(const QString& dataDirectory,
                           std::shared_ptr<RentalStorage> store, Today clock)
    : storage(store ? std::move(store) : std::make_shared<FileRentalStorage>()),
      m_today(clock ? std::move(clock)
                    : [] { return QDate::currentDate(); })
{
    // Preserve the original source-directory default and legacy filenames.
    // Injection changes only data I/O transport and clock for deterministic tests.
    const QDir storageDir(dataDirectory.isEmpty()
                              ? QFileInfo(__FILE__).absolutePath()
                              : dataDirectory);
    vehiclesFileName = storageDir.filePath("vehicles.txt");
    rentalsFileName = storageDir.filePath("rentals.txt");
    loadVehiclesFromFile();
    loadRentalsFromFile();
}

QDate RentalSystem::today() const {
    return m_today ? m_today() : QDate();
}

RentalSystem::~RentalSystem()
{
    qDeleteAll(rentals);
    rentals.clear();

    qDeleteAll(vehicles);
    vehicles.clear();
}

bool RentalSystem::addVehicle(Vehicle* vehicle)
{
    if (vehicle == nullptr || vehicle->getId().trimmed().isEmpty() ||
        vehicle->getBrand().trimmed().isEmpty() ||
        vehicle->getModel().trimmed().isEmpty() ||
        !std::isfinite(vehicle->getPricePerDay()) ||
        vehicle->getPricePerDay() <= 0 ||
        vehicle->getId().contains('|') ||
        vehicle->getId().contains('\n') || vehicle->getId().contains('\r') ||
        vehicle->getBrand().contains('|') || vehicle->getModel().contains('|') ||
        vehicle->getBrand().contains('\n') || vehicle->getModel().contains('\n') ||
        vehicle->getBrand().contains('\r') || vehicle->getModel().contains('\r') ||
        findVehicleById(vehicle->getId()) != nullptr) {
        return false;
    }

    vehicles.append(vehicle);
    if (!saveVehiclesToFile()) {
        vehicles.removeLast(); // Caller still owns the rejected vehicle.
        return false;
    }
    return true;
}

Vehicle* RentalSystem::findVehicleById(const QString& id) const
{
    const QString searchId = id.trimmed();

    for (Vehicle* vehicle : vehicles) {
        if (vehicle->getId().compare(searchId, Qt::CaseInsensitive) == 0) {
            return vehicle;
        }
    }

    return nullptr;
}

bool RentalSystem::hasRentalHistoryForVehicle(const QString& id) const
{
    const QString searchId=id.trimmed();
    if(searchId.isEmpty())
        return false;
    // A returned or archived contract still retains its immutable vehicle ID.
    // A single-file fleet deletion must never orphan a rental ledger record.
    for(const Rental *rental: rentals) {
        if(rental &&
           rental->getVehicleId().compare(searchId,Qt::CaseInsensitive)==0)
            return true;
    }
    return false;
}

bool RentalSystem::removeVehicle(const QString& id)
{
    const QString searchId = id.trimmed();

    for (auto it = vehicles.begin(); it != vehicles.end(); ++it) {
        Vehicle* vehicle = *it;

        if (vehicle->getId().compare(searchId, Qt::CaseInsensitive) != 0) {
            continue;
        }

        // Rental history is an immutable reference even after return/archive.
        // Guard in the domain, not only the UI/typed service, so no caller
        // can delete a vehicle that a surviving rental record still names.
        if (vehicle->getIsRented() ||
            hasRentalHistoryForVehicle(vehicle->getId())) {
            return false;
        }

        const int index = static_cast<int>(it - vehicles.begin());
        vehicles.erase(it);
        if (!saveVehiclesToFile()) {
            vehicles.insert(index, vehicle);
            return false;
        }
        delete vehicle;
        return true;
    }

    return false;
}

bool RentalSystem::rentVehicle(const QString& id)
{
    Vehicle* vehicle = findVehicleById(id);

    if (vehicle == nullptr || vehicle->getIsRented()) {
        return false;
    }

    vehicle->setIsRented(true);
    if (!saveVehiclesToFile()) {
        vehicle->setIsRented(false);
        return false;
    }
    return true;
}

bool RentalSystem::returnVehicle(const QString& id)
{
    Vehicle* vehicle = findVehicleById(id);

    if (vehicle == nullptr || !vehicle->getIsRented()) {
        return false;
    }

    vehicle->setIsRented(false);
    if (!saveVehiclesToFile()) {
        vehicle->setIsRented(true);
        return false;
    }
    return true;
}

QString RentalSystem::generateNextVehicleId(const QString& vehicleType) const
{
    QString prefix;

    if (vehicleType.compare("Car", Qt::CaseInsensitive) == 0) {
        prefix = "C";
    }
    else if (vehicleType.compare("Motorcycle", Qt::CaseInsensitive) == 0) {
        prefix = "M";
    }
    else {
        return QString();
    }

    int highestNumber = 0;

    for (Vehicle* vehicle : vehicles) {
        const QString currentId = vehicle->getId().trimmed().toUpper();

        if (!currentId.startsWith(prefix)) {
            continue;
        }

        bool numberOk = false;
        const int currentNumber = currentId.mid(1).toInt(&numberOk);

        if (numberOk && currentNumber > highestNumber) {
            highestNumber = currentNumber;
        }
    }

    return QString("%1%2")
        .arg(prefix)
        .arg(highestNumber + 1, 3, 10, QChar('0'));
}

QList<Vehicle*> RentalSystem::getVehicles() const
{
    return vehicles;
}

bool RentalSystem::createRental(Rental* rental)
{
    if (rental == nullptr ||
        rental->getRentalId().trimmed().isEmpty() ||
        rental->getRentalId().contains('|') ||
        rental->getRentalId().contains('\n') ||
        rental->getRentalId().contains('\r') ||
        rental->getCustomerName().trimmed().isEmpty() ||
        rental->getPhoneNumber().trimmed().isEmpty() ||
        rental->getVehicleId().trimmed().isEmpty() ||
        !rental->getStartDate().isValid() ||
        !rental->getExpectedReturnDate().isValid() ||
        rental->getStartDate() != today() ||
        rental->getRentalDays() <= 0 ||
        !std::isfinite(rental->getPricePerDay()) ||
        !std::isfinite(rental->getTotalCost()) ||
        rental->getPricePerDay() <= 0 ||
        rental->getTotalCost() <= 0 ||
        rental->getRentalDays() > 365 ||
        rental->getIsReturned() || rental->getIsArchived() ||
        rental->getCustomerName().contains('|') ||
        rental->getCustomerName().contains('\n') ||
        rental->getCustomerName().contains('\r') ||
        rental->getPhoneNumber().contains('|') ||
        rental->getPhoneNumber().contains('\n') ||
        rental->getPhoneNumber().contains('\r') ||
        rental->getExpectedReturnDate() !=
            rental->getStartDate().addDays(rental->getRentalDays()) ||
        findRentalById(rental->getRentalId()) != nullptr) {
        return false;
    }

    Vehicle* vehicle = findVehicleById(rental->getVehicleId());

    if (vehicle == nullptr || vehicle->getIsRented() ||
        !std::isfinite(vehicle->getPricePerDay()) ||
        std::abs(rental->getPricePerDay() - vehicle->getPricePerDay()) > 0.005 ||
        std::abs(rental->getTotalCost() -
                 rental->getPricePerDay() * rental->getRentalDays()) > 0.005) {
        return false;
    }

    if (findActiveRentalByVehicleId(vehicle->getId()) != nullptr) {
        return false;
    }

    rentals.append(rental);
    vehicle->setIsRented(true);
    // The rental ledger is authoritative for vehicle occupancy. Only one
    // atomic file write is required; status is reconstructed on next launch.
    if (!saveRentalsToFile()) {
        rentals.removeLast(); // Rejected object remains owned by caller.
        vehicle->setIsRented(false);
        return false;
    }
    return true;
}

Rental* RentalSystem::findRentalById(const QString& rentalId) const
{
    const QString searchId = rentalId.trimmed();

    for (Rental* rental : rentals) {
        if (rental->getRentalId().compare(searchId, Qt::CaseInsensitive) == 0) {
            return rental;
        }
    }

    return nullptr;
}

Rental* RentalSystem::findActiveRentalByVehicleId(const QString& vehicleId) const
{
    const QString searchId = vehicleId.trimmed();

    for (Rental* rental : rentals) {
        if (!rental->getIsReturned() &&
            rental->getVehicleId().compare(searchId, Qt::CaseInsensitive) == 0) {
            return rental;
        }
    }

    return nullptr;
}

bool RentalSystem::completeRental(const QString& rentalOrVehicleId,
                                  const QDate& actualReturnDate)
{
    Rental* rental = findRentalById(rentalOrVehicleId);

    if (rental == nullptr) {
        rental = findActiveRentalByVehicleId(rentalOrVehicleId);
    }

    if (rental == nullptr || rental->getIsReturned() || !actualReturnDate.isValid()) {
        return false;
    }

    // A return cannot be recorded before the rental began or in the future.
    if (actualReturnDate < rental->getStartDate() ||
        actualReturnDate > today()) {
        return false;
    }

    Vehicle* vehicle = findVehicleById(rental->getVehicleId());

    if (vehicle == nullptr) {
        return false;
    }

    rental->markReturned(actualReturnDate);
    vehicle->setIsRented(false);
    if (!saveRentalsToFile()) {
        // Reconstruct the original rental state without reallocating its pointer.
        rental->restoreActive();
        vehicle->setIsRented(true);
        return false;
    }
    return true;
}

bool RentalSystem::archiveRental(const QString& rentalId)
{
    Rental* rental = findRentalById(rentalId);

    if (rental == nullptr || !rental->getIsReturned() || rental->getIsArchived()) {
        return false;
    }

    rental->setArchived(true);
    if (!saveRentalsToFile()) {
        rental->setArchived(false);
        return false;
    }
    return true;
}

bool RentalSystem::deleteArchivedRental(const QString& rentalId)
{
    const QString searchId = rentalId.trimmed();

    for (auto it = rentals.begin(); it != rentals.end(); ++it) {
        Rental* rental = *it;

        if (rental->getRentalId().compare(searchId, Qt::CaseInsensitive) != 0) {
            continue;
        }

        // Permanent deletion is allowed only after a record is archived.
        if (!rental->getIsArchived()) {
            return false;
        }

        const int index = static_cast<int>(it - rentals.begin());
        rentals.erase(it);
        if (!saveRentalsToFile()) {
            rentals.insert(index, rental);
            return false;
        }
        delete rental;
        return true;
    }

    return false;
}

QString RentalSystem::generateNextRentalId() const
{
    int highestNumber = 0;

    for (Rental* rental : rentals) {
        const QString currentId = rental->getRentalId().trimmed().toUpper();

        if (!currentId.startsWith('R')) {
            continue;
        }

        bool numberOk = false;
        const int currentNumber = currentId.mid(1).toInt(&numberOk);

        if (numberOk && currentNumber > highestNumber) {
            highestNumber = currentNumber;
        }
    }

    return QString("R%1").arg(highestNumber + 1, 3, 10, QChar('0'));
}

QList<Rental*> RentalSystem::getRentals() const
{
    return rentals;
}

QList<Rental*> RentalSystem::getActiveRentals() const
{
    return filterPointers(rentals, [](Rental* rental) {
        return !rental->getIsReturned();
    });
}

void RentalSystem::loadVehiclesFromFile()
{
    const auto bytes = storage->read(vehiclesFileName);
    if (!bytes)
        return;
    QString input = QString::fromUtf8(*bytes);
    QTextStream in(&input, QIODevice::ReadOnly);

    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();

        if (line.isEmpty()) {
            continue;
        }

        const QStringList parts = line.split('|');

        if (parts.size() != 7) {
            continue;
        }

        const QString type = parts[0].trimmed();
        const QString id = parts[1].trimmed();
        const QString brand = parts[2].trimmed();
        const QString model = parts[3].trimmed();

        bool priceOk = false;
        const double price = parts[4].toDouble(&priceOk);
        const QString rentedText = parts[5].trimmed();
        if (rentedText != "0" && rentedText != "1") {
            continue;
        }
        const bool rented = rentedText == "1";

        bool extraOk = false;
        const int extra = parts[6].toInt(&extraOk);

        if (id.isEmpty() || brand.isEmpty() || model.isEmpty() ||
            !priceOk || !std::isfinite(price) || price <= 0 || !extraOk || extra <= 0 ||
            findVehicleById(id) != nullptr) {
            continue;
        }

        Vehicle* vehicle = nullptr;

        if (type.compare("Car", Qt::CaseInsensitive) == 0) {
            vehicle = new Car(id, brand, model, price, extra);
        }
        else if (type.compare("Motorcycle", Qt::CaseInsensitive) == 0) {
            vehicle = new Motorcycle(id, brand, model, price, extra);
        }

        if (vehicle != nullptr) {
            vehicle->setIsRented(rented);
            vehicles.append(vehicle);
        }
    }
}

bool RentalSystem::saveVehiclesToFile() const
{
    QString output;
    QTextStream out(&output, QIODevice::WriteOnly);
    for (Vehicle* vehicle : vehicles) {
        out << vehicle->toFileString() << '\n';
    }
    out.flush();
    if (out.status() != QTextStream::Ok)
        return false;
    return storage->writeAtomic(vehiclesFileName, output.toUtf8());
}

void RentalSystem::loadRentalsFromFile()
{
    const auto bytes = storage->read(rentalsFileName);
    if (!bytes)
        return;
    QString input = QString::fromUtf8(*bytes);
    QTextStream in(&input, QIODevice::ReadOnly);

    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();

        if (line.isEmpty()) {
            continue;
        }

        const QStringList parts = line.split('|', Qt::KeepEmptyParts);

        // New records contain 13 fields. The older enhanced project used a
        // 14-field format that included a driver's licence. Both are accepted
        // so an existing rentals.txt file can be opened without data loss.
        const bool oldFormat = parts.size() == 14;
        const bool newFormat = parts.size() == 13;

        if ((!oldFormat && !newFormat) ||
            parts[0].compare("Rental", Qt::CaseInsensitive) != 0) {
            continue;
        }

        const QString rentalId = parts[1].trimmed();
        const QString customerName = parts[2].trimmed();
        const QString phoneNumber = parts[3].trimmed();

        const int vehicleIndex = oldFormat ? 5 : 4;
        const int startDateIndex = oldFormat ? 6 : 5;
        const int expectedDateIndex = oldFormat ? 7 : 6;
        const int actualDateIndex = oldFormat ? 8 : 7;
        const int daysIndex = oldFormat ? 9 : 8;
        const int priceIndex = oldFormat ? 10 : 9;
        const int totalIndex = oldFormat ? 11 : 10;
        const int returnedIndex = oldFormat ? 12 : 11;
        const int archivedIndex = oldFormat ? 13 : 12;

        const QString vehicleId = parts[vehicleIndex].trimmed();
        const QDate startDate = QDate::fromString(parts[startDateIndex].trimmed(), Qt::ISODate);
        const QDate expectedReturnDate =
            QDate::fromString(parts[expectedDateIndex].trimmed(), Qt::ISODate);
        const QDate actualReturnDate =
            QDate::fromString(parts[actualDateIndex].trimmed(), Qt::ISODate);

        bool daysOk = false;
        const int rentalDays = parts[daysIndex].toInt(&daysOk);

        bool priceOk = false;
        const double pricePerDay = parts[priceIndex].toDouble(&priceOk);

        bool totalOk = false;
        const double totalCost = parts[totalIndex].toDouble(&totalOk);

        const QString returnedText = parts[returnedIndex].trimmed();
        const QString archivedText = parts[archivedIndex].trimmed();
        if ((returnedText != "0" && returnedText != "1") ||
            (archivedText != "0" && archivedText != "1")) {
            continue;
        }
        const bool returned = returnedText == "1";
        const bool archived = archivedText == "1";

        if (rentalId.isEmpty() || customerName.isEmpty() ||
            phoneNumber.isEmpty() || vehicleId.isEmpty() ||
            !startDate.isValid() || !expectedReturnDate.isValid() ||
            !daysOk || rentalDays <= 0 ||
            !priceOk || !std::isfinite(pricePerDay) || pricePerDay <= 0 ||
            !totalOk || !std::isfinite(totalCost) || totalCost <= 0 ||
            (archived && !returned) ||
            (returned && actualReturnDate.isValid() && actualReturnDate < startDate) ||
            (!returned && actualReturnDate.isValid()) ||
            findRentalById(rentalId) != nullptr) {
            continue;
        }

        // Active rentals require an existing vehicle. Historical returned
        // records remain valid even if that vehicle was removed later.
        if (!returned && (findVehicleById(vehicleId) == nullptr ||
                          findActiveRentalByVehicleId(vehicleId) != nullptr)) {
            continue;
        }

        Rental* rental = new Rental(rentalId,
                                    customerName,
                                    phoneNumber,
                                    vehicleId,
                                    startDate,
                                    expectedReturnDate,
                                    rentalDays,
                                    pricePerDay,
                                    totalCost);

        if (returned) {
            // Legacy records without a return date were historically recovered
            // using the expected date. Preserve that behaviour until an explicit
            // reviewed data migration can make missing history visible.
            rental->markReturned(actualReturnDate.isValid()
                                     ? actualReturnDate : expectedReturnDate);
        }

        rental->setArchived(archived);
        rentals.append(rental);

        if (!returned) {
            Vehicle* vehicle = findVehicleById(vehicleId);
            if (vehicle != nullptr) {
                vehicle->setIsRented(true);
            }
        }
    }

    // Reconcile fleet status with active rental records so the two text files
    // cannot disagree after a manual edit or interrupted previous session.
    for (Vehicle* vehicle : vehicles) {
        vehicle->setIsRented(
            findActiveRentalByVehicleId(vehicle->getId()) != nullptr
        );
    }

    // Loading must be read-only. Legacy data can be migrated only through an
    // explicit, backed-up action, never as a side effect of viewing records.
}


bool RentalSystem::saveRentalsToFile() const
{
    QString output;
    QTextStream out(&output, QIODevice::WriteOnly);
    for (Rental* rental : rentals) {
        out << rental->toFileString() << '\n';
    }
    out.flush();
    if (out.status() != QTextStream::Ok)
        return false;
    return storage->writeAtomic(rentalsFileName, output.toUtf8());
}
