# Applications

Numbered in the order they were developed. Each one is standalone: it carries its own
`prj.conf`, and its own `Kconfig` where a promptless symbol needs selecting. Nothing
here depends on configuration passed at build time.

    01-coherent-dma     non-cacheable memory for coherent buffers with the cache on
    02-dac-dma-tone     timer-triggered DMA to the DAC: the first generated waveform

They build in sequence in the sense that each relies on what the previous one
established, but any of them can be built and flashed on its own. The header comment in
each `src/main.c` explains what that application demonstrates and why it exists.

See [../BUILDING.md](../BUILDING.md) for the toolchain and the build invocation.
