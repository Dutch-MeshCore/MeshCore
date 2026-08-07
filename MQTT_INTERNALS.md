# MQTT Bridge Internals

Developer-facing notes on how the MQTT observer feature is structured in the codebase: source files, the seams that keep it isolated from upstream MeshCore code, and how on-device settings are migrated across firmware versions. For user-facing setup and CLI reference, see [MQTT_IMPLEMENTATION.md](MQTT_IMPLEMENTATION.md).

## Files

### Core Implementation
- `src/helpers/bridges/MQTTBridge.h` - MQTT bridge class definition
- `src/helpers/bridges/MQTTBridge.cpp` - MQTT bridge implementation
- `src/helpers/MQTTPresets.h` - Preset definitions, CA certificates, and lookup functions
- `src/helpers/MQTTDefaults.h` - Compile-time defaults for fresh `/mqtt_prefs`
- `src/helpers/MQTTMessageBuilder.h` - JSON message formatting utilities
- `src/helpers/MQTTMessageBuilder.cpp` - JSON message formatting implementation
- `src/helpers/JWTHelper.h` - JWT token generation for Ed25519-based authentication
- `src/helpers/CommonCLI_Observer.cpp` - All observer CLI command handling (MQTT, WiFi,
  timezone, NTP, OTA, SNMP, alerts)

### Integration seams with upstream code

The observer feature is kept out of upstream-tracked files through three mechanisms:

- **CLI hook methods** — upstream `CommonCLI.cpp` delegates to three `CommonCLI`
  methods defined in the fork-owned `CommonCLI_Observer.cpp`: `handleObserverCommand()`,
  `handleObserverSetCmd()`, and `handleObserverGetCmd()`. Each returns `true` if it
  consumed the command, otherwise the upstream parser runs. Only these three call
  sites touch upstream CLI code.
- **Callback virtuals** — observer behaviour needed from the application is exposed
  as default-no-op virtuals on `CommonCLICallbacks` (e.g. `restartBridgeSlot`,
  `isMqttBridgeRunning`, `syncMqttNtp`, `onAlertConfigChanged`, `sendAlertText`,
  `resolveAlertScope`, `beginDeferredOtaUpdate`). The example apps override them
  behind `#ifdef WITH_MQTT_BRIDGE`.
- **Separate settings file** — observer settings (MQTT slots, WiFi, timezone, SNMP,
  radio watchdog, fault alerts) live in the `MQTTPrefs` struct persisted to
  `/mqtt_prefs`, keeping `NodePrefs` / `/com_prefs` aligned with the upstream layout.

Remaining integration points in upstream files:
- `examples/simple_repeater/MyMesh.{h,cpp}`, `examples/simple_room_server/MyMesh.{h,cpp}` -
  bridge/alerter/SNMP wiring and packet-feed hooks, guarded by `#ifdef WITH_MQTT_BRIDGE`;
  plus the `createObserverPacketManager()` call in each constructor (see below)
- `src/helpers/CommonCLI.{h,cpp}` - the three CLI hooks, `MQTTPrefs` load/save/migration
- `src/Dispatcher.{h,cpp}` - radio watchdog block, guarded by `#ifdef WITH_MQTT_BRIDGE`

### Capture vs. duty-cycle throttling

RX processing needs a free packet from the static pool before `logRx()` (and thus the
MQTT uplink) can run — `Dispatcher::checkRecv()` silently discards received data when
the pool is empty. Because the outbound queue holds pool packets with no expiry,
duty-cycle throttling can park the entire pool waiting on TX budget, capping capture at
the TX rate — and the parked repeats absorb every budget refill, starving the node's
own CLI responses and making it un-administrable over the mesh. Observer builds
therefore use `RxReservePacketManager` (fork-owned,
`src/helpers/RxReservePacketManager.h`): below the RX reserve (a quarter of the pool)
it sheds only low-priority outbound (multi-hop flood repeats, adverts, trace), keeping
the node's own responses/ACKs queueable; below a smaller emergency floor it sheds
everything to keep capture alive. Queued packets still untransmitted 30 s past their
scheduled time are expired at dequeue, so under throttle the queue holds only fresh
traffic and admin responses reach the trickle of TX budget. Non-observer builds keep
the upstream pool behavior.

### Runtime construction and slot memory

- **Deferred construction** — `MQTTBridge` is heap-allocated in each app's `begin()`
  (`bridge = new MQTTBridge(...)` in `MyMesh.cpp`) rather than held as a static member,
  because constructing it at static-init time crashes on ESP32 classic.
- **Runtime slot array** — `RUNTIME_MQTT_SLOTS` (`MQTTPresets.h`) is 6 with PSRAM and 3
  without, saving ~1.2 KB of heap on non-PSRAM boards. `MAX_MQTT_SLOTS` stays 6 on every
  build because it fixes the persisted `MQTTPrefs` layout, so slot config survives moving
  firmware between board classes. Three runtime slots suffice without PSRAM:
  `_max_active_slots` caps those boards at 2 live connections, leaving one spare for
  reconfiguration. Configured slots past the cap report `(inactive)`.
- **Buffers** — the 768-byte JWT `auth_token` is inline in every `MQTTSlot`, not allocated
  per JWT-auth slot. What varies is the MQTT client's TX/RX buffer: 896 bytes (the minimum
  that fits a CONNECT plus a 768-byte JWT) uniformly on PSRAM boards to limit
  fragmentation from mixed allocations, and 896 or 512 per slot on non-PSRAM boards so
  non-JWT slots leave smaller holes across teardown/recreate cycles. The large
  JSON/raw-packet buffers go through `psram_malloc()`, which prefers PSRAM and falls back
  to internal heap.

### Reconnection, backoff, and circuit breaker

The client's own auto-reconnect is disabled (`setAutoReconnect(false)`); the bridge drives
reconnection per slot.

- Backoff ladder: 10 s → 30 s → 60 s → 120 s → 300 s, staggered by 3 s × slot index so
  slots don't all handshake at once.
- The ladder resets only after a connection has held for 2 minutes
  (`BACKOFF_STABLE_RESET_MS`), which is longer than the 75 s keepalive — a link that can't
  survive one keepalive round-trip keeps its earned rung instead of hammering TLS
  handshakes at the 10 s rung. CONNACK alone does not reset it.
- After 3 more failures at the top rung (~15 min) the slot's circuit breaker trips and
  routine reconnects stop. A tripped slot is probed once every 30 minutes (with a fresh
  JWT where applicable); a successful connect clears the breaker, as does reconfiguring
  the slot.
- Message retransmit timeout is 15 s — one retry inside esp-mqtt's 30 s outbox expiry,
  preserving at-least-once delivery while capping duplicates at one.

### Message building

- The `hash` field in `packets` messages is MeshCore's own packet hash,
  `Packet::calculatePacketHash()` — SHA256 over the payload type and payload (plus
  `path_len` for TRACE), truncated to `MAX_HASH_SIZE`. It is the same value the dispatcher
  uses, so uplinked hashes match the mesh.
- `score` is recomputed at publish time from the packet's SNR and length via the radio's
  `packetScore()`, so it matches the value the firmware used on receive.
- Timezone: the JChristensen/Timezone object (`_timezone_storage`, inline since the
  memory-defrag work) is kept current from `timezone_string` via `setRules()`, but
  `formatIsoTimestampForMqtt()` explicitly ignores it — every published timestamp, time,
  and date field is UTC off `gmtime()`, matching Python's
  `datetime.now(timezone.utc).isoformat()`. The timezone prefs therefore do not affect
  MQTT message content.

### Command namespacing

CLI commands sit at two levels. `bridge.*` is low-level and shared by all bridge types
(MQTT, RS232, ESP-NOW): `bridge.enabled` is the master switch, and `bridge.source` selects
which packet events non-MQTT bridges capture. The MQTT bridge ignores `bridge.source` in
favour of independent `mqtt.rx` / `mqtt.tx` controls. Everything MQTT-specific lives under
`mqtt.*` (shared settings), `mqttN.*` (per-slot broker config), `wifi.*`, and `timezone.*`.

### `/mqtt_prefs` file format

`/mqtt_prefs` is written with an 8-byte `MQTTPrefsHeader` (`magic`, `version`,
`payload_len`) followed by the raw `MQTTPrefs` payload. The magic is
`{0xF5, 'M', 'Q', 'P'}` — its leading non-ASCII byte can never collide with the first
bytes of a legacy (headerless) file, whose payload begins with the `mqtt_origin`
string. Bump `MQTT_PREFS_VERSION` when the payload layout changes incompatibly; a file
whose version this firmware doesn't recognize is left untouched and the in-memory prefs
fall back to defaults (no downgrade, no misread). `saveMQTTPrefs()` also refuses to
write while such a file is present (`_mqtt_prefs_hold`), so a `set` command after a
firmware downgrade can't clobber the newer config — observer settings changed in that
state simply don't persist. The frozen legacy layouts are pinned with `static_assert`s
in `CommonCLI.h`, so every target build re-verifies the fleet's file offsets.

Adding a field to the current version stays backward compatible: append it to the end
of `MQTTPrefs`. An older, shorter payload still loads and the missing tail keeps its
default; a newer, longer one is truncated harmlessly.

### Settings upgrade / migration

`loadPrefs()` handles every historical on-device format one-time at boot:
- **`/mqtt_prefs`** — if the file has the version header it is read directly. Otherwise
  it is a legacy headerless file and its layout is detected by size: pre-slot
  (`OldMQTTPrefs`), 3-slot (`ThreeSlotMQTTPrefs`), or the 6-slot layout shipped on
  `observer-firmware` back when it was named `mqtt-bridge-implementation-flex`
  (`Legacy6SlotMQTTPrefs`). Each is field-copied into
  the current compact `MQTTPrefs` and re-saved with the version header — which also
  drops the vestigial `_legacy_*` fields the flex layout carried mid-struct. This is a
  one-time rewrite; every deployed device performs it on its first boot of versioned
  firmware, after which all reads take the header path.
  The pre-slot (`OldMQTTPrefs`) copy maps the old single-broker keys onto slots:
  `mqtt.analyzer.us = on` → slot 1 `analyzer-us`, `mqtt.analyzer.eu = on` → slot 2
  `analyzer-eu`, and a configured `mqtt.server` / `mqtt.port` / `mqtt.username` /
  `mqtt.password` → slot 3 `custom` with those values preserved. Origin, IATA, message
  types, WiFi, and timezone carry over as-is.
- **`/com_prefs`** — a file written by fork firmware that predates the `MQTTPrefs` split
  (a zero-filled MQTT gap plus a trailing observer block) is detected by size; the
  trailing SNMP / radio-watchdog / fault-alert settings and the `rx_boosted_gain` /
  `flood_max_*` fields are recovered, carried into `/mqtt_prefs`, and both files are
  rewritten in the current formats.
- Settings the pre-split firmware stored *inside* the `/com_prefs` MQTT gap (the MQTT
  slot/WiFi config itself) are **not** recovered — users upgrading from firmware that
  old must re-enter their MQTT and WiFi configuration.
