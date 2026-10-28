#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check a launch config against the settings the engine refuses.

usage: check-launch-config.py CONFIG

Engine v0.29.0 and later stop at startup when `system` names `privileged`:
the setting belongs to the parameters of the wapp that needs it. A firmware
that embeds such a config never starts its supervisor. This check covers the
refusals that have shipped; the engine is the authority on the rest.
"""
import json
import sys

USAGE = "usage: check-launch-config.py CONFIG"


def problems(config):
    """The refusals found in a parsed launch config."""
    found = []
    system = config.get("system")
    if not isinstance(system, dict):
        return ["`system` is missing or not an object"]
    if "privileged" in system:
        found.append(
            '`system.privileged` is refused; set "privileged" in '
            "`supervisor.params`"
        )
    params = config.get("supervisor", {}).get("params", {})
    if "privileged" in params and not isinstance(params["privileged"], bool):
        found.append("`supervisor.params.privileged` is not a boolean")
    return found


def main():
    if len(sys.argv) != 2:
        sys.exit(USAGE)
    path = sys.argv[1]
    with open(path) as f:
        config = json.load(f)
    found = problems(config)
    if not found:
        print(f"{path}: accepted")
        return 0
    for line in found:
        print(f"{path}: {line}", file=sys.stderr)
    return 1


sys.exit(main())
