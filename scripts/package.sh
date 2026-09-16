#!/bin/bash
# naught 打包脚本（M8）：从已部署的应用生成 DMG 与 zip。
# 用法：scripts/package.sh [版本号]（默认取 Info.plist 的 CFBundleShortVersionString）
set -euo pipefail
cd "$(dirname "$0")/.."

APP="$HOME/Applications/naught.app"
if [[ ! -d "$APP" ]]; then
    echo "先部署：按 docs/roadmap.md 的开工流程把 naught.app 装到 ~/Applications" >&2
    exit 1
fi
VER="${1:-$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$APP/Contents/Info.plist")}"
STAGE="$(mktemp -d)/naught"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
codesign --force --deep --sign - "$STAGE/naught.app"

OUT="release"
mkdir -p "$OUT"
# zip（自包含，免公证门槛低）
/usr/bin/ditto -c -k --keepParent "$STAGE/naught.app" "$OUT/naught-$VER-macos.zip"
# DMG
hdiutil create -volname "naught" -srcfolder "$STAGE" -ov -format UDZO \
    "$OUT/naught-$VER-macos.dmg" >/dev/null
rm -rf "$(dirname "$STAGE")"
echo "打包完成："
ls -lh "$OUT" | tail -2
