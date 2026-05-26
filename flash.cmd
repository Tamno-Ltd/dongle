@echo off
setlocal

call "%~dp0scripts\ncs-env.cmd"
if errorlevel 1 exit /b %errorlevel%

pushd "%~dp0"
west flash -d build
set "FLASH_RESULT=%errorlevel%"
popd

exit /b %FLASH_RESULT%
