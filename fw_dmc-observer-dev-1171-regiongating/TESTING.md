# Test plan for these prebuilts

Firmware: `v1.17.1-dev-dutchmeshcore.nl-observer-mqtt-<hash>`
Analyzer: **https://observers.elektrovodka.nl**

Covers two groups of changes:

- **A. New from the agessaman catch-up** (111 upstream commits, merged
  2026-08-29). Not previously on any DMC node.
- **B. New in the DMC region tree** since region gating landed.

Tests are ordered so a failure early tells you not to bother with the rest.
Anything marked **[critical]** is a change that could lose data or brick
configuration, and should be tested before these builds go near a real site.

---

## 0. Before you start

Flash the `-merged.bin` at `0x0` if the board has never run observer firmware
(the observer partition table differs from stock). Otherwise the plain `.bin`
over the existing install is enough.

Then confirm the node identifies correctly:

```
ver
```

Expect `v1.17.1-dev-dutchmeshcore.nl-observer-mqtt-<hash>`. The `-dev` marker is
what separates these from production nodes in the analyzer's roster, so if it is
missing, stop: you have the wrong build.

```
get wifi.status
get mqtt.status
```

Then find the node at **observers.elektrovodka.nl** in the roster. If it does not
appear within a couple of minutes, check the **Ingestion monitor** page before
suspecting the node.

---

## 1. [critical] Preferences survive the upgrade

**Why this matters most.** The catch-up moved observer preferences from the
binary `/mqtt_prefs` to JSON `/mqtt.json`. Deployed nodes migrate on first boot.
Two DMC-only settings had to be added to the new serializer by hand; if that was
wrong, they are silently lost on migration and reset on every boot.

**Test on a node that already has real configuration, not a fresh flash.**

Before flashing, record:

```
get mqtt.iata
get mqtt1.preset
get mqtt2.preset
get mqtt.filter.interval
get mqtt.config
get mqtt.neighbors
get mqtt.neighbors.interval
get wifi.ssid
get mqtt1.filter
```

Flash, reboot, and read every one of them back. **All must be unchanged.**

Then reboot a second time and read them again. A value that survives the first
boot but resets on the second means it migrated but is not being persisted, which
is the more subtle failure.

Pay particular attention to `mqtt.filter.interval` and `mqtt.config`: those are
the two that were not in the upstream serializer.

Also confirm WiFi credentials survived. `/mqtt_prefs` carried them too, so a
botched migration takes the node off the network entirely.

---

## 2. Node reaches the analyzer and reports sanely

On **observers.elektrovodka.nl**:

- **Roster**: node present, `last seen` advancing, model and firmware version
  correct (with `-dev`).
- **Detail → Telemetry**: battery, uptime, noise floor, heap, queue length all
  populated and plausible. Uptime should climb smoothly with no unexplained
  reboot markers.
- **Detail → Traffic / Signal**: packets arriving, RSSI/SNR scatter filling in.

Leave it an hour before judging the charts.

---

## 3. Group A: what the merge brought

### 3.1 New display commands

```
get display.timeout
set display.timeout 30
set display.flip 1
set display.flip 0
set display.timeout 0
```

`display.timeout <secs>` blanks the panel after inactivity; `0` keeps it lit.
`display.flip 1` rotates 180 degrees from the compiled orientation. Verify on the
physical screen. Then reboot and confirm both survived (they are stored in
`/mqtt.json`, so this doubles as a second migration check).

### 3.2 New broker preset

```
get mqtt.presets
```

Expect **36** presets, including the new `ntxmesh`. If you see 35, the preset
count constant is wrong again.

### 3.3 [critical] MQTT reconnection and token renewal

The merge changed how JWT credentials are renewed: the client is no longer
stopped to renew a token, a still-valid token is reused on ordinary reconnects,
and a live session is no longer bounced just to refresh. This is the change most
likely to show up as instability.

Test by forcing outages:

- Pull WiFi (or power the AP down) for 30 seconds, restore. The node should
  reconnect without a reboot.
- Repeat for 5 minutes, then for 20 minutes.
- Leave the node up for **more than 24 hours**, which is the JWT lifetime. Token
  renewal happens then, and the old failure mode was a visible disconnect.

In the analyzer, watch **Detail → Telemetry**: uptime must keep climbing across
all of this. A reset uptime means the node rebooted, which it should not.
`get mqtt.status` and `get mqtt1.diag` on the console give per-slot disconnect
counts and the last TLS/socket error.

### 3.4 NTP and clock correctness

```
get mqtt.ntp
get mqtt.ntp.diag
```

`ntp.diag` probes every configured server without changing the clock. Then check
that published timestamps are sane: in the analyzer, an observation's time should
match wall-clock UTC. Timestamps drifting hours off used to indicate the
zone-naive bug, which these builds do not have.

### 3.5 Memory headroom on non-PSRAM boards

The neighbours JSON buffer is now allocated on first use rather than at bridge
start. On the non-PSRAM boards in this set (Wireless Tracker, Heltec V3, WSL3,
Xiao S3 WIO), compare `internal_heap` in **Detail → Telemetry** against the same
board on the previous firmware. It should be **higher** at idle, and should only
dip while a neighbours publish is in flight.

```
set mqtt.neighbors on
discover.scopes
```

Watch heap during that cycle. It should recover afterwards.

---

## 4. Group B: DMC region tree

### 4.1 Region gating

```
get dc.gate
get dc.gate.thresh
get dc.gate.hyst
set dc.gate 1
set dc.gate.thresh 70
set dc.gate.hyst 10
```

These three moved position in the source during the merge (upstream relocated
the surrounding radio commands to a new `CommonRadioPrefs` class; the DMC gating
commands stayed behind). **They are the single most likely thing to have broken
in the merge**, so check all six get/set forms respond correctly.

In the analyzer the region-gate readout lives on the **Airtime** tab (it renders
only when gating is enabled, so an absent panel means the node is reporting the
gate as off). It shows the state plus a duty chart with gate and recover
reference lines. Confirm those lines match the threshold and hysteresis you set.

To see gating actually engage you need real duty-cycle pressure, so this is a
busy-site test rather than a bench one.

### 4.2 Packet filter and its statistics

```
get mqtt1.filter
get mqtt.filter.interval
set mqtt.filter.interval 60
```

The **Packet Filter** section is also on the **Airtime** tab, below the region
gate. It should populate within a couple of intervals: KPI tiles, drops by reason
and by type, routing split, hash-size split and **Top Blocked**.
`set mqtt.filter.interval 0` disables the topic; confirm the section stops
updating, then re-enable it.

Both this and the region gate render only when filter data is present, so if the
whole Airtime tab is bare, suspect the `filter` topic rather than the panels.

Note that changing the interval restarts the bridge, so expect a brief
disconnect in the roster.

### 4.3 Config topic

Off by default, so nothing publishes until you opt in:

```
get mqtt.config
set mqtt.config on
```

Then, as an operator who owns this node, look at two tabs:

**Config tab**: the raw payload. Identity, advert intervals, the radio block,
repeat limits, the region-gate block and the region/scope table. Confirm it
contains **no** credentials, keys or WiFi settings. `owner_key` is a public key
and is expected.

**Inspector tab**: the settings **Checkup**, which grades the node against the DMC community
recommendations and is the most useful part of this feature. It evaluates twelve
items:

| Item | Recommendation |
|---|---|
| `flood_advert` | flood advert interval >= 47 h |
| `zero_hop_advert` | zero-hop advert ~240 min |
| `region_default` | must be set |
| `unscoped_flood` | denied |
| `region_scopes` | at least one tagged scope |
| `path_hash` | mode 1 (2-byte) |
| `loop_detect` | not off |
| `dutycycle` | derived from `radio.airtime_factor`; 10% is legal, above fails |
| `sf` / `cr` / `freq` / `bw` | SF7 / CR5 / 869.618 MHz / 62.5 kHz |

Non-ok items carry a suggested CLI fix command, except the radio settings.

This is a good end-to-end test of the whole chain: a value you change on the
console should change the Checkup verdict after the next config publish. Try it:

```
set dutycycle 25
```

`dutycycle` should flip to a failing state (above the 10% legal limit), then
return to ok after `set dutycycle 10`.

Note the polarity trap on unscoped flood: `region.wildcard_flood === true` means
unscoped flooding is **allowed**, which is the state the checkup advises against.

Turn `mqtt.config` back off afterwards if you do not want it publishing
continuously.

---

## 5. Regressions to rule out

These all worked before and must still work. The merge relocated a lot of CLI
handling, so they are worth a pass.

```
get radio
set radio 869.525,250,11,5
get dutycycle
set dutycycle 10
get af
get cad
get int.thresh
get agc.reset.interval
get multi.acks
get path.hash.mode
get rxdelay
get txdelay
get direct.txdelay
get radio.rxgain
```

All of these moved from `CommonCLI` into the new `CommonRadioPrefs` upstream.
Same names, same syntax; if any responds `Unknown command`, the merge dropped it.

### 5.1 [critical] FEM gain on V4: still works, but the storage moved

`set/get radio.fem.rxgain` and `radio.fem.txgain` still exist. Upstream moved
them out of `CommonCLI` into variant-specific board code (`47b0b7b3`);
`CommonCLI::handleCommand` dispatches to `_board->handleCommand()` before its own
command chain, so they behave exactly as before from the console. Boards
carrying them: heltec_v4, heltec_v4_r8, heltec_t096, heltec_tracker_v2,
station_g3_esp32.

On a **Heltec V4** (in this prebuilt set), check all four forms:

```
get radio.fem.rxgain
set radio.fem.rxgain off
get radio.fem.rxgain
set radio.fem.rxgain on
get radio.fem.txgain
```

**The migration is the part worth testing.** Before the refactor the value lived
in `NodePrefs.radio_fem_rxgain` (legacy binary prefs, offset 293). Now the
variant's `attachDynamicPrefs()` reads a `fem_rxgain` key from the custom
key-value store and calls `setLoRaFemLnaEnabled()` at boot. If those two stores
do not bridge on upgrade, a V4 that had LNA **on** can come up with it **off**:
the boot path does `strcmp(buf, "1")`, and an absent key leaves the buffer empty,
which reads as disabled.

That would be a quiet RX-sensitivity loss with no error anywhere, so:

1. On the **pre-merge** firmware, set `radio.fem.rxgain on` and confirm with a get.
2. Flash this build.
3. `get radio.fem.rxgain` **before changing anything.** It must still report `on`.

If it reports `off`, the pref did not migrate. Note it and set it again; the
command works, so this is recoverable, but every upgraded V4 in the field would
need the same treatment.

Also confirm the basics still behave: `set name`, `set repeat off/on`,
`reboot`, and that the node reappears in the roster afterwards.

---

## 6. Per-board notes

| Board | Watch for |
|---|---|
| Heltec Wireless Tracker | Screen should render. If blank, you flashed the wrong env: use `Heltec_Wireless_Tracker_repeater_observer_mqtt`, not `heltec_tracker_v1_1_*`. GPS is off after every reboot; `gps on` re-enables it and this is a known gap, not a fault. |
| Heltec V3 / WSL3 / Xiao S3 WIO | Non-PSRAM: **two TLS broker slots maximum**. A third configured slot shows `(inactive)`. Watch `internal_heap`. |
| Station G2 | Check FEM behaviour if fitted, given the removed CLI commands above. |
| T-Beam S3 Supreme | Has PSRAM: all six slots usable, and the neighbours table is not capped at 20. |
| LilyGo T3S3 | Non-PSRAM, same two-slot limit. |
| Heltec V4 | Baseline reference board; if something misbehaves everywhere, compare here first. |

---

## 7. What "passing" means

Minimum before these go to a real site:

1. Section 1 clean on a node with real prior configuration.
2. Section 3.3 clean across a 24-hour-plus run (JWT renewal without a reboot).
3. Section 4.1 all six commands responding.
4. Section 5 with no `Unknown command` other than the two documented FEM ones.

Sections 3.5, 4.2 and 4.3 are worth doing but are not blockers.

---

## 8. Reporting

Include the exact `ver` string, the board, and whether the node was upgraded from
a previous observer build or freshly flashed. The upgrade path is where the
interesting failures live, and a fresh flash will not reproduce them.
