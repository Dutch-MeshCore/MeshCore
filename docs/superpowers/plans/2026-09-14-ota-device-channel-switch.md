# On-device `ota branch` channel switch — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let an operator switch which OTA release channel (stable/dev) an observer device pulls updates from at runtime, via a persisted `ota branch [stable|dev|default]` command, instead of the channel being fixed at build time.

**Architecture:** A header-only resolver maps a 1-byte `NodePrefs::ota_channel` selector (0=native, 1=stable, 2=dev) to one of three compile-time base URLs baked by `build.sh` (`OTA_MANIFEST_BASE` = native, `OTA_MANIFEST_BASE_STABLE`, `OTA_MANIFEST_BASE_DEV`). `otaFromManifest` gains a `manifest_base` parameter; every OTA call site resolves the base from prefs, so both the `ota check` path and the deferred flash path honour the selected channel. Reporting (`ver`/MQTT/SNMP) is unchanged.

**Tech Stack:** C++ (Arduino/ESP32), PlatformIO, ConfigSerializer JSON prefs (`/prefs.json`), GoogleTest native test env.

**Base branch:** `origin/dmc-observer-dev` @ `7e3e8b76` (this worktree is already reset onto it). Design spec: `docs/superpowers/specs/2026-09-14-ota-device-channel-switch-design.md`.

**External dependency (NOT in this plan):** `DutchMeshCore-OTA` `feat/dev-stable-ota-channels` (serves `/mqtt/dev/v` + `/mqtt/dev/fw`, tag `observer-mqtt-dev`) must be merged/deployed and the Cloudflare "Always Use HTTPS: Off" + bot-challenge Skip rules extended to `/mqtt/dev/*`, or `ota branch dev` + `ota check` returns an HTTP/connect error (stable unaffected).

---

## File Structure

- **Create** `src/helpers/OtaChannel.h` — channel enum + `ota_resolve_base()` + `ota_parse_channel()` + `ota_channel_name()`. Header-only, internal-linkage inline functions. Single responsibility: map selector ⇄ base URL/name.
- **Create** `test/test_ota_channel/test_ota_channel.cpp` — native GoogleTest for the resolver/parser.
- **Modify** `platformio.ini` — add `[env:native_ota_channel]`.
- **Modify** `src/helpers/CommonCLI.h` — add `uint8_t ota_channel = 0;` field + `def("ota_ch", ota_channel);`.
- **Modify** `src/MeshCore.h` — add `manifest_base` param to the `otaFromManifest` virtual.
- **Modify** `src/helpers/ESP32Board.h` / `.cpp` — thread `manifest_base` through both `otaFromManifest` defs, `otaFromManifestImpl`, and `OtaTaskArgs`.
- **Modify** `src/helpers/CommonCLI_Observer.cpp` — add the `ota branch` command; pass resolved base into the `ota check`/`ota update` calls.
- **Modify** `examples/simple_repeater/MyMesh.cpp` + `examples/simple_room_server/MyMesh.cpp` — pass resolved base at the deferred flash call.
- **Modify** `build.sh` — bake `OTA_MANIFEST_BASE_STABLE` + `OTA_MANIFEST_BASE_DEV`.

Signature chosen: `otaFromManifest(const char* manifest_base, const char* current_ver, bool dry_run, char reply[])`.

---

## Task 1: OtaChannel.h resolver + parser (TDD, native test)

**Files:**
- Create: `src/helpers/OtaChannel.h`
- Create: `test/test_ota_channel/test_ota_channel.cpp`
- Modify: `platformio.ini` (add `[env:native_ota_channel]`)

- [ ] **Step 1: Write the failing test**

Create `test/test_ota_channel/test_ota_channel.cpp`:

```cpp
#include <gtest/gtest.h>
#include "helpers/OtaChannel.h"

// The three base URLs are provided as -D macros by the test env (see platformio.ini).
TEST(OtaChannel, ResolvesNativeToBaseMacro) {
  EXPECT_STREQ(ota_resolve_base(OTA_CH_NATIVE), "https://stable.example/mqtt/v");
}
TEST(OtaChannel, ResolvesStable) {
  EXPECT_STREQ(ota_resolve_base(OTA_CH_STABLE), "https://stable.example/mqtt/v");
}
TEST(OtaChannel, ResolvesDev) {
  EXPECT_STREQ(ota_resolve_base(OTA_CH_DEV), "https://dev.example/mqtt/dev/v");
}
TEST(OtaChannel, ParseKnownKeywords) {
  uint8_t ch = 99;
  EXPECT_TRUE(ota_parse_channel("stable", &ch));  EXPECT_EQ(ch, OTA_CH_STABLE);
  EXPECT_TRUE(ota_parse_channel("dev", &ch));     EXPECT_EQ(ch, OTA_CH_DEV);
  EXPECT_TRUE(ota_parse_channel("default", &ch)); EXPECT_EQ(ch, OTA_CH_NATIVE);
}
TEST(OtaChannel, ParseRejectsUnknownAndLeavesOutputUntouched) {
  uint8_t ch = 7;
  EXPECT_FALSE(ota_parse_channel("beta", &ch));
  EXPECT_EQ(ch, 7);
}
TEST(OtaChannel, NameLabels) {
  EXPECT_STREQ(ota_channel_name(OTA_CH_NATIVE), "native");
  EXPECT_STREQ(ota_channel_name(OTA_CH_STABLE), "stable");
  EXPECT_STREQ(ota_channel_name(OTA_CH_DEV), "dev");
}
```

Add `[env:native_ota_channel]` to `platformio.ini` (after `[env:native_region_gating]`, mirror its style):

```ini
[env:native_ota_channel]
platform = native
test_framework = googletest
build_flags = -std=c++17
  -I test/mocks
  -I src
  -DOTA_MANIFEST_BASE="\"https://stable.example/mqtt/v\""
  -DOTA_MANIFEST_BASE_STABLE="\"https://stable.example/mqtt/v\""
  -DOTA_MANIFEST_BASE_DEV="\"https://dev.example/mqtt/dev/v\""
test_build_src = yes
test_filter = test_ota_channel
build_src_filter =
  -<*>
lib_deps =
  google/googletest @ 1.17.0
```

Also add `test_ota_channel` to the `test_ignore` line of `[env:native]` (line ~183), so the catch-all `native` env doesn't compile this test without the OTA macros defined:

```ini
test_ignore = test_kiss_modem, test_region_gating, test_ota_channel
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `pio test -e native_ota_channel`
Expected: FAIL/ERRORED — `helpers/OtaChannel.h` not found (file doesn't exist yet).
(Windows note: a failing/blocked native env reports as ERRORED in the pio summary; if the summary is unclear, run the built binary directly, e.g. `.pio/build/native_ota_channel/program.exe`, to see RED output.)

- [ ] **Step 3: Write the header**

Create `src/helpers/OtaChannel.h`:

```cpp
#pragma once
#include <stdint.h>
#include <string.h>

// OTA release-channel selector, persisted in NodePrefs::ota_channel.
enum OtaChannel : uint8_t {
  OTA_CH_NATIVE = 0,  // follow the channel this build was made for
  OTA_CH_STABLE = 1,
  OTA_CH_DEV    = 2,
};

// Resolve the effective manifest base URL for a channel selector.
// build.sh injects the three bases as compile-time macros:
//   OTA_MANIFEST_BASE        = this build's native channel (defined on every OTA build)
//   OTA_MANIFEST_BASE_STABLE = stable channel
//   OTA_MANIFEST_BASE_DEV    = dev channel
// stable/dev fall back to the native base when their macro is undefined (legacy/local
// builds that only define OTA_MANIFEST_BASE), so this never returns nullptr on an
// OTA-capable build. On a non-OTA build it returns nullptr.
static inline const char* ota_resolve_base(uint8_t channel) {
#if defined(OTA_MANIFEST_BASE)
  switch (channel) {
    case OTA_CH_STABLE:
#if defined(OTA_MANIFEST_BASE_STABLE)
      return OTA_MANIFEST_BASE_STABLE;
#else
      return OTA_MANIFEST_BASE;
#endif
    case OTA_CH_DEV:
#if defined(OTA_MANIFEST_BASE_DEV)
      return OTA_MANIFEST_BASE_DEV;
#else
      return OTA_MANIFEST_BASE;
#endif
    case OTA_CH_NATIVE:
    default:
      return OTA_MANIFEST_BASE;
  }
#else
  (void)channel;
  return nullptr;
#endif
}

// Human label for a selector (for the `ota branch` report).
static inline const char* ota_channel_name(uint8_t channel) {
  switch (channel) {
    case OTA_CH_STABLE: return "stable";
    case OTA_CH_DEV:    return "dev";
    default:            return "native";
  }
}

// Parse an `ota branch` argument. Returns true and sets *out on a known keyword
// (stable|dev|default; "default" -> native); returns false and leaves *out untouched
// otherwise.
static inline bool ota_parse_channel(const char* arg, uint8_t* out) {
  if (strcmp(arg, "stable") == 0)  { *out = OTA_CH_STABLE; return true; }
  if (strcmp(arg, "dev") == 0)     { *out = OTA_CH_DEV;    return true; }
  if (strcmp(arg, "default") == 0) { *out = OTA_CH_NATIVE; return true; }
  return false;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `pio test -e native_ota_channel`
Expected: PASS — 6 tests (`OtaChannel.*`) green.

- [ ] **Step 5: Commit**

```bash
git add src/helpers/OtaChannel.h test/test_ota_channel/test_ota_channel.cpp platformio.ini
git commit -m "feat(ota): channel-base resolver + arg parser with native tests"
```

---

## Task 2: Add the `ota_channel` prefs field (TDD via config-serializer round-trip)

**Files:**
- Modify: `test/test_config_serializer/test_config_serializer.cpp` (add round-trip test)
- Modify: `src/helpers/CommonCLI.h` (NodePrefs field + `structure()`)

Spec §5.3 (default, missing-key, round-trip) is verified here — `test_config_serializer` already round-trips real `NodePrefs` natively (e.g. `TEST(NodePrefs, FemGainSettingsRoundTrip)`) and runs under `[env:native]`, needing no OTA macros.

- [ ] **Step 1: Write the failing test**

In `test/test_config_serializer/test_config_serializer.cpp`, add after the `FemGainSettingsRoundTrip` test:

```cpp
TEST(NodePrefs, OtaChannelDefaultsToNative) {
    NodePrefs prefs;
    EXPECT_EQ(0, prefs.ota_channel);  // 0 == native
}

TEST(NodePrefs, OtaChannelRoundTrip) {
    NodePrefs saved;
    saved.ota_channel = 2;  // dev

    MockPrintStream output;
    ASSERT_TRUE(saved.saveSerial(output));
    std::string serialised(reinterpret_cast<const char*>(output.getBytes()), output.getLength());
    EXPECT_NE(std::string::npos, serialised.find("ota_ch:2"));

    MockInputStream input(serialised.c_str());
    NodePrefs loaded;
    loaded.ota_channel = 1;  // start different
    ASSERT_TRUE(loaded.loadSerial(input)) << serialised;
    EXPECT_EQ(2, loaded.ota_channel);
}

TEST(NodePrefs, OtaChannelMissingKeyKeepsDefault) {
    // A /prefs.json written before this field existed has no ota_ch key.
    MockInputStream input("{name:\"n\"}");
    NodePrefs loaded;               // ota_channel default 0 (native)
    ASSERT_TRUE(loaded.loadSerial(input));
    EXPECT_EQ(0, loaded.ota_channel);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e native -f test_config_serializer`
Expected: FAIL — compile error, `NodePrefs` has no member `ota_channel`.

- [ ] **Step 3: Add the field**

In `src/helpers/CommonCLI.h`, in `class NodePrefs`, add the field immediately after `uint8_t dutycycle_auto = 1;` (line 78):

```cpp
  uint8_t ota_channel = 0;      // OTA release channel selector: 0=native, 1=stable, 2=dev
```

- [ ] **Step 4: Serialize it**

In the top-level `NodePrefs::structure()` (the block ending at line 240 with `def("custom", custom);`), add after `def("disc_mod", discovery_mod_timestamp);` (line 233):

```cpp
    def("ota_ch", ota_channel);   // OTA release channel: 0=native, 1=stable, 2=dev
```

Named JSON key ⇒ an existing `/prefs.json` without `ota_ch` loads the default `0` (native). No memset, no byte-offset append.

- [ ] **Step 5: Run to verify it passes**

Run: `pio test -e native -f test_config_serializer`
Expected: PASS — including the three new `NodePrefs.OtaChannel*` tests.

- [ ] **Step 6: Commit**

```bash
git add test/test_config_serializer/test_config_serializer.cpp src/helpers/CommonCLI.h
git commit -m "feat(ota): persist ota_channel selector in NodePrefs"
```

---

## Task 3: Thread `manifest_base` through the OTA fetch path

Signature change + all call sites in one commit so the tree stays compilable.

**Files:**
- Modify: `src/MeshCore.h:75`
- Modify: `src/helpers/ESP32Board.h:161,165`
- Modify: `src/helpers/ESP32Board.cpp` (OtaTaskArgs ~163, ota_task_entry ~174, otaFromManifest ~179, otaFromManifestImpl ~198 incl. lines 233/234/236/251, stub ~420)
- Modify: `src/helpers/CommonCLI_Observer.cpp` (`#include`, calls at ~1271 and ~1279)
- Modify: `examples/simple_repeater/MyMesh.cpp:2008`
- Modify: `examples/simple_room_server/MyMesh.cpp:1623`

- [ ] **Step 1: Base virtual (`src/MeshCore.h:75`)**

Replace:

```cpp
  virtual bool otaFromManifest(const char* current_ver, bool dry_run, char reply[]) { return false; }
```

with:

```cpp
  virtual bool otaFromManifest(const char* manifest_base, const char* current_ver, bool dry_run, char reply[]) { return false; }
```

- [ ] **Step 2: ESP32Board declarations (`src/helpers/ESP32Board.h`)**

Line 161 → `bool otaFromManifest(const char* manifest_base, const char* current_ver, bool dry_run, char reply[]) override;`
Line 165 → `bool otaFromManifestImpl(const char* manifest_base, const char* current_ver, bool dry_run, char reply[]);`

- [ ] **Step 3: ESP32Board.cpp — task args + entry + both defs + impl**

`OtaTaskArgs` (add a field):

```cpp
struct OtaTaskArgs {
  ESP32Board* self;
  const char* manifest_base;
  const char* current_ver;
  bool dry_run;
  char* reply;
  volatile bool result;
  volatile bool done;
};
```

`ota_task_entry` (line 174):

```cpp
  a->result = a->self->otaFromManifestImpl(a->manifest_base, a->current_ver, a->dry_run, a->reply);
```

`otaFromManifest` (line 179) — new signature + args init:

```cpp
bool ESP32Board::otaFromManifest(const char* manifest_base, const char* current_ver, bool dry_run, char reply[]) {
  // ... unchanged comment block ...
  OtaTaskArgs args = { this, manifest_base, current_ver, dry_run, reply, false, false };
```

`otaFromManifestImpl` (line 198) — new signature:

```cpp
bool ESP32Board::otaFromManifestImpl(const char* manifest_base, const char* current_ver, bool dry_run, char reply[]) {
```

Inside the impl, replace every `OTA_MANIFEST_BASE` **usage** (NOT the `#if !defined(OTA_MANIFEST_BASE)` guard on line 199, which stays) with `manifest_base`:
- line 233: `if (strncmp(manifest_base, "https://", 8) == 0) {`
- line 234: `snprintf(murl, sizeof(murl), "http://%s/%s.json", manifest_base + 8, OTA_VARIANT);`
- line 236: `snprintf(murl, sizeof(murl), "%s/%s.json", manifest_base, OTA_VARIANT);`
- line 251: `snprintf(murl, sizeof(murl), "%s/%s.json", manifest_base, OTA_VARIANT);`

`#else` stub (line 420):

```cpp
bool ESP32Board::otaFromManifest(const char* manifest_base, const char* current_ver, bool dry_run, char reply[]) {
  strcpy(reply, "ERR: not supported");
  return false;
}
```

- [ ] **Step 4: CommonCLI_Observer.cpp call sites**

Add near the top with the other includes:

```cpp
#include "OtaChannel.h"
```

Update the two calls in the `ota check`/`ota update` block (currently `_board->otaFromManifest(_callbacks->getFirmwareVer(), true, reply)` at ~1271 and inside the `if (...)` at ~1279) to pass the resolved base first:

```cpp
      _board->otaFromManifest(ota_resolve_base(_prefs->ota_channel), _callbacks->getFirmwareVer(), true, reply);
```

```cpp
      if (_board->otaFromManifest(ota_resolve_base(_prefs->ota_channel), _callbacks->getFirmwareVer(), true, reply)) {
```

- [ ] **Step 5: MyMesh deferred flash call sites**

In **`examples/simple_repeater/MyMesh.cpp`** add near its includes:

```cpp
#include <helpers/OtaChannel.h>
```

Line 2008, replace:

```cpp
    } else if (!_cli.getBoard()->otaFromManifest(getFirmwareVer(), false, ota_reply)) {
```

with:

```cpp
    } else if (!_cli.getBoard()->otaFromManifest(ota_resolve_base(_prefs.ota_channel), getFirmwareVer(), false, ota_reply)) {
```

In **`examples/simple_room_server/MyMesh.cpp`** add near its includes:

```cpp
#include <helpers/OtaChannel.h>
```

Line 1623, replace:

```cpp
    if (may_flash && !_cli.getBoard()->otaFromManifest(getFirmwareVer(), false, ota_reply)) {
```

with:

```cpp
    if (may_flash && !_cli.getBoard()->otaFromManifest(ota_resolve_base(_prefs.ota_channel), getFirmwareVer(), false, ota_reply)) {
```

(`_prefs` is a `NodePrefs` member of MyMesh — accessed as `_prefs.` here, vs `_prefs->` in CommonCLI where it is a pointer.)

- [ ] **Step 6: Verify it compiles**

Run: `export FIRMWARE_VERSION=v0.0.0-test && sh build.sh build-firmware Heltec_v3_repeater_observer_mqtt`
Expected: build SUCCEEDS (links). This exercises the new signature across MeshCore.h, ESP32Board, CommonCLI_Observer, and repeater MyMesh. (Room-server path is covered in Task 6.)

- [ ] **Step 7: Commit**

```bash
git add src/MeshCore.h src/helpers/ESP32Board.h src/helpers/ESP32Board.cpp src/helpers/CommonCLI_Observer.cpp examples/simple_repeater/MyMesh.cpp examples/simple_room_server/MyMesh.cpp
git commit -m "feat(ota): pass runtime manifest base through otaFromManifest"
```

---

## Task 4: The `ota branch` command

**Files:**
- Modify: `src/helpers/CommonCLI_Observer.cpp` (new else-if beside the `ota check`/`ota update` block; `#include "OtaChannel.h"` already added in Task 3)

- [ ] **Step 1: Add the command handler**

Immediately AFTER the closing of the `ota check`/`ota update` else-if block (the `return true;` that ends it, ~line 1315) and BEFORE the next `} else if (memcmp(command, "start webconfig"...`, insert:

```cpp
  } else if (memcmp(command, "ota branch", 10) == 0) {
    // Switch (or report) the OTA release channel this device pulls from. The
    // selection is persisted (NodePrefs::ota_channel) and resolved to a baked-in
    // base URL by ota_resolve_base(); it changes only WHERE updates are fetched,
    // never the running image's reported version. Reachable from any admin path,
    // same as `ota update`.
#if defined(WITH_MQTT_BRIDGE) && defined(OTA_MANIFEST_BASE)
    const char* arg = command + 10;
    while (*arg == ' ') arg++;
    if (*arg == 0) {
      snprintf(reply, 160, "channel: %s (%s), base %s",
               ota_channel_name(_prefs->ota_channel),
               _prefs->ota_channel == OTA_CH_NATIVE ? "native" : "override",
               ota_resolve_base(_prefs->ota_channel));
    } else {
      uint8_t ch;
      if (!ota_parse_channel(arg, &ch)) {
        strcpy(reply, "ERR: usage ota branch [stable|dev|default]");
      } else {
        _prefs->ota_channel = ch;
        savePrefs();
        snprintf(reply, 160, "channel set to %s, base %s",
                 ota_channel_name(ch), ota_resolve_base(ch));
      }
    }
#else
    strcpy(reply, "ERR: online OTA not supported on this build");
#endif
    return true;
```

- [ ] **Step 2: Verify it compiles**

Run: `export FIRMWARE_VERSION=v0.0.0-test && sh build.sh build-firmware Heltec_v3_repeater_observer_mqtt`
Expected: build SUCCEEDS.

- [ ] **Step 3: Commit**

```bash
git add src/helpers/CommonCLI_Observer.cpp
git commit -m "feat(ota): add 'ota branch [stable|dev|default]' command"
```

---

## Task 5: Bake both channel bases in build.sh

**Files:**
- Modify: `build.sh` (OTA flags export at line 226; env-default section ~221)

- [ ] **Step 1: Add the two base env vars**

In `build.sh`, right after line 221 (`OTA_MANIFEST_BASE_URL="${OTA_MANIFEST_BASE_URL:-https://ota.dutchmeshcore.nl/mqtt/v}"`), add:

```bash
  # Both named channel bases are baked into EVERY observer build so `ota branch`
  # can re-point a device at either channel at runtime. OTA_MANIFEST_BASE above stays
  # the build's NATIVE channel (= stable base for stable builds, dev base for dev
  # builds), so `ota branch default` resolves correctly. These two must match the
  # paths DutchMeshCore-OTA serves (feat/dev-stable-ota-channels): /mqtt/v + /mqtt/dev/v.
  OTA_MANIFEST_BASE_STABLE_URL="${OTA_MANIFEST_BASE_STABLE_URL:-https://ota.dutchmeshcore.nl/mqtt/v}"
  OTA_MANIFEST_BASE_DEV_URL="${OTA_MANIFEST_BASE_DEV_URL:-https://ota.dutchmeshcore.nl/mqtt/dev/v}"
```

- [ ] **Step 2: Inject them as -D flags**

Extend the `export PLATFORMIO_BUILD_FLAGS=...` line (226) by appending two flags before the closing quote:

```bash
  export PLATFORMIO_BUILD_FLAGS="${PLATFORMIO_BUILD_FLAGS} -DFIRMWARE_BUILD_DATE='\"${FIRMWARE_BUILD_DATE}\"' -DFIRMWARE_VERSION='\"${EMBEDDED_VERSION_STRING}\"' -DOTA_VARIANT='\"$1\"' -DOTA_MANIFEST_BASE='\"${OTA_MANIFEST_BASE_URL}\"' -DOTA_MANIFEST_BASE_STABLE='\"${OTA_MANIFEST_BASE_STABLE_URL}\"' -DOTA_MANIFEST_BASE_DEV='\"${OTA_MANIFEST_BASE_DEV_URL}\"'"
```

- [ ] **Step 3: Verify the flags reach the build**

Run: `export FIRMWARE_VERSION=v0.0.0-test && sh build.sh build-firmware Heltec_v3_repeater_observer_mqtt`
Expected: build SUCCEEDS. Optionally confirm the macros are present:
`grep -R "OTA_MANIFEST_BASE_DEV" .pio/build/Heltec_v3_repeater_observer_mqtt/ 2>/dev/null | head` — or inspect the compile command via `pio run -e Heltec_v3_repeater_observer_mqtt -v 2>&1 | grep OTA_MANIFEST_BASE_DEV | head -1`.

- [ ] **Step 4: Commit**

```bash
git add build.sh
git commit -m "build(ota): bake stable + dev manifest bases into observer builds"
```

---

## Task 6: End-to-end verification

**Files:** none (verification only)

- [ ] **Step 1: Native tests green**

Run: `pio test -e native_ota_channel`
Expected: PASS (6 `OtaChannel.*` tests).

- [ ] **Step 2: Repeater observer builds**

Run: `export FIRMWARE_VERSION=v0.0.0-test && sh build.sh build-firmware Heltec_v3_repeater_observer_mqtt`
Expected: SUCCESS; a `.bin` appears under `out/`.

- [ ] **Step 3: Room-server observer builds** (covers the room-server MyMesh call site)

Run: `export FIRMWARE_VERSION=v0.0.0-test && sh build.sh build-firmware Heltec_v3_room_server_observer_mqtt`
Expected: SUCCESS.
(If `Heltec_v3_room_server_observer_mqtt` is not a valid env, pick any `*_room_server_observer_mqtt` env from `sh build.sh list`.)

- [ ] **Step 4: Sanity of the guard on a non-OTA build (optional)**

Run: `export FIRMWARE_VERSION=v0.0.0-test && sh build.sh build-firmware Heltec_v3_repeater`
Expected: SUCCESS — the `ota branch` handler compiles down to the `ERR: online OTA not supported on this build` arm (no `OTA_MANIFEST_BASE`), and `NodePrefs::ota_channel` is a harmless unused byte.

- [ ] **Step 5: Update the spec's dependency checklist status if anything changed; no commit needed unless edited.**

---

## Post-implementation notes

- **On-device manual check (when hardware is available):** `ota branch` → reports `native`; `ota branch dev` → `channel set to dev, base https://ota.dutchmeshcore.nl/mqtt/dev/v`; reboot; `ota branch` → still `dev (override)` (persistence); `ota branch default` → back to `native`. `ota branch dev; ota check` returns an HTTP error until the server dev channel is deployed — expected.
- **Do not push and do not add AI co-author trailers** (standing user preference). Leave integration (PR/merge) to the user.
