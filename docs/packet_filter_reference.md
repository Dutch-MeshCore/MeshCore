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

---

# Enabling the Filter

Display the current status:

```text
filter
```

Enable filtering:

```text
filter on
```

Disable filtering:

```text
filter off
```

Reset all settings to their defaults:

```text
filter reset
```

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
```

Set the maximum hop count for a packet type:

```text
filter hops <type> <max_hops>
```

Example:

```text
filter hops 05 16
```

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
```

Configure a rate limit:

```text
filter rate <type> <limit> <seconds>
```

Example:

```text
filter rate 05 20 60
```

This allows up to **20 Group Text packets every 60 seconds**.

Setting the limit to **0** disables rate limiting for that packet type.

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

List blocked channels:

```text
filter channel list
```

Add a blocked channel:

```text
filter channel add <channel_name>
```

Examples:

```text
filter channel add #bot
filter channel add #test
```

Remove a blocked channel:

```text
filter channel remove <channel_name>
```

Example:

```text
filter channel remove #test
```

Up to **16 channels** can be blocked.

Only **Group Text (GRP_TXT)** packets are affected.

---

# Minimum Path Hash Size

Display the current value:

```text
filter hash
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
```

Packets containing fewer path hash bytes than configured are discarded.

Default:

```text
1
```

---

# Malformed Group Message Filtering

Display the current setting:

```text
filter malformed
```

Enable validation:

```text
filter malformed on
```

Disable validation:

```text
filter malformed off
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
```

Example:

```text
Filter on: Blocked [ Hops: 3 | Rate: 12 | Channel: 1 | Hash: 0 | Malformed: 2 ]
```

Display per-packet-type statistics:

```text
filter count
```

Example:

```text
05: 2,10
```

Meaning:

* Packet Type 05 (Group Text)
* 2 packets blocked by hop limit
* 10 packets blocked by rate limiting

Statistics are reset together with the repeater statistics.

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
filter count
```

to understand the effect. Take into account that this will prevent your repeater from forwarding the packet to other repeaters and companions, but it will still receive them.

---

# Important Notes

* Filtering is disabled by default. You have to enable it.
* Only forwarded packets are filtered.
* Direct-routed packets always bypass the filter.
* Channel blocking only affects `GRP_TXT` packets.
* Malformed message validation only applies to `GRP_TXT` packets.
* Rate limits are applied per packet type, not per sender.