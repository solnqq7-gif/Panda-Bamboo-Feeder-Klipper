# Panda Bamboo Feeder – Klipper buffer firmware

Unofficial replacement firmware for the BIQU **Panda Bamboo Feeder** (PBF). It turns the feeder
into a **filament buffer and load/unload assistant for single-extruder Klipper printers**. It
does not use the stock 5-way hub: the feeder outlet goes straight to the printer through a PTFE
tube.

For Traditional Chinese, see [`繁中README.md`](繁中README.md). The detailed guides are in
Traditional Chinese.

> **Not affiliated with BIQU / BigTreeTech.** Flashing replaces the stock firmware. Back up the
> whole flash first (see the installation guide) and use this at your own risk.

## What it does

- **Buffer**: when the printer pulls the buffer carriage home, the feeder tops it up and parks
  it with some slack. The screen shows each top-up as an arrow filling up beside `READY`.
- **Load**: pushes the filament to the printer and stops when a Klipper
  `filament_switch_sensor` sees it. The sensor is picked from the printer's object list or chosen
  in the settings. The first load from the drive gear measures the path length.
- **Unload help**: an unload G-code can send `M117 PBF:FREE` (release the gear during tip
  forming) and `M117 PBF:FOLLOW` (follow the printer's extruder both ways). Without markers, a
  long retraction while not printing is treated as an unload.
- **Runout**: when the tail leaves the feeder, it stops and leaves the rest to the printer.
- **Manual refill during a print**: insert new filament after a runout. The feeder grabs and
  preloads it, then pushes it down the tube until it butts against the old tail. For 3 minutes
  after a preload, the jam check allows the whole path length for this. When the joint nears the
  extruder, the feeder keeps pressure on the new filament until the extruder takes it in.
- **Jam detection**: if the buffer never refills, it can send `PAUSE` and a Mobileraker
  push notification.
- **No-feed alarm**: while printing, if the printer extrudes 100 mm (configurable) and the buffer
  never empties, the filament is not coming through the feeder (extruder grinding or clogged, new
  filament not taken in). It pauses the print and sends a push notification. It is measured in
  printer extrusion, not time.
- **Standby**: releases the gear (no holding current) while the printer is idle and cold, and
  grips again as soon as it heats.
- **Setup tests**: buffer travel, plus the lowest run and holding currents that work.
- **Web page** (Traditional Chinese) with live status, log, settings and firmware upload. There
  is also a JSON API, OTA updates, and an OLED status screen (English).
- Talks to the printer through **Moonraker** over WiFi. An API key is optional, for Moonraker
  setups that do not trust the feeder's address.

## Status

Version 0.11.6. Developed and used on a **Creality K1 Max**. Other Klipper printers should work
but have not been tested yet. Firmware binaries will be published under **Releases** once
the current version has been verified on the hardware.

## Requirements

- BIQU Panda Bamboo Feeder with its 24 V power supply
- A Klipper printer with Moonraker, on the same 2.4 GHz network
- Recommended: a `filament_switch_sensor` near the extruder, plus `[pause_resume]` and
  `[display_status]` (Mainsail and Fluidd configs include both)

The full checklist, tools and flashing steps are in
[`docs/安裝說明.md`](docs/安裝說明.md).

## Documentation

| File | Content |
|---|---|
| [`docs/安裝說明.md`](docs/安裝說明.md) | Installation: tools, backup, flashing, printer checklist, troubleshooting |
| [`docs/使用說明.md`](docs/使用說明.md) | User guide: keys, screen, states, web page, settings |
| [`docs/tip-forming-generic.md`](docs/tip-forming-generic.md) | Unload/load G-code templates for any Klipper printer (how to use `M117 PBF:…`) |
| [`docs/tip-forming.md`](docs/tip-forming.md) | The same, as tested on a Creality K1 Max |

## Building from source

Needs [PlatformIO](https://platformio.org/):

```bash
cd firmware/pbf-custom
pio run -e pbf_custom
```

Output files: `.pio/build/pbf_custom/` (`bootloader.bin`, `partitions.bin`, `firmware.bin`).
`boot_app0.bin` comes with the Arduino-ESP32 framework package. Upload over USB with
`pio run -e pbf_custom -t upload`. A feeder that already runs this firmware can also be updated
from its web page.

Host regression checks (Python 3 and a C++17 `g++`, or set `CXX` to your compiler):

```bash
python tests/run_regressions.py
```

These exercise the production control functions with simulated printer and hardware inputs;
they do not connect to a printer. Version 0.11.6 relaxes the buffer once the printer's nozzle
target drops to 0: it pulls the parked slack back until the carriage is home (never further,
at most the parking amount + 6 mm), so the printer's tube fitting is not pulled on through the
end-of-print moves and the idle time; the next top-up after heating refills it. It builds and
passes 193 host checks. Flashed by OTA: settings were kept, and at boot (printer idle, carriage
out) it relaxed 2.8 mm and stopped with the carriage home. The end-of-print relax has not been
seen on the hardware yet.
Version 0.11.5 caps a full unload (double BACK) at the
measured unload length + 20 mm instead of x1.3, and always finishes the last 15 mm once the
tip has cleared the gear. It builds and passes 170 host checks; OTA startup and preserved settings have been verified.
Version 0.11.4 lets double UP return to STAGED during manual feed, retract, and printer-follow
moves: the first tap stops the move and also counts toward the double click. A lone stop tap
cannot start a delayed load. Following stays paused until the current printer move ends. OTA
startup and preserved settings have been verified. Physical button verification is still
pending.
Version 0.11.3 fixes test cleanup during a print, menu access during a print, stale FREE
markers, and numeric setting validation.

## License

MIT, see [`LICENSE`](LICENSE). Third-party components are listed in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
