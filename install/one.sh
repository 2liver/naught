#!/bin/sh
# 「無」(naught) 一条指令安装（macOS，无需开发环境）
#
#   curl -fsSL https://raw.githubusercontent.com/2liver/naught/main/install/one.sh | sh
#
# 自定义安装位置（仅默认的 ~/Applications 会出现在启动台）：
#   curl -fsSL .../install/one.sh | sh -s -- /你想要的目录
#
# 无法访问 GitHub 时先设置代理，例如：
#   export https_proxy=http://127.0.0.1:7897
set -e

PREFIX="${1:-$HOME/Applications}"
REPO="https://github.com/2liver/naught"

LATEST=$(curl -fsSL "https://api.github.com/repos/2liver/naught/releases/latest" \
    | sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p')
[ -n "$LATEST" ] || { echo "找不到最新版本"; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
curl -fsSL -o "$TMP/naught.zip" "$REPO/releases/download/$LATEST/naught-macos.zip"
unzip -q -o "$TMP/naught.zip" -d "$TMP"

mkdir -p "$PREFIX"
rm -rf "$PREFIX/naught.app"
cp -R "$TMP/naught.app" "$PREFIX/"

LSREG="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
"$LSREG" -f "$PREFIX/naught.app" >/dev/null 2>&1 || true

echo "已安装：$PREFIX/naught.app"
[ "$PREFIX" = "$HOME/Applications" ] && echo "启动台/聚焦搜索里找「無」"
