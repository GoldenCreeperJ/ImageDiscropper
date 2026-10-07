#!/bin/bash
# ============================================================================
# 文件：assets/icons/make_icns.sh
# 作用：从本目录 mac/icon_NxN.png（iconset 命名，由母版 assets/icon.png 经 ffmpeg 派生）
#       打包生成 mac/idc.icns——需在 macOS 上运行（依赖系统 iconutil）。
# 用法：cd assets/icons && bash make_icns.sh
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")"

ICONSET="mac/idc.iconset"
rm -rf "$ICONSET"
mkdir -p "$ICONSET"

# iconset 命名：icon_16x16.png / icon_16x16@2x.png（=32）… icon_512x512@2x.png（=1024）。
for s in 16 32 128 256 512; do
    cp "mac/icon_${s}x${s}.png" "$ICONSET/icon_${s}x${s}.png"
done
cp "mac/icon_32x32.png"  "$ICONSET/icon_16x16@2x.png"
cp "mac/icon_64x64.png"  "$ICONSET/icon_32x32@2x.png"
cp "mac/icon_256x256.png" "$ICONSET/icon_128x128@2x.png"
cp "mac/icon_512x512.png" "$ICONSET/icon_256x256@2x.png"
cp "mac/icon_1024x1024.png" "$ICONSET/icon_512x512@2x.png"

iconutil -c icns "$ICONSET" -o mac/idc.icns
rm -rf "$ICONSET"
echo "已生成 mac/idc.icns（提交后 GUI CMakeLists 将自动注入 bundle Resources）。"
