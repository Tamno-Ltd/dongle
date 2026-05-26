@echo off
setlocal

if not defined NCS_TOOLCHAIN_DIR set "NCS_TOOLCHAIN_DIR=C:\ncs\toolchains\fd21892d0f"
if not defined NCS_ROOT set "NCS_ROOT=C:\ncs\v3.2.3"

if not exist "%NCS_TOOLCHAIN_DIR%\opt\bin\Scripts\west.exe" (
	echo west.exe was not found under "%NCS_TOOLCHAIN_DIR%".
	echo Set NCS_TOOLCHAIN_DIR to your NCS v3.2.3 toolchain directory and try again.
	exit /b 1
)

if not exist "%NCS_ROOT%\zephyr" (
	echo Zephyr was not found under "%NCS_ROOT%\zephyr".
	echo Set NCS_ROOT to your NCS v3.2.3 SDK directory and try again.
	exit /b 1
)

endlocal ^
	& set "NCS_TOOLCHAIN_DIR=%NCS_TOOLCHAIN_DIR%" ^
	& set "NCS_ROOT=%NCS_ROOT%" ^
	& set "PATH=%NCS_TOOLCHAIN_DIR%;%NCS_TOOLCHAIN_DIR%\mingw64\bin;%NCS_TOOLCHAIN_DIR%\bin;%NCS_TOOLCHAIN_DIR%\opt\bin;%NCS_TOOLCHAIN_DIR%\opt\bin\Scripts;%NCS_TOOLCHAIN_DIR%\opt\nanopb\generator-bin;%NCS_TOOLCHAIN_DIR%\nrfutil\bin;%NCS_TOOLCHAIN_DIR%\opt\zephyr-sdk\arm-zephyr-eabi\bin;%NCS_TOOLCHAIN_DIR%\opt\zephyr-sdk\riscv64-zephyr-elf\bin;%PATH%" ^
	& set "PYTHONPATH=%NCS_TOOLCHAIN_DIR%\opt\bin;%NCS_TOOLCHAIN_DIR%\opt\bin\Lib;%NCS_TOOLCHAIN_DIR%\opt\bin\Lib\site-packages" ^
	& set "NRFUTIL_HOME=%NCS_TOOLCHAIN_DIR%\nrfutil\home" ^
	& set "ZEPHYR_TOOLCHAIN_VARIANT=zephyr" ^
	& set "ZEPHYR_SDK_INSTALL_DIR=%NCS_TOOLCHAIN_DIR%\opt\zephyr-sdk" ^
	& set "ZEPHYR_BASE=%NCS_ROOT%\zephyr"
