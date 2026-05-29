# Dongle ESB Receiver

Minimal NCS v3.2.3 Zephyr application that runs on the nRF54H20 **radio core
(cpurad)** as an Enhanced ShockBurst (ESB) **receiver (PRX)**. Every received
packet is printed over SEGGER RTT. RTT logging is configured like the Tempo
firmware development path.

## What it does

- Starts the HF clock (nRF54H20 `CLOCK_CONTROL_NRF2` path).
- Initializes ESB in PRX mode: DPL, 2 Mbps, 16-bit CRC, selective auto-ack.
- Listens on RF channel 70 (pairs with the tempo cpurad PTX) with the shared default addresses.
- Prints each received payload (length, pipe, RSSI, hex dump) and a once-per-
  second `alive: total_rx=...` heartbeat over RTT.

## Build

Open an NCS v3.2.3 toolchain terminal, then run:

```powershell
cd C:\tamno\dongle
west build -p -b nrf54h20dk/nrf54h20/cpurad -d build .
```

From a plain Command Prompt where `west` is not on `PATH`, run the local wrapper
instead:

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

`west flash` also programs the auto-generated UICR/BICR and an empty companion
application-core image; nothing extra is needed.

## View RTT output

SEGGER RTT auto-detect can fail on this target. Use the **manual RTT control
block address** for the current build and terminal/up-buffer 0:

```text
0x23001010
```

This lives in the cpurad RAM region (`0x23000000`), which is different from a
cpuapp image. Recompute it after any change to linker settings, RTT buffer
sizes, memory layout, or board target:

```cmd
C:\ncs\toolchains\fd21892d0f\opt\zephyr-sdk\arm-zephyr-eabi\bin\arm-zephyr-eabi-nm.exe -n build\dongle\zephyr\zephyr.elf | findstr _SEGGER_RTT
```

Expected output once a transmitter is sending:

```text
Dongle ESB receiver starting
HF clock started
ESB PRX ready: 2 Mbps DPL, ch=70, fast-ramp=0
Listening for ESB packets on channel 70...
RX pipe=0 len=8 rssi=-42
payload
                01 00 03 04 05 06 07 08  |........
alive: total_rx=10 (+10/s)
```

`CONFIG_LOG_BACKEND_RTT` and `CONFIG_RTT_CONSOLE` are both enabled, so Zephyr
logs and `printk()` output are visible over RTT.

## Test transmitter

The receiver uses the same default ESB addresses as the NCS samples and the
tempo radio app, so only the **RF channel** has to match the sender.

- **tempo cpurad PTX** (channel 70 — matches this receiver as shipped). Flash the
  tempo firmware on the transmitter board; its cpurad core sends on channel 70.

- **NCS `esb_ptx` sample** (channel 2). To use it instead, change
  `DONGLE_ESB_CHANNEL` in `src/main.c` to `2`, rebuild, then on a second
  nRF54H20 DK:

  ```cmd
  west build -p -b nrf54h20dk/nrf54h20/cpurad -d build_ptx C:\ncs\v3.2.3\nrf\samples\esb\esb_ptx
  west flash -d build_ptx
  ```

  It sends an 8-byte payload every 100 ms with an incrementing counter in
  `data[1]`.

## ESB configuration

| Parameter        | Value                                            |
|------------------|--------------------------------------------------|
| protocol         | `ESB_PROTOCOL_ESB_DPL` (dynamic payload length)  |
| mode             | `ESB_MODE_PRX` (receiver)                         |
| bitrate          | `ESB_BITRATE_2MBPS`                               |
| crc              | `ESB_CRC_16BIT`                                   |
| RF channel       | `70` (`DONGLE_ESB_CHANNEL` in `src/main.c`)       |
| base address 0   | `E7 E7 E7 E7`                                     |
| base address 1   | `C2 C2 C2 C2`                                     |
| address prefixes | `E7 C2 C3 C4 C5 C6 C7 C8`                         |
| max payload      | `CONFIG_ESB_MAX_PAYLOAD_LENGTH=32`                |
