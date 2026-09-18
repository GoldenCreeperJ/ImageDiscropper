#!/usr/bin/env bash
# ============================================================================
# 文件：examples/l1_extract.sh
# 作用：演示 L1「标准提取」模式（extract）——极性恒 keep，保留选框 / 带，坍缩输出单图。
#       覆盖三种几何：中心矩形（--rect）、水平带（--hband）、垂直带（--vband）。
# 约定（examples/README.md 约定）：使用仓库内示例图片 examples/sample.png；输出到系统临时目录，
#       不污染仓库；每一步带中文注释。
# 前置：需先在仓库根构建出可执行文件 idc（CLion 或 cmake --build）；sample.png 需自备
#       （建议尺寸 ≥ 640×480，否则脚本内坐标可能越界，见 examples/README.md）。
# ============================================================================
set -euo pipefail

# --- 定位脚本目录与仓库根（examples/ 的上一级即 CLI 目录，再上一级为仓库根）。---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# --- 定位 idc 可执行文件：优先用环境变量 IDC，其次扫描常见构建输出目录，最后回退 PATH。---
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

# --- 输入图（仓库内示例图片，硬编码路径）与临时输出目录（不污染仓库）。---
IMG="${SCRIPT_DIR}/sample.png"
OUT="$(mktemp -d)"
trap 'echo "输出目录：${OUT}"' EXIT

if [ ! -f "${IMG}" ]; then
  echo "错误：未找到示例图片 ${IMG}（请先提供，详见 examples/README.md）" >&2
  exit 3
fi

echo "== L1 标准提取（extract）=="

# 1) 保留中心矩形 [200,120)~[440,360)：输出即该矩形的坍缩单图（240×240）。
"${IDC}" extract --input "${IMG}" --rect 200,120,440,360 --output "${OUT}/extract_rect.png"

# 2) 保留水平带 y∈[120,360)：切割线贯穿全宽，输出为 全宽×240 的横带。
"${IDC}" extract --input "${IMG}" --hband 120,360 --output "${OUT}/extract_hband.png"

# 3) 保留垂直带 x∈[200,440)：切割线贯穿全高，输出为 240×全高 的竖带。
"${IDC}" extract --input "${IMG}" --vband 200,440 --output "${OUT}/extract_vband.png"

echo "完成：3 张提取结果已写入 ${OUT}"
