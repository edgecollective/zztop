# Working in this repository

## Layout

    firmware/     Zephyr applications and the out-of-tree board definition
    host/         Python tooling that drives the instrument and analyses its output
    hardware/     circuit designs and simulations
    docs/         project documentation and images
    data/         captured measurement data

Each application under `firmware/apps/` is self-contained and carries all of its own
configuration. Nothing is passed on the build command line, because a flag typed once
at a terminal is not a record of how something was built.

Circuit designs under `hardware/` are defined in code with tscircuit, so the schematic,
the netlist and the simulation all come from a single source and every change shows up
as a readable diff.

## Naming

Directories are named for what they contain, in the order the work happened:
`01-coherent-dma`, `02-dac-dma-tone`, and so on. Sequential prefixes let a reader follow
the development without a table of contents.

## What belongs here

This repository is the curated record, not the working notebook. Exploration, dead ends
and half-finished ideas live elsewhere; what lands here is the part that has been
validated and is worth someone else's time. Practically, that means:

- Sources, not generated artifacts. Simulator netlists yes, simulator output no.
- Data that would take real effort to reproduce, with enough header context to be
  interpreted years from now.
- Third-party documentation gets cited, never republished.

Write for a reader who has no other context: no internal shorthand, no references to
private notes, no vocabulary that only makes sense to whoever was in the room.

## Prose

Plain ASCII. Hyphens rather than typographic dashes, straight quotes, no decorative
Unicode. Format only where it earns its keep.
