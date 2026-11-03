/* SPDX-License-Identifier: GPL-3.0-only */
/* The engine as one unit, as the firmware is built: Quorum's dinghy.c (which pulls in android_platform.c through
 * platform.c), then android_tail.c, which reads what the phone needs from inside it, and android_touch.c, the finger. */
#include "dinghy.c"
#include "android_tail.c"
#include "android_touch.c"
