@echo off
chcp 65001 > nul

echo copying source code
copy "..\c_windows_pthread.c" "." > nul

echo running gcc compiler
echo.
gcc -Wall -Wextra c_windows_pthread.c -o c_windows_pthread.exe -lws2_32 -lpthread

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Compilation failed!
    exit /b
)

if not exist "..\build" (
    mkdir "..\build"
)

echo.
echo moving exe to ..\build
if exist "c_windows_pthread.exe" (
    move /y "c_windows_pthread.exe" "..\build\" > nul
) else (
    echo [ERROR] Executable file not found!
    exit /b
)

echo cleaning up
del /f /q "c_windows_pthread.c" > nul 2>&1

echo.
echo SUCCESS!
