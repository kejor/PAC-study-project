#!/usr/bin/env python3
"""
run_tests.py -- PAC vulnerability suite harness (ADR-2026-05-30-005).

Drives each test binary through the same host-aware runner used by the
baselines (Docker linux/arm64 on macOS, direct exec on Linux), captures
stdout and exit code, and classifies the result against
harness/expectations.yml. Writes a one-line PASS/FAIL/ERROR summary per
case and a machine-readable build/results.json. Exits 0 only if every
case classified as PASS.

Stdlib only: a tiny YAML parser covers our flat schema so the suite does
not need PyYAML on the host.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = ROOT / "build"
EXPECTATIONS_PATH = ROOT / "harness" / "expectations.yml"
RESULTS_PATH = BUILD_DIR / "results.json"

VARIANTS = ("nopac", "pac")


# --------------------------- minimal YAML parser ---------------------------
# Supports exactly the shape used in expectations.yml:
#   top-level: name: <mapping>
#   nested:    name: { key: value, key: [list], ... }
# No multiline, no anchors, no quoted-key shenanigans. Throws on anything
# unexpected so a typo in the file doesn't silently pass through.

_INLINE_MAP_RE = re.compile(r"^\s*(\w+):\s*\{(.*)\}\s*$")
_TOP_KEY_RE    = re.compile(r"^([A-Za-z0-9_]+):\s*$")


def _parse_scalar(tok: str):
    tok = tok.strip()
    if tok.startswith("[") and tok.endswith("]"):
        body = tok[1:-1].strip()
        if not body:
            return []
        return [_parse_scalar(p) for p in body.split(",")]
    if tok.startswith('"') and tok.endswith('"'):
        return tok[1:-1]
    if tok.startswith("'") and tok.endswith("'"):
        return tok[1:-1]
    if re.fullmatch(r"-?\d+", tok):
        return int(tok)
    return tok


def _parse_inline_mapping(body: str) -> dict:
    """Parse `key: value, key: value` allowing values to be [..] lists."""
    result = {}
    i = 0
    depth = 0
    field_start = 0
    fields = []
    while i < len(body):
        c = body[i]
        if c == "[":
            depth += 1
        elif c == "]":
            depth -= 1
        elif c == "," and depth == 0:
            fields.append(body[field_start:i])
            field_start = i + 1
        i += 1
    fields.append(body[field_start:])
    for f in fields:
        if ":" not in f:
            raise ValueError(f"bad field {f!r}")
        k, _, v = f.partition(":")
        result[k.strip()] = _parse_scalar(v)
    return result


def load_expectations(path: Path) -> dict:
    out: dict = {}
    cur_top: str | None = None
    for raw in path.read_text().splitlines():
        line = raw.split("#", 1)[0].rstrip()
        if not line.strip():
            continue
        m_inline = _INLINE_MAP_RE.match(line)
        if m_inline and cur_top:
            key, body = m_inline.group(1), m_inline.group(2)
            out[cur_top][key] = _parse_inline_mapping(body)
            continue
        m_top = _TOP_KEY_RE.match(line)
        if m_top:
            cur_top = m_top.group(1)
            out[cur_top] = {}
            continue
        raise ValueError(f"unparseable line: {raw!r}")
    return out


# --------------------------- runner / classifier ---------------------------

def build_command(variant: str, test: str) -> list[str]:
    """Construct the runner command for a single (variant, test) pair."""
    docker_run = os.environ.get("DOCKER_RUN", "").strip()
    bin_in_container = f"/build/{variant}/{test}"
    bin_on_host = str(BUILD_DIR / variant / test)
    if docker_run:
        return shlex.split(docker_run) + [bin_in_container]
    return [bin_on_host]


def run_one(variant: str, test: str) -> dict:
    cmd = build_command(variant, test)
    try:
        proc = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=30,
        )
    except FileNotFoundError as e:
        return {"variant": variant, "test": test, "status": "ERROR",
                "reason": f"runner not found: {e}", "cmd": cmd}
    except subprocess.TimeoutExpired:
        return {"variant": variant, "test": test, "status": "ERROR",
                "reason": "timeout after 30s", "cmd": cmd}
    return {
        "variant": variant,
        "test": test,
        "cmd": cmd,
        "exit": proc.returncode,
        "stdout": proc.stdout.decode("utf-8", errors="replace"),
        "stderr": proc.stderr.decode("utf-8", errors="replace"),
    }


def classify(result: dict, rule: dict) -> tuple[str, str]:
    """Return (status, reason) where status in {PASS, FAIL, ERROR}."""
    if result.get("status") == "ERROR":
        return "ERROR", result.get("reason", "unknown")

    stdout = result["stdout"]
    stderr = result["stderr"]
    exit_code = result["exit"]

    # UNEXPECTED:* in either stream means broken test setup, regardless of
    # what the expectation rule says.
    for stream_name, stream in (("stdout", stdout), ("stderr", stderr)):
        if "UNEXPECTED:" in stream:
            first = next(
                (ln for ln in stream.splitlines() if "UNEXPECTED:" in ln),
                "")
            return "ERROR", f"UNEXPECTED in {stream_name}: {first.strip()}"

    accepted_exits = rule.get("exit", [])
    if accepted_exits and exit_code not in accepted_exits:
        return ("FAIL",
                f"exit {exit_code} not in accepted {accepted_exits}")

    if "stdout_contains" in rule:
        needle = rule["stdout_contains"]
        if needle not in stdout:
            return "FAIL", f"stdout missing {needle!r}"

    if "stdout_excludes" in rule:
        needle = rule["stdout_excludes"]
        if needle in stdout:
            return "FAIL", f"stdout unexpectedly contains {needle!r}"

    return "PASS", "ok"


# --------------------------------- main -----------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--all", action="store_true",
                   help="run every test in expectations.yml")
    g.add_argument("--test", metavar="NAME",
                   help="run only this test (e.g. t01_stack_bof_ret)")
    args = ap.parse_args()

    expectations = load_expectations(EXPECTATIONS_PATH)
    if args.test:
        if args.test not in expectations:
            print(f"unknown test: {args.test}", file=sys.stderr)
            return 2
        tests = [args.test]
    else:
        tests = list(expectations.keys())

    BUILD_DIR.mkdir(exist_ok=True)
    summary = []
    all_pass = True
    for t in tests:
        for v in VARIANTS:
            rule = expectations[t].get(v)
            if not rule:
                continue
            raw = run_one(v, t)
            status, reason = classify(raw, rule)
            label = f"{t}[{v}]"
            print(f"  {status:5}  {label:48}  {reason}")
            entry = {
                "test": t, "variant": v, "status": status, "reason": reason,
                "exit": raw.get("exit"),
                "stdout_tail": raw.get("stdout", "")[-400:],
                "stderr_tail": raw.get("stderr", "")[-400:],
            }
            summary.append(entry)
            if status != "PASS":
                all_pass = False

    RESULTS_PATH.write_text(json.dumps(summary, indent=2))
    print(f"\nResults written to {RESULTS_PATH}")
    if all_pass:
        print("ALL PASS")
        return 0
    print("FAILURES PRESENT")
    return 1


if __name__ == "__main__":
    sys.exit(main())
