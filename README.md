# Dongle — Wireless ESB → USB HID Mouse

NCS v3.2.3 Zephyr application for the nRF54H20 DK that turns the board into a
USB **HID mouse**: it receives mouse-movement packets over Enhanced ShockBurst
(ESB) and replays them to the connected PC as USB HID input reports, so the host
cursor moves. It enumerates in Windows Device Manager as a HID-compliant mouse.

```
remote PTX  --ESB 4 Mbps-->  cpurad (PRX)  --ICBMSG IPC-->  cpuapp (USB HID)  --USB-->  PC
```

## Why two cores

On the nRF54H20 the RADIO/ESB peripheral lives on the **radio core (cpurad)**
while the USB high-speed controller lives on the **application core (cpuapp)**.
A single-core image cannot do both, so this is a **sysbuild with two images**:

- **cpuapp** (root app, `src/` + `usb/`): brings up USB HID, receives 7-byte
  mouse reports from cpurad over the `cpuapp_cpurad_ipc` ICBMSG link, and submits
  them to the host. Owns SEGGER RTT logging.
- **cpurad** (`radio/`): the ESB PRX receiver. Forwards each received payload to
  cpuapp over IPC. Logs over its own SEGGER RTT channel (a separate control
  block from cpuapp; no UART console).

cpuapp boots cpurad at runtime via `CONFIG_SOC_NRF54H20_CPURAD_ENABLE=y`.
The structure mirrors the NCS `spis_wakeup` sample and the tempo firmware.

## On-air / report format

One canonical **7-byte layout** is used end to end (ESB payload → IPC → USB HID
report). The remote transmitter must send exactly these bytes:

| Bytes | Field   | Type                | Notes              |
|-------|---------|---------------------|--------------------|
| 0-1   | buttons | `uint16` LE         | bit 0 = button 1   |
| 2-3   | X delta | `int16`  LE         | relative           |
| 4-5   | Y delta | `int16`  LE         | relative           |
| 6     | wheel   | `int8`              | relative           |

Defined in `include/dongle_ipc.h` and encoded/decoded by `usb/usb_hid_report.h`.

## Build

Open an NCS v3.2.3 toolchain terminal, then run:

```powershell
cd C:\tamno\dongle
west build -p -b nrf54h20dk/nrf54h20/cpuapp --sysbuild -d build .
```

From a plain Command Prompt where `west` is not on `PATH`, use the wrapper:

```cmd
cd C:\tamno\dongle
build.cmd
```

Build products (sysbuild domains):

- `build\dongle\zephyr\zephyr.hex` — cpuapp USB HID image
- `build\remote_rad\zephyr\zephyr.hex` — cpurad ESB PRX image
- `build\uicr\zephyr\uicr.hex` — generated UICR/periphconf

The only build warning is the benign `Experimental symbol ESB_FAST_SWITCHING is
enabled`.

## Flash

```powershell
west flash -d build
```

Or from a plain Command Prompt:

```cmd
flash.cmd
```

`west flash` programs all sysbuild domains (cpuapp app, cpurad radio image, and
the generated UICR/BICR). Plug the DK's USB into the host PC to enumerate the
mouse.

## View RTT output

Both cores log over their own SEGGER RTT control block. Auto-detect can fail on
this target; use the **manual control block address** for the core you want and
terminal/up-buffer 0:

| Core   | Image            | RTT control block |
|--------|------------------|-------------------|
| cpuapp | `dongle`         | `0x2F001010`      |
| cpurad | `remote_rad`     | `0x23001010`      |

Recompute after any change to linker settings, RTT buffer sizes, memory layout,
or board target:

```cmd
C:\ncs\toolchains\fd21892d0f\opt\zephyr-sdk\arm-zephyr-eabi\bin\arm-zephyr-eabi-nm.exe -n build\dongle\zephyr\zephyr.elf | findstr _SEGGER_RTT
C:\ncs\toolchains\fd21892d0f\opt\zephyr-sdk\arm-zephyr-eabi\bin\arm-zephyr-eabi-nm.exe -n build\remote_rad\zephyr\zephyr.elf | findstr _SEGGER_RTT
```

Expected cpuapp (`0x2F001010`) output once plugged in and a transmitter is
sending:

```text
Dongle USB HID mouse starting (cpuapp)
USB HID mouse initialized (16-button, 16-bit X/Y)
USB device enabled
HID interface ready
radio IPC endpoint bound
alive: hid_tx=120 (+120/s) dropped=0
```

Expected cpurad (`0x23001010`) output:

```text
Dongle ESB receiver starting (cpurad)
HF clock started
ESB PRX ready: 4 Mbps DPL, ch=70, fast-ramp=1
cpuapp IPC endpoint bound
Listening for ESB mouse packets on channel 70...
alive: rx=120 (+120/s) drops=0
```

## Transmitter (separate work)

This repo is **receiver only**. A matching ESB **transmitter (PTX)** must send
the 7-byte report layout above on the same RF channel and addresses. The
receiver listens on **channel 70** with the shared default ESB addresses (same
as the NCS samples and the tempo cpurad PTX); set `DONGLE_ESB_CHANNEL` in
`radio/src/main.c` to `2` to listen to the NCS `esb_ptx` sample instead.

> Note: the tempo cpurad PTX currently transmits placeholder (zeroed) deltas, so
> it proves the link but won't move the cursor. A real transmitter must populate
> the 7-byte payload from actual mouse motion.

## ESB configuration (`radio/src/main.c`)

| Parameter        | Value                                            |
|------------------|--------------------------------------------------|
| protocol         | `ESB_PROTOCOL_ESB_DPL` (dynamic payload length)  |
| mode             | `ESB_MODE_PRX` (receiver)                         |
| bitrate          | `ESB_BITRATE_4MBPS`                              |
| crc              | `ESB_CRC_16BIT`                                   |
| fast ramp-up     | `CONFIG_ESB_FAST_SWITCHING=y` (40 us ramp)       |
| RF channel       | `70` (`DONGLE_ESB_CHANNEL`)                       |
| base address 0   | `E7 E7 E7 E7`                                     |
| base address 1   | `C2 C2 C2 C2`                                     |
| address prefixes | `E7 C2 C3 C4 C5 C6 C7 C8`                         |
| max payload      | `CONFIG_ESB_MAX_PAYLOAD_LENGTH=32`               |

## File map

| Path | Role |
|------|------|
| `sysbuild.cmake` | adds the `remote_rad` cpurad image |
| `CMakeLists.txt`, `prj.conf` | cpuapp root app (USB HID + IPC) |
| `src/main.c` | cpuapp: USB init + IPC receive → HID submit |
| `usb/usb_hid.c`, `usb/usb_hid.h`, `usb/usb_hid_report.h` | USB HID mouse (new usbd stack) |
| `include/dongle_ipc.h` | shared cpuapp↔cpurad IPC contract |
| `boards/nrf54h20dk_nrf54h20_cpuapp.overlay` | HID device node |
| `radio/` | cpurad ESB PRX image (CMake, prj.conf, src, boards) |
| `build.cmd`, `flash.cmd`, `scripts/ncs-env.cmd` | local NCS v3.2.3 wrappers |

## Releases

Pushing a `v*` tag triggers the **Release** GitHub Action
(`.github/workflows/release.yml`): it builds the cpuapp+cpurad sysbuild, guards
against forbidden Bluetooth symbols on both cores, and publishes a
`dongle-<tag>-firmware.zip` containing the flashable hexes (cpuapp `dongle` app +
BICR, cpurad `remote_rad` image, and the generated `uicr`).

```bash
git tag -a v0.2.0 -m "v0.2.0 — ESB to USB HID mouse"
git push origin v0.2.0
```
