#!/usr/bin/env bash
# ============================================================================
# 文件：examples/l3_grid_compose.sh
# 作用：演示 L3「网格分割」模式（grid）——以基准点 (x0,y0) 为相位锚、单元尺寸 (cw,ch) 为周期
#       生成贯穿全图的切割线并诱导网格（行 / 列数由图像边界自动推导，非参数）；逐单元
#       --keep / --remove 选择，可排序（--sort/--decorate/--order）后分离导出或 --compose 重排合并。
# 约定（examples/README.md 约定）：使用仓库内示例图片 examples/sample.png；输出到系统临时目录，
#       不污染仓库；每一步带中文注释。
# 前置：需先构建出 idc；sample.png 需自备（建议 640×480，见 examples/README.md）。
#       本脚本以 --grid 0,0,160,120 为例：640×480 图恰好铺成 4 列 × 4 行 = 16 个单元，
#       单元序号按行主序 index = row*4 + col。
# ============================================================================
set -euo pipefail

# --- 定位脚本目录与仓库根。---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# --- 定位 idc（同 l1_extract.sh 的解析策略）。---
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

# --- 输入图与临时输出目录。---
IMG="${SCRIPT_DIR}/sample.png"
OUT="$(mktemp -d)"
trap 'echo "输出目录：${OUT}"' EXIT

if [ ! -f "${IMG}" ]; then
  echo "错误：未找到示例图片 ${IMG}（请先提供，详见 examples/README.md）" >&2
  exit 3
fi

echo "== L3 网格分割（grid）=="

# 1) 保留四角单元 (0,0)(0,3)(3,0)(3,3)，重排合并到 2x2 画布（每格 160×120）→ 320×240。
#    缺省 --sort row-major：按序号升序 [0,3,12,15] 依次填入画布格子。
"${IDC}" grid --input "${IMG}" --grid 0,0,160,120 \
  --keep 0,0 --keep 0,3 --keep 3,0 --keep 3,3 \
  --compose --canvas 2x2 --output "${OUT}/grid_corners_2x2.png"

# 2) 列主序重排：同样保留四角，但 --sort column-major 改变填入顺序。
"${IDC}" grid --input "${IMG}" --grid 0,0,160,120 \
  --keep 0,0 --keep 0,3 --keep 3,0 --keep 3,3 \
  --sort column-major --compose --canvas 2x2 --output "${OUT}/grid_corners_colmajor.png"

# 3) 自定义序列：--sort custom + --order 显式定义保留单元与其顺序（此处对角线两格，先右下后左上）。
"${IDC}" grid --input "${IMG}" --grid 0,0,160,120 \
  --sort custom --order 3,3;0,0 \
  --compose --canvas 2x1 --output "${OUT}/grid_custom_order.png"

# 4) 删除单元 + 分离导出：--remove 指定要删除的单元，其余保留并逐块写入文件夹。
#    --margin discard（缺省）：跨越图像边界的残缺单元直接舍弃。
"${IDC}" grid --input "${IMG}" --grid 0,0,160,120 \
  --remove 1,1 --remove 2,2 --margin discard \
  --output-dir "${OUT}/grid_separate" --format png --naming "cell_{index:03d}"

echo "完成：重排合并 3 张 + 分离文件夹已写入 ${OUT}"
ls -1 "${OUT}/grid_separate"
