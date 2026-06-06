# Claude Handoff

This repo is a minimal Nordic Connect SDK / Zephyr application for the Tamno dongle target. It was created as a standalone NCS v3.2.3 project with RTT logging configured to match the existing Tempo firmware development workflow.

It now runs on the nRF54H20 **radio core (`cpurad`)** as an Enhanced ShockBurst (ESB) **receiver (PRX)** that prints every received packet over RTT. The original cpuapp hello-world is preserved in git history (commit `00a5351`).

## Project Snapshot

- Local path: `C:\tamno\dongle`
- Git remote: `https://github.com/Tamno-Ltd/dongle.git`
- Main branch: `main`
- Initial pushed commit: `00a5351 Add dongle hello RTT app`
- Purpose: receive ESB packets on `nrf54h20dk/nrf54h20/cpurad` and make the payloads visible through SEGGER RTT.

## Local SDK Context

The machine has NCS v3.2.3 installed locally:

- SDK root: `C:\ncs\v3.2.3`
- Zephyr base: `C:\ncs\v3.2.3\zephyr`
- Toolchain bundle for v3.2.3: `C:\ncs\toolchains\fd21892d0f`
- Bundled `west.exe`: `C:\ncs\toolchains\fd21892d0f\opt\bin\Scripts\west.exe`

Plain `cmd.exe` and plain PowerShell do not necessarily have `west` on `PATH`. Use the repo wrappers unless the shell was launched from the NCS toolchain environment.

## Build And Flash

Recommended from plain Command Prompt:

```cmd
cd C:\tamno\dongle
build.cmd
flash.cmd
```

Equivalent command if the NCS v3.2.3 environment is already loaded:

```cmd
west build -p -b nrf54h20dk/nrf54h20/cpurad -d build .
west flash -d build
```

The wrapper scripts call `scripts\ncs-env.cmd`, which sets:

- `NCS_ROOT=C:\ncs\v3.2.3`
- `NCS_TOOLCHAIN_DIR=C:\ncs\toolchains\fd21892d0f`
- `ZEPHYR_BASE=%NCS_ROOT%\zephyr`
- `ZEPHYR_TOOLCHAIN_VARIANT=zephyr`
- `ZEPHYR_SDK_INSTALL_DIR=%NCS_TOOLCHAIN_DIR%\opt\zephyr-sdk`
- PATH entries needed for `west`, CMake, Python, nrfutil, and the Zephyr SDK toolchains.

The build has been verified successfully (pristine `cpurad` build, exit 0, `src/main.c` compiles with no warnings). It uses sysbuild and creates at least these build products:

- `build\dongle\zephyr\zephyr.elf` (the cpurad ESB receiver image)
- `build\dongle\zephyr\zephyr.hex`
- `build\uicr\zephyr\uicr.hex`

Sysbuild also auto-adds an `empty_app_core` image (`build\empty_app_core\`) for the application core; it requires no maintenance and is programmed automatically by `west flash`. The only build warning is a generic `LTO ... assigned 'y' but got 'n'` from the NCS/MPSL build — benign.

## App Behavior

Source: `src\main.c`

At boot it starts the HF clock (nRF54H20 `CLOCK_CONTROL_NRF2` path), initializes ESB in PRX mode, and starts RX:

```text
Dongle ESB receiver starting
HF clock started
ESB PRX ready: 2 Mbps DPL, ch=70, fast-ramp=0
Listening for ESB packets on channel 70...
```

For each received packet (in RADIO ISR context, via the ESB event handler) it logs the pipe, length, RSSI, and a hex dump of the payload:

```text
RX pipe=0 len=8 rssi=-42
payload
                01 00 03 04 05 06 07 08  |........
```

Once per second it logs a heartbeat with the running RX count:

```text
alive: total_rx=10 (+10/s)
```

It uses both `LOG_INF()`/`LOG_HEXDUMP_INF()` and `printk()` so RTT logging and RTT console routing are both exercised.

ESB parameters (must match the transmitter): DPL, 2 Mbps, 16-bit CRC, selective auto-ack, RF channel `70` (`DONGLE_ESB_CHANNEL` in `src\main.c`, matching the tempo cpurad PTX), base addresses `E7E7E7E7` / `C2C2C2C2`, prefixes `E7 C2 C3 C4 C5 C6 C7 C8`. These addresses match the NCS `esb_ptx`/`esb_prx` samples and the tempo cpurad PTX. The NCS `esb_ptx` sample transmits on channel `2` — set `DONGLE_ESB_CHANNEL` to `2` to listen to it instead.

## RTT Logging Configuration

Source: `prj.conf`

Relevant settings:

```text
CONFIG_ESB=y
CONFIG_ESB_MAX_PAYLOAD_LENGTH=32
CONFIG_CLOCK_CONTROL=y
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=3
CONFIG_LOG_BACKEND_RTT=y
CONFIG_USE_SEGGER_RTT=y
CONFIG_SEGGER_RTT_BUFFER_SIZE_UP=4096
CONFIG_CONSOLE=y
CONFIG_RTT_CONSOLE=y
CONFIG_UART_CONSOLE=n
CONFIG_PRINTK=y
CONFIG_LOG_PRINTK=y
CONFIG_BOOT_BANNER=n
```

The ESB radio core also needs board-scoped config and devicetree under `boards\`:

- `boards\nrf54h20dk_nrf54h20_cpurad.conf`: `CONFIG_NRFX_GPPI_V1=n` (use the GPPI v0 path on nRF54H20, matching the NCS `esb_*` samples and tempo).
- `boards\nrf54h20dk_nrf54h20_cpurad.overlay`: adds `errata216_mboxes` (HMPAN-216, required by ESB init on nRF54H20), plus `gpiote130` and `dppic020` (radio event routing, from the NCS `esb_prx` sample overlay).

This mirrors Tempo's RTT development path:

- `CONFIG_LOG_BACKEND_RTT=y`
- `CONFIG_USE_SEGGER_RTT=y`
- `CONFIG_SEGGER_RTT_BUFFER_SIZE_UP=4096`

`CONFIG_RTT_CONSOLE=y` was added here so `printk()` also appears over RTT.

## Manual RTT Control Block Address

SEGGER RTT auto-detect may fail on this target. For the current verified `cpurad` build, the RTT control block address is:

```text
0x23001010
```

This came from `build\dongle\zephyr\zephyr.map`:

```text
0x0000000023000000 __rtt_buff_data_start = .
0x0000000023001010 _SEGGER_RTT
```

Note this is in the cpurad RAM region (`0x23000000`) and differs from a cpuapp image (the original hello build had `0x2F001010`). Recompute it whenever the build's core target or memory layout changes.

In SEGGER RTT Viewer or RTT Client:

- Use manual RTT control block address: `0x23001010`
- Use terminal/up-buffer 0.

Do not treat this as permanent if linker settings, RTT buffer sizes, memory regions, or board target change. Recompute it after relevant build changes:

```cmd
C:\ncs\toolchains\fd21892d0f\opt\zephyr-sdk\arm-zephyr-eabi\bin\arm-zephyr-eabi-nm.exe -n build\dongle\zephyr\zephyr.elf | findstr _SEGGER_RTT
```

Or from the map:

```cmd
rg "_SEGGER_RTT|__rtt_buff_data_start" build\dongle\zephyr\zephyr.map
```

## Relationship To Tempo Firmware

Tempo lives at `C:\tamno\tempo`. It is an existing NCS v3.2.3 firmware project for nRF54H20 and was used as the reference for RTT logging setup.

Tempo context that matters here:

- Tempo pins NCS with a `west.yml` revision of `v3.2.3`.
- Tempo `CMakeLists.txt` checks for NCS 3.2.3 or newer patch version.
- Tempo uses `CONFIG_LOG_BACKEND_RTT=y`, `CONFIG_USE_SEGGER_RTT=y`, and `CONFIG_SEGGER_RTT_BUFFER_SIZE_UP=4096`.

The dongle app intentionally follows those patterns but stays minimal.

## Repo File Map

- `CMakeLists.txt`: Zephyr app entry point, NCS v3.2.3 guard, adds `src/main.c`.
- `prj.conf`: ESB, clock, RTT logging, and console configuration.
- `src/main.c`: HF clock start, ESB PRX init, RX event handler that prints payloads over RTT.
- `boards/nrf54h20dk_nrf54h20_cpurad.conf`: board-scoped ESB config (`CONFIG_NRFX_GPPI_V1=n`).
- `boards/nrf54h20dk_nrf54h20_cpurad.overlay`: cpurad devicetree for ESB (errata216 mboxes, gpiote130, dppic020).
- `west.yml`: local manifest pinned to `sdk-nrf` `v3.2.3`.
- `build.cmd`: plain-cmd build wrapper.
- `flash.cmd`: plain-cmd flash wrapper.
- `scripts\ncs-env.cmd`: local NCS v3.2.3 environment setup.
- `README.md`: user-facing build, flash, and RTT instructions.
- `.gitignore`: ignores build outputs, local west metadata, editor files, `.omc/`, and leaked binary artifacts.

## Git And Hygiene

Generated build output is intentionally ignored. Do not commit:

- `build\`
- `.west\`
- `.omc\`
- generated `.elf`, `.hex`, `.bin`, `.map`, `.o`, `.a`

The repo was pushed to GitHub and `main` tracks `origin/main`.

Before changing firmware behavior, run:

```cmd
git status --short
build.cmd
```

After changing RTT config or memory layout, re-check the manual RTT control block address.

## Known Assumptions

- The target board is `nrf54h20dk/nrf54h20/cpurad` (the radio core, where the RADIO peripheral and ESB live).
- Build was verified locally; flashing and live RTT output were not verified by Codex because that requires the physical board/debug connection.
- If using an NCS toolchain install with a different bundle ID, set `NCS_TOOLCHAIN_DIR` before running `build.cmd` or edit `scripts\ncs-env.cmd`.
