# Panda Bamboo Feeder – Klipper buffer firmware

Unofficial replacement firmware for the BIQU **Panda Bamboo Feeder** (PBF). It turns the feeder
into a **filament buffer and load/unload assistant for single-extruder Klipper printers**. It
does not use the stock 5-way hub: the feeder outlet goes straight to the printer through a PTFE
tube.

For Traditional Chinese, see [`繁中README.md`](繁中README.md). The detailed guides are in
English under [`docs/en/`](docs/en/), with the Traditional Chinese originals in [`docs/`](docs/).

> [!WARNING]
> **Not affiliated with BIQU / BigTreeTech.** Flashing replaces the stock firmware. Back up the
> whole flash first (see the installation guide) and use this at your own risk.

## Highlights

### 1. Buffer

When the printer pulls the buffer carriage home, the feeder tops it up and parks it with some
slack, so <mark><b>the printer pulls from the slack instead of dragging filament off the spool</b></mark>.
The screen shows each top-up as an arrow filling up beside `READY`.

### 2. Spool tangle and jam detection

<mark><b>A tangled spool, a spool end fastened to the hub, or a snagged tube pauses the print a few
seconds after the buffer runs empty</b></mark>, instead of leaving the extruder to grind and
under-extrude until the print fails.

How it works: once the printer has used up the buffer, the feeder tops it up. If the filament
cannot move, the carriage never leaves its rest position. After 80 mm of feeding (the jam length
setting, about 3 seconds at the default speed) the feeder stops, sends `PAUSE` to Klipper and a
push notification through Mobileraker Companion.

> [!TIP]
> A real runout is handled differently: when the tail leaves the feeder, it stops and lets the
> printer's own runout handling take over.

## More features

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
- **No-feed alarm**: the opposite of a jam. While printing, if the printer extrudes 100 mm
  (configurable) and the buffer never empties, the filament is not coming through the feeder
  (extruder grinding or clogged, new filament not taken in). It pauses the print and sends a push
  notification. It is measured in printer extrusion, not time.
- **Relax when cold**: once the printer's nozzle target is 0, the parked slack is pulled back
  until the carriage is home, so the tube fitting is not pulled on while the printer is idle.
- **Standby**: releases the gear (no holding current) while the printer is idle and cold, and
  grips again as soon as it heats.
- **Setup tests**: buffer travel, plus the lowest run and holding currents that work.
- **Web page** in English or Traditional Chinese (switch at the top right; push notifications
  follow the same language) with live status, log, settings and firmware upload; a "?" beside
  the trickier settings explains them. There is also a JSON API, OTA updates, and an OLED
  status screen (English).
- Talks to the printer through **Moonraker** over WiFi. An API key is optional, for Moonraker
  setups that do not trust the feeder's address.

## Status

Version **0.11.9**. Developed and used on a **Creality K1 Max**. Other Klipper printers should
work but have not been tested yet. Ready-to-flash files (four binaries plus `SHA256SUMS.txt`) are
under [**Releases**](https://github.com/solnqq7-gif/Panda-Bamboo-Feeder-Klipper/releases/latest).
Changes per version are listed under [Changelog](#changelog).

## Requirements

- BIQU Panda Bamboo Feeder with its 24 V power supply
- A Klipper printer with Moonraker, on the same 2.4 GHz network
- Recommended: a `filament_switch_sensor` near the extruder, plus `[pause_resume]` and
  `[display_status]` (Mainsail and Fluidd configs include both)
- For push notifications: [Mobileraker](https://github.com/Clon1998/mobileraker) with its
  Companion on the printer

The full checklist, tools and flashing steps are in
[`docs/en/installation.md`](docs/en/installation.md).

## Documentation

| English | 繁體中文 | Content |
|---|---|---|
| [`docs/en/installation.md`](docs/en/installation.md) | [`docs/安裝說明.md`](docs/安裝說明.md) | Installation: tools, backup, flashing, printer checklist, troubleshooting |
| [`docs/en/user-guide.md`](docs/en/user-guide.md) | [`docs/使用說明.md`](docs/使用說明.md) | User guide: keys, screen, states, web page, settings |
| [`docs/en/tip-forming-generic.md`](docs/en/tip-forming-generic.md) | [`docs/tip-forming-generic.md`](docs/tip-forming-generic.md) | Unload/load G-code templates for any Klipper printer (how to use `M117 PBF:…`) |
| [`docs/en/tip-forming.md`](docs/en/tip-forming.md) | [`docs/tip-forming.md`](docs/tip-forming.md) | The same, as tested on a Creality K1 Max |

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

Host regression checks (Python 3 and a C++17 `g++`, or set `CXX` to your compiler), from
`firmware/pbf-custom`:

```bash
python tests/run_regressions.py
```

These exercise the production control functions with simulated printer and hardware inputs;
they do not connect to a printer.

## Changelog

<details>
<summary>Recent versions (click to expand)</summary>

- **0.11.9**: English web page. The English/中文 button at the top right switches the language
  for the whole feeder: the web page, push notifications and settings error messages. 204 host
  checks; flashed by OTA with all settings kept, language switch checked on the hardware.
- **0.11.8**: a "?" next to the 15 settings that are easiest to get wrong on the web page;
  tapping it opens a short explanation under that row.
- **0.11.7**: the "heater off" and other hold messages are replaced once the printer heats again
  or a top-up starts (before, the stale text stayed on screen for the whole next print; feeding
  was not affected). 0.11.7 and 0.11.8 build and pass 202 host checks; flashed by OTA with all
  settings kept. The fixed message was confirmed on the hardware.
- **0.11.6**: relaxes the buffer once the printer's nozzle target drops to 0: it pulls the parked
  slack back until the carriage is home (never further, at most the parking amount + 6 mm), so
  the printer's tube fitting is not pulled on through the end-of-print moves and the idle time;
  the next top-up after heating refills it. 193 host checks. Seen on the hardware: 2.8 mm at boot
  with the printer idle, and 15.6 mm at the end of a print on 2026-10-21; all 9 prints since the
  flash completed.
- **0.11.5**: caps a full unload (double BACK) at the measured unload length + 20 mm instead of
  x1.3, and always finishes the last 15 mm once the tip has cleared the gear. 170 host checks;
  OTA startup and preserved settings verified.
- **0.11.4**: double UP returns to STAGED during manual feed, retract, and printer-follow moves:
  the first tap stops the move and also counts toward the double click. A lone stop tap cannot
  start a delayed load. Following stays paused until the current printer move ends. Physical
  button verification is still pending.
- **0.11.3**: fixes test cleanup during a print, menu access during a print, stale FREE markers,
  and numeric setting validation.

</details>

## License

MIT, see [`LICENSE`](LICENSE). Third-party components are listed in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
