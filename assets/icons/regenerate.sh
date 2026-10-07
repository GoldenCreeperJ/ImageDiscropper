#!/bin/bash
# ============================================================================
# 文件：assets/icons/regenerate.sh
# 作用：应用图标全流程派生（幂等、跨平台，供本地与 CI 动态构建）——
#       ① assets/icon.svg（矢量母版，VTracer 出品，背景填充 #242F00）
#          → 光栅化 1024 母版 assets/icon.png（iOS 硬性要求：无 alpha，脚本内校验）；
#       ② 1024 母版 → 各平台尺寸（ffmpeg lanczos）；
#       ③ Windows idc.ico（9 尺寸条目，ffmpeg 多流 mux；条目为经典 BMP/DIB——
#          兼容性最强，PNG 条目仅是 Vista+ 扩展，故不求 PNG；无需 PowerShell）；
#       ④ svg 复制为 Linux scalable 图标；
#       ⑤ Android 自适应：从 SVG **剥离背景路径**（fill=#242F00，图形内部不含同色）
#          得前景 SVG → 光栅 1024 → 缩 432；背景层为纯色。确定性 100%、零阈值。
#       ⑥ Android 模板密度系列（ImageDiscropperCpp/packaging/android/res/mipmap-*，Qt 打包源目录）。
# 依赖：Inkscape（或 rsvg-convert）、ffmpeg、python3（仅 ⑤ 剥离背景路径）。
# 用法：bash assets/icons/regenerate.sh（Git Bash / Linux / macOS / CI ubuntu runner）
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ICONS="$ROOT/assets/icons"
INKSCAPE="${INKSCAPE:-/d/Applications/Inkscape/bin/inkscape.com}"
FFMPEG="${FFMPEG:-/d/Runtimes/Ffmpeg/ffmpeg-8.0.1-essentials_build/bin/ffmpeg.exe}"
PYTHON="${PYTHON:-/d/Runtimes/Python/Python312/python.exe}"

# rsvg-convert（CI ubuntu：apt install librsvg2-bin）与 Inkscape 参数形态不同，按名分派。
if command -v "$INKSCAPE" >/dev/null 2>&1; then
    case "$(basename "$INKSCAPE")" in
        rsvg-convert*|rsvg*) RASTER=rsvg ;;
        *) RASTER=inkscape ;;
    esac
elif command -v rsvg-convert >/dev/null 2>&1; then
    INKSCAPE=rsvg-convert; RASTER=rsvg
else
    echo "未找到 Inkscape/rsvg-convert（可用环境变量 INKSCAPE 指定）"; exit 1
fi
command -v "$FFMPEG" >/dev/null 2>&1 || { echo "未找到 ffmpeg（可用环境变量 FFMPEG 指定）"; exit 1; }
command -v "$PYTHON" >/dev/null 2>&1 || { echo "未找到 python3（可用环境变量 PYTHON 指定）"; exit 1; }

rasterize() { # $1=svg $2=width $3=out.png
    if [ "$RASTER" = "rsvg" ]; then
        "$INKSCAPE" -w "$2" -h "$2" -o "$3" "$1"
    else
        "$INKSCAPE" --export-type=png --export-width="$2" -o "$3" "$1"
    fi
}

# ---------------------------------------------------------------------------
# ① svg → 1024 母版 + 无 alpha 校验
# ---------------------------------------------------------------------------
echo "== ① 光栅化 1024 母版 =="
rasterize "$ROOT/assets/icon.svg" 1024 "$ROOT/assets/icon.png"

echo "== 校验母版无 alpha =="
if "$FFMPEG" -i "$ROOT/assets/icon.png" -vf alphaextract,format=gray -f rawvideo - 2>/dev/null \
     | tr -d '\377' | grep -q .; then
    echo "母版含透明像素：SVG 背景未填实，iOS 光栅会被拒。" >&2; exit 1
fi
echo "OK：1024 母版全不透明。"

# ---------------------------------------------------------------------------
# ② 各平台尺寸
# ---------------------------------------------------------------------------
echo "== ② 派生各平台尺寸 =="
# CI 全新 checkout 下各平台目录可能不存在，先建齐。
mkdir -p "$ICONS/windows" "$ICONS/mac" "$ICONS/ios" "$ICONS/android" "$ICONS/linux"
resize() { # $1=size $2=dst（从母版）
  "$FFMPEG" -y -loglevel error -i "$ROOT/assets/icon.png" -vf "scale=$1:$1:flags=lanczos" "$2"
}
resize_from() { # $1=src $2=size $3=dst（从任意源）
  "$FFMPEG" -y -loglevel error -i "$1" -vf "scale=$2:$2:flags=lanczos" "$3"
}
for s in 16 20 24 32 40 48 64 128 256; do resize $s "$ICONS/windows/icon-$s.png"; done
resize 50 "$ICONS/windows/StoreLogo-50.png"
for s in 16 32 64 128 256 512 1024; do resize $s "$ICONS/mac/icon_${s}x${s}.png"; done
for s in 16 22 24 32 48 64 128 256 512; do
  mkdir -p "$ICONS/linux/hicolor/${s}x${s}/apps"
  resize $s "$ICONS/linux/hicolor/${s}x${s}/apps/imagediscropper.png"
done
for s in 20 29 40 80 120 152 167 180 1024; do resize $s "$ICONS/ios/icon-$s.png"; done
for s in 48 72 96 144 192; do resize $s "$ICONS/android/mipmap-$s.png"; done
resize 432 "$ICONS/android/icon-432.png"
resize 512 "$ICONS/android/icon-512.png"

# ---------------------------------------------------------------------------
# ③ Windows .ico（9 尺寸，ffmpeg 多流 mux；条目为经典 BMP/DIB）
# ---------------------------------------------------------------------------
echo "== ③ 打包 Windows idc.ico =="
ICO_ARGS=()
for s in 16 20 24 32 40 48 64 128 256; do
  ICO_ARGS+=(-i "$ICONS/windows/icon-$s.png")
done
for ((i = 0; i < 9; i++)); do ICO_ARGS+=(-map "$i"); done
"$FFMPEG" -y -loglevel error "${ICO_ARGS[@]}" "$ICONS/windows/idc.ico"
echo "OK：idc.ico（9 尺寸，BMP/DIB 条目）"

# ---------------------------------------------------------------------------
# ④ Linux scalable（svg 本体）
# ---------------------------------------------------------------------------
echo "== ④ 复制 Linux scalable =="
mkdir -p "$ICONS/linux/scalable/apps"
cp "$ROOT/assets/icon.svg" "$ICONS/linux/scalable/apps/imagediscropper.svg"

# ---------------------------------------------------------------------------
# ⑤ Android 自适应：剥离背景路径得前景 SVG → 光栅 1024 → 缩 432；背景纯色。
#    （图形内部不含 #242F00，剥离即精确前景，无阈值/无 chromakey 误伤）
# ---------------------------------------------------------------------------
echo "== ⑤ Android 自适应前后景（SVG 路径剥离）=="
"$PYTHON" - "$ROOT/assets/icon.svg" "$ICONS/android/icon_foreground.svg" <<'PYEOF'
import re, sys
src, dst = sys.argv[1], sys.argv[2]
svg = open(src, encoding="utf-8").read()
# VTracer 输出为自闭合 <path ... />；删除全部 fill="#242F00" 的背景路径（图形内部不含同色）。
svg = re.sub(r'<path[^>]*fill="#242F00"[^>]*/>', '', svg)
open(dst, "w", encoding="utf-8").write(svg)
PYEOF
rasterize "$ICONS/android/icon_foreground.svg" 1024 "$ICONS/android/ic_foreground_1024.png"
resize_from "$ICONS/android/ic_foreground_1024.png" 432 "$ICONS/android/ic_foreground.png"
# 注意：color 源默认 yuv420p，RGB→YUV→RGB 会偏 2（如 0x2F→0x2D）；图内 format=rgb24 使其直出。
"$FFMPEG" -y -loglevel error -f lavfi -i "color=c=0x242F00:s=432x432,format=rgb24" -frames:v 1 \
    "$ICONS/android/ic_background.png"
echo "OK：ic_foreground/ic_background（SVG 路径剥离，确定性 100%）"

# ---------------------------------------------------------------------------
# ⑥ Android 模板密度系列：Qt 打包源目录（ImageDiscropperCpp/packaging/android/res/mipmap-*）
#    直接引用这些文件，故从 ② 的母版尺寸与 ⑤ 的前景层按密度缩放刷新。
#    密度映射：legacy 48/72/96/144/192；foreground 108/162/216/324/432（mdpi..xxxhdpi）。
#    （colors.xml / ic_launcher.xml 为手工文件，不属派生，脚本不触碰。）
# ---------------------------------------------------------------------------
echo "== ⑥ Android 模板 res 密度系列 =="
RES="$ROOT/ImageDiscropperCpp/packaging/android/res"
for d in mdpi hdpi xhdpi xxhdpi xxxhdpi; do mkdir -p "$RES/mipmap-$d"; done
legacy=48
for d in mdpi hdpi xhdpi xxhdpi xxxhdpi; do
  resize "$legacy" "$RES/mipmap-$d/ic_launcher.png"
  legacy=$((legacy * 3 / 2))
done
fg=108
for d in mdpi hdpi xhdpi xxhdpi xxxhdpi; do
  resize_from "$ICONS/android/ic_foreground.png" "$fg" "$RES/mipmap-$d/ic_launcher_foreground.png"
  fg=$((fg * 3 / 2))
done
echo "OK：res 密度系列（ic_launcher 48..192 + foreground 108..432）"

echo ""
echo "全部完成。清单："
echo "  windows/  idc.ico(9) + icon-{16..256}.png + StoreLogo-50.png"
echo "  mac/      icon_{16..1024}x{16..1024}.png（idc.icns 由 CI macos 腿 make_icns.sh 动态生成）"
echo "  linux/    hicolor{16,22,24,32,48,64,128,256,512} + scalable/imagediscropper.svg"
echo "  ios/      icon-{20,29,40,80,120,152,167,180,1024}.png"
echo "  android/  mipmap-{48,72,96,144,192}.png + icon-432/512 + ic_foreground/ic_background(432, 自适应)"
echo "  res/      ImageDiscropperCpp/packaging/android/res/mipmap-*（legacy 48..192 + foreground 108..432）"
