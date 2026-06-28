# DMC MQTT OTA — Site Admin Setup

How to host firmware over-the-air (OTA) updates for the DutchMeshCore MQTT
observer firmware at **`ota.dutchmeshcore.nl`** (behind Cloudflare).

## What the device does

Each observer device (repeater / room server, `*_observer_mqtt` builds) has the
manifest base baked in at build time:

```
OTA_MANIFEST_BASE = https://ota.dutchmeshcore.nl/mqtt/v
```

On `ota check` / `ota update` it fetches a small (~180 byte) per-variant manifest:

```
https://ota.dutchmeshcore.nl/mqtt/v/<variant>.json
   e.g. .../mqtt/v/Heltec_v3_repeater_observer_mqtt.json
```

Two distinct request patterns — **both must work**:

| Command      | Manifest fetch                          | Firmware (.bin) download |
|--------------|-----------------------------------------|--------------------------|
| `ota check`  | **plain HTTP** GET (no TLS, by design*) | — |
| `ota update` | **HTTPS** GET (cert-verified)           | **HTTPS**, cert-verified |

\* The cheap check uses plain HTTP on purpose: it avoids a TLS handshake so it
can run while the two live MQTT TLS sessions are up on no-PSRAM boards. This is
the single most important thing to get right in Cloudflare (step 3a).

The firmware already forces HTTP/1.0 so Cloudflare's chunked-transfer responses
parse correctly — no action needed for that.

## 1. DNS

- Create `ota.dutchmeshcore.nl`, **proxied through Cloudflare** (orange cloud).
- Point it at the origin web server that will hold the manifests + binaries.

## 2. Origin web server

Serve two trees (paths are examples; only `/mqtt/v/` is fixed by the firmware,
the binary location is whatever you put in each manifest's `file` field):

```
/mqtt/v/<variant>.json      # per-variant manifests   (served over HTTP + HTTPS)
/mqtt/bin/<variant>-...bin   # firmware binaries        (served over HTTPS)
```

- `*.json` → `Content-Type: application/json`
- `*.bin`  → `Content-Type: application/octet-stream`
- Recommended Cloudflare SSL/TLS mode: **Full** (origin listens on 443). The
  device's `.bin` download is cert-verified, but it validates the **Cloudflare
  edge** certificate (Universal SSL), so the origin cert doesn't need to be
  publicly trusted.

## 3. Cloudflare configuration (the critical part)

### 3a. 🔴 Disable forced HTTPS for the OTA path
The `ota check` is plain HTTP. Cloudflare's default **"Always Use HTTPS"** would
301-redirect it; the device does **not** follow redirects and reports
`ERR: manifest HTTP 301`.

- Rules → **Configuration Rules** (or a legacy Page Rule) matching
  `ota.dutchmeshcore.nl/mqtt/v/*`:
  - **Always Use HTTPS: Off**
  - **Automatic HTTPS Rewrites: Off**
- Do **not** enable the zone-wide "Always Use HTTPS" toggle (or scope an
  exception for this hostname/path).

✅ Test: `curl -i http://ota.dutchmeshcore.nl/mqtt/v/<variant>.json` must return
**200 + JSON**, not a 301.

### 3b. Skip bot / security challenges for the OTA path
The ESP32 HTTP client sends no browser headers, so Bot Fight Mode / Browser
Integrity Check / Managed Challenge would hand it an HTML challenge (403/503)
instead of JSON.

- WAF → create a **Skip** rule for `ota.dutchmeshcore.nl/mqtt/*` that skips:
  Bot Fight Mode, Browser Integrity Check, and all Managed Challenges.

### 3c. Caching
Manifests change every release; binaries do not (their filename carries the
version + git hash).

- `*.json`: short edge cache TTL (≤ 5 min) **or** purge them in the publish step.
- `*.bin`: safe to "Cache Everything" with a long TTL.

### 3d. TLS / certificate
No special action. Cloudflare Universal SSL (Let's Encrypt / Google Trust
Services) is already trusted by the firmware's embedded root-CA bundle.

## 4. Manifest format

One JSON file per build target at `/mqtt/v/<variant>.json`. Fields the firmware
reads:

```json
{
  "file":        "https://ota.dutchmeshcore.nl/mqtt/bin/Heltec_v3_repeater_observer_mqtt-v1.16.0-abc1234.bin",
  "version":     "v1.16.0.5",
  "baseVersion": "v1.16.0",
  "build":       5,
  "hash":        "abc1234",
  "partSig":     "<sha from scripts/partition_signature.py>",
  "partitionChange": false
}
```

- `file` — **absolute HTTPS URL** to the firmware binary (may be on this host or
  elsewhere).
- `version` / `baseVersion` / `build` — used to decide "how many builds behind".
- `hash` — git short hash (fallback identity for images without a build number).
- `partSig` — partition-layout signature; the device refuses an update whose
  partition layout doesn't match what's flashed (OTA can't rewrite the table).
- `partitionChange` — legacy boolean fallback for manifests predating `partSig`.

These are produced by the build/publish pipeline (CI), not written by hand — the
admin just needs the `/mqtt/v/` directory to exist and be writable by whatever
publishes to it.

## 5. Verification

From any machine:

```sh
# 5a. plain HTTP must return 200 (NOT 301) — proves step 3a is correct
curl -i  http://ota.dutchmeshcore.nl/mqtt/v/Heltec_v3_repeater_observer_mqtt.json

# 5b. HTTPS manifest
curl -i https://ota.dutchmeshcore.nl/mqtt/v/Heltec_v3_repeater_observer_mqtt.json

# 5c. binary reachable over HTTPS with correct size
curl -I https://ota.dutchmeshcore.nl/mqtt/bin/<the-binary>.bin
```

On a device:
- `ota check` → reports the available version / how many builds behind.
- `ota update` → downloads + flashes + reboots into the new image.

## 6. Publish model — server pulls from GitHub (no credentials)

**Decided architecture:**
- **Binaries live on GitHub Releases.** The MQTT CI (`build-*-mqtt-firmwares.yml`)
  already publishes `‹env›-‹version›-‹hash›.bin` assets via
  `softprops/action-gh-release` — no extra setup, no secrets.
- **Manifests are served by a small Docker container** on the DMC side (behind
  Cloudflare at `ota.dutchmeshcore.nl/mqtt/v/`). It is a *pull* mirror: it polls
  the public GitHub Releases API every few minutes and regenerates the `*.json`.
  No credentials are exchanged in either direction (CI uses the built-in
  `GITHUB_TOKEN`; the container reads public releases anonymously).

The manifest's `file` points **directly at the GitHub release asset URL** — the
device's HTTPS `httpUpdate` follows GitHub's redirect and GitHub's CA is in the
embedded bundle, so the bins never need to be mirrored to the DMC server. Only
the tiny `*.json` are served by the container (this is required: the plain-HTTP
`ota check` needs a non-redirecting host, which GitHub is not).

Minimal manifest the container emits (hash-based update check — `build`/`partSig`
optional, add later if "N builds behind" / partition-compat precision is wanted):

```json
{ "file": "https://github.com/Dutch-MeshCore/MeshCore/releases/download/‹tag›/‹env›-‹ver›-‹hash›.bin",
  "version": "v1.16.0", "hash": "abc1234" }
```

> **TODO — build the OTA-manifest Docker container.** Cron (every few min) →
> `GET /repos/Dutch-MeshCore/MeshCore/releases/latest` → for each
> `*_observer_mqtt-*.bin` asset, parse env/version/hash from the filename and
> write `/mqtt/v/‹env›.json` → serve over http+https → purge the Cloudflare cache
> for changed `*.json` (or rely on the short TTL from step 3c). Stateless, no
> secrets, ~1 small script + a web server (nginx/caddy) in the image.
