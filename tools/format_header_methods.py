#!/usr/bin/env python3
"""Insert blank lines between adjacent method declarations in C++ .h files."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER_DIRS = [
    ROOT / "sdk",
    ROOT / "common",
    ROOT / "protocal",
    ROOT / "SuperServer",
    ROOT / "SessionServer",
    ROOT / "RecordServer",
    ROOT / "AOIServer",
    ROOT / "SceneServer",
    ROOT / "GatewayServer",
    ROOT / "LoggerServer",
    ROOT / "GlobalServer",
    ROOT / "ZoneServer",
]

ACCESS_RE = re.compile(r"^\s*(public|protected|private)\s*:\s*(//.*)?$")
NESTED_TYPE_RE = re.compile(
    r"^\s*(class|struct|enum\s+class|enum|union)\s+[\w:<>,\s&]+\{?\s*$"
)
METHOD_HINT_RE = re.compile(
    r"\b(virtual|static|explicit|friend|operator\b|~\w+\s*\()"
)
USING_RE = re.compile(r"^\s*using\s+\w+\s*=")
CLASS_START_RE = re.compile(r"^\s*(class|struct)\b")
FORWARD_DECL_RE = re.compile(r"^\s*(class|struct)\b[^;]*;\s*(//.*)?$")


def strip_trailing_ws(line: str) -> str:
    return line.rstrip()


def is_access_spec(line: str) -> bool:
    return bool(ACCESS_RE.match(line))


def is_blank(line: str) -> bool:
    return line.strip() == ""


def code_part(line: str) -> str:
    s = line.split("//", 1)[0].strip()
    if "/**<" in s:
        s = s.split("/**<", 1)[0].strip()
    return s


def count_brace_delta(line: str) -> int:
    delta = 0
    in_str = False
    in_char = False
    i = 0
    while i < len(line):
        c = line[i]
        if in_str:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_str = False
            i += 1
            continue
        if in_char:
            if c == "\\":
                i += 2
                continue
            if c == "'":
                in_char = False
            i += 1
            continue
        if c == '"':
            in_str = True
            i += 1
            continue
        if c == "'":
            in_char = True
            i += 1
            continue
        if c == "{":
            delta += 1
        elif c == "}":
            delta -= 1
        i += 1
    return delta


def member_ends_at(depth: int, cp: str) -> bool:
    if depth > 0:
        return False
    stripped = cp.rstrip()
    return stripped.endswith(";") or stripped.endswith("}")


def is_method_member(lines: list[str]) -> bool:
    non_empty = [l for l in lines if not is_blank(l)]
    if not non_empty:
        return False
    if all(is_access_spec(l) for l in non_empty):
        return False
    if any(l.strip().startswith("friend ") for l in non_empty):
        return False
    joined = "\n".join(
        code_part(l) for l in non_empty if not l.strip().startswith("/**")
    )
    if USING_RE.match(non_empty[-1]):
        return False
    if any(NESTED_TYPE_RE.match(l) for l in non_empty):
        return False
    if METHOD_HINT_RE.search(joined):
        return True
    if re.search(
        r"\([^;]*\)\s*(const\s*)?(override\s*)?(=\s*default\s*)?(=\s*delete\s*)?[;{]",
        joined,
    ):
        return True
    return False


def split_access_groups(member: list[str]) -> list[list[str]]:
    groups: list[list[str]] = []
    chunk: list[str] = []
    for line in member:
        if is_access_spec(line):
            if chunk:
                groups.append(chunk)
            chunk = [line]
        else:
            if chunk and all(is_access_spec(l) for l in chunk):
                groups.append(chunk)
                chunk = [line]
            else:
                chunk.append(line)
    if chunk:
        groups.append(chunk)
    return groups


def format_class_body(lines: list[str]) -> list[str]:
    members: list[list[str]] = []
    current: list[str] = []
    depth = 0
    in_doxy = False

    for line in lines:
        stripped = line.strip()
        if stripped.startswith("/**"):
            in_doxy = True
        if in_doxy:
            current.append(line)
            if stripped.endswith("*/"):
                in_doxy = False
            continue

        if not current and is_blank(line):
            continue

        if is_access_spec(line) and not in_doxy and depth == 0:
            if current:
                members.append(current)
                current = []
            current.append(line)
            continue

        current.append(line)
        depth += count_brace_delta(line)
        cp = code_part(line)
        if member_ends_at(depth, cp):
            members.append(current)
            current = []
            depth = 0

    if current:
        members.append(current)

    out: list[str] = []
    prev_method = False
    for member in members:
        member = [strip_trailing_ws(l) for l in member]
        while member and is_blank(member[-1]):
            member.pop()
        if not member:
            continue

        for sub in split_access_groups(member):
            sub = [l for l in sub if not is_blank(l)]
            if not sub:
                continue
            only_access = all(is_access_spec(l) for l in sub)
            is_method = is_method_member(sub)

            if only_access:
                if out and not is_blank(out[-1]):
                    out.append("")
                out.extend(sub)
                prev_method = False
                continue

            if is_method and prev_method and out and not is_blank(out[-1]):
                out.append("")
            out.extend(sub)
            prev_method = is_method

    return out


def find_class_open(lines: list[str], start: int) -> int | None:
    j = start
    n = len(lines)
    while j < n:
        if "{" in lines[j].split("//")[0]:
            return j
        if ";" in lines[j].split("//")[0]:
            return None
        j += 1
    return None


def format_content(text: str) -> str:
    lines = [strip_trailing_ws(l) for l in text.splitlines()]
    result: list[str] = []
    i = 0
    n = len(lines)

    while i < n:
        line = lines[i]
        if CLASS_START_RE.match(line) and not FORWARD_DECL_RE.match(line):
            open_idx = find_class_open(lines, i)
            if open_idx is None:
                result.append(line)
                i += 1
                continue

            header = lines[i : open_idx + 1]
            depth = 0
            for h in header:
                depth += count_brace_delta(h)
            body_start = open_idx + 1
            k = body_start
            while k < n:
                depth += count_brace_delta(lines[k])
                if depth <= 0:
                    close_idx = k
                    break
                k += 1
            else:
                result.append(line)
                i += 1
                continue

            body = lines[body_start:close_idx]
            formatted_body = format_class_body(body)
            result.extend(header)
            result.extend(formatted_body)
            result.append(lines[close_idx])
            i = close_idx + 1
            continue

        result.append(line)
        i += 1

    cleaned: list[str] = []
    blank_run = 0
    for line in result:
        if is_blank(line):
            blank_run += 1
            if blank_run <= 1:
                cleaned.append("")
        else:
            blank_run = 0
            cleaned.append(line)

    while cleaned and is_blank(cleaned[-1]):
        cleaned.pop()
    return "\n".join(cleaned) + ("\n" if text.endswith("\n") else "")


def collect_headers() -> list[Path]:
    headers: list[Path] = []
    for d in HEADER_DIRS:
        if d.is_dir():
            headers.extend(sorted(d.glob("*.h")))
    return headers


def main() -> int:
    changed = 0
    for path in collect_headers():
        original = path.read_text(encoding="utf-8")
        formatted = format_content(original)
        if formatted != original:
            path.write_text(formatted, encoding="utf-8")
            print(f"formatted: {path.relative_to(ROOT)}")
            changed += 1
    print(f"done: {changed} file(s) updated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
