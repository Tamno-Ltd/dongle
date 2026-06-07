# Claude Handoff

This repo is a Nordic Connect SDK / Zephyr application for the Tamno dongle
(nRF54H20 DK target). It is a **wireless ESB → USB HID mouse**: it receives
mouse-movement packets over Enhanced ShockBurst and replays them to the host PC
as USB HID input reports, so the dongle enumerates in Windows Device Manager as
a HID-compliant mouse and moves the cursor.

```
remote PTX  --ESB 4 Mbps-->  cpurad (PRX)  --ICBMSG IPC-->  cpuapp (USB HID)  --USB-->  PC
```

History: started as a cpuapp hello-world (commit `00a5351`), became a standalone
cpurad ESB PRX receiver, then was restructured into the current two-core sysbuild
(cpuapp USB HID + cpurad ESB) to add the USB-mouse output path. The USB HID code
is adapted from the tempo firmware (`C:\tamno\tempo\app\usb\`).

## Architecture (why two cores)

On the nRF54H20 the RADIO/ESB peripheral is on the **radio core (cpurad)** and
the USB high-speed controller (`usbhs`) is on the **application core (cpuapp)** —
it is hard-disabled on cpurad in the board DTS. A single-core image cannot do
both, so this is a **sysbuild with two images**:

- **cpuapp** = root app. Brings up USB HID, receives 7-byte mouse reports from
  cpurad over the `cpuapp_cpurad_ipc` ICBMSG link, submits them to the host.
  Owns SEGGER RTT logging.
- **cpurad** = `radio/` image. ESB PRX receiver; forwards each payload to cpuapp
  over IPC. Logs over its own SEGGER RTT channel (separate control block from
  cpuapp; no UART console).

cpuapp boots cpurad at runtime via `CONFIG_SOC_NRF54H20_CPURAD_ENABLE=y`. The
ICBMSG instance + bellboards come from the DK base DTS (no overlay needed). The
structure mirrors the NCS `spis_wakeup` sample and the tempo firmware. **No
MCUboot** (receiver-only; deliberately omitted).

## Project Snapshot

- Local path: `C:\tamno\dongle`
- Git remote: `https://github.com/Tamno-Ltd/dongle.git`
- Main branch: `main`
- Purpose: receive ESB mouse packets on cpurad, replay them as a USB HID mouse
  from cpuapp.

## Local SDK Context

NCS v3.2.3 installed locally:

- SDK root: `C:\ncs\v3.2.3`
- Zephyr base: `C:\ncs\v3.2.3\zephyr`
- Toolchain bundle: `C:\ncs\toolchains\fd21892d0f`
- Bundled `west.exe`: `C:\ncs\toolchains\fd21892d0f\opt\bin\Scripts\west.exe`

Plain `cmd.exe`/PowerShell do not necessarily have `west` on `PATH`. Use the repo
wrappers unless the shell was launched from the NCS toolchain environment.

## Build And Flash

Recommended from plain Command Prompt:

```cmd
cd C:\tamno\dongle
build.cmd
flash.cmd
```

Equivalent if the NCS v3.2.3 environment is already loaded:

```cmd
west build -p -b nrf54h20dk/nrf54h20/cpuapp --sysbuild -d build .
west flash -d build
```

> Build wrapper note: invoking `build.cmd` through git-bash needs MSYS arg
> conversion disabled, e.g. `MSYS2_ARG_CONV_EXCL='*' cmd.exe /c 'C:\tamno\dongle\build.cmd'`
> (otherwise git-bash rewrites `/c` to `C:\` and cmd just prints its banner).
> From a real Command Prompt this is not an issue.

Build verified locally (pristine sysbuild, exit 0). Sysbuild domains / products:

- `build\dongle\zephyr\zephyr.hex` — cpuapp USB HID image (+ `bicr.hex`)
- `build\remote_rad\zephyr\zephyr.hex` — cpurad ESB PRX image
- `build\uicr\zephyr\uicr.hex` (+ `periphconf.hex`)

`west flash` programs all domains. The only build warning is the benign
`Experimental symbol ESB_FAST_SWITCHING is enabled`. (Note: the old single-core
build auto-added an `empty_app_core` image; the explicit `remote_rad` replaces
it, so there is no `empty_app_core` anymore.)

## App Behavior

### cpuapp — `src/main.c` (+ `usb/usb_hid.c`)

At boot: `dongle_usb_init()` (registers the HID report descriptor + VID/PID/
strings on the new `usbd` stack), `dongle_usb_enable()`, then opens the
`cpuapp_cpurad_ipc` ICBMSG endpoint `"dongle_mouse"`. Each received IPC message
is decoded into `(dx, dy, buttons, wheel)` and submitted with
`dongle_usb_submit_mouse_report()` (gated by `hid_ready`, so packets before host
enumeration return `-EACCES` and are counted as dropped). Once per second logs
`alive: hid_tx=N (+N/s) dropped=M` over RTT.

```text
Dongle USB HID mouse starting (cpuapp)
USB HID mouse initialized (16-button, 16-bit X/Y)
USB device enabled
HID interface ready
radio IPC endpoint bound
alive: hid_tx=120 (+120/s) dropped=0
```

VID `0x2fe3` / PID `0x0100`, manufacturer "Tamno", product "Tamno Dongle Mouse"
(in `usb/usb_hid.c`). HID report descriptor: 16 buttons + 16-bit relative X/Y +
8-bit wheel → **7-byte** report. FS and HS configs both registered.

### cpurad — `radio/src/main.c`

Starts the HF clock (`CLOCK_CONTROL_NRF2`), inits ESB in PRX mode, registers the
`"dongle_mouse"` IPC endpoint, starts RX. The ESB event handler runs in RADIO
ISR context, so it only `k_msgq_put`s each payload; a dedicated `ipc_tx_thread`
waits for the cpuapp bind, then drains the queue and `ipc_service_send`s each
report. Logs over its own SEGGER RTT control block (separate from cpuapp's;
no UART console).

## Report / On-air Format (canonical, 7 bytes)

Used end to end (ESB payload → IPC → USB HID report). Defined in
`include/dongle_ipc.h`; encoded/decoded in `usb/usb_hid_report.h`.

| Bytes | Field   | Type        |
|-------|---------|-------------|
| 0-1   | buttons | `uint16` LE |
| 2-3   | X delta | `int16`  LE |
| 4-5   | Y delta | `int16`  LE |
| 6     | wheel   | `int8`      |

The IPC message wraps these 7 bytes: `struct dongle_ipc_mouse_report { uint8_t
type (0x20); uint8_t len (7); uint8_t report[7]; }` (9 bytes, well under the
32-byte ICBMSG block). **The remote transmitter must send exactly the 7-byte
report layout** on channel 70 with the shared addresses.

## Key Config

cpuapp `prj.conf`:

```text
CONFIG_USB_DEVICE_STACK_NEXT=y       # new usbd stack (required for USBHS)
CONFIG_UDC_DWC2_DMA=n
CONFIG_UDC_DWC2_USBHS_VBUS_READY_TIMEOUT=100
CONFIG_UDC_BUF_POOL_SIZE=8192        # Windows MS-OS descriptor probe (wLength=4095)
CONFIG_IPC_SERVICE=y                 # ICBMSG auto-selects (default y, needs MBOX + DT node)
CONFIG_MBOX=y
CONFIG_SOC_NRF54H20_CPURAD_ENABLE=y  # boot cpurad so the IPC binds
CONFIG_LOG_BACKEND_RTT=y / CONFIG_USE_SEGGER_RTT=y / CONFIG_RTT_CONSOLE=y
```

cpuapp DT: `boards/nrf54h20dk_nrf54h20_cpuapp.overlay` adds the
`zephyr,hid-device` node (`in-report-size = <7>`). `zephyr_udc0` is already
`okay` on cpuapp from the base DTS; no `chosen { zephyr,usbd }` is needed.

cpurad `radio/prj.conf`: `CONFIG_ESB=y`, `CONFIG_ESB_FAST_SWITCHING=y`,
`CONFIG_IPC_SERVICE=y` + `CONFIG_IPC_SERVICE_BACKEND_ICBMSG=y` + `CONFIG_MBOX=y`,
plus its own RTT logging (`CONFIG_LOG_BACKEND_RTT=y`, `CONFIG_SERIAL=n`). Board:
`radio/boards/
nrf54h20dk_nrf54h20_cpurad.conf` (`CONFIG_NRFX_GPPI_V1=n`) and `.overlay`
(errata216 mboxes + dppic020, uart135/136 disabled; **no** gpiote130 claim — the
ESB subsystem needs no GPIOTE and claiming it on cpurad in a sysbuild risks a
PERIPHCONF conflict). **No `CONFIG_ROM_START_OFFSET`** — that was a tempo MCUboot
artifact and is not needed here (verified against the NCS `spis_wakeup` sample).

## Manual RTT Control Block Address

Both cores log over their own SEGGER RTT control block. For the current verified
build:

| Core   | Image        | RAM region    | Control block |
|--------|--------------|---------------|---------------|
| cpuapp | `dongle`     | `0x2F000000`  | `0x2F001010`  |
| cpurad | `remote_rad` | `0x23000000`  | `0x23001010`  |

In SEGGER RTT Viewer/Client: pick the address for the core you want, terminal/
up-buffer 0. Recompute after any linker/RTT/memory/board-target change:

```cmd
C:\ncs\toolchains\fd21892d0f\opt\zephyr-sdk\arm-zephyr-eabi\bin\arm-zephyr-eabi-nm.exe -n build\dongle\zephyr\zephyr.elf | findstr _SEGGER_RTT
C:\ncs\toolchains\fd21892d0f\opt\zephyr-sdk\arm-zephyr-eabi\bin\arm-zephyr-eabi-nm.exe -n build\remote_rad\zephyr\zephyr.elf | findstr _SEGGER_RTT
```

## Repo File Map

- `sysbuild.cmake`: adds the `remote_rad` cpurad image (guarded by `SB_CONFIG_SOC_NRF54H20`).
- `CMakeLists.txt` / `prj.conf`: cpuapp root app (USB HID + IPC).
- `src/main.c`: cpuapp entry — USB init/enable + IPC receive → HID submit.
- `usb/usb_hid.c`, `usb/usb_hid.h`, `usb/usb_hid_report.h`: USB HID mouse (new usbd stack), adapted from tempo.
- `include/dongle_ipc.h`: shared cpuapp↔cpurad IPC contract (endpoint name, message struct, 7-byte layout).
- `boards/nrf54h20dk_nrf54h20_cpuapp.overlay`: HID device node.
- `radio/CMakeLists.txt`, `radio/prj.conf`: cpurad ESB PRX image.
- `radio/src/main.c`: ESB PRX + ISR→msgq→IPC forwarding.
- `radio/boards/nrf54h20dk_nrf54h20_cpurad.{conf,overlay}`: cpurad board config + devicetree.
- `west.yml`: local manifest pinned to `sdk-nrf` `v3.2.3`.
- `build.cmd` / `flash.cmd` / `scripts\ncs-env.cmd`: local NCS v3.2.3 wrappers.
- `.github/workflows/release.yml`: tagged-build release (cpuapp+cpurad sysbuild).
- `README.md`: user-facing build/flash/RTT/format docs.

## Relationship To Tempo Firmware

Tempo lives at `C:\tamno\tempo` (NCS v3.2.3, nRF54H20). It is the reference for:

- The USB HID module (`app/usb/usb_hid.c` etc. — lifted and renamed `tempo_`→`dongle_`).
- The cpuapp↔cpurad ICBMSG IPC pattern (`src/main.c`, `app/ipc/ipc_radio.h`).
- RTT logging setup.

Tempo's data flow is the **mirror** of the dongle's: tempo reads a sensor on
cpuapp and transmits over ESB from cpurad (PTX); the dongle receives over ESB on
cpurad (PRX) and outputs USB HID from cpuapp. Tempo also pins NCS to `v3.2.3` and
guards the NCS version in `CMakeLists.txt`, which this repo follows. Tempo uses
MCUboot; the dongle deliberately does not.

## Git And Hygiene

Do not commit generated output: `build*\`, `.west\`, `.omc\`, and stray
`.elf/.hex/.bin/.map/.o/.a`. The `.gitignore` covers these. `main` tracks
`origin/main`.

Before changing firmware behavior:

```cmd
git status --short
build.cmd
```

After changing RTT config or memory layout, re-check the manual RTT control block
address (cpuapp image).

## Known Assumptions

- Board target is now `nrf54h20dk/nrf54h20/cpuapp` **with `--sysbuild`** (cpuapp
  root + cpurad `remote_rad`). The old standalone `cpurad` target is gone.
- This repo is **receiver only**. A matching ESB transmitter (PTX) that sends the
  7-byte report layout on channel 70 is separate work. The tempo cpurad PTX sends
  zeroed deltas today (proves the link, won't move the cursor).
- Build was verified locally; flashing, USB enumeration, and live cursor movement
  require the physical board + a real transmitter and were not hardware-verified.
- If using an NCS toolchain bundle with a different ID, set `NCS_TOOLCHAIN_DIR`
  before `build.cmd` or edit `scripts\ncs-env.cmd`.
