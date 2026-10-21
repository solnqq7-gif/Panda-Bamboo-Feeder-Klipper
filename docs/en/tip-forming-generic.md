# Unload / load G-code examples (generic version)

For: any Klipper printer + Panda Bamboo Feeder custom firmware v0.10.0 or newer.
The version tested on a Creality K1 Max is in [`tip-forming.md`](tip-forming.md).
Traditional Chinese original: [`../tip-forming-generic.md`](../tip-forming-generic.md).

**This document shows how to use `M117 PBF:FREE` / `M117 PBF:FOLLOW`** so that the printer and the
feeder work together during unloads and loads. The tip forming moves below are only an example, not
tuned values. **Replace them with an unload sequence that already works on your printer** (the stock
unload macro or your slicer's filament change settings both work), and just put the `M117` lines in
following the structure below.

---

## 1. Structure

| Part | What goes there | What the feeder does |
|---|---|---|
| Start | `M117 PBF:FREE` + `G4 P400` | Releases its gear (FREE) so the printer pulls the filament itself, with no tug of war during the back-and-forth of tip forming |
| Tip forming | Your own tip forming moves | Gear spins freely |
| End of tip forming | `M400` + `M117 PBF:FOLLOW` + `G4 P400` | Grips again and switches to pulling back in sync (FOLLOW) |
| Long retraction | `G1 E-<distance>` | Turns backwards at the printer's speed and pulls the filament tip out of the extruder gear |
| End | `M400` + `M117` | Clears the message and returns to normal |

What you must not leave out:

- **`G4 P400`**: the feeder reads the printer every 0.2 seconds. Wait two cycles so it sees the message.
- **The `M400` before `PBF:FOLLOW`**: Klipper applies `M117` before the moves queued ahead of it are done.
  Without `M400`, the feeder grips again while tip forming is still moving back and forth.
- **The final `M117`**: an `M117` message stays on the display. If you do not clear it, the feeder stays
  in FREE / FOLLOW. (The feeder has a 60-second timeout as a safety net, but do not rely on it.)
- **No long retraction inside FREE**: in FREE the gear spins freely and nothing takes up the filament
  coming back at the inlet side; a few hundred mm will tangle into a knot. To pull back far, switch to
  FOLLOW first.

## 2. Unload template (run it from the console, Mobileraker, or a Mainsail / Fluidd macro button)

```gcode
; ==== Adjust for your printer ====
M109 S230          ; heat to the unload temperature and wait (PLA about 215, PETG about 230). Remove if already hot
M83                ; relative extrusion

M117 PBF:FREE      ; feeder releases its gear
G4 P400            ; wait until the feeder sees the message

; ==== Tip forming (example: the same moves as the tested K1 version; your own work too) ====
G1 E10 F300        ; extrude a little first for a clean tip
G1 E-15 F3000      ; fast retraction out of the melt zone
G1 E-22.47 F2400
G1 E-6.42 F1200
G1 E-3.21 F720
G1 E5 F356         ; 4 cooling moves: the tip hardens in the cold zone while it moves
G1 E-5 F384
G1 E5 F412
G1 E-5 F440
G1 E5 F467
G1 E-5 F495
G1 E5 F523
G1 E-5 F3000
G1 E-15 F3000
; ==== End of tip forming ====

M400               ; required: wait until tip forming is really done
M117 PBF:FOLLOW    ; feeder grips again and pulls back in sync
G4 P400
G1 E-60 F2000      ; pull the tip out of the extruder gear; distance see section 4
M400
M117               ; clear the marker
```

After the unload, press the feeder's **unload key** (DOWN) to pull the filament back to the spool.

## 3. Load template

The filament is already at the extruder entry (sent with the feeder's UP key and stopped after the
sensor). Now let the extruder pull it into the hotend:

```gcode
M109 S230          ; heat and wait
M83
M117 PBF:FOLLOW    ; feeder pushes in sync with the extruder
G4 P400
G1 E100 F300       ; extruder pulls the filament into the hotend and extrudes some; distance depends on your printer
M400
M117
```

## 4. How long the final retraction should be

The goal is **a tip that has left the extruder gear**, so the feeder can pull it back (the feeder cannot
pull filament that the extruder still grips).

| Extruder type | Retraction distance (tip position, measured from the nozzle) |
|---|---|
| Direct drive | Nozzle to extruder gear, about 50–100mm, plus 10–20mm margin |
| Bowden | The whole tube length from nozzle to extruder gear, plus 10–20mm margin (often several hundred mm) |

In the template, tip position = position at the end of tip forming + the distance of the final `G1 E-`.
The example's tip forming ends at about 62mm; with `E-60` the tip ends up at about 122mm.

**Check**: after the unload, press the feeder's unload key once. If it pulls the filament = the tip has
left the extruder gear; if it slips and grinds = the filament is still gripped, so increase the distance.

## 5. As a `printer.cfg` macro (optional)

If you are willing to change the printer config, wrap it in a macro and then just send `PBF_UNLOAD`:

```ini
[gcode_macro PBF_UNLOAD]
description: Unload and notify the Panda Bamboo Feeder
gcode:
  {% set temp = params.TEMP|default(230)|int %}
  {% set dist = params.DIST|default(60)|float %}
  M109 S{temp}
  SAVE_GCODE_STATE NAME=pbf_unload
  M83
  M117 PBF:FREE
  G4 P400
  G1 E10 F300
  G1 E-15 F3000
  G1 E-22.47 F2400
  G1 E-6.42 F1200
  G1 E-3.21 F720
  G1 E5 F356
  G1 E-5 F384
  G1 E5 F412
  G1 E-5 F440
  G1 E5 F467
  G1 E-5 F495
  G1 E5 F523
  G1 E-5 F3000
  G1 E-15 F3000
  M400
  M117 PBF:FOLLOW
  G4 P400
  G1 E-{dist} F2000
  M400
  M117
  RESTORE_GCODE_STATE NAME=pbf_unload
```

Usage: `PBF_UNLOAD TEMP=215 DIST=80`.
**Do not** name the macro like one the printer already has, such as `UNLOAD_FILAMENT` or
`LOAD_FILAMENT`; one would override the other.

## 6. When the printer has no `[display_status]`

`M117` needs `[display_status]` (Mainsail and Fluidd configs include it).
Without it, `M117` reports `Unknown command`. Use the fallback channel instead: add a macro to
`printer.cfg` that only holds a variable

```ini
[gcode_macro PBF]
variable_mode: 0
gcode:
  # only holds the variable, no content needed
```

and replace `M117 PBF:FREE` / `M117 PBF:FOLLOW` / `M117` in the templates with

```gcode
SET_GCODE_VARIABLE MACRO=PBF VARIABLE=mode VALUE=1   ; FREE
SET_GCODE_VARIABLE MACRO=PBF VARIABLE=mode VALUE=2   ; FOLLOW
SET_GCODE_VARIABLE MACRO=PBF VARIABLE=mode VALUE=0   ; clear
```

The feeder reads both channels and uses whichever carries a marker.

## 7. Other notes

- **FOLLOW does not run during a real print** (so a marker that was never cleared cannot keep feeding
  through a whole print). To change filament in the middle of a print file, pause first;
  `M600` usually starts with `PAUSE` anyway.
- It also works with no marker at all: when the feeder sees retractions add up to more than 25mm while
  not printing (changeable on the web page), it releases its gear by itself.
  Markers are more precise, though, so adding them is recommended.
- The tip forming part of this template has only been tested on a K1. Whether the tip catches can only
  be found by unloading and measuring it with calipers
  (method in section 4 of [`tip-forming.md`](tip-forming.md)).
