#!/usr/bin/env bash
# ============================================================
#  pull.sh —— 一键拉取主仓库与 Common 子模块（双仓库最新代码）
#
#  用法：
#    ./pull.sh                # 默认：主仓库 rebase 拉取 + Common 拉取 main 最新
#    ./pull.sh --no-rebase    # 主仓库 / Common 均 merge 拉取
#    ./pull.sh --ff-only      # 主仓库 / Common 均仅 fast-forward
#    ./pull.sh --keep-proxy   # 保留 HTTP_PROXY/HTTPS_PROXY 环境变量
#
#  流程：
#    1. fetch + pull 主仓库（RPG_Server）
#    2. submodule sync + update --init（HTTPS 失败则 Common 改 SSH 重试）
#    3. fetch + pull Common（RPG_Common，跟踪 .gitmodules branch，默认 main）
#    4. 校验 Common/*.proto；缺失 Protobuf/ 时确保 protoc 并 gen_proto.sh
#    5. 缺失 TLS 证书时自动 gen_tls_certs.sh（证书不入库，避免 pull 后工作区脏）
#
#  说明：
#    - 默认 unset 本地代理（避免失效代理导致 GitHub HTTPS 失败）
#    - HTTPS / SSH 互为回退：当前协议失败时自动切换 origin 重试（主仓库与 Common）
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

try_switch_https() {
    local repo="$1"
    local https_url="$2"
    local ssh_url="$3"
    local label="$4"

    local url
    url="$(git -C "${repo}" remote get-url origin 2>/dev/null || true)"
    [[ "${url}" == "${ssh_url}" ]] || return 1
    warn "${label} SSH 拉取失败，尝试切换为 HTTPS..."
    git -C "${repo}" remote set-url origin "${https_url}"
    return 0
}

print_github_auth_hint() {
    warn "GitHub 认证失败。可选方案："
    warn "  1) SSH：将公钥添加到 https://github.com/settings/keys"
    if [[ -f "${HOME}/.ssh/id_ed25519.pub" ]]; then
        warn "     公钥：$(cat "${HOME}/.ssh/id_ed25519.pub")"
    elif [[ -f "${HOME}/.ssh/id_rsa.pub" ]]; then
        warn "     公钥：$(cat "${HOME}/.ssh/id_rsa.pub")"
    else
        warn "     生成：ssh-keygen -t ed25519 -C \"你的邮箱\" -f ~/.ssh/id_ed25519"
    fi
    warn "     验证：ssh -T git@github.com"
    warn "  2) HTTPS：配置 Personal Access Token 后 git credential 存储凭据"
}

git_pull_repo() {
    local repo="$1"
    local branch="$2"
    local label="$3"

    info "拉取 ${label}：origin ${branch}（模式=${PULL_MODE}）"
    git -C "${repo}" fetch --all --prune
    case "${PULL_MODE}" in
        rebase)
            git -C "${repo}" pull --rebase --autostash origin "${branch}"
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
    if try_switch_https "${repo}" "${https_url}" "${ssh_url}" "${label}"; then
        git_pull_repo "${repo}" "${branch}" "${label}" && return 0
    fi
    if try_switch_ssh "${repo}" "${https_url}" "${ssh_url}" "${label}"; then
        git_pull_repo "${repo}" "${branch}" "${label}" && return 0
    fi
    print_github_auth_hint
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

switch_common_submodule_to_https() {
    git config submodule.Common.url "${COMMON_HTTPS}"
    git submodule sync Common
    if common_repo_ready; then
        git -C Common remote set-url origin "${COMMON_HTTPS}" 2>/dev/null || true
    fi
}

switch_common_submodule_to_ssh() {
    git config submodule.Common.url "${COMMON_SSH}"
    git submodule sync Common
    if common_repo_ready; then
        git -C Common remote set-url origin "${COMMON_SSH}" 2>/dev/null || true
    fi
}

init_common_submodule() {
    info "同步并初始化 Common 子模块..."
    git submodule sync --recursive
    if git submodule update --init --recursive; then
        return 0
    fi
    warn "Common 子模块 HTTPS 初始化失败，尝试 SSH..."
    switch_common_submodule_to_ssh
    if git submodule update --init --recursive; then
        return 0
    fi
    warn "Common 子模块 SSH 初始化失败，尝试切回 HTTPS..."
    switch_common_submodule_to_https
    git submodule update --init --recursive
}

ensure_common_on_branch() {
    common_repo_ready || err "Common 子模块未初始化，请执行：git submodule update --init --recursive"

    local branch
    branch="$(common_branch)"

    if ! git -C Common symbolic-ref -q HEAD &>/dev/null; then
        info "Common 处于 detached HEAD，切换到 ${branch}..."
        git -C Common fetch --all --prune 2>/dev/null || true
        if git -C Common show-ref --verify --quiet "refs/remotes/origin/${branch}"; then
            git -C Common checkout -B "${branch}" "origin/${branch}"
        else
            git -C Common checkout -B "${branch}"
        fi
    fi
}

pull_common_latest() {
    ensure_common_on_branch

    local branch
    branch="$(git -C Common branch --show-current)"
    [[ -n "${branch}" ]] || branch="$(common_branch)"

    pull_with_ssh_fallback "Common" "${branch}" "Common (RPG_Common)" \
        "${COMMON_HTTPS}" "${COMMON_SSH}" \
        || err "Common 拉取失败。请检查网络、SSH（git@github.com）与分支 ${branch}"
}

ensure_protoc() {
    local protoc="${SCRIPT_DIR}/3Party/protobuf/bin/protoc"
    if [[ -x "${protoc}" ]] || command -v protoc >/dev/null 2>&1; then
        return 0
    fi
    warn "protoc 未找到，正在构建 3Party protobuf..."
    chmod +x "${SCRIPT_DIR}/3Party/build_protobuf.sh" 2>/dev/null || true
    "${SCRIPT_DIR}/3Party/build_protobuf.sh" \
        || err "protoc 构建失败。请安装 protobuf 开发包，或执行: ./3Party/fetch_vendor.sh && ./3Party/build_protobuf.sh"
}

ensure_protobuf_generated() {
    if [[ -f Protobuf/LoginMsg.pb.h ]]; then
        return 0
    fi
    if [[ ! -x "${SCRIPT_DIR}/scripts/gen_proto.sh" ]]; then
        err "Protobuf/ 缺失且 scripts/gen_proto.sh 不可用"
    fi
    ensure_protoc
    warn "Protobuf/ 生成物缺失，正在运行 ./scripts/gen_proto.sh ..."
    chmod +x "${SCRIPT_DIR}/scripts/gen_proto.sh" 2>/dev/null || true
    "${SCRIPT_DIR}/scripts/gen_proto.sh" || err "gen_proto.sh 失败"
}

ensure_tls_certs() {
    if [[ -f config/tls/server.key && -f config/tls/server.crt && -f config/tls/ca.crt ]]; then
        return 0
    fi
    if [[ ! -x "${SCRIPT_DIR}/scripts/gen_tls_certs.sh" ]]; then
        warn "TLS 证书缺失且 scripts/gen_tls_certs.sh 不可用，启动前请手动生成"
        return 0
    fi
    info "TLS 证书缺失，正在运行 ./scripts/gen_tls_certs.sh ..."
    chmod +x "${SCRIPT_DIR}/scripts/gen_tls_certs.sh" 2>/dev/null || true
    "${SCRIPT_DIR}/scripts/gen_tls_certs.sh" \
        || warn "gen_tls_certs.sh 失败，RunServer 可能因 TLS 无法启动"
}

verify_common_protocol() {
    if [[ ! -f Common/ClientCommon.proto ]] || [[ ! -f Common/LoginMsg.proto ]]; then
        err "Common 协议不完整。请执行：git submodule update --init --recursive"
    fi
    ensure_protobuf_generated
}

branch="$(git branch --show-current)"
[[ -n "${branch}" ]] || err "无法确定当前分支（detached HEAD？）"

info "===== 拉取主仓库 (RPG_Server) ====="
pull_with_ssh_fallback "." "${branch}" "主仓库 (RPG_Server)" \
    "${MAIN_HTTPS}" "${MAIN_SSH}" \
    || err "主仓库拉取失败。请检查网络与 origin 配置"
ok "主仓库已更新"

info "===== 同步并拉取 Common (RPG_Common) ====="
init_common_submodule
pull_common_latest
ok "Common 已更新"

verify_common_protocol

ensure_tls_certs

recorded_common="$(git ls-tree HEAD Common 2>/dev/null | awk '{print $3}' || true)"
actual_common="$(git -C Common rev-parse HEAD 2>/dev/null || true)"
if [[ -n "${recorded_common}" && -n "${actual_common}" && "${recorded_common}" != "${actual_common}" ]]; then
    warn "Common 当前 ${actual_common:0:7} 领先主仓库记录的子模块指针 ${recorded_common:0:7}（正常；开发中可忽略，提交前请 ./push.sh）"
fi

main_sha="$(git rev-parse --short HEAD)"
common_sha="$(git -C Common rev-parse --short HEAD)"
ok "完成。RPG_Server @ ${main_sha}，Common @ ${common_sha}"
