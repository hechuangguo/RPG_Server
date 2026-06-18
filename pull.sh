#!/usr/bin/env bash
# ============================================================
#  pull.sh —— 一键拉取主仓库与 Common 子模块
#
#  用法：
#    ./pull.sh              # git pull --rebase + submodule update（默认）
#    ./pull.sh --no-rebase    # git pull（merge）
#    ./pull.sh --ff-only      # git pull --ff-only
#    ./pull.sh --keep-proxy   # 保留 HTTP_PROXY/HTTPS_PROXY 环境变量
#
#  流程：
#    1. 拉取 RPG_Server 主仓库
#    2. git submodule sync + update --init --recursive（Common → RPG_Common）
#    3. 校验 Common/ClientTypes.h 与 LoginMsg.h 存在
#
#  说明：
#    - 默认 unset 本地代理（避免失效的 127.0.0.1:40387 导致 GitHub HTTPS 失败）
#    - Common submodule HTTPS 失败时自动尝试切换为 SSH 并重试一次
# ============================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
RED='\033[0;31m'
RESET='\033[0m'

info()  { echo -e "${CYAN}[INFO]${RESET}  $*"; }
ok()    { echo -e "${GREEN}[OK]${RESET}    $*"; }
warn()  { echo -e "${YELLOW}[WARN]${RESET}  $*"; }
err()   { echo -e "${RED}[ERR]${RESET}   $*" >&2; exit 1; }

PULL_MODE="rebase"
KEEP_PROXY=false

for arg in "$@"; do
    case "${arg}" in
        --no-rebase)  PULL_MODE="merge" ;;
        --ff-only)    PULL_MODE="ff-only" ;;
        --keep-proxy) KEEP_PROXY=true ;;
        -h|--help)
            sed -n '3,18p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            err "未知参数：${arg}（可用 --no-rebase / --ff-only / --keep-proxy）"
            ;;
    esac
done

if ! git rev-parse --is-inside-work-tree &>/dev/null; then
    err "当前目录不是 Git 仓库：${SCRIPT_DIR}"
fi

if [[ "${KEEP_PROXY}" == false ]]; then
    unset HTTP_PROXY HTTPS_PROXY http_proxy https_proxy ALL_PROXY all_proxy || true
    info "已清除本地 HTTP(S) 代理环境变量（可用 --keep-proxy 保留）"
fi

branch="$(git branch --show-current)"
if [[ -z "${branch}" ]]; then
    err "无法确定当前分支（detached HEAD？）"
fi

info "拉取主仓库：origin ${branch}（模式=${PULL_MODE}）"
case "${PULL_MODE}" in
    rebase)
        git pull --rebase origin "${branch}"
        ;;
    merge)
        git pull origin "${branch}"
        ;;
    ff-only)
        git pull --ff-only origin "${branch}"
        ;;
esac
ok "主仓库已更新"

update_submodules() {
    git submodule sync --recursive
    git submodule update --init --recursive
}

try_common_ssh_fallback() {
    [[ -d Common ]] || return 1
    local url
    url="$(git -C Common remote get-url origin 2>/dev/null || true)"
    [[ "${url}" == https://github.com/hechuangguo/RPG_Common.git ]] || return 1
    warn "Common submodule HTTPS 拉取失败，尝试切换为 SSH..."
    git -C Common remote set-url origin git@github.com:hechuangguo/RPG_Common.git
    return 0
}

info "同步并拉取子模块（Common/）..."
if ! update_submodules; then
    if try_common_ssh_fallback; then
        update_submodules || err "子模块拉取失败。请检查 SSH（git@github.com）与网络连接。"
    else
        err "子模块拉取失败。可尝试：git submodule update --init --recursive"
    fi
fi
ok "子模块已同步"

if [[ ! -f Common/ClientTypes.h ]] || [[ ! -f Common/LoginMsg.h ]]; then
    err "Common 协议头不完整。请执行：git submodule update --init --recursive"
fi

common_sha="$(git -C Common rev-parse --short HEAD 2>/dev/null || echo unknown)"
ok "校验通过：Common @ ${common_sha} (ClientTypes.h + LoginMsg.h)"
