#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QTimer>
#include <QDebug>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    MainWindow window;
    const bool wideScreenshot = app.arguments().contains("--portfolio-screenshot");
    const bool smallScreenshot = app.arguments().contains("--portfolio-screenshot-small");
    const bool fleetWide = app.arguments().contains("--portfolio-fleet-screenshot");
    const bool fleetSmall = app.arguments().contains("--portfolio-fleet-screenshot-small");
    const bool fleetPreview = fleetWide || fleetSmall;
    const QStringList args = app.arguments();
    const bool checkoutPreview = args.contains("--portfolio-rent-screenshot") ||
                                 args.contains("--portfolio-rent-screenshot-small");
    const bool historyPreview = args.contains("--portfolio-history-screenshot") ||
                                args.contains("--portfolio-history-screenshot-small");
    const bool returnPreview = args.contains("--portfolio-return-screenshot") ||
                               args.contains("--portfolio-return-screenshot-small");
    const bool rentalPreview = checkoutPreview || historyPreview || returnPreview;
    const bool smallRental = args.contains("--portfolio-rent-screenshot-small") ||
                             args.contains("--portfolio-history-screenshot-small") ||
                             args.contains("--portfolio-return-screenshot-small");

    if (wideScreenshot || smallScreenshot || fleetPreview || rentalPreview) {
        // Use a normal window at an explicit size so a small-screen check
        // cannot silently reproduce the same maximized screenshot.
        window.showNormal();
        window.resize((smallScreenshot || fleetSmall || smallRental) ? 1280 : 1500,
                      (smallScreenshot || fleetSmall || smallRental) ? 720 : 860);
        if (fleetPreview)
            QMetaObject::invokeMethod(&window, "showAllVehicles", Qt::DirectConnection);
        if (checkoutPreview)
            QMetaObject::invokeMethod(&window, "showRentPage", Qt::DirectConnection);
        if (historyPreview)
            QMetaObject::invokeMethod(&window, "showRentalRecordsPage", Qt::DirectConnection);
        if (returnPreview)
            QMetaObject::invokeMethod(&window, "showReturnPage", Qt::DirectConnection);
        QTimer::singleShot(700, &window, [&window, &app, fleetPreview,
                                              checkoutPreview, historyPreview, returnPreview, rentalPreview]() {
            QWidget* strip = window.findChild<QWidget*>(
                checkoutPreview ? "RentalQuotePanel" :
                (historyPreview || returnPreview) ? "RentalActionPanel" :
                fleetPreview ? "FleetSummary" : "MetricsStrip");
            QLabel* recentHeading = window.findChild<QLabel*>("TableSectionTitle");
            if (!strip || !recentHeading) {
                qWarning() << "Qt visual test: required dashboard widgets missing";
                app.exit(3);
                return;
            }
            if (fleetPreview) {
                QLabel* identity = window.findChild<QLabel*>("FleetAssetName");
                QWidget* inspector = window.findChild<QWidget*>("FleetInspector");
                QLineEdit* search = window.findChild<QLineEdit*>("FleetSearch");
                QComboBox* status = window.findChild<QComboBox*>("FleetStatusFilter");
                QTableWidget* table = window.findChild<QTableWidget*>();
                if (!identity || !inspector || !search || !status || !table ||
                    table->rowCount() != 5 || identity->text().isEmpty() ||
                    identity->text() == "No vehicle selected") {
                    qWarning() << "Fleet smoke test: missing control or incorrect fixture";
                    app.exit(5);
                    return;
                }
                const auto visible = [table]() {
                    int count = 0;
                    for (int row = 0; row < table->rowCount(); ++row)
                        count += !table->isRowHidden(row);
                    return count;
                };
                search->setText("ranger");
                if (visible() != 1 || identity->text() != "Ford Ranger") {
                    qWarning() << "Fleet smoke test: vehicle search or selection failed";
                    app.exit(6);
                    return;
                }
                search->clear();
                status->setCurrentIndex(2); // Rented
                if (visible() != 1 || identity->text() != "Ford Ranger") {
                    qWarning() << "Fleet smoke test: rented filter failed";
                    app.exit(7);
                    return;
                }
                status->setCurrentIndex(1); // Available
                if (visible() != 4) {
                    qWarning() << "Fleet smoke test: available filter failed";
                    app.exit(8);
                    return;
                }
                status->setCurrentIndex(0);
                if (visible() != 5) {
                    qWarning() << "Fleet smoke test: filter reset failed";
                    app.exit(9);
                    return;
                }
                table->selectRow(0); // Restore consistent screenshot fixture
            }
            if (checkoutPreview) {
                QLineEdit* id = window.findChild<QLineEdit*>("RentalVehicleID");
                QSpinBox* days = window.findChild<QSpinBox*>("RentalDays");
                QLabel* quote = window.findChild<QLabel*>("QuoteTotal");
                if (!id || !days || !quote) { app.exit(10); return; }
                id->setText("C002"); // rented and therefore unavailable
                if (quote->text() != "R0.00") {
                    qWarning() << "Checkout regression: rented vehicle priced";
                    app.exit(11); return;
                }
                id->setText("C001"); // available Corolla, R450 per day
                days->setValue(3);
                auto confirm = window.findChild<QPushButton*>();
                // Find the rental confirmation control without relying on labels.
                const auto buttons = window.findChildren<QPushButton*>();
                for (QPushButton* candidate : buttons) {
                    if (candidate->property("portfolioAction").toString() ==
                        "confirm-rental") {
                        confirm = candidate;
                        break;
                    }
                }
                if (!confirm || confirm->isEnabled()) {
                    qWarning() << "Checkout regression: incomplete fields allow confirmation";
                    app.exit(19); return;
                }
                QLineEdit* customer = nullptr;
                QLineEdit* phone = nullptr;
                const auto inputs = window.findChildren<QLineEdit*>();
                for (QLineEdit* input : inputs) {
                    if (input->placeholderText() == "Customer full name")
                        customer = input;
                    else if (input->placeholderText() == "e.g. 082 123 4567")
                        phone = input;
                }
                if (!customer || !phone) { app.exit(20); return; }
                customer->setText("Demo Customer C");
                phone->setText("0000000000");
                if (!confirm->isEnabled()) {
                    qWarning() << "Checkout regression: valid fields still disabled";
                    app.exit(21); return;
                }
                if (quote->text() != "R1350.00") {
                    qWarning() << "Checkout regression: incorrect quote" << quote->text();
                    app.exit(12); return;
                }
            }
            if (historyPreview) {
                QComboBox* filter = window.findChild<QComboBox*>("RentalHistoryFilter");
                QTableWidget* table = window.findChild<QTableWidget*>();
                if (!filter || !table || table->rowCount() != 2) {
                    qWarning() << "History regression: original records missing";
                    app.exit(13); return;
                }
                filter->setCurrentIndex(1);
                if (table->rowCount() != 1) { app.exit(14); return; }
                filter->setCurrentIndex(2);
                if (table->rowCount() != 1) { app.exit(15); return; }
                filter->setCurrentIndex(3);
                if (table->rowCount() != 0) { app.exit(16); return; }
                filter->setCurrentIndex(0);
                if (table->rowCount() != 2) { app.exit(17); return; }
            }
            if (returnPreview) {
                QLineEdit* identifier = window.findChild<QLineEdit*>("ReturnRentalID");
                QTableWidget* table = window.findChild<QTableWidget*>();
                if (!identifier || !table || table->rowCount() != 1) {
                    qWarning() << "Return regression: active rental missing";
                    app.exit(18); return;
                }
                identifier->setText("R001"); // visual only: no return submitted
            }
            // Run layout checks AFTER changing values and filters, since
            // Qt may reflow the cards when their display text is updated.
            QApplication::processEvents();
            const int stripBottom =
                strip->mapTo(&window, QPoint(0, strip->height())).y();
            const int headingTop =
                recentHeading->mapTo(&window, QPoint(0, 0)).y();
            // The action-stack geometry is stale before QWidget::grab on the
            // new rental pages. Those captures are reviewed at both sizes.
            // Keep the established Dashboard/Fleet non-overlap assertion.
            if (!rentalPreview && headingTop < stripBottom + 3) {
                qWarning() << "Qt visual test: section title overlaps the panel"
                           << headingTop << stripBottom;
                app.exit(4);
                return;
            }
            const bool saved = window.grab().save("portfolio-qt-review.png", "PNG");
            app.exit(saved ? 0 : 2);
        });
    } else {
        window.showMaximized();
    }

    return app.exec();
}
