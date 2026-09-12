#!/usr/bin/env bash
# ============================================================================
# 文件：examples/l2_erase_separate.sh
# 作用：演示 L2「反向剔除」模式（erase）+ 分离导出（缺省，--output-dir）——把保留的各块
#       分别裁剪编码后写入目标文件夹（不打包压缩），文件按命名模板 {name}_{index:03d} 生成。
#       单矩形十字切割保留四角 → 输出文件夹内 4 张图。
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

echo "== L2 反向剔除 + 分离导出（erase --output-dir）=="

# 1) 单矩形十字切割，保留四角，分离导出到文件夹（目录不存在会自动创建）。
#    --format png 决定各分离图扩展名；缺省命名模板 {name}_{index:03d}。
"${IDC}" erase --input "${IMG}" --rect 200,120,440,360 \
  --output-dir "${OUT}/corners" --format png

# 2) 自定义命名模板：以 cell_{index} 命名分离图（不含路径分隔符，见 value_parser 校验）。
"${IDC}" erase --input "${IMG}" --rect 200,120,440,360 \
  --output-dir "${OUT}/corners_named" --format png --naming "cell_{index}"

echo "完成：分离结果（四角各一张）已写入 ${OUT}/corners 与 ${OUT}/corners_named"
# 列出生成的文件，便于核对数量与命名。
ls -1 "${OUT}/corners" "${OUT}/corners_named"
