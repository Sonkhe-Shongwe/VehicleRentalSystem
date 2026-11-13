@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem FUNC-D2: safe local functional diagnostic of an extracted QA kit.
rem Only fixed, SHA-pinned fictional records. No imports or customer data.
set "RS_ROOT=%~dp0"
set "RS_EXE=%RS_ROOT%RentalStudioP2Review.exe"
if not exist "%RS_EXE%" goto :missing
if not exist "%RS_ROOT%demo\vehicles.txt" goto :missing
if not exist "%RS_ROOT%demo\rentals.txt" goto :missing
if not exist "%RS_ROOT%SOURCE_COMMIT.txt" goto :missing

rem Clear developer test overrides inside this CMD child process only.
set "QT_SCALE_FACTOR="
set "QT_QPA_PLATFORM="
set "QT_PLUGIN_PATH="
set "QML_IMPORT_PATH="
set "QML2_IMPORT_PATH="
set "QT_QPA_PLATFORM_PLUGIN_PATH="

rem Create a unique evidence folder; never overwrite previous reports.
set "RS_REPORT=%RS_ROOT%functional-check-evidence-%RANDOM%-%RANDOM%"
if exist "%RS_REPORT%" goto :report_error
mkdir "%RS_REPORT%"
if errorlevel 1 goto :report_error
set "RS_SUMMARY=%RS_REPORT%\RESULTS.txt"
> "%RS_SUMMARY%" echo Rental Studio 2.0 - fictional functional verification
>> "%RS_SUMMARY%" echo Manual Windows desktop, scaling and Narrator: NOT TESTED
>> "%RS_SUMMARY%" echo No real customer records, data migration or installer approval
set /p RS_SOURCE=<"%RS_ROOT%SOURCE_COMMIT.txt"
>> "%RS_SUMMARY%" echo Source commit: %RS_SOURCE%
echo.
echo Running real Qt/C++ checks against protected fictional records...
echo Evidence folder: %RS_REPORT%
echo.

pushd "%RS_REPORT%"
if errorlevel 1 goto :report_error
call :check "Original model and fixture integrity" "01-source" "--self-test" ""
if errorlevel 1 goto :failed
call :check "Real Qt window startup and available desktop fit" "02-startup" "--startup-fit-smoke" ""
if errorlevel 1 goto :failed
call :check "Full create-book-return-remove journey, wide" "03-journey-wide" "--p6-integrated-flow" ""
if errorlevel 1 goto :failed
call :check "Full create-book-return-remove journey, compact" "04-journey-compact" "--p6-integrated-flow" "--compact"
if errorlevel 1 goto :failed
call :check "Independent-process fictional persistence, compact" "05-restart" "--p6-restart-flow" "--compact"
if errorlevel 1 goto :failed
call :check "Stale booking quote is rejected, compact" "06-stale-quote" "--p5-stale-quote-smoke" "--compact"
if errorlevel 1 goto :failed
call :check "Cancelled booking is not saved, compact" "07-cancel" "--p5-booking-cancel-smoke" "--compact"
if errorlevel 1 goto :failed
call :check "Original fictional source still passes integrity" "08-source-after" "--self-test" ""
if errorlevel 1 goto :failed
popd
>> "%RS_SUMMARY%" echo FINAL: PASS - all eight checks exited 0
echo.
echo ALL 8 REAL APP FUNCTIONAL CHECKS PASSED.
echo This does not certify physical Windows scaling, Narrator or release.
echo Open RESULTS.txt in the evidence folder for the check list.
exit /b 0

:check
echo Checking: %~1
"%RS_EXE%" %~3 %~4 > "%RS_REPORT%\%~2.log" 2>&1
if not "%ERRORLEVEL%"=="0" goto :check_failed
>> "%RS_SUMMARY%" echo %~1: PASS
echo PASS: %~1
exit /b 0

:check_failed
>> "%RS_SUMMARY%" echo %~1: FAIL - exit %ERRORLEVEL%
echo FAILED: %~1
echo See %~2.log in the evidence folder.
exit /b 1

:failed
popd
>> "%RS_SUMMARY%" echo FINAL: FAIL - functional check incomplete
echo.
echo Some functional checks FAILED. No product acceptance can be claimed.
echo Review detailed logs privately before sharing them.
exit /b 1

:missing
echo ERROR: Complete Windows preview kit not found. Extract the entire ZIP.
echo Do not replace demo fixtures with business files.
exit /b 2

:report_error
echo ERROR: Could not create a fresh functional-check evidence directory.
exit /b 3
