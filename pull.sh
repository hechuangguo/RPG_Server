#!/usr/bin/env bash
# ============================================================
#  pull.sh —— 一键拉取主仓库与 Common 子模块（全部最新代码）
#
#  用法：
#    ./pull.sh                # 默认：主仓库 rebase 拉取 + Common 拉取 main 最新
#    ./pull.sh --no-rebase    # 主仓库 / Common 均 merge 拉取
#    ./pull.sh --ff-only      # 主仓库 / Common 均仅 fast-forward
#    ./pull.sh --keep-proxy   # 保留 HTTP_PROXY/HTTPS_PROXY 环境变量
#
#  流程：
#    1. fetch --all --prune（主仓库）
#    2. git pull（RPG_Server 当前分支）
#    3. submodule sync + update --init --recursive
#    4. fetch + pull Common（RPG_Common，跟踪 .gitmodules 中 branch，默认 main）
#    5. 校验 Common 协议与 proto 生成物
#
#  说明：
#    - 默认 unset 本地代理（避免失效代理导致 GitHub HTTPS 失败）
#    - HTTPS 失败时自动尝试切换为 SSH 并重试（主仓库与 Common）
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

MAIN_HTTPS="https://github.com/hechuangguo/RPG_Server.git"
MAIN_SSH="git@github.com:hechuangguo/RPG_Server.git"
COMMON_HTTPS="https://github.com/hechuangguo/RPG_Common.git"
COMMON_SSH="git@github.com:hechuangguo/RPG_Common.git"

PULL_MODE="rebase"
KEEP_PROXY=false

for arg in "$@"; do
    case "${arg}" in
        --no-rebase)  PULL_MODE="merge" ;;
        --ff-only)    PULL_MODE="ff-only" ;;
        --keep-proxy) KEEP_PROXY=true ;;
        -h|--help)
            sed -n '3,20p' "$0" | sed 's/^# \{0,1\}//'
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

try_switch_ssh() {
    local repo="$1"
    local https_url="$2"
    local ssh_url="$3"
    local label="$4"

    local url
    url="$(git -C "${repo}" remote get-url origin 2>/dev/null || true)"
    [[ "${url}" == "${https_url}" ]] || return 1
    warn "${label} HTTPS 拉取失败，尝试切换为 SSH..."
    git -C "${repo}" remote set-url origin "${ssh_url}"
    return 0
}

git_pull_repo() {
    local repo="$1"
    local branch="$2"
    local label="$3"

    info "拉取 ${label}：origin ${branch}（模式=${PULL_MODE}）"
    git -C "${repo}" fetch --all --prune
    case "${PULL_MODE}" in
        rebase)
            git -C "${repo}" pull --rebase origin "${branch}"
            ;;
        merge)
            git -C "${repo}" pull origin "${branch}"
            ;;
        ff-only)
            git -C "${repo}" pull --ff-only origin "${branch}"
            ;;
    esac
}

pull_with_ssh_fallback() {
    local repo="$1"
    local branch="$2"
    local label="$3"
    local https_url="$4"
    local ssh_url="$5"

    if git_pull_repo "${repo}" "${branch}" "${label}"; then
        return 0
    fi
    if try_switch_ssh "${repo}" "${https_url}" "${ssh_url}" "${label}"; then
        git_pull_repo "${repo}" "${branch}" "${label}"
        return 0
    fi
    return 1
}

common_branch() {
    local branch="main"
    if git config -f .gitmodules --get submodule.Common.branch &>/dev/null; then
        branch="$(git config -f .gitmodules --get submodule.Common.branch)"
    fi
    echo "${branch}"
}

common_repo_ready() {
    git -C Common rev-parse --is-inside-work-tree &>/dev/null
}

ensure_common_checked_out() {
    common_repo_ready || err "Common 子模块未初始化，请先：git submodule update --init --recursive"

    local branch
    branch="$(common_branch)"

    if ! git -C Common symbolic-ref -q HEAD &>/dev/null; then
        info "Common 处于 detached HEAD，切换到 ${branch}..."
        git -C Common fetch --all --prune
        if git -C Common show-ref --verify --quiet "refs/remotes/origin/${branch}"; then
            git -C Common checkout -B "${branch}" "origin/${branch}"
        else
            git -C Common checkout -B "${branch}"
        fi
    fi
}

update_submodules() {
    git submodule sync --recursive
    git submodule update --init --recursive
}

pull_common_latest() {
    ensure_common_checked_out

    local branch
    branch="$(git -C Common branch --show-current)"
    [[ -n "${branch}" ]] || branch="$(common_branch)"

    pull_with_ssh_fallback "Common" "${branch}" "Common (RPG_Common)" \
        "${COMMON_HTTPS}" "${COMMON_SSH}" \
        || err "Common 拉取失败。请检查网络、SSH（git@github.com）与分支 ${branch}"
}

branch="$(git branch --show-current)"
[[ -n "${branch}" ]] || err "无法确定当前分支（detached HEAD？）"

info "===== 拉取主仓库 (RPG_Server) ====="
pull_with_ssh_fallback "." "${branch}" "主仓库 (RPG_Server)" \
    "${MAIN_HTTPS}" "${MAIN_SSH}" \
    || err "主仓库拉取失败。请检查网络与 origin 配置"
ok "主仓库已更新"

info "===== 同步并拉取 Common (RPG_Common) ====="
if ! update_submodules; then
    if try_switch_ssh "Common" "${COMMON_HTTPS}" "${COMMON_SSH}" "Common"; then
        update_submodules || err "Common 子模块初始化失败"
    else
        err "Common 子模块初始化失败。可尝试：git submodule update --init --recursive"
    fi
fi
pull_common_latest
ok "Common 已更新"

if [[ ! -f Common/ClientCommon.proto ]] \
    || [[ ! -f Common/LoginMsg.proto ]] \
    || [[ ! -f Protobuf/LoginMsg.pb.h ]]; then
    err "Common 协议不完整。请执行：git submodule update --init --recursive && ./scripts/gen_proto.sh"
fi

main_sha="$(git rev-parse --short HEAD)"
common_sha="$(git -C Common rev-parse --short HEAD)"
ok "完成。RPG_Server @ ${main_sha}，Common @ ${common_sha}"
