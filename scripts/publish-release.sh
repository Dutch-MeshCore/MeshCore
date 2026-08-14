#!/usr/bin/env bash
#
# publish-release.sh — build a DMC firmware set LOCALLY and publish it as a
# GitHub draft pre-release, WITHOUT spending GitHub Actions runner minutes.
#
# The CI build workflows do nothing but run `build.sh <target>`, so building
# locally is byte-for-byte equivalent. The DMC build workflows are now
# dispatch-only (no tag auto-trigger), so creating the release tag never fires a
# runner build.
#
# Usage:
#   GITHUB_TOKEN=<pat> bash scripts/publish-release.sh <build-target> <version> <tag> "<release title>"
#
# Examples (run each from the branch that has the target):
#   # dmc-observer-dev-1-17:
#   GITHUB_TOKEN=... bash scripts/publish-release.sh \
#     build-repeater-mqtt-firmwares    v1.17.1 dmc-repeater-mqtt-v1.17.1    "DMC Repeater MQTT Firmware v1.17.1"
#   GITHUB_TOKEN=... bash scripts/publish-release.sh \
#     build-room-server-mqtt-firmwares v1.17.1 dmc-room-server-mqtt-v1.17.1 "DMC Roomserver Firmware v1.17.1"
#   # dmc-dev:
#   GITHUB_TOKEN=... bash scripts/publish-release.sh \
#     build-dmc-repeater-firmwares     v1.17.1 dmc-repeater-v1.17.1         "DMC Repeater Firmware v1.17.1"
#
# The GITHUB_TOKEN needs `contents: write` (a classic PAT with `repo`, or a
# fine-grained PAT with Contents: Read and write on Dutch-MeshCore/MeshCore).
#
# It creates a DRAFT pre-release and uploads everything build.sh emits into out/.
# Review it on GitHub, then Publish (drafts do not create the git tag until you
# publish, and because the workflows are dispatch-only, publishing never triggers
# a runner build). Your toolbox flasher picks the assets up from the Release.

set -euo pipefail

REPO="Dutch-MeshCore/MeshCore"

TARGET="${1:?usage: publish-release.sh <build-target> <version> <tag> \"<title>\"}"
VERSION="${2:?missing <version>, e.g. v1.17.1}"
TAG="${3:?missing <tag>, e.g. dmc-repeater-mqtt-v1.17.1}"
TITLE="${4:?missing \"<release title>\"}"
: "${GITHUB_TOKEN:?set GITHUB_TOKEN (PAT with contents:write on $REPO)}"

# Version must have no extra hyphens: setup-build-environment parses the embedded
# version as the text AFTER the last '-' in the tag (v1.17.1 ok; v1.17.1-rc1 not).
case "$VERSION" in *-*) echo "ERROR: version '$VERSION' must not contain '-'"; exit 1;; esac

echo "==> Building '$TARGET' at FIRMWARE_VERSION=$VERSION (local, no runner)"
FIRMWARE_VERSION="$VERSION" bash build.sh "$TARGET"

shopt -s nullglob
ASSETS=(out/*)
[ "${#ASSETS[@]}" -gt 0 ] || { echo "ERROR: no artifacts in out/"; exit 1; }
echo "==> Built ${#ASSETS[@]} artifact(s):"; printf '    %s\n' "${ASSETS[@]}"

COMMIT="$(git rev-parse HEAD)"

if command -v gh >/dev/null 2>&1; then
  echo "==> Publishing draft pre-release via gh CLI"
  gh release create "$TAG" --repo "$REPO" --draft --prerelease \
    --target "$COMMIT" --title "$TITLE" --notes "" "${ASSETS[@]}"
else
  echo "==> gh CLI not found; using the GitHub REST API (curl + python3)"
  api="https://api.github.com/repos/$REPO"
  payload="$(python3 - "$TAG" "$COMMIT" "$TITLE" <<'PY'
import json, sys
tag, commit, title = sys.argv[1:4]
print(json.dumps({"tag_name": tag, "target_commitish": commit,
                  "name": title, "draft": True, "prerelease": True, "body": ""}))
PY
)"
  resp="$(curl -fsS -X POST "$api/releases" \
    -H "Authorization: Bearer $GITHUB_TOKEN" \
    -H "Accept: application/vnd.github+json" \
    -d "$payload")"
  rel_id="$(printf '%s' "$resp" | python3 -c 'import sys,json;print(json.load(sys.stdin).get("id",""))')"
  [ -n "$rel_id" ] || { echo "ERROR: release create failed:"; echo "$resp"; exit 1; }
  echo "==> Draft release id=$rel_id; uploading assets"
  for a in "${ASSETS[@]}"; do
    name="$(basename "$a")"
    curl -fsS -X POST \
      "https://uploads.github.com/repos/$REPO/releases/$rel_id/assets?name=$name" \
      -H "Authorization: Bearer $GITHUB_TOKEN" \
      -H "Content-Type: application/octet-stream" \
      --data-binary @"$a" >/dev/null
    echo "    uploaded $name"
  done
fi

echo "==> Done. Draft pre-release '$TITLE' ($TAG) created."
echo "    Review + Publish from: https://github.com/$REPO/releases"
