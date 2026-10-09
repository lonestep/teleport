@echo off
setlocal enabledelayedexpansion

if "%LOG_DIR%"=="" (
    set "LOG_DIR=%TEMP%\teleport_bench_logs"
)

if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"
del /q "%LOG_DIR%\*.*" >nul 2>nul

echo [CLEANUP] Terminating previous teleport processes...
taskkill /F /IM teleport.exe >nul 2>nul

echo [START] Starting 4 Receiver processes...
for /L %%i in (1, 1, 4) do (
    start "" /B teleport.exe mp_listen 20000000 %%i > "%LOG_DIR%\receiver_%%i.log" 2>&1
)

echo [WAIT] Waiting for all 4 Receiver processes to be ready...
for /L %%i in (1, 1, 4) do (
    set /A RETRIES=0
    :check_receiver_loop_%%i
    findstr /C:"ready on topic" "%LOG_DIR%\receiver_%%i.log" >nul 2>nul
    if errorlevel 1 (
        powershell -Command "Start-Sleep -Milliseconds 50" 2>nul || ping 127.0.0.1 -n 1 -w 50 >nul
        set /A RETRIES+=1
        if !RETRIES! geq 100 (
            echo [ERROR] Timeout waiting for Receiver %%i to initialize!
            exit /b 1
        )
        goto check_receiver_loop_%%i
    )
)
echo [READY] All 4 Receiver processes are fully initialized.

echo [START] Starting 12 Sender processes (20,000,000 total messages)...
for /L %%i in (1, 1, 8) do (
    start "" /B teleport.exe mp_send 1666667 %%i 4 > "%LOG_DIR%\sender_%%i.log" 2>&1
)
for /L %%i in (9, 1, 12) do (
    start "" /B teleport.exe mp_send 1666666 %%i 4 > "%LOG_DIR%\sender_%%i.log" 2>&1
)

echo [WAIT] Waiting for 12 Sender processes to complete...
:wait_senders
set "ALL_SENDERS_DONE=1"
for /L %%i in (1, 1, 12) do (
    findstr /C:"complete:" "%LOG_DIR%\sender_%%i.log" >nul 2>nul
    if errorlevel 1 set "ALL_SENDERS_DONE=0"
)
if "!ALL_SENDERS_DONE!"=="0" (
    powershell -Command "Start-Sleep -Milliseconds 200" 2>nul || ping 127.0.0.1 -n 1 -w 200 >nul
    goto wait_senders
)
echo [DONE] All 12 Sender processes completed successfully.

echo [WAIT] Waiting for 4 Receiver processes to complete...
:wait_receivers
set "ALL_RECEIVERS_DONE=1"
for /L %%i in (1, 1, 4) do (
    findstr /C:"Total Messages Received:" "%LOG_DIR%\receiver_%%i.log" >nul 2>nul
    if errorlevel 1 set "ALL_RECEIVERS_DONE=0"
)
if "!ALL_RECEIVERS_DONE!"=="0" (
    powershell -Command "Start-Sleep -Milliseconds 200" 2>nul || ping 127.0.0.1 -n 1 -w 200 >nul
    goto wait_receivers
)
echo [DONE] All 4 Receiver processes completed successfully.
