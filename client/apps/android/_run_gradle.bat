@echo off
REM ============================================================================
REM Foreground Gradle build (for automation).
REM ASCII ONLY: cmd reads .bat as ANSI, and Chinese comments break the REM prefix
REM so the comment text gets executed as garbage commands (that was a real bug).
REM Unlike build_apk.bat this keeps the Gradle daemon alive, so the build survives
REM even if the parent console process is reaped.
REM ============================================================================
cd /d "%~dp0"
echo ==== APK build start %DATE% %TIME% ==== > _build.log
call "%~dp0gradlew.bat" assembleDebug >> _build.log 2>&1
if errorlevel 1 (
  echo FAILED rc=%errorlevel% >> _build.log
) else (
  echo DONE %DATE% %TIME% >> _build.log
)
powershell -NoProfile -Command "Get-Content '%~dp0_build.log' -Tail 25"
