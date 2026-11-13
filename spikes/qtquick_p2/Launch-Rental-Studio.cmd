@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem FUNC-D1: manual one-click launch of SHA-pinned fictional source only.
rem No elevation, no network, no imports, no production data or persistence.
set "RS_ROOT=%~dp0"
if not exist "%RS_ROOT%RentalStudioP2Review.exe" goto :missing
if not exist "%RS_ROOT%demo\vehicles.txt" goto :missing
if not exist "%RS_ROOT%demo\rentals.txt" goto :missing
if not exist "%RS_ROOT%SOURCE_COMMIT.txt" goto :missing

rem Developer shells can inject broken/scaled Qt paths into a portable kit.
rem Clear inherited overrides only in this temporary CMD child process.
set "QT_SCALE_FACTOR="
set "QT_QPA_PLATFORM="
set "QT_PLUGIN_PATH="
set "QML_IMPORT_PATH="
set "QML2_IMPORT_PATH="
set "QT_QPA_PLATFORM_PLUGIN_PATH="

if /I "%~1"=="--verify" goto :verify
if not "%~1"=="" goto :usage

echo Starting Rental Studio fictional demo...
echo Changes are temporary and will be discarded when the app closes.
echo No real customers or fleet records. This is not an installer.
start "" /D "%RS_ROOT%" "%RS_ROOT%RentalStudioP2Review.exe"
if errorlevel 1 goto :launch_error
exit /b 0

:verify
if not "%~2"=="" goto :usage
pushd "%RS_ROOT%"
if errorlevel 1 goto :launch_error
"%RS_ROOT%RentalStudioP2Review.exe" --self-test
if errorlevel 1 goto :verify_error
popd
echo Fictional-only packaged native C++ self-test PASS.
exit /b 0

:verify_error
popd
echo ERROR: bundled fictional fixture check failed.
exit /b 2

:missing
echo ERROR: Incomplete Rental Studio package. Extract the ENTIRE ZIP first.
echo Do not replace demo vehicles.txt or rentals.txt with operational files.
exit /b 2

:launch_error
echo ERROR: Windows could not start the packaged Qt executable.
echo Check required official Microsoft Visual C++ runtime and extracted DLLs.
echo Consult START_HERE_FUNCTIONAL_DEMO.md and keep SOURCE_COMMIT.txt.
exit /b 3

:usage
echo Only no arguments or --verify are supported.
exit /b 2
