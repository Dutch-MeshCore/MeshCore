# MeshCore MQTT Observer - Neighbour Discovery

## Overview

Neighbour discovery lets an observer publish its **zero-hop repeater neighbours** - each with signal quality, last-heard age, and the region scopes it floods to - to the MQTT `neighbors` topic, so a collector can map the local mesh topology.

Discovery has two triggers:

* **Manual, one-shot** - `discover.scopes`
* **Periodic** - `set mqtt.neighbors on`, repeating on a configurable interval

Both do the same two-phase work: refresh the zero-hop neighbour table, then query each neighbour for its region scopes, then publish the assembled table once.

This is an **observer-build feature**. It is compiled in on all PSRAM boards, and on non-PSRAM boards only when built with `MQTT_NEIGHBORS_WITHOUT_PSRAM`. Where it is not compiled in, the commands reply `Err - neighbors not enabled in this build`. Non-PSRAM builds cap the table at **20 neighbours** per pass to bound internal-DRAM use, and set `truncated` in the message with the true `total_neighbors`.

---

# Refreshing the Zero-Hop Table

Refresh the zero-hop neighbour table without publishing anything:

```text
discover.neighbors
```

This sends a zero-hop discovery and collects responses over a **60-second** window. It is available on any build (it does not require the neighbours feature), and is the refresh phase that scope publishing builds on.

To list the current table locally:

```text
neighbors
```

(limited to the 8 most recent adverts; each line is `{pubkey-prefix}:{timestamp}:{snr*4}`).

---

# Publishing the Neighbour Table Once

```text
discover.scopes
```

Refreshes the zero-hop table, queries each neighbour for its region scopes, and publishes the assembled table to the MQTT `neighbors` topic **once**.

* If the cache is current, the scope queries run immediately in one shot.
* If a `discover.neighbors` refresh is already collecting responses - whether started from the CLI or by the periodic timer - the scope queries are **queued behind its 60-second window** so they run against the refreshed table. The reply reports the wait, e.g.:

```text
OK - scopes queued (47s discovery remaining)
```

A queued one-shot request survives `set mqtt.neighbors off`; only the periodic timer's own refresh is cancelled by turning periodic publishing off.

**Requires** a neighbours-enabled observer build; elsewhere it replies `Err - neighbors not enabled in this build`.

---

# Periodic Publishing

Enable or disable automatic publishing:

```text
get mqtt.neighbors
set mqtt.neighbors <on|off>
```

**Default:** `off`.

When enabled, each cycle first runs a 60-second zero-hop refresh (equivalent to `discover.neighbors`), then queries the refreshed table for scopes and publishes when the scope-query phase completes. The setting is read live by the mesh loop - no restart is required, and enabling it triggers a discovery on the next pass.

Set how often the table is published:

```text
get mqtt.neighbors.interval
set mqtt.neighbors.interval <hours>
```

**Allowed values:** `12`-`336` hours. Out-of-range values are **rejected, not clamped**.

**Default:** `24` (hours).

While periodic publishing is on, `get mqtt.status` gains a trailing field:

```text
nbr: <next>/<last>
```

`<next>` is the time to the next automatic publish (`3h12m`, `12m`, `45s`, or `active`/`due`) and `<last>` is the last publish result (`ok`, `failed`, or `none`).

---

# The Neighbours Topic

```text
meshcore/{IATA}/{DEVICE_PUBLIC_KEY}/neighbors
```

The table is published to **every configured slot's** `neighbors` topic at QoS 0, retained only where the broker preset allows it. MeshRank slots use `meshrank/uplink/{token}/{DEVICE_PUBLIC_KEY}/neighbors` instead.

## Message Format

```json
{
  "timestamp": "2024-01-01T12:00:00.000000+00:00",
  "origin": "MeshCore-HOWL",
  "origin_id": "A1B2C3D4E5F67890...",
  "total_neighbors": 2,
  "queried_neighbors": 2,
  "truncated": false,
  "self": { "scopes": "DEN,APRS", "default_scope": "*" },
  "neighbors": [
    {
      "pubkey": "0011223344556677...",
      "snr": 9.75,
      "heard_secs_ago": 42,
      "scopes": "DEN,APRS",
      "status": "responded"
    },
    {
      "pubkey": "8899AABBCCDDEEFF...",
      "snr": 12.5,
      "heard_secs_ago": null,
      "scopes": "DEN",
      "status": "responded"
    }
  ]
}
```

Field notes:

* `total_neighbors` - size of the neighbour-table snapshot the cycle started from.
* `queried_neighbors` - how many scope requests were confirmed transmitted.
* `truncated` - whether the 10 KB publish buffer filled before every entry fit. All three fields are always present, so the `neighbors` array can be **shorter** than `total_neighbors`; compare its length against that field rather than assuming the table is complete.
* Entries are ordered most- to least-useful (usable age first, then most recently heard, then stronger SNR); the tail is dropped if the payload would exceed the buffer.
* `status` is `responded`, `timeout`, or `send_failed` per neighbour.
* `heard_secs_ago` is `null` when the age cannot be computed (the neighbour was last heard before the clock was set, so the stored stamp and current clock are from different epochs). **Consumers must treat `null` as unknown, not as zero** - the key is always present, so a missing key means older firmware, and a `null` never means "heard just now". A neighbour that answers the scope query has its stamp refreshed, so a `null` age normally clears itself on the next cycle.
* `self.default_scope` is the region this node floods to by default (`region default`); it is `*` when no default region is set, matching the unscoped flood the radio performs in that case.

---

# Build Requirements

| Board class | Neighbour discovery |
| ----------- | ------------------- |
| PSRAM (Heltec V3/V4, Station G2/G3, T-Beam Supreme, …) | Compiled in; full table |
| Non-PSRAM (Heltec Wireless Tracker v1.1/v2, …) | Only when built with `MQTT_NEIGHBORS_WITHOUT_PSRAM`; capped at 20 neighbours per pass, sets `truncated` |
| Non-observer / feature not compiled in | `Err - neighbors not enabled in this build` |

---

# Important Notes

* Neighbour discovery is an **observer** feature, gated on the build - check `get mqtt.neighbors` before relying on it.
* `discover.neighbors` only refreshes the local zero-hop table; `discover.scopes` refreshes **and publishes** once.
* Periodic publishing defaults to `off`; enable it with `set mqtt.neighbors on` and tune the cadence with `set mqtt.neighbors.interval <12-336>`.
* The table is published to every configured broker slot's `neighbors` topic.
* Always compare `neighbors.length` against `total_neighbors`, and treat `heard_secs_ago: null` as *unknown*.
* Non-PSRAM builds cap the pass at 20 neighbours and flag it with `truncated`.
