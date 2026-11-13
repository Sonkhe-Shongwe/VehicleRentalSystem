# Architecture

Rental Studio has three operational workspaces and an explicitly enabled SQLite Lab. The normal launch builds a disposable session from fictional fixtures. A separate opt-in persistent fictional mode is available for repeatable local testing.

| Layer | Responsibility |
| --- | --- |
| [Qt Quick interface](../spikes/qtquick_p2/Main.qml) | Renders the workspaces, navigation, filters, forms and confirmations. |
| [Application services](../src/application) | Expose typed fleet and rental data and coordinate quotes, booking, return and archive. |
| [Rental domain](../rentalsystem.cpp) | Enforces availability and lifecycle rules, commits local changes and rolls back failed writes. |
| [SQLite Lab](../src/application/p7_sandbox) | Runs an isolated fictional lifecycle with normalized tables, transactions, constraints and integer-cent amounts. |

## Booking and return

A quote previews the proposed vehicle, dates, duration and amount. Confirmation recomputes the quote against current availability and rejects stale or invalid requests. The booking service publishes its receipt after the protected write succeeds. For a return, the domain validates the contract state and date, writes the update and releases the vehicle. Returned contracts stay in history and can be archived by a separate action.

## Data and money

The standard demo writes its fictional records through the local `QSaveFile` path. The application services use integer cents for quote calculations and verify the amount at confirmation. Booked contract values represent obligations recorded on the contracts. The demo has no payment collection.

The SQLite Lab uses its own generated fixtures and temporary database. Its schema enforces foreign keys and one active contract per occupied vehicle. Its lifecycle and rollback tests exercise this database independently of the normal workspaces.

## UI and media

Gallery and Table share vehicle identities and C++ model state. Rentals uses stable contract IDs across filters. The application provides accessible labels, keyboard focus states and reduced-motion handling. Vehicle photos illustrate make and model families. The app displays a labelled silhouette for families without a reference photo. See the [asset credits](ASSET_CREDITS.md).
