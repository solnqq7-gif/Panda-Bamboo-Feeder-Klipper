# Unload tip forming: K1 Max version

For: Creality K1 Max (with a modified extruder that needs about 10mm more retraction than stock) +
Panda Bamboo Feeder custom firmware v0.8.8 or newer.
For other Klipper printers, see the generic version [`tip-forming-generic.md`](tip-forming-generic.md): it shows
the same use of `M117 PBF:FREE` / `PBF:FOLLOW`, without the K1-only `RESTORE_E_CURRENT` / `SET_E_MIN_CURRENT`.
Traditional Chinese original: [`../tip-forming.md`](../tip-forming.md).

Assumptions:
- **No 250mm long retraction.** The K1 only pulls the tip out of its own extruder gear; the PBF unload key
  pulls the rest back to the spool.
- **Mobileraker heats to 230 °C before it runs the G-code**, so the G-code has no `M109`.
- **The printer's `printer.cfg` is not changed.** Everything lives in the Mobileraker G-code and the PBF firmware.
- **The goal is a tip that does not catch.** A thin string is acceptable.

## 1. Current version: stock tip forming sequence + FOLLOW at the end (2026-09-24)

The tip forming part is **identical to the stock `_UNLOAD_FILAMENT`**. During tip forming the feeder
releases its gear (FREE); afterwards it switches to pulling back in sync (FOLLOW) and pulls the tip out
of the extruder gear.

```gcode
RESTORE_E_CURRENT
M117 PBF:FREE
G4 P400
M83
G1 E10 F300
G1 E-15 F3000
G1 E-22.4700 F2400
G1 E-6.4200 F1200
G1 E-3.2100 F720
G1 E5.0000 F356
G1 E-5.0000 F384
G1 E5.0000 F412
G1 E-5.0000 F440
G1 E5.0000 F467
G1 E-5.0000 F495
G1 E5.0000 F523
G1 E-5.0000 F3000
G1 E-15 F3000
M400
M117 PBF:FOLLOW
G4 P400
G1 E-60 F2000
M400
M117
SET_E_MIN_CURRENT
```

### Differences from stock (only these)

| Where | Stock | This version | Why |
|---|---|---|---|
| Start | `M109 S{hotend_temp}` | Removed | Mobileraker has already heated to 230 before it lets you run it |
| Start | — | `M117 PBF:FREE` + `G4 P400` | Tells the feeder to release its gear during tip forming; waits two polling cycles |
| After tip forming | — | `M400` + `M117 PBF:FOLLOW` + `G4 P400` | Switches only when tip forming is really done; the feeder grips again, ready to pull back in sync |
| End | — | `G1 E-60 F2000` | Pulls the tip from 62mm to 122mm, clear of the extruder gear and the toolhead sensor |
| End | — | `M400` + `M117` | Waits for the moves to finish, clears the message |

The `RESPOND TYPE=command MSG="Unloading filament..."` line was removed too; it only shows a message.
Stock does not turn the heater off, and neither does this version (Klipper turns it off after 10 minutes idle).

### Tip position

| Command | Tip distance from the nozzle | Feeder |
|---|---|---|
| `G1 E10 F300` (ram) | 0 | FREE |
| `E-15` / `E-22.47` / `E-6.42` / `E-3.21` | 15 → 37.5 → 43.9 → **47.1** | FREE |
| Four ±5 cooling moves (about 5 s) | 42.1 ↔ 47.1 | FREE |
| `G1 E-15 F3000` (stock end) | 62.1 | FREE |
| `G1 E-60 F2000` (added, about 1.8 s) | **122.1** | FOLLOW |

Net E = −112.1mm, tip travel 122.1mm. The 10mm difference is the filament the ram pushes out of the
nozzle, which does not come back. **To judge whether the tip has left the gear, use 122, not 112.**

Where the distance comes from: on a stock extruder the tip leaves the gear after about 102mm; this modified
extruder needs about 10mm more (112); another 10mm of margin → 122.

### Why the last part uses FOLLOW (the previous version was FREE + `E-50`)

The version used from 2026-09-22 to 09-24 stayed in FREE throughout and ended with `G1 E-50 F2000`
(tip at 112mm). Reasons for switching to FOLLOW:

- **In FREE, nothing pulls the tip once it leaves the K1 gear**, so it stops right above the gear. In
  FOLLOW the feeder keeps turning backwards in sync and also pulls away the last 10–20mm, so the tip
  really clears the gear and the toolhead sensor.
- 10mm more margin (112 → 122).
- This part is a one-way long retraction with no back-and-forth, so FOLLOW with its 0.2 s polling keeps
  up (on 2026-09-20 it followed a 302mm `F2000` retraction).

**FOLLOW only changes where the tip ends up, not its shape.** The shape comes entirely from the stock
tip forming part before it.

## 2. Do not change the tip forming part

### 2026-09-21: tip forming at 32mm + skinnydip (jammed in testing)

Tip forming 32mm from the nozzle, ±8mm cooling moves up to 45mm/s, then a skinnydip (+25mm back into the
melt zone, then out at 70mm/s), with the temperature lowered before tip forming.

**Result: the tip was thicker than 2.2mm and caught.** The stock sequence turned out to work fine on this printer.

Most likely causes (not tested one by one; this is a guess):

- **32mm is still too close to the hot zone**: the filament is still soft there, and the ±8 push-back
  squashed it. Stock does its cooling moves at 42–47mm, where it is cool enough that the moves only
  stretch the tip thinner and never thicken it.
- **The skinnydip pushed the already-formed soft tip 25mm down.** A direct drive has no cooling tube to
  limit the diameter, so pushing it down makes a mushroom.

The earlier claim that the stock sequence "is Prusa MK3 values and does nothing on a K1" **was proven wrong in testing**.

### 2026-09-24: only one cooling move, final pull slowed to F2000 (not adopted)

We considered cutting the four ±5 cooling moves down to only the last one (`E5 F523` / `E-5 F2000`)
and slowing the final `E-5` / `E-15` from F3000 to F2000. **Not tested; judged likely to give blunt tips,
so not adopted:**

- After the ram and the 47mm retraction the tip is still soft. The four moves keep it in the cold zone for
  about 5 seconds, hardening while it moves (moving keeps it from sticking to the tube wall). Cut to about
  0.7 seconds, the tip would still be soft when it is dragged through the heat sink, the gear and the tube
  entry, and would tend to mushroom or pull strings.
- The final fast pull at 50mm/s snaps the already-thinned neck cleanly; at 33mm/s it pulls strings more easily.

**This document takes the stock sequence as the reference.** Before changing the tip forming part,
measure the tips of the current version using the method in section 4.

## 3. Check that the tip has left the extruder gear

112mm is the estimated need (K1 nozzle to gear about 45–50mm + toolhead sensor + the extra 10mm of the
modified extruder); 122 leaves another 10mm.

**Check**: after the unload, press the PBF unload key once. If it pulls the filament = the tip has left
the K1 gear; if it slips and grinds = still gripped, so increase the `-60`.
(The feeder cannot pull out filament that the K1 extruder grips; verified on 2026-09-20.)

**Also check that the toolhead sensor has cleared**: if the sensor still reads "filament" when a load
starts, the feeder waits for it to go empty and then show filament again before it stops; in that case
the next load will not stop at the extruder:

```bash
curl -s "http://192.168.1.100:4409/printer/objects/query?filament_switch_sensor%20filament_sensor_2"
```

`filament_detected` must be `false`.

## 4. Tip measurement log

The only way to judge tip forming is to measure. After each unload:

1. Pull the filament out with the PBF unload key and cut off the first 30mm or so.
2. Measure the **thickest point** with calipers and take a side photo.
3. Note the shape: pointed / blunt but not thick / mushroom (thicker than the filament) / with a string (about how long).
4. Note whether and where it caught: above the gear during the unload / when the unload key pulled it back
   to the spool / on the next load.

Pass: **no catching**; strings are acceptable. Thickest point, provisional limit **≤ 1.9mm**
(originally 1.8, but on 2026-09-24 a 1.87 tip did not catch; the 09-21 version at 2.2mm and more did, so
the limit lies between those two numbers).

Why 1.87 still passes: the thickest part of the tip most likely melted inside the hotend bore (about 2mm
on a typical 1.75mm hotend) and set while being pulled up, so it fits through a bore of the same size.
What catches is a tip **deformed after leaving the bore** (for example a soft tip pushed back down into a
mushroom by a skinnydip); those exceed 2mm.

| Date | Version | Filament / temperature | Thickest (mm) | Shape | String | Caught? Where |
|---|---|---|---|---|---|---|
| 2026-09-24 | Stock tip forming + FOLLOW 60 | Black PETG / 230 °C | 1.87 (about 4mm from the tip, string not included) | Short cone; a short stretch of the neck slightly uneven and shiny; no mushroom | Yes, one long thin string (a few cm by eye from the photo) | No |
| | Stock tip forming + FOLLOW 60 | | | | | |
| | Stock tip forming + FOLLOW 60 | | | | | |

Run at least 3 unloads from the same spool before drawing conclusions; single results vary a lot.

### Temperature (what Mobileraker heats to before the unload)

- **PETG: keep 230 °C, no lower.** 230 is already at the low end for PETG; any lower and the ram
  (`E10 F300`, about 12mm³/s) may not push through. PETG strings easily by nature; a long string on the tip
  is the material, not a problem with the sequence.
- **PLA: 230 °C is on the hot side.** Try 215 °C; strings are usually shorter. Change only the temperature, one variable at a time.

## 5. TPU

The stock sequence's `F3000` (50mm/s) retractions do not suit TPU: TPU is elastic, so a fast pull only
stretches it and it springs back, and it may buckle and jam in the heat break. You can try halving all `F`
values and raising Mobileraker's starting temperature from 210 to 230 (at 210 TPU resists pushing and the
gear tends to flatten it). **This has not been tested.**

## 6. Feeder gear release (FREE) and pulling back in sync (FOLLOW)

### Why release during tip forming

The stock sequence has four fast ±5mm back-and-forth moves. Even though FOLLOW now follows both ways, it
still reacts through polling every 0.2 seconds; it cannot keep up with such short moves and tends to catch
up only after the move has already reversed. The tip forming part should therefore still use FREE.
With the gear released and spinning freely, the printer pulls the filament itself and the problem is gone.

The filament that comes back during FREE (in the current version the tip travels to 62mm) is partly taken
up by the buffer carriage; the rest stays in the tube.
**Do not do longer retractions inside FREE**: nothing takes up the filament at the inlet side, and a few
hundred mm tangle into a knot. To pull back further, switch to FOLLOW.

### Marker: `M117 PBF:FREE`

`M117` writes to Klipper's standard object `display_status.message`, which the feeder reads when it polls,
so nothing needs to be added to the printer config.

- Message contains `PBF:FREE` → enters **FREE**: motor off, one beep, gear spins freely.
- Message cleared / changed, timeout, any key, K1 starts printing, link lost → grips again.
- If the K1 toolhead is already empty when it grips again → stays at **STAGED** and keeps gripping; then
  the unload key pulls the filament back to the spool.
- As soon as FREE triggers, a 60-second guard starts; even if the feeder grips again early, it does not
  top up automatically and push filament back into the K1 during that time.

The feeder's own notifications are sent as `M117 $MR$:PBF: jam|…` (`PBF: 卡料|…` when the page is in Chinese).
What follows `PBF:` is not `FREE`, so they are never mistaken for the marker.

### From FREE to FOLLOW: `M117 PBF:FOLLOW`

The current unload version works like this: after tip forming it switches to FOLLOW, and the feeder pulls
back the last 60mm in sync.

```gcode
M400              ; switch only when tip forming is really done
M117 PBF:FOLLOW
G4 P400           ; wait two polling cycles
G1 E-60 F2000     ; to pull out several hundred mm, increase the distance
M400
M117
```

What the feeder does, in order:

1. The message changes from `PBF:FREE` to `PBF:FOLLOW` → counts as "message changed": leaves FREE, grips again.
2. K1's E speed goes above 3mm/s → starts turning backwards at the same speed (at most 60mm/s).
3. K1 stops → the feeder stops. If it took up more than 20mm, it counts as an unload: it stays at
   **STAGED** and does not top up automatically.

**The `M400` before `M117 PBF:FOLLOW` must stay**: in the G-code queue, `M117` takes effect before the
moves ahead of it. Without `M400`, the feeder grips again while tip forming is still moving back and forth.

`FREE` / `FOLLOW` are case-insensitive, so `M117 PBF:follow` works too. During an explicit FOLLOW, retractions
adding up to more than 25mm do not switch back to FREE, and the feeder follows both forward loading and
backward unloading. FOLLOW does not run while the K1 is in a real print, so a marker that was never cleared
cannot keep feeding.

### Fallback: detect unloads by retraction length when there is no marker

An unload from the K1 touch screen (`QUIT_MATERIAL`) has no `M117`. So when followed retractions add up
to more than 25mm (configurable), the feeder also releases its gear, and grips again 3 seconds after the
retraction stops. With the stock sequence: after `E-15`, the threshold is passed halfway through
`E-22.47`. The longest pause between the cooling moves is about 0.85 seconds (`E5 F356`), so it is never
taken for the end.

On this printer `PAUSE` retracts only 3mm, and normal print retractions are smaller, so neither triggers it.
`M600` calls `_UNLOAD_FILAMENT` (52mm), which does trigger it; releasing during a filament change is right anyway.

### Safety measures

| Measure | Behavior |
|---|---|
| Timeout (default 60 s, 5–600) | Grips again automatically, and does not release again until the message changes |
| Any key | Grips again at once, also locked |
| K1 starts printing / link lost | Grips again at once |
| Message is already `PBF:FREE` at boot | No action; the message must be cleared once before it counts |
| 60-second top-up guard after FREE | No automatic top-ups; `PBF:FOLLOW` still follows. When the K1 starts a real print, the guard ends at once and top-ups resume |

The last two exist because an `M117` message stays on the display. If an unload is cancelled, `PBF:FREE`
stays there; without these two measures the gear would be released again and again, and a gear that does
not grip is like having no feeder.

### Related settings (web page)

| Setting | Default |
|---|---|
| Release the gear when K1 unloads (M117 PBF:FREE) | On |
| Release timeout | 60 s |
| Without a marker, detect unloads by retraction length | On |
| Retraction that counts as an unload | 25mm |

Manual test: the "Release gear / Grip gear" button on the web page, or the serial commands `free` / `grip`.

## 7. K1 notes

- **`RESTORE_E_CURRENT` is required.** The K1 runs its extruder at a very low current when idle; without it the extruder slips.
- **`SET_E_MIN_CURRENT` at the end is required too**, with an `M400` before it. Without it the extruder
  motor stays at high current while idle and gets hot.
- It extrudes 10mm out of the nozzle, so move the toolhead away from the print first.
- Do not name a macro `_UNLOAD_FILAMENT` / `UNLOAD_FILAMENT` / `LOAD_FILAMENT`
  (if you ever turn this into a macro); the stock cfg would override it.
