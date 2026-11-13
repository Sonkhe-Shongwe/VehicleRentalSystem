#include "mainwindow.h"
#include "rentalsystem.h"

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QAbstractButton>
#include <QFile>
#include <QKeyEvent>
#include <QPixmap>
#include <QPair>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void require(bool condition, const QString& description)
{
    if (!condition)
        throw std::runtime_error(description.toStdString());
}

template <typename T>
T* widget(MainWindow& window, const char* name)
{
    T* found = window.findChild<T*>(QString::fromLatin1(name));
    require(found != nullptr, QString("Missing Qt control: %1").arg(name));
    return found;
}

QPushButton* button(MainWindow& window, const QString& label)
{
    for (QPushButton* candidate : window.findChildren<QPushButton*>()) {
        if (candidate->text() == label)
            return candidate;
    }
    throw std::runtime_error(("Missing button: " + label).toStdString());
}

void navigate(MainWindow& window, const char* slot)
{
    require(QMetaObject::invokeMethod(&window, slot, Qt::DirectConnection),
            QString("Qt navigation slot failed: %1").arg(slot));
    QApplication::processEvents();
}

// Send actual Qt key events to a visible control rather than invoking a slot.
void pressKey(QWidget* target, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QKeyEvent down(QEvent::KeyPress, key, modifiers);
    QApplication::sendEvent(target, &down);
    QKeyEvent up(QEvent::KeyRelease, key, modifiers);
    QApplication::sendEvent(target, &up);
    QApplication::processEvents();
}

// Both actual Qt table background tokens are checked, including zebra stripes.
// WCAG AA normal text needs at least 4.5:1 contrast.
double channel(int component)
{
    const double x = component / 255.0;
    return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
}
double luminance(const QColor& color)
{
    return 0.2126 * channel(color.red()) +
           0.7152 * channel(color.green()) +
           0.0722 * channel(color.blue());
}
double contrast(const QColor& foreground, const QColor& background)
{
    const double first = luminance(foreground);
    const double second = luminance(background);
    return (std::max(first, second) + 0.05) /
           (std::min(first, second) + 0.05);
}
void verifyTableStatusContrast(QTableWidgetItem* status, const QString& state)
{
    require(status != nullptr, "Missing status cell: " + state);
    const QColor ink = status->foreground().color();
    for (const QColor& surface : {QColor("#fffefa"), QColor("#f5f7f0")}) {
        const double ratio = contrast(ink, surface);
        require(ratio >= 4.5,
                QString("%1 table text contrast below 4.5:1: %2")
                    .arg(state).arg(ratio, 0, 'f', 2));
    }
}

QRect boundsInWindow(MainWindow& window, QWidget* control, const QString& label)
{
    require(control != nullptr, "Missing layout control: " + label);
    require(control->isVisible(), "Layout control is hidden: " + label);
    require(control->width() > 0 && control->height() > 0,
            "Layout control is empty: " + label);
    const QRect bounds(control->mapTo(&window, QPoint(0, 0)), control->size());
    require(window.rect().contains(bounds),
            QString("%1 extends beyond window: %2,%3 %4x%5")
                .arg(label).arg(bounds.x()).arg(bounds.y())
                .arg(bounds.width()).arg(bounds.height()));
    return bounds;
}

void verifyLayoutAtSize(MainWindow& window, int width, int height)
{
    window.resize(width, height);
    QApplication::processEvents();
    require(window.size() == QSize(width, height),
            "Could not apply expected responsive window size");

    const auto named = [&](const char* objectName) {
        // Rental History and Return have intentionally shared visual object
        // names; choose the active page rather than a hidden stacked sibling.
        const QString name = QString::fromLatin1(objectName);
        for (QWidget* candidate : window.findChildren<QWidget*>(name)) {
            if (candidate->isVisible())
                return boundsInWindow(window, candidate, name);
        }
        require(false, "No visible Qt layout control: " + name);
        return QRect();
    };
    const auto visibleButton = [&](const char* caption) {
        const QString text = QString::fromLatin1(caption);
        return boundsInWindow(window, button(window, text), text);
    };
    const auto stackedAboveTable = [&](const QRect& panel, const char* screen) {
        const QRect heading = named("TableSectionTitle");
        require(panel.bottom() + 1 < heading.top(),
                QString("%1 panel overlaps table heading at %2x%3")
                    .arg(QString::fromLatin1(screen)).arg(width).arg(height));
        const QRect table = boundsInWindow(window,
                                           window.findChild<QTableWidget*>(), "DataTable");
        require(heading.bottom() < table.top(),
                QString("%1 heading overlaps table").arg(QString::fromLatin1(screen)));
    };
    const auto contained = [&](const QRect& child, const QRect& panel,
                               const char* label) {
        require(panel.contains(child),
                QString("%1 exceeds its panel at %2x%3")
                    .arg(QString::fromLatin1(label)).arg(width).arg(height));
    };

    navigate(window, "showDashboardPage");
    const QRect hero = named("FleetHero");
    const QRect metrics = named("MetricsStrip");
    require(hero.bottom() < metrics.top(),
            "Dashboard hero overlaps metric strip");
    stackedAboveTable(metrics, "Dashboard");

    navigate(window, "showAllVehicles");
    const QRect inspector = named("FleetInspector");
    const QRect summary = named("FleetSummary");
    const QRect search = named("FleetSearch");
    const QRect filter = named("FleetStatusFilter");
    require(inspector.bottom() < summary.top() && summary.bottom() < search.top(),
            "Fleet inspector, summary or filters overlap");
    require(search.right() < filter.left(), "Fleet search/filter controls overlap");
    stackedAboveTable(filter, "Fleet");

    navigate(window, "showRentPage");
    const QRect form = named("RentalFormPanel");
    const QRect quote = named("RentalQuotePanel");
    require(form.right() < quote.left(), "Rental form and estimate overlap");
    for (const char* field : {"RentalVehicleID", "RentalDays"}) {
        contained(named(field), form, field);
    }
    contained(named("QuoteTotal"), quote, "QuoteTotal");
    contained(visibleButton("Confirm rental"), form, "Confirm rental");
    stackedAboveTable(form, "New Rental");

    navigate(window, "showRentalRecordsPage");
    const QRect history = named("RentalActionPanel");
    contained(named("RentalHistoryFilter"), history, "History filter");
    contained(named("RentalRecordID"), history, "History record ID");
    contained(visibleButton("Archive returned"), history, "Archive returned");
    contained(visibleButton("Delete archived"), history, "Delete archived");
    stackedAboveTable(history, "Rental History");

    navigate(window, "showReturnPage");
    const QRect returns = named("RentalActionPanel");
    contained(named("ReturnRentalID"), returns, "Return ID");
    contained(visibleButton("Complete return"), returns, "Complete return");
    stackedAboveTable(returns, "Return Processing");
}

QByteArray contents(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Unable to read temporary persisted fixture: " + path);
    return file.readAll();
}

// Snapshot both existence and bytes: cancelled actions must neither create a
// new file nor rewrite an existing one (even with equivalent contents).
QPair<bool, QByteArray> fileSnapshot(const QString& path)
{
    const bool exists = QFile::exists(path);
    return {exists, exists ? contents(path) : QByteArray()};
}
}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    // Never instantiate the production/default-path MainWindow in this test.
    QTemporaryDir isolated;
    if (!isolated.isValid()) {
        std::cerr << "Cannot create disposable rental test directory\n";
        return 1;
    }

    QStringList expectedDialogs;
    QStringList observedDialogs;
    bool unexpectedDialog = false;
    bool approveConfirmations = true;
    // Modal confirmation and information boxes have nested event loops.
    // Drive exactly the expected dialog titles, not arbitrary "Yes" prompts.
    QTimer dialogDriver;
    QObject::connect(&dialogDriver, &QTimer::timeout, &application, [&]() {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        const QString title = dialog->windowTitle();
        const int next = observedDialogs.size();
        const bool expected = next < expectedDialogs.size() &&
                              title == expectedDialogs.at(next);
        observedDialogs.append(title);
        if (!expected)
            unexpectedDialog = true;
        const auto choice = expected && (dialog->standardButtons() & QMessageBox::Yes)
                                ? (approveConfirmations ? QMessageBox::Yes : QMessageBox::No)
                                : (dialog->standardButtons() & QMessageBox::Ok)
                                      ? QMessageBox::Ok : QMessageBox::No;
        if (QAbstractButton* press = dialog->button(choice))
            press->click();
        else
            dialog->reject();
    });
    dialogDriver.start(5);

    auto perform = [&](MainWindow& window, const QString& label,
                       const QStringList& dialogs) {
        expectedDialogs = dialogs;
        observedDialogs.clear();
        unexpectedDialog = false;
        button(window, label)->click();
        require(!unexpectedDialog && observedDialogs == expectedDialogs,
                QString("Unexpected dialogs after %1: expected [%2], observed [%3]")
                    .arg(label, expectedDialogs.join(", "), observedDialogs.join(", ")));
    };

    auto performEnter = [&](QLineEdit* field, const QStringList& dialogs) {
        expectedDialogs = dialogs;
        observedDialogs.clear();
        unexpectedDialog = false;
        // QMessageBox nested event loops can leave the Qt offscreen platform
        // without an active top-level window. Restore the visible owner first.
        QApplication::setActiveWindow(field->window());
        field->setFocus(Qt::TabFocusReason);
        QApplication::processEvents();
        require(field->hasFocus(),
                QString("Keyboard target must receive focus (visible=%1, "
                        "activeWindow=%2, focusedWidget=%3)")
                    .arg(field->isVisible())
                    .arg(QApplication::activeWindow() == field->window())
                    .arg(QApplication::focusWidget()
                             ? QApplication::focusWidget()->objectName()
                             : QStringLiteral("<none>")));
        pressKey(field, Qt::Key_Return);
        require(!unexpectedDialog && observedDialogs == expectedDialogs,
                QString("Unexpected Enter dialogs: expected [%1], observed [%2]")
                    .arg(expectedDialogs.join(", "), observedDialogs.join(", ")));
    };

    try {
        // Photo must decode from a local Qt resource, not an unreachable
        // network URL. This verifies bundling only, not photographic taste.
        const QPixmap rangerPhoto(":/portfolio/ford_ranger.jpg");
        require(!rangerPhoto.isNull() && rangerPhoto.width() >= 1000 &&
                rangerPhoto.height() >= 500,
                "Licensed review-only Ranger photograph must decode from Qt resources");
        const QString vehiclesPath = isolated.filePath("vehicles.txt");
        const QString rentalsPath = isolated.filePath("rentals.txt");
        // Exercise the same five real Qt workspaces at both required sizes
        // before any transaction can alter the disposable test data.
        {
            MainWindow layoutWindow(isolated.path());
            layoutWindow.showNormal();
            verifyLayoutAtSize(layoutWindow, 1280, 720);
            verifyLayoutAtSize(layoutWindow, 1500, 860);
            // The dashboard hero is an actionable navigation surface, not
            // a second rental path with independent business logic.
            navigate(layoutWindow, "showDashboardPage");
            const auto vehicleBefore = fileSnapshot(vehiclesPath);
            const auto rentalsBefore = fileSnapshot(rentalsPath);
            QPushButton* heroAction = widget<QPushButton>(layoutWindow, "HeroRentalAction");
            require(heroAction->isVisible() && heroAction->isEnabled(),
                    "Dashboard's rental CTA is visible and available");
            heroAction->click();
            QApplication::processEvents();
            require(widget<QLabel>(layoutWindow, "PageTitle")->text() == "Rent Vehicle" &&
                    widget<QWidget>(layoutWindow, "RentalFormPanel")->isVisible(),
                    "Dashboard CTA opens the existing New Rental workspace");
            require(fileSnapshot(vehiclesPath) == vehicleBefore &&
                    fileSnapshot(rentalsPath) == rentalsBefore,
                    "Dashboard navigation never creates or rewrites rental data");
            std::cout << "PASS: five Qt workspaces at 1280x720 and 1500x860 "
                         "without primary-panel clipping/overlap\n";
        }
        {
            auto window = std::make_unique<MainWindow>(isolated.path());
            window->showNormal();
            window->resize(1280, 720);
            navigate(*window, "showAddPage");

            auto* id = widget<QLineEdit>(*window, "AddVehicleID");
            require(id->text() == "C001", "First generated car ID is C001");
            widget<QLineEdit>(*window, "AddVehicleBrand")->setText("Fictional");
            widget<QLineEdit>(*window, "AddVehicleModel")->setText("Test Hatch");
            widget<QLineEdit>(*window, "AddVehiclePrice")->setText("125.00");
            widget<QLineEdit>(*window, "AddVehicleExtra")->setText("4");
            perform(*window, "Save Vehicle", {"Confirm Add Vehicle", "Vehicle Added"});

            // Add a second vehicle using the existing motorcycle form.
            button(*window, "Motorcycle")->click();
            const QString motorcycleId = id->text();
            require(motorcycleId.startsWith("M"), "Motorcycle receives an M-prefixed identifier");
            widget<QLineEdit>(*window, "AddVehicleBrand")->setText("Fictional");
            widget<QLineEdit>(*window, "AddVehicleModel")->setText("Test Bike");
            widget<QLineEdit>(*window, "AddVehiclePrice")->setText("75.00");
            widget<QLineEdit>(*window, "AddVehicleExtra")->setText("500");
            perform(*window, "Save Vehicle", {"Confirm Add Vehicle", "Vehicle Added"});

            navigate(*window, "showAllVehicles");
            require(window->findChild<QTableWidget*>()->rowCount() == 2,
                    "GUI fleet table reflects both created vehicles");
            verifyTableStatusContrast(window->findChild<QTableWidget*>()->item(0, 5), "Available");

            navigate(*window, "showRentPage");
            auto* rentalVehicle = widget<QLineEdit>(*window, "RentalVehicleID");
            auto* confirm = button(*window, "Confirm rental");
            rentalVehicle->setText("C001");
            widget<QSpinBox>(*window, "RentalDays")->setValue(2);
            require(!confirm->isEnabled(), "Customer gating prevents incomplete rental");
            require(widget<QLabel>(*window, "QuoteTotal")->text() == "R250.00",
                    "Quote is derived from daily rate and two-day duration");
            QLineEdit* customer = nullptr;
            QLineEdit* phone = nullptr;
            for (QLineEdit* entry : window->findChildren<QLineEdit*>()) {
                if (entry->placeholderText() == "Customer full name") customer = entry;
                if (entry->placeholderText() == "e.g. 082 123 4567") phone = entry;
            }
            require(customer && phone, "Customer input fields exist");
            // Tab from Vehicle ID must reach Customer Name in the visual form.
            // The offscreen Qt platform has no physical window manager. Give
            // the visible test window an explicit Qt application focus owner.
            // This is *not* evidence of physical Windows focus behaviour.
            QApplication::setActiveWindow(window.get());
            rentalVehicle->setFocus(Qt::TabFocusReason);
            QApplication::processEvents();
            require(rentalVehicle->hasFocus(),
                    QString("Rental ID must receive focus (visible=%1, "
                            "windowActive=%2, focusedWidget=%3)")
                        .arg(rentalVehicle->isVisible())
                        .arg(QApplication::activeWindow() == window.get())
                        .arg(QApplication::focusWidget()
                                 ? QApplication::focusWidget()->objectName()
                                 : QStringLiteral("<none>")));
            pressKey(rentalVehicle, Qt::Key_Tab);
            require(customer->hasFocus(), "Tab from Vehicle ID reaches Customer Name");
            customer->setText("Fictional Test Customer");
            phone->setText("0000000000");
            require(confirm->isEnabled(), "Completed rental details enable confirmation");

            // A customer pressing No must leave inventory and rental files
            // exactly untouched, including whether rentals.txt even exists.
            const auto cancelledRentVehicles = fileSnapshot(vehiclesPath);
            const auto cancelledRentRentals = fileSnapshot(rentalsPath);
            approveConfirmations = false;
            perform(*window, "Confirm rental", {"Confirm Rental"});
            approveConfirmations = true;
            require(fileSnapshot(vehiclesPath) == cancelledRentVehicles &&
                    fileSnapshot(rentalsPath) == cancelledRentRentals,
                    "Declined rental must not create/rewrite persistent records");
            {
                RentalSystem disk(isolated.path());
                require(disk.getActiveRentals().isEmpty() &&
                        !disk.findVehicleById("C001")->getIsRented(),
                        "Declined rental must not reserve the vehicle");
            }
            require(confirm->isEnabled(),
                    "Declined rental keeps the complete form available");
            perform(*window, "Confirm rental", {"Confirm Rental", "Rental Created"});

            // Rental cannot be booked a second time through the UI.
            rentalVehicle->setText("C001");
            customer->setText("Fictional Other Customer");
            phone->setText("0000000000");
            require(!confirm->isEnabled(), "Active rental prevents another confirmation");
            {
                RentalSystem disk(isolated.path());
                require(disk.getActiveRentals().size() == 1,
                        "Exactly one rental was saved");
                require(disk.findVehicleById("C001")->getIsRented(),
                        "Persisted vehicle is unavailable");
                require(disk.findRentalById("R001")->getTotalCost() == 250.00,
                        "Persisted rental total is correct");
            }
        }

        // Reopen a brand new Qt MainWindow against the same disposable data.
        const QByteArray beforeRestartVehicles = contents(vehiclesPath);
        const QByteArray beforeRestartRentals = contents(rentalsPath);
        {
            auto window = std::make_unique<MainWindow>(isolated.path());
            window->showNormal();
            window->resize(1280, 720);
            require(contents(vehiclesPath) == beforeRestartVehicles &&
                    contents(rentalsPath) == beforeRestartRentals,
                    "Reopening the real GUI does not rewrite source files");

            navigate(*window, "showAllVehicles");
            verifyTableStatusContrast(window->findChild<QTableWidget*>()->item(0, 5), "Rented");

            navigate(*window, "showRentalRecordsPage");
            require(window->findChild<QTableWidget*>()->rowCount() == 1,
                    "Saved active contract appears in Rental History");
            widget<QLineEdit>(*window, "RentalRecordID")->setText("R001");
            perform(*window, "Archive returned", {"Archive Error"});
            perform(*window, "Delete archived", {"Delete Error"});

            navigate(*window, "showReturnPage");
            auto* returnId = widget<QLineEdit>(*window, "ReturnRentalID");
            returnId->setText("R001");
            const auto cancelledReturnVehicles = fileSnapshot(vehiclesPath);
            const auto cancelledReturnRentals = fileSnapshot(rentalsPath);
            approveConfirmations = false;
            performEnter(returnId, {"Confirm Return"});
            approveConfirmations = true;
            require(fileSnapshot(vehiclesPath) == cancelledReturnVehicles &&
                    fileSnapshot(rentalsPath) == cancelledReturnRentals,
                    "Declined return must preserve fleet and contract bytes");
            {
                RentalSystem disk(isolated.path());
                require(disk.getActiveRentals().size() == 1 &&
                        disk.findVehicleById("C001")->getIsRented(),
                        "Declined return must leave the contract active");
            }
            require(returnId->text() == "R001",
                    "Declined return retains the entered rental ID");
            performEnter(returnId, {"Confirm Return", "Return Completed"});
            returnId->setText("R001");
            performEnter(returnId, {"Return Error"});

            navigate(*window, "showRentalRecordsPage");
            verifyTableStatusContrast(window->findChild<QTableWidget*>()->item(0, 9), "Returned");
            widget<QLineEdit>(*window, "RentalRecordID")->setText("R001");
            const auto cancelledArchive = fileSnapshot(rentalsPath);
            approveConfirmations = false;
            perform(*window, "Archive returned", {"Confirm Archive"});
            approveConfirmations = true;
            require(fileSnapshot(rentalsPath) == cancelledArchive,
                    "Declined archive cannot rewrite rental ledger");
            {
                RentalSystem disk(isolated.path());
                require(!disk.findRentalById("R001")->getIsArchived(),
                        "Declined archive leaves returned record unarchived");
            }
            perform(*window, "Archive returned", {"Confirm Archive", "Record Archived"});
            widget<QComboBox>(*window, "RentalHistoryFilter")->setCurrentIndex(3);
            verifyTableStatusContrast(window->findChild<QTableWidget*>()->item(0, 9), "Archived");
            widget<QLineEdit>(*window, "RentalRecordID")->setText("R001");
            const auto cancelledDeletion = fileSnapshot(rentalsPath);
            approveConfirmations = false;
            perform(*window, "Delete archived", {"Confirm Permanent Deletion"});
            approveConfirmations = true;
            require(fileSnapshot(rentalsPath) == cancelledDeletion,
                    "Declined permanent deletion must preserve exact ledger bytes");
            {
                RentalSystem disk(isolated.path());
                require(disk.findRentalById("R001") != nullptr &&
                        disk.findRentalById("R001")->getIsArchived(),
                        "Declined deletion retains the archived record");
            }
            perform(*window, "Delete archived", {"Confirm Permanent Deletion", "Record Deleted"});
        }
        {
            RentalSystem disk(isolated.path());
            require(disk.getVehicles().size() == 2, "Fleet remains after restart");
            require(disk.getActiveRentals().isEmpty(), "Returned occupancy persists");
            require(!disk.findVehicleById("C001")->getIsRented(),
                    "Previously booked vehicle is available");
            require(disk.getRentals().isEmpty(), "Archived deletion persists after restart");
        }
        std::cout << "PASS: real Qt Widgets add, rent, guard, restart, return, "
                     "archive and delete using isolated fictional data\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Qt lifecycle regression failed: " << error.what() << '\n';
        return 1;
    }
}
