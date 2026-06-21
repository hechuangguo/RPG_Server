#!/usr/bin/env bash
# ============================================================
#  push.sh —— 一键提交并推送主仓库与 Common 子模块（全部改动）
#
#  用法：
#    ./push.sh -m "提交说明"
#
#  流程：
#    1. Common（RPG_Common）：add -A → commit → push origin/main
#    2. 主仓库（RPG_Server）：add -A（含 Common 子模块指针）→ commit → push
#
#  说明：
#    - 仅支持 -m 参数；-m 为必填
#    - 提交并推送两个仓库中的全部改动（含未跟踪文件，受 .gitignore 约束）
#    - Common 推送成功后，主仓库会同步更新 submodule 指针并一并提交
#    - HTTPS push 失败时自动切换 SSH 重试（Common 与主仓库）
# ============================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

GREEN='\033[0;32m'
CYAN='\033[0;36m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
RESET='\033[0m'

info() { echo -e "${CYAN}[INFO]${RESET}  $*"; }
ok()   { echo -e "${GREEN}[OK]${RESET}    $*"; }
warn() { echo -e "${YELLOW}[WARN]${RESET}  $*"; }
err()  { echo -e "${RED}[ERR]${RESET}   $*" >&2; exit 1; }

MAIN_HTTPS="https://github.com/hechuangguo/RPG_Server.git"
MAIN_SSH="git@github.com:hechuangguo/RPG_Server.git"
COMMON_HTTPS="https://github.com/hechuangguo/RPG_Common.git"
COMMON_SSH="git@github.com:hechuangguo/RPG_Common.git"

COMMIT_MSG=""

if [[ $# -eq 0 ]]; then
    err "用法：./push.sh -m \"提交说明\""
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        -m)
            [[ $# -ge 2 ]] || err "缺少 -m 参数值，用法：./push.sh -m \"提交说明\""
            COMMIT_MSG="$2"
            shift 2
            ;;
        -h|--help)
            sed -n '3,16p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            err "未知参数：$1，仅支持 -m \"提交说明\""
            ;;
    esac
done

[[ -n "${COMMIT_MSG}" ]] || err "提交说明不能为空，用法：./push.sh -m \"提交说明\""

git rev-parse --is-inside-work-tree &>/dev/null || err "当前目录不是 Git 仓库：${SCRIPT_DIR}"

MAIN_BRANCH="$(git branch --show-current)"
[[ -n "${MAIN_BRANCH}" ]] || err "主仓库处于 detached HEAD，请先 checkout 分支（如 main）"

unset HTTP_PROXY HTTPS_PROXY http_proxy https_proxy ALL_PROXY all_proxy 2>/dev/null || true

common_default_branch() {
    local branch="main"
    if git config -f .gitmodules --get submodule.Common.branch &>/dev/null; then
        branch="$(git config -f .gitmodules --get submodule.Common.branch)"
    fi
    echo "${branch}"
}

repo_has_changes() {
    local repo="$1"
    ! git -C "${repo}" diff --quiet 2>/dev/null ||
    ! git -C "${repo}" diff --cached --quiet 2>/dev/null ||
    [[ -n "$(git -C "${repo}" ls-files --others --exclude-standard)" ]]
}

try_switch_ssh() {
    local repo="$1"
    local https_url="$2"
    local ssh_url="$3"
    local label="$4"

    local url
    url="$(git -C "${repo}" remote get-url origin 2>/dev/null || true)"
    [[ "${url}" == "${https_url}" ]] || return 1
    warn "${label} HTTPS push 失败，切换 SSH 重试..."
    git -C "${repo}" remote set-url origin "${ssh_url}"
    return 0
}

git_push_repo() {
    local repo="$1"
    local branch="$2"
    local label="$3"
    local https_url="$4"
    local ssh_url="$5"

    if git -C "${repo}" push origin "${branch}"; then
        return 0
    fi
    if try_switch_ssh "${repo}" "${https_url}" "${ssh_url}" "${label}"; then
        git -C "${repo}" push origin "${branch}" && return 0
    fi
    return 1
}

common_repo_ready() {
    git -C Common rev-parse --is-inside-work-tree &>/dev/null
}

commit_and_push_common() {
    if ! common_repo_ready; then
        warn "Common 子模块未初始化，跳过"
        return 0
    fi

    local branch
    branch="$(common_default_branch)"

    info "===== Common (RPG_Common) ====="

    local detached=false
    if ! git -C Common symbolic-ref -q HEAD &>/dev/null; then
        detached=true
        warn "Common 处于 detached HEAD，将先提交再合并到 ${branch}"
    fi

    git -C Common add -A
    if repo_has_changes Common; then
        git -C Common commit -m "${COMMIT_MSG}"
        ok "Common 已提交（全部改动）"
    else
        info "Common 无新改动，跳过 commit"
    fi

    if [[ "${detached}" == true ]]; then
        local tip
        tip="$(git -C Common rev-parse HEAD)"
        git -C Common checkout "${branch}" 2>/dev/null \
            || git -C Common checkout -B "${branch}" "origin/${branch}" 2>/dev/null \
            || git -C Common checkout -B "${branch}"
        if ! git -C Common merge-base --is-ancestor "${tip}" HEAD 2>/dev/null; then
            info "合并 detached 提交到 ${branch}..."
            git -C Common merge "${tip}" -m "${COMMIT_MSG}" \
                || err "Common 合并到 ${branch} 失败，请手动解决冲突后重试"
            ok "Common 已合并到 ${branch}"
        fi
    fi

    branch="$(git -C Common branch --show-current)"
    [[ -n "${branch}" ]] || err "Common 不在任何分支上，请手动 checkout ${branch}"

    local ahead
    ahead="$(git -C Common rev-list --count "origin/${branch}..HEAD" 2>/dev/null || echo 0)"
    if [[ "${ahead}" -gt 0 ]]; then
        info "推送 Common → origin/${branch}（${ahead} 个 commit）"
        git_push_repo Common "${branch}" "Common" "${COMMON_HTTPS}" "${COMMON_SSH}" \
            || err "Common push 失败，请先 ./pull.sh 解决冲突后再试"
        ok "Common 已推送"
    else
        info "Common 已与远程同步"
    fi
}

commit_and_push_main() {
    info "===== 主仓库 (RPG_Server) ====="

    # Common 推送后需同步子模块指针
    git add -A
    if repo_has_changes .; then
        git commit -m "${COMMIT_MSG}"
        ok "主仓库已提交（含 Common 子模块指针与其它全部改动）"
    else
        info "主仓库无新改动，跳过 commit"
    fi

    local ahead
    ahead="$(git rev-list --count "origin/${MAIN_BRANCH}..HEAD" 2>/dev/null || echo 0)"
    if [[ "${ahead}" -gt 0 ]]; then
        info "推送主仓库 → origin/${MAIN_BRANCH}（${ahead} 个 commit）"
        git_push_repo "." "${MAIN_BRANCH}" "主仓库" "${MAIN_HTTPS}" "${MAIN_SSH}" \
            || err "主仓库 push 失败，请先 ./pull.sh 解决冲突后再试"
        ok "主仓库已推送"
    else
        info "主仓库已与远程同步"
    fi
}

info "提交说明：${COMMIT_MSG}"
commit_and_push_common
commit_and_push_main

common_sha="$(git -C Common rev-parse --short HEAD 2>/dev/null || echo "-")"
ok "完成。RPG_Server @ $(git rev-parse --short HEAD)，Common @ ${common_sha}"
