# MeshCore MQTT Observer - Per-Broker Packet Filter

## Overview

Every MQTT broker slot (1-6) has its own **packet-type allowlist** that decides which MeshCore payload types are uploaded to that broker. This lets a single observer feed different packet mixes to different brokers - for example everything to a private collector, but only adverts to a public analyzer.

The filter:

* Is independent per slot (`mqtt1` … `mqtt6`)
* Applies to both `packets` and `raw` publications, for received (RX) packets and for TX packets permitted by `mqtt.tx`
* Does **not** affect forwarding, local packet processing, capture logs, the status message, or neighbour publishing
* Applies live - changing it does not reconnect the broker

The filter defaults to **`all`** (every payload type is uploaded), so a fresh install behaves exactly as before it existed.

---

# Packet Types

The allowlist is built from MeshCore payload types 0-15:

| Type | Name        | Type  | Name                    |
| ---- | ----------- | ----- | ----------------------- |
| 0    | `req`       | 8     | `path`                  |
| 1    | `response`  | 9     | `trace`                 |
| 2    | `txt_msg`   | 10    | `multipart`             |
| 3    | `ack`       | 11    | `control`               |
| 4    | `advert`    | 12-14 | reserved (number only)  |
| 5    | `grp_txt`   | 15    | `raw_custom`            |
| 6    | `grp_data`  |       |                         |
| 7    | `anon_req`  |       |                         |

Names are lowercase and exact. Types 12-14 are reserved upstream and have no name, so they can only be selected by number.

---

# Viewing a Slot's Filter

```text
get mqttN.filter
```

Example:

```text
get mqtt1.filter
```

The reply is always in the canonical numeric form - `all`, `none`, or an ascending comma-separated list of numbers - whichever spelling was used to set it.

---

# Setting a Slot's Filter

```text
set mqttN.filter <all|none|list>
```

The `list` form is a comma-separated set of payload-type **names or numbers**, and the two may be mixed. The following are all equivalent, sending only text messages and adverts to slot 1:

```text
set mqtt1.filter txt_msg,advert
set mqtt1.filter 2,4
set mqtt1.filter advert, 2
```

Select everything:

```text
set mqtt1.filter all
```

Select nothing (keep the broker connected for status / neighbours, but upload no packet traffic):

```text
set mqtt1.filter none
```

Reset a slot back to the default - a **bare** command also means `all`:

```text
set mqtt1.filter
```

---

# How the Filter Behaves

* It gates both the structured `packets` topic and the `raw` topic, for RX packets and for the TX packets that `mqtt.tx` already permits.
* It does **not** touch local packet processing, forwarding, capture logs, the `status` message, or the `neighbors` publication.
* A rejected packet is dropped **before** it is copied into the publish queue, so a narrow filter saves the queue slot and the per-packet work, not just the upload.
* Changes take effect immediately, without reconnecting the broker.

---

# Statistics

The running count of filtered packets is reported by:

```text
get mqtt.stats
```

as a `filt=<n>` field.

A slot whose filter is **not** `all` also shows it in the slot diagnostics:

```text
get mqttN.diag
```

and on the WebConfig **Stats** tab. This matters because a filtered slot otherwise looks identical to an idle, healthy one - the diagnostics are how you tell "nothing to send" from "everything filtered out".

The diag reply is capped at 160 characters. When a slot is also reporting a long error tail, the filter is summarised as `filter:<n>/16` (n of 16 types allowed) rather than listed in full, because a list clipped mid-way would read as a different, valid allowlist. `get mqttN.filter` always gives the exact value.

---

# WebConfig

In the WebConfig portal the allowlist appears as **a checkbox per type** under each configured slot, with **All** / **None** shortcuts. Clearing every box is `none` (nothing uploaded).

---

# Recommended Configurations

## Private collector - everything

The default. Nothing to configure:

```text
set mqtt1.preset dutchmeshcore-1
set mqtt1.filter all
```

## Public analyzer - adverts and text only

Keep a public broker useful for mapping without uploading every ACK and control packet:

```text
set mqtt2.filter advert,txt_msg
```

## Status / neighbours only - no packet traffic

Keep a broker connected for the `status` and `neighbors` topics while uploading no packets at all:

```text
set mqtt3.filter none
```

---

# Persistent Configuration

Per-slot filters are stored with the rest of the MQTT settings in:

```text
/mqtt.json
```

The setting persists across reboots and is applied on the next connection to each slot.

**Downgrade note:** rolling *forward* and back within this release family is safe - firmware that predates the packet filter simply ignores it, and slots revert to `all` if that firmware re-saves. Rolling back to a build released *before* the packet filter existed is the case to watch: that firmware rejects the longer settings file outright and falls back to defaults, losing the stored WiFi credentials along with the broker config. Slots left at the `all` default keep the file in the shorter layout those builds can read, so if you may need to roll a node back that far, reset every slot to `all` first.

---

# Important Notes

* The per-broker filter is an **allowlist** - listed types are uploaded, everything else is dropped for that slot.
* The default is `all`; a bare `set mqttN.filter` returns a slot to `all`.
* It affects only what is **published** to that broker - never forwarding, local processing, status, or neighbours.
* It applies to both `packets` and `raw`, and only to the TX packets that `mqtt.tx` already allows.
* Each of the six slots is independent.
* `get mqttN.filter` always answers in canonical numeric form regardless of how it was set.
