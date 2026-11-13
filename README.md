# Rental Studio 2.0

Rental Studio is an offline Windows desktop app for running a fictional vehicle rental operation. Built with C++17 and Qt 6 Quick, it shows how a quote becomes a booking, how a return frees a vehicle, and how completed contracts stay available for review.

| Workspace | What you can do |
| --- | --- |
| **Overview** | See available vehicles, active rentals, overdue returns and booked contract value. |
| **Vehicles** | Browse 131 fictional cars and motorcycles across 70 model families. Search, switch between Gallery and Table, inspect a vehicle, and manage the fleet. |
| **Rentals** | Review a quote, confirm a booking, process a return, and find contracts by status or vehicle ID. |

The default demo resets when you close the app. Use fictional details throughout. The optional SQLite Lab shows the rental lifecycle using a separate temporary database.

## Run on Windows

1. Clone this repository and open the root `CMakeLists.txt` in **Qt Creator**.
2. Select a **64-bit Qt 6.8.3 or later MinGW desktop kit** with Widgets, Quick, Quick Controls, Quick Dialogs, SQL, Test and SVG support. Choose the `rental-studio-local` CMake preset.
3. Select **`rental_studio_qtquick_spike`** as the run target, then build and run. Launch without arguments for the 131-vehicle demo.

## Follow a rental from quote to return

1. Start in **Overview** and review the fleet availability and the rental flagged for attention.
2. In **Vehicles**, find the available Toyota Corolla `C001`. Select it in Gallery, switch to Table, then choose **Book selected vehicle**.
3. Set the rental duration and select **Preview C++ quote**. Enter a fictional name and contact number, then select **Confirm booking**.
4. In **Rentals**, select the new active contract and open **View contract details**. Choose **Return selected rental**, then **Confirm return**. Find the contract with the **Returned** or **History** filter. The vehicle is available again.

To try the **SQLite Lab**, add `--p7-fictional-sql-lab` to the run arguments in Qt Creator and relaunch. In the Lab, select **Preview 3 days**, **Confirm booking**, **Return rental**, **Archive rental**, and **Retire vehicle**. The completed run shows zero active contracts and one historical contract.

## Explore the engineering

- [Architecture](docs/ARCHITECTURE.md) explains the C++ and QML boundary, pricing, transaction safeguards and the SQLite Lab.
- [Testing](docs/TESTING.md) shows how to run CTest and reproduce the native screenshot captures.
- [Image and icon credits](docs/ASSET_CREDITS.md) record the reference media used in this edition.
