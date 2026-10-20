#!/bin/bash
# One-shot installer: build + bundle + sign, copy to /Applications, and
# (optionally) set up start-at-login.
#
# Usage: scripts/install.sh [--login]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
APP_SRC="$REPO_DIR/build/MacFans.app"
APP_DST="/Applications/MacFans.app"

"$SCRIPT_DIR/package.sh"

echo "Installing to $APP_DST ..."
rm -rf "$APP_DST"
cp -R "$APP_SRC" "$APP_DST"
# Clear any quarantine so it opens without a Gatekeeper prompt.
xattr -dr com.apple.quarantine "$APP_DST" 2>/dev/null || true

echo "Installed: $APP_DST"

if [[ "${1:-}" == "--login" ]]; then
    "$SCRIPT_DIR/install-launchagent.sh" "$APP_DST"
fi

open "$APP_DST"
echo "Done. MacFans is in /Applications and now open."
