#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <QLineEdit>
#include <QTableWidget>
#include <QLabel>
#include <QStackedWidget>
#include <QFrame>
#include <QDateEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QResizeEvent>

#include "rentalsystem.h"

// Administrator/employee interface for managing vehicles and rental transactions.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    // Test-only storage injection; the ordinary application retains its existing path.
    explicit MainWindow(const QString& dataDirectory, QWidget* parent = nullptr);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    RentalSystem rentalSystem;

    QWidget* centralWidget;
    QWidget* sidebarWidget;
    QWidget* mainContentWidget;

    QStackedWidget* actionStack;
    QTableWidget* dataTable;
    QLabel* emptyTableLabel;

    QLabel* pageTitleLabel;
    QLabel* pageSubtitleLabel;
    QLabel* tableSectionTitleLabel;

    // Dashboard values.
    QLabel* fleetHeroValue;
    QLabel* fleetHeroHint;
    QLabel* totalVehiclesValue;
    QLabel* availableVehiclesValue;
    QLabel* rentedVehiclesValue;
    QLabel* activeRentalsValue;
    QLabel* carsValue;
    QLabel* motorcyclesValue;
    QLabel* utilisationValue;
    QLabel* revenueValue;

    // Fleet-only inspector and search filters.
    QLineEdit* fleetSearchEdit = nullptr;
    QComboBox* fleetStatusCombo = nullptr;
    QLabel* fleetAssetPhoto = nullptr;
    QLabel* fleetAssetName = nullptr;
    QLabel* fleetAssetMeta = nullptr;
    QLabel* fleetAssetRate = nullptr;
    QLabel* fleetAssetStatus = nullptr;
    QLabel* fleetAssetCredit = nullptr;

    // All Vehicles summary values.
    QLabel* allTotalVehiclesValue;
    QLabel* allAvailableVehiclesValue;
    QLabel* allRentedVehiclesValue;
    QLabel* allActiveRentalsValue;
    QLabel* allCarsValue;
    QLabel* allMotorcyclesValue;

    // Available Vehicles summary values.
    QLabel* availableTotalValue;
    QLabel* availableCarsValue;
    QLabel* availableMotorcyclesValue;

    // Sidebar buttons.
    QPushButton* dashboardButton;
    QPushButton* addPageButton;
    QPushButton* removePageButton;
    QPushButton* searchPageButton;
    QPushButton* allVehiclesButton;
    QPushButton* availableVehiclesButton;
    QPushButton* rentPageButton;
    QPushButton* rentalRecordsButton;
    QPushButton* returnPageButton;
    QPushButton* exitButton;

    // Add vehicle form.
    QString selectedVehicleType;
    QPushButton* carTypeButton;
    QPushButton* motorcycleTypeButton;
    QLabel* addExtraLabel;
    QLineEdit* addIdEdit;
    QLineEdit* addBrandEdit;
    QLineEdit* addModelEdit;
    QLineEdit* addPriceEdit;
    QLineEdit* addExtraEdit;

    // Search form.
    QLineEdit* searchIdEdit;

    // Rent transaction form.
    QLineEdit* rentVehicleIdEdit;
    QLineEdit* rentCustomerNameEdit;
    QLineEdit* rentPhoneEdit;
    QDateEdit* rentStartDateEdit;
    QSpinBox* rentDaysSpinBox;
    QLabel* rentLookupMessageLabel;
    QLabel* rentVehicleValue;
    QLabel* rentAvailabilityValue;
    QLabel* rentPriceValue;
    QLabel* rentExpectedReturnValue;
    QLabel* rentTotalValue;
    QPushButton* rentConfirmButton;

    // Rental history controls.
    QComboBox* rentalFilterCombo;
    QLineEdit* rentalRecordIdEdit;

    // Return form.
    QLineEdit* returnRentalIdEdit;
    QDateEdit* actualReturnDateEdit;

    // Remove form.
    QLineEdit* removeIdEdit;

    void setupUi();
    void setupSidebar();
    void setupMainContent();
    void applyStyles();

    QWidget* createDashboardPage();
    QWidget* createAddPage();
    QWidget* createSearchPage();
    QWidget* createRentPage();
    QWidget* createRentalRecordsPage();
    QWidget* createReturnPage();
    QWidget* createRemovePage();
    QWidget* createAllVehiclesSummaryPage();
    QWidget* createAvailableVehiclesSummaryPage();

    QFrame* createStatCard(const QString& title, QLabel*& valueLabel);

    void configureVehicleTable();
    void configureRentalTable();
    void resizeRentalTableColumns();
    void configureDashboardRentalTable();
    void refreshVehicleTable(const QList<Vehicle*>& vehicles);
    void refreshRentalTable(const QList<Rental*>& rentals);
    void refreshDashboardRecentRentals();
    void updateDashboardCards();
    void filterFleetTable();
    void updateFleetInspector();

    QList<Vehicle*> availableVehicles() const;
    QList<Rental*> filteredRentals() const;
    QList<Rental*> recentRentals() const;

    void setActiveButton(QPushButton* activeButton);
    void showEmptyTableMessage(const QString& title, const QString& subtitle);
    void hideEmptyTableMessage();
    void showWarningMessage(const QString& title,
                            const QString& mainText,
                            const QString& details);

    void updateGeneratedVehicleId();
    void clearAddFields();
    void clearRentalForm();
    void updateAddExtraLabel();

    static QString currency(double value);
    static bool containsFileSeparator(const QString& value);

private slots:
    void showDashboardPage();
    void showAddPage();
    void showSearchPage();
    void showAllVehicles();
    void showAvailableVehicles();
    void showRentPage();
    void showRentalRecordsPage();
    void showReturnPage();
    void showRemovePage();

    void addVehicle();
    void searchVehicle();
    void rentVehicle();
    void returnVehicle();
    void removeVehicle();
    void archiveRentalRecord();
    void deleteArchivedRentalRecord();

    void updateRentalEstimate();
    void refreshRentalRecordsFilter();
    void exitApplication();
};

#endif
