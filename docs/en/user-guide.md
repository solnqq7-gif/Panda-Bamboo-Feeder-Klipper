# Panda Bamboo Feeder custom firmware: user guide

Firmware: v0.11.9. Changes per version:

- v0.11.9: English web page; switch with **English / 中文** at the top right. This is a setting of the feeder itself, so push notifications and settings error messages switch too.
- v0.11.8: the web settings that are easiest to get wrong (15 fields) get a circled question mark beside them. Tap it to open an explanation under that field; tap again to close it.
- v0.11.7: fixes a status message that did not update. When the nozzle heats again or top-ups resume, the hold messages `K1 heater off`, `relaxing buffer`, `waiting for K1` and `K1 extruder empty` are replaced by `K1 printing: buffer on` (`buffer on` when not printing). Before, the old line stayed until the end of the print; only the display was wrong, feeding was always fine.
- v0.11.6: **relax the buffer while the heater is off**. Once the printer's nozzle target temperature is 0 and the carriage has been still for 2 seconds, the feeder slowly pulls back the parked slack and stops as soon as the carriage is home (at most "parking amount + 6mm", never into the filament in the extruder). The extruder's tube fitting is no longer pulled on by the buffer through the end of a print and while idle; the next heat-up tops up again as usual. Reason: in 2026-10, several times in a row, the tube slipped out of the extruder fitting in the first minutes of a print. Standby now also logs when the carriage returns home or leaves home.
- v0.11.5: the limit of a full unload (double DOWN, "unload until empty") is now the measured unload length + 20mm (was ×1.3); once the tip has cleared the gear, it always finishes the last 15mm before stopping. Section 4 now describes the complete key behavior.
- v0.11.4: fixes double UP back to STAGED during manual feed and retract: the stop tap counts as the first tap of the double click, and following is allowed again only after the current printer move ends.
- v0.11.3: fixes test cleanup that disturbed a print, the two-key menu bypassing the print lock, boot and link loss wrongly clearing the FREE lock, and numeric setting overflow; adds host regression checks.
- v0.11.2: when the printer link drops, the last heater state read is kept (a K1 often does not answer for half a minute after a print, and v0.11.1 pushed 82mm into a loose tube during that time). While top-ups are held, every time the carriage returns home or leaves home is logged, to find when the tube came out. Fixes a lost log line when two lines were written at the same time.
- v0.11.1: **no top-ups while the nozzle is not heated**. When the printer's nozzle target temperature is 0 (the end macro has turned the heater off, or idle), there are no top-ups, and a running top-up stops. Reason: once at the end of a K1 print the tube came out of the extruder fitting; the feeder thought the buffer was empty, pushed 80mm into the loose tube, and paused a print that had already finished.
- v0.11.0: adds the **no-feed alarm** (during a print, if the printer extrudes 100mm and the buffer never empties, pause and notify). During a **manual refill**, keeps pressure on the new filament as the joint nears the extruder, until the extruder takes it in. Top-up logging during a print is now one line per minute, so the log is not flushed within minutes.
- v0.10.0: **works with any Klipper printer**. **Manual refill**: for 3 minutes after a preload the jam check allows "jam length + path length", and new filament inserted during a print is pushed down to the old tail. New screen layout (top-ups as an arrow animation beside the title, the IP on the bottom line in READY, `P7` now `L7`, IN1/IN2 as box icons). Adds "Printer name" (the "K1" that was hard-coded in the web page, screen, log and notifications is replaced by this name). The filament sensor is picked automatically from the printer's object list or chosen by hand (it used to be the K1's `filament_sensor_2`, hard-coded), and the `filament_sensor` at the back of the K1 is no longer read. Adds a Moonraker **API key** field (only needed when Moonraker refuses the connection); the status bar shows "refused" when that happens. "Feed on after the sensor triggers" now goes up to **5000mm** instead of 100mm (above 40mm at the manual feed speed, the last 40mm slow), and the load/unload distance limits rise to 10000mm. The unload G-code examples are split into a generic and a K1 version.
- v0.9.0: adds **standby** (9.6): releases the gear while the K1 is idle, not heating and the nozzle is cold, and grips as soon as the K1 heats. "Refill" is renamed "top-up" everywhere.
- v0.8.8: web page only: colored setting groups, the load/unload current level as a drop-down, statistics moved under the status with a log toggle, "Feed/refill speed" renamed "Manual feed / top-up speed".
- v0.8.7: the FREE top-up guard ends as soon as the K1 starts printing.
- v0.8.6: `M117 PBF:FOLLOW` follows both loading and unloading.
- v0.8.5: no top-ups for 60 s after FREE, FOLLOW marker added, lock for the load/unload distances added.

Purpose: a **filament buffer for single-extruder printers** only, plus a helping push when loading and
unloading. **No 5-way hub** (the feeder outlet goes straight to the printer through a PTFE tube). Works with
any Klipper + Moonraker printer; developed and tested on a Creality K1 Max, and the examples in this guide
(IPs, G-code) come from that printer.
Installation (tools, flashing, printer checklist): [`installation.md`](installation.md).
Traditional Chinese original: [`../使用說明.md`](../使用說明.md).

---

## 1. Connection and web page

| Item | Details |
|---|---|
| Web page | http://192.168.1.50 or http://pbf.local (some Android phones do not support .local; use the IP) |
| WiFi | Connects automatically at boot (the first time it uses the WiFi saved by the stock firmware) |
| When WiFi fails | After 20 seconds it opens the hotspot **PBF-xxxx**, password `pbf12345`, address http://192.168.4.1 |
| Motor power | Needs the **24V power supply**. On USB alone the motor does not move and the screen shows NO 24V |
| Printer link | Uses the printer address saved by the stock firmware (on the K1, for example, 192.168.1.100:4409). Change it, and the printer name, on the web page under "Settings → Printer link". See section 7.5 |

The web page has no password. Anyone on the same network can open it.

---

## 2. Screen

```
READY        ⇨            ← state (large); in READY the top-up arrow is on the right
woke: K1 heating          ← message, progress
L7 550mA                  ← pressure level, motor current (NO 24V when there is no 24V)
IN1 ▣ IN2 ▣ BUF:out       ← sensors: box with a small solid square = filament, empty box = none
192.168.1.50 2xUP:STAGED ← READY: IP + key hint; other states: key hint
```

- **Top-up arrow** (v0.10.0): in READY there is a hollow arrow to the right of the title. During a top-up it
  fills from left to right (relative to the amount this top-up expects to feed); when full it stays for
  0.3 s, then clears. **During top-ups the title stays READY and the message line does not change**, so the
  screen does not flash on every top-up (the web page still shows "Topping up").
- **Where the IP is**: in READY on the bottom line (the old `hold UP:+20` hint was removed; holding UP still
  feeds at most 20mm); in other states, and when locked during a print, on the third line.
- The `L7` on the third line is the web page's "Pressure level L7" (up to v0.9.0 the screen showed `P7`; same thing).

**Sensors**

| Screen | Web page | Position and meaning |
|---|---|---|
| IN1 | Inlet | At the inlet: whether filament is inserted; when the tail passes, used to detect a runout |
| IN2 | Gear | At the drive gear: whether the gear has gripped the filament |
| BUF | Buffer | Screen `home` / web "Buffer: home · empty" = the carriage is at its rest position = **the buffer stock is used up, a top-up is needed**. Screen `out` / web "Buffer: out · stocked" = the extra filament has pushed the carriage away from home = **the buffer holds stock** (normally it parks mid-travel in this state). How it works: see 7.3. Up to v0.8.0 the screen showed `rest`/`PULLED` and the web page "resting"/"pulled out"; those were wrong descriptions |

**Brightness**

| Situation | Brightness (default) |
|---|---|
| Normal (states other than READY, menu) | 80% |
| READY, STANDBY | 10% |
| A top-up starts in READY | 80%, held 5 s after the top-up |
| Any key or web action in READY | 80%, held 10 s |

---

## 3. States

| Screen | Web page | Meaning | Buffer top-ups |
|---|---|---|---|
| EMPTY | Empty | No filament | — |
| INSERTING | Gripping filament | Filament inserted; the motor turns slowly so the gear grips it | — |
| PUSH IN | Push the filament in further | Not gripped within 15 seconds; push it in a little further | — |
| PRELOAD | Preloading | Once gripped, feeds the preload length automatically (default 100mm) | — |
| STAGED | Staged (buffer paused, tube can be swapped) | Waits for you to feed; **no automatic top-ups in this state**, so you can swap the PTFE tube. The feeder enters this state by itself after a K1 unload | Paused |
| LOADING | Loading | Feeding a distance; the last 40mm slower | — |
| READY | Ready (buffer on) | The filament is at the printer; the feeder tops up automatically as the printer uses filament | **On** |
| FEEDING | Topping up | The carriage is home (buffer used up); topping up now | **On** |
| STANDBY | Standby (gear released) | The K1 has been idle long enough in READY: the motor is off and stays cool. Back to READY as soon as the K1 heats, starts printing, or any key is pressed; see 9.6 | Paused |
| RUNOUT | Runout: K1 takes over | The tail has left the gear; the motor is relaxed and does nothing (sends a notification) | — |
| FOLLOW | Following K1 | Follows both ways when `PBF:FOLLOW` is sent explicitly; the automatic mode without a marker still follows retractions only | — |
| FREE | Gear released (printer pulls) | Motor off, gear spins freely, so the K1 pulls the filament out itself (for tip forming / unloading). See 9.5 | Paused |
| UNLOADING | Unloading | Unloading | — |
| UNLOADED | Unloaded | The tip has passed back through the gear; pull it out or load again | — |
| STUCK! | Jam! | A top-up went past the jam length and the carriage is still home (not pushed out) | Stopped |
| ERROR | Error | See section 8 | Stopped |
| NO POWER | No motor power (24 V?) | No 24V | — |

---

## 4. Keys

UP key = load, DOWN key = unload.

**Timing**

| Action | Detected as |
|---|---|
| Tap | Released within 0.6 s |
| Hold | Held longer than 0.6 s; stops when released |
| Double tap | Two taps less than 0.45 s apart |
| Both keys | Both keys held together for 1 s |

**Special states first**: in the states below, a key press is handled by this table (checked from top to
bottom) and does not also trigger the "normal use" action further down.

| State | Any key |
|---|---|
| Settings menu open | Goes to the menu (section 5) |
| STUCK! / ERROR | **Clears the alarm** and returns to normal |
| FREE (gear released) | Grips again and ends FREE |
| STANDBY | **Only wakes it** back to READY (the screen is dim, so you cannot see the key hints at that moment). A web page action wakes it and is then carried out |
| K1 printing (keys locked) | Stops a running action; otherwise a short beep and the screen shows "LOCKED: K1 printing". Keys work normally while the K1 is paused |
| An action is running (gripping, preload, load/unload, FOLLOW, test, held feed/retract) | **Stops immediately.** Releasing the stopping press does not count as a tap (exception for UP below) |

**Normal use**

| Action | STAGED and other states | READY / FEEDING (filament at the printer) |
|---|---|---|
| UP tap | **Feed distance** (default 500mm); if the filament is only at the inlet and not gripped yet, grips it instead; with no filament at all, nothing happens | Nothing; the screen shows `hold UP to feed (max 20mm)` |
| UP hold | Feeds as long as you hold (15mm/s) | Feeds **at most 20mm per hold**; a running top-up stops first |
| UP double tap | During a manual feed or retract with IN2 showing filament, **stops and returns to STAGED** | **Returns to STAGED** (buffer paused, tube can be swapped) |
| DOWN tap | **Retract distance** (default 600mm); if the tip clears the gear early, retracts 15mm more and stops. Nothing happens when the gear has no filament | Same |
| DOWN again within 2 s of a DOWN tap | Short press = stop; hold = switch from the distance retract to a held retract | Same |
| DOWN hold | Retracts as long as you hold (15mm/s) | Same |
| DOWN double tap | **Unload until empty**: retracts until the tip has cleared the gear, then 15mm more. The limit is **the measured unload length + 20mm** ("Unload limit" in section 10); if the tip is still in the gear at the limit, it reports `tip stuck` (section 8) | Same |
| Both keys for 1 s | Opens the settings menu; not while printing or paused, while the motor turns, or during a test | Same |

- From v0.11.4, **the short UP press that stops an action also counts as the first tap of a double tap**;
  a second short UP within 450ms returns to STAGED. A single stop tap does not start a load after the wait.
- This double tap also works for held feed/retract, web page moves and FOLLOW. After FOLLOW is stopped,
  following is allowed again only once the printer's current move has ended, so it does not start moving
  again right after returning to STAGED.
- A DOWN tap waits about 0.5 s to rule out a double tap before it starts retracting.
- **The lock stays on if the link drops during a print** (from v0.8.2): if the K1 cannot be reached during a
  print, the lock stays and the screen shows "LOCKED: K1 offline". The K1 keeps printing without Wi-Fi, so
  the lock opens only once the K1 is back and reports "not printing". If the K1 has really lost power or
  crashed, unlock it like this: untick "Lock keys and web moves while printing" on the web page, or turn
  off the printer link, or restart the feeder (at boot it does not know the K1's state yet and does not lock).

---

## 5. Settings menu (both keys for 1 s)

From v0.11.3 the settings menu cannot be opened while the printer is printing or paused, even with the
print lock turned off. If a print starts while the menu is open, the menu saves and closes so the buffer
keeps working.

Use: **UP** = next item (after the last item back to the first), **DOWN** = change or run; numbers go up one
step per press and wrap to the minimum after the maximum. The menu only takes taps; holding does nothing.
Pressing DOWN on Exit, or both keys for 1 s again, leaves the menu and saves.

| Item | Description |
|---|---|
| Pressure | Pressure level L1–L10 (see section 7) |
| Pressure test | Pressure test (finds the lowest current that works, see 7.2) |
| Buffer test | Buffer travel test (measures the carriage travel and sets the parking amount, see 7.3) |
| Full test | Full test (buffer travel + pressure, for the first install, see 7.0) |
| Buzzer | Buzzer on/off |
| Preload | Preload length, +10mm per press, cycles 0–200 |
| Load length | Feed distance, +50mm per press, cycles 50–1500 (calibrated automatically during loading, see section 7) |
| Stuck length | Jam length, +10mm per press, cycles 30–200 |
| Idle release | Release motor when idle. **Must be off to use the buffer** (see 7.1); the full test turns it off |
| Brightness | Normal brightness, 10–100% |
| Dim (READY) | READY brightness, 0–50% |

---

## 6. Web page

**Controls**

| Button | Description |
|---|---|
| Hold to feed (green) / Hold to retract (orange) | Turns only while your finger is down (20mm per second). In READY, Hold to feed gives at most 20mm |
| Feed distance [ ] mm | Feeds the distance next to it. The number is **shared with an UP tap** and saved when you change it |
| Retract distance [ ] mm | Retracts the distance next to it. The number is **shared with a DOWN tap** and saved when you change it |
| Stop / clear alarm | Stops any action (tests included) and clears alarms |
| Release gear / Grip gear | Switches FREE by hand, see 9.5. The timeout applies too |
| Test buzzer | One beep |

- While you hold a button, the phone sends a signal every 0.25 s; the feeder stops by itself if nothing
  arrives for 0.6 s (protection against WiFi drops). Switching to another app or locking the phone also stops it.
- "**last move**" in the status line can measure the tube length: in STAGED, use Hold to feed to bring the
  filament to the extruder, read this number and enter it as Feed distance.

**Setup tests**: three buttons, Full test, Buffer travel test and Pressure test; see 7.0.

**Statistics** (from v0.8.8 between the status and Controls): the totals are described in 10.5. The red
"Clear statistics" asks for confirmation first; "Show log ▾ / Hide log ▴" on the right toggles the live
log. It starts hidden, and the phone remembers your last choice.

**Other sections**: Settings (section 10), WiFi, Firmware update (section 11).

**Circled question marks beside settings** (v0.11.8): the fields that are easiest to get wrong have a **?**
beside them. Tap it to open an explanation under that field; tap it again, or another ?, to close it.
These fields have one: Pressure level, Load/unload current level, Holding current, Buffer parking amount,
Jam length, Release motor when idle, Reverse motor direction, Steps per mm, API key, Filament sensor,
No-feed alarm, Feed on after the sensor triggers, Lock load/unload distances, Release the gear when K1
unloads, Without a marker, detect unloads by retraction length. The names of the other fields say enough;
details are in section 10.

**Language switch** (v0.11.9): the **English / 中文** button at the top right switches the language of the
web page. It is a setting of the feeder itself (stored in flash), so every phone and computer that opens
the page sees the same language; **push notifications** and settings error messages switch too. The small
screen and the log stay in English (the screen font has no Chinese). The default is Chinese.

---

## 7. Setup tests, pressure level and path calibration

### 7.0 Setup tests (start here on the first install)

From v0.11.3 a test cannot start while printing or paused. If the printer starts printing during a test,
the test stops and leaves the nozzle target temperature and extruder enable state that the print is using;
a test that ends normally while idle still restores the original temperature.

The web page's "Setup tests" section has three buttons, and the screen menu has the same three items
(Full test / Buffer test / Pressure test).

| Button | What it does | Time | Filament / heating | Settings it changes |
|---|---|---|---|---|
| **Full test** | Runs the buffer travel test, then the pressure test with the new parking amount, then gives a summary | About 4–8 minutes | About 30cm, heats | Parking amount, pressure level, holding current; turns off "Release motor when idle" if it is on |
| **Buffer travel test** | Pushes the carriage to the end to measure its travel and sets the parking amount (7.3) | About 20 s | No filament, no heating | Parking amount |
| **Pressure test** | Finds the lowest currents that still push and hold; adds 1 level to pushing and 2 steps to holding as margin (7.2) | About 4–8 minutes | About 30cm, heats | Pressure level, holding current |

**First install**: bring the filament to the K1 extruder (the K1 toolhead sensor must show filament), then press "Full test".

**Pre-checks** (the same for all three; if one fails the test does not start, and the web page says why):
the motor has 24V, the feeder has filament, the feeder is READY or STAGED, the K1 can be reached, the K1 is
not printing, the K1 toolhead sensor shows filament.

**Stop**: press the same button again, any key on the feeder, or "Stop" on the web page.

**Result**: the web page shows pass or fail, the measured values, and what to check on a failure; the
settings are saved automatically. Common failures:

| Web page shows | Meaning |
|---|---|
| Fed 30 mm and the carriage never moved | The K1 extruder does not grip the filament, or the tube is not connected properly |
| Could not find the end of the travel | No signal from the comb sensor |
| The two measurements differ too much | The tube is moving or catching; fix it in place and test again |
| Travel too short | The PTFE tube is too short or bent too tightly |
| The current pressure level cannot push to the new parking position | Appears when only the buffer travel test ran: the measurement itself is valid; just run the pressure test next |
| No pressure level could push the carriage | The path is jammed |
| The holding current cannot hold against the printer | The gear does not grip the filament firmly |

> With "Release motor when idle" on, the buffer does nothing (see 7.1). It is off by default from v0.8.1; if
> you turned it on by hand, the full test turns it off and says so in the result. When you run only one of
> the other two tests, turn it off in the settings yourself.

### 7.1 Pressure level

The pressure level is the motor current. It decides two things: the force for pushing, and **the force with
which the gear holds the filament at standstill**.

| Level | L1 | L2 | L3 | L4 | L5 | L6 | L7 | L8 | L9 | L10 |
|---|---|---|---|---|---|---|---|---|---|---|
| Current mA | 250 | 300 | 350 | 400 | 450 | 500 | 550 | 600 | 650 | 700 |

The stock firmware runs at 600mA (about L8), with about half that at standstill.

This firmware splits the current into three uses, because the needs differ a lot:

| Use | Setting | Why separate |
|---|---|---|
| Buffer top-ups | Pressure level | Pushes only 10mm at a time, but thousands of times per print |
| Holding at standstill | Holding current (mA) | Draws power for the whole print; the main source of heat |
| Load/unload / manual moves | Load/unload current level | Hundreds of mm at a time against the friction of the whole path; only a few times a day |

The pressure test measures the first two. The third keeps a higher default (L8): the pressure test does not
measure what long loads and unloads need, and unloading with the low measured current would slip.

**Why holding force matters**: if the gear cannot hold at standstill, the printer pulls the filament straight
from the spool through the gear, and the buffer does nothing at all; it is as if the feeder were not there.
So **"Release motor when idle" must be off** for the buffer to work. To save power while not in use, use
"Standby" (9.6) instead: it only releases when the K1 certainly cannot pull filament.

### 7.2 Pressure test (pushing and holding, in two parts)

"Is the current enough" is really two questions, and only the second one causes heat all print long. So they
are measured separately, each with its own margin (1 level for pushing, 2 steps for holding; see "Margin" below):

| | Pushing | Holding |
|---|---|---|
| What it tests | Can the motor push the carriage to its parking position | Can the gear at standstill hold against the printer's pull |
| If it fails | Top-ups cannot keep up, ending in a jam alarm | The printer pulls straight from the spool; the buffer is useless |
| Setting it decides | **Pressure level** | **Holding current (mA)** |
| Heat all print long | No (only during the 0.5 s of a top-up) | **Yes** |
| Filament needed | **None** | Yes, about 50–70mm per step |

One press of "Pressure test" runs both parts in turn. **Requirements**: the pre-checks in 7.0 (from v0.8.1
also: the K1 toolhead sensor must show filament). Before the holding part, the nozzle heats to the set
temperature automatically; afterwards the original target temperature is restored.

**Part 1: pushing (no filament used)**

```
For each level:
  1. Ask the K1 to enable its extruder (anchors the other end of the filament)
  2. The feeder pulls back by itself to remove the slack stored in the buffer
  3. Let go, and the normal top-up loop refills the buffer to the parking position
  4. Refilled, a sensible distance, and the carriage did not stall on the way → pass
```

Step 2 only pulls back the parked part, at most "parking amount + 6mm", and stops as soon as the carriage is
home, so it never pulls the filament in the K1 extruder.

**Part 2: holding (uses filament)**

The pressure level stays at the result of part 1, and the holding current climbs this ladder:
**60 → 90 → 120 → 150 → 180 → 200 → 250 → 300 → 350…mA** (+50mA per step up to 700). Steps above the run
current are skipped.

Each step (from v0.8.3):

```
  1. The feeder empties the buffer itself, then the top-up loop fills it    ← start: just topped up
  2. The K1 extrudes a fixed length (about 2.5 top-ups' worth)
  3. The K1 extrudes 2mm at a time until the carriage is home              ← end: one more top-up done
  4. Compare "fed by the feeder" with "used by the K1"
```

Start and end are both "just topped up", so if the gear holds, the two should be equal. Pass: **the
difference is at most 3mm or 5% (whichever is larger)**.

- Fed **less** than used: the printer pulled filament from the spool straight through the gear; the holding current is too low.
- Fed **more** than used: the carriage spring pushed the stock back past the gear; the holding current is too
  low as well. The larger the parking amount, the tighter the spring, and the more likely this is.

> Up to v0.8.2 the test just extruded a fixed length and compared the ratio. But a top-up is always a whole
> round (about 19mm with a 15mm parking amount), so extruding 48mm triggers either 2 or 3 rounds, and the
> ratio could only be about 77% or 121%, depending on where the carriage happened to start. Results measured
> then (including the 150mA / 90mA minimums) are only a rough guide.

**Both parts search the same way**: a coarse pass first (every second step; for holding, every other rung of
the ladder). After the first pass, test one step lower to find the real minimum, **repeat** that test to rule
out luck, then add the margin and save automatically.

**Margin** (from v0.8.4):

| | Margin | Example |
|---|---|---|
| Pushing (pressure level) | **+1 level** | Minimum L1 → saves L2 |
| Holding (holding current) | **+2 steps** (on the ladder above) | Minimum 60mA → saves 120mA; minimum 150mA → saves 200mA; minimum 200mA → saves 300mA |

Holding gets one extra step because the test only measures about a minute of steady, slow pulling; it does not
cover the retractions, fast extrusion and vibration of a whole print. If the result would exceed the run
current, it stops at the run current.
Up to v0.8.3 both parts added only 1 step.

> Example (2026-09-21, parking amount 15mm): pushing minimum L1 → L2; holding at 60mA passed twice (fed 1.6
> and 1.8mm more than used, 3mm allowed) → +2 steps = **120mA**.

> Neither test exists in the stock firmware. **The stock calibration measures the path length**, not the
> current (see 7.4). Older versions had "tensioning" and an "unload-style auto calibration"; those were a
> design mistake. The carriage is a spring arm that stores slack: pulling back can only empty it, never push
> it out, so that check could never succeed and only ground the filament all the way to the limit. Removed in v0.5.0.

### 7.3 Buffer parking amount (overfeed)

**How it works**: the filament is held at both ends: at this end by the feeder gear (holding current), at the
other by the K1 extruder. The extra filament the feeder pushes in has nowhere to go. It is compressed in the
tube and presses against the outside of the bend, which tightens the tube; the tight tube pulls the carriage
(the tube fitting) outward, and the space it opens holds exactly the extra filament. So:

- **Carriage pushed out = the buffer holds stock**; the further out, the more stock
- When the printer uses filament, it uses this stock first; as the stock shrinks, the tube relaxes and **the
  spring pulls the carriage back home**, pushing the stock toward the printer at the same time
- **Back home = stock used up**; when it reaches the home switch, the feeder tops up

The printer "pulling" the filament does not drag the carriage away: the filament slides freely in the tube and
fitting, and the carriage only feels the tube tightening or relaxing. Pulling the filament directly actually
relaxes the tube, so the carriage moves toward home.

When the carriage returns home, a top-up starts; after the carriage leaves home it feeds **"Buffer parking
amount" more** and stops. This value decides where the buffer normally parks within its travel (about 21mm
in total); each top-up is about "2–3mm before leaving home + parking amount".

Parking near home (value too small) has two drawbacks:

- Too little stock, so the printer uses it up quickly. Between the carriage reaching home and the feeder
  topping up, the printer is pulling directly against the held gear, which is when it works hardest
- Top-ups become very frequent

Parking further out, the spring actually helps push filament toward the printer, and the printer only has to
overcome the friction in the tube. In a 57-minute test print with a 2mm parking amount there were 1117 top-ups
(the carriage came home on average every 4.3mm used); a larger parking amount cuts this proportionally.

**Protection**: if the comb sensor gives no signal for 3mm in a row while overfeeding, the carriage has reached
the end (or the motor can no longer push). The feeder stops at once and logs it, instead of pushing on until the
filament kinks. So a larger value does not grind filament; but parked at the outer end there is no room to absorb
the printer's retractions, so for real use leave some distance.

**Measured travel** (v0.8.1): after each top-up, the `topup` field of the web API `/api/status` records that round:

| Field | Meaning |
|---|---|
| `home_mm` | How much was fed before the carriage left home |
| `over_mm` | How much was actually fed after it left home (normally the parking amount; less when it hits the end) |
| `last_edge_mm` | At which mm of the overfeed the comb last gave a signal = where the carriage actually stopped |
| `edges` | Number of comb sensor signals in this round |
| `stalled` | 1 = this round stopped because it hit the end |

Also, `comb_edges` is the number of comb signals since the last top-up, and `buf_full` is the firmware's
"buffer holds stock" decision. When it hits the end, the log says
`buffer: carriage stopped moving X mm into the overfeed (last comb edge at Y mm, left home after Z mm, N edges)`;
Y is the largest usable parking amount.

**Buffer travel test** (v0.8.1):

1. Asks the K1 to enable its extruder; the feeder pulls back to remove the stock and lets the carriage go home
   (at most 30mm, stops as soon as it is home, never pulls the filament in the K1 extruder).
2. Pushes at the load/unload current at 10mm/s: first notes how much was fed before the carriage left home, then
   keeps pushing until the comb sensor gives no signal for 3mm in a row, that is, until the end.
3. Repeats once. If the two runs differ by more than 3mm, the result is unreliable.
4. Takes the shorter run as the travel; **parking amount = travel − 5mm** (rounded down, at least 3, at most 25).
   The outer 5mm is left for the printer's retractions (slicer retraction about 1mm, PAUSE 3mm).
5. Removes the stock again and lets the normal top-up loop park at the new position with the current pressure
   level. If it cannot get there, it tells you to run the pressure test.

It pushes with the load/unload current because it measures the travel of the mechanism; whether the current
pressure level can reach it is for the pressure test to decide.

### 7.4 Path length calibration (automatic; the stock calibration's counterpart)

With "Lock load/unload distances" off, there is no calibration button to press. When you **press UP in STAGED to
load**, and halfway the K1 toolhead sensor changes from "no filament" to "filament", the feeder knows the real
distance from the gear to the toolhead sensor. It then sets **Feed distance** to "measured value + path
calibration margin" and saves it (the margin is 20mm by default and can be changed in the settings). The log says:

```
load: K1 toolhead sensor triggered after 468 mm, feeding 10 mm more
path calibrated: gear -> K1 toolhead sensor is 468 mm, load length now 488 mm (+20 margin)
```

This needs "Stop loading at the extruder" on, and the toolhead sensor must show no filament when the load
starts (the log says why arming succeeded or failed).

Once the distances are right, tick **"Lock load/unload distances"** on the web page. Locking has three effects:

1. A load still stops when it reaches the K1 sensor, but no longer overwrites "Feed distance".
2. Full unloads no longer learn and overwrite the unload reference, so a stuck unload cannot spoil the next distance.
3. Loads and full unloads use the web page's "Feed distance" and "Retract distance" directly. Changing these two
   numbers by hand still works.

---

## 7.5 Printer link (Klipper / Moonraker)

The feeder reads the printer's status every 0.2 seconds: print state, extruder speed, temperatures and one
filament sensor. If the data is more than 3 seconds old, the link counts as lost; the features below switch
off and the feeder works as a plain feeder. The only exception is the print lock: if the last state seen before
the link dropped was "printing", the lock stays (see the table below).

| Setting | Description |
|---|---|
| Printer name | The name used for the printer on the web page, screen, log and notifications; default `Printer`. Letters, digits and common symbols only (the screen font has no Chinese), at most 12 characters. On the K1 used here, set it to `K1` |
| Address / port | Moonraker's address. Plain Klipper usually uses 7125; on a stock K1 both 7125 and 4409 work |
| API key | Normally empty. Only fill it in when the status bar shows "refused (needs API key)"; see section 7 of [`installation.md`](installation.md). **A wrong key makes a working connection get refused.** The web page never shows the stored key; saving it empty keeps it, and the "Clear API key" button deletes it |
| Filament sensor | The Klipper sensor that decides "the filament has reached the extruder". Only switch-type `filament_switch_sensor` sensors are supported (an encoder-type `filament_motion_sensor` reports "no filament" after boot until the filament moves, which would make the feeder think the extruder is empty and skip top-ups, so it is not listed). **Auto**: if there is only one, it is used; with several, names containing toolhead / extruder / nozzle / head / tool are preferred, and otherwise the last one in the config (on a K1 it picks `filament_sensor_2` on the toolhead). The list shows the sensors the printer actually has; "No sensor" feeds by distance only |

| Feature (each can be turned on or off on the web page) | Behavior |
|---|---|
| Lock keys and web moves while printing | While the K1 prints, loading, unloading and calibration are disabled; "Stop" still works. No lock while the K1 is paused. If the link drops during a print, the lock stays (screen "LOCKED: K1 offline") until the K1 reports it is not printing |
| On a jam, pause K1 and notify | On STUCK!, if the K1 is printing, sends PAUSE and the notification "PBF: jam" ("PBF: no feed" for the no-feed alarm) |
| No-feed alarm after this much extrusion (v0.11.0) | While the K1 prints, if the printer has extruded the set amount (default 100mm) and the buffer never emptied and the feeder never topped up → STUCK!, with pause and notification as in the row above. It means the filament is not coming through the feeder: the extruder slips or is clogged, the new filament of a refill was not taken in, or the carriage sensor is broken. Measured with the printer's `print_stats.filament_used` (extrusion), not with time, so heating, leveling and slow layers do not trigger it. On a K1 print, a top-up happened about every 20mm, at most 27mm apart. 0 = off |
| No top-ups while the nozzle is not heated (v0.11.1, always on) | When the printer's nozzle target temperature is 0 there are no top-ups, and a running top-up stops. The screen shows `K1 heater off` (from v0.11.7 replaced by `K1 printing: buffer on` once it heats again), and the log says `buffer: not topping up, K1 heater is off ...; carriage home/out`. If the carriage returns home or leaves home during this time, although the printer uses no filament, a line `buffer (held): carriage back home/left home` is logged (at most 8 lines each time): a carriage that suddenly springs back home usually means the tube has come loose from the extruder fitting. Klipper refuses cold extrusion anyway, so with a target of 0 the printer cannot use this filament. The K1's END_PRINT turns the heater off as its first step and only then retracts 3mm, moves the toolhead to the right and lowers the bed; so at the end of a print the feeder already stands still even though Klipper still says "printing", and it never pauses a finished print because of a jam. While the link is down, the last heater state read is kept (from v0.11.2); if the feeder has never connected since boot, or the printer link is off, it tops up as before |
| Relax the buffer while the nozzle is not heated (v0.11.6, always on) | While the buffer is parked, the carriage spring is held out by the tight tube, and this pull hangs on the extruder's tube fitting the whole time; releasing the gear in standby does not remove it (the filament is held by the pinch wheel and does not slide back). So once the nozzle target is 0 and the carriage has been out and still for 2 seconds, the feeder slowly pulls the parked part back at 10mm/s and **stops as soon as the carriage is home**, at most "parking amount + 6mm": any further would pull the filament in the extruder. The screen shows `relaxing buffer XXmm`, and when done the log says `buffer: relaxed X mm with the K1 heater off, carriage home`. If the end-of-print retraction or toolhead moves push the carriage out again, it relaxes again, at most 3 times per heater-off. When the K1 heats again, relaxing stops; with the carriage home, the normal top-up refills it (about one 17mm round). A key press stops it, and it does not relax again automatically during this heater-off. If the carriage is still out at the limit, it stops and logs `relax pulled X mm and the carriage is still out`, which points to a problem with the carriage sensor. A nozzle-wipe macro at the start of a print that turns the heater off to cool, or a pause that turns the heater off, also relaxes once and tops up again; this does not affect the print |
| Stop loading at the extruder | During a distance load, as soon as the filament sensor chosen above sees the filament, the feeder feeds "Feed on after the sensor triggers" more (default 10mm, at most 5000mm) and stops: above 40mm at the manual feed speed, the last 40mm slow. The set distance becomes a limit. If the sensor is far from the extruder (next to the spool, say), set this distance to that tube length |
| Follow K1 retractions (not while printing) | While the K1 is not printing, when the extruder retracts faster than 3mm/s, the feeder turns backwards at the same speed (FOLLOW) and stops when the K1 stops |
| Two-way sync on G-code request | While the K1 is not printing and `M117 PBF:FOLLOW` is active, the feeder follows both forward loading and backward unloading; long retractions do not switch to FREE. Upper or lower case both work |
| Notify when the filament runs out | When the tail leaves the feeder (RUNOUT), sends the notification "PBF: filament ran out" |
| Release the gear when K1 unloads | When the unload G-code sends `M117 PBF:FREE`, the motor is switched off and the gear spins freely (FREE); see 9.5 |

- Notifications use the Mobileraker Companion format (`M117 $MR$:title|body`); the K1 needs Companion
  installed to receive them. The web page has a "Test notification" button.
- If the toolhead sensor already shows filament when a load starts (the filament was already in the extruder),
  the sensor is not used and the feeder feeds by distance only.
- **On the K1 used for this guide, "Stop loading at the extruder" is currently off** (turned off on 2026-09-20 by
  habit; the filament is inserted by hand instead; the firmware default is on). When it is off, a distance load
  feeds only the set distance; to bring the filament to the extruder, control it yourself with "Hold to feed".
  It can be turned back on at any time on the web page.

---

## 8. Alarms and errors

| Screen | Cause | Fix |
|---|---|---|
| STUCK!: fed XXmm, buffer still empty (up to v0.8.0: buffer still pulled) | A top-up went past the jam length (default 80mm) and the carriage is still home, not pushed out. Usually the tail is caught on the spool, or the gear slips. For top-ups that start within 3 minutes after a preload, the limit is jam length + path length (manual refill, see section 9) | Check the spool and the path; press any key to clear |
| Pressure test: no level worked | Not even L10 passed | Check whether the tube is too long, sharply bent or jammed, or the spool drags too much |
| Pressure test: heat the nozzle first | The nozzle is not hot, and Klipper does not allow extrusion | Heat the nozzle to the filament's temperature first |
| ERROR: tip stuck: XXXmm pulled, still in the gear | Unload until empty reached its limit (measured unload length + 20mm) and the tail is still in the gear. Usually a bulge from tip forming catches, or the load/unload current is too low; or this unload needed 20mm or more beyond the last measured length | Pull the filament by hand to help it out, or press any key to clear and double-tap DOWN again; if it happens often, raise the load/unload current level |
| ERROR: fed XXXmm, K1 never saw the filament | A load fed the whole distance and the K1 toolhead sensor never reacted. The filament may be bent, stuck halfway, or not in the sensor | Check the path by hand, fix it and load again. Only a "full load" reports this; a distance jog does not |
| NO POWER | No 24V | Connect the power supply; the driver chip is set up again automatically once power returns |
| RUNOUT | Filament used up, the tail has left the gear | Normal state; the K1's runout handling takes over; inserting new filament makes the feeder grip it automatically |

With the buzzer on, alarms beep repeatedly; press any key to stop.

---

## 9. Typical workflows

**Loading new filament (steps 1–4)**
1. Insert the filament at the inlet → grips automatically, preloads 100mm → STAGED.
2. UP tap → Feed distance to the K1 → READY. The first time you do this, the path length is calibrated (7.4).
3. Press "Load" in Mobileraker; as the K1 takes filament, the feeder tops up automatically.
4. Start the print; during the print the feeder tops up automatically.

**Runout (step 6)**
When the tail passes the inlet, the feeder only logs it; once the tail has left the gear → RUNOUT, the motor
relaxes and the K1's runout handling takes over. Inserting new filament makes the feeder grip and preload it,
back to step 2.

**Manual refill (joining new filament during a print)**
1. The filament runs out and the tail leaves the gear → RUNOUT.
2. While the print continues, insert new filament at the inlet → grips and preloads automatically → because the
   K1 is printing, it returns to READY by itself.
3. The feeder pushes the new filament all the way down the tube until it butts against the old tail and pushes
   out the carriage, then tops up as usual (the new filament follows the old tail).
4. **Keeps pressure on as the joint nears the extruder** (v0.11.0): after the old tail leaves the feeder gear,
   the joint reaches the extruder gear once the printer has extruded about "path length" more.
   From 100mm before the expected arrival, the feeder pushes the carriage to the end of its travel every 1.5
   seconds, so the new filament keeps pressing against the extruder gear until the extruder takes it in.
   It counts as done once the joint is 100mm past the extruder and the printer has emptied the buffer twice
   more (log `refill: done`).
   If it is never taken in, the feeder stops pushing harder after 30mm, keeps the pressure, and the "no-feed
   alarm" pauses the print. The web page status line shows "refill: pushing the new filament into the extruder".

This stretch can be as long as the whole tube, with the carriage home the whole time, which would normally count
as a jam. So **top-ups that start within 3 minutes after a preload completes** use a relaxed jam check of "jam
length + path length" (path length = Feed distance, plus "Feed on after the sensor triggers" when "Stop loading
at the extruder" is on). There is still a limit, so a real jam does not feed forever. The log says
`refill: ... allowed`. After 3 minutes the normal check applies again.

Join the new filament before the K1's runout sensor triggers. Once it triggers, the K1 pauses and unloads, and the
feeder stays at STAGED; then just load with an UP tap (no pressure is kept in this case, and none is needed).
The pressure timing is estimated from the "path length" (Feed distance in section 10, plus "Feed on after the
sensor triggers"); if Feed distance is accurate, the timing is too.

**Manual refill tips**

1. **Insert new filament as soon as you see RUNOUT (or get the "PBF: filament ran out" notification).** The old
   tail keeps moving toward the printer; join too late and the K1's runout sensor triggers first and pauses the
   print, and it becomes a normal filament change.
2. **Cut the front of the new filament square and straighten it**, with no hook or flattened end. The front has
   to butt against the old tail and later be taken in by the extruder.
3. Push the new filament in until the feeder grips it (the IN2 box on the screen turns solid), then let go. Do
   not pull it afterwards, and there is no need to keep holding it.
4. **Set "Feed distance" accurately.** Too short, and the pressure starts too late, when the joint is already at
   the extruder; too long only starts the pressure early, which does no harm.
5. While the pressure is on, the feeder clicks about every 1.5 seconds (the motor pushing the carriage to the
   end). This is normal.
6. While the joint passes the extruder, the print may under-extrude a little (the seam, the extruder taking hold
   of the new filament). Check those layers afterwards.
7. If the extruder never takes the new filament, the "no-feed" alarm pauses the print after the K1 extrudes about
   100mm more (see below). Push the new filament toward the extruder so it grips, then **first press any key on
   the feeder to clear the alarm, and only then resume the print on the K1**.
8. If the K1 has already paused when you join the new filament (for example, the runout sensor has triggered):
   no pressure is kept automatically. Load the filament to the extruder with an UP tap, check that the extruder
   takes it, then resume the print.

**Jam (step 5)**
A top-up goes past 80mm and the carriage is still home (not pushed out) → STUCK! and the motor stops; if the K1 is
printing it pauses automatically and a notification goes to your phone.
Exception: within 3 minutes after a preload; see manual refill above.

**No feed (v0.11.0)**
While the K1 prints, it extrudes 100mm and the buffer never empties → STUCK! (`no feed: K1 used 100mm`), automatic
PAUSE and the notification "PBF: no feed".
Check whether the extruder takes filament, whether the nozzle is clogged, and whether the new filament of a refill
is gripped. When fixed, press any key to clear, then resume the print on the printer.

**No-feed alarm tips**

1. **Always clear the alarm with any key on the feeder first, then resume the print on the K1.** During the alarm
   the feeder's motor is released; resume without clearing and the K1 pulls straight from the spool, the buffer
   does nothing, and this alarm will not trigger again.
2. The 100mm threshold was measured on a K1: during printing a top-up happened about every 20mm extruded, at most
   27mm apart. With another printer or way of printing, if it often alarms by mistake, raise the threshold (for
   example 150–200); 0 = off.
3. It assumes the buffer works normally: "Release motor when idle" off, the gear gripping the filament. When you
   print without the buffer on purpose, set the threshold to 0.
4. It is only checked while the K1 is "printing". Pauses, filament changes and manual extrusion do not count; every
   top-up resets the count.
5. It needs the link to the K1. While the link is down it is not checked; after reconnecting it counts from zero.
6. Pausing and notifying follow the "On a jam, pause K1 and notify" switch. With it off, the feeder only stops at
   STUCK! and does not pause the K1.

**Unloading (step 7)**
1. Run your unload G-code in Mobileraker. As the K1 retracts, the feeder turns backwards in sync (FOLLOW).
2. Once more than 20mm has been followed, the feeder decides this is an unload and not a print retraction, and
   **switches to STAGED and pauses the buffer by itself** (one beep). This step matters: without the pause, right
   after the retraction empties the buffer, the top-up loop would push the filament straight back into the
   extruder and fight the unload.
3. DOWN double tap → unload until empty → UNLOADED. A single tap retracts the set distance instead.

Also, as long as the K1 reports that its extruder is empty (toolhead sensor shows no filament and not printing),
the feeder does not top up: the far end of the tube is empty then, so no amount of pushing can push the carriage
out, and it would only push on until the jam alarm. The screen shows `K1 extruder empty`.

**Buffer back on automatically when printing**
If the K1 starts printing while the feeder is at STAGED (for example, you just swapped the tube and forgot to
switch back), the feeder switches to READY and turns the buffer on by itself; otherwise the K1 would pull straight
from the spool for the whole print.

**Swapping the PTFE tube**
In READY, UP double tap → STAGED (buffer paused); moving the tube does not trigger top-ups. When done, feed with an
UP tap, or bring the filament into place with Hold to feed.

---

## 9.5 Releasing the gear during unloads (K1 unload handshake)

Tip forming in an unload G-code moves the filament back and forth several times (the stock sequence is four
±5mm moves). Even though FOLLOW now works both ways, it still reacts through polling every 0.2 seconds; it
cannot keep up with such quick short moves and tends to catch up only after the G-code has already reversed.
**Simply releasing the gear so it spins freely**, and letting the printer pull, is much cleaner.

### Only the Mobileraker unload G-code changes

`M117` writes to Klipper's `display_status.message`, a standard object the feeder reads when it polls.
**The printer's `printer.cfg` needs no change at all.**

```gcode
M117 PBF:FREE     ; release the gear
G4 P400           ; wait two polling cycles for the feeder to react
  ...(tip forming, retraction)...
M400              ; wait until the moves are really done
M117              ; clear the message → grip again
```

The feeder looks for `PBF:FREE` or `PBF:FOLLOW` in the message (case-insensitive); anything else, including an
empty message, counts as normal standby.
The complete unload G-code: the K1 version is in [`tip-forming.md`](tip-forming.md); for other Klipper printers use
the generic version [`tip-forming-generic.md`](tip-forming-generic.md). Both show how to use `M117`; replace the
tip forming moves with your own.

If you still need to retract after FREE, switch to FOLLOW first, so the feeder actively pulls back in sync. The
current unload G-code does this: after tip forming it switches to FOLLOW and takes up the last 60mm. To retract
several hundred mm, write it the same way with a longer distance:

```gcode
M400
M117 PBF:FOLLOW   ; switch from FREE to actively following backwards
G4 P400           ; wait two polling cycles
G1 E-250 F1800    ; example: an extra long retraction; adjust distance/speed for your printer
M400
M117              ; clear the handshake marker when everything is done
```

`PBF:FOLLOW` is an explicit command, so it is not affected by "switch to FREE after 25mm". So that a marker you
forgot to clear cannot keep feeding during a real print, FOLLOW does not run while the K1 reports "printing".

A load's `E200` can be synced both ways like this:

```gcode
M83
M117 PBF:FOLLOW
G4 P400
G1 E200 F300.0
M400
M117
```

`G4 P400` gives the feeder two polling cycles to receive the marker; at the end, `M400` must first wait for the
load to finish before `M117` clears the marker.

### Fallback: detect unloads by retraction length when there is no marker

Not every unload goes through Mobileraker: an unload from the K1 touch screen runs `QUIT_MATERIAL`, which has no
`M117`. So there is a second path: **when followed retractions add up to more than the set length (default 25mm),
the feeder releases its gear**, and grips again 3 seconds after the retraction stops. If an `M117` marker appears
in the meantime, the marker takes over (it knows exactly when the unload ends).

On this printer `PAUSE` retracts only 3mm, and normal print retractions are smaller, so neither triggers it.

### What the feeder does

- Sees `PBF:FREE` → enters **FREE**: motor off, one beep, gear spins freely.
- Sees `PBF:FOLLOW` → feeds at the same speed on forward E moves of the K1 and retracts at the same speed on
  backward ones; long retractions do not switch to FREE.
- Message cleared / changed, timeout, any key, K1 starts printing → grips again.
- If the K1 reports that the toolhead is already empty when it grips again, it stays at **STAGED** and **keeps
  gripping** until the next state change; then a single press of the unload key pulls the filament back to the spool.

### Safety measures

| Measure | Behavior |
|---|---|
| Timeout (default 60 s) | Grips again automatically when the time is up, and does not release again until the message changes |
| Any key | Grips again at once, and likewise stays locked against automatic release |
| K1 starts printing | Grips again at once |
| Link to the K1 lost | Grips again at once (stale data always means grip) |
| 60 s after FREE triggers | No automatic top-ups, even if FREE ended early and the carriage is already home; FOLLOW is still allowed. As soon as the K1 starts a real print, the guard ends and top-ups resume |
| Message is already `PBF:FREE` at boot | **No action**; the message must be cleared once before it counts |
| End of a retraction-detected unload | 3 seconds without retraction (the gaps in tip forming moves are all < 1 second, so they are not mistaken for the end) |

The last two exist because **an `M117` message stays** and does not disappear by itself. If an unload G-code is
cancelled, `PBF:FREE` stays on the display, so after a timeout grip the feeder must stay locked, and an existing
message at boot must be ignored. A gear that does not grip means the K1 pulls the filament straight from the spool
through the gear, and the buffer does nothing.

### Important limitation

**Releasing the gear only suits unloads where the printer just pulls the filament out of its own extruder**
(about 100mm). If the unload G-code has a long retraction of several hundred mm, that part must be done by the
motor turning backwards (FOLLOW); otherwise nothing takes up the returning filament at the inlet side, and it
tangles into a knot.

### Fallback channel (optional)

If the printer has `[gcode_macro PBF]` (`variable_mode: 0`), you can use
`SET_GCODE_VARIABLE MACRO=PBF VARIABLE=mode VALUE=1/2/0` instead, where 1 = FREE, 2 = FOLLOW, 0 = clear.
Both channels are supported. **`M117` is the default and is all you need; you do not have to add this.**

### Manual test

The **Release gear / Grip gear** button under "Controls" on the web page, or the serial commands `free` / `grip`.
A manual release is subject to the timeout too.

---

## 9.6 Standby (gear released while the K1 is idle)

In READY the gear must hold the filament with the "Holding current" all the time for the buffer to work (7.1), so
the motor draws power and makes heat all day. Holding is really only needed when the K1 **can pull filament**.

**The rule**: Klipper does not allow cold extrusion, so anything that moves filament (printing, loading or
unloading from Mobileraker or the K1 screen) must **heat the nozzle first**. Conversely, when the K1 is not
printing, not heating, and the nozzle is too cold to extrude, it cannot pull filament, and the gear can let go;
as soon as heating starts the gear grips again, minutes before the first extrusion.

**Enter**: in READY, when all of these conditions **hold continuously** for the set number of minutes (default 5):

| Condition | What is checked |
|---|---|
| The K1 is neither printing nor paused | `print_stats` |
| The nozzle is not heating | Target temperature = 0 |
| The nozzle is too cold to extrude | Klipper's `can_extrude` (below about 170°C on a K1) |
| The feeder is not moving | No top-up, manual move or test; and no key press or web action during that time |

**Wake up** (grip again, back to READY) immediately when any of these happens:

- The K1 starts printing or pauses
- The K1 starts heating the nozzle, or the nozzle is still hot enough to extrude
- The K1's extruder moves
- Any key on the feeder (only wakes it, nothing else), or any web page action (wakes it and then is carried out)
- "Standby" or the K1 link is turned off

Releasing the gear does **not** let the stock slide back: the filament is held by the pinch wheel, and in testing
the carriage was still out after 20 minutes of standby. So from v0.11.6 the buffer is relaxed as soon as the nozzle
heater goes off (the feature "Relax the buffer while the nozzle is not heated" in the table of section 7.5), and
the buffer is usually already empty when standby starts. After waking, once the K1 heats with an empty buffer, the
feeder tops up once and the buffer is back at its parking position. If the carriage returns home or leaves home
during standby, a line `buffer (held): ... (K1 standby, standby)` is logged too, to help find when the tube came out.

**When the K1 is off or the link is down**: the feeder keeps the last K1 state it read. If the K1 was idle and cold
before it went off, the feeder enters or stays in standby as usual; once the K1 restarts and connects, the
conditions above apply. If the feeder never reaches the K1 after boot, it treats the K1 as off.

> The only risk: the link drops while the feeder is in standby, and the K1 starts printing during that time. The
> gear is released then, so the K1 pulls straight from the spool (as if there were no feeder; it still prints),
> and the feeder grips again once the link is back and it sees the K1 printing.

**No standby when the "K1 link" is turned off in the settings**: the feeder would never know when to wake up.

The web page status shows a countdown (for example "standby in 4:32"); once in standby, the state shows "Standby
(gear released)". The log says:

```
standby: K1 idle for 5 min (not printing, not heating, too cold to extrude)
READY -> STANDBY: K1 idle: gear released
STANDBY -> READY: woke: K1 heating
```

Difference from "Release motor when idle": that setting releases as soon as READY goes idle, so the buffer fails
during K1 prints too; standby only releases when the K1 certainly cannot pull filament.

---

## 10. Settings

From v0.11.3, a numeric setting that is out of range, contains non-digits, or is `NaN` / infinite is rejected and
the old value is kept. If any field in a web page save is invalid, none of the batch is applied; the error message
names the field.

The settings on the web page are in four groups, each with its own background color: **Motor and buffer** (green),
**Speed** (pink), **Screen and buzzer** (blue), **Printer link** (beige). Where the table below and the web page say
"K1", the page actually shows your printer name. "Lock load/unload distances" is in the printer link group, next to
"Path calibration margin". The two drop-downs, Pressure level and Load/unload current level, apply as soon as you
change them; the other fields need "Save settings".

| Setting | Default | Range | Description |
|---|---|---|---|
| Pressure level | L7 | L1–L10 | See section 7; best decided by the pressure test. Changing this drop-down on the web page **applies immediately**, no save needed |
| Buzzer | On | | |
| Preload length | 100mm | 0–300 | Length fed automatically once the gear grips |
| Feed distance | 500mm | 1–10000 | UP tap / web Feed distance. Calibrated automatically when a load reaches the filament sensor (7.4) |
| Retract distance | 600mm | 1–10000 | DOWN tap / web Retract distance |
| Unload limit (measured) | Auto | — | The distance limit of "unload until empty" (DOWN double tap, serial `unload`). **Updated automatically after a successful unload until empty from READY**: the distance the tip actually needed to clear the gear **+ 20mm** (×1.3 up to v0.11.4). Once the tip has cleared the gear, the last 15mm are always finished, regardless of the limit. Until measured, it is estimated as "preload + feed distance + 300mm"; with "Lock load/unload distances" on, "Retract distance" is used directly |
| Jam length | 80mm | 30–500 | If a top-up goes past this length and the carriage is still home (not pushed out), it alarms. Within 3 minutes after a preload the path length is added (manual refill) |
| No-feed alarm after this much extrusion | 100mm | 0, 30–2000 | See 7.5. 0 = off |
| Buffer parking amount | 8mm | 0–25 | How much more is fed after the carriage leaves home; decides where the buffer normally parks (7.3). The limit is 25 to allow measuring the travel; for real use stay within the recommended value |
| Pressure test nozzle temperature | 230°C | 150–300 | Heated to automatically before the pressure test; the original setting is restored afterwards |
| Release motor when idle | Off (On up to v0.8.0) | | **Must be off to use the buffer**: when on, the K1 pulls filament straight from the spool through the gear and the buffer does nothing. Off means warmth at standstill, see 7.1; to save power use standby below |
| Standby while K1 is idle (gear released) | On | | Releases the gear while the K1 is not printing, not heating and the nozzle is cold; grips again as soon as the K1 heats. See 9.6 |
| Idle time before standby | 5 minutes | 1–120 | How long the conditions above must hold |
| Holding current | 300mA | 50–700 | Current with which the gear holds the filament at standstill; never above the run current. Drawn for the whole print; most of the heat comes from here |
| Load/unload current level | L8 (600mA) | Same as pressure level, L1–L10 | Current for loading, unloading, manual moves and gripping on insert. "Same as pressure level" = the same as the pressure level (serial value 0). These moves drag the filament through the whole path and need far more force than a buffer top-up |
| Manual feed speed (called "feed speed" up to v0.8.7) | 30mm/s | 5–60 | Distance feed, UP tap (the last 40mm always slow) |
| Top-up speed (called "refill speed" up to v0.8.7) | 20mm/s | 5–60 | Buffer top-ups during a print; speeds up 1.5× after 1 second |
| Retract speed | 30mm/s | 5–60 | |
| Steps per mm | 250 | | Measured at about 250–261 |
| Reverse motor direction | Off | | |
| Screen brightness: normal / READY / during top-ups | 80/10/80% | | See section 2 |
| Path calibration margin | 20mm | 0–250 | Margin added to the measured value during path calibration (7.4) |
| Lock load/unload distances | Off | | When on, the feed distance and the unload reference are never rewritten automatically; full unloads use "Retract distance" directly (7.4) |
| Release the gear when K1 unloads | On | | See 9.5. Relies on `M117 PBF:FREE` in the unload G-code; no setting to change on the printer |
| Release timeout | 60 s | 5–600 | The longest time after a release before it always grips again. The real action takes only about 10 s |
| Without a marker, detect unloads by retraction length | On | | See the fallback part of 9.5. Used for unloads from the K1 touch screen |
| Retraction that counts as an unload | 25mm | 10–200 | Retractions adding up to more than this count as an unload. PAUSE retracts only 3mm |
| Printer name | Printer | 12 letters/digits | See 7.5 |
| API key | Empty | | See 7.5 |
| Feed on after the sensor triggers | 10mm | 0–5000 | See 7.5 |
| Web page language | 中文 (Chinese) | 中文 / English | Switched with the button at the top right of the web page (section 6); push notifications use the same language. API / serial setting name `english` (1 = English) |
| Filament sensor | Auto | Auto / a sensor on the printer / No sensor | See 7.5. Serial `set sensor auto`, `set sensor none` or `set sensor filament_sensor_2` |

---

## 10.5 Statistics (stored in flash)

The web page's "Statistics" section shows the total length fed and retracted, the number of top-ups, jam alarms,
unload jams, UART glitches and powered hours.

This data is stored in flash, but designed to be very cautious:

- A fixed-size block that never grows
- Counts are kept in memory and written to flash **at most every 10 minutes** (plus once at the end of a print).
  NVS has limited erase cycles, and a single print has thousands of top-ups; writing each one would wear it out quickly
- Every read checks the magic, the version and the checksum; on any mismatch it resets to zero, and suspicious data
  is never shown
- Resets automatically after a firmware update (the field definitions may have changed)
- If it cannot write, it counts only this boot and says so on the web page; **a statistics failure never affects feeding**

The "Clear statistics" button (red, asks for confirmation first) resets it at any time.

---

## 11. Firmware updates and restoring stock

- **Web update**: on the web page under "Firmware update", choose `firmware.bin` and upload it; the feeder restarts when done.
- **USB update**: connect the computer with USB-C and run `pio run -e pbf_custom -t upload` in `firmware/pbf-custom`.
- **Restore stock (with all original settings)**: connect the computer with USB-C and write your own backup back as in
  section 12 of [`installation.md`](installation.md).

---

## 12. Current limitations (v0.11.9)

- v0.11.9 (English web page, notification language) builds and passes 204 host checks; the English and Chinese web
  pages were clicked through in a local preview at phone width (no Chinese left on screen in English mode; the switch
  button saves the setting and reloads). OTA flash on 2026-10-21: all 45 existing settings kept, the new language
  setting defaults to Chinese; on the hardware, switching to English gave English settings error messages, and
  switching back to Chinese worked. **No English push notification has been received yet.**
- The v0.11.7 message fix was confirmed on the hardware on 2026-10-21: after the K1 heats again, `K1 heater off` is no longer shown.

- v0.11.7 (replaces the `K1 heater off` hold message after heating) and v0.11.8 (setting help question marks) build and
  pass 202 host checks; the web page question marks were clicked through in a local preview at phone width, in dark and
  light mode (tapping ? does not tick the option next to it). OTA flash of v0.11.8 on 2026-10-21: all 45 settings kept,
  statistics reset, connected to the K1 at boot.
- The v0.11.6 end-of-print relax was confirmed on the hardware on 2026-10-21: after the print ended and the heater went
  off it pulled back 15.6mm and the carriage returned home; all 9 prints since the flash completed normally.
- v0.11.6 "Relax the buffer while the nozzle is not heated" builds and passes 193 host checks; OTA flash on 2026-10-18
  02:20, all settings kept, statistics reset. At boot the K1 had finished printing, the heater was off and the carriage
  was out; about 2 seconds after connecting to the K1 it relaxed by itself: after 2.8mm the carriage was home. **Not yet
  verified**: relaxing when the heater goes off at the end of a print (the length should be close to the parking
  amount), the first top-up after heating at the next print start pushing out the carriage normally, and the tube no
  longer slipping out of the fitting in the first minutes of a print. This fix is based on reasoning (the October jams
  all happened in the first minutes of a print, and the guess is that the buffer pulled the tube loose during the end
  of the print and the idle time); if it still comes out on the first layer, the cause lies elsewhere (a worn fitting
  or tube end, or the print start moves themselves).
- The v0.11.5 unload limit (measured value + 20mm) builds and passes 170 host checks; after the OTA flash on 2026-10-10
  it started normally with settings kept; **the unload limit is not yet verified on the hardware**. The margin is much
  smaller than ×1.3: when unloading from READY, the carriage may be parked at different points, so the needed length
  can differ by up to one carriage travel (about 20mm on the K1 Max), which is close to this margin. If it differs by
  more than 20mm, it falsely reports `tip stuck`; press any key to clear and double-tap DOWN again.
- v0.11.4 adds regression checks for double UP during manual feed and retract; builds and passes 161 host checks; starts
  normally with settings kept after an OTA update; the physical keys and motor operation are still to be confirmed on
  the hardware.
- The four v0.11.3 fixes (test cleanup protection, menu protection while printing, stale FREE marker lock, numeric
  validation) pass the build and host regression checks. The FREE lock is cleared only when the link is up, Klipper is
  ready and the FREE marker has really been seen cleared; waiting at boot and link loss no longer count as cleared.

- The buffer loop and retraction following (FOLLOW) were tested on the K1 and work. The print lock was confirmed during
  real prints (keys blocked, lock kept when the link dropped during a print, unlocked after reconnecting); the jam pause
  triggered for real on 2026-09-26 and paused the print; the pressure test ran to the end on the hardware on 2026-09-21
  (both pushing and holding). **There is no log yet that confirms stopping at the extruder.**
- For the buffer to work, "Release motor when idle" must be off; while the K1 is in use (printing, heating) the motor
  stays warm (see 7.1), and while the K1 is idle standby takes over (9.6).
- Standby (v0.9.0) was verified on the hardware on 2026-09-24: enters after the idle time, wakes within about 1 second
  when the K1 heats (`M104 S100`), enters again after the heater is turned off, web actions wake it, and a locked phone
  screen (hold_stop) does not. On 2026-10-11 it was confirmed that starting a print directly wakes it and top-ups work
  after heating. **Not yet verified**: waking with a physical key, and the buffer being empty right at wake-up.
- Retraction following has a delay of about 0.2–0.4 seconds: when the K1 starts retracting, the feeder first pushes a
  little forward, and when it stops the feeder retracts a little extra; the buffer makes this up afterwards.
- When the feeder is not connected to the K1, it does not know whether the K1 is printing; do not press keys during a print.
- The screen is English only.
- Releasing the gear (9.5) was confirmed on the hardware on 2026-10-03: released during a K1 unload, gripped again after
  the unload, and the fallback (long retraction without a marker) also triggered.
- Printer name and automatic sensor choice (v0.10.0) were confirmed on the K1 (it picked `filament_sensor_2`), and the
  new screen layout (arrow animation, IP position, boxes) works; but it has only been tested on the K1, and nobody has
  tried other Klipper printers yet.
- The no-feed alarm and the refill pressure (v0.11.0) have not been triggered on the hardware yet: that needs a print
  that really stops feeding, or a manual refill during a print. The 100mm threshold comes from one real print (a top-up
  about every 20mm, at most 27mm apart).
- No top-ups while the nozzle is not heated (v0.11.1) was confirmed after the flash on 2026-09-27: with a K1 target of 0
  and the carriage home, there are no top-ups. At a real print end on the same day at 02:03: a top-up that was starting
  when the heater went off did stop, but after the print the link to the K1 dropped, and v0.11.1 treated the lost link
  as "unknown" and pushed 82mm again. v0.11.2 fixes this (the last state read is kept while the link is down), but has
  not met a real link loss yet. After the v0.11.2 flash it was confirmed that with the K1 connected at boot and the
  heater off there are no top-ups, and both lines written at the same time stay in the log. Top-ups resuming after
  heating again were confirmed (2026-10-11, v0.11.5, print started and top-ups normal after heating). What this
  prevents is the hard push and the pause **after** the tube has come out; why the tube came out on 2026-09-26 is not
  confirmed yet (the guess: with the buffer parked far out, the end-of-print retraction plus the toolhead moving to the
  far right left the carriage no travel to give, and the pull landed on the extruder fitting). From v0.11.6 the buffer
  relaxes as soon as the heater goes off, so the fitting is no longer pulled by the buffer at the end of a print and
  while idle; but the end of print retracts and moves the toolhead right after turning the heater off, almost at the
  same time as the relaxing, so the pull in those few seconds is only reduced, not removed.
- Top-up logging during a print (from v0.11.0) is at most one summary line per minute. The log keeps only the last 80
  lines; with one line per top-up before, other events were flushed out within minutes.
- The buffer travel test and the full test ran to the end on the hardware on 2026-09-21: carriage travel 20.4mm,
  parking 15mm, run current L2, holding 90mA. v0.11.3 changed the protection at the end of tests, and they have not
  been rerun on the hardware since.

---

## Appendix: serial commands (USB, 115200 baud)

`help`, `status`, `set <name> <value>`, `defaults`, `load`, `unload`, `stop`, `jog <mm>`, `ptest`, `btest` (buffer travel test), `fulltest` (full test), `beep`, `free`, `grip`, `verbose on|off`, `reboot`

Setting names: `pressure buzzer steps_mm preload_mm load_mm unload_mm lock_move_distance stuck_mm idle_release invert_dir load_speed buffer_speed unload_speed standby standby_min brightness brightness_ready brightness_active k1_enabled printer_name printer_host printer_port api_key sensor lock_printing pause_on_stuck stop_at_extruder extruder_extra_mm retract_sync notify_runout hold_ma move_level full_when_displaced overfeed_mm ptest_temp path_margin_mm free_on_unload free_timeout_s free_on_retract free_retract_mm`
