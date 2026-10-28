#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check that a firmware image embeds the board's launch configuration.

usage: check-fw-config.py IMAGE CONFIG

The engine embeds the launch configuration as JSON text. The image passes when
one JSON object in it with a "supervisor" key equals CONFIG, compared as parsed
JSON. Exit status 1 names what the image holds instead.
"""
import json
import sys

USAGE = "usage: check-fw-config.py IMAGE CONFIG"


def embedded(image):
    """Every JSON object in the image that has a "supervisor" key."""
    dec = json.JSONDecoder()
    text = image.decode("latin-1")
    found = []
    pos = text.find('"supervisor"')
    while pos != -1:
        start = text.rfind("{", 0, pos)
        while start != -1:
            try:
                obj, _ = dec.raw_decode(text, start)
            except ValueError:
                obj = None
            if isinstance(obj, dict) and "supervisor" in obj:
                found.append(obj)
                break
            start = text.rfind("{", 0, start)
        pos = text.find('"supervisor"', pos + 1)
    return found


def main():
    if len(sys.argv) != 3:
        sys.exit(USAGE)
    image_path, config_path = sys.argv[1:]
    want = json.load(open(config_path))
    found = embedded(open(image_path, "rb").read())
    if want in found:
        print(f"{image_path}: embeds {config_path}")
        return 0
    print(f"{image_path}: does not embed {config_path}", file=sys.stderr)
    for obj in found:
        console = obj.get("supervisor", {}).get("params", {}).get("console")
        print(f"  it embeds a configuration with the supervisor console {json.dumps(console)}", file=sys.stderr)
    if not found:
        print("  it embeds no launch configuration that could be read", file=sys.stderr)
    return 1


sys.exit(main())
