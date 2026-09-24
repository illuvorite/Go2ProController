@echo off
REM ============================================================================
REM Build the Android APK (Gradle + NDK + CMake).
REM Log: _build.log (ends with DONE/FAILED)   Output: app\build\outputs\apk\debug\*.apk
REM
REM ASCII ONLY: cmd reads .bat files as ANSI. Chinese comments get mangled and the
REM REM-prefix is lost, so the comment text is executed as commands and the script
REM breaks halfway (this actually happened - keep every .bat here ASCII-only).
REM ============================================================================
cd /d "%~dp0"
set "LOG=%~dp0_build.log"
echo ==== APK build start %DATE% %TIME% ==== > "%LOG%"
call "%~dp0gradlew.bat" --no-daemon assembleDebug >> "%LOG%" 2>&1
if errorlevel 1 (
  echo FAILED rc=%errorlevel% >> "%LOG%"
) else (
  echo DONE %DATE% %TIME% >> "%LOG%"
)
