<p align="center">
  <img src="docs/zztop_logo.png" alt="zZtop" width="420">
</p>

# zZtop - Zephyr Impedance Testing Open Platform

An open-source electrochemical impedance spectroscopy (EIS) and frequency-response
analysis instrument, built from the ground up toward professional-quality, low-cost,
sovereign lab instrumentation.

> **Experimental.** Active, early-stage R&D. Expect churn and rebuilds. Work is tracked
> here at edgecollective while it develops; a curated multi-repo ecosystem comes later,
> once the R&D settles.

## What this is

zZtop grows an impedance-measurement instrument in small, cross-validated steps. The
digital measurement engine runs on an Electrosmith Daisy Seed (STM32H750) under Zephyr:
coherent DMA waveform generation feeding a streaming software lock-in, producing swept
Nyquist spectra on test networks.

The analog side starts with a minimal two-probe front-end, whose job is to keep the
output impedance of the Daisy Seed's PCM3060 audio codec, and the coupling capacitors
on its outputs, out of the measurement. It goes back to basics: the classic adder
potentiostat (Bard and Faulkner, Electrochemical Methods, Ch. 15), which pairs a
potential-control amplifier, a reference voltage-follower, and a current-follower
transimpedance stage.

That design picks up the lineage of our earlier
[olm-pstat](https://github.com/p-v-o-s/olm-pstat) (presented at Open Hardware Summit
2013, with a schematic contribution from Jack Summers) and the later four-probe work,
simplified to two electrodes first, before we earn the four-probe version.

## Layout

    firmware/     Zephyr applications and the out-of-tree board definition
    host/         Python tooling that drives the instrument and analyses its output
    hardware/     circuit designs and simulations
    docs/         project documentation
    data/         captured measurement data

See [CONTRIBUTING.md](CONTRIBUTING.md) for conventions.

## Approach

Circuits are defined in code with [tscircuit](https://tscircuit.com), so the schematic,
the netlist and the simulation come from one source and the design history is the git
log. KiCad and ngspice handle rigorous capture and validation.

First-stage prototypes use through-hole DIP parts, which keeps the research phase quick
and reworkable. Later development moves to modern surface-mount parts.

## Building

Each area builds on its own. For the circuit designs:

    cd hardware/two-probe-frontend
    npm install
    npx tsci build      # compile, validate, run the simulation
    npx tsci dev        # interactive schematic and PCB preview

Firmware build instructions arrive with the board support.

## License

Open source; license to be finalized. Hardware and software will be released openly.

---

*Development on this repository is assisted by Anthropic's Claude Code agentic platform,
under Craig Versek's direction and review.*
