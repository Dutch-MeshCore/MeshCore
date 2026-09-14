# Design: on-device `ota branch` channel switch

**Date:** 2026-09-14
**Repo:** `Dutch-MeshCore/MeshCore` (firmware, device side only)
**Base branch:** `origin/dmc-observer-dev` @ `7e3e8b76` (canonical; local `dmc-observer-dev`
@ `80f426ee` was stale and must not be used).
**Feature:** let an operator switch which OTA release channel (stable vs dev) a device
pulls updates from, at runtime, with a persisted `ota branch` command — instead of the
channel being fixed at build time.

---

## 1. Background / current behaviour

Observer builds (`*_observer_mqtt`, repeater + room server) already have a pull-OTA:

- `ota check` / `ota update` in `src/helpers/CommonCLI_Observer.cpp`, guarded by
  `#if defined(WITH_MQTT_BRIDGE) && defined(OTA_MANIFEST_BASE)`.
- The work is done by `Board::otaFromManifest(current_ver, dry_run, reply)`
  (base virtual `src/MeshCore.h`, real impl `ESP32Board::otaFromManifestImpl`).
- The manifest URL is `<OTA_MANIFEST_BASE>/<OTA_VARIANT>.json`. Both macros are baked
  in by `build.sh` (`-DOTA_MANIFEST_BASE=…`, `-DOTA_VARIANT=<env>`); the base defaults
  to `https://ota.dutchmeshcore.nl/mqtt/v` and is overridable via `OTA_MANIFEST_BASE_URL`.
- build.sh comment, verbatim: *"this URL IS the channel: a device only ever sees updates
  published under the base it was built with."*

So today the channel is 100% compile-time. There is **no runtime override** and **no
`ota branch` command**. A stable-flashed device can never look at the dev channel (or
vice versa) without re-flashing.

## 2. Goal

Add a persisted, runtime channel selector so an operator can do:

```
ota branch            -> report current channel + resolved base URL
ota branch stable     -> pull from the stable channel
ota branch dev        -> pull from the dev channel
ota branch default    -> clear override, follow the build's native channel
```

The selection changes only **where the device looks for updates**. It does not change the
running image's reported version (`ver` / MQTT `firmware_version` / SNMP stay truthful).

## 3. Non-goals

- Server-side channel plumbing. Already implemented in the **separate** `DutchMeshCore-OTA`
  repo on branch `feat/dev-stable-ota-channels` (also on `origin`): nginx serves
  `/mqtt/dev/v/` + `/mqtt/dev/fw/`, poller segregates by GitHub release tag
  `observer-mqtt-dev`. **External dependency (out of scope here):** that branch must be
  merged → deployed, and Cloudflare must get the same "Always Use HTTPS: Off" +
  bot-challenge Skip rules for `ota.dutchmeshcore.nl/mqtt/dev/*` that the stable
  `/mqtt/v/*` and `/mqtt/fw/*` paths already have. Until then, `ota branch dev` +
  `ota check` returns an HTTP/connect error (the honest failure), while stable is
  unaffected.
- Arbitrary base-URL override (`ota base <url>`). Rejected as YAGNI.
- Per-build one-shot revert after update. Rejected: scope is "same as `ota update`".

## 4. Design

### 4.1 CLI (`CommonCLI_Observer.cpp`, beside the `ota check`/`ota update` block)

Parse `ota branch` and, optionally, a trailing `stable` | `dev` | `default`.

- No arg → report, e.g.
  `> channel: dev (override), base https://ota.dutchmeshcore.nl/mqtt/dev/v`
  or `> channel: stable (native), base https://ota.dutchmeshcore.nl/mqtt/v`.
- `stable` / `dev` / `default` → set the prefs field, persist, reply `> channel set to <x>`.
- Anything else → `ERR: usage ota branch [stable|dev|default]`.
- Same guard as the OTA block; on non-OTA builds → `ERR: online OTA not supported on this build`.
- **Scope:** identical to `ota update` — reachable from any admin path (serial / mesh
  admin / MQTT). Reads (no-arg) allowed everywhere.

### 4.2 Storage

Add `uint8_t ota_channel` to the observer repeater/room-server `NodePrefs`:

| value | meaning |
|-------|---------|
| `0`   | native (default — follow the build's own channel) |
| `1`   | stable |
| `2`   | dev |

Persisted via `ConfigSerializer::def("ota_channel", ota_channel)` in that struct's
serialize list. Because `/prefs.json` uses **named JSON keys**, an existing file without
the key loads the C++ default (`0`/native) — fully back-compatible. Do **not** memset the
struct and do **not** use byte-offset append (ConfigSerializer gotchas for this fork).

### 4.3 Build wiring (`build.sh`, observer path)

Today build.sh bakes only `OTA_MANIFEST_BASE` (= the build's own channel). Add two more
`-D` flags to **every** observer build so a device knows both channels' bases regardless
of which one it was built for:

```
-DOTA_MANIFEST_BASE_STABLE='"https://ota.dutchmeshcore.nl/mqtt/v"'
-DOTA_MANIFEST_BASE_DEV='"https://ota.dutchmeshcore.nl/mqtt/dev/v"'
```

`OTA_MANIFEST_BASE` stays = the native channel (unchanged), so `native` resolves to the
right place on both stable and dev builds. The dev URL matches `DutchMeshCore-OTA`'s
`feat/dev-stable-ota-channels` exactly.

### 4.4 Resolution helper

A single function maps the stored enum + the compile-time macros → an effective base URL:

```
0/native -> OTA_MANIFEST_BASE
1/stable -> OTA_MANIFEST_BASE_STABLE  (fallback OTA_MANIFEST_BASE if undefined)
2/dev    -> OTA_MANIFEST_BASE_DEV     (fallback OTA_MANIFEST_BASE if undefined)
```

Fallback covers legacy/local builds that only define `OTA_MANIFEST_BASE`. The macros are
global `-D`, so the helper can live wherever prefs + macros meet (the CLI/observer TU).

### 4.5 Threading the base into the OTA path

`otaFromManifest` gains a `const char* manifest_base` parameter:

- base virtual `src/MeshCore.h:75`
- `ESP32Board::otaFromManifest` + `otaFromManifestImpl` (`ESP32Board.h`/`.cpp`); the impl
  uses `manifest_base` in place of the literal `OTA_MANIFEST_BASE` when building `murl`
  (both the plain-HTTP check path and the HTTPS update path). The
  `#if !defined(OTA_MANIFEST_BASE)…` "not configured" guard stays.
- Call sites resolve the base from prefs and pass it:
  - `CommonCLI_Observer.cpp` `ota check` and the `ota update` pre-check.
  - `examples/simple_repeater/MyMesh.cpp` and `examples/simple_room_server/MyMesh.cpp`
    deferred flash path (`beginDeferredOtaUpdate` → later `otaFromManifest(…, false, …)`).
    These **re-resolve from prefs at flash time**, so the channel selected at check time
    is the one that flashes (no need to stash the URL across the defer).

### 4.6 Reporting

Unchanged. `ota check`'s existing "new base" wording already fires when the target
differs from the running version, so switching channel then `ota check` naturally shows
the new channel's candidate build.

## 5. Testing (native test env)

Pure-logic unit tests (board OTA mocked; no network):

1. **Arg parsing:** `stable`/`dev`/`default` set the expected enum; unknown → usage error;
   no-arg → report string contains the resolved base.
2. **Resolution:** native/stable/dev → correct macro URL; undefined dev/stable macro →
   fallback to `OTA_MANIFEST_BASE`.
3. **Prefs round-trip:** default is `0`/native; a `/prefs.json` without `ota_channel`
   loads as native; set→serialize→load preserves the value.

Windows PlatformIO note: a failing gtest env shows as ERRORED — run the built
`program.exe` directly to see RED output.

## 6. External dependency checklist (tracked, not built here)

- [ ] Merge `DutchMeshCore-OTA` `feat/dev-stable-ota-channels` → master, deploy.
- [ ] Cloudflare: replicate the `/mqtt/v/*` + `/mqtt/fw/*` "Always Use HTTPS: Off" and
      bot-challenge Skip rules for `/mqtt/dev/v/*` + `/mqtt/dev/fw/*`.
- [ ] Confirm the dev-channel firmware workflow publishes to the `observer-mqtt-dev` tag
      (matches the poller's `DEV_RELEASE_TAG`).

## 7. Open item for implementation

Feature branch base: re-create `claude/ota-branch-device-switch-1103b6` off
`origin/dmc-observer-dev` @ `7e3e8b76`. The branch currently carries 4 unrelated
main-based commits (incl. "ci: surface … build workflows on main") — decide whether to
preserve those elsewhere or start the feature branch fresh before writing code.
