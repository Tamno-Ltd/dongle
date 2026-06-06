@echo off
setlocal

call "%~dp0scripts\ncs-env.cmd"
if errorlevel 1 exit /b %errorlevel%

pushd "%~dp0"
west build -p -b nrf54h20dk/nrf54h20/cpurad -d build .
set "BUILD_RESULT=%errorlevel%"
popd

exit /b %BUILD_RESULT%
