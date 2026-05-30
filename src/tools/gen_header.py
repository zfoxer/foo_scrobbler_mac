#!/usr/bin/env python3
import os
import pathlib


def require_env(name: str) -> str:
    value = os.environ.get(name)
    if value is None or value == "":
        raise SystemExit(f"{name} not set")
    return value


def project_root() -> pathlib.Path:
    srcroot = os.environ.get("SRCROOT")
    if srcroot:
        return pathlib.Path(srcroot)
    return pathlib.Path(__file__).resolve().parents[1]


def cpp_string_literal(value: str) -> str:
    escaped = (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "\\r")
        .replace("\t", "\\t")
    )
    return f'"{escaped}"'


def main() -> None:
    key = require_env("LASTFM_API_KEY")
    secret = require_env("LASTFM_API_SECRET")

    content = f"""#pragma once
#include <string>

static inline std::string _key() {{
    return {cpp_string_literal(key)};
}}

static inline std::string _sec() {{
    return {cpp_string_literal(secret)};
}}
"""

    out_dir = project_root() / "generated"
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "no.generated.h").write_text(content)


if __name__ == "__main__":
    main()
