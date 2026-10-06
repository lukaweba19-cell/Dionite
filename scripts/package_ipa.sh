#!/bin/sh
# ============================================================================
# Dionite — generate the Xcode project, build an unsigned iOS app, and package
# it as an .ipa (Payload/Dionite.app) with no certificates or provisioning.
#
# Usage:  sh ./scripts/package_ipa.sh [output-dir]
# ============================================================================
set -eu

OUT_DIR="${1:-dist}"
CONFIGURATION="${CONFIGURATION:-Release}"
SCHEME="Dionite"
PROJECT="Dionite.xcodeproj"
DERIVED="build/DerivedData"

if ! command -v xcodegen >/dev/null 2>&1; then
    echo "error: xcodegen is required (brew install xcodegen)" >&2
    exit 1
fi

echo "==> Generating ${PROJECT}"
xcodegen generate

echo "==> Building ${SCHEME} (${CONFIGURATION}, generic iOS device, unsigned)"
rm -rf "${DERIVED}"
mkdir -p build
set +e
xcodebuild \
    -project "${PROJECT}" \
    -scheme "${SCHEME}" \
    -configuration "${CONFIGURATION}" \
    -destination 'generic/platform=iOS' \
    -derivedDataPath "${DERIVED}" \
    CODE_SIGNING_ALLOWED=NO \
    CODE_SIGNING_REQUIRED=NO \
    CODE_SIGN_IDENTITY="" \
    ONLY_ACTIVE_ARCH=NO \
    build > build/xcodebuild.log 2>&1
STATUS=$?
set -e
if [ "${STATUS}" -ne 0 ]; then
    echo "error: xcodebuild failed with status ${STATUS}" >&2
    tail -n 80 build/xcodebuild.log >&2
    exit "${STATUS}"
fi
tail -n 25 build/xcodebuild.log

APP="${DERIVED}/Build/Products/${CONFIGURATION}-iphoneos/Dionite.app"
if [ ! -d "${APP}" ]; then
    echo "error: expected app bundle not found at ${APP}" >&2
    exit 1
fi

echo "==> Packaging ${OUT_DIR}/Dionite.ipa"
rm -rf "${OUT_DIR}/Payload"
mkdir -p "${OUT_DIR}/Payload"
cp -R "${APP}" "${OUT_DIR}/Payload/Dionite.app"

# Ad-hoc re-sign so the payload is accepted by tooling that expects a
# _CodeSignature directory. It is NOT a valid identity — sideload with
# AltStore / TrollStore / a free team as usual.
if command -v codesign >/dev/null 2>&1; then
    codesign --force --sign - "${OUT_DIR}/Payload/Dionite.app" >/dev/null 2>&1 || true
fi

rm -f "${OUT_DIR}/Dionite.ipa"
( cd "${OUT_DIR}" && zip -qry Dionite.ipa Payload )

# Keep the symbol file next to the archive for crash symbolication.
DSYM="${APP}.dSYM"
if [ -d "${DSYM}" ]; then
    mkdir -p "${OUT_DIR}/symbols"
    rm -rf "${OUT_DIR}/symbols/Dionite.app.dSYM"
    cp -R "${DSYM}" "${OUT_DIR}/symbols/Dionite.app.dSYM"
fi

echo "==> Done: ${OUT_DIR}/Dionite.ipa"
ls -la "${OUT_DIR}/Dionite.ipa"
