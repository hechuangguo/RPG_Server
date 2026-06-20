#!/usr/bin/env bash
# gen_password_digest.sh — 生成 SHA-256 摘要与 bcrypt(hex(digest)) 供 GameUser 种子/迁移
#
# 用法：./scripts/gen_password_digest.sh [明文密码...]
# 默认：123456 test1234

set -euo pipefail

if ! command -v python3 >/dev/null 2>&1; then
    echo "ERR: python3 required" >&2
    exit 1
fi

if [[ "$#" -eq 0 ]]; then
    set -- 123456 test1234
fi

python3 - "$@" << 'PY'
import hashlib
import os
import re
import subprocess
import sys

try:
    import crypt
except ImportError:
    crypt = None

HEX_DIGEST_RE = re.compile(r"^[0-9a-f]{64}$")

def bcrypt_hex_crypt(hex_str: str) -> str:
    if crypt is None:
        raise RuntimeError("python3 crypt module unavailable")
    if not HEX_DIGEST_RE.fullmatch(hex_str):
        raise ValueError("digest hex must be 64 lowercase hex chars")
    salt = crypt.mksalt(crypt.METHOD_BLOWFISH)
    return crypt.crypt(hex_str, salt)

def bcrypt_hex_php(hex_str: str) -> str:
    if not HEX_DIGEST_RE.fullmatch(hex_str):
        raise ValueError("digest hex must be 64 lowercase hex chars")
    env = os.environ.copy()
    env["DIGEST_HEX"] = hex_str
    return subprocess.check_output(
        [
            "php",
            "-r",
            'echo password_hash(getenv("DIGEST_HEX"), PASSWORD_BCRYPT, ["cost"=>12]);',
        ],
        text=True,
        env=env,
    ).strip()

def bcrypt_hex(hex_str: str) -> str:
    if crypt is not None:
        try:
            return bcrypt_hex_crypt(hex_str)
        except (OSError, ValueError):
            pass
    return bcrypt_hex_php(hex_str)

for pwd in sys.argv[1:]:
    digest = hashlib.sha256(pwd.encode("utf-8")).digest()
    hex_d = digest.hex()
    bhash = bcrypt_hex(hex_d)
    print(f"password={pwd}")
    print(f"  sha256_hex={hex_d}")
    print(f"  bcrypt={bhash}")
    print()
PY
