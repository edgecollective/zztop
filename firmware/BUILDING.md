# Building the firmware

Everything human-authored lives in this repository: the applications under `apps/` and
the out-of-tree board definition under `boards/`. Everything upstream is pinned exactly
in `toolchain-pins.txt`. Nothing depends on paths outside the checkout.

## Toolchain

    Zephyr        v4.4.1 (1f6485eca254); module revisions in toolchain-pins.txt
    Zephyr SDK    1.0.1, arm-zephyr-eabi toolchain only
    west          1.5.0
    Python        3.13 (Zephyr 4.4 requires 3.12 or newer for the CMake build)

Hardware: Electrosmith Daisy Seed Rev7 (STM32H750IBK6), with a SEGGER J-Link debug probe
on the Cortex 2x5 SWD (Serial Wire Debug) header.

`west` is Zephyr's meta-tool. It manages the workspace of upstream repositories, and
wraps the CMake build and the flash step. See the
[west documentation](https://docs.zephyrproject.org/latest/develop/west/index.html) for
what it can do beyond the two commands used here.

## Recreate the workspace

Once per machine:

    python3.13 -m venv ~/zephyrproject/.venv
    source ~/zephyrproject/.venv/bin/activate
    pip install west==1.5.0
    cd ~/zephyrproject && west init . && git -C zephyr checkout v4.4.1
    west update && west zephyr-export
    pip install -r zephyr/scripts/requirements.txt
    west sdk install -t arm-zephyr-eabi

Then check the module revisions against `toolchain-pins.txt`. Zephyr pulls a lot of
modules and a drifting one changes behaviour without changing your code.

## How a Zephyr build is configured

Zephyr has two separate configuration systems, and conflating them is the most common
way to lose an afternoon.

**Devicetree** describes the hardware: which peripherals exist, at which addresses, wired
to which pins, and how memory is laid out. It comes from `.dts` and `.dtsi` files plus
per-application `.overlay` files.
([devicetree guide](https://docs.zephyrproject.org/latest/build/dts/index.html))

**Kconfig** selects software: which drivers and subsystems get compiled in, and their
options. It comes from `prj.conf`, an application `Kconfig`, and the board's `defconfig`.
([Kconfig guide](https://docs.zephyrproject.org/latest/build/kconfig/index.html))

The rule of thumb: devicetree says *this board has a converter at this address*; Kconfig
says *compile the converter driver*. Most features need both, and a feature that is
present in one but missing from the other fails in confusing ways -- typically a build
that succeeds and a peripheral that never responds.

## Build an application

Every application is standalone and carries all of its own Kconfig, in `prj.conf` plus an
application `Kconfig` for promptless select-only symbols. Point `BOARD_ROOT` at this
repository's `firmware/` directory so the out-of-tree board is found:

    ZZTOP=/path/to/this/repo
    cd ~/zephyrproject && source .venv/bin/activate
    west build -b daisy_seed -s "$ZZTOP/firmware/apps/<app>" -d /tmp/build-<app> \
      -S rtt-console -p always -- -DBOARD_ROOT="$ZZTOP/firmware"

`BOARD_ROOT` points at the directory *containing* `boards/`, not at `boards/` itself.
This is easy to get wrong and the failure is an unhelpful "board not found".

**No Kconfig on the command line.** A flag typed at a terminal is ephemeral state that
does not replicate; six weeks later nobody can tell how a binary was configured. If an
application needs a symbol, it goes in that application's `prj.conf`.

## Flashing

`west flash -r jlink` works, but its output can end at "Flashing file:" with no error and
no flash actually written. The verifiable path is to drive the J-Link directly and watch
for the acknowledgement:

    printf 'r\nh\nloadfile <build>/zephyr/zephyr.hex\nr\ng\nq\n' | \
      JLinkExe -device STM32H750IB -if SWD -speed 4000 -autoconnect 1

Expect an explicit "O.K." after "Downloading file". Anything else means nothing was
programmed, regardless of how the invocation exited.

## Reading debug output with the data cache enabled

Debug output comes back over SEGGER RTT (Real Time Transfer), which carries printf-style
output through the debug probe by way of a ring buffer in the target's RAM, with no UART
required. ([SEGGER's description](https://kb.segger.com/RTT))

Because the host reads that buffer out of physical memory, it has to live somewhere the
CPU's data cache is not holding a newer copy. The board definition places it in `sram1`,
which this project keeps non-cacheable for exactly this reason.

Find the control block address in the built image:

    <sdk>/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-nm <build>/zephyr/zephyr.elf | grep _SEGGER_RTT

Two traps are worth knowing before you lose an evening to them:

- The stock RTT search heuristics are unreliable here. Read the control block by address
  rather than letting a tool hunt for it.
- A stale ring buffer inherited from a previous image will silently swallow all output.
  Applications in this repository set the RTT initialisation mode to always-initialise to
  prevent it.

## The board definition

`boards/electrosmith/daisy_seed/` is an out-of-tree Zephyr board: it lives in this project
rather than upstream, which is what `BOARD_ROOT` above makes possible. Five files, each
with one job:

    board.yml               declares the board and its SoC to the build system
    Kconfig.daisy_seed      the board's Kconfig entry point
    daisy_seed_defconfig    Kconfig defaults applied whenever this board is selected
    daisy_seed.dts          the devicetree: the hardware description
    board.cmake             flash and debug runner configuration

The [board porting guide](https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html)
covers the general shape. Two things in this particular board are load-bearing for this
project: the non-cacheable `sram1` region that keeps DMA (Direct Memory Access) buffers
and debug output coherent with the cache enabled, and the devicetree alias the RTT control
block is placed through.

That alias is worth understanding as a pattern, because it is how a hardware fact in the
devicetree reaches a software option in Kconfig. The devicetree declares the region and
gives it an alias; a Kconfig option names that alias as the section to place buffers in.
Neither system knows about the other directly. The alias is the contract between them.

The DMA arrangement was cross-checked against Electrosmith's own libDaisy, whose DAC
(digital-to-analog converter) driver uses the same timer trigger and circular stream, and
whose DMA buffer section also resolves to `sram1`. That is independent confirmation that
`sram1` is the DMA buffer RAM on this part, rather than a guess that happened to work.
