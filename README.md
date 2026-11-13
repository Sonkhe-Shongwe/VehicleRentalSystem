# Rental Studio 2.0

Rental Studio is a Windows desktop app for managing vehicle rentals. Check fleet availability, prepare a quote, confirm a booking, and check the vehicle back in. The rental register keeps active contracts and return history in one place.

[Build from source](#build-from-source) · [Screenshots](docs/screenshots/README.md) · [Architecture](docs/ARCHITECTURE.md) · [Testing](docs/TESTING.md)

## Demo

![Rental Studio walkthrough showing a quote, booking, return and restored vehicle availability](docs/media/rental-walkthrough.webp)

[Download the 67-second MP4](docs/media/rental-walkthrough.mp4?raw=true)

## Build from source

Rental Studio uses C++17 and Qt Quick.

1. Clone this repository and open the root `CMakeLists.txt` in Qt Creator.
2. Select a 64-bit Qt 6 desktop kit with Widgets, Quick, Quick Controls, Quick Dialogs, SQL, Test and SVG. The CI build uses Qt 6.8.3. For a Windows MinGW build, use a Qt kit built for MinGW.
3. Select the `rental-studio-local` CMake preset. Build and run the `rental_studio_qtquick_spike` target. `VehicleRentalSystem_A3` is the original Qt Widgets target.

## Try a rental

1. In **Vehicles**, search for `C001`, select the Toyota Corolla and choose **Book selected vehicle**.
2. Set the duration to three days and choose **Preview quote**. Enter **Demo Customer** and **0000000000**, then choose **Confirm booking**.
3. In **Rentals**, select the new contract and choose **Return selected rental**, then **Confirm return**. The contract appears in **History** and the Corolla is available again.

The demo starts with sample records and resets when you close the app. The [architecture guide](docs/ARCHITECTURE.md#local-data) covers how records are stored and how to open the SQLite Lab.

[Asset credits](docs/ASSET_CREDITS.md)
