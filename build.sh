#!/usr/bin/env bash
# Build Magic Tattoos UI and copy it to Vortex staging (default), the game Data folder, or a release zip.
# Usage: build.sh [staging|live|package|test|uninstall-staging|uninstall-live]
# Set SKYRIM_DIR, VORTEX_STAGING, or VCPKG_ROOT to override the paths below.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
TARGET="${1:-staging}"
GAME="${SKYRIM_DIR:-/c/Program Files (x86)/Steam/steamapps/common/Skyrim Special Edition}"
DATA="$GAME/Data"
STAGING="${VORTEX_STAGING:-/c/Users/white/AppData/Roaming/Vortex/skyrimse/mods/Magic Tattoos UI}"
DIST="$ROOT/dist"
export VCPKG_ROOT="${VCPKG_ROOT:-M:/Repositories/vcpkg}"

case "$TARGET" in
  uninstall-live)
    rm -f "$DATA/SKSE/Plugins/MagicTattoosUI.dll" "$DATA/SKSE/Plugins/MagicTattoosUI.pdb"
    echo "Removed Magic Tattoos UI from $DATA"
    echo "Your settings, favorites and preset backups in $DATA/SKSE/Plugins/MagicTattoosUI were kept."
    exit 0
    ;;
  uninstall-staging)
    if [[ -d "$STAGING" ]]; then
      rm -rf "$STAGING"
      echo "Removed $STAGING"
    else
      echo "Nothing staged at $STAGING"
    fi
    echo "Vortex will list the mod as missing. Remove it there, then deploy."
    exit 0
    ;;
  staging|live|package|test) ;;
  *)
    echo "Unknown target '$TARGET'. Use staging, live, package, test, uninstall-staging, or uninstall-live." >&2
    exit 1
    ;;
esac

VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"

# Use the CMake and Ninja that vcpkg downloads when they aren't installed system-wide.
tool_dir() { dirname "$(cygpath -u "$("$VCPKG_ROOT/vcpkg.exe" fetch "$1" 2>/dev/null | tail -1)")"; }
command -v cmake > /dev/null || PATH="$(tool_dir cmake):$PATH"
command -v ninja > /dev/null || PATH="$(tool_dir ninja):$PATH"
export PATH

# MSYS rewrites /x-style arguments into paths; the MSVC tools must see them untouched.
export MSYS_NO_PATHCONV=1
MSVC="$ROOT/tools/msvc.cmd"
cd "$ROOT"
"$MSVC" cmake --preset release
"$MSVC" cmake --build --preset release
"$MSVC" ctest --preset release --output-on-failure
unset MSYS_NO_PATHCONV

if [[ "$TARGET" == "test" ]]; then
  echo "Built and tested $VERSION"
  exit 0
fi

BUILD="$ROOT/build/release"
rm -rf "$DIST"
mkdir -p "$DIST/SKSE/Plugins"
cp -f "$BUILD/MagicTattoosUI.dll" "$DIST/SKSE/Plugins/"
cp -f "$BUILD/MagicTattoosUI.pdb" "$DIST/SKSE/Plugins/"

# Only our own files may ship: anything else would overwrite another mod (Menu Framework, MTF, PapyrusUtil).
while IFS= read -r -d '' file; do
  rel="${file#"$DIST"/}"
  case "$rel" in
    SKSE/Plugins/MagicTattoosUI.dll|SKSE/Plugins/MagicTattoosUI.pdb|SKSE/Plugins/MagicTattoosUI/*) ;;
    *)
      echo "Unexpected file $rel in dist. Only Magic Tattoos UI files may ship." >&2
      exit 1
      ;;
  esac
done < <(find "$DIST" -type f -print0)

case "$TARGET" in
  staging)
    mkdir -p "$STAGING/SKSE/Plugins"
    cp -f "$DIST/SKSE/Plugins/MagicTattoosUI.dll" "$DIST/SKSE/Plugins/MagicTattoosUI.pdb" "$STAGING/SKSE/Plugins/"
    ;;
  live)
    mkdir -p "$DATA/SKSE/Plugins"
    cp -f "$DIST/SKSE/Plugins/MagicTattoosUI.dll" "$DIST/SKSE/Plugins/MagicTattoosUI.pdb" "$DATA/SKSE/Plugins/"
    ;;
  package)
    PACKAGE="$DIST/package"
    ZIP="$DIST/MagicTattoosUI-$VERSION.zip"
    rm -rf "$PACKAGE" "$ZIP"
    mkdir -p "$PACKAGE/SKSE/Plugins"
    cp -f "$DIST/SKSE/Plugins/MagicTattoosUI.dll" "$PACKAGE/SKSE/Plugins/"
    # Windows bsdtar, not Git Bash's GNU tar, which cannot write zips. Compress-Archive in Windows PowerShell writes backslash paths.
    "$(cygpath -u "$SYSTEMROOT")/System32/tar.exe" -a -cf "$(cygpath -w "$ZIP")" -C "$(cygpath -w "$PACKAGE")" SKSE
    ;;
esac

if [[ "$TARGET" == "staging" && ! -f "$STAGING/meta.ini" ]]; then
  cat > "$STAGING/meta.ini" << EOF
[General]
gameName=SkyrimSE
modid=0
version=$VERSION
newestVersion=
category="-1,"
nexusFileStatus=1
installationFile=Magic Tattoos UI
repository=local
ignoredVersion=
comments=
notes=
nexusDescription=
url=
hasCustomURL=false
lastNexusQuery=
lastNexusUpdate=
nexusLastModified=
converted=false
validated=false
tracked=0
EOF
fi

if [[ "$TARGET" == "staging" && -f "$STAGING/meta.ini" ]]; then
  sed -i "s/^version=.*/version=$VERSION/" "$STAGING/meta.ini"
fi

if [[ "$TARGET" == "package" ]]; then
  echo "Packaged $VERSION at $ZIP"
elif [[ "$TARGET" == "live" ]]; then
  echo "Copied $VERSION into $DATA"
  echo "Restart Skyrim. Leave Magic Tattoos UI disabled in Vortex so deploy does not replace these files."
else
  echo "Staged $VERSION at $STAGING"
  echo "Enable Magic Tattoos UI in Vortex and deploy, then restart Skyrim."
fi
