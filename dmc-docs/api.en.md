# Repeater API

The DMC "API" is the repeater/observer CLI, reachable over several transports. There is
**no REST `/api/*` interface** in DMC.

## What it is

Every `get`/`set` command you type on the serial console can also be sent
programmatically. Scripts and dashboards that read status or set options talk to that same
CLI.

## Transports

- **USB serial** at 115200 baud.
- **TCP CLI on port 23** for `_ethernet` builds (RAK4631). Status via `eth.status`.

## Reply contract

Commands return a text reply. The reply buffer is a fixed **160 bytes**. Treat the reply
strings as a stable consumed interface: mc2mqtt, CoreScope, core-hunter, and terminal.js,
among others, parse them.

## Examples

Serial (send a command and read the reply):

```text
get mqtt.status
```

TCP on an ethernet build:

```bash
# Connect to the CLI on port 23 and ask for status
nc <repeater-ip> 23
get mqtt.status
```

## Common use cases

- Dashboards and scripts that read current status.
- Automating `set` commands when rolling out repeaters.
