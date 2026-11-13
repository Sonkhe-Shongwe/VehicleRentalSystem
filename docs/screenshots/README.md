# Rental Studio on Windows

Rental Studio running on Windows with sample data. Captured with Qt 6.8.3 and MSVC from commit `1f1e626bd92d09845370473109fbf04c222185d2`. [CI run](https://github.com/Sonkhe-Shongwe/VehicleRentalSystem/actions/runs/43408623067) · [Asset credits](../ASSET_CREDITS.md)

| Screen | 1500 × 860 | 1280 × 720 |
| --- | --- | --- |
| Overview | [Wide](overview-1500x860.png) | [Compact](overview-1280x720.png) |
| Vehicles | [Wide](vehicles-1500x860.png) | [Compact](vehicles-1280x720.png) |
| Rentals | [Wide](rentals-1500x860.png) | [Compact](rentals-1280x720.png) |
| SQLite Lab | [Wide](sqlite-lab-1500x860.png) | [Compact](sqlite-lab-1280x720.png) |

## Vehicles

The gallery keeps the search, view switch and inspector visible at both sizes. It uses three columns in the wide window and two in the compact window. Cards label photos as references and silhouettes as illustrations.

![Vehicles workspace with gallery, filters and inspector](vehicles-1500x860.png)

## Rentals

The register and selected contract stay side by side. Select an active contract to review its details and process a return.

![Rentals workspace with the contract register and selected rental actions](rentals-1500x860.png)

## SQLite Lab

The completed run shows zero active contracts and one contract in History.

![SQLite Lab after completing a booking, return, archive and vehicle retirement](sqlite-lab-1500x860.png)
