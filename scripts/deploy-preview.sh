#!/bin/bash
# 部署预览版到启动台：~/Applications/naught-preview.app（無·预览）。
# 独立 bundle ID com.2liver.naught.preview——与稳定版 com.2liver.naught
# 共存，LaunchServices 不会被同 ID 覆盖注册（审查 R1：此前只写在
# commit message 里，靠手工 PlistBuddy，现收口进脚本）。
# 用法：scripts/deploy-preview.sh
set -euo pipefail
cd "$(dirname "$0")/.."

CM=/Users/2liver/Library/Python/3.9/lib/python/site-packages/cmake/data/bin/cmake
QT=/Users/2liver/Qt/6.9.3/macos
KF6=/Users/2liver/kf6/lib

STAGE=$(mktemp -d /tmp/naught-deploy.XXXXXX)
trap 'rm -rf "$STAGE"' EXIT

"$CM" --install build --prefix "$STAGE" > /dev/null
mv "$STAGE/naught.app" "$STAGE/naught-preview.app"
APP="$STAGE/naught-preview.app"

# 独立 bundle ID + 预览名
/usr/libexec/PlistBuddy -c 'Add :CFBundleDisplayName string 無·预览' \
  "$APP/Contents/Info.plist" 2>/dev/null \
  || /usr/libexec/PlistBuddy -c 'Set :CFBundleDisplayName 無·预览' "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleName 無·预览' "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleIdentifier com.2liver.naught.preview' \
  "$APP/Contents/Info.plist"

# kf6 高亮库（macdeployqt 不打包非 Qt 依赖）
install_name_tool -change "$KF6/libKF6SyntaxHighlighting.6.dylib" \
  @executable_path/../Frameworks/libKF6SyntaxHighlighting.6.31.0.dylib \
  "$APP/Contents/MacOS/naught"
"$QT/bin/macdeployqt" "$APP" -always-overwrite > /dev/null
mkdir -p "$APP/Contents/PlugIns/platforms"
cp "$QT/plugins/platforms/libqoffscreen.dylib" "$APP/Contents/PlugIns/platforms/"
cp "$KF6/libKF6SyntaxHighlighting.6.31.0.dylib" "$APP/Contents/Frameworks/"

codesign --force --deep --sign - "$APP" > /dev/null
codesign --verify --deep --strict "$APP"
env -i HOME="$HOME" QT_QPA_PLATFORM=offscreen "$APP/Contents/MacOS/naught" --selftest

rm -rf "$HOME/Applications/naught-preview.app"
mv "$APP" "$HOME/Applications/naught-preview.app"

LSREG="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
"$LSREG" -f "$HOME/Applications/naught-preview.app" >/dev/null 2>&1 || true
rm -f /tmp/naught-freeze-diag.log
echo "已部署：$HOME/Applications/naught-preview.app（無·预览 / com.2liver.naught.preview）"
