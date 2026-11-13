# Rental Studio 2.0

Rental Studio is an offline Windows desktop portfolio project built with **C++, Qt 6 Quick and QML**. It demonstrates a vehicle rental workflow with generated fictional records. The fleet contains **131 vehicles across 70 model families**.

## Explore the app

- **Overview** shows fleet availability, active rentals and due dates.
- **Vehicles** lets you switch between Gallery and Table, filter the fleet and inspect a vehicle.
- **Rentals** takes you from quote to booking and return, with a record in contract history. Closing and reopening the standard demo resets its fictional session.
- **SQLite Lab** demonstrates the same lifecycle through SQLite, from quote to booking, return, archive and vehicle retirement. It runs in an isolated temporary database and requires an explicit launch option.

## Run from source on Windows

1. [Download the verified source snapshot](https://codeload.github.com/Sonkhe-Shongwe/vehicle-rental-system/zip/87e001e880dae32770492e03556af346757d6517) from the development repository and extract it. Open the extracted folder's root `CMakeLists.txt` in **Qt Creator**.
2. Select a **64-bit Windows Qt 6.5 or later MinGW kit** with Qt Widgets, Quick, Quick Controls, Quick Dialogs, Test, SQL and SVG support. Choose the `rental-studio-local` CMake preset.
3. Select the `rental_studio_qtquick_spike` run target, then build and run. To try the **SQLite Lab**, add `--p7-fictional-sql-lab` to the run arguments before launching.

## Design and verification

QML handles presentation and interaction. C++ owns pricing in integer cents, booking eligibility, date validation and persistence. The standard demo uses protected `QSaveFile` writes. The opt-in SQLite Lab demonstrates transactions, foreign keys and active-booking constraints.

The upstream source at [`87e001e880da`](https://github.com/Sonkhe-Shongwe/vehicle-rental-system/commit/87e001e880dae32770492e03556af346757d6517) passed **150/150 native tests on Linux**, **150/150 on Windows**, and **8/8 packaged journeys** on a separate GitHub-hosted Windows runner. Independent testing on a clean, offline Windows machine is deferred. [Verified GitHub Actions run](https://github.com/Sonkhe-Shongwe/vehicle-rental-system/actions/runs/43291522841)

Rental Studio began as a university Qt Widgets project and evolved into a Qt Quick desktop application. The [development repository](https://github.com/Sonkhe-Shongwe/vehicle-rental-system) holds the source and history. [Architecture](https://github.com/Sonkhe-Shongwe/vehicle-rental-system/blob/87e001e880dae32770492e03556af346757d6517/docs/ARCHITECTURE.md#real-application) · [Catalogue photo credits](https://github.com/Sonkhe-Shongwe/vehicle-rental-system/blob/87e001e880dae32770492e03556af346757d6517/catalogue_acquisition/ACTIVE_ASSET_CREDITS.md) · [Portfolio photo credits](https://github.com/Sonkhe-Shongwe/vehicle-rental-system/blob/87e001e880dae32770492e03556af346757d6517/assets/portfolio/IMAGE_CREDITS.md) · [Asset notes](https://github.com/Sonkhe-Shongwe/vehicle-rental-system/blob/87e001e880dae32770492e03556af346757d6517/assets/README.md)
