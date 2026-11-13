# Testing Rental Studio

The source in this repository was imported from development commit `87e001e880dae32770492e03556af346757d6517`. The portfolio edition retains the application and native tests. Its asset selection uses 29 reference photos with recorded CC0 declarations, and its catalogue tests assert both retained photos and labelled fallback illustrations.

## Verified portfolio build

The portfolio source commit [`6037e4932ecd`](https://github.com/Sonkhe-Shongwe/VehicleRentalSystem/commit/6037e4932ecd94197ce477bf3bc8f06a51be4d5c) passed both jobs in [GitHub Actions run 43404615624](https://github.com/Sonkhe-Shongwe/VehicleRentalSystem/actions/runs/43404615624). Both jobs checked out that exact source commit.

| Hosted environment | Native CTests | Additional checks |
| --- | --- | --- |
| Ubuntu 24.04, GCC 13.3.0, Qt 6.8.3 | 150 passed, 0 failed | 8 PNGs validated |
| Windows Server 2022, MSVC 19.44.35229.0, Qt 6.8.3 | 150 passed, 0 failed | 8 PNGs validated and 8/8 fictional checks on the relocated Windows app |

The [screenshot gallery](screenshots/README.md) preserves the original standalone import captures from source `0438d89bbc7c`. A clean offline consumer Windows check is deferred.

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


## Rental walkthrough video

The [67-second walkthrough](media/rental-walkthrough.mp4) edits together frames from one run of the compiled Qt app on the Windows runner. The fictional booking and return use the app’s existing controls and C++ services. Captions and pauses were added for readability.

In a fresh working directory, run the built Qt Quick app with `--portfolio-demo-capture` as its sole argument. It runs against a disposable fictional session and writes 54 frames to `portfolio-demo-frames`. The capture checks the confirmed quote, the active contract, the return in History and the vehicle’s restored availability. The Windows CI job uploads the frames for review.
