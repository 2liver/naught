#!/bin/sh
# 「無」(naught) —— 从源码构建并安装到启动台（macOS）
# 需要：Qt 6 + CMake。用法：./install.sh
set -e
cd "$(dirname "$0")"

cmake -S . -B build
cmake --build build
cmake --install build --prefix "$HOME/Applications"

LSREG="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
"$LSREG" -f "$HOME/Applications/naught.app" >/dev/null 2>&1 || true

echo "已安装：$HOME/Applications/naught.app（启动台显示为「無」）"
