# Architecture

Rental Studio began as a university project using Qt Widgets. The current desktop app uses Qt Quick for Overview, Vehicles and Rentals, with C++ handling pricing, availability and changes to rental records.

| Area | Entry point |
| --- | --- |
| Navigation, workspaces and dialogs | [Main.qml](../spikes/qtquick_p2/Main.qml) |
| Fleet and rental services | [Application layer](../src/application) |
| Rental rules and local storage | [rentalsystem.cpp](../rentalsystem.cpp) |
| SQLite transactions and constraints | [sqlite_repository.h](../src/application/p7_sandbox/sqlite_repository.h) |

## Booking and return

The booking service recalculates the quote when the user confirms it. For example, a three-day quote at R 450 per day totals R 1,350. If the rate changes to R 500 before confirmation, the old quote is rejected. A fresh preview shows R 1,500. The regression test checks that the rejected quote leaves the vehicle available and creates no rental record. [Confirmation service](../src/application/rental_confirm_service.cpp) · [Regression test](../tests/p3_rental_confirm_tests.cpp)

The app shows a booking confirmation after the save succeeds. A return releases the vehicle and keeps the contract in History. Archiving is a separate action.

## Local data

The standard demo uses `QSaveFile` to write local records in a temporary session. An optional persistent fictional mode keeps sample records between launches. The standard quote uses the existing decimal rates. Booked value is the amount on a contract. The app has no payment collection.

The SQLite Lab uses its own temporary database. It stores amounts in integer cents and enforces foreign keys and one active contract per vehicle.

## Interface

Gallery and Table share the same vehicle selection. Rentals tracks the selected contract by ID across filters. The interface provides keyboard navigation, accessible labels and reduced-motion handling. See [asset credits](ASSET_CREDITS.md) for the vehicle reference photos.
