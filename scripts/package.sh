#!/bin/bash
# Make MacFans.app self-contained and launchable from anywhere:
#   1. build
#   2. copy the required Qt plugins into the bundle
#   3. recursively bundle all Qt/Homebrew frameworks + dylibs (bundle_deps.py)
#   4. apply a valid ad-hoc code signature (fixes the "damaged / unverified"
#      Gatekeeper error)
#
# Does NOT use macdeployqt (it mis-resolves transitive deps against Homebrew Qt).
#
# Usage: scripts/package.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_DIR/build"
APP="$BUILD_DIR/MacFans.app"

QT_PREFIX="$(brew --prefix qt6 2>/dev/null || echo /opt/homebrew/opt/qt6)"
QT_PLUGINS="$("$QT_PREFIX/bin/qmake6" -query QT_INSTALL_PLUGINS)"

# 1. Build.
cmake -S "$REPO_DIR" -B "$BUILD_DIR" -DCMAKE_PREFIX_PATH="$QT_PREFIX" >/dev/null
cmake --build "$BUILD_DIR" -j8

# 2. Copy the Qt plugins the app actually uses.
mkdir -p "$APP/Contents/PlugIns/platforms" \
         "$APP/Contents/PlugIns/styles" \
         "$APP/Contents/PlugIns/imageformats" \
         "$APP/Contents/PlugIns/iconengines"
cp -f "$QT_PLUGINS/platforms/libqcocoa.dylib"     "$APP/Contents/PlugIns/platforms/"
cp -f "$QT_PLUGINS/styles/"*.dylib                "$APP/Contents/PlugIns/styles/"       2>/dev/null || true
cp -f "$QT_PLUGINS/imageformats/"*.dylib          "$APP/Contents/PlugIns/imageformats/" 2>/dev/null || true
cp -f "$QT_PLUGINS/iconengines/"*.dylib           "$APP/Contents/PlugIns/iconengines/"  2>/dev/null || true

# Tell Qt where its plugins live inside the bundle.
mkdir -p "$APP/Contents/Resources"
cat > "$APP/Contents/Resources/qt.conf" <<'EOF'
[Paths]
Plugins = PlugIns
EOF

# 3. Recursively bundle frameworks + dylibs and fix all linkage.
python3 "$SCRIPT_DIR/bundle_deps.py" "$APP"

# 4. Valid ad-hoc signature sealing all resources (sign nested first).
find "$APP/Contents/Frameworks" "$APP/Contents/PlugIns" -type f \
    \( -name "*.dylib" -o -perm +111 \) -exec codesign --force --sign - --timestamp=none {} + 2>/dev/null || true
codesign --force --sign - --timestamp=none "$APP/Contents/MacOS/macfans-helper" 2>/dev/null || true
codesign --force --deep --sign - --timestamp=none "$APP"

echo "---"
codesign --verify --deep --strict --verbose=2 "$APP" 2>&1 | tail -2 || true
echo "Packaged: $APP"
