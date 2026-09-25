# MeshCore Simple Repeater Packet Filter

## Overview

The filter can selectively block forwarded packets based on:

* Hop count
* Per-packet-type rate limits
* Per-origin advert window (each node's advert at most once per N hours)
* Blocked path prefixes (isolate one upstream repeater)
* Minimum path hash size
* Name of a group channel
* Sender name or message text of a group text (block, throttle, or dose)
* Malformed group text messages
* Packet type

A **dry-run** mode counts every drop but still forwards, so a setting can be
sized before it is enforced. Every drop also adds the packet's estimated
time-on-air to a **saved airtime** counter.

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
| `filter dryrun` | Current dry-run state |
| `filter dryrun on` / `filter dryrun off` | Count drops without dropping (see [Dry-run](#dry-run)) |
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
| `filter advert` | Current per-origin advert window and cache fill |
| `filter advert <hours>` | Set the window (0-720 h; 0 = off) |
| `filter advert clear` | Forget every remembered advert origin |
| `filter path list` | Blocked path prefixes |
| `filter path add <hex>` / `filter path remove <hex>` | Block or unblock a path prefix |
| `filter sender list` | Sender rules, in evaluation order |
| `filter sender add <name> [secs] [prob]` | Block, throttle or dose a sender (see [Sender and text rules](#sender-and-text-rules)) |
| `filter sender remove <name>` | Remove a sender rule |
| `filter text list` / `filter text add <pattern> [secs] [prob]` / `filter text remove <pattern>` | The same for message text |
| `filter watch list` | Channels the rules can read (Public plus the watch list) |
| `filter watch add <#name>` / `filter watch remove <#name>` | Add or remove a channel the rules read |
| `filter age` | Current message age limit and its drops |
| `filter age <minutes>` / `filter age off` | Drop group texts older than this (1-10080 min; see [Message age limit](#message-age-limit)) |
| `filter stats <topic>` | Drops for one topic, in detail |

The same list is available on the device:

```text
filter help
> filter [ help | on | off | reset | dryrun | types | count | stats | hops | rate | channel | hash | malformed | advert | path | sender | text | watch | age ]
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

## Dry-run

Not sure a limit is right? Turn on dry-run: every rule is evaluated and every
drop is **counted exactly as it would be**, but the packet is still forwarded.

```text
filter dryrun on
> Filter: dry-run on
```

While it is on, the status line carries a marker so the counters are not
mistaken for real drops:

```text
filter
> Filter on: Blocked [ Hops: 3 | Rate: 12 | Channel: 1 | Hash: 0 | Malformed: 2 ] (dry-run)
```

Watch `filter stats <topic>` and `filter stats air` until the numbers look
right, then switch it off to enforce:

```text
filter dryrun off
> Filter: dry-run off
```

Dry-run is persisted like every other setting, so a repeater left in dry-run
stays in dry-run across a reboot. Default: `off`.

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

# Per-Origin Advert Window

The per-type rate limit caps the *total* advert budget, so a node that
re-advertises every few minutes eats the budget and legitimate nodes get
dropped with it. The advert window works per node instead: **each origin's
flood advert is forwarded at most once every N hours**. It still passes each
node's advert once per window, so every node stays reachable through this
repeater.

Display the current window:

```text
filter advert
> Filter: advert origin window 0h (cache 0/256)
```

Set the window:

```text
filter advert <hours>
```

Example:

```text
filter advert 48
> Filter: OK
```

Each origin is now re-flooded at most once every 48 hours. Allowed values are
`0` to `720` hours (30 days); `0` turns the window off. Origins are keyed on
the first 4 bytes of the advert's public key and kept in a 256-entry cache;
when it fills, the oldest entry is forgotten. The cache lives in RAM, so after
a reboot every origin gets one free pass.

Forget every remembered origin without changing the window:

```text
filter advert clear
> Filter: advert cache cleared
```

The window runs *before* the per-type advert rate limit, so a repeat advert
never consumes the budget of a legitimate one. Drops are reported by
`filter stats advert`.

Default:

```text
0 (off)
```

---

# Blocked Path Prefixes

Every flood packet carries the IDs of the repeaters that relayed it. When one
upstream repeater keeps injecting junk, block its ID prefix and everything that
travelled through it is dropped here, without needing the key to any channel.

List the blocked prefixes:

```text
filter path list
A1B2,C3
```

With nothing blocked the reply is `None`.

Block a prefix:

```text
filter path add <hex>
```

Example:

```text
filter path add a1b2
> Filter: path A1B2 added
```

A prefix is 2 to 8 hex digits (1 to 4 bytes), case-insensitive. Up to
**8 prefixes** can be blocked. A packet is dropped when **any** entry of its
path starts with a blocked prefix.

Unblock a prefix:

```text
filter path remove A1B2
> Filter: path A1B2 removed
```

Two things to know:

* A prefix is compared against whole path entries, aligned to the packet's
  path hash size. `A1B2` is one 2-byte ID: it matches a 2-byte entry starting
  `A1B2`, never the two 1-byte entries `A1` then `B2`. A prefix **longer** than
  the packet's hash size never matches, so to catch a repeater that floods with
  1-byte hashes, block its 1-byte prefix (`A1`).
* Short prefixes collide: `A1` also drops every other repeater whose ID starts
  with `A1`. Use the longest prefix the traffic's hash size allows.

Drops per prefix are reported by `filter stats path`.

---

# Sender and Text Rules

Group texts decrypt to `SenderName: message text`. A sender rule matches the
name, a text rule matches the message, and each rule either **blocks** every
match, **throttles** it (one match per N seconds slips past, the rest are
dropped), or **doses** it (the rule decides only a percentage of its matches).
This is the tool for one bot flooding a channel you would rather keep: slow it
down instead of muting the channel.

```text
filter sender add <name> [secs] [prob]
filter text add <pattern> [secs] [prob]
```

| Argument | Meaning |
| -------- | ------- |
| `name` | Sender name, exact and case-sensitive. End it with `*` for a prefix (`Bot*` matches `Bot1`, `BotXY`); a lone `*` matches every sender. Up to 15 characters. |
| `pattern` | Text to find anywhere in the message, case-sensitive. Start it with `^` to match only at the beginning. One word, no spaces. Up to 23 characters. |
| `secs` | `0` (default) blocks. `1`–`65535` throttles: one matching message per that many seconds passes, the excess is dropped. |
| `prob` | `1`–`100` (default `100`): the share of matches the rule decides. `50` drops half. `0` is rejected: remove the rule instead. |

Examples:

```text
filter sender add SpamBot            # drop everything SpamBot says
filter sender add Bob 60             # Bob gets one message a minute; the rest is dropped
filter sender add Bot* 0 50          # every Bot… name loses half its messages
filter text add ^BEACON              # drop messages that start with BEACON
filter text add RX_in_place 600      # at most one such report per 10 minutes
filter sender remove Bob
```

How the rules are applied:

* Sender rules are evaluated top to bottom, then text rules, on every group
  text the repeater can decrypt. **The first rule that decides drops the
  packet.** A rule that *steps aside* (a failed dosing roll, or a throttle pass
  within budget) leaves the decision to the next rule, so a throttled sender
  can still be caught by a later catch-all.
* A throttle window is not extended by over-rate messages: a sender pushing
  hard still gets exactly one pass per window. Throttle state lives in RAM, so
  the first match after a reboot is a free pass.
* Dosing rolls per received packet with the same random source as the soft
  cutoff; a retransmitted copy rolls again.
* Only **plain** group texts carry a `Sender: text` shape. Binary group data,
  adverts and everything else never match a sender or text rule.
* Up to **8 sender rules** and **8 text rules**. Adding a rule that already
  exists, or beyond the limit, fails.

## Which channels the rules can read

The rules only see traffic the repeater can decrypt. **Public** is always
readable. For a `#name` channel the key is derived from the name, exactly as
the companion apps do, so add it to the watch list and it becomes readable too:

```text
filter watch add #bots
> Filter: watch #bots added
filter watch list
Public (11),#bots (a3)
```

Up to **4** watched channels. Private channels with a shared key cannot be
watched. A watched channel is *read*, not blocked; to block one use
`filter channel add`. A message on a watched channel is only decrypted when
at least one sender or text rule exists or the [message age limit](#message-age-limit)
is on, so the watch list costs nothing until you use it.

Drops per rule are reported by `filter stats sender` and `filter stats text`;
throttle passes are shown alongside.

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

The ±1 week check is skipped while the repeater's clock has not been set (see
[Message age limit](#message-age-limit)); a zero timestamp is always rejected.
The malformed scan reads the Public channel only.

Default:

```text
off
```

---

# Message Age Limit

Drops group texts whose timestamp is older than a set number of minutes, so old
messages that are replayed or trickle in late are not repeated again.

```text
filter age 60
> Filter: OK
filter age
> Message age: max 60m, dropped 0
filter age off
> Filter: OK
```

* Range **1-10080 minutes** (one week). `off` or `0` turns it off. Default: off.
* It checks the time the **sender** put in the message against the repeater's
  own clock. A message stamped in the future is never counted as old.
* Only channels the repeater can read are checked: **Public**, plus any `#`
  channel on the [watch list](#which-channels-the-rules-can-read). Direct
  messages and other channels are encrypted with keys the repeater does not
  have, so their age cannot be read and they are never dropped by this.
* It works independently of `filter malformed`.

**The repeater's clock must be right.** A repeater without a battery-backed
clock or GPS starts at a default date after a reboot, until it is synced (for
example with `clock sync` from a companion app). While the clock reads earlier
than 2026, the age limit does nothing, and `filter age` shows
`(clock not set, inactive)`. A clock that is set but wrong makes the limit
wrong in the same way, so check `clock` before relying on it.

**Choose a margin for other people's clocks.** A phone or node whose clock runs
behind stamps its messages in the past. With a tight limit its messages look
old and are dropped. Start generous (for example `filter age 60`), try it with
`filter dryrun on`, and check `filter stats age` before going lower.

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
> filter stats [ hops | rate | channel | hash | malformed | top | advert | path | air | sender | text | age ]
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
| `filter stats advert` | `> Filter: advert origin limit off`, when the window is 0 |
| `filter stats path` | `None`, when no prefixes are blocked |
| `filter stats air` | `> Filter: no airtime saved yet` |
| `filter stats sender` / `filter stats text` | `None`, when no rule is set |
| `filter stats age` | `> Filter: message age limit off`, when no limit is set |

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

### advert

```text
filter stats advert
> Advert origins: window 48h, dropped 132, cache 87/256
```

The window in force, the adverts dropped because their origin had already
passed inside it, and how many origins the cache currently remembers.

### age

```text
filter stats age
> Message age: max 60m, dropped 37
```

The limit in force and the group texts it dropped. While the clock is not set
the line ends in `(clock not set, inactive)`.

### path

```text
filter stats path
A1B2: 412,C3: 0
```

Drops per blocked prefix, so you can tell which entry is doing the work.

### sender and text

```text
filter sender list
Bob throttle 60s,Bot* block 50%,SpamBot block

filter stats sender
Bob: 12 (pass 3),Bot*: 40,SpamBot: 5
```

The setting view shows each rule's mode; the stats view shows the drops it
caused and, for a throttle, how many messages it let pass within budget.

### air

```text
filter stats air
> Filter: saved airtime 214500 ms (3m 34s)
```

The estimated time-on-air the dropped packets would have taken to retransmit,
using the same estimate the repeater bills its own airtime with. In dry-run it
is what the drops *would* have saved. This is usually the number that matters
on a shared channel.

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
`uptime_secs`, `boot_id`, `enabled`, `dryrun`); `totals` per reason (`hops`,
`rate`, `channel`, `hash`, `malformed`, `advert`, `path`); `air_ms`, the
estimated time-on-air the drops saved; per-type `hops` and `rate` drops
(non-zero types only); the `hash` size split and top blocked types;
`malformed` reasons; per-channel `channels`; the worst `top_sources`; the
`advert` window state; the blocked `paths` with their drops; the `senders` and
`texts` rules with their drops and passes, and the `watch` list; a `config`
block with each type's `limit`/`secs`/`soft`/`hops_max`; and a `region_gate`
block with the live duty-cycle region-gating state.

### dryrun, advert, paths, air_ms

```json
"dryrun": false,
"air_ms": 214500,
"advert": { "window_h": 48, "cache": 87, "cache_size": 256 },
"paths": [ { "prefix": "A1B2", "drops": 412 }, { "prefix": "C3", "drops": 0 } ]
```

| Field | Meaning |
| --- | --- |
| `dryrun` | `true` while [dry-run](#dry-run) is on: every counter in the message then reports what *would* have been dropped, and nothing was. |
| `totals.advert` | Adverts dropped by the [per-origin advert window](#per-origin-advert-window). |
| `totals.path` | Packets dropped by the [blocked path prefixes](#blocked-path-prefixes). |
| `air_ms` | Cumulative estimated time-on-air (ms) the drops saved, same estimate as `filter stats air`; in dry-run, what they would have saved. |
| `advert.window_h` | The configured window in hours, `0` = off. |
| `advert.cache` / `advert.cache_size` | Origins currently remembered and the cache capacity; `cache` at capacity means the oldest origins are being forgotten early. |
| `paths[]` | One entry per blocked prefix (upper-case hex) with its drops. Omitted when nothing is blocked. |
| `totals.sender` / `totals.text` | Drops by the [sender and text rules](#sender-and-text-rules). |
| `senders[]` / `texts[]` | One entry per rule, in evaluation order: `pattern`, `secs` (0 = block), `prob`, `drops`, and `pass` (throttle passes within budget). Omitted when no rule is set. |
| `watch[]` | Channel names the rules may read besides Public. Omitted when empty. |

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
* Dry-run
* Advert window (hours)
* Blocked path prefixes
* Sender rules, text rules and the watch list
* Message age limit

Files written by older firmware are shorter; the settings they do not contain
load at their defaults. The advert origin cache is not persisted.

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
filter advert 24
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

## Slowing One Sender Down Instead of Muting Them

```text
filter watch add #test
filter sender add Bob 60
filter stats sender          # Bob: <drops> (pass <passes>)
```

Bob still gets one message a minute through this repeater. Like every sender
rule this matches the *name in the message*, not a verified identity: a
renamed sender evades it.

## Isolating One Upstream Repeater

```text
filter path add <hex prefix>
```

Monitor `filter stats path` to confirm the entry is doing the work, and
remember that a short prefix also catches every other repeater whose ID starts
the same way.

## Sizing a Setting Before Enforcing It

```text
filter dryrun on
... apply the settings you are considering ...
filter stats air
filter dryrun off
```

Nothing is dropped while dry-run is on; the counters show what *would* be.

---

# Important Notes

* Filtering is disabled by default. You have to enable it.
* In dry-run every drop is counted but nothing is dropped; the status line ends in `(dry-run)`.
* Only forwarded packets are filtered.
* Direct-routed packets always bypass the filter.
* Channel blocking only affects `GRP_TXT` packets.
* Malformed message validation only applies to `GRP_TXT` packets.
* Rate limits are applied per packet type, not per sender. The advert window is the exception: it is per origin.
* Path prefixes are matched against whole path entries; a prefix longer than the packet's hash size never matches.
* Sender and text rules read only Public and watched `#` channels, only plain group texts, and match names as text, not verified identities.
* The message age limit reads the same channels and trusts the repeater's clock; it is inactive while that clock is not set.
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
| `> Filter: error <hours> range is 0-720` | Advert window outside `0`–`720` |
| `> Filter: error <minutes> range is 1-10080, or off` | Message age limit out of range or not a number |
| `> Filter: error path prefix is 2-8 hex digits` | Path prefix empty, odd-length, longer than 4 bytes, or not hex |
| `> Filter: error <secs> range is 0-65535, <prob> range is 1-100` | Sender/text rule argument out of range or not a number |
| `> Filter: error <pattern> max 15 chars` / `max 23 chars` | Sender name or text pattern too long |
| `> Filter: syntax error 'filter dryrun <on \| off>'` | Wrong argument |
| `> Filter: syntax error 'filter hops <type> <max_hops>'` | Wrong number of arguments |
| `> Filter: syntax error 'filter rate <type> <limit> <secs>'` | Wrong number of arguments |
| `> Filter: syntax error 'filter channel [list \| add \| remove] <#name \| Public>'` | Wrong number of arguments |
| `Failed` | `filter channel add` with the list full, or `remove` with no such channel; `filter path add` with the list full or the prefix already blocked, or `remove` with no such prefix; the same for `filter sender`, `filter text` and `filter watch` (`watch add Public` also fails: it is always watched) |

An unknown `filter stats` topic is not an error: it returns the list of topics.
