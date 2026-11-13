#include "mainwindow.h"

#include "car.h"
#include "motorcycle.h"
#include "rental.h"

#include <QApplication>
#include <QAbstractSpinBox>
#include <QBrush>
#include <QColor>
#include <QDoubleValidator>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QMessageBox>
#include <QPixmap>
#include <QScreen>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include <memory>

namespace
{
bool confirmAction(QWidget* parent,
                   const QString& title,
                   const QString& message)
{
    return QMessageBox::question(parent,
                                 title,
                                 message,
                                 QMessageBox::Yes | QMessageBox::No)
           == QMessageBox::Yes;
}

QLabel* makeFieldLabel(const QString& text)
{
    QLabel* label = new QLabel(text);
    label->setStyleSheet("font-size: 12px; font-weight: 600; color: #374151;");
    return label;
}
}

MainWindow::MainWindow(QWidget* parent)
    : MainWindow(QString(), parent)
{
}

MainWindow::MainWindow(const QString& dataDirectory, QWidget* parent)
    : QMainWindow(parent), rentalSystem(dataDirectory)
{
    setupUi();
    showDashboardPage();
}

void MainWindow::setupUi()
{
    centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    setupSidebar();
    setupMainContent();

    QHBoxLayout* rootLayout = new QHBoxLayout();
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(sidebarWidget);
    rootLayout->addWidget(mainContentWidget, 1);
    centralWidget->setLayout(rootLayout);

    setWindowTitle("Vehicle Rental Management System");
    setMinimumSize(1280, 720);
    resize(1500, 860);

    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen != nullptr) {
        const QRect available = screen->availableGeometry();
        move(available.center() - rect().center());
    }

    applyStyles();
}

void MainWindow::applyStyles()
{
    // Portfolio visual experiment: surface styles only; rental workflows are untouched.
    setStyleSheet(
        "QMainWindow { background-color: #f5f4ef; }"
        "QWidget#Sidebar { background-color: #16252b; }"

        "QFrame#BrandPanel { background: transparent; }"
        "QLabel#BrandMark { color: #16252b; background: #cff375; border-radius: 9px; font-size: 17px; font-weight: 900; }"
        "QLabel#BrandName { color: #ffffff; font-size: 13px; font-weight: 800; }"
        "QLabel#BrandDescriptor { color: #a5c2b5; font-size: 9px; }"
        "QLabel#SidebarGroup { color: #7fa299; font-size: 10px; font-weight: bold; padding: 7px 18px 5px; }"
        "QPushButton#SidebarButton { color: #b9c9c5; background: transparent; border: none; text-align: left; padding: 12px 18px; font-size: 13px; border-radius: 7px; }"
        "QPushButton#SidebarButton:hover { background-color: #293c40; color: #ffffff; }"
        "QPushButton#SidebarButton:checked { background-color: #cff375; color: #16252b; font-weight: bold; }"
        "QPushButton#ExitButton { background-color: #334448; color: #f7faf7; border: 1px solid #42565b; border-radius: 8px; padding: 10px 12px; font-weight: bold; margin: 0 12px; }"
        "QPushButton#ExitButton:hover { background-color: #5c3333; }"
        "QWidget#MainContent { background-color: #f5f4ef; }"
        "QLabel#PageTitle { font-size: 27px; font-weight: 750; color: #172830; }"
        "QLabel#PageSubtitle { font-size: 13px; color: #657774; }"
        "QLabel#TableSectionTitle { font-size: 15px; font-weight: bold; color: #25393b; padding-top: 6px; }"
        "QFrame#RentalActionPanel { background: #fffefa; border: 1px solid #e1e5dd; border-radius: 12px; }"
        "QLabel#ActionPanelKicker { color: #3d7565; font-size: 11px; font-weight: bold; }"
        "QLabel#ActionPanelNote { color: #73857c; font-size: 11px; }"
        "QFrame#RentalFormPanel { background: #fffefa; border: 1px solid #e1e5dd; border-radius: 13px; }"
        "QLabel#RentalKicker { color: #3d7565; font-size: 10px; font-weight: bold; }"
        "QLabel#RentalPanelTitle { color: #172830; font-size: 22px; font-weight: bold; }"
        "QLabel#SameDayNote { background: #edf3e7; color: #2e584a; border-radius: 6px; padding: 8px; font-size: 12px; }"
        "QLabel#RentalLookupHint { color: #657774; font-size: 12px; }"
        "QFrame#RentalQuotePanel { background: #172a30; border: 1px solid #2c4244; border-radius: 13px; }"
        "QLabel#QuoteKicker { color: #cff375; font-size: 11px; font-weight: bold; }"
        "QLabel#QuoteFieldCaption { color: #adc3bc; font-size: 11px; }"
        "QLabel#QuoteFieldValue { color: #ffffff; font-size: 12px; font-weight: 600; }"
        "QLabel#QuoteTotal { color: #ffffff; font-size: 34px; font-weight: bold; }"
        "QLabel#QuoteDisclaimer { color: #a9c2b8; font-size: 10px; }"
        "QFrame#QuoteRule { background: #40575a; border: none; }"
        "QFrame#FleetInspector { background: #172a30; border: 1px solid #2c4244; border-radius: 14px; }"
        "QLabel#FleetAssetPhoto { background: #263a3d; color: #bbd3cb; border-radius: 8px; font-size: 10px; font-weight: bold; }"
        "QLabel#FleetAssetCredit { color: #a7bbb6; font-size: 9px; }"
        "QLabel#FleetAssetKicker { color: #cff375; font-size: 10px; font-weight: bold; }"
        "QLabel#FleetAssetName { color: #ffffff; font-size: 27px; font-weight: bold; }"
        "QLabel#FleetAssetMeta { color: #ceded8; font-size: 12px; }"
        "QLabel#FleetAssetPriceLabel { color: #a8c1b5; font-size: 10px; font-weight: bold; }"
        "QLabel#FleetAssetRate { color: #ffffff; font-size: 27px; font-weight: bold; }"
        "QFrame#FleetSummary { background: #f9faf5; border: 1px solid #e4e8df; border-radius: 9px; }"
        "QLabel#FleetSummaryLabel { color: #687e75; font-size: 10px; }"
        "QLabel#FleetSummaryValue { color: #1b302f; font-size: 17px; font-weight: bold; }"
        "QFrame#FleetHero { background-color: #172a30; border: 1px solid #2c4244; border-radius: 14px; }"
        "QLabel#FleetHeroKicker { color: #cff375; font-size: 11px; font-weight: bold; }"
        "QLabel#FleetHeroValue { color: #ffffff; font-size: 44px; font-weight: bold; }"
        "QLabel#FleetHeroHint { color: #d1f69a; font-size: 12px; font-weight: bold; }"
        "QLabel#FleetHeroDescription { color: #d4dfd9; font-size: 12px; }"
        "QLabel#FleetPhotoCredit { color: #e0eae2; font-size: 10px; }"
        "QPushButton#HeroRentalAction { background: #cff375; color: #172830; border: 1px solid #b7d965; border-radius: 7px; padding: 7px 12px; font-size: 12px; font-weight: bold; }"
        "QPushButton#HeroRentalAction:hover { background: #b6e358; }"
        "QPushButton#HeroRentalAction:focus { border: 2px solid #fffefa; padding: 6px 11px; }"
        "QFrame#Card, QFrame#FormCard { background-color: #fffefa; border: 1px solid #e1e5dd; border-radius: 11px; }"
        "QFrame#SecondaryCard { background-color: #f9faf5; border: 1px solid #e4e8df; border-radius: 10px; }"
        "QFrame#MetricsStrip { background-color: #f9faf5; border: 1px solid #e4e8df; border-radius: 10px; }"
        "QLabel#CompactTitle { font-size: 11px; color: #647671; }"
        "QLabel#CompactValue { font-size: 18px; font-weight: 700; color: #172830; }"
        "QLabel#CardTitle { font-size: 12px; color: #60746f; }"
        "QLabel#CardValue { font-size: 24px; font-weight: bold; color: #1b2d31; }"
        "QFrame#SecondaryCard QLabel#CardValue { font-size: 20px; }"
        "QLineEdit, QDateEdit, QSpinBox, QComboBox { background-color: #fffefa; border: 1px solid #cbd6ce; border-radius: 7px; padding: 5px 8px; font-size: 13px; min-height: 20px; color: #1b2d31; }"
        "QLineEdit:focus, QDateEdit:focus, QSpinBox:focus, QComboBox:focus { border: 2px solid #357668; }"
        "QPushButton#ActionButton { background-color: #cff375; color: #172830; border: none; border-radius: 8px; padding: 10px 16px; font-weight: bold; }"
        "QPushButton#ActionButton:hover { background-color: #b6e358; }"
        "QPushButton#ActionButton:disabled { background-color: #c5cbc0; color: #6d756c; }"
        "QPushButton#SecondaryButton { background-color: #e9ede5; color: #172830; border: 1px solid #dbe3d9; border-radius: 8px; padding: 10px 16px; font-weight: bold; }"
        "QPushButton#SecondaryButton:hover { background-color: #dfe8dc; }"
        "QPushButton#DangerButton { background-color: #ad4141; color: white; border: none; border-radius: 8px; padding: 10px 16px; font-weight: bold; }"
        "QPushButton#DangerButton:hover { background-color: #963636; }"
        "QPushButton#TypeButton { background-color: #f7f8f3; color: #172830; border: 1px solid #d5ded4; border-radius: 8px; padding: 9px 22px; font-weight: bold; }"
        "QPushButton#TypeButton:checked { background-color: #c3ec74; color: #172830; border-color: #87b941; }"
        // Real keyboard focus must be distinct from hover and checked states.
        "QPushButton#SidebarButton:focus { border: 2px solid #fffefa; padding: 10px 16px; }"
        "QPushButton#ActionButton:focus, QPushButton#SecondaryButton:focus, QPushButton#DangerButton:focus { border: 2px solid #274a45; padding: 8px 14px; }"
        "QPushButton#TypeButton:focus { border: 2px solid #274a45; padding: 7px 20px; }"
        "QPushButton#ExitButton:focus { border: 2px solid #fffefa; padding: 8px 10px; }"
        "QHeaderView::section { background-color: #25393b; color: #f5f8f1; padding: 7px 8px; border: none; font-weight: bold; }"
        "QTableWidget { background-color: #fffefa; color: #243436; border: 1px solid #e1e5dd; border-radius: 8px; alternate-background-color: #f5f7f0; selection-background-color: #e0f3bd; selection-color: #19312a; }"
    );
}

void MainWindow::setupSidebar()
{
    sidebarWidget = new QWidget();
    sidebarWidget->setObjectName("Sidebar");
    sidebarWidget->setFixedWidth(235);


    QFrame* brand = new QFrame();
    brand->setObjectName("BrandPanel");
    QHBoxLayout* brandLayout = new QHBoxLayout(brand);
    brandLayout->setContentsMargins(16, 19, 10, 18);
    brandLayout->setSpacing(11);
    QLabel* mark = new QLabel("RM");
    mark->setObjectName("BrandMark");
    mark->setAlignment(Qt::AlignCenter);
    mark->setFixedSize(41, 41);
    brandLayout->addWidget(mark);
    QVBoxLayout* brandWords = new QVBoxLayout();
    brandWords->setSpacing(2);
    QLabel* name = new QLabel("RENTAL MANAGER");
    name->setObjectName("BrandName");
    QLabel* subname = new QLabel("OPERATIONS CONSOLE");
    subname->setObjectName("BrandDescriptor");
    brandWords->addWidget(name);
    brandWords->addWidget(subname);
    brandLayout->addLayout(brandWords, 1);

    dashboardButton = new QPushButton("Overview");
    addPageButton = new QPushButton("Add Vehicle");
    removePageButton = new QPushButton("Remove Vehicle");
    searchPageButton = new QPushButton("Search Vehicle");
    allVehiclesButton = new QPushButton("Fleet Inventory");
    availableVehiclesButton = new QPushButton("Available Vehicles");
    rentPageButton = new QPushButton("New Rental");
    rentalRecordsButton = new QPushButton("Rental History");
    returnPageButton = new QPushButton("Process Return");
    exitButton = new QPushButton("Exit");

    const QList<QPushButton*> navigationButtons = {
        dashboardButton, allVehiclesButton, availableVehiclesButton,
        addPageButton, searchPageButton, removePageButton,
        rentPageButton, rentalRecordsButton, returnPageButton
    };
    for (QPushButton* button : navigationButtons) {
        button->setObjectName("SidebarButton");
        button->setCheckable(true);
    }
    exitButton->setObjectName("ExitButton");

    auto sectionTitle = [](const QString& title) {
        QLabel* group = new QLabel(title);
        group->setObjectName("SidebarGroup");
        return group;
    };
    QVBoxLayout* layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 12);
    layout->setSpacing(1);
    layout->addWidget(brand);
    layout->addWidget(sectionTitle("WORKSPACE"));
    layout->addWidget(dashboardButton);
    layout->addSpacing(11);
    layout->addWidget(sectionTitle("FLEET"));
    layout->addWidget(allVehiclesButton);
    layout->addWidget(availableVehiclesButton);
    layout->addWidget(addPageButton);
    layout->addWidget(searchPageButton);
    layout->addWidget(removePageButton);
    layout->addSpacing(11);
    layout->addWidget(sectionTitle("RENTALS"));
    layout->addWidget(rentPageButton);
    layout->addWidget(rentalRecordsButton);
    layout->addWidget(returnPageButton);
    layout->addStretch();
    layout->addWidget(exitButton);
    sidebarWidget->setLayout(layout);

    connect(dashboardButton, &QPushButton::clicked, this, &MainWindow::showDashboardPage);
    connect(addPageButton, &QPushButton::clicked, this, &MainWindow::showAddPage);
    connect(removePageButton, &QPushButton::clicked, this, &MainWindow::showRemovePage);
    connect(searchPageButton, &QPushButton::clicked, this, &MainWindow::showSearchPage);
    connect(allVehiclesButton, &QPushButton::clicked, this, &MainWindow::showAllVehicles);
    connect(availableVehiclesButton, &QPushButton::clicked, this, &MainWindow::showAvailableVehicles);
    connect(rentPageButton, &QPushButton::clicked, this, &MainWindow::showRentPage);
    connect(rentalRecordsButton, &QPushButton::clicked, this, &MainWindow::showRentalRecordsPage);
    connect(returnPageButton, &QPushButton::clicked, this, &MainWindow::showReturnPage);
    connect(exitButton, &QPushButton::clicked, this, &MainWindow::exitApplication);
}

void MainWindow::setupMainContent()
{
    mainContentWidget = new QWidget();
    mainContentWidget->setObjectName("MainContent");

    pageTitleLabel = new QLabel();
    pageTitleLabel->setObjectName("PageTitle");

    pageSubtitleLabel = new QLabel();
    pageSubtitleLabel->setObjectName("PageSubtitle");

    tableSectionTitleLabel = new QLabel();
    tableSectionTitleLabel->setObjectName("TableSectionTitle");

    actionStack = new QStackedWidget();
    // Dashboard height is set on its page, not globally. Other rental forms
    // must retain their established compact presentation.
    actionStack->addWidget(createDashboardPage());                  // 0
    actionStack->addWidget(createAddPage());                        // 1
    actionStack->addWidget(createSearchPage());                     // 2
    actionStack->addWidget(createRentPage());                       // 3
    actionStack->addWidget(createRentalRecordsPage());              // 4
    actionStack->addWidget(createReturnPage());                     // 5
    actionStack->addWidget(createRemovePage());                     // 6
    actionStack->addWidget(createAllVehiclesSummaryPage());         // 7
    actionStack->addWidget(createAvailableVehiclesSummaryPage());   // 8

    dataTable = new QTableWidget();
    dataTable->setShowGrid(false);
    dataTable->setAlternatingRowColors(true);
    dataTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    dataTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    dataTable->setSelectionMode(QAbstractItemView::SingleSelection);
    dataTable->verticalHeader()->setVisible(false);
    dataTable->verticalHeader()->setDefaultSectionSize(32);

    emptyTableLabel = new QLabel(dataTable->viewport());
    emptyTableLabel->setAlignment(Qt::AlignCenter);
    emptyTableLabel->setTextFormat(Qt::RichText);
    emptyTableLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    emptyTableLabel->setStyleSheet("color: #6b7280; background: transparent;");
    emptyTableLabel->hide();
    connect(dataTable, &QTableWidget::itemSelectionChanged,
            this, &MainWindow::updateFleetInspector);

    QVBoxLayout* layout = new QVBoxLayout();
    layout->setContentsMargins(28, 22, 28, 24);
    layout->setSpacing(7);
    layout->addWidget(pageTitleLabel);
    layout->addWidget(pageSubtitleLabel);
    layout->addWidget(actionStack);
    layout->addWidget(tableSectionTitleLabel);
    layout->addWidget(dataTable, 1);
    mainContentWidget->setLayout(layout);
}


QFrame* MainWindow::createStatCard(const QString& title, QLabel*& valueLabel)
{
    QFrame* card = new QFrame();
    card->setObjectName("Card");
    card->setMinimumHeight(88);

    QLabel* titleLabel = new QLabel(title);
    titleLabel->setObjectName("CardTitle");

    valueLabel = new QLabel("0");
    valueLabel->setObjectName("CardValue");

    QVBoxLayout* layout = new QVBoxLayout();
    layout->setContentsMargins(15, 11, 15, 11);
    layout->addWidget(titleLabel);
    layout->addWidget(valueLabel);
    card->setLayout(layout);

    return card;
}

QWidget* MainWindow::createDashboardPage()
{
    QWidget* page = new QWidget();
    QVBoxLayout* pageLayout = new QVBoxLayout();
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(11);

    // A photographic hero is a review-only enhancement; availability is still
    // calculated from real runtime vehicles, never hard-coded demo statistics.
    QFrame* hero = new QFrame();
    hero->setObjectName("FleetHero");
    hero->setFixedHeight(166);
    QHBoxLayout* heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(26, 14, 18, 14);
    heroLayout->setSpacing(18);
    QVBoxLayout* heroCopy = new QVBoxLayout();
    heroCopy->setSpacing(1);
    QLabel* heroKicker = new QLabel("RENTAL OPERATIONS  /  FLEET READINESS");
    heroKicker->setObjectName("FleetHeroKicker");
    fleetHeroValue = new QLabel("0 / 0");
    fleetHeroValue->setObjectName("FleetHeroValue");
    fleetHeroHint = new QLabel("NO VEHICLES LOADED");
    fleetHeroHint->setObjectName("FleetHeroHint");
    QLabel* description = new QLabel("Available vehicles across your rental fleet");
    description->setObjectName("FleetHeroDescription");
    heroCopy->addWidget(heroKicker);
    heroCopy->addWidget(fleetHeroValue);
    heroCopy->addWidget(fleetHeroHint);
    QHBoxLayout* heroBottom = new QHBoxLayout();
    heroBottom->setContentsMargins(0, 0, 0, 0);
    heroBottom->setSpacing(16);
    heroBottom->addWidget(description);
    QPushButton* newRentalAction = new QPushButton("Start rental");
    newRentalAction->setObjectName("HeroRentalAction");
    newRentalAction->setAccessibleName("Open New Rental");
    newRentalAction->setToolTip("Open the existing New Rental workspace");
    newRentalAction->setCursor(Qt::PointingHandCursor);
    connect(newRentalAction, &QPushButton::clicked, this, &MainWindow::showRentPage);
    heroBottom->addWidget(newRentalAction, 0, Qt::AlignVCenter);
    heroBottom->addStretch(1);
    heroCopy->addLayout(heroBottom);
    heroCopy->addStretch();
    heroLayout->addLayout(heroCopy, 3);

    QVBoxLayout* photograph = new QVBoxLayout();
    photograph->setSpacing(3);
    QLabel* photo = new QLabel();
    photo->setAccessibleName("Representative Ford Ranger vehicle photograph");
    photo->setToolTip("Illustrative model photograph; not a photograph of an actual rental fleet vehicle");
    photo->setAlignment(Qt::AlignCenter);
    photo->setFixedSize(280, 124);
    const QPixmap source(":/portfolio/ford_ranger.jpg");
    if (!source.isNull()) {
        QPixmap scaled = source.scaled(photo->size(), Qt::KeepAspectRatioByExpanding,
                                       Qt::SmoothTransformation);
        const int x = (scaled.width() - photo->width()) / 2;
        const int y = (scaled.height() - photo->height()) / 2;
        photo->setPixmap(scaled.copy(x, y, photo->width(), photo->height()));
    } else {
        photo->setText("Representative vehicle photo unavailable");
    }
    QLabel* photoCredit = new QLabel("Ford Ranger · Hyins / CC0 · illustrative");
    photoCredit->setObjectName("FleetPhotoCredit");
    photoCredit->setAlignment(Qt::AlignRight);
    photograph->addWidget(photo);
    photograph->addWidget(photoCredit);
    heroLayout->addLayout(photograph);
    pageLayout->addWidget(hero);

    QGridLayout* cards = new QGridLayout();
    cards->setContentsMargins(0, 0, 0, 0);
    cards->setHorizontalSpacing(11);
    cards->setVerticalSpacing(11);
    cards->addWidget(createStatCard("Total Vehicles", totalVehiclesValue), 0, 0);
    cards->addWidget(createStatCard("Available Vehicles", availableVehiclesValue), 0, 1);
    cards->addWidget(createStatCard("Active Rentals", activeRentalsValue), 0, 2);
    cards->addWidget(createStatCard("Fleet Utilisation", utilisationValue), 0, 3);

    // Four secondary indicators are grouped in one compact strip so the hero,
    // four primary KPIs and recent-rental table remain visible at desktop sizes.
    pageLayout->addLayout(cards);
    QFrame* metricStrip = new QFrame();
    metricStrip->setObjectName("MetricsStrip");
    metricStrip->setFixedHeight(58);
    QHBoxLayout* stripLayout = new QHBoxLayout(metricStrip);
    stripLayout->setContentsMargins(20, 7, 20, 7);
    stripLayout->setSpacing(18);
    auto appendCompactMetric = [stripLayout](const QString& title, QLabel*& value) {
        QVBoxLayout* group = new QVBoxLayout();
        group->setSpacing(1);
        QLabel* label = new QLabel(title);
        label->setObjectName("CompactTitle");
        value = new QLabel("0");
        value->setObjectName("CompactValue");
        group->addWidget(label);
        group->addWidget(value);
        stripLayout->addLayout(group, 1);
    };
    appendCompactMetric("Rented", rentedVehiclesValue);
    appendCompactMetric("Cars", carsValue);
    appendCompactMetric("Motorcycles", motorcyclesValue);
    appendCompactMetric("Booked Rental Value", revenueValue);
    pageLayout->addWidget(metricStrip);
    pageLayout->addStretch(1); // consume surplus space below the complete metric group
    page->setLayout(pageLayout);
    return page;
}

QWidget* MainWindow::createAddPage()
{
    QWidget* page = new QWidget();
    QFrame* card = new QFrame();
    card->setObjectName("FormCard");

    selectedVehicleType = "Car";

    carTypeButton = new QPushButton("Car");
    motorcycleTypeButton = new QPushButton("Motorcycle");
    carTypeButton->setObjectName("TypeButton");
    motorcycleTypeButton->setObjectName("TypeButton");
    carTypeButton->setCheckable(true);
    motorcycleTypeButton->setCheckable(true);
    carTypeButton->setChecked(true);

    addIdEdit = new QLineEdit();
    addBrandEdit = new QLineEdit();
    addModelEdit = new QLineEdit();
    addPriceEdit = new QLineEdit();
    addExtraEdit = new QLineEdit();
    addIdEdit->setObjectName("AddVehicleID");
    addBrandEdit->setObjectName("AddVehicleBrand");
    addModelEdit->setObjectName("AddVehicleModel");
    addPriceEdit->setObjectName("AddVehiclePrice");
    addExtraEdit->setObjectName("AddVehicleExtra");
    addExtraLabel = makeFieldLabel("Number of Doors");

    // The system still creates the ID automatically. The shorter label keeps
    // the form natural and avoids suggesting that the vehicle itself is automatic.
    addIdEdit->setReadOnly(true);
    addIdEdit->setStyleSheet("background-color: #f3f4f6; font-weight: bold;");
    addBrandEdit->setPlaceholderText("e.g. Toyota");
    addModelEdit->setPlaceholderText("e.g. Corolla");
    addPriceEdit->setPlaceholderText("e.g. 450.00");
    addExtraEdit->setPlaceholderText("e.g. 4");

    addPriceEdit->setValidator(new QDoubleValidator(0.01, 1000000.0, 2, addPriceEdit));
    addExtraEdit->setValidator(new QIntValidator(1, 10000, addExtraEdit));

    QPushButton* clearButton = new QPushButton("Clear Fields");
    QPushButton* saveButton = new QPushButton("Save Vehicle");
    clearButton->setObjectName("SecondaryButton");
    saveButton->setObjectName("ActionButton");

    QHBoxLayout* typeRow = new QHBoxLayout();
    typeRow->setSpacing(12);
    typeRow->addWidget(makeFieldLabel("Vehicle Type"));
    typeRow->addWidget(carTypeButton);
    typeRow->addWidget(motorcycleTypeButton);
    typeRow->addStretch();

    QGridLayout* form = new QGridLayout();
    form->setContentsMargins(24, 18, 24, 18);
    form->setHorizontalSpacing(22);
    form->setVerticalSpacing(9);
    form->setColumnStretch(0, 1);
    form->setColumnStretch(1, 1);

    form->addLayout(typeRow, 0, 0, 1, 2);
    form->addWidget(makeFieldLabel("Vehicle ID"), 1, 0, 1, 2);
    form->addWidget(addIdEdit, 2, 0, 1, 2);
    form->addWidget(makeFieldLabel("Brand"), 3, 0);
    form->addWidget(makeFieldLabel("Model"), 3, 1);
    form->addWidget(addBrandEdit, 4, 0);
    form->addWidget(addModelEdit, 4, 1);
    form->addWidget(makeFieldLabel("Price Per Day"), 5, 0);
    form->addWidget(addExtraLabel, 5, 1);
    form->addWidget(addPriceEdit, 6, 0);
    form->addWidget(addExtraEdit, 6, 1);

    QHBoxLayout* actions = new QHBoxLayout();
    actions->setSpacing(10);
    actions->addWidget(saveButton);
    actions->addWidget(clearButton);
    actions->addStretch();
    form->addLayout(actions, 7, 0, 1, 2);

    card->setLayout(form);
    card->setMinimumWidth(820);
    card->setMaximumWidth(940);
    card->setMinimumHeight(275);

    QVBoxLayout* pageLayout = new QVBoxLayout();
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->addWidget(card, 0, Qt::AlignLeft);
    pageLayout->addStretch();
    page->setLayout(pageLayout);

    connect(carTypeButton, &QPushButton::clicked, this, [this]() {
        selectedVehicleType = "Car";
        carTypeButton->setChecked(true);
        motorcycleTypeButton->setChecked(false);
        updateAddExtraLabel();
    });

    connect(motorcycleTypeButton, &QPushButton::clicked, this, [this]() {
        selectedVehicleType = "Motorcycle";
        motorcycleTypeButton->setChecked(true);
        carTypeButton->setChecked(false);
        updateAddExtraLabel();
    });

    connect(clearButton, &QPushButton::clicked, this, &MainWindow::clearAddFields);
    connect(saveButton, &QPushButton::clicked, this, &MainWindow::addVehicle);
    connect(addBrandEdit, &QLineEdit::returnPressed, this, &MainWindow::addVehicle);
    connect(addModelEdit, &QLineEdit::returnPressed, this, &MainWindow::addVehicle);
    connect(addPriceEdit, &QLineEdit::returnPressed, this, &MainWindow::addVehicle);
    connect(addExtraEdit, &QLineEdit::returnPressed, this, &MainWindow::addVehicle);

    updateGeneratedVehicleId();
    return page;
}


QWidget* MainWindow::createAllVehiclesSummaryPage()
{
    // Existing shared fleet table remains authoritative. The inspector adds
    // visual context without modifying any rental business operations.
    QWidget* page = new QWidget();
    QVBoxLayout* content = new QVBoxLayout(page);
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(9);

    QFrame* inspector = new QFrame();
    inspector->setObjectName("FleetInspector");
    inspector->setFixedHeight(156);
    QHBoxLayout* row = new QHBoxLayout(inspector);
    row->setContentsMargins(18, 12, 20, 12);
    row->setSpacing(18);

    QVBoxLayout* media = new QVBoxLayout();
    media->setSpacing(3);
    fleetAssetPhoto = new QLabel("Select a vehicle");
    fleetAssetPhoto->setObjectName("FleetAssetPhoto");
    fleetAssetPhoto->setAlignment(Qt::AlignCenter);
    fleetAssetPhoto->setFixedSize(238, 110);
    fleetAssetPhoto->setAccessibleName("Illustrative vehicle photo or class placeholder");
    fleetAssetCredit = new QLabel("Model photographs are illustrative");
    fleetAssetCredit->setObjectName("FleetAssetCredit");
    fleetAssetCredit->setFixedWidth(238);
    fleetAssetCredit->setAlignment(Qt::AlignRight);
    media->addWidget(fleetAssetPhoto);
    media->addWidget(fleetAssetCredit);
    row->addLayout(media);

    QVBoxLayout* assetInfo = new QVBoxLayout();
    assetInfo->setSpacing(4);
    QLabel* kicker = new QLabel("SELECTED FLEET ASSET  /  VEHICLE PROFILE");
    kicker->setObjectName("FleetAssetKicker");
    fleetAssetName = new QLabel("Select a vehicle");
    fleetAssetName->setObjectName("FleetAssetName");
    fleetAssetMeta = new QLabel("Choose a row below to inspect a vehicle");
    fleetAssetMeta->setObjectName("FleetAssetMeta");
    fleetAssetStatus = new QLabel("NO SELECTION");
    fleetAssetStatus->setObjectName("FleetAssetStatus");
    assetInfo->addWidget(kicker);
    assetInfo->addWidget(fleetAssetName);
    assetInfo->addWidget(fleetAssetMeta);
    assetInfo->addSpacing(2);
    assetInfo->addWidget(fleetAssetStatus, 0, Qt::AlignLeft);
    assetInfo->addStretch();
    row->addLayout(assetInfo, 1);

    QVBoxLayout* pricing = new QVBoxLayout();
    pricing->setAlignment(Qt::AlignRight);
    QLabel* priceLabel = new QLabel("DAILY RENTAL RATE");
    priceLabel->setObjectName("FleetAssetPriceLabel");
    fleetAssetRate = new QLabel("R0.00");
    fleetAssetRate->setObjectName("FleetAssetRate");
    pricing->addWidget(priceLabel, 0, Qt::AlignRight);
    pricing->addWidget(fleetAssetRate, 0, Qt::AlignRight);
    pricing->addStretch();
    row->addLayout(pricing);
    content->addWidget(inspector);

    QFrame* summary = new QFrame();
    summary->setObjectName("FleetSummary");
    summary->setFixedHeight(52);
    QHBoxLayout* summaryRow = new QHBoxLayout(summary);
    summaryRow->setContentsMargins(18, 7, 18, 7);
    summaryRow->setSpacing(8);
    auto indicator = [summaryRow](const QString& text, QLabel*& value) {
        QVBoxLayout* group = new QVBoxLayout();
        group->setSpacing(1);
        QLabel* caption = new QLabel(text);
        caption->setObjectName("FleetSummaryLabel");
        value = new QLabel("0");
        value->setObjectName("FleetSummaryValue");
        group->addWidget(caption);
        group->addWidget(value);
        summaryRow->addLayout(group, 1);
    };
    indicator("All vehicles", allTotalVehiclesValue);
    indicator("Ready", allAvailableVehiclesValue);
    indicator("Rented", allRentedVehiclesValue);
    indicator("Active contracts", allActiveRentalsValue);
    indicator("Cars", allCarsValue);
    indicator("Motorcycles", allMotorcyclesValue);
    content->addWidget(summary);

    QHBoxLayout* filters = new QHBoxLayout();
    filters->setSpacing(10);
    fleetSearchEdit = new QLineEdit();
    fleetSearchEdit->setObjectName("FleetSearch");
    fleetSearchEdit->setPlaceholderText("Search vehicle ID, brand or model...");
    fleetSearchEdit->setAccessibleName("Filter fleet vehicles");
    fleetSearchEdit->setClearButtonEnabled(true);
    fleetStatusCombo = new QComboBox();
    fleetStatusCombo->setObjectName("FleetStatusFilter");
    fleetStatusCombo->addItems({"All statuses", "Available", "Rented"});
    fleetStatusCombo->setAccessibleName("Filter by rental status");
    fleetStatusCombo->setMinimumWidth(155);
    QPushButton* registerButton = new QPushButton("+ Add vehicle");
    registerButton->setObjectName("ActionButton");
    connect(registerButton, &QPushButton::clicked, this, &MainWindow::showAddPage);
    connect(fleetSearchEdit, &QLineEdit::textChanged, this,
            [this](const QString&) { filterFleetTable(); });
    connect(fleetStatusCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { filterFleetTable(); });
    filters->addWidget(fleetSearchEdit, 1);
    filters->addWidget(fleetStatusCombo);
    filters->addWidget(registerButton);
    content->addLayout(filters);
    return page;
}

QWidget* MainWindow::createAvailableVehiclesSummaryPage()
{
    QWidget* page = new QWidget();

    QHBoxLayout* cards = new QHBoxLayout();
    cards->setContentsMargins(0, 0, 0, 0);
    cards->setSpacing(11);
    cards->addWidget(createStatCard("Total Available", availableTotalValue));
    cards->addWidget(createStatCard("Available Cars", availableCarsValue));
    cards->addWidget(createStatCard("Available Motorcycles", availableMotorcyclesValue));

    page->setLayout(cards);
    return page;
}

QWidget* MainWindow::createSearchPage()
{
    QWidget* page = new QWidget();
    QFrame* card = new QFrame();
    card->setObjectName("FormCard");

    searchIdEdit = new QLineEdit();
    searchIdEdit->setPlaceholderText("Enter vehicle ID, e.g. C001");
    searchIdEdit->setFixedWidth(430);

    QPushButton* searchButton = new QPushButton("Search");
    QPushButton* clearButton = new QPushButton("Clear Search");
    searchButton->setObjectName("ActionButton");
    clearButton->setObjectName("SecondaryButton");

    QGridLayout* form = new QGridLayout();
    form->setContentsMargins(22, 16, 22, 16);
    form->addWidget(makeFieldLabel("Vehicle ID"), 0, 0, 1, 3);
    form->addWidget(searchIdEdit, 1, 0);
    form->addWidget(clearButton, 1, 1);
    form->addWidget(searchButton, 1, 2);
    card->setLayout(form);
    card->setMaximumWidth(760);

    QVBoxLayout* pageLayout = new QVBoxLayout();
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->addWidget(card, 0, Qt::AlignLeft);
    pageLayout->addStretch();
    page->setLayout(pageLayout);

    connect(searchButton, &QPushButton::clicked, this, &MainWindow::searchVehicle);
    connect(searchIdEdit, &QLineEdit::returnPressed, this, &MainWindow::searchVehicle);
    connect(clearButton, &QPushButton::clicked, this, [this]() {
        searchIdEdit->clear();
        showEmptyTableMessage("Enter a vehicle ID and click Search",
                              "The matching vehicle will appear here.");
    });

    return page;
}

QWidget* MainWindow::createRentPage()
{
    QWidget* page = new QWidget();
    page->setObjectName("RentalWorkspace");
    QHBoxLayout* shell = new QHBoxLayout(page);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(13);

    // Left side: the existing same-day rental workflow, now grouped by task.
    QFrame* formPanel = new QFrame();
    formPanel->setObjectName("RentalFormPanel");
    QVBoxLayout* left = new QVBoxLayout(formPanel);
    left->setContentsMargins(22, 14, 22, 13);
    left->setSpacing(7);
    QLabel* kicker = new QLabel("01  /  CUSTOMER & VEHICLE");
    kicker->setObjectName("RentalKicker");
    QLabel* heading = new QLabel("New rental");
    heading->setObjectName("RentalPanelTitle");
    left->addWidget(kicker);
    left->addWidget(heading);

    rentVehicleIdEdit = new QLineEdit();
    rentVehicleIdEdit->setObjectName("RentalVehicleID");
    rentCustomerNameEdit = new QLineEdit();
    rentPhoneEdit = new QLineEdit();
    rentStartDateEdit = new QDateEdit(QDate::currentDate());
    rentDaysSpinBox = new QSpinBox();
    rentDaysSpinBox->setObjectName("RentalDays");
    rentVehicleIdEdit->setPlaceholderText("e.g. C001");
    rentCustomerNameEdit->setPlaceholderText("Customer full name");
    rentPhoneEdit->setPlaceholderText("e.g. 082 123 4567");

    // Preserve the existing business rule: immediate, same-day checkout only.
    rentStartDateEdit->setCalendarPopup(false);
    rentStartDateEdit->setDisplayFormat("dd MMM yyyy");
    rentStartDateEdit->setMinimumDate(QDate::currentDate());
    rentStartDateEdit->setMaximumDate(QDate::currentDate());
    rentStartDateEdit->setDate(QDate::currentDate());
    rentStartDateEdit->setReadOnly(true);
    rentStartDateEdit->setButtonSymbols(QAbstractSpinBox::NoButtons);
    rentDaysSpinBox->setRange(1, 365);
    rentDaysSpinBox->setValue(1);
    rentDaysSpinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    rentDaysSpinBox->setAlignment(Qt::AlignLeft);

    QGridLayout* form = new QGridLayout();
    form->setContentsMargins(0, 2, 0, 0);
    form->setHorizontalSpacing(13);
    form->setVerticalSpacing(5);
    form->setColumnStretch(0, 1);
    form->setColumnStretch(1, 1);
    form->addWidget(makeFieldLabel("Available Vehicle ID"), 0, 0);
    form->addWidget(makeFieldLabel("Customer Full Name"), 0, 1);
    form->addWidget(rentVehicleIdEdit, 1, 0);
    form->addWidget(rentCustomerNameEdit, 1, 1);
    form->addWidget(makeFieldLabel("Phone Number"), 2, 0);
    form->addWidget(makeFieldLabel("Rental Start Date"), 2, 1);
    form->addWidget(rentPhoneEdit, 3, 0);
    form->addWidget(rentStartDateEdit, 3, 1);
    form->addWidget(makeFieldLabel("Rental Days"), 4, 0);
    form->addWidget(makeFieldLabel("Processing"), 4, 1);
    form->addWidget(rentDaysSpinBox, 5, 0);
    QLabel* sameDayNote = new QLabel("Immediate checkout  /  Today");
    sameDayNote->setObjectName("SameDayNote");
    form->addWidget(sameDayNote, 5, 1);
    left->addLayout(form);

    rentLookupMessageLabel = new QLabel(
        "Enter an available vehicle ID to view the estimate.");
    rentLookupMessageLabel->setObjectName("RentalLookupHint");
    rentLookupMessageLabel->setWordWrap(true);
    left->addWidget(rentLookupMessageLabel);

    QPushButton* clearButton = new QPushButton("Clear fields");
    clearButton->setObjectName("SecondaryButton");
    rentConfirmButton = new QPushButton("Confirm rental");
    rentConfirmButton->setObjectName("ActionButton");
    rentConfirmButton->setProperty("portfolioAction", "confirm-rental");
    rentConfirmButton->setEnabled(false);
    QHBoxLayout* actions = new QHBoxLayout();
    actions->setSpacing(9);
    actions->addWidget(rentConfirmButton);
    actions->addWidget(clearButton);
    actions->addStretch();
    left->addLayout(actions);
    shell->addWidget(formPanel, 3);

    // Right side: the price is derived from the existing RentalSystem values.
    // No payment, discount or checkout capability is implied.
    QFrame* quote = new QFrame();
    quote->setObjectName("RentalQuotePanel");
    quote->setMinimumWidth(310);
    QVBoxLayout* right = new QVBoxLayout(quote);
    right->setContentsMargins(23, 18, 23, 18);
    right->setSpacing(8);
    QLabel* quoteKicker = new QLabel("02  /  LIVE RENTAL ESTIMATE");
    quoteKicker->setObjectName("QuoteKicker");
    right->addWidget(quoteKicker);

    QLabel* priceCaption = new QLabel("ESTIMATED RENTAL TOTAL");
    priceCaption->setObjectName("QuoteFieldCaption");
    right->addWidget(priceCaption);

    rentTotalValue = new QLabel("R0.00");
    rentTotalValue->setObjectName("QuoteTotal");
    right->addWidget(rentTotalValue);

    QFrame* rule = new QFrame();
    rule->setObjectName("QuoteRule");
    rule->setFixedHeight(1);
    right->addWidget(rule);

    auto addLine = [right](const QString& label, QLabel*& value) {
        QHBoxLayout* line = new QHBoxLayout();
        line->setSpacing(8);
        QLabel* description = new QLabel(label);
        description->setObjectName("QuoteFieldCaption");
        value = new QLabel("-");
        value->setObjectName("QuoteFieldValue");
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setMinimumWidth(100);
        line->addWidget(description);
        line->addWidget(value, 1);
        right->addLayout(line);
    };

    addLine("Vehicle", rentVehicleValue);
    addLine("Daily rate", rentPriceValue);
    addLine("Return due", rentExpectedReturnValue);
    addLine("Availability", rentAvailabilityValue);
    rentAvailabilityValue->setText("Waiting for vehicle ID");
    right->addStretch();
    QLabel* disclaimer = new QLabel(
        "Estimate only. No payment is collected by this application.");
    disclaimer->setObjectName("QuoteDisclaimer");
    disclaimer->setWordWrap(true);
    right->addWidget(disclaimer);
    shell->addWidget(quote, 2);

    connect(rentVehicleIdEdit, &QLineEdit::textChanged,
            this, &MainWindow::updateRentalEstimate);
    connect(rentCustomerNameEdit, &QLineEdit::textChanged,
            this, &MainWindow::updateRentalEstimate);
    connect(rentPhoneEdit, &QLineEdit::textChanged,
            this, &MainWindow::updateRentalEstimate);
    connect(rentStartDateEdit, &QDateEdit::dateChanged,
            this, &MainWindow::updateRentalEstimate);
    connect(rentDaysSpinBox, qOverload<int>(&QSpinBox::valueChanged),
            this, &MainWindow::updateRentalEstimate);
    connect(clearButton, &QPushButton::clicked, this, &MainWindow::clearRentalForm);
    connect(rentConfirmButton, &QPushButton::clicked, this, &MainWindow::rentVehicle);
    connect(rentPhoneEdit, &QLineEdit::returnPressed, this, &MainWindow::rentVehicle);
    updateRentalEstimate();
    return page;
}


QWidget* MainWindow::createRentalRecordsPage()
{
    QWidget* page = new QWidget();
    QFrame* card = new QFrame();
    card->setObjectName("RentalActionPanel");
    QVBoxLayout* inner = new QVBoxLayout(card);
    inner->setContentsMargins(20, 12, 20, 13);
    inner->setSpacing(9);

    QHBoxLayout* heading = new QHBoxLayout();
    QLabel* kicker = new QLabel("RENTAL LEDGER  /  TRANSACTION HISTORY");
    kicker->setObjectName("ActionPanelKicker");
    QLabel* note = new QLabel("Current, returned and archived records");
    note->setObjectName("ActionPanelNote");
    heading->addWidget(kicker);
    heading->addStretch();
    heading->addWidget(note);
    inner->addLayout(heading);

    rentalFilterCombo = new QComboBox();
    rentalFilterCombo->setObjectName("RentalHistoryFilter");
    rentalFilterCombo->addItems({
        "All Current Records", "Active Rentals",
        "Returned Rentals", "Archived Records"
    });
    rentalRecordIdEdit = new QLineEdit();
    rentalRecordIdEdit->setObjectName("RentalRecordID");
    rentalRecordIdEdit->setPlaceholderText("Rental ID, e.g. R001");
    rentalRecordIdEdit->setMinimumWidth(190);
    QPushButton* archiveButton = new QPushButton("Archive returned");
    QPushButton* deleteButton = new QPushButton("Delete archived");
    archiveButton->setObjectName("SecondaryButton");
    deleteButton->setObjectName("DangerButton");

    QGridLayout* controls = new QGridLayout();
    controls->setContentsMargins(0, 0, 0, 0);
    controls->setHorizontalSpacing(11);
    controls->setVerticalSpacing(4);
    controls->setColumnStretch(0, 2);
    controls->setColumnStretch(1, 2);
    controls->addWidget(makeFieldLabel("Transaction status"), 0, 0);
    controls->addWidget(makeFieldLabel("Record ID for archive or deletion"), 0, 1);
    controls->addWidget(rentalFilterCombo, 1, 0);
    controls->addWidget(rentalRecordIdEdit, 1, 1);
    controls->addWidget(archiveButton, 1, 2);
    controls->addWidget(deleteButton, 1, 3);
    inner->addLayout(controls);

    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(card);

    connect(rentalFilterCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MainWindow::refreshRentalRecordsFilter);
    connect(archiveButton, &QPushButton::clicked,
            this, &MainWindow::archiveRentalRecord);
    connect(deleteButton, &QPushButton::clicked,
            this, &MainWindow::deleteArchivedRentalRecord);
    return page;
}


QWidget* MainWindow::createReturnPage()
{
    QWidget* page = new QWidget();
    QFrame* card = new QFrame();
    card->setObjectName("RentalActionPanel");
    QVBoxLayout* inner = new QVBoxLayout(card);
    inner->setContentsMargins(20, 12, 20, 13);
    inner->setSpacing(9);

    QHBoxLayout* heading = new QHBoxLayout();
    QLabel* kicker = new QLabel("RENTAL OPERATIONS  /  VEHICLE RETURN");
    kicker->setObjectName("ActionPanelKicker");
    QLabel* note = new QLabel("Complete an active contract to release its vehicle");
    note->setObjectName("ActionPanelNote");
    heading->addWidget(kicker);
    heading->addStretch();
    heading->addWidget(note);
    inner->addLayout(heading);

    returnRentalIdEdit = new QLineEdit();
    returnRentalIdEdit->setObjectName("ReturnRentalID");
    returnRentalIdEdit->setPlaceholderText("R001 or C002");
    returnRentalIdEdit->setMinimumWidth(250);

    actualReturnDateEdit = new QDateEdit(QDate::currentDate());
    actualReturnDateEdit->setCalendarPopup(false);
    actualReturnDateEdit->setDisplayFormat("dd MMM yyyy");
    actualReturnDateEdit->setMinimumDate(QDate::currentDate());
    actualReturnDateEdit->setMaximumDate(QDate::currentDate());
    actualReturnDateEdit->setDate(QDate::currentDate());
    actualReturnDateEdit->setReadOnly(true);
    actualReturnDateEdit->setButtonSymbols(QAbstractSpinBox::NoButtons);
    actualReturnDateEdit->setMinimumWidth(180);

    QPushButton* clearButton = new QPushButton("Clear");
    QPushButton* returnButton = new QPushButton("Complete return");
    clearButton->setObjectName("SecondaryButton");
    returnButton->setObjectName("ActionButton");

    QGridLayout* controls = new QGridLayout();
    controls->setContentsMargins(0, 0, 0, 0);
    controls->setHorizontalSpacing(12);
    controls->setVerticalSpacing(4);
    controls->setColumnStretch(0, 2);
    controls->setColumnStretch(1, 1);
    controls->addWidget(makeFieldLabel("Active Rental ID or Vehicle ID"), 0, 0);
    controls->addWidget(makeFieldLabel("Actual return date"), 0, 1);
    controls->addWidget(returnRentalIdEdit, 1, 0);
    controls->addWidget(actualReturnDateEdit, 1, 1);
    controls->addWidget(clearButton, 1, 2);
    controls->addWidget(returnButton, 1, 3);
    inner->addLayout(controls);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(card);

    connect(clearButton, &QPushButton::clicked, this, [this]() {
        returnRentalIdEdit->clear();
        actualReturnDateEdit->setMinimumDate(QDate::currentDate());
        actualReturnDateEdit->setDate(QDate::currentDate());
    });
    connect(returnButton, &QPushButton::clicked, this, &MainWindow::returnVehicle);
    connect(returnRentalIdEdit, &QLineEdit::returnPressed, this, &MainWindow::returnVehicle);
    return page;
}


QWidget* MainWindow::createRemovePage()
{
    QWidget* page = new QWidget();
    QFrame* card = new QFrame();
    card->setObjectName("FormCard");

    removeIdEdit = new QLineEdit();
    removeIdEdit->setPlaceholderText("Enter vehicle ID, e.g. C001");
    removeIdEdit->setFixedWidth(430);

    QPushButton* clearButton = new QPushButton("Clear");
    QPushButton* removeButton = new QPushButton("Remove Vehicle");
    clearButton->setObjectName("SecondaryButton");
    removeButton->setObjectName("DangerButton");

    QGridLayout* form = new QGridLayout();
    form->setContentsMargins(22, 16, 22, 16);
    form->setHorizontalSpacing(14);
    form->addWidget(makeFieldLabel("Vehicle ID to Remove"), 0, 0, 1, 3);
    form->addWidget(removeIdEdit, 1, 0);
    form->addWidget(clearButton, 1, 1);
    form->addWidget(removeButton, 1, 2);
    card->setLayout(form);
    card->setMaximumWidth(760);

    QVBoxLayout* pageLayout = new QVBoxLayout();
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->addWidget(card, 0, Qt::AlignLeft);
    pageLayout->addStretch();
    page->setLayout(pageLayout);

    connect(clearButton, &QPushButton::clicked, this, [this]() {
        removeIdEdit->clear();
    });
    connect(removeButton, &QPushButton::clicked, this, &MainWindow::removeVehicle);
    connect(removeIdEdit, &QLineEdit::returnPressed, this, &MainWindow::removeVehicle);

    return page;
}

void MainWindow::configureVehicleTable()
{
    hideEmptyTableMessage();
    dataTable->setSortingEnabled(false);
    dataTable->clear();
    dataTable->setColumnCount(7);
    dataTable->setHorizontalHeaderLabels({
        "ID", "Type", "Brand", "Model", "Price/Day", "Status", "Details"
    });
    dataTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    dataTable->setSortingEnabled(true);
}

void MainWindow::configureRentalTable()
{
    hideEmptyTableMessage();
    dataTable->setSortingEnabled(false);
    dataTable->clear();

    QHeaderView* header = dataTable->horizontalHeader();
    header->setStretchLastSection(false);
    header->setMinimumSectionSize(48);
    header->setSectionResizeMode(QHeaderView::Fixed);

    // The Return Vehicle page only needs the information required to complete
    // an active return. Rental History keeps a broader transaction overview.
    const bool returnPageVisible = actionStack->currentIndex() == 5;

    // Each column receives a deliberate percentage of the available table width.
    // This keeps customer names readable, gives Status enough room, and avoids
    // a horizontal scrollbar even when the application window is resized.
    dataTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    if (returnPageVisible) {
        dataTable->setColumnCount(8);
        dataTable->setHorizontalHeaderLabels({
            "Rental ID",
            "Vehicle",
            "Customer",
            "Phone",
            "Start",
            "Due",
            "Days",
            "Total"
        });
    }
    else {
        dataTable->setColumnCount(10);
        dataTable->setHorizontalHeaderLabels({
            "Rental ID",
            "Customer",
            "Phone",
            "Vehicle",
            "Start",
            "Expected",
            "Actual",
            "Days",
            "Total",
            "Status"
        });
    }

    resizeRentalTableColumns();
    QTimer::singleShot(0, this, &MainWindow::resizeRentalTableColumns);
    dataTable->setSortingEnabled(true);
}

void MainWindow::resizeRentalTableColumns()
{
    if (dataTable == nullptr || actionStack == nullptr) {
        return;
    }

    const bool returnPageVisible = actionStack->currentIndex() == 5;
    const bool historyPageVisible = actionStack->currentIndex() == 4;

    if (!returnPageVisible && !historyPageVisible) {
        return;
    }

    const int expectedColumns = returnPageVisible ? 8 : 10;
    if (dataTable->columnCount() != expectedColumns) {
        return;
    }

    // Percentages sum to 100. The final column receives any rounding remainder.
    const QList<int> percentages = returnPageVisible
                                       ? QList<int>({8, 8, 22, 14, 12, 12, 8, 16})
                                       : QList<int>({9, 16, 10, 7, 10, 10, 9, 6, 10, 13});

    const int usableWidth = qMax(1, dataTable->viewport()->width() - 2);
    int assignedWidth = 0;

    for (int column = 0; column < percentages.size(); ++column) {
        const int width = (column == percentages.size() - 1)
        ? usableWidth - assignedWidth
        : (usableWidth * percentages[column]) / 100;

        dataTable->setColumnWidth(column, qMax(48, width));
        assignedWidth += width;
    }
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);

    if (dataTable != nullptr) {
        resizeRentalTableColumns();

        if (emptyTableLabel != nullptr) {
            emptyTableLabel->setGeometry(dataTable->viewport()->rect());
        }
    }
}


void MainWindow::configureDashboardRentalTable()
{
    hideEmptyTableMessage();
    dataTable->setSortingEnabled(false);
    dataTable->clear();
    dataTable->setColumnCount(7);
    dataTable->setHorizontalHeaderLabels({
        "Rental ID", "Customer", "Vehicle ID", "Start Date", "Days", "Total Cost", "Status"
    });
    dataTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    dataTable->setSortingEnabled(true);
}

void MainWindow::refreshVehicleTable(const QList<Vehicle*>& vehicles)
{
    configureVehicleTable();
    dataTable->setSortingEnabled(false);
    dataTable->setRowCount(0);

    int row = 0;
    for (Vehicle* vehicle : vehicles) {
        dataTable->insertRow(row);

        const QStringList values = {
            vehicle->getId(),
            vehicle->typeName(),
            vehicle->getBrand(),
            vehicle->getModel(),
            currency(vehicle->getPricePerDay()),
            vehicle->getIsRented() ? "Rented" : "Available",
            vehicle->extraDetails()
        };

        for (int column = 0; column < values.size(); ++column) {
            QTableWidgetItem* item = new QTableWidgetItem(values[column]);

            if (column == 0 || column == 4 || column == 5) {
                item->setTextAlignment(Qt::AlignCenter);
            }

            if (column == 5) {
                item->setForeground(vehicle->getIsRented()
                                    ? QColor(185, 28, 28)
                                    : QColor(21, 128, 61));
            }

            dataTable->setItem(row, column, item);
        }

        ++row;
    }

    dataTable->setSortingEnabled(true);
    updateDashboardCards();
    if (actionStack->currentIndex() == 7 && fleetSearchEdit != nullptr)
        filterFleetTable();

    if (vehicles.isEmpty()) {
        showEmptyTableMessage("No vehicles to display",
                              "No vehicles match the current page or filter.");
    }
}

void MainWindow::filterFleetTable()
{
    if (actionStack->currentIndex() != 7 || !fleetSearchEdit || !fleetStatusCombo)
        return;
    const QString needle = fleetSearchEdit->text().trimmed();
    const int filter = fleetStatusCombo->currentIndex();
    int firstVisible = -1;
    for (int row = 0; row < dataTable->rowCount(); ++row) {
        QString terms;
        for (int column = 0; column < qMin(4, dataTable->columnCount()); ++column) {
            if (const QTableWidgetItem* item = dataTable->item(row, column))
                terms += " " + item->text();
        }
        const QTableWidgetItem* state = dataTable->item(row, 5);
        const bool rented = state && state->text() == "Rented";
        const bool matches = terms.contains(needle, Qt::CaseInsensitive) &&
            (filter == 0 || (filter == 1 && !rented) || (filter == 2 && rented));
        dataTable->setRowHidden(row, !matches);
        if (matches && firstVisible < 0)
            firstVisible = row;
    }
    if (firstVisible >= 0 &&
        (dataTable->currentRow() < 0 ||
         dataTable->isRowHidden(dataTable->currentRow())))
        dataTable->selectRow(firstVisible);
    updateFleetInspector();
}

void MainWindow::updateFleetInspector()
{
    if (actionStack->currentIndex() != 7 || !fleetAssetName)
        return;
    const int row = dataTable->currentRow();
    const QTableWidgetItem* idItem =
        row >= 0 && !dataTable->isRowHidden(row) ? dataTable->item(row, 0) : nullptr;
    Vehicle* vehicle = idItem ? rentalSystem.findVehicleById(idItem->text()) : nullptr;
    if (!vehicle) {
        fleetAssetName->setText("No vehicle selected");
        fleetAssetMeta->setText("Choose a matching vehicle from the table");
        fleetAssetRate->setText("R0.00");
        fleetAssetStatus->setText("NO MATCH");
        fleetAssetStatus->setStyleSheet(
            "background: #e2e8df; color: #354a45; padding: 5px 9px; border-radius: 6px;");
        fleetAssetPhoto->clear();
        fleetAssetPhoto->setText("No vehicle selected");
        fleetAssetCredit->setText("Illustrative photography");
        return;
    }
    fleetAssetName->setText(vehicle->getBrand() + " " + vehicle->getModel());
    fleetAssetMeta->setText(vehicle->getId() + "  /  " + vehicle->typeName() +
                           "  /  " + vehicle->extraDetails());
    fleetAssetRate->setText(currency(vehicle->getPricePerDay()));
    const bool rented = vehicle->getIsRented();
    fleetAssetStatus->setText(rented ? "CURRENTLY RENTED" : "READY TO RENT");
    fleetAssetStatus->setStyleSheet(rented
        ? "background: #f9e6db; color: #84401e; padding: 5px 9px; border-radius: 6px;"
        : "background: #cff375; color: #1a342d; padding: 5px 9px; border-radius: 6px;");

    const QString id = vehicle->getId();
    const QString source = id == "C001" ? ":/portfolio/toyota_corolla.jpg"
                         : id == "C002" ? ":/portfolio/ford_ranger.jpg"
                         : id == "C003" ? ":/portfolio/hyundai_i20.jpg" : QString();
    const QPixmap photo(source);
    if (!source.isEmpty() && !photo.isNull()) {
        const QSize box = fleetAssetPhoto->size();
        const QPixmap scaled = photo.scaled(box, Qt::KeepAspectRatioByExpanding,
                                           Qt::SmoothTransformation);
        fleetAssetPhoto->setPixmap(scaled.copy((scaled.width() - box.width()) / 2,
                                               (scaled.height() - box.height()) / 2,
                                               box.width(), box.height()));
        fleetAssetCredit->setText(id == "C001" ? "Corolla / 01x07x2022000 · CC0"
                                  : id == "C002" ? "Ranger / Hyins · CC0"
                                  : "i20 / Trop86 · CC0");
    } else {
        fleetAssetPhoto->setPixmap(QPixmap());
        fleetAssetPhoto->setText(vehicle->typeName() == "Motorcycle"
                                    ? "MOTORCYCLE  /  IMAGE PENDING"
                                    : "CAR  /  IMAGE PENDING");
        fleetAssetCredit->setText("No approved photo for this vehicle");
    }
}

void MainWindow::refreshRentalTable(const QList<Rental*>& rentals)
{
    configureRentalTable();
    dataTable->setSortingEnabled(false);
    dataTable->setRowCount(0);

    const bool returnPageVisible = actionStack->currentIndex() == 5;

    int row = 0;
    for (Rental* rental : rentals) {
        dataTable->insertRow(row);

        const QString actualReturn = rental->getActualReturnDate().isValid()
                                         ? rental->getActualReturnDate().toString("dd/MM/yyyy")
                                         : "-";

        const QStringList values = returnPageVisible
                                       ? QStringList({
                                             rental->getRentalId(),
                                             rental->getVehicleId(),
                                             rental->getCustomerName(),
                                             rental->getPhoneNumber(),
                                             rental->getStartDate().toString("dd/MM/yyyy"),
                                             rental->getExpectedReturnDate().toString("dd/MM/yyyy"),
                                             QString::number(rental->getRentalDays()),
                                             currency(rental->getTotalCost())
                                         })
                                       : QStringList({
                                             rental->getRentalId(),
                                             rental->getCustomerName(),
                                             rental->getPhoneNumber(),
                                             rental->getVehicleId(),
                                             rental->getStartDate().toString("dd/MM/yyyy"),
                                             rental->getExpectedReturnDate().toString("dd/MM/yyyy"),
                                             actualReturn,
                                             QString::number(rental->getRentalDays()),
                                             currency(rental->getTotalCost()),
                                             rental->statusText()
                                         });

        for (int column = 0; column < values.size(); ++column) {
            QTableWidgetItem* item = new QTableWidgetItem(values[column]);

            if (returnPageVisible) {
                if (column != 2) {
                    item->setTextAlignment(Qt::AlignCenter);
                }
            }
            else if (column != 1) {
                item->setTextAlignment(Qt::AlignCenter);
            }

            if (!returnPageVisible && column == 9) {
                if (rental->getIsArchived()) {
                    item->setForeground(QColor(75, 85, 99));
                }
                else if (rental->getIsReturned()) {
                    item->setForeground(QColor(21, 128, 61));
                }
                else {
                    item->setForeground(QColor(37, 99, 235));
                }
            }

            dataTable->setItem(row, column, item);
        }

        ++row;
    }

    dataTable->setSortingEnabled(true);
    resizeRentalTableColumns();
    QTimer::singleShot(0, this, &MainWindow::resizeRentalTableColumns);
    updateDashboardCards();

    if (rentals.isEmpty()) {
        showEmptyTableMessage("No rental records to display",
                              "No rental records match the selected filter.");
    }
}


void MainWindow::refreshDashboardRecentRentals()
{
    const QList<Rental*> rentals = recentRentals();

    configureDashboardRentalTable();
    dataTable->setSortingEnabled(false);
    dataTable->setRowCount(0);

    int row = 0;
    for (Rental* rental : rentals) {
        dataTable->insertRow(row);

        const QStringList values = {
            rental->getRentalId(),
            rental->getCustomerName(),
            rental->getVehicleId(),
            rental->getStartDate().toString("dd MMM yyyy"),
            QString::number(rental->getRentalDays()),
            currency(rental->getTotalCost()),
            rental->statusText()
        };

        for (int column = 0; column < values.size(); ++column) {
            QTableWidgetItem* item = new QTableWidgetItem(values[column]);
            if (column == 0 || column == 2 || column >= 4) {
                item->setTextAlignment(Qt::AlignCenter);
            }

            if (column == 6) {
                item->setForeground(rental->getIsReturned()
                                    ? QColor(21, 128, 61)
                                    : QColor(37, 99, 235));
            }
            dataTable->setItem(row, column, item);
        }
        ++row;
    }

    dataTable->setSortingEnabled(true);
    updateDashboardCards();

    if (rentals.isEmpty()) {
        showEmptyTableMessage("No recent rental activity",
                              "New rental transactions will appear here.");
    }
}

void MainWindow::updateDashboardCards()
{
    const QList<Vehicle*> vehicles = rentalSystem.getVehicles();
    const QList<Rental*> rentals = rentalSystem.getRentals();

    const int available = countPointersWhere(vehicles, [](Vehicle* vehicle) {
        return !vehicle->getIsRented();
    });

    const int rented = countPointersWhere(vehicles, [](Vehicle* vehicle) {
        return vehicle->getIsRented();
    });

    const int active = countPointersWhere(rentals, [](Rental* rental) {
        return !rental->getIsReturned();
    });

    const int cars = countPointersWhere(vehicles, [](Vehicle* vehicle) {
        return vehicle->typeName().compare("Car", Qt::CaseInsensitive) == 0;
    });

    const int motorcycles = countPointersWhere(vehicles, [](Vehicle* vehicle) {
        return vehicle->typeName().compare("Motorcycle", Qt::CaseInsensitive) == 0;
    });

    const int availableCars = countPointersWhere(vehicles, [](Vehicle* vehicle) {
        return !vehicle->getIsRented() &&
               vehicle->typeName().compare("Car", Qt::CaseInsensitive) == 0;
    });

    const int availableMotorcycles = countPointersWhere(vehicles, [](Vehicle* vehicle) {
        return !vehicle->getIsRented() &&
               vehicle->typeName().compare("Motorcycle", Qt::CaseInsensitive) == 0;
    });

    // Archived rentals remain part of the business's historical revenue.
    double revenue = 0.0;
    for (Rental* rental : rentals) {
        revenue += rental->getTotalCost();
    }

    const double utilisation = vehicles.isEmpty()
                                   ? 0.0
                                   : (static_cast<double>(rented) / vehicles.size()) * 100.0;

    // Dashboard hero uses actual loaded fleet counts. No demo data is fabricated.
    fleetHeroValue->setText(QString("%1 / %2").arg(available).arg(vehicles.size()));
    fleetHeroHint->setText(vehicles.isEmpty()
        ? "NO VEHICLES LOADED"
        : QString("%1% AVAILABLE").arg(QString::number(100.0 * available / vehicles.size(), 'f', 0)));

    // Dashboard.
    totalVehiclesValue->setText(QString::number(vehicles.size()));
    availableVehiclesValue->setText(QString::number(available));
    rentedVehiclesValue->setText(QString::number(rented));
    activeRentalsValue->setText(QString::number(active));
    carsValue->setText(QString::number(cars));
    motorcyclesValue->setText(QString::number(motorcycles));
    utilisationValue->setText(QString::number(utilisation, 'f', 0) + "%");
    revenueValue->setText(currency(revenue));

    // All Vehicles. Revenue is intentionally excluded from this page.
    allTotalVehiclesValue->setText(QString::number(vehicles.size()));
    allAvailableVehiclesValue->setText(QString::number(available));
    allRentedVehiclesValue->setText(QString::number(rented));
    allActiveRentalsValue->setText(QString::number(active));
    allCarsValue->setText(QString::number(cars));
    allMotorcyclesValue->setText(QString::number(motorcycles));

    // Available Vehicles. Revenue is intentionally excluded from this page.
    availableTotalValue->setText(QString::number(available));
    availableCarsValue->setText(QString::number(availableCars));
    availableMotorcyclesValue->setText(QString::number(availableMotorcycles));
}


QList<Vehicle*> MainWindow::availableVehicles() const
{
    return filterPointers(rentalSystem.getVehicles(), [](Vehicle* vehicle) {
        return !vehicle->getIsRented();
    });
}

QList<Rental*> MainWindow::filteredRentals() const
{
    const QList<Rental*> rentals = rentalSystem.getRentals();
    const int selectedFilter = rentalFilterCombo->currentIndex();

    if (selectedFilter == 1) {
        return filterPointers(rentals, [](Rental* rental) {
            return !rental->getIsReturned();
        });
    }

    if (selectedFilter == 2) {
        return filterPointers(rentals, [](Rental* rental) {
            return rental->getIsReturned() && !rental->getIsArchived();
        });
    }

    if (selectedFilter == 3) {
        return filterPointers(rentals, [](Rental* rental) {
            return rental->getIsArchived();
        });
    }

    return filterPointers(rentals, [](Rental* rental) {
        return !rental->getIsArchived();
    });
}

QList<Rental*> MainWindow::recentRentals() const
{
    const QList<Rental*> rentals = rentalSystem.getRentals();
    QList<Rental*> result;

    for (int index = rentals.size() - 1; index >= 0 && result.size() < 5; --index) {
        Rental* rental = rentals[index];
        if (!rental->getIsArchived()) {
            result.append(rental);
        }
    }

    return result;
}

void MainWindow::setActiveButton(QPushButton* activeButton)
{
    const QList<QPushButton*> navigationButtons = {
        dashboardButton,
        addPageButton,
        removePageButton,
        searchPageButton,
        allVehiclesButton,
        availableVehiclesButton,
        rentPageButton,
        rentalRecordsButton,
        returnPageButton
    };

    for (QPushButton* button : navigationButtons) {
        button->setChecked(button == activeButton);
    }
}

void MainWindow::showEmptyTableMessage(const QString& title,
                                       const QString& subtitle)
{
    dataTable->setSortingEnabled(false);
    dataTable->setRowCount(0);
    dataTable->setSortingEnabled(true);

    emptyTableLabel->setText(
        QString("<div style='text-align:center;'>"
                "<div style='font-size:36px; color:#c7cdd6;'>⌕</div>"
                "<div style='font-size:16px; font-weight:bold; color:#374151; margin-top:8px;'>%1</div>"
                "<div style='font-size:14px; color:#6b7280; margin-top:6px;'>%2</div>"
                "</div>")
            .arg(title, subtitle));

    emptyTableLabel->setGeometry(dataTable->viewport()->rect());
    emptyTableLabel->show();
    emptyTableLabel->raise();
}

void MainWindow::hideEmptyTableMessage()
{
    emptyTableLabel->hide();
}

void MainWindow::showWarningMessage(const QString& title,
                                    const QString& mainText,
                                    const QString& details)
{
    QMessageBox messageBox(this);
    messageBox.setIcon(QMessageBox::Warning);
    messageBox.setWindowTitle(title);
    messageBox.setText(mainText);
    messageBox.setInformativeText(details);
    messageBox.setStandardButtons(QMessageBox::Ok);

    std::unique_ptr<QSpacerItem> spacer = std::make_unique<QSpacerItem>(
        280, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);

    QGridLayout* layout = qobject_cast<QGridLayout*>(messageBox.layout());
    if (layout != nullptr) {
        layout->addItem(spacer.release(),
                        layout->rowCount(),
                        0,
                        1,
                        layout->columnCount());
    }

    messageBox.exec();
}

void MainWindow::showDashboardPage()
{
    pageTitleLabel->setText("Dashboard");
    pageSubtitleLabel->setText(
        "Live fleet availability, booked rental value, and recent transactions.");
    actionStack->setCurrentIndex(0);
    actionStack->setMaximumHeight(385); // hero + KPI cards + metrics strip + spacing
    tableSectionTitleLabel->setVisible(true);
    tableSectionTitleLabel->setText("Recent Rental Activity");
    refreshDashboardRecentRentals();
    setActiveButton(dashboardButton);
}


void MainWindow::showAddPage()
{
    pageTitleLabel->setText("Add Vehicle");
    pageSubtitleLabel->setText("Register a new car or motorcycle in the rental fleet.");
    actionStack->setCurrentIndex(1);
    actionStack->setMaximumHeight(305);
    tableSectionTitleLabel->setVisible(false);
    updateGeneratedVehicleId();
    refreshVehicleTable(rentalSystem.getVehicles());
    setActiveButton(addPageButton);
}


void MainWindow::showSearchPage()
{
    pageTitleLabel->setText("Search Vehicle");
    pageSubtitleLabel->setText("Find a vehicle using its unique fleet ID.");
    actionStack->setCurrentIndex(2);
    actionStack->setMaximumHeight(110);
    tableSectionTitleLabel->setVisible(true);
    tableSectionTitleLabel->setText("Search Result");
    configureVehicleTable();
    showEmptyTableMessage("Enter a vehicle ID and click Search",
                          "The matching vehicle will appear here.");
    setActiveButton(searchPageButton);
}


void MainWindow::showAllVehicles()
{
    pageTitleLabel->setText("Fleet Inventory");
    pageSubtitleLabel->setText(
        "Explore vehicles, inspect availability and manage your rental fleet.");
    actionStack->setCurrentIndex(7);
    actionStack->setMaximumHeight(308);
    tableSectionTitleLabel->setVisible(true);
    tableSectionTitleLabel->setText("Vehicle inventory  /  Select a row to inspect");
    refreshVehicleTable(rentalSystem.getVehicles());
    if (dataTable->rowCount() > 0)
        dataTable->selectRow(0);
    filterFleetTable();
    setActiveButton(allVehiclesButton);
}


void MainWindow::showAvailableVehicles()
{
    pageTitleLabel->setText("Available Vehicles");
    pageSubtitleLabel->setText("Cars and motorcycles currently ready for a new rental transaction.");
    actionStack->setCurrentIndex(8);
    actionStack->setMaximumHeight(115);
    tableSectionTitleLabel->setVisible(false);
    refreshVehicleTable(availableVehicles());
    setActiveButton(availableVehiclesButton);
}


void MainWindow::showRentPage()
{
    pageTitleLabel->setText("Rent Vehicle");
    pageSubtitleLabel->setText(
        "Capture the customer, type the rental days and review the calculated rental details.");
    actionStack->setCurrentIndex(3);
    actionStack->setMaximumHeight(359);
    tableSectionTitleLabel->setVisible(true);
    tableSectionTitleLabel->setText("Vehicles ready for checkout");
    rentStartDateEdit->setMinimumDate(QDate::currentDate());
    rentStartDateEdit->setMaximumDate(QDate::currentDate());
    rentStartDateEdit->setDate(QDate::currentDate());
    updateRentalEstimate();
    refreshVehicleTable(availableVehicles());
    setActiveButton(rentPageButton);
}


void MainWindow::showRentalRecordsPage()
{
    pageTitleLabel->setText("Rental History");
    pageSubtitleLabel->setText("Review active, returned and archived customer rental transactions.");
    actionStack->setCurrentIndex(4);
    actionStack->setMaximumHeight(138);
    tableSectionTitleLabel->setVisible(true);
    tableSectionTitleLabel->setText("Rental Transactions");
    refreshRentalTable(filteredRentals());
    setActiveButton(rentalRecordsButton);
}


void MainWindow::showReturnPage()
{
    pageTitleLabel->setText("Return Vehicle");
    pageSubtitleLabel->setText("Complete an active rental using its rental ID or vehicle ID.");
    actionStack->setCurrentIndex(5);
    actionStack->setMaximumHeight(138);
    tableSectionTitleLabel->setVisible(true);
    tableSectionTitleLabel->setText("Active Rentals");
    actualReturnDateEdit->setMinimumDate(QDate::currentDate());
    actualReturnDateEdit->setMaximumDate(QDate::currentDate());
    actualReturnDateEdit->setDate(QDate::currentDate());
    refreshRentalTable(rentalSystem.getActiveRentals());
    setActiveButton(returnPageButton);
}


void MainWindow::showRemovePage()
{
    pageTitleLabel->setText("Remove Vehicle");
    pageSubtitleLabel->setText("Remove an available vehicle from the fleet. Active rentals are protected.");
    actionStack->setCurrentIndex(6);
    actionStack->setMaximumHeight(110);
    tableSectionTitleLabel->setVisible(false);
    refreshVehicleTable(rentalSystem.getVehicles());
    setActiveButton(removePageButton);
}


void MainWindow::addVehicle()
{
    const QString id = addIdEdit->text().trimmed().toUpper();
    const QString brand = addBrandEdit->text().trimmed();
    const QString model = addModelEdit->text().trimmed();

    bool priceOk = false;
    const double price = addPriceEdit->text().trimmed().toDouble(&priceOk);

    bool extraOk = false;
    const int extra = addExtraEdit->text().trimmed().toInt(&extraOk);

    QStringList errors;

    if (id.isEmpty()) {
        errors.append("Vehicle ID could not be generated.");
    }
    if (brand.isEmpty()) {
        errors.append("Brand is required.");
    }
    if (model.isEmpty()) {
        errors.append("Model is required.");
    }
    if (containsFileSeparator(brand) || containsFileSeparator(model)) {
        errors.append("Brand and model cannot contain the | character.");
    }
    if (!priceOk || !isPositiveValue(price)) {
        errors.append("Price per day must be greater than 0.");
    }
    if (!extraOk || !isPositiveValue(extra)) {
        errors.append(selectedVehicleType == "Car"
                          ? "Number of doors must be greater than 0."
                          : "Engine capacity must be greater than 0.");
    }

    if (!errors.isEmpty()) {
        showWarningMessage("Add Error",
                           "Please correct the vehicle details.",
                           "- " + errors.join("\n- "));
        return;
    }

    Vehicle* vehicle = nullptr;

    if (selectedVehicleType == "Car") {
        vehicle = new Car(id, brand, model, price, extra);
    }
    else {
        vehicle = new Motorcycle(id, brand, model, price, extra);
    }

    const QString message =
        QString("Add this vehicle to the fleet?\n\n"
                "ID: %1\n"
                "Type: %2\n"
                "Brand: %3\n"
                "Model: %4\n"
                "Price per day: %5\n"
                "Details: %6")
            .arg(id,
                 vehicle->typeName(),
                 brand,
                 model,
                 currency(price),
                 vehicle->extraDetails());

    if (!confirmAction(this, "Confirm Add Vehicle", message)) {
        delete vehicle;
        return;
    }

    if (!rentalSystem.addVehicle(vehicle)) {
        delete vehicle;
        showWarningMessage("Add Error",
                           "The vehicle could not be added.",
                           "Check that the generated ID is unique and try again.");
        return;
    }

    // Refresh the table while keeping the completed form visible.
    refreshVehicleTable(rentalSystem.getVehicles());

    // The entered vehicle details remain visible until the user clicks OK.
    QMessageBox::information(this,
                             "Vehicle Added",
                             "Vehicle '" + id + "' was added successfully.");

    // Clear the form only after the success message is closed.
    clearAddFields();
}

void MainWindow::searchVehicle()
{
    const QString id = searchIdEdit->text().trimmed().toUpper();

    if (id.isEmpty()) {
        showWarningMessage("Search Error",
                           "Vehicle ID is required.",
                           "Enter an ID such as C001 or M001.");
        return;
    }

    Vehicle* vehicle = rentalSystem.findVehicleById(id);

    if (vehicle == nullptr) {
        showWarningMessage("Search Error",
                           "Vehicle not found.",
                           "No vehicle with ID '" + id + "' exists.");
        return;
    }

    refreshVehicleTable({vehicle});
}

void MainWindow::updateRentalEstimate()
{
    const QString id = rentVehicleIdEdit->text().trimmed().toUpper();
    Vehicle* vehicle = rentalSystem.findVehicleById(id);

    const QDate expectedReturn =
        rentStartDateEdit->date().addDays(rentDaysSpinBox->value());
    rentExpectedReturnValue->setText(expectedReturn.toString("dd MMM yyyy"));

    rentPriceValue->setText("R0.00");
    rentTotalValue->setText("R0.00");
    rentVehicleValue->setText("-");
    rentConfirmButton->setEnabled(false);

    if (id.isEmpty()) {
        rentAvailabilityValue->setText("Waiting for vehicle ID");
        rentAvailabilityValue->setStyleSheet(
            "font-size:15px; font-weight:bold; color:#6b7280; background:#f9fafb; "
            "border:1px solid #e5e7eb; border-radius:6px; padding:8px 10px;");
        rentLookupMessageLabel->setText(
            "Enter an available vehicle ID to view its details and calculate the rental cost.");
        rentLookupMessageLabel->setStyleSheet("color:#6b7280; font-size:12px;");
        return;
    }

    if (vehicle == nullptr) {
        rentAvailabilityValue->setText("Not found");
        rentAvailabilityValue->setStyleSheet(
            "font-size:15px; font-weight:bold; color:#dc2626; background:#fef2f2; "
            "border:1px solid #fecaca; border-radius:6px; padding:8px 10px;");
        rentLookupMessageLabel->setText(
            "No vehicle was found with ID " + id + ". Check the ID or select one from the table below.");
        rentLookupMessageLabel->setStyleSheet("color:#dc2626; font-size:12px;");
        return;
    }

    rentVehicleValue->setText(vehicle->getBrand() + " " + vehicle->getModel());
    rentPriceValue->setText(currency(vehicle->getPricePerDay()));

    if (vehicle->getIsRented()) {
        rentAvailabilityValue->setText("Currently rented");
        rentAvailabilityValue->setStyleSheet(
            "font-size:15px; font-weight:bold; color:#b45309; background:#fffbeb; "
            "border:1px solid #fde68a; border-radius:6px; padding:8px 10px;");
        rentLookupMessageLabel->setText(
            "This vehicle is unavailable because it has an active rental.");
        rentLookupMessageLabel->setStyleSheet("color:#b45309; font-size:12px;");
        return;
    }

    const double total = vehicle->getPricePerDay() * rentDaysSpinBox->value();
    rentTotalValue->setText(currency(total));
    rentAvailabilityValue->setText("Available");
    rentAvailabilityValue->setStyleSheet(
        "font-size:15px; font-weight:bold; color:#15803d; background:#f0fdf4; "
        "border:1px solid #bbf7d0; border-radius:6px; padding:8px 10px;");
    // Explain the current checkout step without implying a booking is
    // completed before the operator confirms the rental.
    const bool customerComplete =
        !rentCustomerNameEdit->text().trimmed().isEmpty() &&
        !rentPhoneEdit->text().trimmed().isEmpty();
    rentLookupMessageLabel->setText(customerComplete
        ? "Details entered. Review the estimate and confirm the rental."
        : "Vehicle found and available. Complete customer details to continue.");
    rentLookupMessageLabel->setStyleSheet("color:#15803d; font-size:12px;");
    // Detailed customer/phone validation remains in rentVehicle(), unchanged.
    rentConfirmButton->setEnabled(customerComplete);
}


void MainWindow::rentVehicle()
{
    const QString vehicleId = rentVehicleIdEdit->text().trimmed().toUpper();
    const QString customerName = rentCustomerNameEdit->text().trimmed();
    const QString phone = rentPhoneEdit->text().trimmed();
    const QDate startDate = rentStartDateEdit->date();
    const int days = rentDaysSpinBox->value();

    QStringList errors;

    if (vehicleId.isEmpty()) {
        errors.append("Vehicle ID is required.");
    }
    if (customerName.isEmpty()) {
        errors.append("Customer full name is required.");
    }
    if (phone.isEmpty()) {
        errors.append("Phone number is required.");
    }
    else if (phone.length() < 7) {
        errors.append("Phone number is too short.");
    }
    if (containsFileSeparator(customerName) || containsFileSeparator(phone)) {
        errors.append("Customer details cannot contain the | character.");
    }
    if (startDate != QDate::currentDate()) {
        errors.append("Rental start date must be today's date.");
    }

    Vehicle* vehicle = rentalSystem.findVehicleById(vehicleId);

    if (vehicle == nullptr) {
        errors.append("The selected vehicle does not exist.");
    }
    else if (vehicle->getIsRented()) {
        errors.append("The selected vehicle is already rented.");
    }

    if (!errors.isEmpty()) {
        showWarningMessage("Rental Error",
                           "Please correct the rental details.",
                           "- " + errors.join("\n- "));
        return;
    }

    const QString rentalId = rentalSystem.generateNextRentalId();
    const QDate expectedReturnDate = startDate.addDays(days);
    const double pricePerDay = vehicle->getPricePerDay();
    const double totalCost = pricePerDay * days;

    Rental* rental = new Rental(rentalId,
                                customerName,
                                phone,
                                vehicleId,
                                startDate,
                                expectedReturnDate,
                                days,
                                pricePerDay,
                                totalCost);

    const QString confirmation =
        QString("Create this rental transaction?\n\n"
                "Rental ID: %1\n"
                "Customer: %2\n"
                "Phone: %3\n"
                "Vehicle: %4 - %5 %6\n"
                "Start date: %7\n"
                "Rental days: %8\n"
                "Expected return: %9\n"
                "Price per day: %10\n"
                "Total cost: %11")
            .arg(rentalId)
            .arg(customerName)
            .arg(phone)
            .arg(vehicleId)
            .arg(vehicle->getBrand())
            .arg(vehicle->getModel())
            .arg(startDate.toString("dd MMM yyyy"))
            .arg(days)
            .arg(expectedReturnDate.toString("dd MMM yyyy"))
            .arg(currency(pricePerDay))
            .arg(currency(totalCost));

    if (!confirmAction(this, "Confirm Rental", confirmation)) {
        delete rental;
        return;
    }

    if (!rentalSystem.createRental(rental)) {
        delete rental;
        showWarningMessage("Rental Error",
                           "The rental could not be created.",
                           "The vehicle may no longer be available. Refresh and try again.");
        return;
    }

    clearRentalForm();
    refreshVehicleTable(availableVehicles());

    QMessageBox::information(
        this,
        "Rental Created",
        QString("Rental %1 was created successfully.\nTotal cost: %2")
            .arg(rentalId, currency(totalCost)));
}


void MainWindow::returnVehicle()
{
    const QString identifier = returnRentalIdEdit->text().trimmed().toUpper();

    if (identifier.isEmpty()) {
        showWarningMessage("Return Error",
                           "Rental ID or vehicle ID is required.",
                           "Enter an active rental ID such as R001 or its vehicle ID.");
        return;
    }

    Rental* rental = rentalSystem.findRentalById(identifier);
    if (rental == nullptr) {
        rental = rentalSystem.findActiveRentalByVehicleId(identifier);
    }

    if (rental == nullptr) {
        showWarningMessage("Return Error",
                           "Active rental not found.",
                           "No active rental matches '" + identifier + "'.");
        return;
    }

    if (rental->getIsReturned()) {
        showWarningMessage("Return Error",
                           "Rental already completed.",
                           "Rental '" + rental->getRentalId() + "' has already been returned.");
        return;
    }

    const QDate actualReturnDate = actualReturnDateEdit->date();

    if (actualReturnDate != QDate::currentDate()) {
        showWarningMessage("Return Error",
                           "Invalid return date.",
                           "The actual return date must be today's date.");
        return;
    }

    if (actualReturnDate < rental->getStartDate()) {
        showWarningMessage("Return Error",
                           "Invalid return date.",
                           "The actual return date cannot be before the rental start date.");
        return;
    }

    const QString confirmation =
        QString("Complete this rental return?\n\n"
                "Rental ID: %1\n"
                "Customer: %2\n"
                "Vehicle ID: %3\n"
                "Expected return: %4\n"
                "Actual return: %5\n"
                "Rental total: %6")
            .arg(rental->getRentalId(),
                 rental->getCustomerName(),
                 rental->getVehicleId(),
                 rental->getExpectedReturnDate().toString("dd MMM yyyy"),
                 actualReturnDate.toString("dd MMM yyyy"),
                 currency(rental->getTotalCost()));

    if (!confirmAction(this, "Confirm Return", confirmation)) {
        return;
    }

    if (!rentalSystem.completeRental(rental->getRentalId(), actualReturnDate)) {
        showWarningMessage("Return Error",
                           "The return could not be completed.",
                           "The rental or vehicle data may have changed.");
        return;
    }

    const QString completedId = rental->getRentalId();
    returnRentalIdEdit->clear();
    actualReturnDateEdit->setDate(QDate::currentDate());
    refreshRentalTable(rentalSystem.getActiveRentals());

    QMessageBox::information(this,
                             "Return Completed",
                             "Rental '" + completedId + "' was returned successfully.");
}

void MainWindow::removeVehicle()
{
    const QString id = removeIdEdit->text().trimmed().toUpper();

    if (id.isEmpty()) {
        showWarningMessage("Remove Error",
                           "Vehicle ID is required.",
                           "Enter the ID of the vehicle to remove.");
        return;
    }

    Vehicle* vehicle = rentalSystem.findVehicleById(id);

    if (vehicle == nullptr) {
        showWarningMessage("Remove Error",
                           "Vehicle not found.",
                           "No vehicle with ID '" + id + "' exists.");
        return;
    }

    if (vehicle->getIsRented() ||
        rentalSystem.findActiveRentalByVehicleId(id) != nullptr) {
        showWarningMessage("Remove Error",
                           "Rented vehicle cannot be removed.",
                           "Complete the active rental return before removing vehicle '" + id + "'.");
        return;
    }

    // Match the C++ domain invariant in the legacy Widgets interface too:
    // returned and archived records must never reference a deleted vehicle.
    if (rentalSystem.hasRentalHistoryForVehicle(id)) {
        showWarningMessage("Remove Error",
                           "Rental history prevents vehicle removal.",
                           "A returned or archived rental still references this vehicle. Preserve the vehicle while that history exists.");
        return;
    }

    const QString confirmation =
        QString("Permanently remove this vehicle from the fleet?\n\n"
                "ID: %1\n"
                "Type: %2\n"
                "Vehicle: %3 %4\n"
                "Price per day: %5")
            .arg(id,
                 vehicle->typeName(),
                 vehicle->getBrand(),
                 vehicle->getModel(),
                 currency(vehicle->getPricePerDay()));

    if (!confirmAction(this, "Confirm Vehicle Removal", confirmation)) {
        return;
    }

    if (!rentalSystem.removeVehicle(id)) {
        showWarningMessage("Remove Error",
                           "The vehicle could not be removed.",
                           "The vehicle may be rented, referenced by historical contracts, or storage may have rejected the change.");
        return;
    }

    removeIdEdit->clear();
    refreshVehicleTable(rentalSystem.getVehicles());

    QMessageBox::information(this,
                             "Vehicle Removed",
                             "Vehicle '" + id + "' was removed successfully.");
}

void MainWindow::archiveRentalRecord()
{
    const QString rentalId = rentalRecordIdEdit->text().trimmed().toUpper();

    if (rentalId.isEmpty()) {
        showWarningMessage("Archive Error",
                           "Rental ID is required.",
                           "Enter the returned rental record that should be archived.");
        return;
    }

    Rental* rental = rentalSystem.findRentalById(rentalId);

    if (rental == nullptr) {
        showWarningMessage("Archive Error",
                           "Rental record not found.",
                           "No rental with ID '" + rentalId + "' exists.");
        return;
    }

    if (!rental->getIsReturned()) {
        showWarningMessage("Archive Error",
                           "Active rental cannot be archived.",
                           "Complete the vehicle return before archiving this record.");
        return;
    }

    if (rental->getIsArchived()) {
        showWarningMessage("Archive Error",
                           "Record already archived.",
                           "Rental '" + rentalId + "' is already in Archived Records.");
        return;
    }

    if (!confirmAction(this,
                       "Confirm Archive",
                       "Archive rental record '" + rentalId + "'?\n\n"
                                                              "The record will remain in rentals.txt and can be reviewed under Archived Records.")) {
        return;
    }

    if (!rentalSystem.archiveRental(rentalId)) {
        showWarningMessage("Archive Error",
                           "The record could not be archived.",
                           "Only completed rental records can be archived.");
        return;
    }

    rentalRecordIdEdit->clear();
    refreshRentalTable(filteredRentals());

    QMessageBox::information(this,
                             "Record Archived",
                             "Rental record '" + rentalId + "' was archived successfully.");
}

void MainWindow::deleteArchivedRentalRecord()
{
    const QString rentalId = rentalRecordIdEdit->text().trimmed().toUpper();

    if (rentalId.isEmpty()) {
        showWarningMessage("Delete Error",
                           "Rental ID is required.",
                           "Enter the archived rental record that should be permanently deleted.");
        return;
    }

    Rental* rental = rentalSystem.findRentalById(rentalId);

    if (rental == nullptr) {
        showWarningMessage("Delete Error",
                           "Rental record not found.",
                           "No rental with ID '" + rentalId + "' exists.");
        return;
    }

    if (!rental->getIsArchived()) {
        showWarningMessage("Delete Error",
                           "Record must be archived first.",
                           "Only archived rental records can be permanently deleted.");
        return;
    }

    if (!confirmAction(this,
                       "Confirm Permanent Deletion",
                       "Permanently delete archived rental record '" + rentalId + "'?\n\n"
                                                                                  "This action removes the record from rentals.txt and cannot be undone.")) {
        return;
    }

    if (!rentalSystem.deleteArchivedRental(rentalId)) {
        showWarningMessage("Delete Error",
                           "The record could not be deleted.",
                           "Only archived records can be permanently deleted.");
        return;
    }

    rentalRecordIdEdit->clear();
    refreshRentalTable(filteredRentals());

    QMessageBox::information(this,
                             "Record Deleted",
                             "Archived rental record '" + rentalId + "' was permanently deleted.");
}

void MainWindow::refreshRentalRecordsFilter()
{
    if (actionStack->currentIndex() == 4) {
        refreshRentalTable(filteredRentals());
    }
}

void MainWindow::updateGeneratedVehicleId()
{
    addIdEdit->setText(rentalSystem.generateNextVehicleId(selectedVehicleType));
}

void MainWindow::clearAddFields()
{
    addBrandEdit->clear();
    addModelEdit->clear();
    addPriceEdit->clear();
    addExtraEdit->clear();
    updateGeneratedVehicleId();
    addBrandEdit->setFocus();
}

void MainWindow::clearRentalForm()
{
    rentVehicleIdEdit->clear();
    rentCustomerNameEdit->clear();
    rentPhoneEdit->clear();
    rentStartDateEdit->setMinimumDate(QDate::currentDate());
    rentStartDateEdit->setMaximumDate(QDate::currentDate());
    rentStartDateEdit->setDate(QDate::currentDate());
    rentDaysSpinBox->setValue(1);
    updateRentalEstimate();
    rentVehicleIdEdit->setFocus();
}


void MainWindow::updateAddExtraLabel()
{
    if (selectedVehicleType == "Car") {
        addExtraLabel->setText("Number of Doors");
        addBrandEdit->setPlaceholderText("e.g. Toyota");
        addModelEdit->setPlaceholderText("e.g. Corolla");
        addPriceEdit->setPlaceholderText("e.g. 450.00");
        addExtraEdit->setPlaceholderText("e.g. 4");
    }
    else {
        addExtraLabel->setText("Engine Capacity CC");
        addBrandEdit->setPlaceholderText("e.g. Yamaha");
        addModelEdit->setPlaceholderText("e.g. MT-07");
        addPriceEdit->setPlaceholderText("e.g. 300.00");
        addExtraEdit->setPlaceholderText("e.g. 689");
    }

    updateGeneratedVehicleId();
}

QString MainWindow::currency(double value)
{
    return "R" + QString::number(value, 'f', 2);
}

bool MainWindow::containsFileSeparator(const QString& value)
{
    return value.contains('|');
}

void MainWindow::exitApplication()
{
    if (confirmAction(this,
                      "Exit Application",
                      "Are you sure you want to exit the Vehicle Rental Management System?")) {
        close();
    }
}