#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Give the version of the wapp images. It comes from the git tags, as the
# version of the firmware does.
#
# Note: the registry of the engine holds 14 characters of version, thus this
# script gives the tag, the count of the commits after it, and "dirty" for a
# tree with local changes. The hash is not part of it: the digest of the image
# names the exact bytes, and the version of the STM32 firmware travels in the
# wapp that carries it.

set -eu

described=$(git describe --tags --dirty --always 2>/dev/null || echo 0.0.0)

version=$(printf '%s\n' "$described" | sed -e 's/^v//' -e 's/-g[0-9a-f]\{7,\}//')

printf '%s\n' "$version" | sed -e 's/-/./g'
