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

## 6. Publish access (for CI — not an admin runtime concern)

The GitHub Actions publish workflow needs a way to upload the regenerated
`*.json` manifests and `*.bin` files to the origin. Decide one of:
- SSH + rsync/scp (provide a deploy key / restricted user with write access to
  `/mqtt/`), or
- an object store the origin serves from (S3 / R2 credentials), or
- the origin pulls from a GitHub Release on a schedule/webhook.

Whichever you choose, the publish step must also **purge the Cloudflare cache**
for the updated `*.json` (or rely on the short TTL from step 3c).
