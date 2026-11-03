#!/usr/bin/env python3
"""Replays upstream test/command/suite cases through the real `groonga` executable and checks every
response against the case's committed .expected file.

usage: run_oracle.py <groonga> <suite-dir> <list-file>

For each case the .test script is piped to `groonga -n <fresh db>`. Each response is a JSON value
on stdout; its header (status, start time, elapsed) is normalised to the [0, 0.0, 0.0] the
.expected files use, and its compact or pretty-printed rendering must then appear in the .expected
text, in order. A case that prints fewer responses than the .expected holds, or any response that
differs, fails. Prints PASS/FAIL per case and a final `RESULT <passed> <failed>` line.
"""
import json
import os
import subprocess
import sys
import tempfile


def render(value):
    return (
        json.dumps(value, separators=(",", ":"), ensure_ascii=False),
        json.dumps(value, indent=2, ensure_ascii=False),
    )


def responses(text):
    decoder = json.JSONDecoder()
    pos, out = 0, []
    while pos < len(text):
        while pos < len(text) and text[pos].isspace():
            pos += 1
        if pos >= len(text):
            break
        value, pos = decoder.raw_decode(text, pos)
        out.append(value)
    return out


def normalise(value):
    header = value[0]
    if header[0] != 0:
        raise ValueError("non-zero status %r" % (header,))
    value[0] = [0, 0.0, 0.0]
    return value


def run_case(groonga, suite, name):
    test_path = os.path.join(suite, name + ".test")
    expected = open(os.path.join(suite, name + ".expected"), encoding="utf-8").read()
    with tempfile.TemporaryDirectory(prefix="groonga-oracle-") as tmp:
        with open(test_path, "rb") as stdin:
            proc = subprocess.run(
                [groonga, "-n", os.path.join(tmp, "db")],
                stdin=stdin, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                timeout=120,
            )
    if proc.returncode != 0:
        return "groonga exit %d" % proc.returncode
    try:
        values = [normalise(v) for v in responses(proc.stdout.decode("utf-8"))]
    except ValueError as error:
        return str(error)
    if not values:
        return "no responses"
    pos = 0
    for index, value in enumerate(values):
        compact, pretty = render(value)
        hits = [p for p in (expected.find(compact, pos), expected.find(pretty, pos)) if p >= 0]
        if not hits:
            return "response %d differs from .expected: %s" % (index + 1, compact[:160])
        pos = min(hits) + 1
    return None


def main():
    groonga, suite, listing = sys.argv[1:4]
    passed = failed = 0
    for name in open(listing, encoding="utf-8").read().split():
        try:
            problem = run_case(groonga, suite, name)
        except Exception as error:
            problem = "%s: %s" % (type(error).__name__, error)
        if problem is None:
            passed += 1
            print("PASS", name)
        else:
            failed += 1
            print("FAIL", name, "-", problem)
    print("RESULT", passed, failed)
    return 0


if __name__ == "__main__":
    sys.exit(main())
