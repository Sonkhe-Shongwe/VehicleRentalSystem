#pragma once
#include <QByteArray>
#include <QString>

// Fixed app-owned fictional data. No caller can select a directory.
// Fail closed rather than importing unknown or real-person rental records.
class P5SyntheticSession final {
public:
    static QString location();
    static bool open(const QByteArray &vehiclesSeed,const QByteArray &rentalsSeed,
                     const QByteArray &marker,QString *reason);
    static bool cleanForTest(const QByteArray &marker,QString *reason);
private:
    static bool validate(const QByteArray &vehiclesSeed,
                         const QByteArray &rentalsSeed,
                         const QByteArray &marker,QString *reason);
};
