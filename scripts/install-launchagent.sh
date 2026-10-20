#!/bin/bash
# Install a LaunchAgent so MacFans starts automatically at login.
#
# Usage: scripts/install-launchagent.sh [/path/to/MacFans.app]
# If no path is given, it looks in /Applications, then the local build.
set -euo pipefail

LABEL="local.macfans.app"
AGENT_DIR="$HOME/Library/LaunchAgents"
AGENT_PLIST="$AGENT_DIR/$LABEL.plist"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Resolve the app bundle.
APP="${1:-}"
if [[ -z "$APP" ]]; then
    if [[ -d "/Applications/MacFans.app" ]]; then
        APP="/Applications/MacFans.app"
    elif [[ -d "$REPO_DIR/build/MacFans.app" ]]; then
        APP="$REPO_DIR/build/MacFans.app"
    else
        echo "error: MacFans.app not found. Pass its path as an argument." >&2
        exit 1
    fi
fi

BIN="$APP/Contents/MacOS/MacFans"
if [[ ! -x "$BIN" ]]; then
    echo "error: executable not found at $BIN" >&2
    exit 1
fi

mkdir -p "$AGENT_DIR"
sed "s#__MACFANS_BIN__#$BIN#g" \
    "$REPO_DIR/resources/local.macfans.app.plist.in" > "$AGENT_PLIST"

# Reload cleanly.
launchctl unload "$AGENT_PLIST" 2>/dev/null || true
launchctl load  "$AGENT_PLIST"

echo "Installed LaunchAgent:"
echo "  $AGENT_PLIST"
echo "  -> $BIN"
echo "MacFans will now start at login (and just started now)."
