@echo off
chcp 65001 > nul

echo C build
call build_c.bat

echo.
echo Python build
call build_py.bat

echo.
echo Finished!
