# WAVESURFER

Bastl Instruments' **Kastle 2 Wave Bard** sample player as a user machine for
[Sidecar](https://github.com/trigcondition/sidecar-firmware) (the Analog Rytm expander): a Pi `.rmp` machine, stereo
out, playing the project's sample pool. Made for vocals and chops as much as for drums.

Not made or endorsed by Bastl Instruments.

- Id `wavesurfer`, version 1.0.0, plugin API 3 (a machine), 48 kHz
- Board: Pi (built for the Pi 4 and 5; tested on a Pi 5)
- Licence: MIT

## Install

Copy `wavesurfer.a64.rmp` into `/Plugins` on the Sidecar's SD card and restart, put WAVESURFER on a user slot (U1..U5)
on the USER SLOTS page, and pick that slot in a track's machine list (that gives the track a WAVESURFER sound: save the
kit). Samples come from the project's SAMPLES pool, bank A (files under `/Samples`).

## Knobs

| P | Knob | Short | What it does |
|---|------|-------|--------------|
| 1 | Sample | SMPL | the sample in the bank (Wave Bard's Sample) |
| 2 | Pitch | PTCH | free pitch, 1/4x..4x, centre 1x |
| 3 | Length | LEN | right of centre forward, longer to the right; left of centre backwards |
| 4 | Bank | BANK | 1..16 |
| 5 | FX | FX | left: delay synced to the Rytm's tempo; right: chorus; harder: drive |
| 6 | Filter | FILT | Wave Bard's DJ filter: left low-pass, right high-pass |
| 7 | Scale | SCAL | the quantizer's scale (Wave Bard's seven) |
| 8 | Root | ROOT | the quantizer's root |
| 9 | Fine | FINE | +-1 semitone |
| 10 | Note Track | TRCK | how the trig's note moves the pitch: right +1 V/oct (start), centre none, left inverted |
| 11 | Vel>Sample | VSMP | the trig's velocity picks a later sample |
| 12 | Vel>Level | VLEV | the trig's velocity sets the level (0: every hit the same) |
| 13 | First Slot | FRST | the pool row of bank 1, sample 1 |
| 14 | Bank Size | SIZE | samples a bank, 1..16: bank b, sample n is pool row FRST + (b-1) x SIZE + (n-1) |
| 15 | Tune | TUNE | +-24 semitones on the trig's note |
| 16 | Start | STRT | where in the sample a trig starts: 128 steps over the sample (an 8-bar loop: 16ths) |

Lock STRT per step to chop a vocal phrase. Keep PTCH centred and transpose with TUNE: a note jump of 6 semitones or
more turns Wave Bard's quantizer on (its design), and PTCH then moves in octaves. Trigs closer than 20 ms are ignored,
and a trig is ignored while a sample plays backwards (Wave Bard's). The output is Wave Bard's +3 dB, with a soft knee
above 0.8.

## How it runs

Wave Bard's own code (`AppWaveBard`: Init, AudioLoop, its second core's FX, UiLoop, its parameter maps) and the Kastle 2
DSP library run unchanged at the Kastle's 44 kHz in its 48-sample blocks, then 12/11 up to 48 kHz. Its sample file is a
virtual one whose samples are pool rows, read with the plugin API's `pool_read`; the Sidecar keeps one pool sample
ready per voice, so WAVESURFER asks only for the one it plays next and a trig waits (13 ms at most) until it is ready.
A trig is the Kastle's trigger with its note on the pitch CV (1 V/oct) and its velocity on the sample CV. The Kastle's
platform is global, and the Pi renders two cores of voices at once: Wave Bard runs under one lock, each voice with its
own copy of it.

## Build

`bash build.sh` (aarch64-linux-gnu-gcc / g++ 13.3.0, python3) makes `wavesurfer.a64.rmp`. `bash build.sh test` also
makes the test kit's renders in `tests/wav` (run `python3 make_kit.py` and `python3 make_phrase.py` in `tests` once
first; needs qemu-user-static): a melody, a beat from sample locks, pitch, length and reverse, the FX and filter, the
quantizer, velocity, vocal phrases and chops, a gain check and a stress test (two voices, random trigs and knobs).
