#!/bin/bash
# Build WAVEBARD for the Pi Sidecar: wavebard.a64.rmp (an RMPA package, AArch64), and the tests.
# Needs aarch64-linux-gnu-gcc / g++ and python3; the tests also qemu-user-static (they run as AArch64, as the Pi does).
#   bash build.sh         the package
#   bash build.sh test    also tests/wav/*.wav and the stress tests
set -e
cd "$(dirname "$0")"
K=third_party/kastle2/code/src
SRCS="wavebard.cc $K/apps/WaveBard/AppWaveBard.cpp $K/common/controls/FancyPot.cpp $K/common/core/midi/Message.cpp
  $K/common/dsp/control/AdsrEnv.cpp $K/common/dsp/control/EnvelopeFollower.cpp $K/common/dsp/effects/HardClipper.cpp
  $K/common/dsp/effects/SoftClipper.cpp $K/common/dsp/filters/DjFilterStereo.cpp $K/common/dsp/filters/SvfStereo.cpp $K/common/dsp/filters/Svf.cpp
  $K/common/dsp/synthesis/Oscillator.cpp $K/common/dsp/utility/Quantizer.cpp"
INC="-Ishim -I$K -I$K/apps/WaveBard -Isdk -Isdk/rmp -I."
A64="aarch64-linux-gnu-"
FLAGS="-std=gnu++23 -mcpu=cortex-a72 -O2 -fno-pic -fno-pie -mcmodel=small -ffixed-x18 -mno-outline-atomics
  -ffunction-sections -fdata-sections -fno-common -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables
  -fno-math-errno -fno-stack-protector -fno-tree-loop-distribute-patterns -fno-rtti -fno-threadsafe-statics
  -fno-use-cxa-atexit -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -w $INC"
B=build/a64
rm -rf "$B"; mkdir -p "$B"
for f in $SRCS; do ${A64}g++ $FLAGS -c "$f" -o "$B/$(basename "$f").o"; done
${A64}gcc -mcpu=cortex-a72 -O2 -fno-pic -fno-pie -mcmodel=small -ffixed-x18 -mno-outline-atomics -ffunction-sections \
  -fdata-sections -fno-stack-protector -ffreestanding -fno-tree-loop-distribute-patterns -c sdk/rmp/shim/rmp_libc.c \
  -o $B/rmp_libc.o
${A64}gcc -mcpu=cortex-a72 -O2 -fno-pic -fno-pie -mcmodel=small -ffixed-x18 -mno-outline-atomics -ffunction-sections \
  -fdata-sections -fno-stack-protector -ffreestanding -fno-builtin -c mathshim.c -o $B/mathshim.o
${A64}g++ $FLAGS -c sdk/rmp/shim/rmp_cxx.cpp -o $B/rmp_cxx.o
${A64}g++ $FLAGS -static -nostdlib -nostartfiles -Wl,--emit-relocs -Wl,--gc-sections -Wl,-z,max-page-size=4096 \
  -Wl,--no-warn-rwx-segments -Wl,-T,sdk/rmp/plugin_a64.ld -o build/wavebard.elf $B/*.o -lgcc
python3 tools/rmp_pack.py build/wavebard.elf manifest.json wavebard.a64.rmp
echo "Built wavebard.a64.rmp"

if [ "$1" = test ]; then
  H=build/arm
  rm -rf "$H"; mkdir -p "$H"
  for f in $SRCS tests/host.cc; do ${A64}g++ -std=gnu++23 -O2 -DRMP_NATIVE -w $INC -c "$f" -o "$H/$(basename "$f").o"; done
  ${A64}g++ -static $H/*.o -o $H/host
  (cd tests && qemu-aarch64-static ../$H/host)
fi
