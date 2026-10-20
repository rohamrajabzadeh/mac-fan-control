#!/bin/bash
# Fully remove MacFans: LaunchAgent, privileged helper, saved settings.
# The app itself is left where you put it (delete the .app yourself if desired).
#
# Usage: scripts/uninstall.sh
set -uo pipefail

LABEL="local.macfans.app"
AGENT_PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
HELPER_DIR="/Library/Application Support/MacFans"

echo "This will remove:"
echo "  • LaunchAgent:  $AGENT_PLIST"
echo "  • Helper (root): $HELPER_DIR   (needs your password)"
echo "  • Saved settings"
read -r -p "Continue? [y/N] " ans
[[ "$ans" =~ ^[Yy]$ ]] || { echo "Aborted."; exit 0; }

# 1) Stop and remove the LaunchAgent.
if [[ -f "$AGENT_PLIST" ]]; then
    launchctl unload "$AGENT_PLIST" 2>/dev/null || true
    rm -f "$AGENT_PLIST"
    echo "Removed LaunchAgent."
fi

# 2) Stop any running instance.
killall MacFans 2>/dev/null || true

# 3) Remove the privileged helper (root-owned).
if [[ -d "$HELPER_DIR" ]]; then
    sudo rm -rf "$HELPER_DIR"
    echo "Removed privileged helper."
fi

# 4) Remove QSettings files written by the app.
rm -f "$HOME/Library/Preferences/local.macfans"*.plist 2>/dev/null || true
rm -f "$HOME/Library/Preferences/com.macfans"*.plist 2>/dev/null || true
defaults delete "$LABEL" 2>/dev/null || true

echo "Done. Fans are back under full system control."
echo "If you copied MacFans.app to /Applications, delete it manually."
