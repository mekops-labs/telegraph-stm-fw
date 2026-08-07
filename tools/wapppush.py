#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Push the wapp images to an OCI registry.

The control plane hands a device an image reference, and the engine of that
device stores what the layers hold. One wapp is one layer: the gzip of the
ustar that `make wapp-images` builds, which carries app.wasm and every data
file of that wapp.

Usage:
  tools/wapppush.py <registry-host:port> <image.wapp>...

Note: the transport is plain HTTP. A registry of a LAN needs no more, and a
registry that takes TLS needs a client this script does not try to be.
"""
import gzip
import hashlib
import json
import os
import re
import sys
import urllib.error
import urllib.request

# The filename of an image carries its identity: <name>@<version>-<package>.
NAME_RE = re.compile(r"^(?P<name>[^@]+)@(?P<version>[^-]+)-(?P<package>\d+)\.wapp$")

MANIFEST_TYPE = "application/vnd.oci.image.manifest.v1+json"
CONFIG_TYPE = "application/vnd.oci.image.config.v1+json"
LAYER_TYPE = "application/vnd.oci.image.layer.v1.tar+gzip"


def digest(data):
    return "sha256:" + hashlib.sha256(data).hexdigest()


def request(method, url, data=None, headers=None):
    req = urllib.request.Request(url, data=data, method=method)
    for key, value in (headers or {}).items():
        req.add_header(key, value)
    return urllib.request.urlopen(req, timeout=30)


def blob_exists(base, repo, dgst):
    try:
        request("HEAD", "%s/v2/%s/blobs/%s" % (base, repo, dgst))
        return True
    except urllib.error.HTTPError as err:
        if err.code == 404:
            return False
        raise


def push_blob(base, repo, data):
    dgst = digest(data)
    if blob_exists(base, repo, dgst):
        return dgst

    start = request("POST", "%s/v2/%s/blobs/uploads/" % (base, repo), data=b"")
    location = start.headers["Location"]
    if location.startswith("/"):
        location = base + location

    sep = "&" if "?" in location else "?"
    request("PUT", "%s%sdigest=%s" % (location, sep, dgst), data=data,
            headers={"Content-Type": "application/octet-stream"})
    return dgst


def push(base, path):
    match = NAME_RE.match(os.path.basename(path))
    if match is None:
        sys.exit("%s: the filename is not <name>@<version>-<package>.wapp" % path)

    name = match.group("name")
    version = match.group("version")
    repo = name

    with open(path, "rb") as handle:
        layer = gzip.compress(handle.read(), mtime=0)

    layer_digest = push_blob(base, repo, layer)

    config = json.dumps({
        "architecture": "wasm",
        "os": "wanted",
        "rootfs": {"type": "layers", "diff_ids": [layer_digest]},
    }, separators=(",", ":")).encode()
    config_digest = push_blob(base, repo, config)

    manifest = json.dumps({
        "schemaVersion": 2,
        "mediaType": MANIFEST_TYPE,
        "config": {"mediaType": CONFIG_TYPE, "digest": config_digest,
                   "size": len(config)},
        "layers": [{"mediaType": LAYER_TYPE, "digest": layer_digest,
                    "size": len(layer)}],
    }, separators=(",", ":")).encode()

    request("PUT", "%s/v2/%s/manifests/%s" % (base, repo, version),
            data=manifest, headers={"Content-Type": MANIFEST_TYPE})

    host = base.split("//", 1)[1]
    print("%s:%s  layer %s (%d bytes)" % (host + "/" + repo, version,
                                          layer_digest[:19], len(layer)))
    return "%s/%s:%s" % (host, repo, version)


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)

    base = argv[1]
    if not base.startswith("http"):
        base = "http://" + base

    for path in argv[2:]:
        push(base, path)


if __name__ == "__main__":
    main(sys.argv)
