# Firmware

Zephyr applications for the measurement engine, plus the out-of-tree board definition
for the target hardware.

Applications live under `apps/`, numbered in the order they were developed. Each one is
standalone and carries its full configuration, so it can be built without reference to
anything outside its own directory.

Build instructions and the pinned toolchain arrive with the board support.
