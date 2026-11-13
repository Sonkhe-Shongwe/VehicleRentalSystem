# Testing Rental Studio

## CI results

Source [`536aede8e976`](https://github.com/Sonkhe-Shongwe/VehicleRentalSystem/commit/536aede8e9767ecb8679079d333beaf776b7c8d7) passed [GitHub Actions run 43415843061](https://github.com/Sonkhe-Shongwe/VehicleRentalSystem/actions/runs/43415843061).

| Environment | CTests | Additional checks |
| --- | --- | --- |
| Ubuntu 24.04, GCC 13.3.0, Qt 6.8.3 | 150 passed | 8 screenshots checked |
| Windows Server 2022, MSVC 19.44.35229.0, Qt 6.8.3 | 150 passed | 8 screenshots checked and 8 functional checks passed on the relocated app |

Independent testing on a clean Windows machine without network access is pending.

## Run locally

Configure the `rental-studio-local` preset with a Qt 6 desktop kit. From a terminal configured for that kit:

```sh
cmake --preset rental-studio-local
cmake --build --preset rental-studio-local-all --config Debug
ctest --preset rental-studio-local-all -C Debug
```

The test suite covers quote calculation, booking conflicts, stale quotes, cancellations, returns, archive, fleet changes, failed writes, saving records across restarts, keyboard navigation and the SQLite Lab's constraints and rollback.

## Screenshots

The [GitHub Actions workflow](../.github/workflows/native.yml) runs on Linux GCC and Windows MSVC with Qt 6.8.3. The catalogue and SQLite Lab tests capture the running Qt application at 1500 × 860 and 1280 × 720. To reproduce those two groups after building:

```sh
ctest --preset rental-studio-local-all -C Debug -R "portfolio_catalog|p7_actual_app_lab"
```

Use a Windows display area large enough for the wide capture. CI checks the PNG dimensions, integrity and basic pixel variation before uploading screenshots for visual review.

## Walkthrough

The [walkthrough](media/rental-walkthrough.mp4?raw=true) uses frames captured on Windows from [`536aede8e976`](https://github.com/Sonkhe-Shongwe/VehicleRentalSystem/commit/536aede8e9767ecb8679079d333beaf776b7c8d7) in [run 43415843061](https://github.com/Sonkhe-Shongwe/VehicleRentalSystem/actions/runs/43415843061). The 67-second edit adds captions and pauses.

In a fresh working directory, run the built Qt Quick app with `--portfolio-demo-capture` as its sole argument. It uses a temporary session and writes 54 frames to `portfolio-demo-frames`. The Windows job checks the booking and return and uploads the frames.
