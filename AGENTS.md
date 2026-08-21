# AGENTS.md — Dutch-MeshCore contributor guide

Rules that are specific to this fork and are not visible from the code. Everything else — what
MeshCore is, how to install PlatformIO, the board matrix, the packet format — is in `README.md` and
`docs/`. Read those; do not duplicate them here.

## 1. What this fork is

Dutch-MeshCore is a **downstream-only** fork of [meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore).
Changes flow in, never out. The fork is regularly synced so it tracks the latest upstream release.

The base branch is **`dmc-dev`**, not `main`, `master` or `dev`.

```bash
git remote -v
# upstream  https://github.com/Dutch-MeshCore/MeshCore.git   <- this fork
# mc        https://github.com/meshcore-dev/MeshCore.git     <- what it tracks
# origin    your own fork

git fetch upstream
git checkout upstream/dmc-dev -b feature/<name>
```

`README.md` and `CONTRIBUTING.md` are byte-identical to meshcore-dev's. They are written for the
upstream project and are wrong here on three points:

| `CONTRIBUTING.md` says | Here |
| ---------------------- | ---- |
| "Fork the repo from 'dev' branch" | Branch from `dmc-dev`. `dev` is meshcore-dev's branch. |
| "Open an issue first, get a 👍" | See §6 — issues are disabled on the repo. |
| "Reference any related issue (`Fixes #123`)" | Not possible while issues are disabled. |

**In practice this fork is the repeater packet filter.** Measured against `mc/dev`, the delta is
~1100 lines, and ~1000 of them are `examples/simple_repeater/Filter.cpp`, `Filter.h` and the two
filter documents. The rest is three DMC build workflows, `build.sh`, and a handful of small variant
and radio tweaks.

## 2. Where code goes

Because the fork only ever merges *from* upstream, every DMC change to a file that also exists
upstream is a conflict on **every** sync. Files that exist only here never conflict.

| | conflicts on sync |
| --- | --- |
| DMC-only files (`examples/simple_repeater/Filter.*`, `docs/packet_filter_reference.md`, `.github/workflows/build-dmc-*.yml`, and any new file added here) | never |
| Shared upstream files | every time |

So: **put new functionality in new, DMC-only files.** When you must touch a shared file, keep the
change to as few contiguous hunks as possible, and keep them at stable anchor points — the number
and placement of hunks decides how painful the merge is, not the number of lines.

These seven shared files currently carry DMC changes. Adding an eighth should be a deliberate
decision, not a side effect:

```
build.sh
docs/cli_commands.md
examples/simple_repeater/MyMesh.cpp
examples/simple_repeater/MyMesh.h
src/helpers/radiolib/RadioLibWrappers.cpp
variants/minewsemi_me25ls01/platformio.ini
variants/tenstar_c3/target.h
```

## 3. Build and test

```bash
# unit tests, exactly as CI runs them
pio test -e native -e native_kiss_modem

# firmware for one board
pio run -e RAK_4631_repeater
```

**CI does not build the firmware for pull requests against `dmc-dev`.** `.github/workflows/run-unit-tests.yml`
runs on any pull request, but `pr-build-check.yml` is limited to base `main` and `dev`. Green checks
therefore mean the unit tests passed, not that the firmware compiles. Build at least one repeater
environment locally before you push, and again after every sync from upstream — upstream can change
something out from under the filter.

### Syncing from upstream

```bash
git fetch mc
git merge mc/dev
pio test -e native -e native_kiss_modem
pio run -e RAK_4631_repeater
```

## 4. Packet filter invariants

None of these are visible from the code you are editing. All of them have already caused bugs.

**The CLI reply buffer is 160 bytes.** `main.cpp` writes replies into `char reply[160]`; the mesh
path in `MyMesh.cpp` uses `uint8_t temp[166]` with the reply at offset 5, and `MAX_PACKET_PAYLOAD`
caps that at ~171. Treat 160 as the contract for both. Never `sprintf` a variable-length reply
straight into it. Use a bounded appender that refuses an oversized fragment whole rather than
clipping a number in half — half a number reads as a whole one — and that marks clipped output so a
truncated list cannot pass for a complete one. Two existing commands were silently overflowing this
buffer before one was introduced.

**Reply strings are a consumed interface.** mc2mqtt reads them over serial, and CoreScope,
core-hunter and terminal.js parse them. Changing the wording or shape of an existing reply breaks
tooling silently, with no compile error and no failing test. **Add a new command instead of changing
an existing reply.** It is why the per-topic drop statistics were given their own
`filter stats <topic>` command rather than being appended to the `filter hash` and `filter malformed`
replies, which several tools already parse.

**Preference structs are persisted as raw blobs.** `FilterPrefs` is written to `/filter_prefs` with a
straight `file.write(reinterpret_cast<const uint8_t*>(&_prefs), sizeof(_prefs))` — no version, no
magic, no schema. Existing fields may never be reordered, resized or removed, and new fields go at
the **end**, where an older file simply leaves them at their defaults. Counters are separate from
prefs and are free to change.

**Logic that deserves a test belongs in a firmware-free header.** `Filter.cpp` drags in
`FILESYSTEM`, `ClientACL` and `RTCClock`, so nothing in it can be unit-tested. Counting, selection
and text formatting do not need any of that: put them in a header that includes nothing beyond
`Packet.h` — which itself depends only on `MeshCore.h` — and they compile and run on the `native`
env.

**Document a new command in both places.** `docs/packet_filter_reference.md` is the manual;
`docs/cli_commands.md` is the command contract that downstream tooling reads. A command that exists
in only one of them will be missed.

**The C++ is authoritative, the docs are not.** After changing any reply, diff the documented strings
against the format strings in the source. Doing exactly that is how the `filter hops` / `filter rate`
type-range message was found to say `0-10` while the guard accepts `0-11`.

## 5. Tests

Suites live in `test/test_<name>/` and are picked up automatically by `env:native`; only
`test_kiss_modem` is excluded, via `test_ignore`. No `platformio.ini` change is needed for a new
suite.

**Every suite must define its own gtest `main()`:**

```cpp
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

PlatformIO auto-links `gtest_main` on Linux, so a suite without one passes CI and then fails to link
on the Windows/mingw native toolchain. Every existing suite has it.

Tests are required for every logic change, and written before the implementation. A test you never
watched fail proves nothing.

## 6. Issues, commits and pull requests

**Every change starts with an issue** describing the problem or the feature, so the reasoning is
recorded somewhere other than a diff. Reference it from the pull request with a closing keyword
(`Closes #<n>`).

> **Issues are currently disabled on `Dutch-MeshCore/MeshCore`,** along with discussions. Until an
> org admin enables them, put the rationale that would have gone in the issue into the pull request
> description instead — what the problem was, what was measured, what was decided and why.

**Stage named files.** Never `git add -A` or `git add .`; this repo has firmware build output and
local variant tweaks lying around that must not be committed.

**One commit per logical change,** with a [conventional commit](https://www.conventionalcommits.org)
subject:

```
feat(filter): add filter stats commands
fix(variants): define missing user_btn for R1Neo repeaters
docs(filter): document the statistics replies
ci(dmc): split repeater build and release into separate jobs
```

**One feature per pull request**, and say what you verified. "Tests pass" is only worth stating with
the command and its output behind it; a claim about firmware behaviour is only worth stating if you
flashed it.
