# MeshCore Simple Repeater Packet Filter

## Overview

The filter can selectively block forwarded packets based on:

* Hop count
* Per-packet-type rate limits
* Minimum path hash size
* Name of a group channel
* Malformed group text messages
* Packet type

The filter is **disabled by default**.

Direct-routed packets bypass the filter. Priority packets involving known ACL contacts are also exempt.

Configuration and statistics are kept apart. Every `filter <setting>` command
shows and changes a setting; every `filter stats <topic>` command reports what
that setting actually dropped. See [Statistics](#statistics).

---

# Command Overview

| Command | Purpose |
| ------- | ------- |
| `filter` | Status and the drop totals per reason |
| `filter help` | List every subcommand |
| `filter on` / `filter off` | Enable or disable filtering |
| `filter reset` | Restore all settings to their defaults |
| `filter types` | List the packet type IDs |
| `filter count` | Hop and rate drops per packet type |
| `filter hops` | Current hop limits |
| `filter hops <type> <max_hops>` | Set a hop limit |
| `filter rate` | Current rate limits (incl. soft cutoff) |
| `filter rate <type> <limit> <secs> [soft]` | Set a rate limit (optional soft cutoff) |
| `filter channel list` | Blocked channels |
| `filter channel add` / `filter channel remove` | Block or unblock a channel |
| `filter hash` | Current minimum path hash size |
| `filter hash <bytes>` | Set the minimum path hash size |
| `filter malformed` | Current malformed scan state |
| `filter malformed on` / `filter malformed off` | Enable or disable the malformed scan |
| `filter stats <topic>` | Drops for one topic, in detail |

The same list is available on the device:

```text
filter help
> filter [ help | on | off | reset | types | count | stats <topic> | hops <args> | rate <args> | channel <args> | hash <min_bytes> | malformed <on | off> ]
```

Commands that change a setting reply `> Filter: OK` unless noted otherwise.

---

# Enabling the Filter

Display the current status:

```text
filter
> Filter on: Blocked [ Hops: 326 | Rate: 0 | Channel: 0 | Hash: 8097 | Malformed: 0 ]
```

Enable filtering:

```text
filter on
> Filter: on
```

Disable filtering:

```text
filter off
> Filter: off
```

Reset all settings to their defaults:

```text
filter reset
> Filter: preferences reset
```

This resets the settings, not the counters. Statistics are cleared by
`clear stats` and on reboot.

---

# Packet Types

Display the supported packet types:

```text
filter types
```

| ID | Type      |
| -- | --------- |
| 00 | REQ       |
| 01 | RESPONSE  |
| 02 | TXT_MSG   |
| 03 | ACK       |
| 04 | ADVERT    |
| 05 | GRP_TXT   |
| 06 | GRP_DATA  |
| 07 | ANON_REQ  |
| 08 | PATH      |
| 09 | TRACE     |
| 10 | MULTIPART |
| 11 | CONTROL   |

---

# Hop Count Filtering

Display current limits:

```text
filter hops
[TYPE: MAX_HOPS]
00: 8
01: 8
02: 8
03: 8
04: 8
05: 32
06: 8
07: 8
08: 8
09: 8
10: 8
11: 8
```

Set the maximum hop count for a packet type:

```text
filter hops <type> <max_hops>
```

Example:

```text
filter hops 05 16
> Filter: OK
```

The drops this limit caused are reported by `filter stats hops`.

Default limits:

| Packet Type | Max Hops |
| ----------- | -------- |
| REQ         | 8        |
| RESPONSE    | 8        |
| TXT_MSG     | 8        |
| ACK         | 8        |
| ADVERT      | 8        |
| GRP_TXT     | 32       |
| GRP_DATA    | 8        |
| ANON_REQ    | 8        |
| PATH        | 8        |
| TRACE       | 8        |
| MULTIPART   | 8        |
| CONTROL     | 8        |

---

# Rate Limiting

Display current configuration:

```text
filter rate
[TYPE: LIMIT,SECS,SOFT]
00: 5,60,0
01: 5,60,0
02: 20,60,0
03: 5,60,0
04: 10,60,0
05: 20,60,0
06: 5,60,0
07: 5,60,0
08: 5,60,0
09: 5,60,0
10: 5,60,0
11: 5,60,0
```

The third column is the **soft cutoff** (see below); `0` means it is off.

Configure a rate limit:

```text
filter rate <type> <limit> <seconds> [soft]
```

Example:

```text
filter rate 05 20 60
> Filter: OK
```

The drops this limit caused are reported by `filter stats rate`.

This allows up to **20 Group Text packets every 60 seconds**.

Setting the limit to **0** disables rate limiting for that packet type.

## Soft cutoff

By default a rate limit is a hard cutoff: once a type reaches its limit inside
the window, every further packet of that type is dropped until the window rolls.
That closes the door abruptly and takes legitimate traffic down with the abuse.

The optional `[soft]` argument turns the drop into a gradual ramp. Below `soft`
every packet is forwarded; between `soft` and the hard `limit` the forward
probability falls linearly to zero; at or above `limit` nothing is forwarded.

```text
filter rate 05 20 60 15
> Filter: OK
```

Group Text now forwards normally up to 15 per minute, then tapers off, reaching
a full stop at 20. `soft` must be **less than** `limit`; `0` (the default) keeps
the hard cutoff. The setting is per packet type and is stored in `/filter_prefs`
like the rest of the rate configuration.

Default limits:

| Packet Type | Limit     |
| ----------- | --------- |
| REQ         | 5 / 60 s  |
| RESPONSE    | 5 / 60 s  |
| TXT_MSG     | 20 / 60 s |
| ACK         | 5 / 60 s  |
| ADVERT      | 10 / 60 s |
| GRP_TXT     | 20 / 60 s |
| GRP_DATA    | 5 / 60 s  |
| ANON_REQ    | 5 / 60 s  |
| PATH        | 5 / 60 s  |
| TRACE       | 5 / 60 s  |
| MULTIPART   | 5 / 60 s  |
| CONTROL     | 5 / 60 s  |

---

# Channel Blocking

List blocked channels, with the channel hash byte the filter matches on:

```text
filter channel list
#bot (a3),#test (5c)
```

With nothing blocked the reply is `None`. Drops per channel are reported by
`filter stats channel`.

Add a blocked channel:

```text
filter channel add <channel_name>
```

Examples:

```text
filter channel add #bot
> Filter: channel #bot added
```

Remove a blocked channel:

```text
filter channel remove <channel_name>
```

Example:

```text
filter channel remove #test
> Filter: channel #test removed
```

Up to **16 channels** can be blocked.

Only **Group Text (GRP_TXT)** packets are affected.

---

# Minimum Path Hash Size

Display the current value:

```text
filter hash
> Filter: minimal 1 bytes path hash size
```

Configure the minimum path hash size:

```text
filter hash <bytes>
```

Allowed values:

```text
1
2
3
```

Example:

```text
filter hash 2
> Filter: OK
```

Packets containing fewer path hash bytes than configured are discarded.

Be careful with this one. Nodes flood with 1-byte path hashes by default, so a
minimum of `2` discards nearly all flood traffic rather than just the abusive
part. `filter stats hash` shows exactly that.

Default:

```text
1
```

---

# Malformed Group Message Filtering

Display the current setting:

```text
filter malformed
> Filter: malformed text scan off
```

Enable validation:

```text
filter malformed on
> Filter: malformed scan on
```

Disable validation:

```text
filter malformed off
> Filter: malformed scan off
```

When enabled, Group Text packets are checked for:

* Valid timestamp
* Timestamp within ±1 week
* Valid message structure
* Non-empty text
* Valid UTF-8 encoding

Default:

```text
off
```

---

# Statistics

Display filter status:

```text
filter
> Filter on: Blocked [ Hops: 3 | Rate: 12 | Channel: 1 | Hash: 0 | Malformed: 2 ]
```

Display per-packet-type statistics:

```text
filter count
[TYPE: HOPS,RATE]
00: 0,0
01: 0,0
02: 0,0
03: 0,0
04: 0,0
05: 2,10
06: 0,0
07: 0,0
08: 0,0
09: 0,0
10: 0,0
11: 0,0
```

Meaning:

* Packet Type 05 (Group Text)
* 2 packets blocked by hop limit
* 10 packets blocked by rate limiting

## Per-topic detail

`filter` answers *how much was dropped*; `filter stats <topic>` answers *why,
and against which setting*. Each topic gets its own reply, so it has room for
detail that would not fit on the summary line.

```text
filter stats
> filter stats [ hops | rate | channel | hash | malformed | top ]
```

An unknown topic returns that same list.

Until something has actually been dropped, each topic says so rather than
printing an empty table:

| Command | Reply when nothing was dropped |
| ------- | ------------------------------ |
| `filter stats hops` | `> Filter: no hop drops recorded` |
| `filter stats rate` | `> Filter: no rate drops recorded` |
| `filter stats hash` | `> Filter: no path hash drops recorded` |
| `filter stats malformed` | `> Filter: no malformed drops recorded` |
| `filter stats top` | `> Filter: no source drops recorded` |
| `filter stats channel` | `None`, when no channels are blocked |

The counters live in RAM, so this is also what you see after a reboot or a
`clear stats`.

### hops and rate

Only the packet types that dropped something are listed, with the limit that
caused it in brackets:

```text
filter stats hops
[TYPE: DROPS(MAX)]
04: 326(8)
05: 8097(32)

filter stats rate
[TYPE: DROPS(LIMIT/SECS)]
02: 12(20/60)
```

Type 05 dropped 8097 packets against a hop limit of 32; type 02 dropped 12
against a limit of 20 per 60 seconds. That is the whole question — *is this
limit doing work, and is it set right* — in one line.

### hash

```text
filter stats hash
> Blocked 8097 [1B:8097 2B:0 3B:0]
  Top types: 04:5012 05:2100 02:985
```

The split is the path hash size the dropped packets carried. A large `1B`
figure against a minimum of 2 means the setting is rejecting ordinary traffic
rather than abuse — nodes flood with 1-byte path hashes by default.

### channel

```text
filter stats channel
#bot (a3): 412,#test (5c): 0
```

### malformed

```text
filter stats malformed
> Blocked 12 [ short:1 time:8 empty:0 utf8:3 ]
```

The reasons match the checks in order: payload too short, timestamp zero or
outside the one-week window, plain-text message with no text, and text that is
not valid UTF-8.

### top

```text
filter stats top
> Top drops: a3:412 5c:288 11:190
```

The source identities responsible for the most drops, worst first. The value on
the left is the 1-byte identity hash, which is all a packet carries, so distinct
nodes sharing a leading byte are counted together — treat it as a lead, not as
proof of which node is responsible.

Sources can only be attributed where the payload carries an identity: adverts,
and REQ, RESPONSE, TXT\_MSG, ANON\_REQ and PATH packets. ACK and TRACE hold no
identity and group traffic is encrypted, so those drops appear only in the
totals.

## Limits

Counters saturate rather than wrap, so a busy repeater reports `4294967295`
instead of rolling back to a small number. Any reply clipped by the 160-byte
reply buffer ends in `..`.

Statistics are reset together with the repeater statistics.

---

# Publishing statistics over MQTT (observer builds)

On observer/MQTT firmware the filter statistics are also published as a single
JSON message to a dedicated **`filter`** topic (the same topic prefix as the
`status`/`packets`/`neighbors` messages, with the suffix `filter`). This lets an
analyzer monitor a repeater's shaping live, instead of polling `filter stats`
over RF. Unlike the CLI reply there is no single-packet size limit, so the whole
counter set plus the per-type configuration is sent uncut.

The message carries: an identity envelope (`origin`, `origin_id`, `timestamp`,
`uptime_secs`, `boot_id`, `enabled`); `totals` per reason; per-type `hops` and
`rate` drops (non-zero types only); the `hash` size split and top blocked types;
`malformed` reasons; per-channel `channels`; the worst `top_sources`; a
`config` block with each type's `limit`/`secs`/`soft`/`hops_max`; and a
`region_gate` block with the live duty-cycle region-gating state.

Because the counters are cumulative and saturate, an analyzer derives drop
**rates** by differencing consecutive samples, and uses `boot_id`/`uptime_secs`
to detect a reboot (counter reset) and rebaseline. The `config.soft` vs
`config.limit` pair distinguishes the probabilistic soft cutoff (graceful
shaping) from a hard block.

### region_gate

The current state of duty-cycle **region gating** (see
[Duty-cycle region gating](cli_commands.md#duty-cycle-region-gating) and
`get dc.gate.status` in the CLI docs).
Unlike the drop counters this is instantaneous state, not a cumulative counter,
so it is read directly each sample:

```json
"region_gate": {
  "enabled": true,
  "duty": 74,
  "level": 2,
  "max_level": 4,
  "threshold": 70,
  "hysteresis": 10
}
```

| Field | Meaning |
| --- | --- |
| `enabled` | Whether the feature is switched on (`set dc.gate 1`). |
| `duty` | Live TX duty cycle, 0–100 % of the permitted airtime budget. |
| `level` | Outer region layers currently gated: `0` = none, `1` = wildcard `*`, higher = broader named regions inward. Always protects the innermost cluster and the home region. |
| `max_level` | Highest gate level this repeater's region hierarchy allows (`0` when it has no named regions and thus never gates). |
| `threshold` | Config: start gating when `duty` exceeds this. |
| `hysteresis` | Config: recover (re-open regions) once `duty` falls below `threshold − hysteresis`. |

Plotting `duty` and `level` across the mesh shows, live, which repeaters are
shedding inter-region traffic and how hard — the intended congestion signal.
`enabled: false` means the repeater is not gating; `duty` is still reported and
is useful on its own.

## Interval

```text
set mqtt.filter.interval <seconds>
```

Publishes every `<seconds>` (clamped to the 60–600 s band); `0` disables the
topic. Default **60 s**. Read it with `get mqtt.filter.interval`. The value is
stored in `/mqtt.json`.

---

# Persistent Configuration

Filter settings are stored in:

```text
/filter_prefs
```

The following settings persist across reboots:

* Filter enabled/disabled
* Hop limits
* Rate limits
* Blocked channels
* Minimum hash size
* Malformed message filtering

---

# Recommended Configuration

In any case, if you want to block the spammer (July 2026), make sure to use the following, which is included in the busy repeater example, but not in the typical repeater.

```text
filter rate 02 5 60
```

## Typical Public Repeater

```text
filter on
filter hash 1
filter malformed on
filter rate 05 20 60
filter rate 02 20 60
filter hops 05 32
```

## Busy or Abused Repeater

```text
filter on
filter hash 2
filter malformed on
filter rate 05 10 60
filter rate 02 5 60
filter rate 04 5 60
filter hops 05 16
filter hops 02 16
filter hops 04 8
```

Take into account that `filter hash 2` will block (not forward) all "legacy" packets that are not using multibyte paths.

## Blocking a Noisy Channel

```text
filter channel add <channel_name>
```

Monitor the output of:

```text
filter
filter stats channel
```

to understand the effect. `filter stats channel` reports the drops per blocked
channel, so you can tell which entry is doing the work. Take into account that this will prevent your repeater from forwarding the packet to other repeaters and companions, but it will still receive them.

---

# Important Notes

* Filtering is disabled by default. You have to enable it.
* Only forwarded packets are filtered.
* Direct-routed packets always bypass the filter.
* Channel blocking only affects `GRP_TXT` packets.
* Malformed message validation only applies to `GRP_TXT` packets.
* Rate limits are applied per packet type, not per sender.
* Statistics live in RAM only. They are cleared on reboot and by `clear stats`, and they are never written to `/filter_prefs`.
* Counters saturate at their maximum rather than wrapping around to zero.
* A reply that does not fit the 160-byte CLI buffer ends in `..`.
* `filter <setting>` shows and changes a setting; `filter stats <topic>` reports what it dropped.

---

# Error Replies

| Reply | Cause |
| ----- | ----- |
| `> Filter: command error` | Unrecognised subcommand |
| `> Filter: error <type> range is 0-11` | Packet type outside `00`–`11` |
| `> Filter: error <max_hops> range is 0-64` | Hop limit outside `0`–`64` |
| `> Filter: error hash bytes range is 1-3` | Minimum path hash size outside `1`–`3` |
| `> Filter: syntax error 'filter hops <type> <max_hops>'` | Wrong number of arguments |
| `> Filter: syntax error 'filter rate <type> <limit> <secs>'` | Wrong number of arguments |
| `> Filter: syntax error 'filter channel [list \| add \| remove] <#name \| Public>'` | Wrong number of arguments |
| `Failed` | `filter channel add` with the list full, or `remove` with no such channel |

An unknown `filter stats` topic is not an error: it returns the list of topics.
