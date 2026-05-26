# Dongle Hello

Minimal NCS v3.2.3 Zephyr application with RTT logging configured like the Tempo firmware.

## Build

Open an NCS v3.2.3 toolchain terminal, then run:

```powershell
cd C:\tamno\dongle
west build -p -b nrf54h20dk/nrf54h20/cpuapp -d build .
```

From a plain Command Prompt where `west` is not on `PATH`, run the local wrapper instead:

```cmd
cd C:\tamno\dongle
build.cmd
```

## Flash

```powershell
west flash -d build
```

Or from a plain Command Prompt:

```cmd
flash.cmd
```

## View RTT output

Open SEGGER RTT Viewer or RTT Client and connect to the board. Use auto-detect for the RTT control block and terminal/up-buffer 0.

Expected output:

```text
Hello world from dongle over RTT
Hello world from dongle via printk/RTT
dongle heartbeat
```

The app enables both `CONFIG_LOG_BACKEND_RTT` and `CONFIG_RTT_CONSOLE`, so Zephyr logs and `printk()` output are visible over RTT.
