#!/usr/bin/env bash
# ============================================================================
# 文件：examples/l_annotate.sh
# 作用：演示 CLI 标注输入与导出——命令行给三层命令（extract / erase / grid）附加标注，
#       并用 --save-config 把标注透写回配置 JSON（导出）。
#       覆盖两种载体：
#         · --annotation '<内联 JSON 对象>'：一条 SPEC §7 annotations 元素，可重复；
#         · --annotations <file.json>      ：文件根为标注数组，可多条，可重复。
# 约定（examples/README.md）：使用仓库内示例图片 assets/sample.png（仓库根）；输出到系统临时目录，
#       不污染仓库；每一步带中文注释。
# 前置：需先构建出可执行文件 idc；sample.png 已随仓库提供（仓库根 assets/，≥ 640×480）。
# ============================================================================
set -euo pipefail

# --- 定位脚本目录与仓库根。---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# --- 定位 idc 可执行文件：优先 IDC 环境变量，其次常见构建输出目录，最后回退 PATH。---
IDC="${IDC:-}"
if [ -z "${IDC}" ]; then
  for cand in \
    "${REPO_ROOT}/build/bin/idc" "${REPO_ROOT}/build/bin/idc.exe" \
    "${REPO_ROOT}/cmake-build-debug/bin/idc" "${REPO_ROOT}/cmake-build-debug/bin/idc.exe" \
    "${REPO_ROOT}/cmake-build-release/bin/idc" "${REPO_ROOT}/cmake-build-release/bin/idc.exe" \
    "$(command -v idc 2>/dev/null || true)"; do
    if [ -n "${cand}" ] && [ -x "${cand}" ]; then IDC="${cand}"; break; fi
  done
fi
IDC="${IDC:-idc}"

IMG="${SCRIPT_DIR}/../../../assets/sample.png"
ANNOS="${SCRIPT_DIR}/sample_annotations.json"
OUT="$(mktemp -d)"
trap 'echo "输出目录：${OUT}"' EXIT

if [ ! -f "${IMG}" ]; then
  echo "错误：未找到示例图片 ${IMG}（请先提供，详见 examples/README.md）" >&2
  exit 3
fi

echo "== CLI 标注输入与导出 =="

# 1) extract + 单条内联标注：在保留矩形上烧录一个填充绿圆（c 坐标为原图逻辑坐标）。
"${IDC}" extract --input "${IMG}" --rect 0,0,480,320 \
  --annotation '{"type":"circle","x1":100,"y1":100,"x2":140,"y2":100,"color":"#FF2E7D32","fillColor":"#FF2E7D32","fill":true,"strokeWidth":2}' \
  --output "${OUT}/extract_inline_circle.png"

# 2) extract + 多条内联标注（--annotation 可重复）：一条线 + 一段文字。
"${IDC}" extract --input "${IMG}" --rect 0,0,480,320 \
  --annotation '{"type":"line","points":[[10,300],[470,300]],"color":"#FF1565C0","strokeWidth":4}' \
  --annotation '{"type":"text","x":12,"y":290,"text":"hello idc","fontSize":22,"color":"#FFD84315"}' \
  --output "${OUT}/extract_inline_multi.png"

# 3) extract + 标注文件（根数组：矩形框 + 填充圆 + 文字，见 sample_annotations.json）。
"${IDC}" extract --input "${IMG}" --rect 0,0,480,320 \
  --annotations "${ANNOS}" \
  --output "${OUT}/extract_from_file.png"

# 4) 导出（透写）：extract 附加标注并用 --save-config 把参数+标注序列化到配置 JSON。
#    （--save-config 为 dry-run：只写出配置，不产出图像；--output 仍为必填参数但被忽略。）
"${IDC}" extract --input "${IMG}" --rect 0,0,480,320 \
  --annotations "${ANNOS}" \
  --save-config "${OUT}/cfg_with_annos.json" \
  --output "${OUT}/_ignored.png"

# 5) 配置往返：config --load 上一步导出的配置，再追加一条命令行标注并透写。
#    （命令行标注追加在配置自带 annotations 段之后；dry-run 无需 --input。）
"${IDC}" config --load "${OUT}/cfg_with_annos.json" \
  --annotation '{"type":"line","points":[[0,0],[480,320]],"color":"#FF000000","strokeWidth":2}' \
  --save-config "${OUT}/cfg_merged.json"

echo "完成：标注样例已写入 ${OUT}（cfg_with_annos.json / cfg_merged.json 均含 annotations 段）"
