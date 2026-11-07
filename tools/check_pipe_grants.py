#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check that the pipe grants of a desired state match its wiring.

usage: check_pipe_grants.py SPEC

The broker serves each peer its arguments name, and the display each client
its arguments name. Every pair of pipes needs a grant on both ends, and an
engine without ambient pipes gives a wapp no pipe its grant omits.
"""
import json
import sys

USAGE = "usage: check_pipe_grants.py SPEC"
BROKER = "tg-broker"
DISPLAY = "tg-display"
DISPLAY_CLIENTS = ["rest", "hass"]
MAX_PIPES = 8


def grant(wapp):
    """The (direction, name) clauses of a wapp's merged `pipe` entries."""
    clauses = set()
    for d in (wapp.get("policy") or {}).get("Drivers") or []:
        if d.get("Name") != "pipe":
            continue
        for clause in d.get("Options", "").split(","):
            direction, _, name = clause.partition("=")
            clauses.add((direction, name))
    return clauses


def env(wapp, key):
    for e in wapp.get("envs") or []:
        k, _, v = e.partition("=")
        if k == key:
            return v
    return None


def short(wapp):
    return wapp["name"][3:] if wapp["name"].startswith("tg-") else wapp["name"]


def check_pair(found, server, client, req, rsp, what):
    """`server` reads `req` and writes `rsp`; `client` does the opposite."""
    for w, need in ((server, {("read", req), ("write", rsp)}),
                    (client, {("write", req), ("read", rsp)})):
        for direction, name in sorted(need - grant(w)):
            found.append(f"{w['name']}: no `{direction}={name}` for {what}")


def problems(spec):
    """The mismatches found in a parsed desired state."""
    found = []
    wapps = {w["name"]: w for w in spec.get("wapps", [])}
    broker = wapps.get(BROKER)
    display = wapps.get(DISPLAY)
    expected = {name: set() for name in wapps}

    if broker is not None:
        others = [w for n, w in wapps.items() if n != BROKER]
        for arg in broker.get("args") or []:
            peer = arg.partition(":")[0]
            match = [w for w in others
                     if (env(w, "TELEGRAPH_PEER") or short(w)) == peer]
            if not match:
                found.append(f"{BROKER}: peer `{peer}` has no wapp")
                continue
            req, rsp = f"tg-{peer}-req", f"tg-{peer}-rsp"
            check_pair(found, broker, match[0], req, rsp, f"peer `{peer}`")
            expected[BROKER] |= {("read", req), ("write", rsp)}
            expected[match[0]["name"]] |= {("write", req), ("read", rsp)}

    if display is not None:
        others = [w for n, w in wapps.items() if n not in (BROKER, DISPLAY)]
        for client in display.get("args") or DISPLAY_CLIENTS:
            match = [w for w in others
                     if (env(w, "TELEGRAPH_CLIENT") or short(w)) == client]
            if not match:
                found.append(f"{DISPLAY}: client `{client}` has no wapp")
                continue
            req, rsp = f"tgd-{client}-req", f"tgd-{client}-rsp"
            check_pair(found, display, match[0], req, rsp, f"client `{client}`")
            expected[DISPLAY] |= {("read", req), ("write", rsp)}
            expected[match[0]["name"]] |= {("write", req), ("read", rsp)}

    for name, w in wapps.items():
        g = grant(w)
        for direction, pipe in sorted(g - expected[name]):
            found.append(f"{name}: `{direction}={pipe}` matches no argument")
        if len({pipe for _, pipe in g}) > MAX_PIPES:
            found.append(f"{name}: the grant names over {MAX_PIPES} pipes")
    return found


def main():
    if len(sys.argv) != 2:
        sys.exit(USAGE)
    with open(sys.argv[1]) as f:
        spec = json.load(f)
    found = problems(spec)
    for p in found:
        print(f"{sys.argv[1]}: {p}", file=sys.stderr)
    sys.exit(1 if found else 0)


if __name__ == "__main__":
    main()
