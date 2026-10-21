# Panda Bamboo Feeder custom firmware: installation

Firmware: v0.11.9
For: **single-extruder Klipper printers**. The feeder works only as a filament buffer, plus a helping
push when you load and unload. **The stock 5-way hub is not used**: the feeder outlet goes straight to
the printer through a PTFE tube.

Once installed, see [`user-guide.md`](user-guide.md) for daily use.
Traditional Chinese original: [`../安裝說明.md`](../安裝說明.md).

---

## 1. What you need

### Hardware

| Item | Notes |
|---|---|
| Panda Bamboo Feeder | Any stock firmware version; flashing replaces it completely |
| Stock 24V power supply | The motor needs 24V. On USB alone the motor does not move and the screen shows `NO 24V`. Not needed for flashing |
| USB-C cable | **Must carry data.** Some cables only charge, and the computer then sees no device |
| Computer | Windows, macOS or Linux |
| PTFE tube | From the feeder outlet to the printer's filament inlet or extruder. For 1.75mm filament use 4mm OD tube; fittings as in BIQU's own instructions |
| Tube cutter | Cut the tube ends square; an angled end catches the filament |
| 2.4GHz WiFi | The feeder's chip (ESP32-S3) **does not support 5GHz** |

### Software

| Item | Where to get it |
|---|---|
| Python 3.10 or newer | Download from python.org. On Windows, tick "Add Python to PATH" during setup |
| esptool (flashing tool) | Run `python -m pip install esptool` |
| CH340 USB driver | Windows 10/11, macOS and Linux usually have it built in or install it automatically. Only if no serial (COM) port shows up after plugging in, download the CH340 driver from the WCH website |
| Web browser | Phone or computer, to open the feeder's web page |
| Mobileraker + Mobileraker Companion (optional) | Push notifications on your phone: jam, filament ran out, load incomplete |
| VS Code + PlatformIO (optional) | Only if you want to change the code and build it yourself |

### Firmware files

Download the four files of the latest version from the **Releases** page of the GitHub project and put
them in one folder. Flashing needs all four:

| File | Address |
|---|---|
| `bootloader.bin` | 0x0 |
| `partitions.bin` | 0x8000 |
| `boot_app0.bin` | 0xe000 |
| `firmware.bin` | 0x10000 |

Use `SHA256SUMS.txt` to check that the files are not damaged.

---

## 2. What the printer needs (checklist)

### Required

- [ ] The printer runs **Klipper + Moonraker**. Any Mainsail or Fluidd setup has both, and so does a rooted Creality K1 series printer.
- [ ] The feeder and the printer are on the **same network**.
- [ ] Moonraker lets the feeder connect, in one of two ways (see section 7):
  - The feeder's IP is inside `[authorization] trusted_clients` in `moonraker.conf`.
    The default KIAUH and MainsailOS configs already include home ranges such as `192.168.0.0/16` and
    `10.0.0.0/8`, so **most people need no change**.
  - Or enter Moonraker's API key on the feeder's web page.
- [ ] The filament path is feeder outlet → PTFE tube → printer. **No 5-way hub.**
- [ ] The extruder grips the filament. The buffer only works when both ends are held: the feeder's
  drive gear at one end and the printer's extruder at the other.

### Recommended

| Item | Used for | Without it |
|---|---|---|
| One `[filament_switch_sensor]`, ideally near the extruder | Stop loading automatically at the extruder, measure the path length, no top-ups while the extruder is empty, pre-checks of the setup tests | Still works; loading just feeds the set distance |
| `[pause_resume]` and a `PAUSE` macro | Pause the print automatically on a jam | A jam only sends a push notification |
| `[display_status]` | The unload G-code uses `M117 PBF:FREE` / `PBF:FOLLOW` to talk to the feeder | Use the `[gcode_macro PBF]` fallback channel, or rely on retraction length only |
| The printer's own runout handling (runout sensor + `pause_on_runout` or `runout_gcode`) | The printer pauses when the filament runs out | When the filament runs out, the feeder **only sends a notification and does not pause the printer**. Once the filament left in the tube is used up, the printer prints with nothing |

`[pause_resume]` and `[display_status]` are already included in Mainsail's `mainsail.cfg` and Fluidd's
`client.cfg`.

Only switch-type `filament_switch_sensor` sensors are supported. An encoder-type `filament_motion_sensor`
reports "no filament" after boot until the filament moves, which would make the feeder think the extruder
is empty, so it is not offered in the list.

### Built into Klipper, nothing to set up

`print_stats`, `motion_report`, `extruder`, `SET_STEPPER_ENABLE`, `M104`, `M83`, `G1`, `M400`
(the setup tests use the last few).

---

## 3. Back up the stock firmware first (strongly recommended)

Back up the whole 16MB flash. To return to stock later (with all your original settings), write it back.

1. Connect the feeder to the computer with USB-C.
2. Find the serial port name:
   - Windows: Device Manager → "Ports (COM & LPT)" → something like `USB-SERIAL CH340 (COM5)`
   - macOS: `/dev/cu.wchusbserial…` or `/dev/cu.usbserial-…`
   - Linux: `/dev/ttyUSB0`
3. Run (replace `COM5` with your port):

```bash
python -m esptool --chip esp32s3 --port COM5 --baud 921600 read-flash 0 0x1000000 pbf_backup.bin
```

This takes about 3–4 minutes. Keep `pbf_backup.bin` somewhere safe.

---

## 4. Flash the firmware

Open a command line in the folder with the four files and run (replace `COM5` with your port):

```bash
python -m esptool --chip esp32s3 --port COM5 --baud 921600 write-flash 0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin
```

- With esptool 4.x the command is `write_flash` (underscore); everything else is the same.
- This command **does not erase the WiFi and printer address saved by the stock firmware**. The new
  firmware picks them up on its first boot.
- `Failed to connect`: try another USB cable or port, and close any other program using the serial
  port (slicer, serial monitor).
- When updating over USB, always write all four files. If you write only `firmware.bin` after an
  earlier update from the web page, the feeder may still boot the old version.

The feeder restarts by itself when done. If the screen shows a state, it worked.

---

## 5. Connect to WiFi

- **Flashed over the stock firmware**: the WiFi saved by the stock firmware is used, nothing to set.
- **Never set up, or no connection within 20 seconds**: the feeder opens its own hotspot.
  1. Connect your phone to the WiFi **`PBF-xxxx`**, password **`pbf12345`**
  2. Open **http://192.168.4.1** in the browser
  3. Under "WiFi" at the bottom of the page, enter the name (SSID) and password, then tap "Save and restart"

Once connected, the third line of the screen shows the feeder's IP. Open the web page at
`http://<that IP>` or `http://pbf.local` (some Android phones do not support `.local`; use the IP).

The page starts in Chinese. The **English** button at the top right switches the web page and the push
notifications to English (tap **中文** to switch back).

The web page has no password. Anyone on the same network can open it.

---

## 6. Set up the printer link

Web page: Settings → "Printer link (Klipper / Moonraker)":

| Field | What to enter |
|---|---|
| Enabled | Tick it |
| Printer name | What you call your printer, for example `Voron` or `K1`. The web page, the screen and the notifications use this name. Letters and digits, at most 12 characters |
| Address | The printer's IP, for example `192.168.1.100` |
| Port | Moonraker's port, usually **7125** |
| API key | **Leave it empty for now.** Only needed in the "refused" case of section 7 |
| Filament sensor | Start with "Auto". After saving, the list shows which sensor it picked; choose another one by hand if it is wrong |
| Feed on after the sensor triggers | The distance from the sensor to the extruder gear, plus about 10mm. If the sensor sits next to the spool, measure that tube length (at most 5000mm) |

Tap "Save settings". When the status bar at the top shows "<printer name>: idle", the link works.

---

## 7. Connection refused (Moonraker login)

If the status bar shows **"refused (needs API key)"** and the log says
`Moonraker refused the feeder (HTTP 401)`, Moonraker does not trust the feeder's IP. There are two fixes;
use one:

**Method A: add the feeder to the trusted list** (changes the printer config)

In the `[authorization]` section of `moonraker.conf`, add the feeder's IP or the whole range to
`trusted_clients`:

```ini
[authorization]
trusted_clients:
    192.168.0.0/16
```

Save and restart Moonraker.

**Method B: enter the API key on the feeder** (no change on the printer)

1. Get Moonraker's API key: log in to the printer host over SSH and run `~/moonraker/scripts/fetch-apikey.sh`.
   If your computer is already trusted by Moonraker, you can also open
   `http://<printer IP>:7125/access/api_key` in a browser.
2. Paste it into the "API key" field on the feeder's web page and save.

Notes:

- **A wrong key breaks the connection**: Moonraker rejects a wrong key outright, even from an IP it
  trusts. If the feeder already connects, leave this field empty.
- The web page never shows the stored key, only "stored". Saving with the field empty keeps it; to
  delete it, tap "Clear API key".

---

## 8. Fit the filament and load it the first time

1. Connect the PTFE tube from the feeder outlet to the printer's filament inlet (or extruder).
2. **Before the first load**, set **Feed distance** under "Controls" on the web page to the whole
   tube length plus about 100mm. With a sensor, the feeder stops when the sensor triggers and saves
   the measured length; if the distance is too short, it stops before the filament reaches the sensor
   and reports an error.
3. Insert the filament into the inlet: the feeder grips it, preloads 100mm and stops at **STAGED**.
4. Press the **UP key** once: the filament goes to the printer, and when it stops the state becomes
   **READY** (buffer on).
5. Load on the printer (heat and extrude) so the extruder grips the filament. The load template in
   [`tip-forming-generic.md`](tip-forming-generic.md) makes the feeder push along in sync.

---

## 9. Setup tests (always run them the first time)

The default currents and parking amount were measured on a K1. Measure again on any other printer or
tube length.

**Before you start**: the printer's extruder grips the filament (with a sensor, it must show
filament), the printer is not printing, and the feeder is READY or STAGED.

Web page: "Setup tests" → **Full test**:

1. Buffer travel test: pushes the carriage to the end, measures its travel and sets the parking amount.
   About 20 seconds, no filament used.
2. Pressure test: finds the lowest run and holding currents that work. **It heats the nozzle to
   "Pressure test nozzle temperature"** (default 230°C; for PLA you can first change it to about 210),
   uses about 30cm of filament and takes about 4–8 minutes.

The results are saved to the settings automatically. Any key on the feeder, or the same button again,
stops a test.

---

## 10. Unload G-code (optional, recommended)

To make the feeder release its gear during an unload and then pull back in sync, add a few `M117`
lines to the unload G-code. Templates and explanations:

- Any Klipper printer: [`tip-forming-generic.md`](tip-forming-generic.md)
- Creality K1 Max: [`tip-forming.md`](tip-forming.md)

It also works without them: when retractions add up to more than 25mm while not printing, the feeder
releases its gear by itself.

---

## 11. Updating the firmware later

- **Web update** (easiest): on the web page under "Firmware update", choose `firmware.bin` and upload
  it. The feeder restarts when done. **Do not update during a print.**
- **USB update**: as in section 4, write all four files together.

Statistics reset with each new version; settings are kept.

---

## 12. Restore the stock firmware

Use the backup from section 3:

```bash
python -m esptool --chip esp32s3 --port COM5 --baud 921600 write-flash 0 pbf_backup.bin
```

Without a backup, use the stock firmware files from BIQU and flash them as BIQU describes.

---

## 13. Troubleshooting

| Problem | Cause | Fix |
|---|---|---|
| The computer shows no serial port | Charge-only cable, or missing driver | Use a data cable; install the CH340 driver |
| `Failed to connect` | Another program holds the serial port, or a bad contact | Close the other program, try another USB port |
| Screen shows `NO 24V` | 24V supply not connected | Connect the power supply |
| Status bar shows "not connected" | Wrong address or port, or a different network | Check the IP; Moonraker is usually 7125; open `http://<IP>:7125/server/info` in a computer browser and see if it answers |
| Status bar shows "refused (needs API key)" | Moonraker does not trust the feeder | See section 7 |
| Filament sensor list says "(no sensor found)" | The printer has no `filament_switch_sensor` | That is fine, loading feeds the set distance; or add a sensor to the printer |
| Load error "never saw the filament" | Feed distance is shorter than the path to the sensor, or the filament is stuck on the way | Increase Feed distance; check the tube path |
| Buffer does nothing while printing, keeps going STUCK | The extruder does not grip the filament, or "Release motor when idle" is on | Load the filament on the printer first; turn off "Release motor when idle"; run the full test |
| Print pauses with the notification "PBF: no feed" | The printer extruded 100mm and the buffer never emptied: the extruder slips, the nozzle is clogged, the new filament of a refill was not taken in, or the carriage sensor is broken | Check that the extruder takes filament. When fixed, press any key on the feeder to clear the alarm, then resume the print. The threshold is in the settings; 0 = off |
| Unload G-code reports `Unknown command: M117` | The printer has no `[display_status]` | See section 6 of [`tip-forming-generic.md`](tip-forming-generic.md) |
| No push notifications | Mobileraker Companion is not installed | Install Companion; the web page has a "Test notification" button |
