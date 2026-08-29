# Remote command execution over MQTT: review findings

- Date: 2026-08-29
- Subject: `agessaman/feat/remote-control` (6 commits, **not merged** into
  `observer-firmware` or `observer-firmware-dev`)
- Status: **not adopted.** Deferred to its own spec. This document exists so the
  findings below are not rediscovered from scratch.

DMC has not merged this branch. If it is ever picked up, the defects in section 3
must be closed first: as written, the feature does not deliver the security
property it appears to.

---

## 1. What the branch provides

JWT-authenticated CLI execution over the existing MQTT bridge.

| Commit | Contents |
|---|---|
| `02831a0f` | `RemoteControl.{h,cpp}` policy engine plus 363 lines of host tests |
| `915b71c2` | Ed25519 **verification** added to `JWTHelper` (it only signed before) |
| `ffdf48b5` | `MQTTPrefs` append: `mqtt_remote_enabled`, `mqtt_use_acl`, `mqtt_admin_public_key`, `mqtt_slot_remote_enabled[]` |
| `e0cea569` | CLI: `set/get mqtt.remote`, `mqtt.useacl`, `mqtt.admin`, per-slot `mqttN.remote` |
| `4be30bff` | Bridge wiring, per-slot, with a global kill switch |
| `1d839e44` | `MQTTRemoteCallbacks.h` binding the variant's `ClientACL` and `CommonCLI` |

The design is sound in outline, and the separation is good: `RemoteControl` owns
the security decisions behind injected seams (crypto, authorizer, executor,
clock), so the whole pipeline is host-testable without MQTT or a radio.

**Note:** the `MQTTPrefs` append in `ffdf48b5` is obsolete. Observer preferences
are JSON (`/mqtt.json`) since the 2026-08-29 catch-up, so those fields become
plain struct members plus serializer keys, with no offset contract.

---

## 2. The intended security model

Two-party, and the broker is deliberately **not** one of the parties that can
authorise:

1. The broker only decides who may put a command on the wire. `authorizePublish`
   in `Dutch-MeshCore/collector` lets a role-1 admin subscriber publish
   `meshcore/{IATA}/{PUBKEY}/serial/commands`.
2. The command is an Ed25519-signed JWT. The device verifies the signature and
   checks the signing key against either its own `ClientACL` admin list
   (`mqtt.useacl on`) or an explicit `set mqtt.admin <pubkey>`.

So a broker admin can *deliver* a command but should not be able to *forge* one.
Responses are signed by the device key, so a client can verify a reply genuinely
came from that node.

Defence in depth as built: 10-entry nonce replay buffer, 1 command/sec per
signing key, prefix blacklist (`get wifi.pwd`, `set mqtt.admin`), a hardcoded
`reboot` refusal, target filtering against the device id, a 5 s execution
timeout, 60 s response TTL, and a global plus per-slot kill switch.

---

## 3. Defects

### 3.1 Request tokens have no expiry, and nothing checks one

`RemoteCommandRequest` is `{command, target, nonce, public_key}`. There is no
`iat` or `exp` field, and `RemoteControl::process()` never validates one.
Responses get `exp = iat + RESPONSE_TTL_SEC`; requests get nothing.

**A command token is valid forever.** Any UI offering "set a lifetime" would be
describing something the device does not enforce.

### 3.2 The nonce ring wraps after ten commands

`RCNonceTracker::MAX_NONCES = 10`, a circular buffer. Replay protection holds for
ten authorised commands, after which the oldest nonce is evicted and its token
replays successfully.

### 3.3 Combined, 3.1 and 3.2 break the two-party model

The command payload is visible to the broker and to every role-1 admin
subscriber. With no expiry and a ten-deep replay window, **a captured token is a
permanent capability, not a one-shot**. A broker admin cannot forge a command,
but can capture and replay any command an owner has ever issued, which is nearly
as useful to an attacker.

### 3.4 `setperm` is not blacklisted

`setperm {pubkey} {perms}` grants `PERM_ACL_ADMIN` and is a normal CLI command
reachable remotely. It is simultaneously the natural key-rotation mechanism and
the worst thing to leak: one captured `setperm` token is permanent ownership of
the node, surviving any later revocation.

`set mqtt.admin` *is* blacklisted, which shows the bootstrap risk was considered;
`setperm` reaches the same outcome by another route.

### 3.5 Expiry checking would need to fail closed on an unsynced clock

`RemoteControlClock::unixNow()` returns 0 when the clock is unset, and `process()
falls back to `millisNow() / 1000`. Adding `exp` validation on top of that
fallback would let any node that has not reached NTP accept expired tokens. It
must reject instead.

Relevant because the catch-up brought agessaman's NTP/clock-correctness work: the
accepted NTP epoch is now authoritative before JWT work and the RTC is consulted
when libc cannot vouch for the fallback. Build on that rather than the older
behaviour.

---

## 4. Minimum changes before adoption

1. Add `exp` to the request payload and validate it. Without this, lifetime is
   unenforceable.
2. Size the nonce ring against (max token lifetime x rate limit) rather than a
   flat 10, or replace it with a monotonic per-key counter.
3. Blacklist `setperm`, or gate it behind enforced expiry. Prefer blacklisting
   and making rotation an out-of-band operation.
4. Fail closed on an unsynced clock once expiry is enforced.

None are large. Items 1 and 4 are a field and an early return.

---

## 5. Notes for whoever specs this

**Per-user keys work, with one asymmetry.** A per-user Ed25519 keypair whose
public half is enrolled on the node and whose private half never leaves the
browser is the right model. But if the private key never touches the server, the
server cannot revoke anything: revocation is a per-node ACL edit, and reaching
the node to make that edit needs a *different* valid key. Revoking the last key
leaves serial as the only recovery. A break-glass key held offline should be part
of enrolment rather than discovered later.

**`mqtt.admin` holds one key.** Multi-user requires `mqtt.useacl on` and the
node's `ClientACL`, which holds `MAX_CLIENTS` (20) and usefully never evicts
admin entries.

**Bootstrap is necessarily out-of-band**, which is correct: the first key must
arrive over serial, WebConfig, or a LoRa admin session.

**On-device signing is the better key story, and it already exists.** MeshCore
companion firmware implements `CMD_SIGN_START` / `CMD_SIGN_DATA` /
`CMD_SIGN_FINISH` (`examples/companion_radio/MyMesh.cpp`), letting a host get
bytes signed without the key leaving the radio. `EU-Meshcore-Analyzer` uses
exactly this for its MQTT JWTs. Note it is companion-only: repeater and observer
firmware do not implement the Sign commands.

**Scope collision worth naming.** `DutchMeshCore-Observers`
`docs/observer-analyzer-roadmap.md` states the app "is an ANALYZER, not a device
console ... does not configure repeaters, send messages, expose a terminal, or
otherwise control devices." Building an operator-facing remote console there
reverses a documented decision. Fine to reverse, but do it explicitly and update
that document rather than letting it drift.

**Two implementations of one topic.** `serial/commands` predates this firmware
work; the broker comment points at `meshcoretomqtt`, a host-side bridge that
verifies the JWT on the *host* and writes to a serially-attached node. On that
path the node has no idea a command came from MQTT and none of the on-device
protections above apply. If a portal ever offers remote control, it should offer
it only for firmware-path nodes, or the guarantee silently differs per node.
