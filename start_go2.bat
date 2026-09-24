@echo off
cd /d %~dp0
set TRACE=%~dp0launch_trace.txt

echo %date% %time% === launcher start === > "%TRACE%"

echo [1/5] Reset WSL/WSLg (fixes blank-window glitch) ...
wsl --shutdown >> "%TRACE%" 2>&1
timeout /t 8 /nobreak >nul

echo [2/5] Warm up WSL ...
wsl -d Ubuntu-22.04 -- bash -lc "true" >> "%TRACE%" 2>&1
if errorlevel 1 goto wslfail
timeout /t 3 /nobreak >nul

echo [3/5] Starting Go2 GUI (this window stays while the GUI is open) ...
echo step3: launch app >> "%TRACE%"
wsl -d Ubuntu-22.04 -- bash -lc "cd /mnt/d/code/project/Go2ProController/client && stdbuf -o0 ./build/go2_remote 2>&1 | tee /mnt/d/code/project/Go2ProController/last_run.log" >> "%TRACE%" 2>&1
echo step3 exit=%errorlevel% >> "%TRACE%"

echo [4/5] App exited.
echo   - GUI missing?  Check taskbar for msrdc window "Unitree Go2 ..."
echo   - Full app log: last_run.log
echo   - Step trace  : launch_trace.txt
pause
goto done

:wslfail
echo WSL FAILED TO START. Try:  wsl --shutdown   then run this again.
pause

:done
