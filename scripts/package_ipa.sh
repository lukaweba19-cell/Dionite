#!/usr/bin/env sh
# ============================================================================
# Dionite — build an unsigned .ipa with no certificates or provisioning.
#
# Generates the Xcode project with XcodeGen (unless SKIP_GENERATE=1, used when
# CI has already generated and validated it), builds for device, and packages
# the .app into dist/Dionite.ipa with a Payload/ folder.
# ============================================================================
set -eu
cd "$(dirname "$0")/.."

if [ "${SKIP_GENERATE:-0}" != "1" ]; then
    xcodegen generate
fi

mkdir -p build dist
LOG=build/xcodebuild.log

echo "Building Dionite (Release, iphoneos)..."
if ! xcodebuild \
        -project Dionite.xcodeproj \
        -scheme Dionite \
        -configuration Release \
        -sdk iphoneos \
        -derivedDataPath build \
        CODE_SIGNING_ALLOWED=NO \
        CODE_SIGNING_REQUIRED=NO \
        CODE_SIGN_IDENTITY="" \
        DEVELOPMENT_TEAM="" \
        > "$LOG" 2>&1; then
    echo "xcodebuild failed; last 120 log lines:"
    tail -n 120 "$LOG"
    exit 1
fi

APP="build/Build/Products/Release-iphoneos/Dionite.app"
if [ ! -d "$APP" ]; then
    echo "xcodebuild succeeded but $APP is missing; last log lines:"
    tail -n 80 "$LOG"
    exit 1
fi

rm -rf dist/Payload dist/symbols
mkdir -p dist/Payload dist/symbols
cp -R "$APP" dist/Payload/
if [ -d "$APP.dSYM" ]; then
    cp -R "$APP.dSYM" dist/symbols/
fi

(
    cd dist
    rm -f Dionite.ipa
    zip -qry Dionite.ipa Payload
)

echo "OK: $(du -h dist/Dionite.ipa | cut -f1) -> dist/Dionite.ipa"
