# DMC devsync — `dmc-dev` @ v1.17.1.01

Handoff notes for whoever (agent or human) continues from the 2026-09-11 consolidation of
`dmc-dev`. Durable, fork-specific context that isn't obvious from the diff. See also
[`AGENTS.md`](../AGENTS.md) (contributor rules) and `docs/packet_filter_reference.md`.

## State after this work

`dmc-dev` was synced to `upstream/dev` (meshcore-dev) and every scattered DMC feature was folded onto
one branch, stamped **`FIRMWARE_VERSION v1.17.1.01`** (branding still applied at build time by
`build.sh` → `v1.17.1.01-dutchmeshcore.nl-<hash>`; upstream itself is still `v1.17.1`, so this is the
DMC patch revision on top). Delta vs the previous `dmc-dev`: **122 files, ~+5350/−650**, one merge
commit (`upstream/dev`) plus the feature commits below.

Consolidated onto `dmc-dev`:

| Feature | Origin | Notes |
| --- | --- | --- |
| Repeater packet filter | already on `dmc-dev` | `examples/simple_repeater/Filter.{cpp,h}` |
| Filter drop **stats** + probabilistic **soft cutoff** | ex-`dmc-dev-1172`; **closes PR #2** | `FilterStats.h`, `Limiter.h`, `test_filterstats`, `test_soft_limiter` |
| **Region gating** (`dc.gate.*`, #2960) | ex-`claude/pr-2960-*` | `RegionMap.*`, `Dispatcher.*`, `test_region_gating` (own env) |
| **Duty-cycle-from-frequency** default (`set dutycycle auto`) | **PR #5**, `Fixes #4` | `src/helpers/DutyCycleLimits.{cpp,h}`, `test_duty_cycle_limits` |
| `simple_secure_chat` prefs → ConfigSerializer | **issue #6** | `/node_prefs` (binary) → `/node_prefs.json` (text), migrated |
| `AGENTS.md` | **PR #3** | reconciled to this state |

**`dmc-dev-1172` and `claude/pr-2960-dmc-dev-1172-d8e58d` are now subsumed** by `dmc-dev` and can be
retired once the consolidation is confirmed good.

## Verification at consolidation

- Native: `pio test -e native` **112/112**, `pio test -e native_region_gating` **20/20**.
- Builds: `heltec_v4_repeater`, `Heltec_E290_repeater` (nRF52), `Heltec_Wireless_Tracker_repeater`,
  `heltec_v4_room_server`, `Heltec_v3_terminal_chat` all **SUCCESS**.
- **Known pre-existing failure:** `pio test -e native_kiss_modem` crashes at process init
  (`0xC0000409`, no gtest output) on **Windows/MinGW** — reproduced identically on the pristine base
  `dmc-dev` (`bb96c850`), so it is *not* from this work. That env compiles only `KissModem.cpp`. It
  passes on Linux CI (`run-unit-tests.yml`). Fixing it is a separate Windows-toolchain task.

## Gotchas for the next agent

- **Region-gating & duty-cycle CLI handlers live in `src/helpers/CommonRadioPrefs.cpp`, not
  `CommonCLI.cpp`.** Upstream (`41588d80`) moved the radio-pref handlers (`af`, `dutycycle`,
  `int.thresh`, `cad`, …) into `CommonRadioPrefs`, which uses an **accessor abstraction**
  (`getAirtimeFactor()`/`setDutyCycleAuto()`/…), not direct `_prefs->`. Any new radio/duty pref needs
  a `virtual` accessor pair on `CommonRadioPrefs` **and an impl in every subclass** — the repeater/room
  `RadioPrefs` in `src/helpers/CommonCLI.h` *and* the companion's in
  `examples/companion_radio/NodePrefs.h`. Missing the companion impl makes its `RadioPrefs` abstract
  and breaks `test_companion_node_prefs`.
- **Prefs persistence goes through `ConfigSerializer def()`**, never byte-offset append and never the
  legacy `loadPrefsInt` path (that is `/com_prefs`→`/prefs.json` migration only). `dc_gate*` is
  `def()`'d in the `RepeatPrefs` group; `dc_auto` in the `RadioPrefs` group.
- **Never `memset` a `ConfigSerializer`** (it's polymorphic — vtable + nested serializers). This now
  includes `simple_secure_chat`'s `NodePrefs`; defaults live in in-class initializers.
- **Every native gtest suite must define its own `main()`** (`::testing::InitGoogleTest` +
  `RUN_ALL_TESTS`). PlatformIO does not auto-inject one on this Windows/MinGW setup — a suite without
  it fails to link with `undefined reference to WinMain` (this bit PR #5's `test_duty_cycle_limits`).
- **Version-tag parsing takes the text after the last `-`** (`setup-build-environment/action.yml`), so
  `v1.17.1.01` is safe precisely because it has no hyphens. Keep it that way.
- **Windows native tests show `ERRORED` on any nonzero exit**; run the built `program.exe` under pio's
  environment (not bare Git Bash — that gives a spurious exit 127 from missing MinGW DLLs on PATH).

## Propagation TODO (not done here — scope was `dmc-dev`)

The whole point of consolidating onto `dmc-dev` was to make these clean. Each deserves its own branch
and plan:

- **`dmc-dev-packetlog`**: recreate as `dmc-dev` + the 2 packet-logging `platformio.ini` toggle
  commits. Invariant: `MESH_PACKET_LOGGING` must **never** be enabled on companion USB/BLE/WiFi envs
  (it writes to `Serial`, which the companion protocol also uses) — repeaters/room-servers only.
- **`dmc-observer-dev` (MQTT)**: merge the **new `dmc-dev`** into it (never `upstream` directly —
  `dmc-dev` already resolved the DMC-vs-upstream conflicts). ⚠️ The observer branch **already carries
  region gating**, so expect overlap in the region-gating files and its `filter` MQTT `region_gate`
  block; keep the observer's MQTT superset. Also **re-drop** the non-MQTT build workflows that a
  `dmc-dev`→observer merge re-introduces (observer is MQTT-only: keep `firmware-builder.yml`, drop the
  generic `build-{companion,repeater,room-server}` + `build-dmc-repeater` workflows).

## Toolbox coupling (Dutch-Meshcore-Toolbox)

Firmware CLI/pref changes need matching updates in the sibling `Dutch-Meshcore-Toolbox` repo
(`src/` only — its `docs/` is Vite build output, never commit it):

- **Region gating is no longer observer-MQTT-only.** The toolbox currently gates the `dc.gate` UI to
  observer firmware (`ConfigForm.tsx`, `panelHelpContent.ts`, `useSerialDevice.ts`); with v1.17.1.01
  a plain repeater answers `get dc.gate`, so the gate must broaden (feature-detect, which
  `useSerialDevice.ts` already does at ~line 189) and backup/restore should round-trip `dc.gate.*`.
- Register `v1.17.1.01` in `src/lib/cli/firmwareRegistry.ts`; add an en/nl(/de) changelog entry; the
  `set dutycycle auto` mode and frequency-derived default may warrant help-text and an optional
  `getMaxDutyCyclePercent` mirror in `src/utils/configUtils.ts`.

## Open items on GitHub

Nothing was pushed. After `dmc-dev` is fast-forwarded (user-gated) and pushed, close:
`Dutch-MeshCore/MeshCore#2` (filter stats — subsumed), `#3` (AGENTS.md), `#5` (duty-cycle-from-freq),
and issues `#4` (fixed by #5) and `#6` (secure_chat prefs migrated — resolved via **full
ConfigSerializer migration**, the chosen option).
