#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Write the version header of the firmware. The version comes from the git
# tags. Without a tag, it is the short commit hash. A tree with local changes
# gets the suffix "-dirty".
#
# Note: the script writes the file only when the version changes. Thus a build
# that follows an unchanged tree compiles nothing again.

set -eu

out="$1"
guard="__BOARDS_ARM_STM32_HAZK03_STM32F105RB_SRC_VERSION_H"

version=$(git describe --tags --dirty --always 2>/dev/null || echo unknown)

tmp="$out.tmp"

cat > "$tmp" <<EOF
/* SPDX-License-Identifier: Apache-2.0 */

/* The tool tools/genversion.sh writes this file. Do not edit it, and do not
 * commit it.
 */

#ifndef $guard
#define $guard

#define HAZK03_VERSION "$version"

#endif /* $guard */
EOF

if cmp -s "$tmp" "$out"; then
    rm -f "$tmp"
    exit 0
fi

mv "$tmp" "$out"

# The dependency list of NuttX comes from a separate step, thus a plain build
# does not learn that this header changed. It would keep the old version in the
# image and report no error. Thus a new version drops the objects of this
# directory, and the sources that hold the version compile again.

dir=$(dirname "$out")
rm -f "$dir"/*.o "$dir"/libboard.a
