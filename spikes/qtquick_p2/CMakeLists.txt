# Real Qt Quick proof, opt-in only. This does not replace the Qt Widgets target.
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickDialogs2 Test)
qt_add_executable(rental_studio_qtquick_spike main.cpp
    p5_synthetic_session.h p5_synthetic_session.cpp
    motion_policy.h motion_policy.cpp
    fleet_add_controller.h fleet_add_controller.cpp
    fleet_photo_store.h fleet_photo_store.cpp
    fleet_remove_controller.h fleet_remove_controller.cpp
    p6_archived_delete_controller.h p6_archived_delete_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/application/fleet_read_service.cpp
    ${PROJECT_SOURCE_DIR}/src/application/rental_read_service.cpp
    ${PROJECT_SOURCE_DIR}/src/application/rental_list_model.h
    ${PROJECT_SOURCE_DIR}/src/application/rental_list_model.cpp
    ${PROJECT_SOURCE_DIR}/src/application/rental_quote_service.cpp
    ${PROJECT_SOURCE_DIR}/src/application/rental_confirm_service.cpp
    ${PROJECT_SOURCE_DIR}/src/application/rental_return_controller.h
    ${PROJECT_SOURCE_DIR}/src/application/rental_return_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/application/rental_booking_controller.h
    ${PROJECT_SOURCE_DIR}/src/application/rental_booking_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/application/rental_archive_controller.h
    ${PROJECT_SOURCE_DIR}/src/application/rental_archive_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/application/rental_admin_commands.cpp)
target_link_libraries(rental_studio_qtquick_spike
    PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2 Qt6::Test)
if(WIN32)
    target_link_libraries(rental_studio_qtquick_spike PRIVATE user32)
endif()
# Reuse existing rights-audited, illustrative repo JPEGs in the new
# *fictional* Fleet gallery. They are candidates, not final model/year photos.
# Honda/Yamaha retain a neutral silhouette pending source/owner approval.
qt_add_resources(rental_studio_qtquick_spike "fleet_demo_photo_candidates"
    PREFIX "/fleet-demo-photos"
    BASE "${PROJECT_SOURCE_DIR}/assets"
    FILES
        "${PROJECT_SOURCE_DIR}/assets/toyota_corolla.jpg"
        "${PROJECT_SOURCE_DIR}/assets/ford_ranger.jpg"
        "${PROJECT_SOURCE_DIR}/assets/hyundai_i20.jpg")
# R2 deliberate Inter-vs-system typography trial (OFF in normal offline builds).
# CI opts in to fetch verified pinned official bytes and bundle the TTF locally.
option(BUILD_R2_INTER_FONT_TRIAL "Embed an opt-in source-verified Inter variable font for Qt screenshot comparison" OFF)
if(BUILD_R2_INTER_FONT_TRIAL)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(R2_INTER_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/r2_inter_font")
    set(R2_INTER_FILE "${R2_INTER_BUILD_DIR}/InterVariable.ttf")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/fetch_r2_inter_trial.py"
                --output "${R2_INTER_FILE}"
        RESULT_VARIABLE R2_INTER_FETCH_RESULT
        OUTPUT_VARIABLE R2_INTER_FETCH_OUT
        ERROR_VARIABLE R2_INTER_FETCH_ERR
    )
    if(NOT R2_INTER_FETCH_RESULT EQUAL 0 OR NOT EXISTS "${R2_INTER_FILE}")
        message(FATAL_ERROR "Inter pilot font source verification failed: ${R2_INTER_FETCH_OUT} ${R2_INTER_FETCH_ERR}")
    endif()
    message(STATUS "${R2_INTER_FETCH_OUT}")
    qt_add_resources(rental_studio_qtquick_spike "r2_inter_font_trial"
        PREFIX "/r2-fonts"
        BASE "${R2_INTER_BUILD_DIR}"
        FILES "${R2_INTER_FILE}")
    qt_add_resources(rental_studio_qtquick_spike "r2_inter_font_license"
        PREFIX "/r2-font-notices"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/r2_fonts"
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/r2_fonts/INTER_OFL.txt")
    target_compile_definitions(rental_studio_qtquick_spike PRIVATE RENTAL_STUDIO_R2_INTER_TRIAL=1)
endif()
# R2 licensed, offline local SVG icon resources.
set(R2_ICON_NAMES layout-grid car-front clipboard-list plus layers key-round database circle-check chevron-right x)
set(R2_ICON_ASSETS)
foreach(R2_TONE inverse active emerald)
    foreach(R2_NAME IN LISTS R2_ICON_NAMES)
        list(APPEND R2_ICON_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/assets/r2_icons/${R2_TONE}/${R2_NAME}.svg")
    endforeach()
endforeach()
qt_add_resources(rental_studio_qtquick_spike "r2_lucide_icons"
    PREFIX "/r2-icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/r2_icons"
    FILES ${R2_ICON_ASSETS} "${CMAKE_CURRENT_SOURCE_DIR}/assets/r2_icons/LICENSE-LUCIDE.txt")
find_package(Qt6 6.5 COMPONENTS Svg QUIET)
if(TARGET Qt6::Svg)
    target_link_libraries(rental_studio_qtquick_spike PRIVATE Qt6::Svg)
endif()
# R0 portfolio: embedded fingerprint-guarded demo and optional local CC0 photos.
qt_add_resources(rental_studio_qtquick_spike "portfolio_seed"
    PREFIX "/portfolio-demo"
    BASE "${PROJECT_SOURCE_DIR}/portfolio"
    FILES
        "${PROJECT_SOURCE_DIR}/portfolio/vehicles.txt"
        "${PROJECT_SOURCE_DIR}/portfolio/photo_map.tsv")
file(GLOB PORTFOLIO_CC0_IMAGES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/assets/portfolio/*.jpg")
if(PORTFOLIO_CC0_IMAGES)
    qt_add_resources(rental_studio_qtquick_spike "portfolio_cc0_images"
        PREFIX "/portfolio-photos"
        BASE "${PROJECT_SOURCE_DIR}/assets/portfolio"
        FILES ${PORTFOLIO_CC0_IMAGES})
endif()
# Only intentionally model-matched candidate files enter the QA executable.
# Attribution and per-file licences are shipped with the Windows review ZIP.
file(STRINGS "${PROJECT_SOURCE_DIR}/catalogue_acquisition/model_photo_map.tsv"
    CURATED_PHOTO_MAP_LINES)
set(CURATED_PHOTO_FILES)
foreach(CURATED_PHOTO_MAP_LINE IN LISTS CURATED_PHOTO_MAP_LINES)
    string(REPLACE "|" ";" CURATED_PHOTO_FIELDS "${CURATED_PHOTO_MAP_LINE}")
    list(LENGTH CURATED_PHOTO_FIELDS CURATED_PHOTO_FIELDS_COUNT)
    if(NOT CURATED_PHOTO_FIELDS_COUNT EQUAL 3)
        message(FATAL_ERROR "Invalid curated model image map row")
    endif()
    list(GET CURATED_PHOTO_FIELDS 2 CURATED_PHOTO_RELATIVE)
    if(NOT CURATED_PHOTO_RELATIVE MATCHES "^batch0[1-5]/images/[a-z0-9_]+$")
        message(FATAL_ERROR "Unsafe model image path")
    endif()
    set(CURATED_PHOTO_SOURCE "${PROJECT_SOURCE_DIR}/catalogue_acquisition/${CURATED_PHOTO_RELATIVE}.jpg")
    if(NOT EXISTS "${CURATED_PHOTO_SOURCE}")
        message(FATAL_ERROR "Curated model reference image missing")
    endif()
    list(APPEND CURATED_PHOTO_FILES "${CURATED_PHOTO_SOURCE}")
endforeach()
qt_add_resources(rental_studio_qtquick_spike "curated_model_family_photos"
    PREFIX "/catalogue-photos"
    BASE "${PROJECT_SOURCE_DIR}/catalogue_acquisition"
    FILES
        "${PROJECT_SOURCE_DIR}/catalogue_acquisition/model_photo_map.tsv"
        ${CURATED_PHOTO_FILES})
# Exact existing author/source/licence metadata, bundled for offline display.
qt_add_resources(rental_studio_qtquick_spike "curated_photo_credit_metadata"
    PREFIX "/fleet-credit-data"
    BASE "${PROJECT_SOURCE_DIR}/catalogue_acquisition"
    FILES
        "${PROJECT_SOURCE_DIR}/catalogue_acquisition/batch01/source_manifest.json"
        "${PROJECT_SOURCE_DIR}/catalogue_acquisition/batch02/source_manifest.json"
        "${PROJECT_SOURCE_DIR}/catalogue_acquisition/batch03/source_manifest.json"
        "${PROJECT_SOURCE_DIR}/catalogue_acquisition/batch04/source_manifest.json"
        "${PROJECT_SOURCE_DIR}/catalogue_acquisition/batch05/source_manifest.json")
qt_add_resources(rental_studio_qtquick_spike "portfolio_photo_credit_metadata"
    PREFIX "/portfolio-credit-data"
    BASE "${PROJECT_SOURCE_DIR}/assets/portfolio"
    FILES "${PROJECT_SOURCE_DIR}/assets/portfolio/source_manifest.json")
# Explicit fictional-only integration into this REAL Qt Quick executable.
# NOT the default workspace, not commercial storage and not a Widgets change.
if(BUILD_P7_SQLITE_SANDBOX)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Sql)
    target_link_libraries(rental_studio_qtquick_spike PRIVATE Qt6::Sql)
    target_compile_definitions(rental_studio_qtquick_spike PRIVATE RENTAL_STUDIO_P7_SQL_LAB=1)
    target_sources(rental_studio_qtquick_spike PRIVATE
        ${PROJECT_SOURCE_DIR}/src/application/p7_sandbox/sqlite_repository.h
        ${PROJECT_SOURCE_DIR}/src/application/p7_sandbox/sqlite_read_projection.h
        ${PROJECT_SOURCE_DIR}/src/application/p7_sandbox/fictional_fleet_snapshot.h
        ${PROJECT_SOURCE_DIR}/src/application/p7_sandbox/fictional_operations_snapshot.h
        ${PROJECT_SOURCE_DIR}/src/application/p7_sandbox/repository_preview_controller.h)
endif()
# Compile the SHA-256 of the exact fictional fixtures into the executable.
# Neither a portable kit nor Qt Creator may ingest arbitrary operational records.
file(SHA256 "${PROJECT_SOURCE_DIR}/demo/vehicles.txt" P2_VEHICLES_SHA256)
file(SHA256 "${PROJECT_SOURCE_DIR}/demo/rentals.txt" P2_RENTALS_SHA256)
file(SHA256 "${PROJECT_SOURCE_DIR}/portfolio/vehicles.txt" PORTFOLIO_VEHICLES_SHA256)
target_compile_definitions(rental_studio_qtquick_spike PRIVATE
    RENTAL_STUDIO_DEMO_DIR="${PROJECT_SOURCE_DIR}/demo"
    RENTAL_STUDIO_DEMO_VEHICLES_SHA256="${P2_VEHICLES_SHA256}"
    RENTAL_STUDIO_DEMO_RENTALS_SHA256="${P2_RENTALS_SHA256}"
    RENTAL_STUDIO_PORTFOLIO_VEHICLES_SHA256="${PORTFOLIO_VEHICLES_SHA256}")
target_include_directories(rental_studio_qtquick_spike PRIVATE "${PROJECT_SOURCE_DIR}")
target_sources(rental_studio_qtquick_spike PRIVATE
    "${PROJECT_SOURCE_DIR}/vehicle.cpp"
    "${PROJECT_SOURCE_DIR}/car.cpp"
    "${PROJECT_SOURCE_DIR}/motorcycle.cpp"
    "${PROJECT_SOURCE_DIR}/rental.cpp"
    "${PROJECT_SOURCE_DIR}/rentalsystem.cpp")
# Opt in to the standard qrc:/qt/qml prefix for engine.loadFromModule.
qt_policy(SET QTP0001 NEW)
qt_add_qml_module(rental_studio_qtquick_spike
    URI RentalStudioSpike
    VERSION 1.0
    QML_FILES Main.qml DesignTokens.qml PrecisionPrimaryButton.qml OverviewAvailabilityPanel.qml FleetStatusPill.qml FocusFrame.qml DangerButton.qml VehicleSilhouette.qml WorkspaceNavButton.qml SidebarGlyph.qml R2LucideIcon.qml FleetAddDialog.qml FleetRemoveDialog.qml P6ArchivedDeleteDialog.qml
              AccessibleTextField.qml DomainMessage.qml RentalStatusLabel.qml
              ConfirmActionDialog.qml OverviewMetricCard.qml EditorialVehiclePreview.qml P7FictionalSqlLab.qml P7LabSnapshotCard.qml)
if(BUILD_TESTING)
    if(BUILD_P7_SQLITE_SANDBOX)
        foreach(SIZE wide compact)
            set(P7_ARGS --p7-fictional-sql-app-smoke)
            if(SIZE STREQUAL "compact")
                list(APPEND P7_ARGS --compact)
            endif()
            add_test(NAME rental_studio_p7_actual_app_lab_${SIZE}
                COMMAND rental_studio_qtquick_spike ${P7_ARGS})
            set_tests_properties(rental_studio_p7_actual_app_lab_${SIZE}
                PROPERTIES ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 65)
        endforeach()
    endif()
    # Precision Automotive Light Fleet: gallery, table headings, inspector,
    # read-only availability, photo framing and protected demo sources.
    foreach(SIZE wide compact)
        set(PA_FLEET_ARGS --pa-fleet-003-smoke)
        if(SIZE STREQUAL "compact")
            list(APPEND PA_FLEET_ARGS --compact)
        endif()
        add_test(NAME rental_studio_pa_fleet_003_${SIZE}
            COMMAND rental_studio_qtquick_spike ${PA_FLEET_ARGS})
        set_tests_properties(rental_studio_pa_fleet_003_${SIZE}
            PROPERTIES ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 65)
    endforeach()
    # Phase 1D: real Rentals QML hierarchy, screenshot and fixture integrity.
    foreach(SIZE wide compact)
        set(PA_RENTALS_ARGS --pa-rentals-004-smoke)
        if(SIZE STREQUAL "compact")
            list(APPEND PA_RENTALS_ARGS --compact)
        endif()
        add_test(NAME rental_studio_pa_rentals_004_${SIZE}
            COMMAND rental_studio_qtquick_spike ${PA_RENTALS_ARGS})
        set_tests_properties(rental_studio_pa_rentals_004_${SIZE}
            PROPERTIES ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 65)
    endforeach()
    # R2 real Qt opt-in Inter font trial, wide/compact. The original
    # pa-shell-001 screenshots provide the unchanged Segoe/system baseline.
    foreach(SIZE wide compact)
        set(R2_FONT_ARGS --r2-font-inter --r2-font-trial-smoke)
        if(SIZE STREQUAL "compact")
            list(APPEND R2_FONT_ARGS --compact)
        endif()
        if(BUILD_R2_INTER_FONT_TRIAL)
            add_test(NAME rental_studio_r2_inter_font_${SIZE}
                COMMAND rental_studio_qtquick_spike ${R2_FONT_ARGS})
            set_tests_properties(rental_studio_r2_inter_font_${SIZE}
                PROPERTIES ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 65)
        endif()
    endforeach()
    # Precision Automotive Light dashboard: real totals, availability ring,
    # latest contracts, 1280/1500 captures and no fixture modification.
    foreach(SIZE wide compact)
        set(PA_OVERVIEW_ARGS --pa-overview-002-smoke)
        if(SIZE STREQUAL "compact")
            list(APPEND PA_OVERVIEW_ARGS --compact)
        endif()
        add_test(NAME rental_studio_pa_overview_002_${SIZE}
            COMMAND rental_studio_qtquick_spike ${PA_OVERVIEW_ARGS})
        set_tests_properties(rental_studio_pa_overview_002_${SIZE}
            PROPERTIES ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 65)
    endforeach()
    # Precision Automotive real native shell: sidebar action, booking modal,
    # visible screenshots and untouched protected five-fixture records.
    foreach(SIZE wide compact)
        set(PA_ARGS --pa-shell-001-smoke)
        if(SIZE STREQUAL "compact")
            list(APPEND PA_ARGS --compact)
        endif()
        add_test(NAME rental_studio_pa_shell_001_${SIZE}
            COMMAND rental_studio_qtquick_spike ${PA_ARGS})
        set_tests_properties(rental_studio_pa_shell_001_${SIZE}
            PROPERTIES ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 65)
    endforeach()
    # Real 131-entry fictional gallery at both sizes; five-fixture tests intact.
    foreach(SIZE wide compact)
        set(PORTFOLIO_ARGS --portfolio-catalog-smoke)
        if(SIZE STREQUAL "compact")
            list(APPEND PORTFOLIO_ARGS --compact)
        endif()
        add_test(NAME rental_studio_portfolio_catalog_${SIZE}
            COMMAND rental_studio_qtquick_spike ${PORTFOLIO_ARGS})
        set_tests_properties(rental_studio_portfolio_catalog_${SIZE}
            PROPERTIES ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 65)
    endforeach()
    # Real separate executable processes, opt-in fixed Qt TEST location.
    # Cleanup has its own fixture so CTest also runs it after failures.
    add_test(NAME rental_studio_p5_session_seed
        COMMAND rental_studio_qtquick_spike --p5-session-seed-test)
    add_test(NAME rental_studio_p5_session_reopen
        COMMAND rental_studio_qtquick_spike --p5-session-reopen-test)
    add_test(NAME rental_studio_p5_session_tamper
        COMMAND rental_studio_qtquick_spike --p5-session-tamper-test)
    add_test(NAME rental_studio_p5_session_clean
        COMMAND rental_studio_qtquick_spike --p5-session-clean-test)
    set_tests_properties(rental_studio_p5_session_seed PROPERTIES
        FIXTURES_SETUP p5_synthetic_session
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 25)
    set_tests_properties(rental_studio_p5_session_reopen PROPERTIES
        FIXTURES_REQUIRED p5_synthetic_session
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 25)
    set_tests_properties(rental_studio_p5_session_tamper PROPERTIES
        FIXTURES_REQUIRED p5_synthetic_session
        DEPENDS rental_studio_p5_session_reopen
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 25)
    set_tests_properties(rental_studio_p5_session_clean PROPERTIES
        FIXTURES_CLEANUP p5_synthetic_session
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 25)
    # P5 live native QML first booking, second-process real QML Dashboard
    # recovery, and isolated deterministic disk-write rollback.
    add_test(NAME rental_studio_p5_ui_session_seed
        COMMAND rental_studio_qtquick_spike --p5-ui-session-seed-test)
    add_test(NAME rental_studio_p5_ui_session_reopen
        COMMAND rental_studio_qtquick_spike --p5-ui-session-reopen-test)
    add_test(NAME rental_studio_p5_ui_session_clean
        COMMAND rental_studio_qtquick_spike --p5-ui-session-clean-test)
    set_tests_properties(rental_studio_p5_ui_session_seed PROPERTIES
        FIXTURES_SETUP p5_qml_persistent_session
        ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 55)
    set_tests_properties(rental_studio_p5_ui_session_reopen PROPERTIES
        FIXTURES_REQUIRED p5_qml_persistent_session
        ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 55)
    set_tests_properties(rental_studio_p5_ui_session_clean PROPERTIES
        FIXTURES_CLEANUP p5_qml_persistent_session
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
    add_test(NAME rental_studio_p5_ui_write_failure_wide
        COMMAND rental_studio_qtquick_spike --p5-ui-write-failure-test)
    add_test(NAME rental_studio_p5_ui_write_failure_compact
        COMMAND rental_studio_qtquick_spike --p5-ui-write-failure-test --compact)
    set_tests_properties(
        rental_studio_p5_ui_write_failure_wide
        rental_studio_p5_ui_write_failure_compact PROPERTIES
        ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 55)
    add_test(NAME rental_studio_p2_model COMMAND rental_studio_qtquick_spike --self-test)
    set_tests_properties(rental_studio_p2_model PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 20)
    add_test(NAME rental_studio_p2_qml_wide COMMAND rental_studio_qtquick_spike --ui-smoke)
    add_test(NAME rental_studio_p2_qml_compact COMMAND rental_studio_qtquick_spike --ui-smoke --compact)
    add_test(NAME rental_studio_p2_keyboard_resize COMMAND rental_studio_qtquick_spike --input-smoke)
    set_tests_properties(rental_studio_p2_keyboard_resize PROPERTIES
        ENVIRONMENT "QT_QUICK_BACKEND=software"
        TIMEOUT 35)
    add_test(NAME rental_studio_p2_arrows_focus COMMAND rental_studio_qtquick_spike --arrows-smoke)
    set_tests_properties(rental_studio_p2_arrows_focus PROPERTIES
        ENVIRONMENT "QT_QUICK_BACKEND=software"
        TIMEOUT 40)
    add_test(NAME rental_studio_p2_focus_edges COMMAND rental_studio_qtquick_spike --focus-edges-smoke)
    set_tests_properties(rental_studio_p2_focus_edges PROPERTIES
        ENVIRONMENT "QT_QUICK_BACKEND=software"
        TIMEOUT 40)
    # Genuine image-import/invalid-path/fixture-integrity and actual UI photo
    # rendering tests, wide and compact, no real customer/photo storage.
    add_test(NAME rental_studio_fleet_photo_wide
        COMMAND rental_studio_qtquick_spike --fleet-photo-smoke)
    add_test(NAME rental_studio_fleet_photo_compact
        COMMAND rental_studio_qtquick_spike --fleet-photo-smoke --compact)
    set_tests_properties(rental_studio_fleet_photo_wide
        rental_studio_fleet_photo_compact PROPERTIES
        ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 70)
    add_test(NAME rental_studio_p6_fleet_add_wide COMMAND rental_studio_qtquick_spike --p6-fleet-add-smoke)
    add_test(NAME rental_studio_p6_fleet_add_compact COMMAND rental_studio_qtquick_spike --p6-fleet-add-smoke --compact)
    set_tests_properties(rental_studio_p6_fleet_add_wide rental_studio_p6_fleet_add_compact PROPERTIES ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 65)
    # P6-B: real native keyboard-driven confirmation, cancel/Escape,
    # rented-vehicle refusal, failed atomic writer rollback and reopen.
    add_test(NAME rental_studio_p6_fleet_remove_wide
        COMMAND rental_studio_qtquick_spike --p6-fleet-remove-smoke)
    add_test(NAME rental_studio_p6_fleet_remove_compact
        COMMAND rental_studio_qtquick_spike --p6-fleet-remove-smoke --compact)
    set_tests_properties(rental_studio_p6_fleet_remove_wide
        rental_studio_p6_fleet_remove_compact PROPERTIES
        ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 70)
    # P6-C: real Qt keyboard archive -> exact-confirmed permanent deletion.
    # Tests include Cancel/Escape, invalid phrase, active/returned refusal,
    # rejected storage commit rollback, fixed synthetic mode denial and restart.
    add_test(NAME rental_studio_p6_archived_delete_wide
        COMMAND rental_studio_qtquick_spike --p6-archived-delete-smoke)
    add_test(NAME rental_studio_p6_archived_delete_compact
        COMMAND rental_studio_qtquick_spike --p6-archived-delete-smoke --compact)
    set_tests_properties(rental_studio_p6_archived_delete_wide
        rental_studio_p6_archived_delete_compact PROPERTIES
        ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 75)
    # P6-M: full Qt operator journey across three independent executables,
    # exact stage-allowlisted fictional files, positive and refusal paths.
    add_test(NAME rental_studio_p6_integrated_wide
        COMMAND rental_studio_qtquick_spike --p6-integrated-flow)
    add_test(NAME rental_studio_p6_integrated_compact
        COMMAND rental_studio_qtquick_spike --p6-integrated-flow --compact)
    set_tests_properties(rental_studio_p6_integrated_wide
        rental_studio_p6_integrated_compact PROPERTIES
        ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 155)
    # P6-E: each CTest itself launches three genuinely independent native
    # Qt GUI processes within a unique parent-owned disposable synthetic root.
    # Original fixture hashes and strict child allowlisting protect all inputs.
    add_test(NAME rental_studio_p6_cross_process_wide
        COMMAND rental_studio_qtquick_spike --p6-restart-flow)
    add_test(NAME rental_studio_p6_cross_process_compact
        COMMAND rental_studio_qtquick_spike --p6-restart-flow --compact)
    set_tests_properties(rental_studio_p6_cross_process_wide
        rental_studio_p6_cross_process_compact PROPERTIES
        ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 150)
    # P3-F: genuine QQuickWindow and delegate-backed Rentals state, selection
    # and refresh after a committed C++ fictional-domain operation.
    # UX-S1: ensure the entire Rentals page can reveal lower controls.
    add_test(NAME rental_studio_ux_s1_scroll_wide
        COMMAND rental_studio_qtquick_spike --rentals-page-scroll-smoke)
    add_test(NAME rental_studio_ux_s1_scroll_compact
        COMMAND rental_studio_qtquick_spike --rentals-page-scroll-smoke --compact)
    set_tests_properties(rental_studio_ux_s1_scroll_wide
        rental_studio_ux_s1_scroll_compact PROPERTIES
        ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 55)
    add_test(NAME rental_studio_p3_rentals_qml_wide
        COMMAND rental_studio_qtquick_spike --rentals-smoke)
    add_test(NAME rental_studio_p3_rentals_qml_compact
        COMMAND rental_studio_qtquick_spike --rentals-smoke --compact)
    set_tests_properties(
        rental_studio_p3_rentals_qml_wide
        rental_studio_p3_rentals_qml_compact
        PROPERTIES ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 50)
    # P3-H: real wide/compact Qt Quick Active/History filter and keyboard
    # selection checks, never a QML implementation of lifecycle rules.
    add_test(NAME rental_studio_p3_filter_qml_wide
        COMMAND rental_studio_qtquick_spike --rentals-filter-smoke)
    add_test(NAME rental_studio_p3_filter_qml_compact
        COMMAND rental_studio_qtquick_spike --rentals-filter-smoke --compact)
    set_tests_properties(
        rental_studio_p3_filter_qml_wide
        rental_studio_p3_filter_qml_compact
        PROPERTIES ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 50)
    # P3-I: real confirmed-return QML dialog with domain QSaveFile,
    # post-commit only dual-model refresh, and protected demo source files.
    add_test(NAME rental_studio_p3_return_qml_wide
        COMMAND rental_studio_qtquick_spike --rentals-return-smoke)
    add_test(NAME rental_studio_p3_return_qml_compact
        COMMAND rental_studio_qtquick_spike --rentals-return-smoke --compact)
    set_tests_properties(
        rental_studio_p3_return_qml_wide
        rental_studio_p3_return_qml_compact
        PROPERTIES ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 60)
    add_test(NAME rental_studio_p3_booking_qml_wide
        COMMAND rental_studio_qtquick_spike --rentals-booking-smoke)
    add_test(NAME rental_studio_p3_booking_qml_compact
        COMMAND rental_studio_qtquick_spike --rentals-booking-smoke --compact)
    set_tests_properties(
        rental_studio_p3_booking_qml_wide
        rental_studio_p3_booking_qml_compact
        PROPERTIES ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 60)
    add_test(NAME rental_studio_p3_archive_qml_wide
        COMMAND rental_studio_qtquick_spike --rentals-archive-smoke)
    add_test(NAME rental_studio_p3_archive_qml_compact
        COMMAND rental_studio_qtquick_spike --rentals-archive-smoke --compact)
    set_tests_properties(
        rental_studio_p3_archive_qml_wide
        rental_studio_p3_archive_qml_compact
        PROPERTIES ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 60)
    # All booking-dismissal paths clear uncommitted contact data, revoke
    # C++ quotes, show honest cancellation and never write a rental.
    add_test(NAME rental_studio_p5_booking_cancel_wide
        COMMAND rental_studio_qtquick_spike --p5-booking-cancel-smoke)
    add_test(NAME rental_studio_p5_booking_cancel_compact
        COMMAND rental_studio_qtquick_spike --p5-booking-cancel-smoke --compact)
    set_tests_properties(
        rental_studio_p5_booking_cancel_wide
        rental_studio_p5_booking_cancel_compact PROPERTIES
        ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 65)
    # P5 stale rate and occupancy both require explicit fresh review; a
    # rejected quote cannot write an optimistic receipt or duplicate rental.
    add_test(NAME rental_studio_p5_stale_quote_wide
        COMMAND rental_studio_qtquick_spike --p5-stale-quote-smoke)
    add_test(NAME rental_studio_p5_stale_quote_compact
        COMMAND rental_studio_qtquick_spike --p5-stale-quote-smoke --compact)
    set_tests_properties(
        rental_studio_p5_stale_quote_wide
        rental_studio_p5_stale_quote_compact PROPERTIES
        ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 60)
    # P5 operator workflow: selected real C++ Fleet item into the guided
    # booking modal with a prefilled authoritative quote and no duplicate CTA.
    add_test(NAME rental_studio_p5_fleet_booking_wide
        COMMAND rental_studio_qtquick_spike --p5-fleet-to-booking-smoke)
    add_test(NAME rental_studio_p5_fleet_booking_compact
        COMMAND rental_studio_qtquick_spike --p5-fleet-to-booking-smoke --compact)
    set_tests_properties(
        rental_studio_p5_fleet_booking_wide
        rental_studio_p5_fleet_booking_compact PROPERTIES
        ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 60)
    # UX-CTRL-002 owner-confirmed keyboard routes, contract Enter and danger colors.
    add_test(NAME rental_studio_ux_ctrl_002_wide
        COMMAND rental_studio_qtquick_spike --ux-ctrl-002-smoke)
    add_test(NAME rental_studio_ux_ctrl_002_compact
        COMMAND rental_studio_qtquick_spike --ux-ctrl-002-smoke --compact)
    set_tests_properties(
        rental_studio_ux_ctrl_002_wide
        rental_studio_ux_ctrl_002_compact PROPERTIES
        ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 65)
    # UX-SHELL-004: owner-requested collapsible icon navigation and deliberate
    # All -> Name -> Search horizontal focus, preserving native caret semantics.
    foreach(SIZE wide compact)
        set(SHELL_ARGS --ux-shell-004-smoke)
        if(SIZE STREQUAL "compact")
            list(APPEND SHELL_ARGS --compact)
        endif()
        add_test(NAME rental_studio_ux_shell_004_${SIZE}
            COMMAND rental_studio_qtquick_spike ${SHELL_ARGS})
        set_tests_properties(rental_studio_ux_shell_004_${SIZE} PROPERTIES
            ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 65)
    endforeach()
    # UX-CTRL-003: real bidirectional and boundary-aware Qt Quick keyboard
    # navigation across Overview, Fleet and Rentals at both logical sizes.
    add_test(NAME rental_studio_ux_ctrl_003_wide
        COMMAND rental_studio_qtquick_spike --ux-ctrl-003-smoke)
    add_test(NAME rental_studio_ux_ctrl_003_compact
        COMMAND rental_studio_qtquick_spike --ux-ctrl-003-smoke --compact)
    set_tests_properties(
        rental_studio_ux_ctrl_003_wide
        rental_studio_ux_ctrl_003_compact PROPERTIES
        ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 70)
    # P6-K: purpose-limited native contract detail dialog, true actual dates,
    # hidden-selection guards, modal Escape/focus and no accidental writes.
    add_test(NAME rental_studio_p6_contract_detail_wide
        COMMAND rental_studio_qtquick_spike --p6-contract-detail-smoke)
    add_test(NAME rental_studio_p6_contract_detail_compact
        COMMAND rental_studio_qtquick_spike --p6-contract-detail-smoke --compact)
    set_tests_properties(
        rental_studio_p6_contract_detail_wide
        rental_studio_p6_contract_detail_compact
        PROPERTIES ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 60)
    # P6-J: genuine Qt window recent-contract read model and hidden-history
    # read-only routing at two sizes with source byte nonmutation.
    add_test(NAME rental_studio_p6_recent_activity_wide
        COMMAND rental_studio_qtquick_spike --p6-recent-activity-smoke)
    add_test(NAME rental_studio_p6_recent_activity_compact
        COMMAND rental_studio_qtquick_spike --p6-recent-activity-smoke --compact)
    set_tests_properties(
        rental_studio_p6_recent_activity_wide
        rental_studio_p6_recent_activity_compact
        PROPERTIES ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 60)
    # P5 scoped operational Overview: C++-owned KPIs, post-commit refresh,
    # real wide/compact QQuickWindow and selection-independent totals.
    add_test(NAME rental_studio_p5_overview_wide
        COMMAND rental_studio_qtquick_spike --p5-overview-smoke)
    add_test(NAME rental_studio_p5_overview_compact
        COMMAND rental_studio_qtquick_spike --p5-overview-smoke --compact)
    set_tests_properties(
        rental_studio_p5_overview_wide
        rental_studio_p5_overview_compact
        PROPERTIES ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 65)
    # P4 shared QML token/focus/shell regression at real native window sizes.
    add_test(NAME rental_studio_p4_shared_shell_wide
        COMMAND rental_studio_qtquick_spike --p4-shell-smoke)
    add_test(NAME rental_studio_p4_shared_shell_compact
        COMMAND rental_studio_qtquick_spike --p4-shell-smoke --compact)
    set_tests_properties(
        rental_studio_p4_shared_shell_wide
        rental_studio_p4_shared_shell_compact
        PROPERTIES ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 55)
    # Real component extraction regressions: native fields + shared modal
    # confirmation with explicit Cancel/Escape and zero disk mutations.
    add_test(NAME rental_studio_p4_components_wide
        COMMAND rental_studio_qtquick_spike --p4-components-smoke)
    add_test(NAME rental_studio_p4_components_compact
        COMMAND rental_studio_qtquick_spike --p4-components-smoke --compact)
    set_tests_properties(
        rental_studio_p4_components_wide
        rental_studio_p4_components_compact
        PROPERTIES ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 65)
    add_test(NAME rental_studio_p4_focus_route_wide
        COMMAND rental_studio_qtquick_spike --p4-tab-smoke)
    add_test(NAME rental_studio_p4_focus_route_compact
        COMMAND rental_studio_qtquick_spike --p4-tab-smoke --compact)
    set_tests_properties(
        rental_studio_p4_focus_route_wide
        rental_studio_p4_focus_route_compact
        PROPERTIES ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 65)
    # Validate the actual Qt accessibility interfaces and native keyboard
    # selection-state changes on both real viewport sizes, with fake files.
    add_test(NAME rental_studio_p4_accessibility_wide
        COMMAND rental_studio_qtquick_spike --p4-a11y-smoke)
    add_test(NAME rental_studio_p4_accessibility_compact
        COMMAND rental_studio_qtquick_spike --p4-a11y-smoke --compact)
    set_tests_properties(
        rental_studio_p4_accessibility_wide
        rental_studio_p4_accessibility_compact
        PROPERTIES
        ENVIRONMENT "QT_QUICK_BACKEND=software;QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1"
        TIMEOUT 65)
    # P4: both preference modes, both actual window sizes, live preference
    # change and protected synthetic file assertions. Never mutate host OS.
    foreach(MODE ordinary reduced)
        foreach(SIZE wide compact)
            set(args --p4-motion-smoke)
            if(MODE STREQUAL "reduced")
                list(APPEND args --p4-reduce-motion)
            endif()
            if(SIZE STREQUAL "compact")
                list(APPEND args --compact)
            endif()
            add_test(NAME rental_studio_p4_motion_${MODE}_${SIZE}
                     COMMAND rental_studio_qtquick_spike ${args})
            set_tests_properties(rental_studio_p4_motion_${MODE}_${SIZE}
                PROPERTIES ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 55)
        endforeach()
    endforeach()
    # FUNC-D1: the real operator startup window auto-sizes within QScreen's
    # available logical work area instead of opening at 1500x860 off-screen.
    add_test(NAME rental_studio_functional_startup_fit
        COMMAND rental_studio_qtquick_spike --startup-fit-smoke)
    set_tests_properties(rental_studio_functional_startup_fit PROPERTIES
        ENVIRONMENT "QT_QUICK_BACKEND=software" TIMEOUT 30)
    add_test(NAME rental_studio_p2_synthetic_125 COMMAND rental_studio_qtquick_spike --dpi-smoke)
    add_test(NAME rental_studio_p2_synthetic_150 COMMAND rental_studio_qtquick_spike --dpi-smoke)
    set_tests_properties(rental_studio_p2_synthetic_125 PROPERTIES
        ENVIRONMENT "QT_QUICK_BACKEND=software;QT_SCALE_FACTOR=1.25"
        TIMEOUT 35)
    set_tests_properties(rental_studio_p2_synthetic_150 PROPERTIES
        ENVIRONMENT "QT_QUICK_BACKEND=software;QT_SCALE_FACTOR=1.5"
        TIMEOUT 35)
    set_tests_properties(rental_studio_p2_qml_wide rental_studio_p2_qml_compact PROPERTIES
        ENVIRONMENT "QT_QUICK_BACKEND=software"
        TIMEOUT 40)
endif()
