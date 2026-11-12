# Quorum for Android

[Quorum](https://github.com/zeptosecond-dsp/Quorum), the cellular additive synthesiser for the M-Vave FM-1, as an
Android app. The engine is Quorum's own firmware, unchanged; around it, a phosphor-screen interface played with your
finger as a Fairlight-style lightpen.

## Status

The firmware runs on the phone with its sound and its own screen, played by touching the screen (the FM-1's knobs and
buttons are gone; each page's footer is a row of command words): the header, the Index,
every page's K1-K4 values (slide bar or tap-to-step), pins in the mod and gate matrices, the Modulators page (tabs; FGs dragged by their peak and end; four drawable
breakpoint envelopes), drag-scrolling for the Disk, the matrices and the piano roll,
and Global: VDU with the scanline settings. New pages are drawn in line by line, as on the FM-1. Patches are named
with the phone's own keyboard. The phone has its own tunings table (`engine/android/gen_tunings.py`): Quorum's, with
Wendy Carlos's Superjust, the 22-EDO temperaments' scales and scales for the tunings that had none.

## The rack

The app is a rack face in landscape, the same as [Arcturus S6 for Android](https://github.com/zeptosecond-dsp/Arcturus-Android)'s
but in brushed aluminium: the tube at the left (amber, until another phosphor is chosen on Global: VDU), as tall as the
screen in a thin black bezel; two 5.25" drives standing on end, their lights green (the first one's flickers while the
disk works); and two knobs: **BRIGHTNESS** (the tube's, the same as Global: VDU's) and **MASTER LEVEL** (the output's
VCA). Drag a knob up or to the right to turn it up.

From the tube to the right handle run two rows of 32 green lights, early-'80s gallium phosphide lamps: **GRID A**'s row
now along the top, **GRID B**'s along the bottom. Touch a light to turn its cell over, or swipe along the row.

There are no keys or strike plates on the rack for now: everything else is done on the tube. VINTAGE MODE is switched
on its own line on Global: VINTAGE.

The plate is a picture, `app/src/main/res/drawable-nodpi/rack_bg.jpg`: replace that file to texture it. It is cut to
the screen's shape, never stretched.

## Installing a build

Every push to `main` builds the app. Open the repo's **Releases** on your phone, pick **dev-build**, tap
`quorum-dev.apk` and install it (Android asks once to allow installs from your browser). Each build installs over the
last and keeps your patches.

## How it fits together

| | |
| --- | --- |
| `engine/firmware/` | Quorum's firmware, forked from the Quorum repo at the commit in `engine/QUORUM_COMMIT`. The FM-1 version stays as it is; changes here are Android's own, each inside `#ifdef QUORUM_ANDROID` so the original is always readable beside them. |
| `engine/android/android_platform.c` | The FM-1 as the firmware sees it, on a phone: the firmware's main loop on its own thread, real time, the screen, storage, the panel, MIDI in, and the sound rendered a block at a time into a ring for Oboe. |
| `engine/android/android_ui.h`, `android_touch.c` | The finger as a lightpen: pages say where their touchable things are as they draw; taps open pages, step switches, or bring up the slide bar for a value. |
| `engine/android/android_tail.c` | Read after the firmware: the screen's light before the tube, so the phone can draw the tube itself on the GPU (`CrtView.kt`) at full resolution, with fine scanlines. |
| `engine/tests/desktop_check.sh` | Builds the same thing on a computer and plays it for a few seconds (a WAV and a screen out): a check that runs on every push. |
| `app/` | The Android app: `RackView.kt` (the rack face), `CrtView.kt` (the tube), and `quorum_jni.cpp` with Oboe for the audio. |

## Licence

GPL-3.0-only, as Quorum and Dinghy (see `LICENSE` and `LICENSING.md`). Oboe is Apache-2.0. The floppy drive's sounds are
by [ullvieib](https://freesound.org/people/ullvieib/) on Freesound, CC BY 4.0 (`engine/android/sounds/`).
