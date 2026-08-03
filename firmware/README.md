# Firmware

Zephyr applications for the measurement engine, plus the out-of-tree board definition
for the Electrosmith Daisy Seed.

    boards/              out-of-tree Zephyr board definition (Daisy Seed, STM32H750)
    apps/                measurement applications, numbered in development order
    BUILDING.md          toolchain, workspace setup, build and flash
    toolchain-pins.txt   exact upstream revisions this was validated against

Each application is standalone and carries all of its own configuration, so it can be
built without reference to anything outside its own directory. See
[BUILDING.md](BUILDING.md) for the toolchain and the build invocation.

The board definition carries one thing worth knowing before you use it: a non-cacheable
SRAM region. This part has a data cache, and both DMA (Direct Memory Access) buffers and
debug output need memory that the cache is not holding stale copies of. Rather than
disabling the cache and paying for it everywhere, the board marks `sram1` non-cacheable
and the buffers live there. BUILDING.md explains the mechanism and the devicetree alias
that connects it to the debug transport.
