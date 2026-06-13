#!/usr/bin/env bash
# =============================================================================
#  fetch_vendor.sh — 维护者：从网络下载/更新 3Party/vendor 源码 tar.gz
#
#  用法：
#    ./3Party/fetch_vendor.sh
#    ./3Party/fetch_vendor.sh --force   # 强制重新下载
#
#  升级版本：先改 versions.env，再运行本脚本，最后 git add 3Party/vendor/ 并提交。
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARGS=("--fetch")
[[ "${1:-}" == "--force" ]] && ARGS=(--fetch --force)

exec "${SCRIPT_DIR}/download_and_build.sh" "${ARGS[@]}"
