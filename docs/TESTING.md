# Testing Rental Studio

## CI results

Source [`d859b4d0945d`](https://github.com/Sonkhe-Shongwe/VehicleRentalSystem/commit/d859b4d0945db0c7114cd1864b489016785265f5) passed [GitHub Actions run 43406064178](https://github.com/Sonkhe-Shongwe/VehicleRentalSystem/actions/runs/43406064178).

| Environment | CTests | Additional checks |
| --- | --- | --- |
| Ubuntu 24.04, GCC 13.3.0, Qt 6.8.3 | 150 passed | 8 screenshots checked |
| Windows Server 2022, MSVC 19.44.35229.0, Qt 6.8.3 | 150 passed | 8 screenshots checked and 8 functional checks passed on the relocated app |

Clean offline Windows testing is deferred.

## Run locally

Configure the `rental-studio-local` preset with a Qt 6 desktop kit. From a terminal configured for that kit:

```sh
cmake --preset rental-studio-local
cmake --build --preset rental-studio-local-all --config Debug
ctest --preset rental-studio-local-all -C Debug
```

The test suite covers quote calculation, booking conflicts, stale quotes, cancellations, returns, archive, fleet changes, failed writes, fictional persistence, keyboard navigation and the SQLite Lab's constraints and rollback.

## Screenshots

The [GitHub Actions workflow](../.github/workflows/native.yml) runs on Linux GCC and Windows MSVC with Qt 6.8.3. The catalogue and SQLite Lab tests capture the running Qt application at 1500 × 860 and 1280 × 720. To reproduce those two groups after building:

```sh
ctest --preset rental-studio-local-all -C Debug -R "portfolio_catalog|p7_actual_app_lab"
```

Use a Windows display area large enough for the wide capture. CI checks the PNG dimensions, integrity and basic pixel variation before uploading screenshots for visual review.


## Walkthrough

The [walkthrough](media/rental-walkthrough.mp4) uses frames from one run of the Windows application. Captions and pauses were added for readability.

In a fresh working directory, run the built Qt Quick app with `--portfolio-demo-capture` as its sole argument. It uses a temporary session and writes 54 frames to `portfolio-demo-frames`. The Windows job checks the booking and return and uploads the frames.
