@echo off
chcp 65001 > nul

echo copying source code
copy "..\python_asyncio.py" "." > nul

echo run pyinstaller
echo.
pyinstaller -F python_asyncio.py

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Build failed!
    exit /b
)

if not exist "..\build" (
    mkdir "..\build"
)

echo.
echo moving exe to ..\build
if exist "dist\python_asyncio.exe" (
    move /y "dist\python_asyncio.exe" "..\build\" > nul
) else (
    echo [ERROR] Final exe not found in dist!
    exit /b
)

echo cleaning up
del /f /q "python_asyncio.py" > nul 2>&1
del /f /q "python_asyncio.spec" > nul 2>&1
rmdir /s /q "build" > nul 2>&1
rmdir /s /q "dist" > nul 2>&1

echo.
echo SUCCESS!
