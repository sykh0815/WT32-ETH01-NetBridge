#!/usr/bin/env bash
# Baut die Firmware und legt einen GitHub-Release mit fertigen Firmware-Dateien an.
# Builds the firmware and creates a GitHub release with ready-made firmware files.
#
# Voraussetzungen / requirements: PlatformIO (pio), GitHub CLI (gh, angemeldet mit "gh auth login")
#
# Ablauf / steps:
#   1. Version aus src/main.cpp (FIRMWARE_VERSION) lesen
#   2. pruefen: Changelog-Eintraege vorhanden, alles committet, Tag noch nicht vorhanden
#   3. pio run, Dateien nach dist/vX.Y kopieren (+ zusammengefuegte Komplett-Datei, falls esptool da ist)
#   4. Tag anlegen und pushen, Release mit Notizen aus beiden Changelogs anlegen, Dateien anhaengen
#
# Aufruf / usage:  ./tools/release.sh          (oder: bash tools/release.sh)
set -euo pipefail
cd "$(dirname "$0")/.."

ENV_NAME="wt32-eth01"
VERSION=$(sed -n 's/.*FIRMWARE_VERSION\[\] = "\([^"]*\)".*/\1/p' src/main.cpp)
[ -n "$VERSION" ] || { echo "FIRMWARE_VERSION nicht gefunden / not found in src/main.cpp"; exit 1; }
TAG="v$VERSION"
echo "==> Version $VERSION ($TAG)"

for f in CHANGELOG.md CHANGELOG.de.md; do
  grep -q "^## \[$VERSION\]" "$f" || { echo "Kein Eintrag fuer $VERSION in $f / no entry for $VERSION in $f"; exit 1; }
done
if [ -n "$(git status --porcelain)" ]; then
  echo "Es gibt nicht committete Aenderungen. Bitte zuerst committen und pushen."
  echo "There are uncommitted changes. Please commit and push first."
  exit 1
fi
command -v pio >/dev/null || { echo "PlatformIO (pio) fehlt / missing"; exit 1; }
command -v gh >/dev/null || { echo "GitHub CLI (gh) fehlt / missing: brew install gh"; exit 1; }
gh auth status >/dev/null 2>&1 || { echo "Bitte zuerst / please run first: gh auth login"; exit 1; }
if gh release view "$TAG" >/dev/null 2>&1; then
  echo "Release $TAG existiert schon / already exists. FIRMWARE_VERSION erhoehen / increase."
  exit 1
fi

echo "==> Build"
pio run -e "$ENV_NAME"
BUILD=".pio/build/$ENV_NAME"
BOOT_APP0=$(find "$HOME/.platformio/packages" -path "*framework-arduinoespressif32*" -name boot_app0.bin 2>/dev/null | head -n 1)
[ -n "$BOOT_APP0" ] || { echo "boot_app0.bin nicht gefunden / not found"; exit 1; }

DIST="dist/$TAG"
rm -rf "$DIST"; mkdir -p "$DIST"
cp "$BUILD/bootloader.bin" "$BUILD/partitions.bin" "$BUILD/firmware.bin" "$BOOT_APP0" "$DIST/"
ASSETS=("$DIST/bootloader.bin" "$DIST/partitions.bin" "$DIST/boot_app0.bin" "$DIST/firmware.bin")

# Komplett-Datei (alles ab Adresse 0x0) fuer Web-Flasher / single file at 0x0 for web flashers
FULL="$DIST/wt32-eth01-netbridge-$TAG-full.bin"
MERGE_ARGS=(--chip esp32 merge_bin -o "$FULL" --flash_mode keep --flash_freq keep --flash_size keep
            0x1000 "$DIST/bootloader.bin" 0x8000 "$DIST/partitions.bin" 0xe000 "$DIST/boot_app0.bin" 0x10000 "$DIST/firmware.bin")
if pio pkg exec -p tool-esptoolpy -- esptool.py "${MERGE_ARGS[@]}" >/dev/null 2>&1 \
   || python3 -m esptool "${MERGE_ARGS[@]}" >/dev/null 2>&1 \
   || esptool "${MERGE_ARGS[@]}" >/dev/null 2>&1; then
  ASSETS+=("$FULL")
  echo "==> Komplett-Datei / full image: $FULL"
else
  echo "==> Hinweis: esptool nicht gefunden, Komplett-Datei wird uebersprungen / esptool not found, skipping full image"
fi
(cd "$DIST" && shasum -a 256 *.bin > SHA256SUMS.txt)
ASSETS+=("$DIST/SHA256SUMS.txt")

# Release-Notizen aus beiden Changelogs: alle Versionen seit dem letzten Release
# Release notes from both changelogs: all versions since the previous release
PREV_TAG=$(git describe --tags --abbrev=0 --match 'v*' 2>/dev/null || true)
PREV="${PREV_TAG#v}"
[ "$PREV" = "$VERSION" ] && PREV=""
echo "==> Notizen ab / notes since: ${PREV_TAG:-(Anfang / start)}"
# Gibt die Abschnitte von "## [VERSION]" bis vor "## [PREV]" aus; Versionsueberschriften werden zu ###
section() {
  awk -v v="## [$1]" -v p="## [$3]" '
    index($0,v)==1 {f=1}
    f && p != "## []" && index($0,p)==1 {exit}
    f && /^## \[/ {sub(/^## /,"### "); print; next}
    f && /^### / {sub(/^### /,"#### "); print; next}
    f {print}' "$2"
}
NOTES="$DIST/RELEASE_NOTES.md"
{
  echo "## English"; section "$VERSION" CHANGELOG.md "$PREV"
  echo "## Deutsch"; section "$VERSION" CHANGELOG.de.md "$PREV"
  cat <<'EOF'
---

#### Firmware files / Firmware-Dateien

| File / Datei | Address / Adresse |
|---|---|
| `bootloader.bin` | `0x1000` |
| `partitions.bin` | `0x8000` |
| `boot_app0.bin` | `0xe000` |
| `firmware.bin` | `0x10000` |
| `wt32-eth01-netbridge-…-full.bin` (if present / falls vorhanden) | `0x0` |

Flash in the browser with / im Browser flashen mit <https://espressif.github.io/esptool-js/> (Chrome/Edge).
Updating from an older version keeps all settings / Beim Update bleiben alle Einstellungen erhalten.
EOF
} > "$NOTES"

echo "==> Tag $TAG"
git tag -a "$TAG" -m "Version $VERSION" 2>/dev/null || echo "Tag $TAG existiert lokal schon / exists locally"
git push origin "$TAG"

echo "==> Release"
gh release create "$TAG" --title "WT32-ETH01 NetBridge $VERSION" --notes-file "$NOTES" --latest "${ASSETS[@]}"
echo "==> Fertig / done: $(gh release view "$TAG" --json url -q .url)"
