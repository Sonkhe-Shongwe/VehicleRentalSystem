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

## Try a rental from start to finish

1. Start the app without run arguments. In **Overview**, review availability and the rental that needs attention. **Active booked value** shows the value of active contracts.
2. Open **Vehicles**. Switch between **Gallery** and **Table**, search for `C001`, and select the available Toyota Corolla to see its daily rate and rental status.
3. Choose **Book selected vehicle** or **New rental**. Set the duration and select **Preview C++ quote**. Enter fictional contact details and select **Confirm booking**. In **Rentals**, find the new active contract and open **View contract details**.
4. Choose **Return selected rental**, then **Confirm return**. The vehicle is available again. Use the **Returned** or **History** filter to find the contract, or choose **Archive returned** to move it to the archived view.

For the SQLite demonstration, relaunch with the Lab argument, then select **Preview 3 days**, **Confirm booking**, **Return rental**, **Archive rental**, and **Retire vehicle**. The final screen shows zero active contracts and one historical contract. The temporary Lab database resets on exit.

## See the app running on Windows

The [Windows screenshot archive](https://github.com/Sonkhe-Shongwe/vehicle-rental-system/actions/runs/43291522841/artifacts/13978276615) contains native Qt captures at **1500 × 860** and **1280 × 720** from the same verified build. The artifact is hosted by GitHub Actions and has a limited retention period.

| Screen | What to look for |
| --- | --- |
| Overview | Fleet availability, active booked value and the overdue rental highlighted in the priority panel. |
| Vehicles | Gallery and Table controls, reference-image labels and the vehicle inspector. The gallery uses three columns in the wide capture and two in the compact capture. |
| Rentals | Status and due-date filters, the selected contract and the return action. |
| SQLite Lab | The completed fictional lifecycle, with zero active contracts and one historical record. |

## Design and verification

QML handles presentation and interaction. C++ owns pricing in integer cents, booking eligibility, date validation and persistence. The standard demo uses protected `QSaveFile` writes. The opt-in SQLite Lab demonstrates transactions, foreign keys and active-booking constraints.

The upstream source at [`87e001e880da`](https://github.com/Sonkhe-Shongwe/vehicle-rental-system/commit/87e001e880dae32770492e03556af346757d6517) passed **150/150 native tests on Linux**, **150/150 on Windows**, and **8/8 packaged journeys** on a separate GitHub-hosted Windows runner. Independent testing on a clean, offline Windows machine is deferred. [Verified GitHub Actions run](https://github.com/Sonkhe-Shongwe/vehicle-rental-system/actions/runs/43291522841)

Rental Studio began as a university Qt Widgets project and evolved into a Qt Quick desktop application. The [development repository](https://github.com/Sonkhe-Shongwe/vehicle-rental-system) holds the source and history. [Architecture](https://github.com/Sonkhe-Shongwe/vehicle-rental-system/blob/87e001e880dae32770492e03556af346757d6517/docs/ARCHITECTURE.md#real-application) · [Catalogue photo credits](https://github.com/Sonkhe-Shongwe/vehicle-rental-system/blob/87e001e880dae32770492e03556af346757d6517/catalogue_acquisition/ACTIVE_ASSET_CREDITS.md) · [Portfolio photo credits](https://github.com/Sonkhe-Shongwe/vehicle-rental-system/blob/87e001e880dae32770492e03556af346757d6517/assets/portfolio/IMAGE_CREDITS.md) · [Asset notes](https://github.com/Sonkhe-Shongwe/vehicle-rental-system/blob/87e001e880dae32770492e03556af346757d6517/assets/README.md)
