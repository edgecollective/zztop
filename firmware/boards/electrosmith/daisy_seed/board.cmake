# SPDX-License-Identifier: Apache-2.0
#
# Daisy Seed flashes over the STM32 system-ROM DFU bootloader (BOOT+RESET ->
# enumerates as 0483:df11). dfu-util writes internal flash at 0x08000000;
# ":leave" exits DFU and starts the app after download.
board_runner_args(dfu-util "--pid=0483:df11" "--alt=0" "--dfuse")

# SWD via J-Link -- deferred (Phase 7), wired here so `west debug`/`west flash
# --runner jlink` work once the SWD header is connected.
board_runner_args(jlink "--device=STM32H750IB" "--speed=4000")

include(${ZEPHYR_BASE}/boards/common/dfu-util.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
