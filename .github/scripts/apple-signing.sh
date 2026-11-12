#!/bin/bash
# CI signing setup (adapted from mu-ica/muonline): temporary keychain with the Developer ID
# Application certificate and the App Store Connect API key used by notarytool.
#
# Env: KEYCHAIN_PASSWORD, CERT_P12_BASE64, CERT_PASSWORD, ASC_KEY_ID, ASC_KEY_P8
set -euo pipefail

KEYCHAIN="$RUNNER_TEMP/signing.keychain-db"
security create-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
security set-keychain-settings -lut 21600 "$KEYCHAIN"
security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"

echo "$CERT_P12_BASE64" | base64 --decode > "$RUNNER_TEMP/cert.p12"
security import "$RUNNER_TEMP/cert.p12" -k "$KEYCHAIN" -P "$CERT_PASSWORD" -T /usr/bin/codesign -T /usr/bin/security
rm -f "$RUNNER_TEMP/cert.p12"

security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$KEYCHAIN_PASSWORD" "$KEYCHAIN" >/dev/null
security list-keychains -d user -s "$KEYCHAIN" $(security list-keychains -d user | tr -d '"')
security default-keychain -s "$KEYCHAIN"
security find-identity -v -p codesigning "$KEYCHAIN"

KEY_PATH="$RUNNER_TEMP/AuthKey_${ASC_KEY_ID}.p8"
printf '%s' "$ASC_KEY_P8" > "$KEY_PATH"
echo "ASC_KEY_PATH=$KEY_PATH" >> "$GITHUB_ENV"
