#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Give the version of the wapp images. It comes from the git tags, as the
# version of the firmware does.
#
# Note: the registry takes the name, the version and the package from the
# filename, and it separates them with "@" and "-". Thus this script writes
# every "-" of the description as ".", and it removes a leading "v".

set -eu

version=$(git describe --tags --dirty --always 2>/dev/null || echo 0.0.0)

printf '%s\n' "$version" | sed -e 's/^v//' -e 's/-/./g'
