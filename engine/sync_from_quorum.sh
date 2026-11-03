#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Bring the engine up to date with a Quorum checkout: copies its firmware sources (src, hal, gen) over engine/firmware,
# then puts back the one change the Android build needs (platform.c includes engine/android/android_platform.c when
# QUORUM_ANDROID is set) and records the Quorum commit in engine/QUORUM_COMMIT.
#   engine/sync_from_quorum.sh /path/to/Quorum
set -e
Q="${1:?usage: engine/sync_from_quorum.sh /path/to/Quorum}"
cd "$(dirname "$0")"
rm -rf firmware && mkdir firmware
cp -r "$Q/firmware/src" "$Q/firmware/hal" "$Q/firmware/gen" firmware/
rm -f firmware/hal/*.S
python3 - <<'EOF'
p = 'firmware/src/platform.c'
s = open(p).read()
old = '#ifdef DINGHY_HOST\n#include "../../tests/host.c"'
new = ('#if defined(QUORUM_ANDROID)\n#include "android_platform.c"                 /* Quorum for Android: engine/android/android_platform.c */\n'
       '#elif defined(DINGHY_HOST)\n#include "../../tests/host.c"')
assert old in s, 'platform.c: the DINGHY_HOST switch has moved; update this script'
open(p, 'w').write(s.replace(old, new, 1))
EOF
git -C "$Q" log -1 --format='%H %cd' > QUORUM_COMMIT
echo "engine now at Quorum $(cut -c1-12 QUORUM_COMMIT)"
