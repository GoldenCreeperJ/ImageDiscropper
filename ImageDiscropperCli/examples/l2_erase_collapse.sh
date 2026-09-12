#!/usr/bin/env bash
# ============================================================================
# 文件：examples/l2_erase_collapse.sh
# 作用：演示 L2「反向剔除」模式（erase）+ 坍缩合并（--merge collapse）——极性恒 remove，
#       沿贯穿全图的切割线抽掉中缝、两侧对接，输出尺寸变小为 (W−Δx)×(H−Δy)。
#       覆盖：单矩形十字切割、横带剔除、竖带剔除、多矩形并集剔除。
# 约定（guideline §8.1）：使用仓库内示例图片 examples/sample.png；输出到系统临时目录，
#       不污染仓库；每一步带中文注释。
# 前置：需先构建出 idc；sample.png 需自备（建议 ≥ 640×480，见 examples/README.md）。
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

echo "== L2 反向剔除 + 坍缩合并（erase --merge collapse）=="

# 1) 单矩形十字切割：删除 [200,120)~[440,360) 诱导的十字带（整列 Δx=240 + 整行 Δy=240），
#    保留四角并坍缩对接 → 输出 (W−240)×(H−240)。
"${IDC}" erase --input "${IMG}" --rect 200,120,440,360 --merge collapse --output "${OUT}/erase_rect_collapse.png"

# 2) 横带剔除：删除 y∈[120,360) 的整条横带（Δy=240）→ 输出 全宽×(H−240)。
"${IDC}" erase --input "${IMG}" --hband 120,360 --merge collapse --output "${OUT}/erase_hband_collapse.png"

# 3) 竖带剔除：删除 x∈[200,440) 的整条竖带（Δx=240）→ 输出 (W−240)×全高。
"${IDC}" erase --input "${IMG}" --vband 200,440 --merge collapse --output "${OUT}/erase_vband_collapse.png"

# 4) 多矩形并集剔除：--rect 多次 → 各矩形诱导十字带，删除区为并集（恒为整行/整列，故可坍缩）。
"${IDC}" erase --input "${IMG}" \
  --rect 80,60,160,140 --rect 420,300,520,400 \
  --merge collapse --output "${OUT}/erase_multirect_collapse.png"

echo "完成：4 张坍缩合并结果已写入 ${OUT}"
