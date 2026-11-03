#!/bin/bash
# ============================================================================
# 文件：assets/icons/regenerate.sh
# 作用：应用图标全流程派生（幂等、跨平台，供本地与 CI 动态构建）——
#       ① assets/icon.svg（唯一入库的矢量母版，VTracer 出品，背景填充 #b9c495，圆角 rx=ry=128）
#          → 光栅化 1024 **圆角母版** assets/icons/icon_round.png（带 alpha，四角随 rx 透明）；
#          → 圆角母版压平到背景色 #b9c495 得 **直角满幅母版** assets/icons/icon.png（无 alpha，
#            脚本内校验——iOS App Store 硬性要求；Play 商店列表图标亦由商店自套遮罩）；
#       ①-b macOS 专用 **squircle 母版**：由 icon.svg 派生 assets/icons/icon_mac.svg（背景 rect
#            改写为角部超椭圆 path，n=3 连续曲率、等效 rx=184）→ 光栅 icon_round_mac.png；仅 mac 档使用；
#       注：除 icon.svg 外本脚本写出的每一个文件都在 assets/icons/ 下（母版也在此），
#       assets/ 根只留入库的 icon.svg 与示例输入 sample.png；
#       ② 三套母版 → 各平台尺寸（ffmpeg lanczos）：外壳不套圆角的目标取圆角母版
#          （Windows / Linux / Android legacy / README 小图），macOS 取 squircle 母版，
#          其余取直角母版；macOS 另按 Apple 模板留边（内容占画布 80%、四周透明）——Dock 不套遮罩；
#       ③ Windows idc.ico（9 尺寸条目，ffmpeg 多流 mux；显式 -c:v bmp -pix_fmt bgra 定经典
#          32bpp BMP/DIB 条目并自检——兼容性最强，PNG 条目仅是 Vista+ 扩展，故不求 PNG；无需 PowerShell）；
#       ④ svg 复制为 Linux scalable 图标；
#       ⑤ Android 自适应：从 SVG **删除背景元素**（fill 等于 #b9c495 的 rect/path，按 XML 解析）
#          得前景 SVG → 光栅 1024 → 缩 432；背景层为纯色。确定性 100%、零阈值。
#       ⑥ Android 模板密度系列（ImageDiscropperCpp/packaging/android/res/mipmap-*，Qt 打包源目录）。
# 依赖：Inkscape（或 rsvg-convert）、ffmpeg、python3（①-b squircle 母版 与 ⑤ 删除背景元素）。
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
# ① svg → 圆角母版（带 alpha）→ 压平得直角满幅母版 + 无 alpha 校验
# ---------------------------------------------------------------------------
echo "== ① 光栅化 1024 圆角母版 =="
rasterize "$ROOT/assets/icon.svg" 1024 "$ICONS/icon_round.png"

echo "== 压平到背景色得直角满幅母版 =="
# 圆角切掉的四角填回 #b9c495（与背景同色），产物即满幅直角方块、零透明像素。
# overlay 显式指定 format=rgb：两路输入已是 rgb24/rgba，auto 当下也协商到 rgb24（实测两种写法
# 零像素差异），但显式写明可防版本漂移把 alpha 混合改到 YUV 空间做（4:2:0 会弄脏抗锯齿边）。
"$FFMPEG" -y -loglevel error -f lavfi -i "color=c=0xb9c495:s=1024x1024,format=rgb24" \
    -i "$ICONS/icon_round.png" \
    -filter_complex "[0:v][1:v]overlay=format=rgb,format=rgb24" -frames:v 1 "$ICONS/icon.png"

echo "== 校验直角母版无 alpha =="
if "$FFMPEG" -i "$ICONS/icon.png" -vf alphaextract,format=gray -f rawvideo - 2>/dev/null \
     | tr -d '\377' | grep -q .; then
    echo "直角母版含透明像素：圆角母版压平未生效，iOS 光栅会被拒。" >&2; exit 1
fi
echo "OK：1024 直角母版全不透明。"

# ---------------------------------------------------------------------------
# ①-b macOS 专用 squircle 母版：背景 rect 改写为超椭圆 path 后单独光栅一版
# ---------------------------------------------------------------------------
echo "== ①-b 生成 macOS squircle 母版 =="
# Apple 图标轮廓是连续曲率（squircle），而 rect 的 rx/ry 是纯圆弧：直边与圆角接缝处曲率
# 突变为 0，放大看有"撞墙感"。故把背景 rect 改写为**角部超椭圆**路径，仍是矢量层加工
# （无光栅掩膜）：边中段为精确直线，仅四个角以曲率连续过渡的曲线连接。
# 构造：每角圆心 C 固定，边界点 P(θ) = C + R·(sgn(cosθ)|cosθ|^m, sgn(sinθ)|sinθ|^m)，m=2/n，
# θ 绕一圈；n=2 时精确退化为圆形 rx=R（实测：与 rect rx=184 的 1024 光栅相比，
# 1048576 像素中仅 950 个有差且全在抗锯齿过渡带上——无一侧为全透/全不透明的硬差，
# RGB 通道零差异）。
# n 取 3、R 由"视觉圆角大小不变"反解：45° 内缩量 R(1-0.707^(2/n)) 锁定为圆形 rx=184
# （= Apple 常用 ≈18% 角半径）的 53.9px，得 R=261.2（过渡延展 1.42× 圆弧，两切点间直段长 501.5px、
# 占整条边 49%）；n=2.5 偏保守（R=222.6，1.21×）、n≥4 角部开始"塌肩"。
# 采样按**逐角扫**组织（每角 128 区间、含两端切点，共 516 点），而非整圈等分 θ：整圈扫
# 在四个轴线处只落一个采样点、跳过另一个切点，直边端点因此偏离精确切点（本例坐标差 13.9px、
# 形位偏离 0.0128px；超椭圆在切点处与直边相切，故坐标差远大于形状差）。逐角扫让 8 个切点
# 全部精确命中，相邻角端点同处一条边线上，连线即精确直边。
# 精度（以隐式方程 |dx/R|^n+|dy/R|^n=1 的一阶距离量）：折线相对解析边界最大偏离 0.0051px；
# 坐标再按三位小数量化（≤0.0005px）。三位小数固定格式亦保证跨平台确定性。
"$PYTHON" - "$ROOT/assets/icon.svg" "$ICONS/icon_mac.svg" <<'PYEOF'
import math
import sys
import xml.etree.ElementTree as ET

src, dst = sys.argv[1], sys.argv[2]
SVG_NS = "http://www.w3.org/2000/svg"
BG = "#b9c495"
EXP, R_EQ, SEG = 3.0, 184.0, 128   # n / 等效圆形角半径 / 每角区间数（整圈 4×SEG=512 区间、516 点）

ET.register_namespace("", SVG_NS)
tree = ET.parse(src)
root = tree.getroot()
# 画布边长（用户单位）：有 viewBox 就取它第三段——width 可能被编辑器写成物理尺寸
# （1024px / 27in / 100%），而 viewBox 恒为用户单位；无 viewBox 才取 width（母版当前即此形态）。
# 两者均只接受纯数字或末尾带 px 的写法，否则报错不猜（圆心与满幅断言都建立在用户单位上）。
def num(raw):
    v = (raw or "").strip()
    if v.endswith("px"):
        v = v[:-2].strip()
    try:
        return float(v)
    except ValueError:
        return None

vb = (root.get("viewBox") or "").split()
edge = ("viewBox 第三段", vb[2]) if len(vb) == 4 else ("svg width", root.get("width"))
s = num(edge[1])
if s is None:
    sys.exit("squircle 母版异常：%s 为 %r，非纯数字/px 长度，无法确定画布边长" % edge)
# 同 45° 内缩下反解超椭圆角半径 R：圆形 rx=r 的内缩为 r(1-1/√2)。
m = 2.0 / EXP
r = R_EQ * (1 - math.sqrt(0.5)) / (1 - math.sqrt(0.5) ** m)
# 逐角扫：每角圆心固定，θ 覆盖该角整段并含两端切点（右上→右下→左下→左上）。
pts = []
for i, (cx, cy) in enumerate(((s - r, r), (s - r, s - r), (r, s - r), (r, r))):
    a = -math.pi / 2 + i * math.pi / 2
    for j in range(SEG + 1):
        th = a + (math.pi / 2) * j / SEG
        c, sn = math.cos(th), math.sin(th)
        pts.append((cx + math.copysign(r * abs(c) ** m, c),
                    cy + math.copysign(r * abs(sn) ** m, sn)))
hit = 0
for parent in list(root.iter()):
    for el in list(parent):
        if el.tag == "{%s}rect" % SVG_NS and (el.get("fill") or "").lower() == BG:
            # 上述圆心按满幅 (0,0,s,s) 推得；rect 若带偏移或非满幅则圆心错位，先校验再改写。
            # 属性同样过 num（缺省按 SVG 规范取 0）；不可解析单独报错，不落到"非满幅"的误导分支。
            raw = [el.get(k) or "0" for k in ("x", "y", "width", "height")]
            box = tuple(num(v) for v in raw)
            if any(v is None for v in box):
                sys.exit("squircle 母版异常：背景 rect 尺寸为不可解析长度 %s" % raw)
            if box != (0.0, 0.0, s, s):
                sys.exit("squircle 母版异常：背景 rect 为 %s，非满幅 (0,0,%g,%g)" % (box, s, s))
            # 就地换标签 rect → path：保留 fill，丢弃 rx/ry（圆角由超椭圆接管）。
            el.tag = "{%s}path" % SVG_NS
            for attr in ("width", "height", "x", "y", "rx", "ry"):
                el.attrib.pop(attr, None)
            el.set("d", "M%.3f %.3f %sZ" % (pts[0][0], pts[0][1],
                                           " ".join("L%.3f %.3f" % q for q in pts[1:])))
            hit += 1
if hit != 1:
    sys.exit("squircle 母版异常：背景 rect 命中 %d 个（应为 1）" % hit)
tree.write(dst, encoding="utf-8", xml_declaration=True)
PYEOF
rasterize "$ICONS/icon_mac.svg" 1024 "$ICONS/icon_round_mac.png"
echo "OK：icon_mac.svg → icon_round_mac.png（角部 squircle，n=3、R≈261、逐角扫 516 点）"

# ---------------------------------------------------------------------------
# ② 各平台尺寸
# ---------------------------------------------------------------------------
echo "== ② 派生各平台尺寸 =="
# CI 全新 checkout 下各平台目录可能不存在，先建齐。
mkdir -p "$ICONS/windows" "$ICONS/mac" "$ICONS/ios" "$ICONS/android" "$ICONS/linux"
resize() { # $1=size $2=dst（从直角满幅母版）
  "$FFMPEG" -y -loglevel error -i "$ICONS/icon.png" -vf "scale=$1:$1:flags=lanczos" "$2"
}
resize_r() { # $1=size $2=dst（从圆角母版，保留 alpha）
  "$FFMPEG" -y -loglevel error -i "$ICONS/icon_round.png" -vf "scale=$1:$1:flags=lanczos" "$2"
}
resize_from() { # $1=src $2=size $3=dst（从任意源）
  "$FFMPEG" -y -loglevel error -i "$1" -vf "scale=$2:$2:flags=lanczos" "$3"
}
mac_resize() { # $1=size $2=dst（squircle 母版按 Apple 模板缩至 80% 居中，四周透明）
  # 内容边长四舍五入到偶数（保证 (size-content)/2 为整数、四边对称），
  # 使各档尽量贴住 80%：16→12、32→26、64→52、128→102、256→204、512→410、1024→820。
  local c=$(( ( $1 * 4 / 5 + 1 ) / 2 * 2 ))
  "$FFMPEG" -y -loglevel error -i "$ICONS/icon_round_mac.png" \
      -vf "scale=$c:$c:flags=lanczos,pad=$1:$1:(ow-iw)/2:(oh-ih)/2:color=black@0.0,format=rgba" "$2"
}
# README 展示用小图（128px，**入库**——GitHub 不渲染 SVG，README 以 PNG 展示图标）。
# 圆角由 svg 矢量决定，抗锯齿边来自 Inkscape 光栅化本身（非光栅掩膜）。
resize_r 128 "$ICONS/icon-readme.png"
# Windows 外壳不给图标套圆角 → 圆角母版（alpha 随 .ico 的 32bpp DIB 条目走）。
for s in 16 20 24 32 40 48 64 128 256; do resize_r $s "$ICONS/windows/icon-$s.png"; done
# StoreLogo 是磁贴资产（外壳自己套圆角），取直角母版以免双重圆角。
resize 50 "$ICONS/windows/StoreLogo-50.png"
# macOS：Dock / Finder / Command-Tab 画的就是 icns 像素本身（**无系统遮罩**，与 iOS 不同）
# → 取 ①-b 的 squircle 母版（Apple 轮廓为连续曲率），并按 Apple 模板留 20% 透明边。
for s in 16 32 64 128 256 512 1024; do mac_resize $s "$ICONS/mac/icon_${s}x${s}.png"; done
# Linux 桌面外壳不套圆角 → 圆角母版。
for s in 16 22 24 32 48 64 128 256 512; do
  mkdir -p "$ICONS/linux/hicolor/${s}x${s}/apps"
  resize_r $s "$ICONS/linux/hicolor/${s}x${s}/apps/imagediscropper.png"
done
# iOS 由系统套圆角遮罩且要求无 alpha → 直角母版。
# 尺寸为非完整 Asset Catalog 集（缺 58/60/76/87 等 @2x/@3x 档）：现代 Xcode 用单尺寸 1024
# 自动派生全部，其余仅作无资产目录流程的保险。
for s in 20 29 40 80 120 152 167 180; do resize $s "$ICONS/ios/icon-$s.png"; done
# 1024 档与母版同尺寸（lanczos 1024→1024 是恒等重编码），直接复制省一次 PNG 重编码。
cp "$ICONS/icon.png" "$ICONS/ios/icon-1024.png"
# android/ 仅派生 Play 商店列表图标（512；商店自套遮罩，取直角母版）；
# 启动器/自适应层见 ⑤，res 密度系列见 ⑥。
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
# ico muxer 的默认编码器随 FFmpeg 版本而变（新版可能选 PNG 条目），故显式指定 BMP。
# 选项须放在输入之后、输出之前：放在 -i 前会被当成输入解码器默认值（拿 BMP 去解 PNG）。
# -pix_fmt bgra 亦显式写死：bmp 编码器对 rgba 输入当下选 bgra（32bpp），但"取第一个支持的
# pix_fmt"未写进文档；若某版本改选 bgr24 则 wBitCount=24，自检报错的原因会是版本漂移而非
# 母版改样。显式指定后，自检的 bpp 判据只会因参数/母版被改而触发（实测加此选项产物字节不变）。
"$FFMPEG" -y -loglevel error "${ICO_ARGS[@]}" -c:v bmp -pix_fmt bgra "$ICONS/windows/idc.ico"
# 自检：直接解析 ICO 目录项指向的图像头。注意 ICO 的 BMP 条目**不带 14 字节 BITMAPFILEHEADER**，
# 图像数据直接以 DIB 头开始（biSize=40），而 PNG 条目以 \x89PNG 签名开始——故判据为
# “非 PNG 签名 + DIB 头长度合法 + 32bpp”，而不是找 'BM' 魔数。
# 不用 grep ffmpeg 的 -i 探测文本（版本间格式会变），也不引入 ffprobe 这个未声明依赖。
# 亦不补 struct.unpack_from 的字节边界检查：输入是上一步 ffmpeg 刚产出的本地产物，截断场景不存在；
# 真被截断则 struct.error 直接非零退出（set -euo pipefail 下整步即红），不会静默给出错误结论。
"$PYTHON" - "$ICONS/windows/idc.ico" <<'PYEOF'
import struct
import sys

PNG = b"\x89PNG\r\n\x1a\n"
# 合法 DIB 头长度：BITMAPCOREHEADER 12 / OS22XBITMAPHEADER 16、24 / BITMAPINFOHEADER 40 /
# BITMAPV3HEADER 56 / BITMAPV4HEADER 108 / BITMAPV5HEADER 124（ffmpeg bmp 编码器只出 40）。
DIB_SIZES = (12, 16, 24, 40, 56, 108, 124)
data = open(sys.argv[1], "rb").read()
reserved, kind, count = struct.unpack_from("<HHH", data, 0)
bad = []
for i in range(count):
    entry = 6 + 16 * i
    bpp = struct.unpack_from("<H", data, entry + 6)[0]      # wBitCount
    img = struct.unpack_from("<I", data, entry + 12)[0]     # dwImageOffset
    is_png = data[img:img + 8] == PNG
    bi_size = 0 if is_png else struct.unpack_from("<I", data, img)[0]
    if is_png or bi_size not in DIB_SIZES or bpp not in (0, 32):
        bad.append("#%d png=%d biSize=%d bpp=%d" % (i, is_png, bi_size, bpp))
if reserved != 0 or kind != 1 or count != 9 or bad:
    # reserved 必为 0（ICONDIR 首字段）；type 必为 1（图标，2 为光标）。
    sys.exit("idc.ico 条目异常：reserved=%d type=%d count=%d %s"
             % (reserved, kind, count, "; ".join(bad)))
PYEOF
echo "OK：idc.ico（9 尺寸，32bpp BMP/DIB 条目）"

# ---------------------------------------------------------------------------
# ④ Linux scalable（svg 本体）
# ---------------------------------------------------------------------------
echo "== ④ 复制 Linux scalable =="
mkdir -p "$ICONS/linux/scalable/apps"
cp "$ROOT/assets/icon.svg" "$ICONS/linux/scalable/apps/imagediscropper.svg"

# ---------------------------------------------------------------------------
# ⑤ Android 自适应：删除背景元素得前景 SVG → 光栅 1024 → 缩 432；背景纯色。
#    （图形内部不含 #b9c495，删完即精确前景，无阈值/无 chromakey 误伤）
# ---------------------------------------------------------------------------
echo "== ⑤ Android 自适应前后景（XML 删除背景元素）=="
"$PYTHON" - "$ROOT/assets/icon.svg" "$ICONS/android/icon_foreground.svg" <<'PYEOF'
import sys
import xml.etree.ElementTree as ET

src, dst = sys.argv[1], sys.argv[2]
BG = "#b9c495"
ET.register_namespace("", "http://www.w3.org/2000/svg")
tree = ET.parse(src)
# 走 XML 解析而非正则：属性引号、颜色大小写、是否自闭合、属性顺序都不影响判定。
# 副作用：ElementTree 重写输出会丢弃注释与 DOCTYPE（当前母版两者皆无；若日后添加需知情）。
removed = 0
for parent in list(tree.getroot().iter()):
    for el in list(parent):
        if el.tag.rsplit("}", 1)[-1] in ("rect", "path") \
                and (el.get("fill") or "").lower() == BG:
            parent.remove(el)
            removed += 1
# 图形内部不含背景色，所以命中数必为 1；不等则母版已改样，宁可报错也不静默错删。
if removed != 1:
    sys.exit("前景剥离异常：fill=%s 命中 %d 个元素（应为 1）" % (BG, removed))
tree.write(dst, encoding="utf-8", xml_declaration=True)
PYEOF
rasterize "$ICONS/android/icon_foreground.svg" 1024 "$ICONS/android/ic_foreground_1024.png"
resize_from "$ICONS/android/ic_foreground_1024.png" 432 "$ICONS/android/ic_foreground.png"
# 注意：color 源默认 yuv420p，RGB→YUV→RGB 会偏 2（如 0x2F→0x2D）；图内 format=rgb24 使其直出。
"$FFMPEG" -y -loglevel error -f lavfi -i "color=c=0xb9c495:s=432x432,format=rgb24" -frames:v 1 \
    "$ICONS/android/ic_background.png"
echo "OK：ic_foreground/ic_background（XML 删除背景元素，确定性 100%）"

# ---------------------------------------------------------------------------
# ⑥ Android 模板密度系列：Qt 打包源目录（ImageDiscropperCpp/packaging/android/res/mipmap-*）
#    直接引用这些文件，故从 ② 的母版尺寸与 ⑤ 的前景层按密度缩放刷新。
#    密度乘数不是等比：mdpi→hdpi→xhdpi→xxhdpi→xxxhdpi 为 1.5 / 1.3 / 1.5 / 1.3 交替，
#    故逐档写死（连续 *3/2 会得 48/72/108/162/243 与 108/162/243/364/546，均偏大）。
#    AndroidManifest 只引用 @mipmap/ic_launcher（无 android:roundIcon），圆形启动器由
#    系统自套遮罩，故不派生 ic_launcher_round。
#    （colors.xml / ic_launcher.xml 为手工文件，不属派生，脚本不触碰。）
# ---------------------------------------------------------------------------
echo "== ⑥ Android 模板 res 密度系列 =="
RES="$ROOT/ImageDiscropperCpp/packaging/android/res"
for spec in "mdpi 48 108" "hdpi 72 162" "xhdpi 96 216" "xxhdpi 144 324" "xxxhdpi 192 432"; do
  # 用 read 而非 set --：不覆盖脚本位置参数（$1/$@）。
  read -r density legacy fg <<< "$spec"
  mkdir -p "$RES/mipmap-$density"
  # legacy 图标（预 API 26 的启动器不套遮罩）→ 圆角母版。
  resize_r "$legacy" "$RES/mipmap-$density/ic_launcher.png"
  resize_from "$ICONS/android/ic_foreground.png" "$fg" "$RES/mipmap-$density/ic_launcher_foreground.png"
done
echo "OK：res 密度系列（ic_launcher 48/72/96/144/192 + foreground 108/162/216/324/432）"

echo ""
echo "全部完成。清单："
echo "  本目录    icon.png（直角满幅，无 alpha）+ icon_round.png（圆角带 alpha）+ icon_round_mac.png（squircle）"
echo "            + icon_mac.svg（squircle 派生 svg）+ icon-readme.png（128，README 展示用，唯一入库的派生物）"
echo "  windows/  idc.ico(9) + icon-{16..256}.png + StoreLogo-50.png"
echo "  mac/      icon_{16..1024}x{16..1024}.png（squircle 圆角 + 20% 透明边，idc.icns 由 CI macos 腿 make_icns.sh 动态生成）"
echo "  linux/    hicolor{16,22,24,32,48,64,128,256,512} + scalable/imagediscropper.svg"
echo "  ios/      icon-{20,29,40,80,120,152,167,180,1024}.png"
echo "  android/  icon-512.png（Play 商店列表图标） + ic_foreground/ic_background(432, 自适应)"
echo "  res/      ImageDiscropperCpp/packaging/android/res/mipmap-*（legacy 48..192 + foreground 108..432）"
