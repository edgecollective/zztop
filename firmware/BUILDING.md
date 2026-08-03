# Building the firmware

Everything human-authored lives in this repository: the applications under `apps/` and
the out-of-tree board definition under `boards/`. Everything upstream is pinned exactly
in `toolchain-pins.txt`. Nothing depends on paths outside the checkout.

## Toolchain

    Zephyr        v4.4.1 (1f6485eca254); module revisions in toolchain-pins.txt
    Zephyr SDK    1.0.1, arm-zephyr-eabi toolchain only
    west          1.5.0
    Python        3.13 (Zephyr 4.4 requires 3.12 or newer for the CMake build)

Hardware: Electrosmith Daisy Seed Rev7 (STM32H750IBK6), with a SEGGER J-Link on the
Cortex 2x5 SWD header.

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
modules and a drifting one will change behaviour without changing your code.

## Build an application

Every application is standalone and carries all of its own Kconfig, in `prj.conf` plus
an application `Kconfig` for promptless select-only symbols. Point `BOARD_ROOT` at this
repository's `firmware/` directory so the out-of-tree board is found:

    ZZTOP=/path/to/this/repo
    cd ~/zephyrproject && source .venv/bin/activate
    west build -b daisy_seed -s "$ZZTOP/firmware/apps/<app>" -d /tmp/build-<app> \
      -S rtt-console -p always -- -DBOARD_ROOT="$ZZTOP/firmware"

**No Kconfig on the command line.** A flag typed at a terminal is ephemeral state that
does not replicate; six weeks later nobody can tell how a binary was configured. If an
application needs a symbol, it goes in that application's `prj.conf`.

## Flashing

`west flash -r jlink` works, but its output can end at "Flashing file:" with no error
and no flash actually written. The verifiable path is to drive the J-Link directly and
watch for the acknowledgement:

    printf 'r\nh\nloadfile <build>/zephyr/zephyr.hex\nr\ng\nq\n' | \
      JLinkExe -device STM32H750IB -if SWD -speed 4000 -autoconnect 1

Expect an explicit "O.K." after "Downloading file". Anything else means nothing was
programmed, regardless of how the invocation exited.

## Reading debug output with the data cache enabled

Debug output comes back over SEGGER RTT. With the data cache on, the RTT control block
has to live in a non-cacheable region or the host reads stale memory. The board
definition places it in `sram1`, which this project keeps non-cacheable for exactly this
reason.

Find the control block address in the built image:

    <sdk>/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-nm <build>/zephyr/zephyr.elf | grep _SEGGER_RTT

Two traps are worth knowing before you lose an evening to them:

- The stock RTT search heuristics are unreliable here. Read the control block by address
  rather than letting a tool hunt for it.
- A stale ring buffer inherited from a previous image will silently swallow all output.
  Applications in this repository set the RTT initialisation mode to always-initialise
  to prevent it.

## The board definition

`boards/electrosmith/daisy_seed/` is an out-of-tree Zephyr board for the Daisy Seed. Two
things in it are load-bearing for this project: the non-cacheable `sram1` region that
makes DMA buffers and RTT coherent with the cache enabled, and the section alias the RTT
control block is placed through.

The DMA arrangement was cross-checked against Electrosmith's own libDaisy, whose DAC
driver uses the same timer trigger and circular stream, and whose DMA buffer section
also resolves to `sram1`. That is independent confirmation that `sram1` is the DMA
buffer RAM on this part, rather than a guess that happened to work.
