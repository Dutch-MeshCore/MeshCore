# MeshCore Companion Receive Filter

## Overview

A DMC-only filter in the companion firmware that keeps junk and unwanted channel
messages from reaching the app. It is the receive-side counterpart of the
[repeater packet filter](packet_filter_reference.md) and uses the same rule
engine, list syntax and reply formats where the two overlap.

It filters on:

* Malformed group text (zero or implausible timestamp, empty text, invalid UTF-8)
* Message age (group text older than N minutes)
* Blocked path prefixes (anything relayed by one specific repeater)
* Sender name or message text of a group text (block, throttle, or dose)

The filter is **disabled by default**.

Only flood traffic is filtered. Direct messages, and anything else addressed to
this node, are never touched. Content checks run on the channels the companion
holds the key for, which are exactly the channels the user sees.

---

# Hidden or dropped

A companion normally does not repeat, so for most users every filtered message
is simply not shown. The difference only matters with repeat enabled:

| Reason | Verdict | Shown in the app | Repeated (repeat on) |
| --- | --- | --- | --- |
| malformed, age, path | **drop** (junk) | no | no |
| sender, text | **hide** (personal) | no | yes |

Junk helps nobody, so it is not passed on. A sender or text rule is one user's
taste: the message is hidden here, but still repeated for everyone else, so a
mute list on a repeating companion never censors the mesh.

---

# Command Overview

The commands run through the companion CLI: from the app's command console
(companion protocol `CMD_RUN_CLI_COMMAND`, v14+), over USB serial, or as remote
CLI from a contact that is allowed to use it.

```text
filter
filter help
filter on
filter off
filter reset

filter malformed
filter malformed on|off

filter age
filter age <minutes>|off

filter path list
filter path add <hex>
filter path remove <hex>

filter sender list
filter sender add <name> [secs] [prob]
filter sender remove <name>

filter text list
filter text add <pattern> [secs] [prob]
filter text remove <pattern>

filter stats
filter stats malformed|age|path|sender|text
```

Every change is saved immediately.

---

# Enabling the Filter

```text
filter on
> Filter: on
```

`filter off` keeps every setting but filters nothing. `filter reset` restores the
defaults (everything off, all lists empty); the counters are not affected.

---

# Malformed Group Text

```text
filter malformed on
> Filter: malformed scan on
```

Drops group text whose timestamp is zero, or more than a week off this node's
clock (only once the clock has been set), and plain text that is empty or not
valid UTF-8. These are the same checks as the repeater filter, but on every
channel the companion holds rather than on Public only.

---

# Message Age Limit

```text
filter age 60
> Filter: OK
```

Drops group text stamped more than this many minutes ago (1–10080, or `off`).
Stale messages are what a misbehaving node replays hours later. Inactive while
this node's clock is not set; companions usually get the time from the app.

---

# Blocked Path Prefixes

```text
filter path add A1B2
> Filter: path A1B2 added
```

Drops flood adverts, group text and group data whose path contains a repeater ID
starting with the prefix (2–8 hex digits, up to 8 prefixes). Use it when one
repeater keeps injecting junk. A prefix longer than the packet's path hash size
never matches. Note that a blocked advert also no longer refreshes an existing
contact.

---

# Sender and Text Rules

```text
filter sender add Bot*
> Filter: sender Bot* added
filter text add ^!ping 300
> Filter: text ^!ping added
```

Up to 8 sender rules and 8 text rules, checked top to bottom; the first rule that
decides hides the message.

* **Sender**: exact name, or a prefix when it ends in `*`. Case-sensitive.
* **Text**: substring, or the start of the message when it begins with `^`.
* **secs** (optional): `0` (default) hides every match. Otherwise one match per
  `secs` seconds is shown and the rest are hidden, which slows a chatty bot down
  instead of muting it.
* **prob** (optional, 1–100, default 100): the share of matches the rule decides;
  the rest go on to the next rule.

`filter sender list` / `filter text list` show the rules:

```text
filter text list
^!ping throttle 300s
```

---

# Statistics

```text
filter
> Filter on: Hidden [ Malformed: 3 | Age: 0 | Path: 12 | Sender: 41 | Text: 7 ]
```

Per topic:

| Command | Reply |
| --- | --- |
| `filter stats malformed` | `> Blocked 3 [ short:0 time:2 empty:0 utf8:1 ]` |
| `filter stats age` | `> Message age: max 60m, dropped 0` (or `> Filter: message age limit off`) |
| `filter stats path` | `A1B2: 12` per prefix, or `None` |
| `filter stats sender` / `text` | `Bot*: 41` per rule, with `(pass N)` for throttle passes |

Counters live in RAM and restart at zero after a reboot. A message is counted
once, even when several repeaters relay it.

---

# Persistent Configuration

Settings are stored as JSON in `/cfilter.json` on the companion's internal
filesystem, apart from the main `/prefs.json`. A missing file means defaults
(off). Out-of-range values in a hand-edited file are clamped on load.

---

# Error Replies

| Reply | Cause |
| --- | --- |
| `> Filter: command error` | unknown subcommand |
| `> Filter: syntax error 'filter malformed <on \| off>'` | bad `malformed` argument |
| `> Filter: error <minutes> range is 1-10080, or off` | bad `age` argument |
| `> Filter: error path prefix is 2-8 hex digits` | bad path prefix |
| `> Filter: error <pattern> max N chars` | sender pattern over 15, text pattern over 23 characters |
| `> Filter: error <secs> range is 0-65535, <prob> range is 1-100` | bad rule arguments |
| `Failed` | duplicate entry, list full, or nothing to remove |
