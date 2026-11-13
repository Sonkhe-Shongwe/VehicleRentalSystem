# Rental Studio 2.0

Rental Studio is an offline Windows desktop portfolio project built with **C++, Qt 6 Quick and QML**. It demonstrates a vehicle rental workflow with generated fictional records. The fleet contains **131 vehicles across 70 model families**.

## Explore the app

- **Overview** shows fleet availability, active rentals and due dates.
- **Vehicles** lets you switch between Gallery and Table, filter the fleet and inspect a vehicle.
- **Rentals** takes you from quote to booking and return, with a record in contract history. Closing and reopening the standard demo resets its fictional session.
- **SQLite Lab** is an optional engineering demo. It runs a quote, booking, return, archive and vehicle retirement in an isolated temporary database.

## Run from source on Windows

1. Clone this repository and open its root `CMakeLists.txt` in **Qt Creator**.
2. Select a **64-bit Windows Qt 6 MinGW kit** with Qt Quick, Quick Controls, Quick Dialogs, Qt Test and Qt SQL. Choose the `rental-studio-local` CMake preset.
3. Build and run `rental_studio_qtquick_spike`.

## Design and verification

QML handles presentation and interaction. C++ owns pricing in integer cents, booking eligibility, date validation and persistence. The standard demo uses protected `QSaveFile` writes. The opt-in SQLite Lab demonstrates transactions, foreign keys and active-booking constraints.

The upstream source at [`87e001e880da`](https://github.com/Sonkhe-Shongwe/vehicle-rental-system/commit/87e001e880dae32770492e03556af346757d6517) passed **150/150 native tests on Linux**, **150/150 on Windows**, and **8/8 packaged journeys** on a separate GitHub-hosted Windows runner. The fresh offline Windows check is pending. This repository presents the source demo. [Verified GitHub Actions run](https://github.com/Sonkhe-Shongwe/vehicle-rental-system/actions/runs/43291522841)

Rental Studio began as a university Qt Widgets project and evolved into a Qt Quick desktop application. The [original repository](https://github.com/Sonkhe-Shongwe/vehicle-rental-system) holds the development history. [Architecture](docs/ARCHITECTURE.md) · [Catalogue photo credits](catalogue_acquisition/ACTIVE_ASSET_CREDITS.md) · [Portfolio photo credits](assets/portfolio/IMAGE_CREDITS.md) · [Asset notes](assets/README.md).
