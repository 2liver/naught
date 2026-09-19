#!/bin/bash
# 部署正式版到启动台：~/Applications/naught.app（無）。
# bundle ID com.2liver.naught（正式渠道）——与预览版 com.2liver.naught.preview 共存。
# 用法：scripts/deploy-stable.sh
set -euo pipefail
cd "$(dirname "$0")/.."

CM=/Users/2liver/Library/Python/3.9/lib/python/site-packages/cmake/data/bin/cmake
QT=/Users/2liver/Qt/6.9.3/macos
KF6=/Users/2liver/kf6/lib

STAGE=$(mktemp -d /tmp/naught-deploy.XXXXXX)
trap 'rm -rf "$STAGE"' EXIT

"$CM" --install build --prefix "$STAGE" > /dev/null
APP="$STAGE/naught.app"

# 正式渠道 bundle 元数据
/usr/libexec/PlistBuddy -c 'Add :CFBundleDisplayName string 無' \
  "$APP/Contents/Info.plist" 2>/dev/null \
  || /usr/libexec/PlistBuddy -c 'Set :CFBundleDisplayName 無' "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleName 無' "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleIdentifier com.2liver.naught' \
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

rm -rf "$HOME/Applications/naught.app"
mv "$APP" "$HOME/Applications/naught.app"

LSREG="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
"$LSREG" -f "$HOME/Applications/naught.app" >/dev/null 2>&1 || true
echo "已部署：$HOME/Applications/naught.app（無 / com.2liver.naught / $(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$HOME/Applications/naught.app/Contents/Info.plist")）"
