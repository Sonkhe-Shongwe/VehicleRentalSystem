# Testing Rental Studio

The source in this repository was imported from development commit `87e001e880dae32770492e03556af346757d6517`. The portfolio edition retains the application and native tests. Its asset selection uses 29 reference photos with recorded CC0 declarations, and its catalogue tests assert both retained photos and labelled fallback illustrations.

## Verified portfolio build

The portfolio source commit [`0438d89bbc7c`](https://github.com/Sonkhe-Shongwe/VehicleRentalSystem/commit/0438d89bbc7cd266752606feed251114e8c1e51f) passed both jobs in [GitHub Actions run 43401498487](https://github.com/Sonkhe-Shongwe/VehicleRentalSystem/actions/runs/43401498487). Both jobs checked out that exact source commit.

| Hosted environment | Native CTests | Additional checks |
| --- | --- | --- |
| Ubuntu 24.04, GCC 13.3.0, Qt 6.8.3 | 150 passed, 0 failed | 8 PNGs validated |
| Windows Server 2022, MSVC 19.44.35229.0, Qt 6.8.3 | 150 passed, 0 failed | 8 PNGs validated and 8/8 fictional checks on the relocated Windows app |

The [screenshot gallery](screenshots/README.md) contains the selected Windows captures from this run. The documentation and screenshot commit does not change the compiled inputs. A clean offline consumer Windows check is deferred.

## Run locally

Configure the `rental-studio-local` preset with a Qt 6 desktop kit. From a terminal configured for that kit:

```sh
cmake --preset rental-studio-local
cmake --build --preset rental-studio-local-all --config Debug
ctest --preset rental-studio-local-all -C Debug
```

The test suite covers quote calculation, booking conflicts, stale quotes, cancellations, returns, archive, fleet changes, failed writes, fictional persistence, keyboard navigation and the SQLite Lab's constraints and rollback. The hosted workflow also exercises the relocated Windows build with eight fictional checks.

## Native screenshot captures

The [GitHub Actions workflow](../.github/workflows/native.yml) runs on Linux GCC and Windows MSVC with Qt 6.8.3. The catalogue and SQLite Lab tests capture the running Qt application at 1500 × 860 and 1280 × 720. To reproduce those two groups after building:

```sh
ctest --preset rental-studio-local-all -C Debug -R "portfolio_catalog|p7_actual_app_lab"
```

Use a Windows display area large enough for the wide capture. CI checks the PNG dimensions, integrity and basic pixel variation before uploading screenshots for visual review.
